// Runs the real focus_station.ino against fake hardware and checks it against
// the "ESP32 Focus Station — Technical Build Guide" (sections 3, 6, 7, 8, 11).
#include "Arduino.h"
#include "focus_station.ino"

static int g_pass = 0, g_fail = 0;
#define CHECK(name, cond) do { if (cond) { g_pass++; printf("  PASS  %s\n", name); } \
                               else { g_fail++; printf("  FAIL  %s   (test.cpp:%d)\n", name, __LINE__); } } while (0)

// LCD checks only apply when the sketch is built with USE_LCD 1.
#define LCD_CHECK(name, cond) do { if (USE_LCD) CHECK(name, cond); } while (0)

static void run(uint32_t ms) { for (uint32_t i = 0; i < ms; i++) { g_now++; loop(); } }

static uint32_t waitMode(Mode m, uint32_t maxMs) {
  for (uint32_t i = 0; i < maxMs; i++) { run(1); if (mode == m) return g_now; }
  return 0;
}

static void press(uint32_t holdMs) { g_pinLevel[PIN_BUTTON] = LOW; run(holdMs); g_pinLevel[PIN_BUTTON] = HIGH; }

#if defined(ESP8266)
static const char* BOARD = "NodeMCU ESP8266";
static const int X_SDA = 4, X_SCL = 5, X_CLK = 14, X_DIO = 12, X_BTN = 13, X_LED = 16, X_BUZ = 15;
static uint16_t stored() { uint16_t m, v; EEPROM.get(0, m); EEPROM.get(2, v); return m == 0xF0C5 ? v : 0xFFFF; }
static void store(uint16_t v) { EEPROM.put(0, (uint16_t)0xF0C5); EEPROM.put(2, v); }
#else
static const char* BOARD = "ESP32 DevKit";
static const int X_SDA = 21, X_SCL = 22, X_CLK = 18, X_DIO = 19, X_BTN = 27, X_LED = 25, X_BUZ = 26;
static uint16_t stored() { return g_nvs["sessions"]; }
static void store(uint16_t v) { g_nvs["sessions"] = v; }
#endif

static std::string shown(int r) {
  std::string s = lcd.line(r);
  for (auto& c : s) if (c == 5) c = '#'; else if (c >= 1 && c <= 4) c = '+';
  return s;
}
static void snapshot(const char* when) {
  printf("        %-22s LCD |%s|   TM1637 %02d%s%02d   LED %s\n", when, shown(0).c_str(),
         seg.value / 100, seg.colon() ? ":" : " ", seg.value % 100, g_pinOut[PIN_LED] ? "on" : "off");
  printf("        %-22s     |%s|\n", "", shown(1).c_str());
}

int main() {
  printf("\nBoard: %s\n", BOARD);
  printf("\n[1] Wiring / pin assignment (guide section 3)\n");
  setup();
  run(50);
  LCD_CHECK("LCD I2C on the SDA / SCL pins", g_wireSda == X_SDA && g_wireScl == X_SCL);
  CHECK("LCD address 0x27, 16x2", lcd.addr == 0x27 && lcd.cols == 16 && lcd.rows == 2);
  CHECK("TM1637 on the CLK / DIO pins", seg.clk == X_CLK && seg.dio == X_DIO);
  CHECK("Button pin with internal pull-up", PIN_BUTTON == X_BTN && g_pinMode[X_BTN] == INPUT_PULLUP);
  CHECK("LED pin is an output", PIN_LED == X_LED && g_pinMode[X_LED] == OUTPUT);
  CHECK("Buzzer pin is an output", PIN_BUZZER == X_BUZ && g_pinMode[X_BUZ] == OUTPUT);
  CHECK("Timings are 25 min focus / 5 min break", FOCUS_MS == 1500000UL && BREAK_MS == 300000UL);

  printf("\n[2] Power-on (guide step 10, test plan: Power / LCD / TM1637)\n");
  snapshot("after boot");
  LCD_CHECK("LCD backlight on", lcd.lit);
  LCD_CHECK("LCD shows FOCUS STATION", lcd.line(0) == "FOCUS STATION   ");
  CHECK("Self-test lit every segment at boot", seg.allOnCalls == BOOT_SELF_TEST);
  CHECK("Self-test beeped exactly once at boot", g_risingEdges[PIN_BUZZER] == BOOT_SELF_TEST);
  CHECK("7-segment shows 25:00 after the self-test", seg.value == 2500 && seg.colon() && seg.leadingZero);
  CHECK("LED off in READY", g_pinOut[PIN_LED] == 0);
  CHECK("Buzzer off after the self-test", g_pinOut[PIN_BUZZER] == 0);
  CHECK("Serial prints 'Focus Station ready.'", Serial.saw("Focus Station ready."));
  CHECK("No LCD warning when the LCD answers", !Serial.saw("WARNING"));

  printf("\n[3] Button + LED + countdown (test plan: Button / LED / Countdown)\n");
  press(150);
  uint32_t tStart = waitMode(FOCUS, 200);
  CHECK("Press changes READY -> FOCUS", tStart != 0);
  CHECK("LED on during FOCUS", g_pinOut[PIN_LED] == 1);
  run(999);
  CHECK("Still 25:00 at 0.999 s", seg.value == 2500);
  run(1);
  CHECK("24:59 at exactly 1.000 s", seg.value == 2459);
  snapshot("1 s into focus");
  run(59000);
  CHECK("24:00 at 60 s (once per second)", seg.value == 2400);
  LCD_CHECK("LCD row 1 shows 24:00 + progress bar", lcd.line(1).rfind("24:00 ", 0) == 0);

  printf("\n[4] Pause / resume (guide section 6 control behaviour)\n");
  uint32_t before = remainingMs;
  g_pinLevel[PIN_BUTTON] = LOW; run(1000);
  CHECK("Countdown keeps running while the button is held (no blocking)", before - remainingMs >= 990);
  g_pinLevel[PIN_BUTTON] = HIGH;
  uint32_t tPause = waitMode(PAUSED, 200);
  CHECK("Press while running -> PAUSED", tPause != 0);
  uint32_t frozen = remainingMs;
  bool ledOn = false, ledOff = false, colOn = false, colOff = false;
  for (int i = 0; i < 3000; i++) {
    run(1);
    ledOn |= g_pinOut[PIN_LED] == 1; ledOff |= g_pinOut[PIN_LED] == 0;
    colOn |= seg.colon();            colOff |= !seg.colon();
  }
  CHECK("Time frozen while paused", remainingMs == frozen);
  CHECK("LED blinks while paused", ledOn && ledOff);
  CHECK("Colon blinks while paused", colOn && colOff);
  snapshot("paused");
  run(7000);
  press(150);
  uint32_t tResume = waitMode(FOCUS, 200);
  CHECK("Press while paused resumes FOCUS", tResume != 0);
  CHECK("Resume continues from the frozen time", remainingMs == frozen);

  printf("\n[5] End of focus (test plan: Buzzer, checklist: end-of-session beep)\n");
  g_risingEdges[PIN_BUZZER] = 0;
  uint32_t tBreak = waitMode(BREAK_MODE, FOCUS_MS);
  CHECK("Focus ends after exactly 25:00 of running time (pause excluded)",
        tBreak - tStart == FOCUS_MS + (tResume - tPause));
  run(1000);
  CHECK("Exactly 3 beeps at end of focus", g_risingEdges[PIN_BUZZER] == 3);
  CHECK("Buzzer off after beeping", g_pinOut[PIN_BUZZER] == 0);
  CHECK("LED off during BREAK", g_pinOut[PIN_LED] == 0);
  CHECK("Completed session counted", sessions == 1);
  CHECK("Session count saved to flash", stored() == 1);
  CHECK("7-segment shows 04:59", seg.value == 459);
  snapshot("1 s into break");

  printf("\n[6] Break, pause during break, automatic alternation\n");
  press(150);
  uint32_t tPause2 = waitMode(PAUSED, 200);
  run(5000);
  press(150);
  uint32_t tResume2 = waitMode(BREAK_MODE, 200);
  CHECK("Pausing a BREAK resumes as BREAK (guide's starter code resumed as FOCUS)", tPause2 && tResume2);
  CHECK("LED stays off after resuming the break", g_pinOut[PIN_LED] == 0);
  g_risingEdges[PIN_BUZZER] = 0;
  uint32_t tFocus2 = waitMode(FOCUS, BREAK_MS);
  CHECK("Break ends after exactly 5:00 of running time", tFocus2 - tBreak == BREAK_MS + (tResume2 - tPause2));
  CHECK("Break end starts the next FOCUS automatically", mode == FOCUS);
  run(1000);
  CHECK("Exactly 2 beeps at end of break", g_risingEdges[PIN_BUZZER] == 2);

  printf("\n[7] Long press = reset (guide note: 'optionally add reset')\n");
  g_risingEdges[PIN_BUZZER] = 0;
  g_pinLevel[PIN_BUTTON] = LOW; run(1600);
  CHECK("Hold 1.5 s resets to READY while still held", mode == READY);
  g_pinLevel[PIN_BUTTON] = HIGH; run(300);
  CHECK("Releasing after the hold does not start a session", mode == READY);
  CHECK("One confirmation beep", g_risingEdges[PIN_BUZZER] == 1);
  CHECK("7-segment back to 25:00", seg.value == 2500);
  LCD_CHECK("LCD back to FOCUS STATION", lcd.line(0) == "FOCUS STATION   ");
  LCD_CHECK("Counter shown in READY", lcd.line(1) == "Ready    Done  1");
  snapshot("after reset");
  press(1600); g_pinLevel[PIN_BUTTON] = HIGH; run(300);
  CHECK("Hold in READY clears the counter", sessions == 0 && stored() == 0);
  LCD_CHECK("LCD updates to Done 0", lcd.line(1) == "Ready    Done  0");

  printf("\n[8] Robustness\n");
  for (int i = 0; i < 6; i++) { g_pinLevel[PIN_BUTTON] = (i % 2 == 0) ? LOW : HIGH; run(2); }
  g_pinLevel[PIN_BUTTON] = LOW; run(150);
  for (int i = 0; i < 6; i++) { g_pinLevel[PIN_BUTTON] = (i % 2 == 0) ? HIGH : LOW; run(2); }
  g_pinLevel[PIN_BUTTON] = HIGH; run(100);
  CHECK("Bouncy button contact counts as one press", mode == FOCUS);
  CHECK("LCD never cleared (no flicker)", lcd.clearCalls == 0);
  press(1600); g_pinLevel[PIN_BUTTON] = HIGH; run(300);

  g_now = 0xFFFFFFFFUL - 10000;   // millis() wraps after ~49.7 days
  press(150);
  waitMode(FOCUS, 200);
  uint32_t r0 = remainingMs;
  run(20000);
  CHECK("Countdown correct across millis() overflow", mode == FOCUS && r0 - remainingMs == 20000);
  press(1600); g_pinLevel[PIN_BUTTON] = HIGH; run(300);

  store(7);
  setup(); run(50);
  CHECK("Counter survives a reboot", sessions == 7);
  LCD_CHECK("LCD shows the restored counter", lcd.line(1) == "Ready    Done  7");

#if !USE_LCD
  CHECK("USE_LCD 0: firmware never touches the LCD", lcd.calls == 0);
  CHECK("USE_LCD 0: Serial says the LCD is disabled", Serial.saw("LCD disabled"));
#else
  g_lcdPresent = false; Serial.lines.clear();
  long lcdCalls = lcd.calls;
  setup(); run(50);
  CHECK("Missing LCD prints a clear Serial warning", Serial.saw("WARNING: no LCD at 0x27"));
  CHECK("Missing LCD: setup still finishes", Serial.saw("Focus Station ready."));
  press(150); waitMode(FOCUS, 200); run(2000);
  CHECK("Missing LCD: timer still runs on the 4-digit display", mode == FOCUS && seg.value == 2458);
  CHECK("Missing LCD: firmware never talks to the absent LCD", lcd.calls == lcdCalls);
#endif

  printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
