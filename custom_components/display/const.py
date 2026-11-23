DOMAIN = "display"

CONF_HA_URL = "ha_url"
CONF_NAME = "name"
CONF_BRIGHTNESS = "brightness"
CONF_DEVICE_KEY = "device_key"
CONF_NEW_KEY = "new_key"
CONF_ROTATION = "rotation"

SUBENTRY_DEVICE = "device"

STATIC_PATH = "/display/static"
CARD_FILE = "display-card.js"

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
