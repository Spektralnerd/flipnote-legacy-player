# Flipnote Legacy Player

*English summary: an Android player for Flipnote Studio `.ppm` animations that
still runs on Android 2.1 (API 7) and everything after it. It ships its own
thumbnail browser with silent animated previews, opens ZIP archives like
folders (including Google-Drive exports beyond 2 GB), swipes between notes,
zooms, and plays sound with speed control. The Java player is carried as a
compiled reference APK; all newer functionality is native code plus DEX
patches, so there is no Gradle project here. The full documentation is German:
see [README-DE.md](README-DE.md). Releases carry APKs signed by the maintainer;
the APK produced by CI is unsigned and cannot be installed.*

Ein Player für Flipnote-Studio-Animationen (`.ppm`) auf Android – bewusst für
alte Geräte gebaut. Mindestversion ist Android 2.1 (API 7); getestet wird auf
einem Samsung Galaxy Ace (Android 2.3, ARMv6) und auf aktuellen Geräten.

## Was die App kann

- **Flipnote-Browser** mit Kacheln aus den eingebetteten Vorschaubildern,
  wahlweise mit **lautlos laufenden Vorschauen** (Einstellung „ANIMIERTE
  VORSCHAUEN“), nachlaufendem Scrollen und gemerktem Ordner.
- **ZIP-Archive wie Ordner**: Hunderter-Ordner `0000`, `0100`, … werden direkt
  im Archiv durchsucht, auch bei Archiven über 2 GB und ZIP64. Angetippte
  Flipnotes landen im App-Cache und werden von dort abgespielt.
- **Player** mit Ton (BGM und Soundeffekte), Geschwindigkeit, Loop,
  Suchleiste, Einzelbild-Schritt, Infoansicht, Dateiverknüpfung für `.ppm`.
- **Wischen** zum vorherigen/nächsten Flipnote des Ordners, **Pinch-Zoom**
  bis 8x, Vollbild bis hin zu Display-Ausschnitten moderner Geräte.
- **Absturzprotokoll** `Flipnote-Absturz.txt` auf dem externen Speicher, weil
  alte Geräte selten ein Logcat anbieten.
- Keine Berechtigungen außer Speicherzugriff. Kein Internet, keine Telemetrie,
  keine Werbung.

## Download und Installation

Installierbare, signierte APKs liegen unter
[Releases](../../releases). Jede Version der 1.4.x-Reihe ist ein direktes
Update über die vorige (gleiches Signaturzertifikat, Paket
`de.offlineflipnote.legacy`, `targetSdk 28`).

Die APK aus dem GitHub-Actions-Lauf (Artefakt
`Flipnote-Legacy-Player-unsigned-apk`) ist **unsigniert** und dient nur der
Prüfung des Builds; Android installiert sie nicht.

## Aufbau des Repositorys

| Pfad | Inhalt |
| --- | --- |
| `native/` | Die gesamte neue Funktionalität in C: `flipbrowser.c` (NativeActivity-Browser, API 9+), `eclair_browser.inc` (Bitmap-Pfad für API 7/8), `zip_archive.inc` (ZIP-Index, eigener Inflate), `gallery_pager.inc` (Wischen, Zoom, Vollbild, UI-Takt), `audio_mixer.inc` (Tonmischung), `crash_log.inc` |
| `java/` | Java-Gegenstücke der kleinen DEX-Klassen, die die Werkzeuge erzeugen (Dokumentation, nicht der Build) |
| `tools/` | DEX-Rewriter und -Patcher (`dex_rewriter.py`, `dalvik_patch.py`, `patch-dex-v*.py`), Manifest-Patcher, APK-Packer/Signierer, Dalvik-Verifizierer-Prüfung, Freigabeprüfung `validate-v*.py` |
| `reference/base-v1.4.11.apk` | Buildreferenz: der kompilierte Java-Player (Ressourcen, Demo-Flipnote). Kein installierbares Downgrade. |
| `build/` | Mitgeführte DEX-/Manifest-Stände früherer Versionen (für Vergleichstests); Build-Ausgaben entstehen hier |
| `tests/` | C-Hosttests mit dem echten Decoder/ZIP-Code, Python-Strukturtests, JNI-Hosttest in einer Desktop-JVM mit Android-Doubles |
| `README-DE.md`, `VALIDIERUNG.txt`, `BASE-REFERENCE.txt` | Ausführliche Dokumentation, Prüfbericht und Referenzangaben der aktuellen Version |

## Bauen

Benötigt: Python 3, Clang mit LLD (Cross-Targets für ARMv5, ARMv7, x86,
ARM64, x86-64 sind in Stock-Clang enthalten), `llvm-objcopy`, `readelf`, ein
JDK mit JNI-Headern; für die Signierung die Pakete aus
`requirements-signing.txt`.

```sh
sh build-release.sh build/player-unsigned.apk
python3 tools/validate-v1416.py build/player-unsigned.apk --unsigned

# Signiert (Schlüssel liegen außerhalb des Repositorys):
SIGN_CERT=/privat/cert.pem SIGN_KEY=/privat/key.pem sh build-release.sh build/player.apk
```

Der Build ist reproduzierbar: zwei Läufe ergeben byteidentische APKs.

## Tests

```sh
sh tests/run-host-tests.sh                        # C- und Python-Tests
CLANG=gcc SANITIZE=1 sh tests/run-host-tests.sh   # dieselben unter ASan/UBSan
sh tests/run-controls-test.sh
sh tests/run-jni-host-test.sh                     # Desktop-JVM, API 7/8 und Mixer
```

Alle Tests laufen ohne Android-Gerät und ohne Emulator; sie ersetzen keinen
Gerätetest. Der Workflow unter `.github/workflows/build.yml` baut die
unsignierte APK und führt die Suiten bei jedem Push aus.

## Versionen

| Tag | Inhalt |
| --- | --- |
| `v1.4.16` | Flüssige Wiedergabe auf Android-2.3-Geräten: 40-ms-Takt auf Gingerbread-Hardware, nativer Mixer, verdeckte Nachbarseiten ruhen |
| `v1.4.15` | Dalvik-`VerifyError` beim Abspielen mit Ton (Android 2.3–4.4) behoben, Absturzprotokoll |
| `v1.4.14` | ZIP-Archive als Ordner (1.4.13), Archive über 2 GB / ZIP64 (1.4.14) |
| `v1.4.12` | Android 2.1/2.2 (BrowserActivity), Browser-Menü, nachlaufendes Scrollen |

Die Historie beginnt mit 1.4.12, dem ersten Stand, der als vollständiges
Quellpaket vorliegt. Die Änderungen jeder Version sind in
[README-DE.md](README-DE.md) unter „Neu in …“ beschrieben.

## Lizenz

MIT, siehe [LICENSE](LICENSE). Signaturschlüssel sind nicht Teil des
Repositorys.
