# Diagnose-Images (nur zum Testen)

Nur die Anwendung, in esptool-js an Adresse `0x10000` schreiben (Bootloader/Partitionen bleiben).
Beim Start: Controller-ID im seriellen Log (115200 Baud), danach rote/grüne/blaue/weiße Flächen.
Zurück zur normalen Firmware: `firmware-ili9481.bin` bzw. `firmware-ili9488.bin` an `0x10000`.

Reihenfolge bei leerem Display: `diag-ili9481-5mhz`, dann `-10mhz`, `-16mhz`; danach die Varianten
`cmo35`/`cpt29`/`pvi35` (andere Init-Sequenzen), `diag-ili9486-10mhz`, `diag-st7796-10mhz`.
Diese Dateien gehören nicht zu einem Release und nicht in die OTA-Updates.
