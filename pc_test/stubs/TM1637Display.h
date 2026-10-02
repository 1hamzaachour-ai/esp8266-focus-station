#pragma once
#include "Arduino.h"

struct TM1637Display {
  uint8_t clk, dio;
  int value = -1;
  uint8_t dots = 0;
  bool leadingZero = false;
  uint8_t brightness = 0;
  TM1637Display(uint8_t c, uint8_t d, unsigned int = 100) : clk(c), dio(d) {}
  void setBrightness(uint8_t b, bool = true) { brightness = b; }
  void showNumberDecEx(int num, uint8_t d = 0, bool lz = false, uint8_t = 4, uint8_t = 0) {
    value = num; dots = d; leadingZero = lz;
  }
  int allOnCalls = 0;
  void setSegments(const uint8_t s[], uint8_t = 4, uint8_t = 0) { if (s[0] == 0xFF) allOnCalls++; }
  bool colon() const { return dots & 0b01000000; }
};
