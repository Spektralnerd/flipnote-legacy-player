#!/usr/bin/env python3
"""Version 1.4.15: make the Java player verifiable by Dalvik (Android 2.3-4.4).

The 1.4.x audio mixer stored an int-typed register with aput-short. ART accepts
any integral register there, Dalvik's verifier (canConvertTo1nr) does not, so on
Android 2.3 the class PpmAudioMixer failed verification and the first playback
with sound died with java.lang.VerifyError. This patch

  1. inserts `int-to-short v2, v2` in front of the aput-short in
     PpmAudioMixer.mix() (semantically a no-op: the value is already clamped
     to -32768..32767);
  2. restores the try/catch around AudioTrack.getPlaybackHeadPosition() in
     PpmAudioEngine.positionOf() that 1.3.2 had, so a track released by the
     streaming thread can no longer throw on the UI thread;
  3. adds CrashLog, a Thread.UncaughtExceptionHandler with a native
     uncaughtException(), installed by the library's JNI_OnLoad. It writes
     Flipnote-Absturz.txt before Android's handler closes the app.

Everything else (all other player method bodies, the BrowserActivity bridge
from 1.4.12) is preserved byte for byte after index remapping.
"""
from pathlib import Path
import importlib.util
import sys
from dex_rewriter import *
from dalvik_patch import insert_words, append_words, instruction_bounds, normalized

HERE = Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location('patch1412', HERE / 'patch-dex-v1412.py')
patch1412 = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(patch1412)

MIXER = 'Lde/offlineflipnote/legacy/PpmAudioMixer;'
CRASH_LOG = 'Lde/offlineflipnote/legacy/CrashLog;'
HANDLER_IFACE = 'Ljava/lang/Thread$UncaughtExceptionHandler;'
ENGINE = 'Lde/offlineflipnote/legacy/PpmAudioEngine;'
RUNTIME_EXCEPTION = 'Ljava/lang/RuntimeException;'


def find_method(model, desc, name):
    cls = model.class_by_desc(desc)
    for m in cls.direct_methods + cls.virtual_methods:
        if m.key.name == name:
            return m
    raise KeyError(name)


def fix_mix(model):
    """int-to-short before every aput-short whose value register is not a
    short already. In mix() that is exactly the final sample store."""
    m = find_method(model, MIXER, 'mix')
    words = list(m.code.insns)
    targets = []
    for pc, width, kind in instruction_bounds(words):
        if kind == "insn" and words[pc] & 0xff == 0x51:   # aput-short vAA, vBB, vCC
            value_reg = words[pc] >> 8
            targets.append((pc, value_reg))
    assert len(targets) == 1, targets
    pc, reg = targets[0]
    assert reg < 16, 'int-to-short needs a 4-bit register'
    conv = 0x8f | (reg << 8) | (reg << 12)          # int-to-short vA, vA (12x)
    new_words = insert_words(m.code, words, pc, [conv])
    m.code.insns = new_words
    return pc, reg


def fix_position_of(model):
    """try { head = unsignedHead(track.getPlaybackHeadPosition()); }
       catch (RuntimeException e) { head = 0; } -- as in version 1.3.2."""
    m = find_method(model, ENGINE, 'positionOf')
    words = list(m.code.insns)
    # Expected prologue: iget-object v6,v9,track / invoke-virtual {v6} / move-result v6
    #                    / invoke-static {v6} / move-result-wide v0 / long-to-double v4, v0
    ops = [w & 0xff for w, _k in [(words[pc], kind) for pc, _w, kind in instruction_bounds(words)]]
    assert ops[:6] == [0x54, 0x6e, 0x0a, 0x71, 0x0b, 0x86], ops[:6]
    assert m.code.tries == []
    resume_pc = 10
    assert words[resume_pc] & 0xff == 0x86          # long-to-double v4, v0
    scratch = words[0] >> 8 & 0xf                    # v6 (the iget-object destination)
    handler_pc = len(words)
    handler = [
        0x0d | (scratch << 8),                       # move-exception vS
        0x16 | (0 << 8), 0x0000,                     # const-wide/16 v0, #0
        0x29, (resume_pc - (handler_pc + 3)) & 0xffff,   # goto/16 -> resume
    ]
    words, start = append_words(words, handler)
    assert start == handler_pc
    m.code.insns = words
    model.add_type(RUNTIME_EXCEPTION)
    m.code.tries.append(TryBlock(0, resume_pc, CatchHandler([(RUNTIME_EXCEPTION, handler_pc)], None)))
    return handler_pc


def add_crash_log(model):
    """public final class CrashLog implements Thread.UncaughtExceptionHandler {
           public CrashLog() {}
           public native void uncaughtException(Thread t, Throwable e);
       }"""
    obj = 'Ljava/lang/Object;'
    for d in (CRASH_LOG, obj, HANDLER_IFACE, 'Ljava/lang/Thread;', 'Ljava/lang/Throwable;'):
        model.add_type(d)
    cls = ClassDef(CRASH_LOG, ACC_PUBLIC | ACC_FINAL, obj, [HANDLER_IFACE], None, [], [], [], [], [])
    model.classes.append(cls)
    ctor = MethodKey(CRASH_LOG, '<init>', Proto('V', ()))
    super_ctor = MethodKey(obj, '<init>', Proto('V', ()))
    for m in (ctor, super_ctor):
        model.add_method_ref(m)
    code = CodeItem(1, 1, 1, [], [])
    code.builder = lambda idx, m, c: [0x1070, idx.method_idx[super_ctor], 0x0000, 0x000e]
    cls.direct_methods.append(EncodedMethod(ctor, ACC_PUBLIC | ACC_CONSTRUCTOR, code))
    handler = MethodKey(CRASH_LOG, 'uncaughtException',
                        Proto('V', ('Ljava/lang/Thread;', 'Ljava/lang/Throwable;')))
    model.add_method_ref(handler)
    cls.virtual_methods.append(EncodedMethod(handler, ACC_PUBLIC | ACC_NATIVE, None))


def patch(data):
    staged = patch1412.patch(data)                   # BrowserActivity bridge, as in 1.4.12-1.4.14
    model = DexModel(staged)
    mix_pc, mix_reg = fix_mix(model)
    handler_pc = fix_position_of(model)
    add_crash_log(model)
    result = model.build()
    validate_dex(result)

    rebuilt = DexModel(result)
    reference = DexModel(staged)
    indexes = model.indexes()
    checked = 0
    for oldcls in reference.classes:
        for old in oldcls.direct_methods + oldcls.virtual_methods:
            new = rebuilt.method_by_key(old.key)
            assert new.access_flags == old.access_flags
            if not old.code:
                assert new.code is None
                continue
            remapped = reference._remap_insns(old.code, indexes)
            if old.key.owner == MIXER and old.key.name == 'mix':
                conv = 0x8f | (mix_reg << 8) | (mix_reg << 12)
                assert new.code.insns[mix_pc] == conv
                assert len(new.code.insns) == len(remapped) + 1
                # Structurally identical apart from the inserted conversion:
                # every branch still reaches the same instruction.
                before = normalized(remapped)
                after = normalized(new.code.insns)
                k = [i for i, (pc, _w, _kind) in enumerate(instruction_bounds(new.code.insns)) if pc == mix_pc][0]
                assert after[k] == ('i', (conv,))
                del after[k]
                # Branch ordinals past the insertion shift by one.
                def unshift(entry):
                    if entry[0] != 'b': return entry
                    *head, target = entry
                    return (*head, target - 1 if target > k else target)
                assert [unshift(e) for e in after] == before
            elif old.key.owner == ENGINE and old.key.name == 'positionOf':
                assert new.code.insns[:len(remapped)] == remapped
                assert len(new.code.tries) == 1 and new.code.tries[0].start_addr == 0
                assert new.code.tries[0].handler.pairs == [(RUNTIME_EXCEPTION, handler_pc)]
            else:
                assert remapped == new.code.insns, old.key
                assert old.code.tries == new.code.tries, old.key
            assert old.code.registers_size == new.code.registers_size
            assert old.code.ins_size == new.code.ins_size
            assert old.code.outs_size == new.code.outs_size
            checked += 1
    crash = rebuilt.class_by_desc(CRASH_LOG)
    assert crash.interfaces == [HANDLER_IFACE] and crash.superclass == 'Ljava/lang/Object;'
    assert [m.key.name for m in crash.virtual_methods] == ['uncaughtException']
    assert crash.virtual_methods[0].access_flags & ACC_NATIVE
    print(f'Player: {checked} method bodies checked; mix() narrowed at {mix_pc}, '
          f'positionOf() guarded (handler at {handler_pc}); CrashLog added.')
    return result


if __name__ == '__main__':
    source, dest = map(Path, sys.argv[1:])
    dest.write_bytes(patch(source.read_bytes()))
    print(dest)
