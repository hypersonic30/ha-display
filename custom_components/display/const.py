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

WIDGET_TYPES = ("label", "button", "gauge", "switch", "bar")

# Domains a panel may operate through button/switch widgets (the server maps the tap to a service).
ACTION_DOMAINS = frozenset(
    {"light", "switch", "input_boolean", "fan", "automation", "script", "scene", "button", "input_button", "cover"}
)
