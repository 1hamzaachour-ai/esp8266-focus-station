#pragma once
#include "Arduino.h"

// ESP8266-style emulated EEPROM: starts erased (0xFF), changes persist across a simulated reboot.
struct FakeEEPROM {
  uint8_t data[64];
  int commits = 0;
  FakeEEPROM() { memset(data, 0xFF, sizeof data); }
  void begin(size_t) {}
  template <class T> T& get(int addr, T& t) { memcpy(&t, data + addr, sizeof(T)); return t; }
  template <class T> const T& put(int addr, const T& t) { memcpy(data + addr, &t, sizeof(T)); return t; }
  bool commit() { commits++; return true; }
};
inline FakeEEPROM EEPROM;
