/*
  I2C scanner for the Focus Station (ESP32: SDA 21, SCL 22 | NodeMCU ESP8266: SDA D2, SCL D1).
  Upload, open Serial Monitor at 115200 baud, and put the address it prints
  into LCD_ADDR in focus_station.ino (usually 0x27 or 0x3F).
*/

#include <Wire.h>

#if defined(ESP8266)
const uint8_t PIN_SDA = 4, PIN_SCL = 5;    // D2, D1
#else
const uint8_t PIN_SDA = 21, PIN_SCL = 22;
#endif

void setup() {
  Serial.begin(115200);
  delay(500);
  Wire.begin(PIN_SDA, PIN_SCL);
  Serial.printf("\nI2C scanner (SDA GPIO %u, SCL GPIO %u)\n", PIN_SDA, PIN_SCL);
}

void loop() {
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  device found at 0x%02X\n", addr);
      found++;
    }
  }
  if (found == 0) Serial.println("  nothing found: check SDA/SCL wiring, power and ground");
  Serial.println("---");
  delay(3000);
}
