// Host-side stand-in for the Arduino core. Used only by the host tests.
#pragma once

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Mock clock, defined in test_main.cpp. delay() advances it.
extern uint32_t g_mockMillis;

inline uint32_t millis() { return g_mockMillis; }
inline void delay(uint32_t ms) { g_mockMillis += ms; }
