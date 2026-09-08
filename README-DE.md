# Flipnote Legacy Player 1.4.12 – Android 2.1 und Browser-Einstellungen

Testausgabe auf Grundlage von Version 1.4.11. Eine gemeinsame APK mit
Mindestversion Android 2.1 (API 7); die bisherigen neueren Android-Pfade bleiben
enthalten. Ein physischer Gerätetest dieser Ausgabe wurde hier nicht durchgeführt.

## Bedienung

Im Flipnote-Browser befindet sich oben rechts die dauerhaft erreichbare
Schaltfläche **MENUE**. Sie öffnet **BROWSER-EINSTELLUNGEN**. Die Zeile
**ANIMIERTE VORSCHAUEN** schaltet zwischen **AN** und **AUS** um. Auf Geräten
mit Hardware-Menütaste kann auch diese das Menü öffnen.

- **AN:** sichtbare Vorschauen laufen wie bisher gleichzeitig und lautlos.
- **AUS:** nur statische eingebettete Vorschauen. Die vollständigen PPM-Daten
  und dekodierten Animationszustände werden aus dem Vorschau-Cache freigegeben.
- Die Auswahl bleibt beim Schließen des Browsers und der App gespeichert.
- Der eigentliche Player spielt weiter normal mit Bild und Ton. Der Schalter
  verändert ausschließlich die Galerie-Vorschauen, nicht deren Dateien.

Unter Android 2.1/2.2 ist beim ersten Einsatz **AUS** voreingestellt. Ab Android
2.3 ist die erste Voreinstellung **AN**, damit das bisherige Verhalten erhalten
bleibt. Anschließend zählt immer die selbst gewählte, gespeicherte Einstellung.
Für das Galaxy S4 empfiehlt sich zum Testen **AUS**.

## Nachlaufendes Scrollen

Nach einem zügigen Wischen scrollt die Galerie weiter und bremst allmählich ab.
Ein erneutes Aufsetzen des Fingers stoppt den Nachlauf; dieser Bremstipp öffnet
nicht gleichzeitig eine Flipnote. Am Ordneranfang und -ende hält die Ansicht an.
Ein vor dem Loslassen ruhig gehaltener Finger löst keinen neuen Nachlauf aus.

Während Ziehen und Nachlauf pausieren animierte Vorschauen. Große PPM-Dateien
werden in dieser Zeit nicht neu für Animationen geladen. Sobald die Bewegung
endet, laufen die Vorschauen bei Einstellung AN weiter. Neu hereingescrollte
Kacheln können deshalb zunächst Platzhalter zeigen. Im statischen Modus werden
weiterhin nur die kleinen Vorschaubilder nachgeladen.

## Android 2.1 / 2.2

Nur minSdkVersion herunterzusetzen wäre nicht ausreichend gewesen: Der bisherige
Browser benutzt NativeActivity und libandroid.so, die ab API 9 existieren.

Diese Ausgabe enthält deshalb zusätzlich BrowserActivity, eine normale Activity
mit einem Bitmap-/ImageView-Anzeigepfad für API 7/8. Sie teilt sich Dateiliste,
Vorschauen, Ordner-Rückkehr, Einstellungen und Scrollmodell mit dem bisherigen
Browser. Auf API 9 und neuer leitet sie den Aufruf mitsamt Ergebnisrückgabe an
den bestehenden NativeActivity-Browser weiter.

Die native Playerbibliothek hat keine feste libandroid.so-Abhängigkeit mehr.
Die API-9-Einstiegspunkte werden erst im NativeActivity-Pfad über dlopen/dlsym
ermittelt. Unter API 7/8 werden weder libandroid.so noch libjnigraphics benötigt.

Das ist eine implementierte, aber auf Android-2.1-Hardware noch unbestätigte
Kompatibilitätsausgabe. Der Hosttest ist kein Android-Emulator. Auch für sämtliche
aktuellen oder zukünftigen Hersteller-ROMs gibt es keine Laufzeitgarantie.

## Installation

Paket: de.offlineflipnote.legacy
Version: 1.4.12
Versionscode: 28
minSdk: 7
targetSdk: 28 (unverändert)

Das Signaturzertifikat entspricht Version 1.4.11. Die signierte APK ist als
direktes Update vorgesehen; eine vorherige Deinstallation ist nicht notwendig.
Es wurden keine Berechtigungen ergänzt. Kein Internet, keine Telemetrie, keine
Werbung und kein Export.

## Bestehender Player

Zoom, Galerie-Wischen, Rückkehr zur markierten Datei, Wiedergabebalken,
Geschwindigkeitssteuerung, Audio, Loop, OLED-Schwarz, Infoansicht, Dateiverknüpfung,
Wachhaltung und Vollbildpfade sind weiterhin enthalten.

Die vorhandenen 167 Player-Methodenkörper wurden beim Aufbau des neuen DEX
vollständig erhalten. Ihre Tabellenverweise werden für die zusätzliche
BrowserActivity neu nummeriert. Die gesamte classes.dex ist daher ausdrücklich
NICHT bytegleich mit Version 1.4.11.

## Umfang dieses Quellpakets

Dieses Paket enthält den nativen Quellcode, den Java-Quelltext der neuen kleinen
BrowserActivity, DEX-/Manifest-Werkzeuge und Tests. Der bestehende Java-Player
wird aus der beigelegten Referenz-APK übernommen. Es handelt sich nicht um einen
vollständigen, aus ursprünglichem Java-Quelltext neu kompilierten Gradle-Build.
Die Link-Stubs unter native/fake dienen nur der Importprüfung beim Linken;
sie werden nicht in die APK aufgenommen.

## Bauen

Benötigt: Python 3, Clang mit Android-Targets/LLD, llvm-objcopy, readelf, ein JDK
mit JNI-Headern. Für die Signierung zusätzlich die Pakete aus requirements-signing.txt.

```sh
sh build-release.sh /tmp/player-unsigned.apk

# Privater Schlüssel muss außerhalb dieses Repositorys liegen.
SIGN_CERT=/privat/cert.pem SIGN_KEY=/privat/key.pem \
  sh build-release.sh /tmp/player-signed.apk

sh tests/run-host-tests.sh
SANITIZE=1 sh tests/run-host-tests.sh
sh tests/run-controls-test.sh
SANITIZE=1 sh tests/run-controls-test.sh
sh tests/run-jni-host-test.sh
python3 tools/validate-v1412.py /tmp/player-signed.apk
```

Die Freigabeprüfung erwartet das öffentliche Zertifikat der bisherigen
Update-Reihe. Ein selbst signierter Build mit anderem Schlüssel kann eine
vorhandene Installation dieser Reihe nicht regulär aktualisieren.

reference/base-v1.4.11.apk ist eine Buildreferenz, kein als Update installierbares
Downgrade über 1.4.12. Alte Versionen nicht versehentlich darüber installieren.

## Testumfang

Die C-Tests verwenden den echten Decoder und das echte Scrollmodell. Der JNI-Test
führt den neuen nativen Activity-Pfad in einer Desktop-JVM mit kleinen
Android-Schnittstellen-Doubles aus. Er prüft Methodensignaturen, Bitmap-Übergabe,
Einstellungen, Größenänderung, Lebenszyklus und Ergebnisrückgabe. Er ersetzt
keine echte Android-/Dalvik-/ART-Laufzeit und keine Touch- oder Audiotreiber.

## Technische Referenzen

- NativeActivity (ab API 9): https://developer.android.com/reference/android/app/NativeActivity
- Native APIs / optionale neuere APIs: https://developer.android.com/ndk/guides/stable_apis
- Bitmap: https://developer.android.com/reference/android/graphics/Bitmap
- Activity-Ergebnisweitergabe: https://developer.android.com/reference/android/content/Intent#FLAG_ACTIVITY_FORWARD_RESULT

## Lizenz

Siehe LICENSE. Signaturschlüssel und Passwörter sind nicht Bestandteil des Pakets.
