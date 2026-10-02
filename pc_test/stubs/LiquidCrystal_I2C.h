#pragma once
#include "Arduino.h"

// Keeps a 16x2 character buffer so tests can read what the LCD shows.
// Custom glyphs 1..5 are stored as bytes 1..5.
struct LiquidCrystal_I2C {
  uint8_t addr, cols, rows, col = 0, row = 0;
  char screen[2][17];
  bool lit = false;
  int initCalls = 0;
  int clearCalls = 0;
  long calls = 0;  // every bus operation, to prove an absent LCD is left alone
  LiquidCrystal_I2C(uint8_t a, uint8_t c, uint8_t r) : addr(a), cols(c), rows(r) {
    memset(screen, ' ', sizeof screen); screen[0][16] = screen[1][16] = 0;
  }
  void init() { calls++; initCalls++; }
  void backlight() { calls++; lit = true; }
  void clear() { calls++; clearCalls++; memset(screen, ' ', sizeof screen); screen[0][16] = screen[1][16] = 0; }
  void createChar(uint8_t, uint8_t[]) { calls++; }
  void setCursor(uint8_t c, uint8_t r) { calls++; col = c; row = r; }
  size_t write(uint8_t ch) { calls++; if (row < 2 && col < 16) screen[row][col] = (char)ch; col++; return 1; }
  size_t print(char ch) { return write((uint8_t)ch); }
  size_t print(const char* s) { size_t n = 0; while (*s) n += write((uint8_t)*s++); return n; }
  std::string line(int r) const { return std::string(screen[r], 16); }
};
