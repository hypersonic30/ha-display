# WLAN wechseln (Firmware ab 0.6.0)

Das Display speichert bis zu **3 WLANs** (zum Beispiel Zuhause und der Handy-Hotspot). Beim Start und nach einem
Verbindungsverlust probiert es sie der Reihe nach, zuerst das mit dem besten Empfang (15 s je Netz). Ein WLAN aus
einer älteren Firmware bleibt beim Update erhalten.

## Die Einrichtungsseite öffnen

Das Display öffnet dazu einen eigenen Hotspot (Name `ESP32-Display-XXXX`, Passwort wird auf dem Display angezeigt).
Mit dem Handy verbinden, dann öffnet sich die Seite von selbst (sonst `192.168.4.1` im Browser).

Es gibt zwei Wege dorthin:

1. **Automatisch:** Ist kein gespeichertes WLAN erreichbar, öffnet das Display den Hotspot nach **90 Sekunden**. Das
   gilt beim Start genauso wie nach einem Verbindungsverlust im Betrieb. Die gespeicherten WLANs werden dabei weiter
   versucht. Wird eines erreichbar, schließt sich der Hotspot wieder. Mit „Abbrechen“ auf dem Display kann man ihn
   schließen, er kommt nach weiteren 90 Sekunden ohne WLAN erneut.
2. **Von Hand:** **3 Sekunden** auf eine **leere Stelle** der Seite drücken (nicht auf einen Schalter, Schieber,
   Button, das Seitenumschalt-Element oder ein Dropdown; ein Wischen zählt nicht). Anzeigeelemente wie Label,
   Balken, Gauge, LED, Uhr und Fläche gelten als leer. Das Display zeigt dann sofort den Hotspot mit „Abbrechen“.
   Ohne Eingabe läuft es nach 5 Minuten normal weiter, ein versehentlicher Druck ist also harmlos.
   Auf Seiten ohne eine freie Stelle hilft Weg 1: Router bzw. Hotspot aus und 90 Sekunden warten.
   Auf Statusbildschirmen (Verbinde …, Fehler, Warten auf Layout) reicht ein Druck an irgendeiner Stelle.

## Die Seite

- **Gespeicherte WLANs:** Liste mit „Entfernen“. Passwörter werden nie angezeigt.
- **WLAN hinzufügen:** Name (SSID) **frei eintippen** oder aus der Suchliste wählen (ins Feld tippen zeigt die Liste),
  Passwort eingeben (leer = offenes WLAN, sonst 8 bis 63 Zeichen), „Hinzufügen“ (höchstens 3; gleicher Name ersetzt
  das Passwort). Der Name darf bis zu 32 Bytes lang sein. Versteckte Netze und Netze, die gerade nicht in Reichweite
  sind, gehen so auch. Gespeichert wird ohne Verbindungsprobe.
- **Home Assistant:** URL ist vorausgefüllt. Der Gerätecode wird nicht angezeigt: leer lassen behält ihn.
- „Verbinden & Speichern“ übernimmt alles und verbindet sofort, ohne Neustart.

Läuft der Hotspot gleichzeitig mit einem Verbindungsversuch, kann die Netzsuche kurz leer bleiben (nochmal auf ↻).

## Handy-Hotspot (iPhone/Android)

Ein iPhone schaltet seinen **Persönlichen Hotspot ab**, sobald du dich mit dem WLAN des Displays verbindest. Der
Hotspot taucht deshalb auch nie in der Netzsuche auf. So geht es:

1. Auf der Einrichtungsseite den Hotspot-Namen (SSID) und sein Passwort von Hand eintippen, „Hinzufügen“,
   „Verbinden & Speichern“.
2. Danach den Hotspot am Handy wieder einschalten. Beim iPhone *Einstellungen → Persönlicher Hotspot* geöffnet lassen
   (sonst ist der Hotspot für andere Geräte oft nicht sichtbar) und **„Kompatibilität maximieren“** einschalten: der
   ESP32 funkt nur auf 2,4 GHz.
3. Das Display verbindet sich von selbst. Nach dem Speichern wartet es **5 Minuten** (statt 90 Sekunden), bevor es
   seinen eigenen Hotspot wieder öffnet. So bleibt Zeit zum Umschalten, und das Handy hängt sich nicht wieder an das
   Display-WLAN.

Ist das Handy-WLAN später aus, öffnet das Display nach 90 Sekunden wieder sein Einrichtungs-WLAN.
