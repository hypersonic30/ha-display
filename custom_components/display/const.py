DOMAIN = "display"

CONF_HA_URL = "ha_url"
CONF_NAME = "name"
CONF_BRIGHTNESS = "brightness"
CONF_DEVICE_KEY = "device_key"
CONF_NEW_KEY = "new_key"
CONF_ROTATION = "rotation"
CONF_SIDEBAR = "sidebar"
CONF_CLICK = "click"
CONF_HW_MANAGE = "hw_manage"   # Home Assistant owns the hardware settings of this display
CONF_HW = "hw"                 # ... and they are stored here (keys: see hw.py)

SUBENTRY_DEVICE = "device"

STATIC_PATH = "/display/static"
CARD_FILE = "display-card.js"
PANEL_FILE = "display-panel.js"
PANEL_URL = "display-editor"

SCREEN_W = 480        # landscape size; rotation 1 and 3 swap the two
SCREEN_H = 320
MAX_WIDGETS = 32      # per page
MAX_PAGES = 8
PAGER_MODES = ("name", "count")
DEFAULT_ROTATION = 0
DEFAULT_BRIGHTNESS = 80

WIDGET_TYPES = (
    "label", "button", "gauge", "switch", "bar",
    "slider", "arc", "led", "clock", "dropdown", "roller", "checkbox", "panel", "media", "thermostat",
)

# Domains a panel may operate through button/switch widgets (the server maps the tap to a service).
ACTION_DOMAINS = frozenset(
    {
        "light", "switch", "input_boolean", "fan", "automation", "script", "scene", "button", "input_button", "cover",
        "media_player", "climate", "vacuum",
    }
)
# Domains a switch/checkbox may flip. Locks and alarm panels are deliberately not operable from a panel.
TOGGLE_DOMAINS = frozenset({"light", "switch", "input_boolean", "fan", "automation", "cover", "media_player"})
SLIDER_DOMAINS = frozenset({"light", "cover", "fan", "media_player", "input_number", "number"})
SELECT_DOMAINS = frozenset({"select", "input_select", "media_player"})

# Fixed actions of a button, per domain (the first listed is what an empty action does where one is allowed).
BUTTON_ACTIONS = {
    "media_player": ("play_pause", "play", "pause", "stop", "next", "prev", "volume_up", "volume_down", "mute"),
    "cover": ("toggle", "open", "close", "stop"),
    "climate": ("temp_up", "temp_down"),
    "vacuum": ("start", "pause", "return_home"),
}
DEFAULT_ACTION = {"media_player": "play_pause", "cover": "toggle", "vacuum": "start"}
MEDIA_ACTIONS = ("prev", "play_pause", "next")
THERMOSTAT_ACTIONS = ("temp_up", "temp_down")
FONT_SIZES = (0, 12, 14, 16, 20, 24, 32, 48)   # 0 = automatic; 32/48 are digit-only clock fonts
CLOCK_FORMATS = ("time", "time_s", "date", "datetime")
ICONS = (
    "power", "home", "bell", "play", "pause", "stop", "next", "prev", "plus", "minus", "up", "down", "left", "right",
    "ok", "close", "settings", "refresh", "wifi", "battery", "charge", "warning", "eye", "volume", "mute", "audio",
    "video", "list", "trash", "edit", "gps", "image", "shuffle", "loop", "bluetooth", "usb",
)
# Smallest size per type (w, h); the default is 20x20.
MIN_SIZE = {"media": (200, 100), "thermostat": (140, 90), "arc": (60, 60)}
OPTIONS_MAX = 16
OPTION_BYTES = 31

# Buzzer: tone sequences are lists of [frequency Hz, duration ms]; frequency 0 is a pause. The firmware clamps
# again (firmware/include/beep.h), these are the same limits.
BEEP_MIN_HZ = 100
BEEP_MAX_HZ = 5000
BEEP_MIN_MS = 10
BEEP_MAX_STEP_MS = 2000
BEEP_MAX_STEPS = 16
BEEP_MAX_TOTAL_MS = 5000
BEEP_MAX_REPEAT = 10
BEEP_GAP_MS = 150             # pause between repeats
BEEP_DEFAULT_HZ = 2000
BEEP_DEFAULT_MS = 200
BEEP_PRESETS = {
    "click": [[2500, 20]],
    "notify": [[1800, 120], [0, 60], [2400, 160]],
    "alert": [[2800, 200], [0, 100], [2800, 200], [0, 100], [2800, 200]],
    "doorbell": [[1568, 250], [0, 30], [1175, 400]],
    "success": [[1500, 90], [0, 40], [2000, 90], [0, 40], [2600, 140]],
    "error": [[400, 300], [0, 80], [300, 500]],
}
