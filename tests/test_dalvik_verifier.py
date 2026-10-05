#!/usr/bin/env python3
"""Regression check for Dalvik verifier compatibility of the player DEX.

Android 2.3-4.4 (Dalvik) refuses narrowing stores of int-typed registers that
ART accepts. Version 1.4.3-1.4.14 shipped one such store in
PpmAudioMixer.mix(), which made every playback with sound end in
java.lang.VerifyError on those devices. The built DEX must stay clean and the
reference DEX must still show exactly that one defect, so the checker itself
is known to detect it."""
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from dalvik_verify_check import check  # noqa: E402

built = ROOT / "build" / "classes-v1.4.15.dex"
if not built.exists():
    base = ROOT / "build" / "base.dex"
    with zipfile.ZipFile(ROOT / "reference" / "base-v1.4.11.apk") as z:
        base.write_bytes(z.read("classes.dex"))
    subprocess.check_call([sys.executable, str(ROOT / "tools" / "patch-dex-v1415.py"), str(base), str(built)])

problems = check(built)
assert problems == [], problems

reference = check(ROOT / "build" / "classes-v1.4.14.dex")
assert len(reference) == 1 and "PpmAudioMixer;->mix" in reference[0] and "(short)" in reference[0], reference

# The checker also accepts the compiler-built 1.3.x code paths that are known
# to verify on Android 2.3 (boolean xor with literal 1, cmp results, ...).
from dex_rewriter import DexModel  # noqa: E402
model = DexModel(built.read_bytes())
names = {c.descriptor for c in model.classes}
assert "Lde/offlineflipnote/legacy/CrashLog;" in names
print("PASS: Dalvik narrowing check clean for 1.4.15; reference 1.4.14 shows the single mixer defect")
