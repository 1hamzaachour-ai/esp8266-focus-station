/*
  ESP32 / ESP8266 FOCUS STATION — firmware v2
  The board is detected at compile time; pick it in Tools -> Board.

  ESP32 DevKit (wiring from the "ESP32 Focus Station — Technical Build Guide"):
    LCD 16x2 I2C  -> SDA 21, SCL 22, VCC 5V/VIN   (address 0x27 or 0x3F — run i2c_scanner if unsure)
    TM1637        -> CLK 18, DIO 19, VCC 3V3
    Button        -> GPIO 27 to GND   (internal pull-up, no resistor needed)
    LED           -> GPIO 25 -> 220-330 ohm -> LED anode, cathode -> GND
    Buzzer        -> GPIO 26 (+), GND (-)

  NodeMCU ESP8266 (board "NodeMCU 1.0 (ESP-12E Module)"):
    LCD 16x2 I2C  -> SDA D2, SCL D1, VCC VIN
    TM1637        -> CLK D5, DIO D6, VCC 3V3
    Button        -> D7 to GND
    LED           -> D0 -> 220-330 ohm -> LED anode, cathode -> GND
    Buzzer        -> D8 (+), GND (-)

  One-button controls:
    short press   READY -> start focus | running -> pause | paused -> resume
    long press    running/paused -> reset to READY
    (1.5 s)       READY -> clear the completed-session counter

  Libraries (Arduino Library Manager):
    "LiquidCrystal I2C" by Frank de Brabander
    "TM1637"            by Avishay Orpaz
*/

#include <Wire.h>
#if defined(ESP8266)
#include <EEPROM.h>
extern "C" {
#include <user_interface.h>  // system_phy_set_powerup_option
}
#else
#include <Preferences.h>
#endif
#include <LiquidCrystal_I2C.h>
#include <TM1637Display.h>

// ---------------- Configuration ----------------
#define USE_LCD           0     // 1 = 16x2 I2C LCD connected, 0 = run on the 4-digit display only
#define BOOT_SELF_TEST    1     // 1 = at power-on light every digit + the LED and beep once (wiring check)
#define LCD_ADDR          0x27  // change to 0x3F if the backlight is on but no text appears
#define BUZZER_IS_PASSIVE 0     // 0 = active buzzer (beeps on DC), 1 = passive piezo (needs tone) / Wokwi
#define DEMO_MODE         0     // 1 = 25 s focus / 5 s break, for testing transitions quickly

const bool AUTO_START_NEXT_FOCUS = true;  // false = after a break, wait in READY for a button press

#if defined(ESP8266)
// NodeMCU labels in comments. D3, D4 and D8 are boot-mode pins: the button stays off them,
// and D8 is only safe for the buzzer because a buzzer to GND keeps it LOW at power-up.
const uint8_t PIN_SDA    = 4;   // D2
const uint8_t PIN_SCL    = 5;   // D1
const uint8_t PIN_CLK    = 14;  // D5
const uint8_t PIN_DIO    = 12;  // D6
const uint8_t PIN_BUTTON = 13;  // D7
const uint8_t PIN_LED    = 16;  // D0
const uint8_t PIN_BUZZER = 15;  // D8
#else
const uint8_t PIN_SDA    = 21;
const uint8_t PIN_SCL    = 22;
const uint8_t PIN_CLK    = 18;
const uint8_t PIN_DIO    = 19;
const uint8_t PIN_BUTTON = 27;
const uint8_t PIN_LED    = 25;
const uint8_t PIN_BUZZER = 26;
#endif

#if DEMO_MODE
const uint32_t UNIT_MS = 1000UL;
#else
const uint32_t UNIT_MS = 60UL * 1000UL;
#endif
const uint32_t FOCUS_MS = 25 * UNIT_MS;
const uint32_t BREAK_MS = 5 * UNIT_MS;

const uint32_t DEBOUNCE_MS    = 30;
const uint32_t LONG_PRESS_MS  = 1500;
const uint32_t BLINK_MS       = 500;
const uint32_t BEEP_ON_MS     = 120;
const uint32_t BEEP_OFF_MS    = 120;
const uint16_t BEEP_FREQ_HZ   = 2000;  // passive buzzer only
const uint8_t  SEG_BRIGHTNESS = 5;     // 0..7

const uint8_t BAR_CELLS = 10;          // LCD row 1: "MM:SS " + 10-cell progress bar
const uint8_t BAR_STEPS = BAR_CELLS * 5;

// ---------------- Hardware ----------------
LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);
TM1637Display seg(PIN_CLK, PIN_DIO);
#if !defined(ESP8266)
Preferences prefs;
#endif
bool lcdOk = false;  // false when no LCD answered at boot: the station runs on the 4-digit display alone

// Custom LCD glyphs 1..5: a cell filled 1..5 pixel columns from the left.
uint8_t BAR_GLYPHS[5][8] = {
  {0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00},
  {0x00, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00},
  {0x00, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x00},
  {0x00, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x00},
  {0x00, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x00},
};

// ---------------- State ----------------
enum Mode : uint8_t { READY, FOCUS, BREAK_MODE, PAUSED };
const char* const MODE_LABELS[] = { "READY", "FOCUS", "BREAK", "PAUSED" };

Mode     mode        = READY;
Mode     pausedFrom  = FOCUS;      // which timer PAUSED resumes into
uint32_t remainingMs = FOCUS_MS;
uint32_t lastTickMs  = 0;
uint16_t sessions    = 0;          // completed focus sessions, persisted in NVS

uint32_t shownSecs   = UINT32_MAX; // last value drawn on LCD row 1
uint32_t shownSegKey = UINT32_MAX; // last (seconds, colon) drawn on the TM1637

struct {
  bool     stable    = HIGH;       // debounced level (HIGH = released)
  bool     lastRaw   = HIGH;
  uint32_t changedMs = 0;
  uint32_t pressedMs = 0;
  bool     longFired = false;
} btn;

struct {
  uint8_t  beepsLeft = 0;
  bool     on        = false;
  uint32_t nextMs    = 0;
} buzzer;

// ---------------- Buzzer (non-blocking) ----------------
void buzzerWrite(bool on) {
#if BUZZER_IS_PASSIVE
  if (on) tone(PIN_BUZZER, BEEP_FREQ_HZ);
  else    noTone(PIN_BUZZER);
#else
  digitalWrite(PIN_BUZZER, on ? HIGH : LOW);
#endif
}

void beep(uint8_t count) {
  if (buzzer.on) buzzerWrite(false);
  buzzer.on        = false;
  buzzer.beepsLeft = count;
  buzzer.nextMs    = millis();
}

void updateBuzzer(uint32_t now) {
  if (!buzzer.on && buzzer.beepsLeft == 0) return;
  if ((int32_t)(now - buzzer.nextMs) < 0) return;

  if (buzzer.on) {
    buzzerWrite(false);
    buzzer.on     = false;
    buzzer.nextMs = now + BEEP_OFF_MS;
  } else {
    buzzerWrite(true);
    buzzer.on     = true;
    buzzer.beepsLeft--;
    buzzer.nextMs = now + BEEP_ON_MS;
  }
}

// ---------------- Display ----------------
uint32_t currentDurationMs() {
  Mode m = (mode == PAUSED) ? pausedFrom : mode;
  return (m == BREAK_MODE) ? BREAK_MS : FOCUS_MS;
}

// Prints a full 16-char row: "<label>   Done  3"
void printStatus(const char* label) {
  char line[17];
  snprintf(line, sizeof line, "%-9sDone%3u", label, (unsigned)min<uint16_t>(sessions, 999));
  lcd.print(line);
}

void drawHeader() {
  if (!lcdOk) return;
  lcd.setCursor(0, 0);
  if (mode == READY) lcd.print("FOCUS STATION   ");  // boot screen named in the build guide
  else               printStatus(MODE_LABELS[mode]);
}

void drawTimeLine(uint32_t secs) {
  if (!lcdOk) return;
  lcd.setCursor(0, 1);
  if (mode == READY) {
    printStatus("Ready");
    return;
  }

  char time[16];
  snprintf(time, sizeof time, "%02u:%02u ", (unsigned)(secs / 60), (unsigned)(secs % 60));
  lcd.print(time);

  uint32_t total = currentDurationMs();
  uint8_t  steps = (uint64_t)(total - remainingMs) * BAR_STEPS / total;
  for (uint8_t i = 0; i < BAR_CELLS; i++) {
    uint8_t fill = min<uint8_t>(steps, 5);
    steps -= fill;
    if (fill == 0) lcd.print(' ');
    else           lcd.write(byte(fill));
  }
}

void drawSeg(uint32_t secs, bool colon) {
  seg.showNumberDecEx((secs / 60) * 100 + secs % 60, colon ? 0b01000000 : 0, true);
}

void updateOutputs(uint32_t now) {
  bool blinkOn = (now / BLINK_MS) % 2 == 0;

  digitalWrite(PIN_LED, (mode == FOCUS || (mode == PAUSED && blinkOn)) ? HIGH : LOW);

  uint32_t secs   = (remainingMs + 999) / 1000;  // round up: 25:00 shows until a full second has passed
  bool     colon  = (mode != PAUSED) || blinkOn;
  uint32_t segKey = (secs << 1) | colon;

  if (segKey != shownSegKey) {
    shownSegKey = segKey;
    drawSeg(secs, colon);
  }
  if (secs != shownSecs) {
    shownSecs = secs;
    drawTimeLine(secs);
  }
}

void forceRedraw() {
  drawHeader();
  shownSecs   = UINT32_MAX;
  shownSegKey = UINT32_MAX;
}

// ---------------- Mode changes ----------------
void setMode(Mode m) {
  mode = m;
  forceRedraw();
  Serial.printf("[%8lu ms] %s  (done: %u)\n", (unsigned long)millis(), MODE_LABELS[mode], (unsigned)sessions);
}

void startTimer(Mode m, uint32_t durationMs) {
  remainingMs = durationMs;
  lastTickMs  = millis();
  setMode(m);
}

void resetToReady() {
  remainingMs = FOCUS_MS;
  setMode(READY);
}

// Completed-session counter in flash: NVS on ESP32, emulated EEPROM on ESP8266.
#if defined(ESP8266)
const uint16_t EEPROM_MAGIC = 0xF0C5;  // marks the EEPROM area as ours (fresh boards read 0xFFFF)

void loadSessions() {
  EEPROM.begin(4);
  uint16_t magic;
  EEPROM.get(0, magic);
  if (magic == EEPROM_MAGIC) EEPROM.get(2, sessions);
  else                       sessions = 0;
}

void saveSessions() {
  EEPROM.put(0, EEPROM_MAGIC);
  EEPROM.put(2, sessions);
  EEPROM.commit();
}
#else
void loadSessions() {
  prefs.begin("focus", false);
  sessions = prefs.getUShort("sessions", 0);
}

void saveSessions() {
  prefs.putUShort("sessions", sessions);
}
#endif

void updateTimer(uint32_t now) {
  if (mode != FOCUS && mode != BREAK_MODE) return;

  uint32_t elapsed = now - lastTickMs;
  lastTickMs = now;
  if (elapsed < remainingMs) {
    remainingMs -= elapsed;
    return;
  }

  remainingMs = 0;
  if (mode == FOCUS) {
    sessions++;
    saveSessions();
    beep(3);
    startTimer(BREAK_MODE, BREAK_MS);
  } else {
    beep(2);
    if (AUTO_START_NEXT_FOCUS) startTimer(FOCUS, FOCUS_MS);
    else                       resetToReady();
  }
}

// ---------------- Button (non-blocking, short + long press) ----------------
void onShortPress() {
  switch (mode) {
    case READY:
      startTimer(FOCUS, FOCUS_MS);
      break;
    case FOCUS:
    case BREAK_MODE:
      pausedFrom = mode;
      setMode(PAUSED);
      break;
    case PAUSED:
      lastTickMs = millis();
      setMode(pausedFrom);
      break;
  }
}

void onLongPress() {
  beep(1);  // tells the user they can let go
  if (mode == READY) {
    sessions = 0;
    saveSessions();
    forceRedraw();
    Serial.println("Session counter cleared");
  } else {
    resetToReady();
  }
}

void updateButton(uint32_t now) {
  bool raw = digitalRead(PIN_BUTTON);
  if (raw != btn.lastRaw) {
    btn.lastRaw   = raw;
    btn.changedMs = now;
  }

  if (raw != btn.stable && now - btn.changedMs >= DEBOUNCE_MS) {
    btn.stable = raw;
    if (raw == LOW) {
      btn.pressedMs = now;
      btn.longFired = false;
    } else if (!btn.longFired) {
      onShortPress();  // fires on release so it can't be confused with a long press
    }
  }

  if (btn.stable == LOW && !btn.longFired && now - btn.pressedMs >= LONG_PRESS_MS) {
    btn.longFired = true;
    onLongPress();
  }
}

// ---------------- Arduino entry points ----------------
#if defined(ESP8266)
// No Wi-Fi is used: keep the radio off to save power.
// Kept below the type declarations: Arduino inserts prototypes before the first function.
RF_MODE(RF_DISABLED);

// At power-up only check the supply voltage (~2 ms) instead of running the full
// radio calibration (~200 ms of current spikes) that an unused radio doesn't need.
RF_PRE_INIT() {
  system_phy_set_powerup_option(2);
}
#endif

void setup() {
  Serial.begin(115200);

  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  digitalWrite(PIN_BUZZER, LOW);

  loadSessions();

#if USE_LCD
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.beginTransmission(LCD_ADDR);
  lcdOk = (Wire.endTransmission() == 0);
  if (lcdOk) {
    lcd.init();
    lcd.backlight();
    for (uint8_t i = 0; i < 5; i++) lcd.createChar(i + 1, BAR_GLYPHS[i]);
  } else {
    // Talking to an absent LCD would stall every redraw, so run without it.
    Serial.printf("WARNING: no LCD at 0x%02X. Check SDA/SCL, or run i2c_scanner and change LCD_ADDR. "
                  "Running without the LCD.\n", LCD_ADDR);
  }
#else
  Serial.println("LCD disabled (USE_LCD 0): running on the 4-digit display.");
#endif

  seg.setBrightness(SEG_BRIGHTNESS);

#if BOOT_SELF_TEST
  // Every segment, the LED and one short beep: if any of them stays dark or silent, check its wires.
  const uint8_t allSegments[] = { 0xFF, 0xFF, 0xFF, 0xFF };
  seg.setSegments(allSegments);
  digitalWrite(PIN_LED, HIGH);
  buzzerWrite(true);
  delay(150);
  buzzerWrite(false);
  delay(850);
  digitalWrite(PIN_LED, LOW);
  Serial.println("Self-test done: 88:88 + LED + 1 beep.");
#endif

  resetToReady();
  Serial.println("Focus Station ready.");
}

void loop() {
  uint32_t now = millis();
  updateTimer(now);   // before the button, so a pause counts the time up to the press
  updateButton(now);
  updateBuzzer(now);
  updateOutputs(now);
}
