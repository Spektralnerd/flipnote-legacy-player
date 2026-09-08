#!/usr/bin/env python3
"""Static regression test for metadata and external-intent hooks."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "native" / "gallery_pager.inc").read_text(encoding="utf-8")

required_fragments = [
    "gallery_show_metadata_dialog",
    'NewStringUTF(env, "Flipnote-Info")',
    'NewStringUTF(env, "Info")',
    '"Dateiname: "',
    'Autor: ',
    'Frames: ',
    'Aktueller Frame: ',
    'Bildrate: ',
    'Dauer: ',
    'Schleife: ',
    'Audio: ',
    'BGM-Bildrate: ',
    'Dateigroesse: ',
    '"android.intent.action.VIEW"',
    '"loadSelectedUri", "(Landroid/net/Uri;)V"',
    "gallery_clear_selection();",
]
for fragment in required_fragments:
    assert fragment in source, fragment

# Info must track the same controlsVisible state as the existing overlay.
assert "info_visible = note && controls_visible;" in source
# The external URI must be handled after the player view exists.
assert source.index("gallery_handle_external_intent_once(env, activity);") > source.index(
    "if (!g_gallery_runtime.attached && !gallery_attach(env, activity)) return;")

print("Metadatenansicht/externer Datei-Intent: OK")
