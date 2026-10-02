// Minimal fake Arduino/ESP32 environment so focus_station.ino runs on a PC.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
using std::min;
using std::max;

typedef uint8_t byte;

#if defined(ESP8266)
#define RF_DISABLED 4
#define RF_MODE(mode) int __get_rf_mode() { return mode; }
#define RF_PRE_INIT() void __run_user_rf_pre_init()
#endif
#define HIGH 1
#define LOW 0
#define INPUT 0x01
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05

// ---- simulated clock + pins ----
inline uint32_t g_now = 0;
inline int g_pinLevel[40];          // what the outside world drives (button)
inline int g_pinOut[40];            // what the firmware writes
inline int g_pinMode[40];
inline int g_risingEdges[40];       // count LOW->HIGH writes (buzzer beeps)

inline uint32_t millis() { return g_now; }
inline void delay(uint32_t ms) { g_now += ms; }
inline void pinMode(uint8_t p, uint8_t m) { g_pinMode[p] = m; if (m == INPUT_PULLUP) g_pinLevel[p] = HIGH; }
inline int digitalRead(uint8_t p) { return g_pinLevel[p]; }
inline void digitalWrite(uint8_t p, uint8_t v) {
  if (v && !g_pinOut[p]) g_risingEdges[p]++;
  g_pinOut[p] = v ? 1 : 0;
}
inline int g_toneCalls = 0;
inline void tone(uint8_t, unsigned int) { g_toneCalls++; }
inline void noTone(uint8_t) {}

// ---- Serial ----
struct FakeSerial {
  std::vector<std::string> lines;
  std::string partial;
  void begin(unsigned long) {}
  void add(const char* s) {
    for (const char* c = s; *c; c++) {
      if (*c == '\n') { lines.push_back(partial); partial.clear(); } else partial += *c;
    }
  }
  void println(const char* s) { add(s); add("\n"); }
  void printf(const char* fmt, ...) {
    char buf[256]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap); add(buf);
  }
  bool saw(const char* needle) const {
    for (auto& l : lines) if (l.find(needle) != std::string::npos) return true;
    return false;
  }
};
inline FakeSerial Serial;
