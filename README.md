# Focus Station: an ESP32 / ESP8266 Pomodoro timer

[![CI](https://github.com/1hamzaachour-ai/esp32-focus-station/actions/workflows/ci.yml/badge.svg)](https://github.com/1hamzaachour-ai/esp32-focus-station/actions/workflows/ci.yml)

A one-button Pomodoro timer: 25-minute focus sessions and 5-minute breaks on a TM1637 4-digit display, with a status LED, a buzzer and an optional 16×2 I2C LCD.
The same Arduino sketch runs on an **ESP32 DevKit** or a **NodeMCU ESP8266**. The pins are chosen from the board you compile for.

## Features

- 25 min focus / 5 min break, alternating automatically (configurable)
- One button: press to start, pause or resume; hold 1.5 s to reset
- A paused break resumes as a break, and a paused focus session as focus
- Completed-session counter saved in flash, so it survives power-off
- Non-blocking code: the button, timer, buzzer and displays never freeze each other
- Power-on self-test: `88:88`, LED on and one beep, so wiring faults show up at once
- Optional LCD with mode, session count, time and a progress bar. The station also runs without it.
- PC test bench and CI that compile and test both boards on every push

## Parts

| Qty | Part |
|---|---|
| 1 | ESP32 DevKit **or** NodeMCU 1.0 (ESP-12E, ESP8266) |
| 1 | TM1637 4-digit display |
| 1 | Momentary push button |
| 1 | 5 mm LED and a 220–330 Ω resistor |
| 1 | Active buzzer (a passive one works too, see [Settings](#settings)) |
| 1 | 16×2 LCD with I2C backpack (optional) |
| – | Breadboard, jumper wires, USB **data** cable |

## Wiring

Unplug USB before changing wires.

| Part | Pin | ESP32 DevKit | NodeMCU ESP8266 |
|---|---|---|---|
| TM1637 | VCC / GND | 3V3 / GND | 3V3 / G |
| TM1637 | CLK | GPIO 18 | D5 |
| TM1637 | DIO | GPIO 19 | D6 |
| Button | one leg | GPIO 27 | D7 |
| Button | diagonally opposite leg | GND | G |
| LED | long leg (+) | GPIO 25 via 220–330 Ω | D0 via 220–330 Ω |
| LED | short leg (−) | GND | G |
| Buzzer | + / − | GPIO 26 / GND | D8 / G |
| LCD (optional) | VCC / GND | 5V (VIN) / GND | VIN / G |
| LCD (optional) | SDA / SCL | GPIO 21 / GPIO 22 | D2 / D1 |

NodeMCU notes:
- Keep the button off **D3, D4 and D8**. Those pins choose the boot mode, so a pressed button there stops the board from starting. The buzzer is fine on D8, because a buzzer to ground keeps that pin LOW at power-up.
- Never connect anything to the left-side **SD3, SD2, SD1, CMD, SD0, CLK** pins. They belong to the flash memory chip.

## Controls

| Action | READY | FOCUS / BREAK | PAUSED |
|---|---|---|---|
| Short press | start focus | pause | resume the same timer |
| Hold 1.5 s | clear the session counter | reset to READY | reset to READY |

- **4-digit display:** time left as `MM:SS`. The colon blinks while paused.
- **LED:** on during focus, blinking while paused, off otherwise.
- **Buzzer:** 3 beeps when focus ends, 2 when a break ends, 1 to confirm a hold.
- **LCD** (with `USE_LCD 1`): `FOCUS STATION` at power-on. While running, the mode, the completed-session count, the time and a progress bar.

## Build and upload

1. Install **Arduino IDE 2**. Under **File → Preferences → Additional boards manager URLs**, add:
   - ESP8266: `https://arduino.esp8266.com/stable/package_esp8266com_index.json`
   - ESP32: `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
2. In **Boards Manager**, install **esp8266** (tested with 3.1.2) or **esp32** (tested with 3.3.12).
3. In **Library Manager**, install **TM1637** (Avishay Orpaz) and **LiquidCrystal I2C** (Frank de Brabander).
4. Open `focus_station/focus_station.ino`.
5. Under **Tools → Board**, pick **NodeMCU 1.0 (ESP-12E Module)** or **ESP32 Dev Module**, then the board's port under **Tools → Port**.
6. Click **Upload**.

With arduino-cli: both sketch folders have a `sketch.yaml` that makes the **NodeMCU ESP8266 the default board**, so no `--fqbn` is needed for it.

```sh
arduino-cli compile --upload -p COM6 focus_station                          # NodeMCU ESP8266 (default)
arduino-cli compile --upload --fqbn esp32:esp32:esp32 -p COM5 focus_station   # ESP32
```

If Windows shows no COM port for a NodeMCU, it needs the CP210x (or CH340) USB driver. On Windows 11 it is offered under **Settings → Windows Update → Advanced options → Optional updates → Driver updates**.

## Power-on check

1. For 1 second the display shows `88:88`, the LED lights and the buzzer beeps once. Any part that stays dark or silent has a wiring fault.
2. The display shows `25:00`. Press the button: the countdown starts and the LED turns on.
3. Hold the button for 1.5 s: one beep, and the display returns to `25:00`.

The Serial Monitor at 115200 baud prints:

```text
LCD disabled (USE_LCD 0): running on the 4-digit display.
Self-test done: 88:88 + LED + 1 beep.
[    1550 ms] READY  (done: 0)
Focus Station ready.
```

On an ESP8266, a burst of garbled characters before these lines is normal. That's the chip's bootloader talking at 74880 baud.

## Settings

At the top of `focus_station.ino`:

| Setting | Default | Meaning |
|---|---|---|
| `USE_LCD` | `0` | `1` if a 16×2 I2C LCD is connected. If it doesn't answer at boot, the station warns on Serial and runs without it. |
| `BOOT_SELF_TEST` | `1` | Power-on `88:88` + LED + beep. Set to `0` to skip it. |
| `LCD_ADDR` | `0x27` | LCD I2C address. Some backpacks use `0x3F`; `i2c_scanner` finds yours. |
| `BUZZER_IS_PASSIVE` | `0` | `0` = active buzzer. `1` = passive piezo, driven with a tone. If you hear only a click, set `1`. |
| `DEMO_MODE` | `0` | `1` = 25 **s** focus / 5 **s** break, to test the whole cycle quickly |
| `AUTO_START_NEXT_FOCUS` | `true` | `false` = after a break, wait in READY for a press |

## Testing

`pc_test/` compiles the real `focus_station.ino` on a PC against fake hardware: a simulated clock, button, LCD, TM1637, LED, buzzer and flash. It checks the power-on state, the countdown to the millisecond, pause and resume, beeps, reset, debouncing, `millis()` overflow and the counter surviving a reboot.

```sh
cd pc_test
g++ -std=c++17 -O2 -I stubs -I ../focus_station test.cpp -o test && ./test              # ESP32 build
g++ -std=c++17 -O2 -DESP8266 -I stubs -I ../focus_station test.cpp -o test && ./test    # ESP8266 build
```

No compiler on Windows? `pip install ziglang`, then use `python -m ziglang c++` in place of `g++`.

[CI](.github/workflows/ci.yml) runs both test builds and compiles the firmware and `i2c_scanner` for `esp8266:esp8266:nodemcuv2` and `esp32:esp32:esp32` on every push.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| No COM port | Charge-only cable, or missing USB driver | Use a data cable. Install the CP210x / CH340 driver (see [Build and upload](#build-and-upload)). |
| Upload stuck at `Connecting...` | Board didn't enter upload mode | Hold **FLASH** (NodeMCU) or **BOOT** (ESP32) while the upload starts. |
| Compile stops with `Focus Station needs an ESP8266 or ESP32 board` | A non-ESP board (for example Arduino Uno) is selected | **Tools → Board → esp8266 → NodeMCU 1.0 (ESP-12E Module)**. |
| `This chip is ESP8266, not ESP32` (or the reverse) | Wrong board selected | Pick the board that matches the chip. |
| A digit, the LED or the buzzer stays off during the self-test | Wiring | Check that part's wires against the table above. |
| One beep about 2 s after power-on, then the button does nothing | Button legs on the same internal pair | Use two **diagonally opposite** legs. |
| Buzzer only clicks | Passive buzzer | Set `BUZZER_IS_PASSIVE 1`. |
| LCD backlight on but no text | Contrast or address | Turn the backpack's contrast screw. Then run `i2c_scanner` and set `LCD_ADDR`. |
| ESP8266 prints `Fatal exception 0 (IllegalInstructionCause)` at boot, or uploads end with `MD5 of file does not match data in flash!` | The board can't read its flash memory reliably: a failing flash chip or a weak supply | Try another USB port and a short data cable. If uploads still fail verification with nothing connected, replace the board. |

## Project layout

```text
focus_station/   firmware (ESP32 + ESP8266)
i2c_scanner/     finds the LCD's I2C address
pc_test/         PC test bench: test.cpp + fake Arduino headers in stubs/
wokwi/           Wokwi simulator diagram (ESP32)
.github/         CI workflow
```

To try it without hardware, create an ESP32 project on [wokwi.com](https://wokwi.com). Paste the sketch, then use the files in `wokwi/`, with `BUZZER_IS_PASSIVE 1` and `DEMO_MODE 1`.

## Hardware note: 5 V LCD on a 3.3 V board

Most LCD I2C backpacks pull SDA/SCL up to their own supply. Powered from 5 V, that puts 5 V on the board's I2C pins, which is above the ESP32's and ESP8266's ratings. It usually works, but for a long-lived build add a bidirectional level shifter (BSS138 type), or remove the backpack's pull-ups and use 3.3 V ones.

## Origin

The project started from the *ESP32 Focus Station — Technical Build Guide* starter sketch. Compared with that sketch, this firmware:
1. resumes a paused break as a break (the starter always resumed into focus)
2. adds a working reset (the starter's `resetTimer()` was never called)
3. removes the blocking delays that froze the countdown while the button was held
4. plays 3 beeps at the end of focus instead of 5
5. stops the LCD from flickering every second
6. keeps time to the millisecond across pauses
7. adds ESP8266 support, the LCD-less mode, the self-test and the persistent session counter
