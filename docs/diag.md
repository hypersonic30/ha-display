# Display-Diagnose (nur für Entwickler/Fehlersuche)

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
