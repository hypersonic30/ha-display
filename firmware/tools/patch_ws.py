# PlatformIO pre-script: the WebSockets library hard-codes its max message size;
# make it overridable so -D WEBSOCKETS_MAX_DATA_SIZE takes effect.
Import("env")
import glob, os

root = env.subst("$PROJECT_LIBDEPS_DIR")
for path in glob.glob(os.path.join(root, "*", "WebSockets", "src", "WebSockets.h")):
    text = open(path).read()
    old = "#define WEBSOCKETS_MAX_DATA_SIZE (15 * 1024)"
    new = "#ifndef WEBSOCKETS_MAX_DATA_SIZE\n" + old + "\n#endif"
    if old in text and new not in text:
        open(path, "w").write(text.replace(old, new))


# The ESP32 core can verify against its built-in root bundle (NetworkClientSecure::useBuiltinCACertBundle), but
# the library only offers its own bundle pointer. A sentinel pointer (1) means "use the built-in bundle".
for path in glob.glob(os.path.join(root, "*", "WebSockets", "src", "WebSocketsClient.cpp")):
    text = open(path).read()
    old = "                _client.ssl->setCACertBundle(_CA_bundle, _CA_bundle_size);"
    new = ("                if(_CA_bundle == (const uint8_t *)1) _client.ssl->useBuiltinCACertBundle();\n"
           "                else _client.ssl->setCACertBundle(_CA_bundle, _CA_bundle_size);")
    if old in text and new not in text:
        open(path, "w").write(text.replace(old, new))
