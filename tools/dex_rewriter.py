#!/usr/bin/env python3
from __future__ import annotations
import argparse, hashlib, struct, zlib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Optional

NO_INDEX = 0xffffffff

# DEX map item types
TYPE_HEADER_ITEM = 0x0000
TYPE_STRING_ID_ITEM = 0x0001
TYPE_TYPE_ID_ITEM = 0x0002
TYPE_PROTO_ID_ITEM = 0x0003
TYPE_FIELD_ID_ITEM = 0x0004
TYPE_METHOD_ID_ITEM = 0x0005
TYPE_CLASS_DEF_ITEM = 0x0006
TYPE_MAP_LIST = 0x1000
TYPE_TYPE_LIST = 0x1001
TYPE_CLASS_DATA_ITEM = 0x2000
TYPE_CODE_ITEM = 0x2001
TYPE_STRING_DATA_ITEM = 0x2002
TYPE_ENCODED_ARRAY_ITEM = 0x2005

ACC_PUBLIC = 0x1
ACC_PRIVATE = 0x2
ACC_PROTECTED = 0x4
ACC_STATIC = 0x8
ACC_FINAL = 0x10
ACC_NATIVE = 0x100
ACC_ABSTRACT = 0x400
ACC_CONSTRUCTOR = 0x10000


def uleb(data: bytes, off: int) -> tuple[int, int]:
    result = 0
    shift = 0
    while True:
        value = data[off]
        off += 1
        result |= (value & 0x7f) << shift
        if value < 0x80:
            return result, off
        shift += 7
        if shift > 35:
            raise ValueError('ULEB128 too large')


def sleb(data: bytes, off: int) -> tuple[int, int]:
    result = 0
    shift = 0
    while True:
        value = data[off]
        off += 1
        result |= (value & 0x7f) << shift
        shift += 7
        if value < 0x80:
            if value & 0x40:
                result |= -(1 << shift)
            return result, off
        if shift > 35:
            raise ValueError('SLEB128 too large')


def enc_uleb(value: int) -> bytes:
    if value < 0:
        raise ValueError(value)
    out = bytearray()
    while True:
        b = value & 0x7f
        value >>= 7
        if value:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def enc_sleb(value: int) -> bytes:
    out = bytearray()
    more = True
    while more:
        b = value & 0x7f
        value >>= 7
        sign = b & 0x40
        if (value == 0 and not sign) or (value == -1 and sign):
            more = False
        else:
            b |= 0x80
        out.append(b)
    return bytes(out)


def utf16_sort_key(value: str) -> bytes:
    return value.encode('utf-16-be', 'surrogatepass')


def mutf8_encode(value: str) -> bytes:
    # Encode UTF-16 code units in modified UTF-8, as required by DEX.
    units = struct.unpack('>%dH' % (len(value.encode('utf-16-be', 'surrogatepass')) // 2),
                          value.encode('utf-16-be', 'surrogatepass')) if value else ()
    out = bytearray()
    for unit in units:
        if unit == 0:
            out.extend((0xc0, 0x80))
        elif unit <= 0x7f:
            out.append(unit)
        elif unit <= 0x7ff:
            out.extend((0xc0 | (unit >> 6), 0x80 | (unit & 0x3f)))
        else:
            out.extend((0xe0 | (unit >> 12),
                        0x80 | ((unit >> 6) & 0x3f),
                        0x80 | (unit & 0x3f)))
    return bytes(out)


def mutf8_decode(data: bytes, off: int) -> tuple[str, int]:
    utf16_len, p = uleb(data, off)
    units: list[int] = []
    while data[p] != 0:
        b0 = data[p]
        p += 1
        if b0 < 0x80:
            units.append(b0)
        elif (b0 & 0xe0) == 0xc0:
            b1 = data[p]; p += 1
            units.append(((b0 & 0x1f) << 6) | (b1 & 0x3f))
        else:
            b1 = data[p]; b2 = data[p + 1]; p += 2
            units.append(((b0 & 0x0f) << 12) | ((b1 & 0x3f) << 6) | (b2 & 0x3f))
    p += 1
    raw = b''.join(struct.pack('>H', unit) for unit in units)
    value = raw.decode('utf-16-be', 'surrogatepass')
    # Be lenient: malformed debug-only strings are still preserved.
    if len(raw) // 2 != utf16_len:
        pass
    return value, p


def align(buf: bytearray, alignment: int) -> None:
    while len(buf) % alignment:
        buf.append(0)


def shorty_type(desc: str) -> str:
    return 'L' if desc.startswith('L') or desc.startswith('[') else desc[0]


@dataclass(frozen=True, order=True)
class Proto:
    return_type: str
    params: tuple[str, ...]

    @property
    def shorty(self) -> str:
        return shorty_type(self.return_type) + ''.join(shorty_type(x) for x in self.params)


@dataclass(frozen=True, order=True)
class FieldKey:
    owner: str
    name: str
    type: str


@dataclass(frozen=True, order=True)
class MethodKey:
    owner: str
    name: str
    proto: Proto


@dataclass
class EncodedValue:
    kind: int
    value: Any


@dataclass
class CatchHandler:
    pairs: list[tuple[str, int]]
    catch_all: Optional[int]


@dataclass
class TryBlock:
    start_addr: int
    insn_count: int
    handler: CatchHandler


@dataclass
class CodeItem:
    registers_size: int
    ins_size: int
    outs_size: int
    insns: list[int]
    tries: list[TryBlock]
    builder: Optional[Callable[['BuildIndexes', 'DexModel', 'CodeItem'], list[int]]] = None


@dataclass
class EncodedField:
    key: FieldKey
    access_flags: int


@dataclass
class EncodedMethod:
    key: MethodKey
    access_flags: int
    code: Optional[CodeItem]


@dataclass
class ClassDef:
    descriptor: str
    access_flags: int
    superclass: Optional[str]
    interfaces: list[str]
    source_file: Optional[str]
    static_fields: list[EncodedField]
    instance_fields: list[EncodedField]
    direct_methods: list[EncodedMethod]
    virtual_methods: list[EncodedMethod]
    static_values: list[EncodedValue]


@dataclass
class BuildIndexes:
    strings: list[str]
    string_idx: dict[str, int]
    types: list[str]
    type_idx: dict[str, int]
    protos: list[Proto]
    proto_idx: dict[Proto, int]
    fields: list[FieldKey]
    field_idx: dict[FieldKey, int]
    methods: list[MethodKey]
    method_idx: dict[MethodKey, int]


class DexModel:
    def __init__(self, data: bytes):
        self.data = data
        self._u16 = lambda o: struct.unpack_from('<H', data, o)[0]
        self._u32 = lambda o: struct.unpack_from('<I', data, o)[0]
        if data[:8] != b'dex\n035\0':
            raise ValueError('Only DEX 035 is supported')
        self.strings = self._parse_strings()
        self.types = self._parse_types()
        self.protos = self._parse_protos()
        self.fields = self._parse_fields()
        self.methods = self._parse_methods()
        self.classes = self._parse_classes()
        self.new_strings: set[str] = set()
        self.new_types: set[str] = set()
        self.new_protos: set[Proto] = set()
        self.new_fields: set[FieldKey] = set()
        self.new_methods: set[MethodKey] = set()

    def _parse_strings(self) -> list[str]:
        count = self._u32(0x38); off = self._u32(0x3c)
        return [mutf8_decode(self.data, self._u32(off + i * 4))[0] for i in range(count)]

    def _parse_types(self) -> list[str]:
        count = self._u32(0x40); off = self._u32(0x44)
        return [self.strings[self._u32(off + i * 4)] for i in range(count)]

    def _type_list(self, off: int) -> tuple[str, ...]:
        if not off:
            return ()
        size = self._u32(off)
        return tuple(self.types[self._u16(off + 4 + i * 2)] for i in range(size))

    def _parse_protos(self) -> list[Proto]:
        count = self._u32(0x48); off = self._u32(0x4c)
        result = []
        for i in range(count):
            p = off + i * 12
            result.append(Proto(self.types[self._u32(p + 4)], self._type_list(self._u32(p + 8))))
        return result

    def _parse_fields(self) -> list[FieldKey]:
        count = self._u32(0x50); off = self._u32(0x54)
        result = []
        for i in range(count):
            p = off + i * 8
            result.append(FieldKey(self.types[self._u16(p)], self.strings[self._u32(p + 4)],
                                   self.types[self._u16(p + 2)]))
        return result

    def _parse_methods(self) -> list[MethodKey]:
        count = self._u32(0x58); off = self._u32(0x5c)
        result = []
        for i in range(count):
            p = off + i * 8
            result.append(MethodKey(self.types[self._u16(p)], self.strings[self._u32(p + 4)],
                                    self.protos[self._u16(p + 2)]))
        return result

    def _parse_encoded_value(self, off: int) -> tuple[EncodedValue, int]:
        header = self.data[off]; off += 1
        kind = header & 0x1f; arg = header >> 5
        if kind == 0x1e:
            return EncodedValue(kind, None), off
        if kind == 0x1f:
            return EncodedValue(kind, bool(arg)), off
        if kind == 0x1c:
            values, off = self._parse_encoded_array(off)
            return EncodedValue(kind, values), off
        if kind == 0x1d:
            type_index, off = uleb(self.data, off)
            count, off = uleb(self.data, off)
            elements = []
            for _ in range(count):
                name_index, off = uleb(self.data, off)
                value, off = self._parse_encoded_value(off)
                elements.append((self.strings[name_index], value))
            return EncodedValue(kind, (self.types[type_index], elements)), off
        width = arg + 1
        raw = int.from_bytes(self.data[off:off + width], 'little', signed=False)
        off += width
        if kind == 0x17:
            value = self.strings[raw]
        elif kind == 0x18:
            value = self.types[raw]
        elif kind in (0x19, 0x1b):
            value = self.fields[raw]
        elif kind == 0x1a:
            value = self.methods[raw]
        else:
            value = (raw, width)
        return EncodedValue(kind, value), off

    def _parse_encoded_array(self, off: int) -> tuple[list[EncodedValue], int]:
        count, off = uleb(self.data, off)
        values = []
        for _ in range(count):
            value, off = self._parse_encoded_value(off)
            values.append(value)
        return values, off

    def _parse_code(self, off: int) -> CodeItem:
        registers, ins, outs, tries_size = struct.unpack_from('<4H', self.data, off)
        insns_size = self._u32(off + 12)
        insns_off = off + 16
        insns = list(struct.unpack_from('<%dH' % insns_size, self.data, insns_off))
        p = insns_off + insns_size * 2
        if tries_size and (insns_size & 1):
            p += 2
        raw_tries = []
        for _ in range(tries_size):
            start_addr = self._u32(p)
            insn_count = self._u16(p + 4)
            handler_off = self._u16(p + 6)
            raw_tries.append((start_addr, insn_count, handler_off))
            p += 8
        tries: list[TryBlock] = []
        if tries_size:
            handlers_start = p
            count, q = uleb(self.data, handlers_start)
            handler_by_offset: dict[int, CatchHandler] = {}
            for _ in range(count):
                rel = q - handlers_start
                size, q = sleb(self.data, q)
                pairs = []
                for __ in range(abs(size)):
                    type_index, q = uleb(self.data, q)
                    addr, q = uleb(self.data, q)
                    pairs.append((self.types[type_index], addr))
                catch_all = None
                if size <= 0:
                    catch_all, q = uleb(self.data, q)
                handler_by_offset[rel] = CatchHandler(pairs, catch_all)
            for start_addr, insn_count, handler_off in raw_tries:
                tries.append(TryBlock(start_addr, insn_count, handler_by_offset[handler_off]))
        return CodeItem(registers, ins, outs, insns, tries)

    def _parse_class_data(self, off: int):
        sf_count, off = uleb(self.data, off)
        if_count, off = uleb(self.data, off)
        dm_count, off = uleb(self.data, off)
        vm_count, off = uleb(self.data, off)
        static_fields = []
        index = 0
        for _ in range(sf_count):
            diff, off = uleb(self.data, off); access, off = uleb(self.data, off)
            index += diff
            static_fields.append(EncodedField(self.fields[index], access))
        instance_fields = []
        index = 0
        for _ in range(if_count):
            diff, off = uleb(self.data, off); access, off = uleb(self.data, off)
            index += diff
            instance_fields.append(EncodedField(self.fields[index], access))
        direct_methods = []
        index = 0
        for _ in range(dm_count):
            diff, off = uleb(self.data, off); access, off = uleb(self.data, off); code_off, off = uleb(self.data, off)
            index += diff
            direct_methods.append(EncodedMethod(self.methods[index], access,
                                                 self._parse_code(code_off) if code_off else None))
        virtual_methods = []
        index = 0
        for _ in range(vm_count):
            diff, off = uleb(self.data, off); access, off = uleb(self.data, off); code_off, off = uleb(self.data, off)
            index += diff
            virtual_methods.append(EncodedMethod(self.methods[index], access,
                                                  self._parse_code(code_off) if code_off else None))
        return static_fields, instance_fields, direct_methods, virtual_methods

    def _parse_classes(self) -> list[ClassDef]:
        count = self._u32(0x60); off = self._u32(0x64)
        result = []
        for i in range(count):
            p = off + i * 32
            class_idx, access, super_idx, interfaces_off, source_idx, _annotations, class_data_off, static_values_off = \
                struct.unpack_from('<8I', self.data, p)
            if class_data_off:
                sf, inf, dm, vm = self._parse_class_data(class_data_off)
            else:
                sf, inf, dm, vm = [], [], [], []
            static_values = self._parse_encoded_array(static_values_off)[0] if static_values_off else []
            result.append(ClassDef(
                descriptor=self.types[class_idx],
                access_flags=access,
                superclass=None if super_idx == NO_INDEX else self.types[super_idx],
                interfaces=list(self._type_list(interfaces_off)),
                source_file=None if source_idx == NO_INDEX else self.strings[source_idx],
                static_fields=sf,
                instance_fields=inf,
                direct_methods=dm,
                virtual_methods=vm,
                static_values=static_values,
            ))
        return result

    def class_by_desc(self, desc: str) -> ClassDef:
        for cls in self.classes:
            if cls.descriptor == desc:
                return cls
        raise KeyError(desc)

    def method_by_key(self, key: MethodKey) -> EncodedMethod:
        cls = self.class_by_desc(key.owner)
        for method in cls.direct_methods + cls.virtual_methods:
            if method.key == key:
                return method
        raise KeyError(key)

    def add_type(self, desc: str) -> None:
        self.new_strings.add(desc)
        self.new_types.add(desc)

    def add_proto(self, proto: Proto) -> None:
        self.add_type(proto.return_type)
        for p in proto.params:
            self.add_type(p)
        self.new_strings.add(proto.shorty)
        self.new_protos.add(proto)

    def add_method_ref(self, method: MethodKey) -> None:
        self.add_type(method.owner)
        self.new_strings.add(method.name)
        self.add_proto(method.proto)
        self.new_methods.add(method)

    def add_field_ref(self, field_key: FieldKey) -> None:
        self.add_type(field_key.owner)
        self.add_type(field_key.type)
        self.new_strings.add(field_key.name)
        self.new_fields.add(field_key)

    def indexes(self) -> BuildIndexes:
        strings = sorted(set(self.strings) | self.new_strings, key=utf16_sort_key)
        string_idx = {v: i for i, v in enumerate(strings)}
        types = sorted(set(self.types) | self.new_types, key=lambda d: string_idx[d])
        type_idx = {v: i for i, v in enumerate(types)}
        protos = sorted(set(self.protos) | self.new_protos,
                        key=lambda p: (type_idx[p.return_type], tuple(type_idx[x] for x in p.params)))
        proto_idx = {v: i for i, v in enumerate(protos)}
        fields = sorted(set(self.fields) | self.new_fields,
                        key=lambda f: (type_idx[f.owner], string_idx[f.name], type_idx[f.type]))
        field_idx = {v: i for i, v in enumerate(fields)}
        methods = sorted(set(self.methods) | self.new_methods,
                         key=lambda m: (type_idx[m.owner], string_idx[m.name], proto_idx[m.proto]))
        method_idx = {v: i for i, v in enumerate(methods)}
        return BuildIndexes(strings, string_idx, types, type_idx, protos, proto_idx,
                            fields, field_idx, methods, method_idx)

    def _remap_insns(self, code: CodeItem, idx: BuildIndexes) -> list[int]:
        words = list(code.insns)
        pc = 0
        n = len(words)
        while pc < n:
            w0 = words[pc]; op = w0 & 0xff; high = w0 >> 8
            if op == 0 and high in (1, 2, 3):
                if high == 1:
                    width = 4 + words[pc + 1] * 2
                elif high == 2:
                    width = 2 + words[pc + 1] * 4
                else:
                    element_width = words[pc + 1]
                    size = words[pc + 2] | (words[pc + 3] << 16)
                    width = 4 + (element_width * size + 1) // 2
                pc += width
                continue
            width = OPCODE_WIDTH.get(op)
            if width is None:
                raise ValueError(f'Unknown opcode 0x{op:02x} at code unit {pc}')
            if op == 0x1a:
                old = words[pc + 1]
                words[pc + 1] = idx.string_idx[self.strings[old]]
            elif op == 0x1b:
                old = words[pc + 1] | (words[pc + 2] << 16)
                new = idx.string_idx[self.strings[old]]
                words[pc + 1] = new & 0xffff; words[pc + 2] = new >> 16
            elif op in (0x1c, 0x1f, 0x20, 0x22, 0x23, 0x24, 0x25):
                old = words[pc + 1]
                words[pc + 1] = idx.type_idx[self.types[old]]
            elif 0x52 <= op <= 0x6d:
                old = words[pc + 1]
                words[pc + 1] = idx.field_idx[self.fields[old]]
            elif (0x6e <= op <= 0x72) or (0x74 <= op <= 0x78):
                old = words[pc + 1]
                words[pc + 1] = idx.method_idx[self.methods[old]]
            pc += width
        return words

    def _encode_value(self, value: EncodedValue, idx: BuildIndexes) -> bytes:
        kind = value.kind
        if kind == 0x1e:
            return bytes([kind])
        if kind == 0x1f:
            return bytes([kind | ((1 if value.value else 0) << 5)])
        if kind == 0x1c:
            return bytes([kind]) + self._encode_array(value.value, idx, with_size=True)
        if kind == 0x1d:
            type_desc, elements = value.value
            out = bytearray([kind])
            out += enc_uleb(idx.type_idx[type_desc])
            out += enc_uleb(len(elements))
            for name, element in elements:
                out += enc_uleb(idx.string_idx[name])
                out += self._encode_value(element, idx)
            return bytes(out)
        if kind == 0x17:
            raw = idx.string_idx[value.value]
        elif kind == 0x18:
            raw = idx.type_idx[value.value]
        elif kind in (0x19, 0x1b):
            raw = idx.field_idx[value.value]
        elif kind == 0x1a:
            raw = idx.method_idx[value.value]
        else:
            raw, old_width = value.value
            # Preserve original width for numeric bit patterns, except index
            # values where the smallest width is safer after reindexing.
            width = old_width
            return bytes([kind | ((width - 1) << 5)]) + raw.to_bytes(width, 'little')
        width = max(1, (raw.bit_length() + 7) // 8)
        return bytes([kind | ((width - 1) << 5)]) + raw.to_bytes(width, 'little')

    def _encode_array(self, values: list[EncodedValue], idx: BuildIndexes, with_size: bool = True) -> bytes:
        out = bytearray()
        if with_size:
            out += enc_uleb(len(values))
        for value in values:
            out += self._encode_value(value, idx)
        return bytes(out)

    def _encode_code(self, code: CodeItem, idx: BuildIndexes) -> bytes:
        words = code.builder(idx, self, code) if code.builder else self._remap_insns(code, idx)
        out = bytearray(struct.pack('<4HII', code.registers_size, code.ins_size,
                                    code.outs_size, len(code.tries), 0, len(words)))
        out += struct.pack('<%dH' % len(words), *words)
        if code.tries and (len(words) & 1):
            out += b'\0\0'
        if code.tries:
            # Keep unique handlers in first-use order.
            handlers: list[CatchHandler] = []
            for tb in code.tries:
                if not any(tb.handler is existing for existing in handlers):
                    handlers.append(tb.handler)
            encoded_handlers = bytearray(enc_uleb(len(handlers)))
            offsets: dict[int, int] = {}
            for handler in handlers:
                offsets[id(handler)] = len(encoded_handlers)
                size = len(handler.pairs)
                encoded_handlers += enc_sleb(-size if handler.catch_all is not None else size)
                for type_desc, addr in handler.pairs:
                    encoded_handlers += enc_uleb(idx.type_idx[type_desc])
                    encoded_handlers += enc_uleb(addr)
                if handler.catch_all is not None:
                    encoded_handlers += enc_uleb(handler.catch_all)
            for tb in code.tries:
                handler_off = offsets[id(tb.handler)]
                if handler_off > 0xffff:
                    raise ValueError('Handler offset does not fit in u16')
                out += struct.pack('<IHH', tb.start_addr, tb.insn_count, handler_off)
            out += encoded_handlers
        return bytes(out)

    def build(self) -> bytes:
        idx = self.indexes()
        if len(idx.types) > 0xffff or len(idx.protos) > 0xffff or len(idx.fields) > 0xffff or len(idx.methods) > 0xffff:
            raise ValueError('DEX ID limit exceeded')

        header_size = 0x70
        string_ids_off = header_size
        type_ids_off = string_ids_off + len(idx.strings) * 4
        proto_ids_off = type_ids_off + len(idx.types) * 4
        field_ids_off = proto_ids_off + len(idx.protos) * 12
        method_ids_off = field_ids_off + len(idx.fields) * 8
        class_defs_off = method_ids_off + len(idx.methods) * 8
        class_defs = sorted(self.classes, key=lambda c: idx.type_idx[c.descriptor])
        data_off = class_defs_off + len(class_defs) * 32
        data_off = (data_off + 3) & ~3

        data = bytearray()
        # String data items
        string_data_offsets: dict[str, int] = {}
        string_data_first = data_off + len(data)
        for value in idx.strings:
            string_data_offsets[value] = data_off + len(data)
            utf16_len = len(value.encode('utf-16-le', 'surrogatepass')) // 2
            data += enc_uleb(utf16_len) + mutf8_encode(value) + b'\0'

        # Type lists, deduplicated. Alignment applies to every type_list item.
        type_list_offsets: dict[tuple[str, ...], int] = {(): 0}
        all_type_lists: list[tuple[str, ...]] = []
        seen: set[tuple[str, ...]] = set()
        for proto in idx.protos:
            if proto.params and proto.params not in seen:
                seen.add(proto.params); all_type_lists.append(proto.params)
        for cls in class_defs:
            tup = tuple(cls.interfaces)
            if tup and tup not in seen:
                seen.add(tup); all_type_lists.append(tup)
        type_list_first = 0
        for values in all_type_lists:
            align(data, 4)
            if not type_list_first:
                type_list_first = data_off + len(data)
            type_list_offsets[values] = data_off + len(data)
            data += struct.pack('<I', len(values))
            for value in values:
                data += struct.pack('<H', idx.type_idx[value])
            if len(values) & 1:
                data += b'\0\0'

        # Encode code items before class_data so code_off values are known.
        method_code_offsets: dict[MethodKey, int] = {}
        code_count = 0
        for cls in class_defs:
            for method in cls.direct_methods + cls.virtual_methods:
                if method.code is None:
                    continue
                align(data, 4)
                method_code_offsets[method.key] = data_off + len(data)
                data += self._encode_code(method.code, idx)
                code_count += 1

        # Class data items
        class_data_offsets: dict[str, int] = {}
        class_data_first = 0
        for cls in class_defs:
            if not (cls.static_fields or cls.instance_fields or cls.direct_methods or cls.virtual_methods):
                class_data_offsets[cls.descriptor] = 0
                continue
            if not class_data_first:
                class_data_first = data_off + len(data)
            class_data_offsets[cls.descriptor] = data_off + len(data)
            sf = sorted(cls.static_fields, key=lambda f: idx.field_idx[f.key])
            inf = sorted(cls.instance_fields, key=lambda f: idx.field_idx[f.key])
            dm = sorted(cls.direct_methods, key=lambda m: idx.method_idx[m.key])
            vm = sorted(cls.virtual_methods, key=lambda m: idx.method_idx[m.key])
            data += enc_uleb(len(sf)) + enc_uleb(len(inf)) + enc_uleb(len(dm)) + enc_uleb(len(vm))
            previous = 0
            for field_item in sf:
                current = idx.field_idx[field_item.key]
                data += enc_uleb(current - previous) + enc_uleb(field_item.access_flags)
                previous = current
            previous = 0
            for field_item in inf:
                current = idx.field_idx[field_item.key]
                data += enc_uleb(current - previous) + enc_uleb(field_item.access_flags)
                previous = current
            previous = 0
            for method in dm:
                current = idx.method_idx[method.key]
                data += enc_uleb(current - previous) + enc_uleb(method.access_flags)
                data += enc_uleb(method_code_offsets.get(method.key, 0))
                previous = current
            previous = 0
            for method in vm:
                current = idx.method_idx[method.key]
                data += enc_uleb(current - previous) + enc_uleb(method.access_flags)
                data += enc_uleb(method_code_offsets.get(method.key, 0))
                previous = current

        # Static encoded arrays
        static_value_offsets: dict[str, int] = {}
        static_value_first = 0
        static_value_count = 0
        for cls in class_defs:
            if not cls.static_values:
                static_value_offsets[cls.descriptor] = 0
                continue
            if not static_value_first:
                static_value_first = data_off + len(data)
            static_value_offsets[cls.descriptor] = data_off + len(data)
            data += self._encode_array(cls.static_values, idx, with_size=True)
            static_value_count += 1

        # Map list at end.
        align(data, 4)
        map_off = data_off + len(data)
        map_entries: list[tuple[int, int, int]] = [
            (TYPE_HEADER_ITEM, 1, 0),
            (TYPE_STRING_ID_ITEM, len(idx.strings), string_ids_off),
            (TYPE_TYPE_ID_ITEM, len(idx.types), type_ids_off),
            (TYPE_PROTO_ID_ITEM, len(idx.protos), proto_ids_off),
            (TYPE_FIELD_ID_ITEM, len(idx.fields), field_ids_off),
            (TYPE_METHOD_ID_ITEM, len(idx.methods), method_ids_off),
            (TYPE_CLASS_DEF_ITEM, len(class_defs), class_defs_off),
        ]
        if all_type_lists:
            map_entries.append((TYPE_TYPE_LIST, len(all_type_lists), type_list_first))
        if code_count:
            first_code = min(method_code_offsets.values())
            map_entries.append((TYPE_CODE_ITEM, code_count, first_code))
        map_entries.append((TYPE_STRING_DATA_ITEM, len(idx.strings), string_data_first))
        if class_data_first:
            map_entries.append((TYPE_CLASS_DATA_ITEM,
                                sum(1 for x in class_data_offsets.values() if x), class_data_first))
        if static_value_count:
            map_entries.append((TYPE_ENCODED_ARRAY_ITEM, static_value_count, static_value_first))
        map_entries.append((TYPE_MAP_LIST, 1, map_off))
        map_entries.sort(key=lambda item: item[2])
        data += struct.pack('<I', len(map_entries))
        for item_type, size, offset in map_entries:
            data += struct.pack('<HHII', item_type, 0, size, offset)

        file_size = data_off + len(data)
        out = bytearray(file_size)
        out[:8] = b'dex\n035\0'
        # signature/checksum filled later
        struct.pack_into('<I', out, 0x20, file_size)
        struct.pack_into('<I', out, 0x24, header_size)
        struct.pack_into('<I', out, 0x28, 0x12345678)
        struct.pack_into('<II', out, 0x2c, 0, 0)
        struct.pack_into('<I', out, 0x34, map_off)
        struct.pack_into('<II', out, 0x38, len(idx.strings), string_ids_off)
        struct.pack_into('<II', out, 0x40, len(idx.types), type_ids_off)
        struct.pack_into('<II', out, 0x48, len(idx.protos), proto_ids_off)
        struct.pack_into('<II', out, 0x50, len(idx.fields), field_ids_off)
        struct.pack_into('<II', out, 0x58, len(idx.methods), method_ids_off)
        struct.pack_into('<II', out, 0x60, len(class_defs), class_defs_off)
        struct.pack_into('<II', out, 0x68, len(data), data_off)

        # ID tables
        for i, value in enumerate(idx.strings):
            struct.pack_into('<I', out, string_ids_off + i * 4, string_data_offsets[value])
        for i, value in enumerate(idx.types):
            struct.pack_into('<I', out, type_ids_off + i * 4, idx.string_idx[value])
        for i, proto in enumerate(idx.protos):
            p = proto_ids_off + i * 12
            struct.pack_into('<III', out, p, idx.string_idx[proto.shorty],
                             idx.type_idx[proto.return_type], type_list_offsets[proto.params])
        for i, field_key in enumerate(idx.fields):
            p = field_ids_off + i * 8
            struct.pack_into('<HHI', out, p, idx.type_idx[field_key.owner],
                             idx.type_idx[field_key.type], idx.string_idx[field_key.name])
        for i, method in enumerate(idx.methods):
            p = method_ids_off + i * 8
            struct.pack_into('<HHI', out, p, idx.type_idx[method.owner],
                             idx.proto_idx[method.proto], idx.string_idx[method.name])
        for i, cls in enumerate(class_defs):
            p = class_defs_off + i * 32
            struct.pack_into('<8I', out, p,
                             idx.type_idx[cls.descriptor], cls.access_flags,
                             NO_INDEX if cls.superclass is None else idx.type_idx[cls.superclass],
                             type_list_offsets[tuple(cls.interfaces)],
                             NO_INDEX if cls.source_file is None else idx.string_idx[cls.source_file],
                             0, class_data_offsets[cls.descriptor], static_value_offsets[cls.descriptor])
        out[data_off:] = data

        signature = hashlib.sha1(out[0x20:]).digest()
        out[0x0c:0x20] = signature
        checksum = zlib.adler32(out[0x0c:]) & 0xffffffff
        struct.pack_into('<I', out, 0x08, checksum)
        return bytes(out)


# Standard DEX 035 instruction widths in 16-bit code units.
OPCODE_WIDTH: dict[int, int] = {
    0x00:1, 0x01:1, 0x02:2, 0x03:3, 0x04:1, 0x05:2, 0x06:3,
    0x07:1, 0x08:2, 0x09:3, 0x0a:1, 0x0b:1, 0x0c:1, 0x0d:1,
    0x0e:1, 0x0f:1, 0x10:1, 0x11:1, 0x12:1, 0x13:2, 0x14:3,
    0x15:2, 0x16:2, 0x17:3, 0x18:5, 0x19:2, 0x1a:2, 0x1b:3,
    0x1c:2, 0x1d:1, 0x1e:1, 0x1f:2, 0x20:2, 0x21:1, 0x22:2,
    0x23:2, 0x24:3, 0x25:3, 0x26:3, 0x27:1, 0x28:1, 0x29:2,
    0x2a:3, 0x2b:3, 0x2c:3,
}
for _op in range(0x2d, 0x32): OPCODE_WIDTH[_op] = 2
for _op in range(0x32, 0x3e): OPCODE_WIDTH[_op] = 2
for _op in range(0x44, 0x52): OPCODE_WIDTH[_op] = 2
for _op in range(0x52, 0x60): OPCODE_WIDTH[_op] = 2
for _op in range(0x60, 0x6e): OPCODE_WIDTH[_op] = 2
for _op in range(0x6e, 0x73): OPCODE_WIDTH[_op] = 3
for _op in range(0x74, 0x79): OPCODE_WIDTH[_op] = 3
for _op in range(0x7b, 0x90): OPCODE_WIDTH[_op] = 1
for _op in range(0x90, 0xb0): OPCODE_WIDTH[_op] = 2
for _op in range(0xb0, 0xd0): OPCODE_WIDTH[_op] = 1
for _op in range(0xd0, 0xd8): OPCODE_WIDTH[_op] = 2
for _op in range(0xd8, 0xe3): OPCODE_WIDTH[_op] = 2


def validate_dex(data: bytes) -> None:
    if data[:8] != b'dex\n035\0': raise ValueError('bad magic')
    if struct.unpack_from('<I', data, 0x20)[0] != len(data): raise ValueError('bad file size')
    if hashlib.sha1(data[0x20:]).digest() != data[0x0c:0x20]: raise ValueError('bad SHA-1')
    if zlib.adler32(data[0x0c:]) & 0xffffffff != struct.unpack_from('<I', data, 8)[0]:
        raise ValueError('bad Adler-32')
    # Reparse as a structural check.
    model = DexModel(data)
    if model.strings != sorted(model.strings, key=utf16_sort_key): raise ValueError('unsorted strings')
    type_string_indexes = [model.strings.index(x) for x in model.types]
    if type_string_indexes != sorted(type_string_indexes): raise ValueError('unsorted types')
    # Force decoding of every method and static array via constructor above.


def patch_for_gallery(input_path: Path, output_path: Path) -> None:
    model = DexModel(input_path.read_bytes())
    main_desc = 'Lde/offlineflipnote/legacy/MainActivity;'
    view_desc = 'Landroid/view/View;'
    motion_desc = 'Landroid/view/MotionEvent;'
    listener_desc = 'Landroid/view/View$OnTouchListener;'
    system_desc = 'Ljava/lang/System;'

    main = model.class_by_desc(main_desc)
    if listener_desc not in main.interfaces:
        main.interfaces.append(listener_desc)
        model.add_type(listener_desc)
    model.add_type(motion_desc)

    gallery_tick = MethodKey(main_desc, 'galleryTick', Proto('V', ()))
    on_touch = MethodKey(main_desc, 'onTouch', Proto('Z', (view_desc, motion_desc)))
    load_library = MethodKey(system_desc, 'loadLibrary', Proto('V', ('Ljava/lang/String;',)))
    clinit = MethodKey(main_desc, '<clinit>', Proto('V', ()))
    model.new_strings.add('flipbrowser')
    for method in (gallery_tick, on_touch, load_library, clinit):
        model.add_method_ref(method)

    # Add native listener/ticker methods if absent.
    existing = {m.key for m in main.direct_methods + main.virtual_methods}
    if gallery_tick not in existing:
        main.virtual_methods.append(EncodedMethod(gallery_tick, ACC_PUBLIC | ACC_FINAL | ACC_NATIVE, None))
    if on_touch not in existing:
        main.virtual_methods.append(EncodedMethod(on_touch, ACC_PUBLIC | ACC_FINAL | ACC_NATIVE, None))

    # Add a class initializer which loads libflipbrowser.so.
    if clinit not in existing:
        code = CodeItem(1, 0, 1, [], [])
        def build_clinit(idx: BuildIndexes, _model: DexModel, _code: CodeItem) -> list[int]:
            string_index = idx.string_idx['flipbrowser']
            method_index = idx.method_idx[load_library]
            if string_index > 0xffff:
                raise ValueError('const-string/jumbo not implemented for clinit')
            return [0x001a, string_index, 0x1071, method_index, 0x0000, 0x000e]
        code.builder = build_clinit
        main.direct_methods.append(EncodedMethod(clinit,
                                                  ACC_STATIC | ACC_CONSTRUCTOR,
                                                  code))

    # The ticker enters MainActivity through the synthetic accessor every 16 ms.
    accessor = MethodKey(main_desc, 'access$000', Proto('V', (main_desc,)))
    accessor_method = model.method_by_key(accessor)
    old_code = accessor_method.code
    if old_code is None:
        raise ValueError('access$000 has no code')
    def build_accessor(idx: BuildIndexes, dex: DexModel, code: CodeItem) -> list[int]:
        words = dex._remap_insns(code, idx)
        if not words or words[-1] != 0x000e:
            raise ValueError('unexpected access$000 body')
        # p0 is v0 because registers_size == ins_size == 1.
        invoke = [0x106e, idx.method_idx[gallery_tick], 0x0000]
        return words[:-1] + invoke + [0x000e]
    old_code.builder = build_accessor
    old_code.outs_size = max(old_code.outs_size, 1)

    built = model.build()
    validate_dex(built)
    output_path.write_bytes(built)
    print(output_path)
    print('old:', len(model.data), hashlib.sha256(model.data).hexdigest())
    print('new:', len(built), hashlib.sha256(built).hexdigest())


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--roundtrip', action='store_true')
    args = parser.parse_args()
    model = DexModel(args.input.read_bytes())
    if args.roundtrip:
        built = model.build()
        validate_dex(built)
        args.output.write_bytes(built)
        print('roundtrip', len(model.data), '->', len(built), hashlib.sha256(built).hexdigest())
    else:
        patch_for_gallery(args.input, args.output)

if __name__ == '__main__':
    main()
