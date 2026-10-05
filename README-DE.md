# Flipnote Legacy Player 1.4.16 – flüssige Wiedergabe auf Android-2.3-Geräten

Testausgabe auf Grundlage von Version 1.4.15. Ein physischer Gerätetest dieser
Ausgabe wurde hier nicht durchgeführt.

## Neu in 1.4.16

Anlass: Auf einem Samsung Galaxy Ace (GT-S5830, Android 2.3, ARMv6 mit einem
Kern) spielte 1.4.15 zwar wieder ab, aber mit starkem Ruckeln; Version 1.3.2
lief auf demselben Gerät flüssig. Der Vergleich der beiden Versionen zeigt
fünf Unterschiede, die bei jedem Bild bzw. jedem Tonblock Rechenzeit kosten.
1.4.16 beseitigt alle fünf:

1. **UI-Takt wieder 40 ms auf Gingerbread-Hardware.** Der Java-Player
   aktualisiert Bild, Suchleiste und Zeitanzeige über einen `Handler`-Ticker.
   1.3.2 lief mit 40 ms, 1.4.x mit 16 ms. Jeder Tick ruft zweimal
   `String.format("%d:%02d.%03d", …)` auf (Zeit und Dauer, millisekundengenau)
   und ändert damit den Text der Zeitanzeige, was ein Neulayout des gesamten
   Fensters auslöst; auf einem ARM11 mit 800 MHz sind 60 solcher Durchläufe
   pro Sekunde zu viel. Der Ticker fragt jetzt die native Bibliothek
   (`MainActivity.tickDelayMillis()`): Auf Geräten mit Android ≤ 3.2 (API ≤ 13)
   oder mit der ARMv5/ARMv6-Bibliothek (`armeabi`, also genau die Geräteklasse
   des Galaxy Ace) beträgt der Ruhe-Takt 40 ms wie in 1.3.2; alle anderen
   Geräte behalten 16 ms. Während eines Wischvorgangs und der
   Einrastanimation gilt überall 16 ms, damit die Geste flüssig bleibt. Die
   Bildwechsel richten sich wie bisher nach der Audio- bzw. Systemuhr; mit
   40 ms Takt werden sie wie in 1.3.2 mit bis zu 40 ms Versatz angezeigt.
2. **Audio-Mischung in C statt Java.** Der 1.4.x-Mixer berechnete pro
   Ausgabesample (32768 pro Sekunde) einen `Math.floor()`-Aufruf und drei
   Binärsuchen in Java, auf einem Thread mit `THREAD_PRIORITY_URGENT_AUDIO`,
   der die Oberfläche auf einem Einkernprozessor verdrängt – geschätzt ein
   Viertel der CPU. `PpmAudioMixer.mix()` ist jetzt eine native Methode
   (`native/audio_mixer.inc`) mit derselben Arithmetik in Ganzzahlen: der
   BGM-Schrittwert wird als 24.40-Festkomma aus den Bits des `double`
   gewonnen, Begrenzung und Reihenfolge der Additionen sind identisch. Zwei
   Tests vergleichen die native Ausgabe Sample für Sample mit der Java-Logik
   von 1.4.15 (552 Zufallsblöcke, sechs BGM-Raten, sechs bis sieben Geschwindigkeiten,
   leere Spuren, ungültige Raten, Randbereiche); der Ton ändert sich nicht.
3. **Nachbarseiten laufen nur, wenn man sie sieht.** Die zwei stummen
   Vorschau-Seiten links und rechts der aktuellen Flipnote dekodierten bisher
   ständig Bilder (je 256×192 Pixel in Java), obwohl sie von der mittleren
   Seite vollständig verdeckt sind – dreifacher Dekodieraufwand. Sie laufen
   jetzt nur noch während des Ziehens, der Einrastanimation und bis zum
   Zentrieren nach einem Layout. Gilt auf allen Geräten; sichtbar ist der
   Unterschied nicht, weil verdeckte Seiten nicht gezeichnet werden.
4. **JNI-Namenslookups nur noch einmal.** `galleryTick()` suchte bei jedem
   Tick rund 40 Methoden und Felder per Namen (`GetMethodID`/`GetFieldID`),
   was Dalvik durch Vergleich mit allen Methodennamen der Activity-Hierarchie
   beantwortet. Die IDs werden jetzt je Aufrufstelle einmal ermittelt und
   gemerkt; der Vergleich des Dateinamens mit der Auswahlliste erfolgt nur
   noch, wenn sich Name oder Liste ändern.
5. **Suchleisten-Reparatur höchstens alle 250 ms.** Die vorhandene Prüfung,
   ob die Suchleiste nach dem Umhängen in den Pager unsichtbar geworden ist,
   lief mit elf JNI-Aufrufen pro Tick; jetzt viermal pro Sekunde.

Auf Android 5 und neuer ändert sich am sichtbaren Verhalten nichts
(Punkte 3 bis 5 sparen dort nur Rechenzeit, Punkt 2 liefert identische
Samples). Keine neue Berechtigung, kein neuer Import in der Bibliothek
(weiterhin nur libc/libdl, keine libm, kein Gleitkomma im ARMv5-Build).

### Test auf dem Galaxy Ace

APK signieren, über 1.4.15 installieren. Demo mit Play abspielen (Bild und
Ton). Über den Browser eine Flipnote mit Ton und eine ohne Ton öffnen, einige
Sekunden laufen lassen, zu Nachbarn wischen, Zurück in den Browser. Erwartet:
Wiedergabe wie mit 1.3.2. Falls es weiter ruckelt, bitte angeben, ob es die
Demo betrifft, ob Ton an ist und ob die Flipnote aus einem Ordner oder einer
ZIP stammt. Ein Absturz landet weiterhin in `Flipnote-Absturz.txt`.

## Neu in 1.4.15

- **Wiedergabe unter Android 2.3 bis 4.4 (Dalvik).** Seit Version 1.4.3 hat
  der Audio-Mixer `PpmAudioMixer.mix()` einen Befehl, der einen `int`-Wert
  ohne Umwandlung als `short` in den Tonpuffer schreibt (`aput-short` ohne
  `int-to-short`). Die ART-Laufzeit ab Android 5 lässt das zu, die ältere
  Dalvik-Laufzeit lehnt die Klasse bei der Prüfung ab. Sobald eine Flipnote
  **mit Ton** gestartet wurde (auch die eingebaute Demo), warf Android einen
  `java.lang.VerifyError`, den der Player nicht abfängt – die App schloss
  sich. Der Flipnote-Browser war nicht betroffen, weil er keinen Ton ausgibt;
  Flipnotes ohne Ton und stummgeschaltete Wiedergabe liefen. 1.4.15 setzt die
  fehlende Umwandlung ein (der Wert ist dort bereits auf -32768..32767
  begrenzt, der Ton ändert sich nicht). Alle anderen Methoden des Players sind
  unverändert.
- **Positionsabfrage abgesichert.** 1.3.2 fing bei `getPlaybackHeadPosition()`
  einen Fehler des Audiosystems ab, 1.4.x nicht mehr. Wenn der Audio-Thread
  die Tonspur am Ende freigibt, während die Oberfläche gerade die Position
  liest, konnte das die App beenden. Der Schutz ist wieder da (Position 0
  statt Ausnahme).
- **Absturzprotokoll.** Ein nicht abgefangener Java-Fehler wird vor dem
  Schließen der App in `Flipnote-Absturz.txt` im Hauptordner des externen
  Speichers geschrieben (bei alten Geräten die SD-Karte, z. B.
  `/mnt/sdcard/Flipnote-Absturz.txt`; Ersatz: App-Cache). Die Datei enthält
  Android-Version, Gerät, Thread und den Stacktrace und wächst bei weiteren
  Abstürzen an (ab 256 KB wird neu begonnen). Danach erscheint wie gewohnt die
  Android-Meldung, dass die App beendet wurde. Keine neue Berechtigung, kein
  Internet.
- Werkzeuge: `tools/dalvik_verify_check.py` prüft ein DEX auf genau die
  Verengungsregeln, die Dalvik strenger auslegt als ART;
  `tests/test_dalvik_verifier.py` lässt den Test bei jedem Lauf gegen das
  gebaute DEX laufen und erwartet im 1.4.14-DEX den einen bekannten Fehler.
  Die Prüfung ist Teil von `tools/validate-v1415.py`.

Betroffen von dem Fehler waren alle Ausgaben 1.4.3 bis 1.4.14 auf Geräten mit
Dalvik (Android 2.3, 3.x, 4.0 bis 4.4). Auf Android 5 und neuer gab es kein
sichtbares Problem; dort ändert 1.4.15 am Verhalten nichts außer dem
Absturzprotokoll.

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
Version: 1.4.16
Versionscode: 32
minSdk: 7
targetSdk: 28 (unverändert)

Die beigelegte Test-APK ist **unsigniert** und kann so nicht installiert
werden. Sie ist mit dem Zertifikat der bisherigen Update-Reihe zu signieren
(siehe Bauen); dann ist sie als direktes Update über 1.4.12 bis 1.4.15 vorgesehen, ohne
vorherige Deinstallation. Es wurden keine Berechtigungen ergänzt. Kein
Internet, keine Telemetrie, keine Werbung und kein Export.

## Umfang dieses Quellpakets

Nativer Quellcode (Browser, ZIP-Modul, Galerie-Pager, Absturzprotokoll,
Audio-Mixer), Java-Quelltext der kleinen BrowserActivity und der
CrashLog-Klasse (beide werden als DEX-Klassen mit nativen Methoden erzeugt),
DEX-/Manifest-Werkzeuge einschließlich des Bytecode-Patchers
`tools/dalvik_patch.py`, Tests und die Referenz-APK 1.4.11, aus der der
bestehende Java-Player übernommen wird. `tools/patch-dex-v1416.py` macht
`PpmAudioMixer.mix()` zur nativen Methode und fügt `tickDelayMillis()` ein;
`java/de/offlineflipnote/legacy/PpmAudioMixer-native.java` dokumentiert die
Signatur. Es handelt sich
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
python3 tools/validate-v1416.py /tmp/player-signed.apk
python3 tools/validate-v1416.py /tmp/player-unsigned.apk --unsigned
python3 tools/dalvik_verify_check.py build/classes-v1.4.16.dex
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
Ordner-Merken und 1150 zufällig beschädigte Archive.
`tests/host_mixer_test.c` vergleicht den nativen Mixer mit einer
Gleitkomma-Transkription der Java-Methode, `tests/jni-host/…/MixerTest.java`
tut dasselbe über JNI aus einer Desktop-JVM heraus und prüft
`tickDelayMillis()` für API 10, 13, 14 und 36;
`tests/test_gingerbread_performance.py` sichert die Struktur der
Takt-, Nachbarseiten- und ID-Cache-Änderungen im Quelltext und im DEX. Die großen Archive
belegen als Sparse-Dateien nur wenige KB; auf Dateisystemen ohne
Sparse-Unterstützung werden sie weggelassen (`FLIPNOTE_ZIP_BIG=0` schaltet
sie ab). Der JNI-Test führt den
API-7/8-Pfad zusätzlich durch einen ZIP-Ordner mit Entpacken nach
`getCacheDir()`. Er ersetzt keine echte Android-/Dalvik-/ART-Laufzeit.

## Technische Referenzen

- Handler.postDelayed / UI-Thread-Ticker: https://developer.android.com/reference/android/os/Handler#postDelayed(java.lang.Runnable,%20long)
- Process.THREAD_PRIORITY_URGENT_AUDIO: https://developer.android.com/reference/android/os/Process#THREAD_PRIORITY_URGENT_AUDIO
- JNI-Tipps (IDs cachen, Get<Type>ArrayElements): https://developer.android.com/training/articles/perf-jni
- Dalvik-Bytecode-Verifizierer (CodeVerify, canConvertTo1nr) im AOSP-Zweig
  android-2.3.7_r1: dalvik/vm/analysis/CodeVerify.c
- ART-Verifizierer, VerifyPrimitivePut (akzeptiert jeden Integer-Typ):
  art/runtime/verifier/method_verifier.cc
- Dalvik-Bytecode-Format: https://source.android.com/docs/core/runtime/dalvik-bytecode

- ZIP-Dateiformat (APPNOTE): https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT
- 32-Bit-ABI und off64_t/lseek64 in Bionic: https://android.googlesource.com/platform/bionic/+/master/docs/32-bit-abi.md
- DEFLATE (RFC 1951): https://www.rfc-editor.org/rfc/rfc1951
- Context.getCacheDir(): https://developer.android.com/reference/android/content/Context#getCacheDir()
- NativeActivity (ab API 9): https://developer.android.com/reference/android/app/NativeActivity

## Lizenz

Siehe LICENSE. Signaturschlüssel und Passwörter sind nicht Bestandteil des Pakets.
