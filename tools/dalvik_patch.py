#!/usr/bin/env python3
"""Small Dalvik bytecode editing helpers for the DEX patch scripts.

insert_words() inserts new code units in front of an instruction and repairs
every relative branch, switch payload entry, fill-array-data reference and
try/catch range that spans the insertion point, so a method can be fixed in
place without disturbing the rest of its body.

Only what the Flipnote patches need is implemented, but all 035 opcode
formats are decoded, so the helpers are safe to run over any method.
"""
from __future__ import annotations
from dex_rewriter import OPCODE_WIDTH, CodeItem, TryBlock, CatchHandler

BRANCH_10T = {0x28}                              # goto      +AA
BRANCH_20T = {0x29}                              # goto/16   +AAAA
BRANCH_30T = {0x2a}                              # goto/32   +AAAAAAAA
BRANCH_21T = set(range(0x38, 0x3e))              # if-testz  +BBBB
BRANCH_22T = set(range(0x32, 0x38))              # if-test   +CCCC
BRANCH_31T = {0x26, 0x2b, 0x2c}                  # fill-array-data / switches +BBBBBBBB


def s8(v): return v - 0x100 if v & 0x80 else v
def s16(v): return v - 0x10000 if v & 0x8000 else v
def s32(v): return v - 0x100000000 if v & 0x80000000 else v


def instruction_bounds(words):
    """Yields (pc, width, kind) for every instruction and payload."""
    pc = 0
    n = len(words)
    while pc < n:
        w0 = words[pc]
        op = w0 & 0xff
        high = w0 >> 8
        if op == 0 and high in (1, 2, 3):
            if high == 1:
                width = 4 + words[pc + 1] * 2
            elif high == 2:
                width = 2 + words[pc + 1] * 4
            else:
                element_width = words[pc + 1]
                size = words[pc + 2] | (words[pc + 3] << 16)
                width = 4 + (element_width * size + 1) // 2
            yield pc, width, 'payload%d' % high
            pc += width
            continue
        width = OPCODE_WIDTH.get(op)
        if width is None:
            raise ValueError('unknown opcode 0x%02x at %d' % (op, pc))
        yield pc, width, 'insn'
        pc += width


def encode_12x(op, a, b):
    return [op | (a << 8) | (b << 12)]


def insert_words(code: CodeItem, words: list[int], at_pc: int, new_words: list[int]) -> list[int]:
    """Returns a copy of `words` with `new_words` inserted at `at_pc` and all
    offsets repaired. `code.tries` is updated in place to the new layout."""
    if at_pc < 0 or at_pc > len(words):
        raise ValueError('insertion point outside method')
    bounds = list(instruction_bounds(words))
    if at_pc != len(words) and all(pc != at_pc for pc, _w, _k in bounds):
        raise ValueError('insertion point is not an instruction boundary')
    delta = len(new_words)

    def shift(target):
        # Instruction addresses at or after the insertion point move by delta.
        return target + delta if target >= at_pc else target

    out = list(words)
    # Repair branch and payload references first (on the old layout), then
    # splice. Each offset is relative to its own instruction address.
    for pc, width, kind in bounds:
        if kind != 'insn':
            continue
        op = out[pc] & 0xff
        if op in BRANCH_10T:
            target = pc + s8(out[pc] >> 8)
            new_off = shift(target) - shift(pc)
            if not -128 <= new_off <= 127:
                raise ValueError('goto offset overflow at %d' % pc)
            out[pc] = op | ((new_off & 0xff) << 8)
        elif op in BRANCH_20T or op in BRANCH_21T:
            target = pc + s16(out[pc + 1])
            new_off = shift(target) - shift(pc)
            if not -32768 <= new_off <= 32767:
                raise ValueError('16-bit branch overflow at %d' % pc)
            out[pc + 1] = new_off & 0xffff
        elif op in BRANCH_22T:
            target = pc + s16(out[pc + 1])
            new_off = shift(target) - shift(pc)
            if not -32768 <= new_off <= 32767:
                raise ValueError('16-bit branch overflow at %d' % pc)
            out[pc + 1] = new_off & 0xffff
        elif op in BRANCH_30T or op in BRANCH_31T:
            target = pc + s32(out[pc + 1] | (out[pc + 2] << 16))
            new_off = (shift(target) - shift(pc)) & 0xffffffff
            out[pc + 1] = new_off & 0xffff
            out[pc + 2] = new_off >> 16
            if op in (0x2b, 0x2c):
                # Switch payload targets are relative to the switch instruction.
                ppc = target
                high = out[ppc] >> 8
                if high == 1:
                    size = out[ppc + 1]
                    first = 4
                    stride = 2
                elif high == 2:
                    size = out[ppc + 1]
                    first = 2 + size * 2
                    stride = 2
                else:
                    raise ValueError('switch does not point at a payload')
                for i in range(size):
                    p = ppc + first + i * stride
                    rel = s32(out[p] | (out[p + 1] << 16))
                    new_rel = (shift(pc + rel) - shift(pc)) & 0xffffffff
                    out[p] = new_rel & 0xffff
                    out[p + 1] = new_rel >> 16
    out[at_pc:at_pc] = list(new_words)

    new_tries = []
    for tb in code.tries:
        start = tb.start_addr
        end = tb.start_addr + tb.insn_count
        new_start = shift(start)
        new_end = end + delta if end > at_pc else end
        handler = CatchHandler([(t, shift(a)) for t, a in tb.handler.pairs],
                               shift(tb.handler.catch_all) if tb.handler.catch_all is not None else None)
        new_tries.append(TryBlock(new_start, new_end - new_start, handler))
    # Keep shared handler identity for identical handlers (encoder dedups by id).
    merged = {}
    for tb in new_tries:
        key = (tuple(tb.handler.pairs), tb.handler.catch_all)
        tb.handler = merged.setdefault(key, tb.handler)
    code.tries[:] = new_tries
    return out


def append_words(words: list[int], new_words: list[int]) -> tuple[list[int], int]:
    """Appends code at the end (no offsets change). Returns (words, start_pc)."""
    start = len(words)
    return list(words) + list(new_words), start


def normalized(words: list[int]):
    """Instruction stream with every relative branch replaced by the ordinal
    of its target instruction, so two layouts can be compared structurally."""
    bounds = list(instruction_bounds(words))
    ordinal = {pc: i for i, (pc, _w, _k) in enumerate(bounds)}
    out = []
    for pc, width, kind in bounds:
        w = list(words[pc:pc + width])
        if kind != 'insn':
            out.append((kind, tuple(w)))
            continue
        op = w[0] & 0xff
        if op in BRANCH_10T:
            out.append(('b', op, ordinal[pc + s8(w[0] >> 8)]))
        elif op in BRANCH_20T:
            out.append(('b', op, ordinal[pc + s16(w[1])]))
        elif op in BRANCH_21T or op in BRANCH_22T:
            out.append(('b', op, w[0] >> 8, ordinal[pc + s16(w[1])]))
        elif op in BRANCH_30T or op in BRANCH_31T:
            out.append(('b', op, w[0] >> 8, ordinal[pc + s32(w[1] | (w[2] << 16))]))
        else:
            out.append(('i', tuple(w)))
    return out
