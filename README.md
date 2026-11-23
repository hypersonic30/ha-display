# ESP32 Display – Home Assistant Panel

Fertige Firmware + HA-Integration + Lovelace-Card für ein selbstgebautes ESP32-Panel mit ILI9488-Touchdisplay.  
Widgets werden **ohne Reflashing** direkt in Home Assistant grafisch zugewiesen.

---

## Hardware

| Bauteil | Wert |
|---|---|
| MCU | ESP32-WROOM-32D |
| Display | ILI9488 480×320 SPI TFT |
| Touch | XPT2046 |
| Backlight | GPIO 35 (PWM) |

**SPI-Pinbelegung**

| Signal | GPIO |
|---|---|
| MOSI | 23 |
| SCLK | 18 |
| MISO | 19 |
| TFT CS | 15 |
| TFT DC | 2 |
| TFT RST | 4 |
| Touch CS | 14 |

---

## Features

- **5 Widget-Typen** – Label, Button, Gauge, Switch, Bar
- **Visueller Editor** in Home Assistant (Lovelace Card) – Widgets per Drag & Drop platzieren
- **Ersteinrichtung per Hotspot** – WLAN + HA-Zugangsdaten über Web-GUI eingeben, kein Reflashing
- **Live-Updates** über HA WebSocket API – Zustände sofort auf dem Display
- **Touch-Steuerung** – Button/Switch sendet HA-Service-Calls direkt vom Panel
- **HACS-kompatibel** – Installation als Custom Integration

---

## Installation

### 1. ESP32 flashen

#### Option A – Fertige Firmware (empfohlen)

Benötigt: Python 3 + `esptool`

```bash
pip install esptool
```

Dann flashen (Port anpassen, z. B. `COM3` unter Windows oder `/dev/ttyUSB0` unter Linux):

```bash
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 921600 \
  write_flash \
  0x1000  firmware/bin/bootloader.bin \
  0x8000  firmware/bin/partitions.bin \
  0x10000 firmware/bin/firmware.bin
```

Die fertige `firmware.bin` findet sich auch unter [Releases](https://github.com/hypersonic30/ha-display/releases).

#### Option B – Selbst kompilieren (PlatformIO)

```bash
cd firmware
pio run --target upload
```

Benötigt PlatformIO (`pip install platformio`) und die Datei `firmware/include/secrets.h` (optional, nur für compile-time-Defaults).

---

### 2. HA-Integration über HACS installieren

1. **HACS** → ⋮ → **Custom Repositories**
   - URL: `https://github.com/hypersonic30/ha-display`
   - Kategorie: **Integration**
2. **HACS → Integrationen → ESP32 Display → Installieren**
3. **Home Assistant neu starten**
4. **Einstellungen → Geräte & Dienste → Integration hinzufügen → ESP32 Display**
   - Gerätename eintragen (muss mit dem Panel übereinstimmen, z. B. `Küche`)
   - HA-URL und Long-Lived Access Token

Die Lovelace-Card wird automatisch mit geladen – kein manueller Ressource-Eintrag nötig.

---

### 3. Lovelace-Card hinzufügen

Dashboard bearbeiten → **Card hinzufügen** → `Benutzerdefiniert` → `ESP32 Display`:

```yaml
type: custom:display-card
device: Küche
```

Oder im UI-Editor: Suche nach **ESP32 Display**.

---

## Ersteinrichtung am Panel

Beim ersten Start (keine gespeicherten Zugangsdaten) öffnet der ESP32 automatisch einen Hotspot:

```
SSID: ESP32-Display-XXYY
```

1. Mit dem Hotspot verbinden
2. Browser öffnen: **http://192.168.4.1**
3. WLAN-Netzwerk auswählen, Passwort, HA-URL (`http://192.168.1.x:8123`) und Token eingeben
4. Speichern – das Panel verbindet sich und zeigt „Layout wird geladen…"

Nach erfolgreicher Verbindung verschwindet der Hotspot automatisch.

> Bei falscher WLAN-Konfiguration startet das Panel nach 20 s neu und öffnet wieder den Hotspot.

---

## Layout-Editor

Im Lovelace-Editor ist ein **480×320-Canvas** zu sehen (skaliert auf die verfügbare Breite).

### Widgets hinzufügen

- Widget aus der **Palette** (links) auf den Canvas ziehen **oder** anklicken
- Widget positionieren durch Ziehen direkt auf dem Canvas

### Widget konfigurieren

Klick auf ein Widget öffnet das **Properties-Panel** (rechts):

| Feld | Beschreibung |
|---|---|
| Entität | HA-Entity-ID (gefiltert nach Widget-Typ) |
| Bezeichnung | Anzeigename auf dem Panel |
| Einheit | z. B. `°C`, `%`, `W` |
| Min / Max | Wertebereich für Gauge und Bar |
| Service-Domain | Domain für Button/Switch (z. B. `light`, `switch`) |
| X / Y / Breite / Höhe | Position und Größe in Pixeln |

### Speichern

**Speichern** überträgt das Layout an die HA-Integration. Das Panel lädt es automatisch ohne Neustart.

---

## Widget-Typen

| Typ | Beschreibung | Empfohlene Entitäten |
|---|---|---|
| `label` | Textwert mit optionaler Einheit | `sensor.*`, `input_number.*` |
| `button` | Schaltfläche, sendet `turn_on` / `turn_off` | `light.*`, `switch.*`, `script.*` |
| `gauge` | Kreisanzeige mit Min/Max | `sensor.*`, `input_number.*` |
| `switch` | Toggle-Schalter | `light.*`, `switch.*`, `input_boolean.*` |
| `bar` | Balkenanzeige mit Prozentwert | `sensor.*`, `input_number.*` |

---

## Architektur

```
[HA Lovelace Card]  ──POST /api/display/layout/{device}──►  [HA Integration]
                    ◄─GET  /api/display/layout/{device}──   [HA Integration]
[ESP32 Firmware]    ──► ws://ha:8123/api/websocket ──────►  [HA WebSocket API]
[ILI9488 Display]   ◄── LVGL Widget Rendering
```

- **ESP32-Firmware**: PlatformIO + Arduino, LVGL 8.4, TFT_eSPI
- **HA-Integration**: Python, `HomeAssistantView` REST-Endpoints, `Store` für Layout-Persistenz
- **Lovelace-Card**: Vanilla JS, kein Framework, Shadow DOM

---

## Selbst kompilieren

### Firmware

```bash
cd firmware
cp include/secrets.h.example include/secrets.h  # optional
pio run
# Binary: firmware/.pio/build/esp32/firmware.bin
```

Flash-Größe nach dem Kompilieren: ~75 % (1,47 MB / 1,97 MB), RAM: ~38 %.

### Integration testen

```bash
cd custom_components
python -m pytest  # sofern pytest-homeassistant-custom-component installiert
```

---

## Anforderungen

- Home Assistant ≥ 2025.4.0
- HACS (für automatische Installation)
- ESP32-WROOM-32D mit ILI9488 480×320 + XPT2046 (siehe Pinbelegung oben)

---

## Lizenz

[MIT](LICENSE)
