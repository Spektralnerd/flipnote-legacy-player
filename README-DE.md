# Flipnote Legacy Player 1.4.14 – ZIP-Archive im Flipnote-Browser

Testausgabe auf Grundlage von Version 1.4.13. Der Java-Player und die
DEX-Datei sind weiterhin byteidentisch mit 1.4.12; alle Änderungen liegen im
nativen Browser (`native/flipbrowser.c`, `native/zip_archive.inc`). Ein
physischer Gerätetest dieser Ausgabe wurde hier nicht durchgeführt.

## Neu in 1.4.14

- **Keine 2-GB-Grenze mehr.** 1.4.13 lehnte Archive ab 2 GiB ab (Google Drive
  schneidet seine Download-ZIPs genau dort ab: `…-1-001.zip`). Das ZIP-Modul
  liest die Datei jetzt mit 64-Bit-Positionen über `open`/`lseek64`/`read`
  statt über die 32-Bit-Funktionen `fseek`/`ftell`. `lseek64` gibt es in
  Android seit API 3, also auch auf Android 2.1. Archive über 4 GB mit ZIP64-
  Offsets werden ebenfalls gelesen. Einzelne Einträge bleiben auf 16 MB
  begrenzt, was für PPM-Dateien mehr als genug ist.
- Nach einem nicht lesbaren Archiv bleibt die Kachel „..“ stehen, damit es
  auf dem Bildschirm einen Weg zurück gibt.
- Der Mini-Font zeichnete „/“ spiegelverkehrt als „\“ (seit den ersten
  Versionen); Pfade wie `/storage/emulated/0` werden jetzt richtig angezeigt.

## Neu in 1.4.13

1. **ZIP-Archive werden wie Ordner geöffnet.** Eine ZIP-Datei erscheint im
   Browser als eigene Kachel „ZIP-ARCHIV“. Antippen zeigt die Ordner und
   PPM-Dateien im Archiv, z. B. Hunderter-Ordner `0000`, `0100`, `0200` … Die
   Pfadzeile lautet dann etwa `/sdcard/flipnotes.zip/0100`. „..“ führt aus dem
   Ordner zur Archivwurzel und von dort in den Ordner der ZIP-Datei zurück.
2. **Bis zu 1000 Einträge pro Ordner** (bisher 320). Enthält ein Ordner mehr,
   wird jetzt erst sortiert und dann abgeschnitten: Angezeigt wird der
   alphabetische Anfang, nicht mehr eine zufällige Auswahl in
   Dateisystem-Reihenfolge. Hinweis „MAXIMAL 1000 EINTRAEGE ANGEZEIGT“.
3. **Zuletzt geöffneter Ordner wird gemerkt** (auch ein Ordner in einer ZIP)
   und beim nächsten Start des Browsers wieder geöffnet. Ist er verschwunden
   (SD-Karte entfernt, ZIP gelöscht), startet der Browser wie bisher an der
   Speicherwurzel. Ein Rücksprung aus dem Player geht wie bisher in den
   Ursprungsordner mit markierter Datei vor.

## Bedienung mit ZIP-Archiven

- Vorschauen im Archiv funktionieren wie bei normalen Dateien: statisch aus
  dem eingebetteten Vorschaubild, mit **ANIMIERTE VORSCHAUEN: AN** animiert.
  Für ein statisches Vorschaubild werden nur die ersten 1696 Byte eines
  Eintrags entpackt, für die Animation und das Abspielen die ganze Datei.
- Beim Antippen einer PPM-Datei im Archiv wird sie in den App-Cache entpackt
  (`Context.getCacheDir()/zip/<Kennung>/<Dateiname>`) und dem vorhandenen
  Player wie eine normale Datei übergeben. Wischen zum nächsten und vorherigen
  Flipnote im selben Archivordner funktioniert weiter; die Nachbarn werden erst
  beim Wischen entpackt. Die Zurück-Taste landet wieder im Archivordner mit
  markierter Datei.
- Der Cache behält die 16 zuletzt benutzten Kopien (aktuelles Flipnote und
  Nachbarn) und löscht ältere. Beim nächsten App-Start werden Reste entfernt.
  Android darf den Cache jederzeit leeren. Keine neue Berechtigung.
- Unterstützt: ZIP mit Einträgen „gespeichert“ (stored) oder „Deflate“, also
  das Ergebnis praktisch aller ZIP-Programme; ZIP64-Felder für große
  Eintragszahlen; Archive mit Kommentar oder vorangestellten Daten
  (Selbstentpacker). Der Entpacker ist ein eigener kleiner Inflate in C nach
  RFC 1951; die Bibliothek benötigt weiterhin nur libc und libdl.
- Nicht unterstützt: verschlüsselte Einträge und andere Verfahren (bzip2,
  LZMA, PPMd …). Solche Einträge werden angezeigt und mit „NICHT UNTERSTUETZT“
  beschriftet; Antippen erzeugt einen Hinweis. Einzeleinträge über 16 MB
  werden nicht geöffnet; die Archivgröße selbst ist nicht mehr begrenzt.
- Grenzen: 100.000 PPM-Einträge pro Archiv im Index (Hinweis „ZIP-INDEX
  VOLL“), Eintragsnamen bis 512 Byte, Pfade bis 640 Byte. Nur PPM-Dateien und
  Ordner werden gelistet, Einträge mit `..` im Pfad werden ignoriert. Nicht
  ASCII-Zeichen in Namen zeigt der Mini-Font wie bisher als „?“.
- Gilt nur für den eigenen Pfad-Browser. Über die Android-Systemdateiauswahl
  („SYSTEM-DATEIEN“) lassen sich weiterhin nur einzelne PPM-Dateien öffnen,
  weil dort kein Dateipfad zur Verfügung steht.

## Bisheriger Funktionsumfang

Unverändert enthalten: Browser-Menü **MENUE** mit gespeicherter Einstellung
**ANIMIERTE VORSCHAUEN** (Android 2.1/2.2 startet mit AUS, ab 2.3 mit AN),
nachlaufendes Scrollen, BrowserActivity für API 7/8 mit Bitmap-Anzeigepfad
und Weiterleitung an die NativeActivity ab API 9, Zoom, Galerie-Wischen,
Rückkehr zur markierten Datei, Wiedergabebalken, Geschwindigkeitssteuerung,
Audio, Loop, OLED-Schwarz, Infoansicht, Dateiverknüpfung, Wachhaltung und
Vollbildpfade. Siehe README der Version 1.4.12 für Details.

## Installation

Paket: de.offlineflipnote.legacy
Version: 1.4.14
Versionscode: 30
minSdk: 7
targetSdk: 28 (unverändert)

Die beigelegte Test-APK ist **unsigniert** und kann so nicht installiert
werden. Sie ist mit dem Zertifikat der bisherigen Update-Reihe zu signieren
(siehe Bauen); dann ist sie als direktes Update über 1.4.12/1.4.13 vorgesehen, ohne
vorherige Deinstallation. Es wurden keine Berechtigungen ergänzt. Kein
Internet, keine Telemetrie, keine Werbung und kein Export.

## Umfang dieses Quellpakets

Nativer Quellcode (Browser, ZIP-Modul, Galerie-Pager), Java-Quelltext der
kleinen BrowserActivity, DEX-/Manifest-Werkzeuge, Tests und die Referenz-APK
1.4.11, aus der der bestehende Java-Player übernommen wird. Es handelt sich
nicht um einen vollständigen, aus ursprünglichem Java-Quelltext neu
kompilierten Gradle-Build. Die Link-Stubs unter native/fake dienen nur der
Importprüfung beim Linken; sie werden nicht in die APK aufgenommen.

## Bauen

Benötigt: Python 3, Clang mit Android-Targets/LLD, llvm-objcopy, readelf, ein JDK
mit JNI-Headern. Für die Signierung zusätzlich die Pakete aus requirements-signing.txt.

```sh
sh build-release.sh /tmp/player-unsigned.apk

# Privater Schlüssel muss außerhalb dieses Repositorys liegen.
SIGN_CERT=/privat/cert.pem SIGN_KEY=/privat/key.pem \
  sh build-release.sh /tmp/player-signed.apk

sh tests/run-host-tests.sh
SANITIZE=1 sh tests/run-host-tests.sh          # mit Clang-Sanitizer-Laufzeit
CLANG=gcc SANITIZE=1 sh tests/run-host-tests.sh  # alternativ mit GCC
sh tests/run-controls-test.sh
sh tests/run-jni-host-test.sh
python3 tools/validate-v1414.py /tmp/player-signed.apk
python3 tools/validate-v1414.py /tmp/player-unsigned.apk --unsigned
```

Die Freigabeprüfung erwartet das öffentliche Zertifikat der bisherigen
Update-Reihe. Ein selbst signierter Build mit anderem Schlüssel kann eine
vorhandene Installation dieser Reihe nicht regulär aktualisieren.

reference/base-v1.4.11.apk ist eine Buildreferenz, kein als Update installierbares
Downgrade. Alte Versionen nicht versehentlich darüber installieren.

## Testumfang

Die C-Tests verwenden den echten Decoder, das echte Scrollmodell und den
echten ZIP-Browser. `tests/host_zip_test.c` erzeugt mit
`tests/make_zip_fixtures.py` Archive (Deflate, Stored, ZIP64, nicht
unterstützte Verfahren, Vorspann, Kommentar, leer, defekt, 1200 Einträge,
sowie zwei dünnbesetzte Archive von 2,15 GB und 4,4 GB mit ZIP64) und
prüft Index, Entpacken gegen die Originaldaten in jeder Präfixlänge, CRC,
Navigation, Vorschauen, Cache, das Abschneiden nach dem Sortieren, das
Ordner-Merken und 1150 zufällig beschädigte Archive. Die großen Archive
belegen als Sparse-Dateien nur wenige KB; auf Dateisystemen ohne
Sparse-Unterstützung werden sie weggelassen (`FLIPNOTE_ZIP_BIG=0` schaltet
sie ab). Der JNI-Test führt den
API-7/8-Pfad zusätzlich durch einen ZIP-Ordner mit Entpacken nach
`getCacheDir()`. Er ersetzt keine echte Android-/Dalvik-/ART-Laufzeit.

## Technische Referenzen

- ZIP-Dateiformat (APPNOTE): https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT
- 32-Bit-ABI und off64_t/lseek64 in Bionic: https://android.googlesource.com/platform/bionic/+/master/docs/32-bit-abi.md
- DEFLATE (RFC 1951): https://www.rfc-editor.org/rfc/rfc1951
- Context.getCacheDir(): https://developer.android.com/reference/android/content/Context#getCacheDir()
- NativeActivity (ab API 9): https://developer.android.com/reference/android/app/NativeActivity

## Lizenz

Siehe LICENSE. Signaturschlüssel und Passwörter sind nicht Bestandteil des Pakets.
