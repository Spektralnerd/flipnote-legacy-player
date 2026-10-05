#!/usr/bin/env python3
"""Version 1.4.16: lighter player on Android-2.3-class hardware.

On top of the 1.4.15 DEX (Dalvik-verifiable mixer, CrashLog) this patch

  1. moves PpmAudioMixer.mix([SIIJ)V to native code: the method loses its
     body and becomes `public native`; libflipbrowser.so exports
     Java_de_offlineflipnote_legacy_PpmAudioMixer_mix (audio_mixer.inc) with
     the identical arithmetic;
  2. adds `public static native long tickDelayMillis()` to MainActivity and
     makes the UI ticker MainActivity$1.run() use it instead of the constant
     16 ms: `const-wide/16 v1, #16` becomes `invoke-static {} tickDelayMillis;
     move-result-wide v1`. The native side returns 40 ms while idle on
     Gingerbread-class devices (the interval of version 1.3.2) and 16 ms
     otherwise.

All other method bodies, including the 1.4.12 bridge and the 1.4.15 changes,
are preserved word for word after index remapping.
"""
from pathlib import Path
import importlib.util
import sys
from dex_rewriter import *
from dalvik_patch import instruction_bounds

HERE = Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location('patch1415', HERE / 'patch-dex-v1415.py')
patch1415 = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(patch1415)

MAIN = 'Lde/offlineflipnote/legacy/MainActivity;'
TICKER = 'Lde/offlineflipnote/legacy/MainActivity$1;'
MIXER = 'Lde/offlineflipnote/legacy/PpmAudioMixer;'
TICK_DELAY = MethodKey(MAIN, 'tickDelayMillis', Proto('J', ()))
MIX = MethodKey(MIXER, 'mix', Proto('V', ('[S', 'I', 'I', 'J')))
RUN = MethodKey(TICKER, 'run', Proto('V', ()))

OLD_DELAY_WORDS = [0x0116, 0x0010]          # const-wide/16 v1, #16


def make_mix_native(model):
    m = model.method_by_key(MIX)
    assert m.code is not None and not (m.access_flags & ACC_NATIVE)
    m.code = None
    m.access_flags = ACC_PUBLIC | ACC_NATIVE
    return m


def add_tick_delay(model):
    main = model.class_by_desc(MAIN)
    assert all(x.key != TICK_DELAY for x in main.direct_methods + main.virtual_methods)
    model.add_method_ref(TICK_DELAY)
    main.direct_methods.append(EncodedMethod(TICK_DELAY, ACC_PUBLIC | ACC_STATIC | ACC_NATIVE, None))


def patch_ticker(model):
    """MainActivity$1.run(): ask the native library how long to wait."""
    m = model.method_by_key(RUN)
    code = m.code
    assert code.registers_size == 4 and code.ins_size == 1 and code.outs_size == 4
    assert code.tries == []
    ops = [(pc, code.insns[pc] & 0xff) for pc, _w, kind in instruction_bounds(code.insns) if kind == 'insn']
    # iget-object, invoke-static, iget-object, invoke-static, move-result-object,
    # const-wide/16, invoke-virtual, return-void
    assert [op for _pc, op in ops] == [0x54, 0x71, 0x54, 0x71, 0x0c, 0x16, 0x6e, 0x0e], ops
    delay_pc = ops[5][0]
    assert code.insns[delay_pc:delay_pc + 2] == OLD_DELAY_WORDS, code.insns[delay_pc:delay_pc + 2]
    original = CodeItem(code.registers_size, code.ins_size, code.outs_size, list(code.insns), [])

    def build(idx, dex, c):
        words = dex._remap_insns(original, idx)
        assert words[delay_pc:delay_pc + 2] == OLD_DELAY_WORDS
        return (words[:delay_pc]
                + [0x0071, idx.method_idx[TICK_DELAY], 0x0000,   # invoke-static {}, tickDelayMillis()J
                   0x010b]                                       # move-result-wide v1
                + words[delay_pc + 2:])
    code.builder = build
    return delay_pc


def patch(data):
    staged = patch1415.patch(data)
    model = DexModel(staged)
    make_mix_native(model)
    add_tick_delay(model)
    delay_pc = patch_ticker(model)
    result = model.build()
    validate_dex(result)

    rebuilt = DexModel(result)
    reference = DexModel(staged)
    indexes = model.indexes()
    checked = 0
    for oldcls in reference.classes:
        for old in oldcls.direct_methods + oldcls.virtual_methods:
            new = rebuilt.method_by_key(old.key)
            if old.key == MIX:
                assert new.code is None and new.access_flags == (ACC_PUBLIC | ACC_NATIVE)
                continue
            assert new.access_flags == old.access_flags, old.key
            if not old.code:
                assert new.code is None
                continue
            remapped = reference._remap_insns(old.code, indexes)
            if old.key == RUN:
                expected = (remapped[:delay_pc]
                            + [0x0071, indexes.method_idx[TICK_DELAY], 0x0000, 0x010b]
                            + remapped[delay_pc + 2:])
                assert new.code.insns == expected
            else:
                assert remapped == new.code.insns, old.key
                assert old.code.tries == new.code.tries, old.key
            assert old.code.registers_size == new.code.registers_size
            assert old.code.ins_size == new.code.ins_size
            assert old.code.outs_size == new.code.outs_size
            checked += 1
    tick = rebuilt.method_by_key(TICK_DELAY)
    assert tick.code is None and tick.access_flags == (ACC_PUBLIC | ACC_STATIC | ACC_NATIVE)
    print(f'Player: {checked} method bodies checked; mix() is native, '
          f'tickDelayMillis() added, ticker patched at code unit {delay_pc}.')
    return result


if __name__ == '__main__':
    source, dest = map(Path, sys.argv[1:])
    dest.write_bytes(patch(source.read_bytes()))
    print(dest)
