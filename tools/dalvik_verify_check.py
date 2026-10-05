#!/usr/bin/env python3
"""Dalvik-verifier compatibility check for narrowing primitive stores.

Dalvik (Android 2.3-4.4) rejects a method whose aput-short/byte/char/boolean,
narrow iput/sput, narrow argument or narrow return value comes from a register
typed Integer (the result of arithmetic, aget, iget of an int field, ...).
ART accepts any integral register in those places, which is how such code
can run on new devices and die with java.lang.VerifyError on old ones. This
tool follows every narrow sink back to the definitions of its value register
and reports the ones Dalvik's canConvertTo1nr() table would refuse.

It models the relevant part of Dalvik's lattice: constants are typed by value,
boolean and/or/xor with a 0/1 literal keep Boolean, cmp results are bytes,
narrow loads keep their width. Everything else that produces a 32-bit value
counts as Integer.

usage: dalvik_verify_check.py FILE.dex   (exit code 1 when findings exist)
"""
from __future__ import annotations
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from dex_rewriter import DexModel, OPCODE_WIDTH

NARROW = {'Z': 'boolean', 'B': 'byte', 'S': 'short', 'C': 'char'}
OK_FOR = {
    'boolean': {'Zero', 'One', 'Boolean'},
    'byte': {'Zero', 'One', 'Boolean', 'ConstPosByte', 'ConstByte', 'PosByte', 'Byte'},
    'short': {'Zero', 'One', 'Boolean', 'ConstPosByte', 'ConstByte', 'ConstPosShort', 'ConstShort',
              'PosByte', 'Byte', 'PosShort', 'Short'},
    'char': {'Zero', 'One', 'Boolean', 'ConstPosByte', 'ConstPosShort', 'ConstChar', 'PosByte', 'PosShort', 'Char'},
}
WIDE_RESULT_OPS = set(range(0x9b, 0xa6)) | set(range(0xab, 0xb0)) | set(range(0xbb, 0xc6)) | set(range(0xcb, 0xd0)) | {
    0x04, 0x05, 0x06, 0x0b, 0x16, 0x17, 0x18, 0x19, 0x45, 0x53, 0x61, 0x7d, 0x7e, 0x80, 0x81, 0x83, 0x85, 0x86,
    0x88, 0x89, 0x8b}
NO_DEF_OPS = set(range(0x32, 0x3e)) | set(range(0x4b, 0x52)) | set(range(0x59, 0x60)) | set(range(0x67, 0x6e)) | {
    0x00, 0x0e, 0x0f, 0x10, 0x11, 0x1d, 0x1e, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c}
NO_DEF_OPS |= set(range(0x6e, 0x73)) | set(range(0x74, 0x79))


def s4(v): return v - 16 if v & 8 else v
def s8(v): return v - 256 if v & 0x80 else v
def s16(v): return v - 0x10000 if v & 0x8000 else v
def s32(v): return v - 0x100000000 if v & 0x80000000 else v


def const_type(v):
    if v == 0: return 'Zero'
    if v == 1: return 'One'
    if 0 <= v <= 127: return 'ConstPosByte'
    if -128 <= v <= 127: return 'ConstByte'
    if 0 <= v <= 32767: return 'ConstPosShort'
    if -32768 <= v <= 32767: return 'ConstShort'
    if 0 <= v <= 65535: return 'ConstChar'
    return 'ConstInteger'


def decode(words):
    pc = 0; out = []
    while pc < len(words):
        w0 = words[pc]; op = w0 & 0xff; hi = w0 >> 8
        if op == 0 and hi in (1, 2, 3):
            if hi == 1: width = 4 + words[pc+1] * 2
            elif hi == 2: width = 2 + words[pc+1] * 4
            else: width = 4 + (words[pc+1] * (words[pc+2] | (words[pc+3] << 16)) + 1) // 2
            out.append((pc, 'payload', hi, list(words[pc:pc+width]))); pc += width; continue
        width = OPCODE_WIDTH[op]
        out.append((pc, op, hi, list(words[pc:pc+width]))); pc += width
    return out


def branch_targets(insns, index, by_pc, words):
    pc, op, hi, w = insns[index]
    if op == 'payload': return []
    if op == 0x28: return [pc + s8(hi)]
    if op == 0x29: return [pc + s16(w[1])]
    if op == 0x2a: return [pc + s32(w[1] | (w[2] << 16))]
    if 0x32 <= op <= 0x3d: return [pc + s16(w[1])]
    if op in (0x2b, 0x2c):
        ppc = pc + s32(w[1] | (w[2] << 16)); p = by_pc.get(ppc)
        if p is None: return []
        pw = insns[p][3]; kind = insns[p][2]
        if kind == 1:
            size = pw[1]; return [pc + s32(pw[4+2*i] | (pw[5+2*i] << 16)) for i in range(size)]
        size = pw[1]; return [pc + s32(pw[2+2*size+2*i] | (pw[3+2*size+2*i] << 16)) for i in range(size)]
    return []


def defined_register(op, hi, w):
    """Register (low half for wides) written by this instruction, or None."""
    if op == 'payload' or op in NO_DEF_OPS: return None
    if op in (0x03, 0x06, 0x09): return w[1]           # move/16 forms: AAAA
    if op in (0x24, 0x25): return None                 # filled-new-array: result via move-result
    return hi & 0xf if op in {0x01, 0x04, 0x07, 0x12, 0x20, 0x21, 0x23} or 0x52 <= op <= 0x58 or 0x7b <= op <= 0x8f \
        or 0xb0 <= op <= 0xd7 else hi


def is_boolean_literal_op(op, w):
    """and/or/xor-int/lit8 or lit16 with literal 0 or 1 (Dalvik keeps Boolean)."""
    if op in (0xdd, 0xde, 0xdf): return (s8(w[1] >> 8)) in (0, 1)
    if op in (0xd5, 0xd6, 0xd7): return s16(w[1]) in (0, 1)
    return False


def analyse(dex, m):
    code = m.code
    insns = decode(code.insns)
    by_pc = {ins[0]: i for i, ins in enumerate(insns)}
    preds = {i: set() for i in range(len(insns))}
    for i, (pc, op, hi, w) in enumerate(insns):
        if op == 'payload': continue
        falls = op not in (0x0e, 0x0f, 0x10, 0x11, 0x27, 0x28, 0x29, 0x2a)
        if falls and i + 1 < len(insns): preds[i + 1].add(i)
        for t in branch_targets(insns, i, by_pc, code.insns):
            if t in by_pc: preds[by_pc[t]].add(i)
    for tb in code.tries:
        covered = [i for i, ins in enumerate(insns) if tb.start_addr <= ins[0] < tb.start_addr + tb.insn_count]
        handlers = [a for _t, a in tb.handler.pairs] + ([tb.handler.catch_all] if tb.handler.catch_all is not None else [])
        for h in handlers:
            if h in by_pc:
                for i in covered: preds[by_pc[h]].add(i)

    def result_type(i, reg):
        """Types that may reach instruction i in register reg."""
        found = set(); seen = set(); stack = [(p, reg) for p in preds[i]]
        while stack:
            j, r = stack.pop()
            if (j, r) in seen: continue
            seen.add((j, r))
            pc, op, hi, w = insns[j]
            d = defined_register(op, hi, w) if op != 'payload' else None
            wide = op in WIDE_RESULT_OPS
            if d is None or not (d == r or (wide and d + 1 == r)):
                stack.extend((p, r) for p in preds[j]); continue
            if op == 0x12: found.add(const_type(s4(hi >> 4)))
            elif op == 0x13: found.add(const_type(s16(w[1])))
            elif op == 0x14: found.add(const_type(s32(w[1] | (w[2] << 16))))
            elif op == 0x15: found.add(const_type(s32((w[1] << 16) & 0xffffffff)))
            elif op in (0x01, 0x02, 0x03):               # move: follow the source
                src = hi >> 4 if op == 0x01 else (w[1] if op == 0x02 else w[2])
                stack.extend((p, src) for p in preds[j])
                if not preds[j]: found.add('Param')
            elif op == 0x8f: found.add('Short')
            elif op == 0x8d: found.add('Byte')
            elif op == 0x8e: found.add('Char')
            elif op in (0x4a, 0x58, 0x66): found.add('Short')
            elif op in (0x48, 0x56, 0x64): found.add('Byte')
            elif op in (0x49, 0x57, 0x65): found.add('Char')
            elif op in (0x47, 0x55, 0x63, 0x20): found.add('Boolean')
            elif op in (0x2d, 0x2e, 0x2f, 0x30, 0x31): found.add('Byte')
            elif op == 0x0a:                              # move-result: callee return type
                k = j - 1
                while k >= 0 and insns[k][1] == 'payload': k -= 1
                cop, cw = insns[k][1], insns[k][3]
                ret = dex.methods[cw[1]].proto.return_type if isinstance(cop, int) and (0x6e <= cop <= 0x78) else 'I'
                found.add({'Z': 'Boolean', 'B': 'Byte', 'S': 'Short', 'C': 'Char'}.get(ret, 'Integer'))
            elif is_boolean_literal_op(op, w):
                src = hi >> 4 if op in (0xd5, 0xd6, 0xd7) else (w[1] & 0xff)
                inner = result_type(j, src)
                found |= {'Boolean'} if inner and inner <= {'Boolean', 'Zero', 'One'} else {'Integer'}
            elif op in (0x95, 0x96, 0x97, 0xb5, 0xb6, 0xb7):   # and/or/xor-int (2addr): boolean if both are
                if op >= 0xb0: a_src, b_src = hi & 0xf, hi >> 4
                else: a_src, b_src = w[1] & 0xff, w[1] >> 8
                ta, tb = result_type(j, a_src), result_type(j, b_src)
                found |= {'Boolean'} if ta and tb and ta <= {'Boolean', 'Zero', 'One'} and tb <= {'Boolean', 'Zero', 'One'} else {'Integer'}
            elif op in (0x44, 0x52, 0x60, 0x21) or 0x7b <= op <= 0x8c or 0x90 <= op <= 0xe2: found.add('Integer')
            else: found.add('Other')
        if not found: found.add('Param')
        return found

    findings = []
    for i, (pc, op, hi, w) in enumerate(insns):
        if op == 'payload': continue
        sinks = []
        if op in (0x4e, 0x4f, 0x50, 0x51): sinks.append((hi, ['boolean', 'byte', 'char', 'short'][op - 0x4e], 'aput'))
        elif op in (0x5c, 0x5d, 0x5e, 0x5f): sinks.append((hi & 0xf, ['boolean', 'byte', 'char', 'short'][op - 0x5c], 'iput ' + dex.fields[w[1]].name))
        elif op in (0x6a, 0x6b, 0x6c, 0x6d): sinks.append((hi, ['boolean', 'byte', 'char', 'short'][op - 0x6a], 'sput ' + dex.fields[w[1]].name))
        elif op == 0x0f and m.key.proto.return_type in NARROW: sinks.append((hi, NARROW[m.key.proto.return_type], 'return'))
        elif 0x6e <= op <= 0x72 or 0x74 <= op <= 0x78:
            mk = dex.methods[w[1]]
            if 0x6e <= op <= 0x72:
                cnt = hi >> 4; g = hi & 0xf; regs = [w[2] & 0xf, (w[2] >> 4) & 0xf, (w[2] >> 8) & 0xf, (w[2] >> 12) & 0xf, g][:cnt]
            else: regs = list(range(w[2], w[2] + hi))
            idx = 0 if op in (0x71, 0x77) else 1
            for ptype in mk.proto.params:
                if idx >= len(regs): break
                if ptype in NARROW: sinks.append((regs[idx], NARROW[ptype], f'arg {ptype} of {mk.name}'))
                idx += 2 if ptype in ('J', 'D') else 1
        for reg, kind, desc in sinks:
            types = result_type(i, reg)
            bad = {t for t in types if t not in OK_FOR[kind] and t != 'Param'}
            if bad: findings.append((pc, desc, kind, sorted(types)))
    return findings


def check(path: Path) -> list[str]:
    dex = DexModel(path.read_bytes())
    report = []
    for c in dex.classes:
        for m in c.direct_methods + c.virtual_methods:
            if not m.code: continue
            for pc, desc, kind, types in analyse(dex, m):
                report.append(f'{c.descriptor}->{m.key.name} @{pc}: {desc} ({kind}) <- {types}')
    return report


if __name__ == '__main__':
    problems = check(Path(sys.argv[1]))
    for line in problems: print(line)
    print('Dalvik narrowing findings:', len(problems))
    sys.exit(1 if problems else 0)
