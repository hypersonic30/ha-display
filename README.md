# ESP32 Display – Home-Assistant-Panel

Selbstgebautes Touch-Panel (ESP32 + ILI9488 480×320) für Home Assistant. Das Layout gestaltest du **grafisch in Home Assistant** – ohne die Firmware neu zu flashen. Änderungen erscheinen sofort auf dem Display.

- **Eine Integration, ein Token, beliebig viele Displays** – Geräte sind Unterkonten der Integration
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
3. **Einstellungen → Geräte & Dienste → Integration hinzufügen → ESP32 Display**
   - **Home-Assistant-URL**: so erreichen die Displays Home Assistant (vorausgefüllt), z. B. `http://192.168.1.10:8123`
   - **Langlebiges Zugriffstoken**: Profil → Sicherheit → *Langlebige Zugriffstoken*. Dieses **eine** Token tippst du später in jedes Display ein.
4. In der Integration **Display hinzufügen** – Name (z. B. `Küche`) und Helligkeit. Das ist ein Unterkonto; weitere Displays fügst du genauso hinzu.

Die Karte wird automatisch geladen, ein manueller Ressourcen-Eintrag ist nicht nötig. Jedes Display bekommt außerdem einen Sensor **„Verbunden“** (z. B. für eine Automatisierung „Display offline“).

> **Tipp zur Sicherheit:** Lege das Token für einen eigenen Benutzer **ohne Administrator-Rechte** an (Einstellungen → Personen → Benutzer). Das Token liegt im Flash der Displays; so kann damit niemand mehr als nötig tun. Die Integration prüft das Token lokal und fragt neu danach (Reparatur-Hinweis), falls es gelöscht wurde.

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

1. Mit dem Hotspot `ESP32-Display-XXXX` verbinden, im Browser `http://192.168.4.1` öffnen
2. WLAN wählen und Passwort eingeben
3. **Gerätename** – genau der Name aus Schritt 1.4 (Groß-/Kleinschreibung, Leerzeichen und Umlaute sind egal: `kuche` findet `Küche`)
4. **Home-Assistant-URL** und **Token** – beides zeigt dir die Karte unter **Einrichtung** an (zum Kopieren)

Danach verschwindet der Hotspot, das Display verbindet sich und lädt sein Layout. Bei Problemen zeigt es den Grund an (WLAN nicht erreichbar, Token ungültig, Gerät in HA nicht angelegt) und versucht es weiter – gespeicherte Daten werden nie automatisch gelöscht. **Touch 3 s halten** auf einem Statusbildschirm öffnet die Einrichtung erneut.

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
Lovelace-Karte ──WebSocket──▶ Integration (speichert Layouts, prüft sie)
                                   │  display/layout/subscribe (Live-Push)
ESP32-Firmware ◀──WebSocket────────┘  + Zustände per subscribe_entities, Aktionen per call_service
```

Die Firmware hält **eine** WebSocket-Verbindung zu Home Assistant. Layout-Änderungen und Zustände kommen darüber live an, Touch-Aktionen gehen als Service-Aufruf zurück.

## Fehlersuche

| Anzeige / Verhalten | Ursache |
|---|---|
| „Gerät … ist in Home Assistant nicht angelegt“ | Name am Display ≠ Name in der Integration |
| „Token ungültig“ | Token gelöscht oder falsch kopiert; neues Token in Integration (Reparatur-Hinweis) **und** am Display (Touch 3 s halten) eintragen |
| Touch trifft daneben | Beim Einschalten 3 s halten und neu kalibrieren |
| Punkt in der Karte bleibt grau | Display nicht verbunden (Sensor „Verbunden“ in HA prüfen) |

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
