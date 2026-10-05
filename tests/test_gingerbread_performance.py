#!/usr/bin/env python3
"""Regression checks for the 1.4.16 per-tick cost reductions (Galaxy Ace / Android 2.3).

Version 1.3.2 played smoothly on a GT-S5830 (ARMv6, single core); 1.4.15
stuttered there. The differences per UI tick were: 16 ms instead of 40 ms
ticks (each tick runs two String.format() calls and a relayout of the time
label), ~40 JNI name lookups in galleryTick(), frame decoding for the two
hidden neighbour pages, a seek-bar check with eleven lookups, and a Java mixer
that called Math.floor() plus three binary searches per audio sample on the
URGENT_AUDIO thread. This test pins the structure of the fixes in the native
source and in the built DEX."""
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from dex_rewriter import DexModel, ACC_NATIVE, ACC_STATIC, ACC_PUBLIC, MethodKey, Proto  # noqa: E402
from dalvik_patch import instruction_bounds  # noqa: E402

pager = (ROOT / "native" / "gallery_pager.inc").read_text(encoding="utf-8")
mixer = (ROOT / "native" / "audio_mixer.inc").read_text(encoding="utf-8")
browser = (ROOT / "native" / "flipbrowser.c").read_text(encoding="utf-8")

# --- UI tick interval by device class --------------------------------------
for fragment in [
    "#define GALLERY_TICK_FAST_MS 16",
    "#define GALLERY_TICK_SLOW_MS 40",
    "#define GALLERY_SLOW_DEVICE_MAX_SDK 13",
    "#if defined(__arm__) && defined(__ARM_ARCH) && __ARM_ARCH < 7",
    "#define GALLERY_SLOW_ABI_BUILD 1",
    "static int gallery_device_is_slow(JNIEnv* env)",
    "static jlong gallery_tick_delay_millis(JNIEnv* env)",
    "jlong Java_de_offlineflipnote_legacy_MainActivity_tickDelayMillis(JNIEnv* env, jclass cls)",
    "gallery_android_sdk(env) <= GALLERY_SLOW_DEVICE_MAX_SDK",
]:
    assert fragment in pager, fragment
# Drags and settle animations keep the fast interval on every device.
delay_body = pager[pager.index("static jlong gallery_tick_delay_millis"):pager.index("Java_de_offlineflipnote_legacy_MainActivity_tickDelayMillis")]
assert "g_gallery_runtime.settling || g_gallery_runtime.touching" in delay_body
assert "g_gallery_runtime.center_pending" in delay_body
assert "return GALLERY_TICK_FAST_MS;" in delay_body
assert "gallery_device_is_slow(env) ? GALLERY_TICK_SLOW_MS : GALLERY_TICK_FAST_MS" in delay_body


def tick_delay(slow_build: bool, sdk: int, settling=False, touching=False, center_pending=False) -> int:
    if settling or touching or center_pending:
        return 16
    slow = slow_build or sdk <= 13
    return 40 if slow else 16

assert tick_delay(True, 10) == 40            # Galaxy Ace: armeabi build, Android 2.3
assert tick_delay(False, 10) == 40           # ARMv7 phone on Android 2.3
assert tick_delay(False, 13) == 40           # Honeycomb
assert tick_delay(False, 14) == 16           # ICS and newer keep 1.4.x behaviour
assert tick_delay(False, 36) == 16
assert tick_delay(True, 10, settling=True) == 16
assert tick_delay(True, 10, touching=True) == 16

# --- neighbour pages animate only while they can be seen --------------------
assert "static int gallery_neighbors_visible(void)" in pager
tick_body = pager[pager.index("void Java_de_offlineflipnote_legacy_MainActivity_galleryTick"):pager.index("static int gallery_device_is_slow")]
assert "if (gallery_neighbors_visible()) {" in tick_body
assert tick_body.index("gallery_tick_settle(env, activity, now);") < tick_body.index("gallery_neighbors_visible()")
assert tick_body.count("gallery_page_advance(env, &g_gallery_runtime.") == 2
visible_body = pager[pager.index("static int gallery_neighbors_visible"):pager.index("static void gallery_scroll_to")]
assert "g_gallery_runtime.settling || g_gallery_runtime.center_pending" in visible_body
assert "g_gallery_runtime.horizontal_drag || g_gallery_runtime.drag_x != 0" in visible_body


def neighbors_visible(settling=False, center_pending=False, touching=False, horizontal_drag=False, drag_x=0) -> bool:
    if settling or center_pending:
        return True
    return touching and (horizontal_drag or drag_x != 0)

assert not neighbors_visible()                                   # idle playback: covered pages stay still
assert neighbors_visible(settling=True)
assert neighbors_visible(center_pending=True)
assert neighbors_visible(touching=True, horizontal_drag=True)
assert not neighbors_visible(touching=True)                      # a tap or a vertical touch shows no neighbour
# The frame clock resync keeps a page from bursting through MAX_CATCHUP_FRAMES
# when it becomes visible again after a long covered period.
assert "if ((int32_t)(now - page->next_due_ms) > 1000)" in pager
assert "if (!time_reached(now, page->next_due_ms)) return;" in pager

# --- JNI IDs are resolved once per call site --------------------------------
for fragment in [
    "typedef struct GalleryMethodSlot",
    "typedef struct GalleryFieldSlot",
    "static jmethodID gallery_method(JNIEnv* env, GalleryMethodSlot* slot, jobject receiver,",
    "static jfieldID gallery_field(JNIEnv* env, GalleryFieldSlot* slot, jobject receiver,",
    "if (slot->tried) return slot->id;",
    "static GalleryMethodSlot g_id_activity_has_window_focus;",
    "static GalleryMethodSlot g_id_pager_get_width;",
    "static GalleryMethodSlot g_id_pager_scroll_to;",
    "static GalleryMethodSlot g_id_pager_get_scroll_x;",
    "static GalleryMethodSlot g_id_page_get_height;",
    "static GalleryMethodSlot g_id_player_is_stretched;",
    "static GalleryMethodSlot g_id_player_show_frame;",
    "static GalleryFieldSlot g_id_activity_note;",
    "static GalleryFieldSlot g_id_activity_note_name;",
    "static GalleryFieldSlot g_id_activity_controls_visible;",
    'gallery_method(env, &g_id_activity_has_window_focus,\n            activity, "hasWindowFocus", "()Z")',
    'gallery_method(env, &g_id_player_show_frame, page->view, "showFrame", "(I)V")',
    'gallery_field(env, &g_id_activity_note_name, activity,\n            "noteName", "Ljava/lang/String;")',
]:
    assert fragment in pager, fragment
# The per-tick functions no longer call GetMethodID/GetFieldID themselves.
for name, stop in [
    ("static int gallery_activity_has_window_focus", "static int gallery_legacy_navigation_is_hidden"),
    ("static void gallery_zoom_sync_note", "static void gallery_text_append_jstring"),
    ("static int gallery_current_stretched", "static void gallery_set_button_visibility"),
    ("static void gallery_update_overlay_buttons", "/* Keep the Java seek bar measurable"),
    ("static void gallery_page_advance", "static int gallery_neighbors_visible"),
    ("static void gallery_scroll_to", "static void gallery_set_view_width"),
    ("static int gallery_update_page_width", "static void gallery_finish_pending_center"),
    ("static int gallery_find_current_from_note_name", "static void gallery_refresh_neighbors"),
]:
    body = pager[pager.index(name):pager.index(stop)]
    assert "GetMethodID" not in body and "GetFieldID" not in body, name

# noteName is compared against the list only when the String or the list changed.
assert "jobject note_name_identity;" in pager
assert "g_gallery_runtime.note_name_generation == g_gallery_runtime.generation" in pager
assert "gallery_delete_global(env, &g_gallery_runtime.note_name_identity);" in pager

# --- seek-bar repair is rate limited ----------------------------------------
assert "#define GALLERY_SEEKBAR_REPAIR_INTERVAL_MS 250u" in pager
repair = pager[pager.index("static void gallery_repair_seekbar"):pager.index("static void gallery_page_set_stretched")]
assert "now - g_gallery_runtime.seekbar_repair_checked_ms\n                    < GALLERY_SEEKBAR_REPAIR_INTERVAL_MS" in repair
assert repair.index("g_gallery_runtime.seekbar_repair_checked_ms = now;") < repair.index("activity_cls = (*env)->GetObjectClass(env, activity);")

# --- native mixer -------------------------------------------------------------
assert '#include "audio_mixer.inc"' in browser
for fragment in [
    "void Java_de_offlineflipnote_legacy_PpmAudioMixer_mix(",
    "JNIEnv* env, jobject self, jshortArray out, jint offset, jint count, jlong packed)",
    "static uint64_t mixer_step_q40(uint64_t bits, uint32_t output_rate)",
    "static int mixer_bgm_index(int sample, uint64_t step)",
    "static int mixer_lower_bound(const jint* values, int count, int64_t key)",
    "static uint64_t mixer_udiv64(uint64_t n, uint32_t d)",
    '"bgmSourceRate", "D"',
    '"OUTPUT_SAMPLE_RATE", "I"',
    "if (acc < -32768) acc = -32768;",
    "if (acc > 32767) acc = 32767;",
    "for (i = offset; i < out_length; i++) out[i] = 0;",
    "ReleaseShortArrayElements(env, out, out_data, 0);",
    "JNI_ABORT",
]:
    assert fragment in mixer, fragment
# Integer only: no floating-point arithmetic in the mixer (ARMv5 soft-float build).
import re
code_only = re.sub(r"/\*.*?\*/", "", mixer, flags=re.S).replace("union { jdouble d;", "")
for token in ("floor(", "(double)", "double ", "float ", "/ 32768.0", "jdouble"):
    assert token not in code_only, token


def step_q40(rate: float, output_rate: int) -> int:
    """Reference for mixer_step_q40 using exact rational arithmetic."""
    import struct
    bits = struct.unpack("<Q", struct.pack("<d", rate))[0]
    exponent = (bits >> 52) & 0x7ff
    mantissa = bits & ((1 << 52) - 1)
    if bits >> 63 or exponent in (0, 0x7ff) or not output_rate:
        return 0
    mantissa |= 1 << 52
    shift = exponent - 1075 + 40
    if shift > 10:
        return 0
    while shift < 0:
        mantissa >>= 1
        shift += 1
    mantissa <<= shift
    return mantissa // output_rate


def bgm_index(sample: int, step: int) -> int:
    return (sample * step) >> 40

import math
for rate in (8180.0, 16360.0, 8180.0 * 20 / 12, 8180.0 * 0.5 / 30, 8180.0 * 30 / 0.5):
    step = step_q40(rate, 32768)
    assert 0 < step < 1 << 47
    for sample in (0, 1, 7, 4095, 4096, 32768, 65535, 1_000_000, 123_456_789):
        exact = math.floor(sample * rate / 32768)
        got = bgm_index(sample, step)
        assert got in (exact, exact - 1), (rate, sample, exact, got)
        # The fixed-point result can only fall below floor() right at a boundary.
        if got != exact:
            fraction = sample * rate / 32768 - exact
            assert fraction < 1e-6, (rate, sample, fraction)
assert step_q40(float("nan"), 32768) == 0 and step_q40(float("inf"), 32768) == 0
assert step_q40(-8180.0, 32768) == 0 and step_q40(0.0, 32768) == 0 and step_q40(1e12, 32768) == 0

# --- built DEX ---------------------------------------------------------------
built = ROOT / "build" / "classes-v1.4.16.dex"
if not built.exists():
    base = ROOT / "build" / "base.dex"
    base.parent.mkdir(exist_ok=True)
    with zipfile.ZipFile(ROOT / "reference" / "base-v1.4.11.apk") as z:
        base.write_bytes(z.read("classes.dex"))
    subprocess.check_call([sys.executable, str(ROOT / "tools" / "patch-dex-v1416.py"), str(base), str(built)])
model = DexModel(built.read_bytes())
MAIN = "Lde/offlineflipnote/legacy/MainActivity;"
mix = [m for m in model.class_by_desc("Lde/offlineflipnote/legacy/PpmAudioMixer;").virtual_methods if m.key.name == "mix"][0]
assert mix.code is None and mix.access_flags == ACC_PUBLIC | ACC_NATIVE
tick_key = MethodKey(MAIN, "tickDelayMillis", Proto("J", ()))
tick = [m for m in model.class_by_desc(MAIN).direct_methods if m.key == tick_key]
assert len(tick) == 1 and tick[0].code is None and tick[0].access_flags == ACC_PUBLIC | ACC_STATIC | ACC_NATIVE
run = [m for m in model.class_by_desc("Lde/offlineflipnote/legacy/MainActivity$1;").virtual_methods if m.key.name == "run"][0]
words = run.code.insns
ops = [(pc, words[pc] & 0xff) for pc, _w, kind in instruction_bounds(words) if kind == "insn"]
assert [op for _pc, op in ops] == [0x54, 0x71, 0x54, 0x71, 0x0c, 0x71, 0x0b, 0x6e, 0x0e], ops
call_pc = ops[5][0]
assert model.methods[words[call_pc + 1]] == tick_key and words[call_pc + 3] == 0x010b
assert 0x0116 not in words                      # const-wide/16 v1, #16 is gone
assert run.code.registers_size == 4 and run.code.outs_size == 4
# updatePlaybackUi() still runs before galleryTick() on every tick.
access = model.method_by_key(MethodKey(MAIN, "access$000", Proto("V", (MAIN,))))
names = [model.methods[access.code.insns[pc + 1]].name for pc, _w, kind in instruction_bounds(access.code.insns)
         if kind == "insn" and access.code.insns[pc] & 0xff in (0x6e, 0x70, 0x71)]
assert names == ["updatePlaybackUi", "galleryTick"], names

print("PASS: 1.4.16 per-tick reductions: 40 ms idle tick on Gingerbread-class devices, covered neighbours idle, "
      "cached JNI IDs, rate-limited seek-bar repair, integer-only native mixer wired into the DEX")
