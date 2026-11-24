#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
struct SerialT { template<class... A> void printf(const char*, A...) {} void println(const char*) {} } static Serial;
static inline uint32_t millis() { return 0; }
static inline void delay(int) {}
static inline void pinMode(int,int) {}
static inline int digitalRead(int) { return 1; }
#define INPUT_PULLUP 2
#define LOW 0
struct EspT { void restart() {} } static ESP;
