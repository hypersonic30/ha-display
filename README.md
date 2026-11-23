# ESP32 Display – Home-Assistant-Panel

Selbstgebautes Touch-Panel (ESP32 + ILI9488 480×320) für Home Assistant. Das Layout gestaltest du **grafisch in Home Assistant** – ohne die Firmware neu zu flashen. Änderungen erscheinen sofort auf dem Display.

- **Eine Integration, beliebig viele Displays** – Geräte sind Unterkonten; **kein Home-Assistant-Token im Display**
- **Visueller Editor** (Lovelace-Karte): Elemente ziehen, Entität wählen, speichern
- **Fertige Firmware** zum Flashen, Ersteinrichtung per WLAN-Hotspot am Handy
- **Live-Werte** und **Touch-Bedienung** (Taster, Schalter) direkt am Display
- Alles aus einem Repository, Installation über **HACS**

## Hardware

| Bauteil | Wert |
|---|---|
| MCU | ESP32-WROOM-32D |
| Display | ILI9488 480×320, SPI |
| Touch | XPT2046 |

Pinbelegung (wie im Schaltplan):

| Signal | GPIO | Signal | GPIO |
|---|---|---|---|
| MOSI | 23 | TFT CS | 15 |
| SCLK | 18 | TFT DC | 2 |
| MISO | 19 | TFT RST | 4 |
| Touch CS | 14 | Backlight (TFT_LED) | **32** |

T_IRQ (IO27) und der Piezo-Treiber (IO26) werden von der Firmware nicht genutzt.

## Installation

### 1. Integration über HACS

1. HACS → ⋮ → **Benutzerdefinierte Repositories** → `https://github.com/hypersonic30/ha-display`, Kategorie **Integration**
2. **ESP32 Display** installieren, Home Assistant neu starten
3. **Einstellungen → Geräte & Dienste → Integration hinzufügen → ESP32 Display** – gefragt wird nur die **Home-Assistant-URL**, unter der die Displays Home Assistant erreichen (vorausgefüllt), z. B. `http://192.168.1.10:8123`
4. In der Integration **Display hinzufügen** – Name (z. B. `Küche`) und Helligkeit. Jedes Display ist ein Unterkonto und bekommt automatisch einen **eigenen Gerätecode**.

Die Karte wird automatisch geladen, ein manueller Ressourcen-Eintrag ist nicht nötig. Jedes Display bekommt außerdem einen Sensor **„Verbunden“** (z. B. für eine Automatisierung „Display offline“).

**Es gibt kein Home-Assistant-Token.** Die Displays reden nur mit der Integration, und die spricht innerhalb von Home Assistant mit allem anderen. Ein Display kennt nur seinen eigenen Gerätecode. Ein gestohlenes Display kann damit höchstens die Elemente bedienen, die in *seinem* Layout liegen (Licht, Schalter, Skripte, Szenen, Taster …) – keine Schlösser, Alarmanlagen oder beliebige Dienste. Mit „Display bearbeiten → Neuen Gerätecode erzeugen“ sperrst du ein Display sofort aus.

### 2. Firmware flashen

Fertige Dateien liegen in [`firmware/bin/`](firmware/bin) und bei den [Releases](https://github.com/hypersonic30/ha-display/releases).

```bash
pip install esptool
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 921600 write_flash \
  0x1000  bootloader.bin \
  0x8000  partitions.bin \
  0x10000 firmware.bin
```

(Windows: Port z. B. `COM3`.) Beim ersten Start kalibrierst du den Touch: die Markierungen an den Ecken antippen. Die Werte werden gespeichert; **beim Einschalten 3 s den Touch halten** kalibriert neu.

### 3. Display einrichten

Ohne gespeicherte Zugangsdaten öffnet das Display einen WLAN-Hotspot. **SSID und Passwort stehen auf dem Display** (WPA2, das Passwort ändert sich bei jedem Start).

1. Mit dem Hotspot `ESP32-Display-XXXX` verbinden
2. In der Karte unter **Einrichtung** den **Einrichtungslink** kopieren und im Browser öffnen (Alternative: `http://192.168.4.1` öffnen und Home-Assistant-URL sowie Gerätecode einfügen)
3. Dein **WLAN** auswählen und das Passwort eingeben – URL und Gerätecode sind über den Link schon eingetragen

Danach verschwindet der Hotspot, das Display verbindet sich und lädt Name, Helligkeit und Layout. Bei Problemen zeigt es den Grund an (WLAN nicht erreichbar, Home Assistant/Integration nicht gefunden, Gerätecode abgelehnt) und versucht es weiter – gespeicherte Daten werden nie automatisch gelöscht. **Touch 3 s halten** auf einem Statusbildschirm öffnet die Einrichtung erneut.

> Tipp: Kopiere den Einrichtungslink, **bevor** du dich mit dem Hotspot verbindest – danach hat dein Handy meist kein Home Assistant mehr.

### 4. Karte hinzufügen

Dashboard bearbeiten → Karte hinzufügen → **ESP32 Display** (oder YAML):

```yaml
type: custom:display-card
device: Küche      # optional: ohne Angabe wählst du das Display in der Karte aus
```

## Layout-Editor

- Element aus der **Palette** ziehen oder anklicken
- Auf dem Display-Abbild verschieben (Raster 5 px), an der Ecke **Größe ändern**, Pfeiltasten verschieben (Shift = 5 px), `Entf` löscht
- Rechts: **Entität**, **Beschriftung**, Einheit, Min/Max, Position und Größe
- Die Vorschau zeigt die **aktuellen Werte** aus Home Assistant
- **Speichern** prüft das Layout in Home Assistant und schickt es sofort ans Display

| Element | Zweck | Bedienung am Display |
|---|---|---|
| Wert | Zustand mit optionaler Einheit | – |
| Taster | leuchtet, wenn `on` | tippen: `button`/`input_button` → drücken, `scene`/`script` → starten, sonst umschalten |
| Anzeige | Ringanzeige mit Min/Max | – |
| Schalter | Ein/Aus | tippen: ein-/ausschalten |
| Balken | Fortschrittsbalken mit Wert | – |

Grenzen: höchstens 32 Elemente, Beschriftung ≤ 31 und Einheit ≤ 15 Bytes (Umlaute und ° zählen doppelt). Umlaute, `°` und `€` werden dargestellt.

## So funktioniert es

```
Lovelace-Karte ──HA-WebSocket──▶ ┐
                                 ├─ Integration „display“ ──▶ Home Assistant (Zustände, Dienste)
ESP32-Display ──eigener WebSocket▶ ┘     /api/display/ws, Anmeldung mit Gerätecode
```

Das Display hält **eine** Verbindung zur Integration. Die schickt Layout und Zustände der Entitäten im Layout (live) und nimmt Taps entgegen. Welcher Dienst dabei aufgerufen wird, entscheidet die Integration anhand des gespeicherten Layouts – das Display nennt nur die Element-ID.

## Fehlersuche

| Anzeige / Verhalten | Ursache |
|---|---|
| „Integration ‚ESP32 Display‘ nicht gefunden“ | URL stimmt, aber die Integration fehlt oder Home Assistant wurde noch nicht neu gestartet |
| „Gerätecode abgelehnt“ | Display wurde gelöscht oder sein Code erneuert → Einrichtungslink erneut verwenden (Touch 3 s halten) |
| Touch trifft daneben | Beim Einschalten 3 s halten und neu kalibrieren |
| Punkt in der Karte bleibt grau | Display nicht verbunden (Sensor „Verbunden“ in HA prüfen) |
| Taster/Schalter reagiert nicht | Der Entitätstyp ist nicht bedienbar (nur Licht, Schalter, Lüfter, Rollladen, Automatisierung, Skript, Szene, Taster) |

## Selbst bauen

```bash
cd firmware && pio run            # Binary: .pio/build/esp32/firmware.bin
```

Die Fonts (Montserrat mit Latin-1 und €) liegen fertig in `firmware/src/fonts/`; neu erzeugen mit `python firmware/tools/gen_fonts.py`.

Tests:

```bash
pip install pytest-homeassistant-custom-component && pytest          # Integration
pip install playwright && python tests/browser/run.py                # Karte (Chrome)
```

## Lizenz

[MIT](LICENSE)
