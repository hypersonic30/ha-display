# Hardware einstellen (ab Firmware 0.7.0)

Display-Controller, SPI-Pins, Takt, Backlight und Touch werden **zur Laufzeit** im Einrichtungsportal des
Displays eingestellt, ohne neu zu kompilieren. Die Firmware nutzt dafür LovyanGFX statt TFT_eSPI; die
Init-Folgen und Drehungen der Controller sind die von TFT_eSPI, ein Panel, das vorher lief, sieht gleich aus.

## Standardwerte (Schaltplan dieses Projekts)

| Signal | GPIO | Signal | GPIO |
|---|---|---|---|
| MOSI (SDI) | 23 | TFT CS | 15 |
| SCLK (SCK) | 18 | TFT DC (RS) | 2 |
| MISO (SDO) | 19 | TFT RESET | 4 |
| Touch CS | 14 | Backlight (TFT_LED) | 32 |
| Summer (PIEZO) | 26 | | |

Touch-IRQ (IO27 am Schaltplan) ist standardmäßig **aus** (`-1`): die Firmware fragt den Berührungsdruck wie
bisher per SPI ab. Wer die IRQ-Leitung angeschlossen hat, kann `27` eintragen.

Standard-Controller: `firmware-ili9481` → ILI9481, `firmware-ili9488` → ILI9488. Die beiden Dateien
unterscheiden sich **nur** darin (und heißen so, weil die Integration die Variante für das OTA-Update nutzt).
Wer einen anderen Controller hat, wählt ihn im Portal; die Wahl bleibt bei OTA-Updates erhalten.

Empfohlene Voreinstellung je Controller (das Portal füllt sie beim Wechsel ein):

| Controller | Schreibtakt | Lesetakt | Invertieren |
|---|---|---|---|
| ILI9481 | 10 MHz | 4 MHz | an |
| ILI9488 | 20 MHz | 10 MHz | aus |
| ILI9486 | 20 MHz | 10 MHz | an |
| ST7796 | 20 MHz | 10 MHz | aus |

Der **Takt** ist bei langen, parallelen Leiterbahnen oft entscheidend: Beim Autoren-Board zeigt der ILI9481
bei 5 und 10 MHz ein korrektes Bild, bei 16 MHz bleibt der Schirm schwarz.

## Portal: Bereich „Hardware“

Aufruf: Einrichtungs-WLAN des Displays (beim ersten Start oder per langem Druck auf eine leere Stelle,
siehe [wifi.md](wifi.md)), Seite `192.168.4.1`, ganz unten „Hardware (Display & Touch)“.

- Controller, die sieben Pins (MOSI, SCLK, MISO, CS, DC, RESET, Backlight), Schreib-/Lesetakt.
- **Summer** (`-1` = keiner): Pin des Summers, Standard GPIO 26 (über einen Transistor, braucht ein Tonsignal; siehe README „Signalton“). Der Pin muss ein Ausgang sein; er darf nicht mit einem anderen Signal geteilt werden.
- Backlight bei LOW an, Farbreihenfolge BGR/RGB (Rot und Blau vertauscht → umschalten), Invertierung
  (Farben wie ein Negativ → umschalten).
- Touch (XPT2046) an/aus, Touch-CS, Touch-IRQ (`-1` = nicht benutzen).
- **Display-Test**: malt Rot, Grün, Blau, Weiß (je 1 s), spielt danach einen kurzen Doppelton, wenn ein Summer eingestellt ist, und schreibt die Controller-ID in die serielle
  Konsole (115200 Baud, `[HW]`-Zeilen). `0xBF` mit `94 81` = ILI9481, `0xD3` mit `94 88` = ILI9488,
  `77 96` = ST7796. Nur `00`/`FF`: das Modul antwortet nicht auf Lesebefehle (bei vielen SPI-Modulen
  normal), dann zählt nur das Bild.
- „Hardware speichern & neu starten“, „Auf Standard zurücksetzen“.

Die Pins werden geprüft: keine Flash-Pins (6–11), kein UART0 (1, 3), kein BOOT-Pin (0), Eingangs-Pins
(34–39) nur für MISO und Touch-IRQ, keine doppelt belegten Pins. Wer von einer 0.7.0-Vorabversion ohne Summer-Pin
kommt: die gespeicherte Konfiguration wird gelesen, der Summer bekommt GPIO 26.

## Wenn das Display schwarz bleibt

Das Einrichtungs-WLAN und das Portal laufen auch ohne funktionierendes Display. Zurück zu den
Standardwerten:

1. **Portal** → „Auf Standard zurücksetzen“, oder
2. **BOOT-Taste** (GPIO0) in den ersten 30 Sekunden nach dem Einschalten 3 Sekunden gedrückt halten
   (nicht beim Einschalten selbst: das startet den Flash-Modus), oder
3. automatisch: stürzt die Firmware dreimal hintereinander kurz nach dem Start ab (Panic oder Watchdog
   innerhalb von 20 s), werden die Standardwerte wiederhergestellt.

## Touch-Kalibrierung

Mit 0.7.0 gibt es **eine** Kalibrierung für alle Drehungen (vier Eckpunkte). Eine Kalibrierung aus
früheren Versionen wird nicht übernommen: beim ersten Start nach dem Update fragt das Display nach dem
Laufen der Verbindung nach den vier Ecken (mit 60 s Zeitlimit); beim Einschalten 3 s auf den Schirm
drücken erzwingt sie.

Jederzeit neu kalibrieren geht auch **aus dem Einrichtungsportal**: Karte „Touch“ → „Touch neu
kalibrieren“ (Seite `192.168.4.1`), oder direkt am Display mit dem Knopf **„Touch kalibrieren“** auf dem
Einrichtungsbildschirm (der mit WLAN-Name und Passwort). Das Display zeigt dann vier Markierungen, die
du nacheinander in den Ecken antippst (60 s Zeit); danach erscheint der Einrichtungsbildschirm wieder und
die Seite im Browser meldet das Ergebnis. Der Knopf am Display funktioniert auch mit den groben
Standardwerten, falls der Touch noch nie kalibriert wurde. Während der Kalibrierung antwortet das Portal nicht;
die Seite fragt deshalb alle paar Sekunden nach.

## Selbst bauen

`firmware/platformio.ini` kennt nur noch die beiden Umgebungen `ili9481` und `ili9488`. Sie setzen
`HW_DEFAULT_PANEL` (0 = ILI9481, 1 = ILI9488, 2 = ILI9486, 3 = ST7796); alle Pins stehen in
`firmware/src/hw.cpp` (`hw_defaults`).
