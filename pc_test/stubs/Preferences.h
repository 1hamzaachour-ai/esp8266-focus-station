#pragma once
#include "Arduino.h"

inline std::map<std::string, uint16_t> g_nvs;   // survives a simulated reboot

struct Preferences {
  bool begin(const char*, bool) { return true; }
  uint16_t getUShort(const char* k, uint16_t def) { auto it = g_nvs.find(k); return it == g_nvs.end() ? def : it->second; }
  size_t putUShort(const char* k, uint16_t v) { g_nvs[k] = v; return 2; }
};
