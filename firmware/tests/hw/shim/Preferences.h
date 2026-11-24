#pragma once
#include <Arduino.h>
struct Preferences { bool begin(const char*, bool){return true;} void end(){} size_t getBytesLength(const char*){return 0;} size_t getBytes(const char*, void*, size_t){return 0;} size_t putBytes(const char*, const void*, size_t n){return n;} bool getBool(const char*, bool d){return d;} uint8_t getUChar(const char*, uint8_t d){return d;} void putBool(const char*, bool){} void putUChar(const char*, uint8_t){} void remove(const char*){} };
