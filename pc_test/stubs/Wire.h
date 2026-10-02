#pragma once
#include "Arduino.h"

inline bool g_lcdPresent = true;
inline int  g_wireSda = -1, g_wireScl = -1;

struct TwoWire {
  uint8_t addr = 0;
  bool begin(int sda, int scl) { g_wireSda = sda; g_wireScl = scl; return true; }
  void beginTransmission(uint8_t a) { addr = a; }
  uint8_t endTransmission() { return g_lcdPresent ? 0 : 2; }
};
inline TwoWire Wire;
