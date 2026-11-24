# Display-Diagnose (nur für Entwickler/Fehlersuche)

> **Ab 0.7.0 ersetzt der „Display-Test“ im Portal (Bereich Hardware, siehe [hardware.md](hardware.md)) diese Builds.** Die `diag-*`-Umgebungen gibt es nicht mehr; die Dateien in `firmware/diag/` sind die letzten TFT_eSPI-Diagnose-Images (0.6.x) und nur noch historisch.

Die `diag-*`-Umgebungen in `firmware/platformio.ini` sind **kein Release**. Sie lesen beim Start die Controller-ID
und malen vier Vollbild-Farben (rot, grün, blau, weiß, je 1 s). Das Bild ist sofort sichtbar, wenn die Variante passt.

Flashen: nur die App an `0x10000` (Bootloader und Partitionen bleiben). Seriell 115200 Baud, Reset drücken.
Die Dateien entstehen mit `pio run -e <name>` und liegen dann in `.pio/build/<name>/firmware.bin`.

Varianten: ILI9481 bei 5 / 10 / 16 MHz (Init 1), ILI9481 mit den Init-Folgen CMO35 / CPT29 / PVI35 (10 MHz),
ILI9486, ST7796, ILI9488 (alle 10 MHz). Lesetakt 4 MHz, Touch unverändert.

Serielle Zeilen `[DIAG]`: `0xBF` = `.. 02 04 94 81 ..` ILI9481, `0xD3` = `00 00 94 88` ILI9488, `77 96` ST7796,
`00 93 41` ILI9341. Nur `00` oder `FF`: das Modul antwortet nicht (MISO nicht am Display) – dann zählt nur das Bild.
Zeigt eine Variante die Farben in der richtigen Reihenfolge, ist sie die richtige; sind Farben vertauscht oder
negativ, stimmt `TFT_RGB_ORDER`/`TFT_INVERSION_ON`.

Ergebnis am Autoren-Board (ILI9481, parallele Leiterbahnen): 5 und 10 MHz zeigen ein korrektes Bild mit richtigen Farben, 16 MHz bleibt schwarz.
Deshalb nutzt `[env:ili9481]` seit 0.6.2 10 MHz (Lesetakt 4 MHz); `[env:ili9488]` ist unverändert (27 MHz).
