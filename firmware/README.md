# Firmware

PlatformIO + Arduino (ESP32 core 2.x via `espressif32@6.9.0`), TFT_eSPI for the
ST7796 screen and XPT2046 touch. Milestone **M1 bring-up** is written and
builds; it has **not been flashed** yet.

## Build and flash

```bash
pip install platformio==6.1.18
cd firmware
cp include/secrets.example.h include/secrets.h   # then fill in Wi-Fi and FAITH URL
pio run -t upload && pio device monitor
```

Without `include/secrets.h` it still builds, with the example values and a
warning, so the device won't join your Wi-Fi.

Tests for the pure logic run on your computer, no board needed:

```bash
pio test -e native
```

## Layout

| Path | What |
| --- | --- |
| `lib/dash_core/` | no Arduino code: battery curve, button debounce/long-press, retry backoff, connector interface and registry. Tested in `test/test_core`. |
| `src/main.cpp` | wires it together; the main loop |
| `src/hw.*` | battery ADC, buttons, RGB LED, backlight PWM |
| `src/net.*` | Wi-Fi with backoff, NTP clock |
| `src/faith_link.*` | first connector: polls FAITH `GET /api/health` |
| `src/ui.*` | screens: Home, Links, Device, Talk placeholder |
| `src/settings.*` | NVS: touch calibration, per-link on/off |
| `include/board.h` | non-display pins; display pins are flags in `platformio.ini` |

## What M1 does

- **Status bar**: Wi-Fi in words ("Wi-Fi good", "No Wi-Fi") and battery % or "USB power".
- **Home**: clock and date (NTP), a links summary, a placeholder for Orb's next task.
- **Links**: each connector with its state in words, the reason when it fails,
  and an On/Off switch that's saved across restarts.
- **Device**: battery mV, Wi-Fi, IP, signal, free memory, uptime, version.
- **Touch**: calibrates on first boot and saves it. Hold BOOT while powering on to redo it.
- **BOOT key**: click = next tab, hold = Home.
- **Side button A** (once wired, set `DASH_HAS_PTT_BUTTON=1`): hold shows the Talk screen.
- **Backlight** dims after a minute idle; any touch or button wakes it.
- **RGB LED**: green = all links up, blue = a link is not up, red = no Wi-Fi.

## Check on the board (first flash)

These are guesses until someone sees them on hardware:

1. Colours look right. If red and blue are swapped add `-D TFT_RGB_ORDER=TFT_BGR`;
   if they're inverted add `-D TFT_INVERSION_ON`.
2. Portrait with rotation 0 is the way up you want.
3. Touch hits the tabs where you tap after calibration.
4. Battery % with the cell plugged in matches a multimeter (reading x2).
5. Backlight PWM on IO27 dims rather than flickers.

## Limits

- The FAITH check blocks the loop for up to 3 s when FAITH doesn't answer.
  Fine for M1; links move to their own FreeRTOS task with the Hermes WebSocket in M2.
- `https://` FAITH URLs need its CA in `DASH_FAITH_CA_PEM`. The device will not
  skip certificate checks.
