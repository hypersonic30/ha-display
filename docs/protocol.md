# Panel protocol (v0.4)

WebSocket `/api/display/ws`, JSON text frames. Panel -> server: `hello`, `tap`, `set`, `act`, `page`, `ota_status`.
Server -> panel: `layout`, `states`, `options`, `time`, `ota`, `error`. Unknown message types and unknown
widget fields must be ignored by the firmware.

## Layout frame

```
{"type":"layout","name":..,"rev":..,"settings":{"brightness":..,"rotation":..},"screen":{"w":..,"h":..},
 "page":{"index":..,"count":..,"name":..,"bg":"#rrggbb"?}, "pager":{...}|null, "widgets":[...]}
```

`page.bg` is only present when the page has its own background colour (default: the built-in dark theme).

## Widget object (wire format)

The server only sends fields that differ from their default, so the firmware must treat a missing field as the
default. Always present: `id`, `type`, `x`, `y`, `w`, `h`, `min`, `max`.

| field    | default | meaning |
|----------|---------|---------|
| `entity` | `""`    | entity id |
| `label`  | `""`    | title text (<= 31 bytes UTF-8) |
| `unit`   | `""`    | unit text (<= 15 bytes) |
| `min`,`max` | 0, 100 | numeric range |
| `bg`     | theme   | `"#rrggbb"`: fill / track / "off" colour |
| `fg`     | theme   | text colour |
| `on`     | accent  | colour while the widget is *active* / indicator colour (see below) |
| `font`   | `0`     | font size of the widget's main text: `0` = automatic (as before), else a pixel size from `12, 14, 16, 20, 24`; `clock` additionally `32, 48` (digits only) |
| `attr`   | `""`    | show this attribute instead of the state (label/bar/gauge; `[a-z0-9_]{1,23}`) |
| `icon`   | `""`    | symbol name (table below), button and label |
| `action` | `""`    | fixed action of a button (server side only; the firmware just sends `act`) |
| `format` | `"time"`| clock only: `time` HH:MM, `time_s` HH:MM:SS, `date` DD.MM.YYYY, `datetime` DD.MM. HH:MM |
| `warn`,`crit` | absent | numbers: bar/gauge indicator turns amber at `warn`, red at `crit` |
| `invert` | `false` | with `warn`/`crit`: trigger when the value is `<=` the threshold instead of `>=` |

Types: `label`, `button`, `gauge`, `switch`, `bar` (existing) and new `slider`, `arc`, `led`, `clock`, `dropdown`,
`roller`, `checkbox`, `panel`, `media`, `thermostat`.

### Font size

`font` sets the size of the *main* text: the value of a `label`, the text of `button`/`checkbox`/`switch`/`dropdown`/`roller`,
the title of `media`, the target temperature of `thermostat`, the digits of `clock` (default 32). Secondary texts (small
titles, artist, unit hints) stay automatic. Only the listed sizes exist (symbols are available in 12..24); the firmware
picks the largest available size <= the value if it has no exact font. The server only accepts 32/48 for `clock`.

### Active state

A widget is *active* when its entity state (lower case) is one of: `on`, `open`, `opening`, `closing`, `playing`,
`home`, `unlocked`, `heat`, `cool`, `heat_cool`, `auto`, `active`, `detected`, `armed_home`, `armed_away`,
`cleaning`. Active widgets use the `on` colour, inactive ones the `bg` colour.

### Per type

| type | entity | look | input |
|------|--------|------|-------|
| `label` | any | title `label` (small), value + `unit` (large; value from state or `attr`); optional `icon` before the title; `bg`/`fg` | none |
| `button` | action domains | `icon` and/or `label`; `bg` off, `on` when active, `fg` text | `tap` |
| `switch` | togglable | LVGL switch + `label`; `on` = track colour when on; `bg` track off | `set{on}` |
| `checkbox` | togglable | LVGL checkbox with `label`; `on` = tick/box colour | `set{on}` |
| `led` | any | filled circle (w x h, shown as circle of min(w,h)), `bg` off, `on` active; `label` small text beside/below if it fits | none |
| `bar` | sensor | horizontal bar + value text, range `min`..`max`, indicator `on` (default accent), `warn`/`crit` | none |
| `gauge` | sensor | arc gauge, display only, same colours as bar | none |
| `slider` | slider domains | horizontal slider (knob), value text, range `min`..`max`; `on` indicator, `bg` track | `set{value}` |
| `arc` | slider domains | round slider, value in the middle | `set{value}` |
| `dropdown` | select-like | LVGL dropdown; list = `options` frame; shown text = current value | `set{option}` |
| `roller` | select-like | LVGL roller, same data as dropdown | `set{option}` |
| `clock` | none | local time/date from `time` frames, big text, `fg`/`bg` | none |
| `panel` | none | plain rounded rectangle (`bg`, optional `label` top-left small), drawn *below* later widgets; no input | none |
| `media` | media_player | composite: title, artist, buttons prev / play-pause / next, volume slider; see below | `act`, `set{value}` |
| `thermostat` | climate | composite: current temperature (small), target temperature (large) with `-` / `+` buttons, mode text | `act` |

Z-order = list order (first widget is drawn first / bottom). Minimum sizes enforced by the server: `media` 200x100,
`thermostat` 140x90, `dropdown`/`roller`/`slider`/`checkbox`/`switch` 20 (as before), `arc` 60x60.

## States

`{"type":"states","states":{KEY: "text <= 47 bytes"}}` - a snapshot after the layout and single changes later.
`KEY` is `entity_id` (the state, `"unavailable"` when unknown) or `entity_id|name` (a derived value, `""` when it
does not exist). Which keys a widget reads:

| widget | keys |
|--------|------|
| label, bar, gauge, led, button, switch, checkbox | `entity` (state), or `entity|attr` when `attr` is set (value only, active state still from `entity`) - the server sends **both** keys when `attr` is set |
| slider, arc | `entity|value`: number in the widget's `min`..`max` scale as text (`"37"`, `"21.5"`), `"0"` when off |
| dropdown, roller | `entity|option`: the currently selected option |
| media | `entity` (state), `entity|media_title`, `entity|media_artist`, `entity|value` (volume 0..100) |
| thermostat | `entity` (hvac mode / state), `entity|current_temperature`, `entity|temperature` (target) |

Numbers arrive as plain decimal text with at most one decimal for temperatures. The firmware displays them as received.

## Options frame (dropdown, roller)

`{"type":"options","widget":"id","items":["A","B",..]}` - at most 16 items, each <= 31 bytes. Sent after the layout
and whenever the list changes. Item text is the value sent back in `set{option}`.

## Time frame

`{"type":"time","epoch":1732450000,"offset":3600}` - UTC seconds and the local UTC offset in seconds (includes DST).
Sent after the layout and every 5 minutes, but only while the page has a `clock` widget. The firmware keeps a local clock (epoch + offset + millis since the
frame); no NTP. Until the first frame a clock shows `--:--`.

## Panel -> server

- `{"type":"tap","widget":id}` - button.
- `{"type":"set","widget":id,"on":true}` - switch, checkbox.
- `{"type":"set","widget":id,"value":37}` - slider, arc, media volume. Send on release and, while dragging, at most
  every 250 ms. Value is a number in the widget's `min`..`max` scale.
- `{"type":"set","widget":id,"option":"text"}` - dropdown, roller (on change).
- `{"type":"act","widget":id,"action":"prev"}` - media: `prev`, `play_pause`, `next`; thermostat: `temp_up`, `temp_down`.
- `{"type":"page","dir":"next"|"prev"}` and `ota_status` as before.

The server decides what each message does from the stored layout; a panel can only operate the widgets it was given.

## Icons

`icon` value -> LVGL symbol: `power` LV_SYMBOL_POWER, `home` HOME, `bell` BELL, `play` PLAY, `pause` PAUSE, `stop` STOP,
`next` NEXT, `prev` PREV, `plus` PLUS, `minus` MINUS, `up` UP, `down` DOWN, `left` LEFT, `right` RIGHT, `ok` OK,
`close` CLOSE, `settings` SETTINGS, `refresh` REFRESH, `wifi` WIFI, `battery` BATTERY_FULL, `charge` CHARGE,
`warning` WARNING, `eye` EYE_OPEN, `volume` VOLUME_MAX, `mute` MUTE, `audio` AUDIO, `video` VIDEO, `list` LIST,
`trash` TRASH, `edit` EDIT, `gps` GPS, `image` IMAGE, `shuffle` SHUFFLE, `loop` LOOP, `bluetooth` BLUETOOTH, `usb` USB.
