# Firmware

PlatformIO + Arduino (ESP32 core 2.x via `espressif32@6.9.0`), TFT_eSPI for the
ST7796 screen and XPT2046 touch. Milestone **M1 bring-up** is written and
builds; it has **not been flashed** yet.

## Build and flash

```bash
pip install platformio==6.1.18
cd firmware
cp include/secrets.example.h include/secrets.h   # then fill in Wi-Fi, FAITH URL and (for Projects) a FAITH token
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
| `lib/dash_core/` | no Arduino code: battery curve, button debounce/long-press, retry backoff, connector interface and registry, FAITH projects glance parsing, the mod list and dash-mod frames (`mod.*`), Atlas frames and words (`atlas.*`), file names and the Wi-Fi join code (`share.*`). Tested in `test/test_core`. |
| `src/main.cpp` | wires it together; the main loop |
| `src/hw.*` | battery ADC, buttons, RGB LED, backlight PWM |
| `src/net.*` | Wi-Fi with backoff, NTP clock |
| `src/faith_link.*` | first connector: polls FAITH `GET /api/health` |
| `src/projects_link.*` | FAITH's family projects: polls `GET /api/projects/glance` with `DASH_FAITH_TOKEN` |
| `src/ui.*` | screens: Home cards, Projects, Files, a mod tab (or Mods), Links, Device, mod settings, Talk placeholder |
| `src/gfx.*` | the screen, colours and drawing helpers shared by screens and mods |
| `src/files_link.*` | Files: microSD card, the DASH-Share Wi-Fi, its web page and QR code |
| `src/espnow_bus.*` | ESP-NOW receive queue shared by mods |
| `src/mods/` | mods: `mods.cpp` lists them; `atlas_mod.*` is the truck |
| `src/settings.*` | NVS: touch calibration, per-link on/off |
| `include/board.h` | non-display pins; display pins are flags in `platformio.ini` |

## Files and mods

- **Files** tab: the SD card's files and space, **Share** (a `DASH-Share` Wi-Fi
  and QR code) and **Uploads: on/off**. See [docs/files.md](../docs/files.md).
- **Mods**: each shows in Links with On/Off and, if it has any, **Settings >**.
  One mod with a screen gets a tab; more share a **Mods** tab.
  See [docs/mods/README.md](../docs/mods/README.md).
- **Atlas** tab: Gauges, Codes, Trip. Setup in [docs/mods/atlas.md](../docs/mods/atlas.md);
  the ESP32-C3 bridge firmware is in [`bridge/`](../bridge).
- **Home** is now cards: Links (only when something needs a look), mods,
  Projects, Files. Tap a card to open it.
- **Device** moved under Links (**Device info**), and shows the MAC the bridge needs.

## What M1 does

- **Status bar**: Wi-Fi in words ("Wi-Fi good", "No Wi-Fi") and battery % or "USB power".
- **Home**: clock and date (NTP), a links summary, and a Projects card ("1 needs you, 2 working"; tap it). Without a FAITH token, a placeholder for Orb's next task.
- **Projects**: FAITH's family projects ([FAITH docs/projects.md](https://github.com/Wizard815/F.A.I.T.H/blob/main/docs/projects.md)).
  Each project (yours, Family, Parents) with its threads, each thread's state in words
  (Working, Done, Waiting for approval, Didn't work) and its latest line. Tap a thread for
  the whole latest line. Next/First pages through a long list. Read-only for now:
  starting a thread, sending it more and saying Yes are in the FAITH app until
  hold-to-talk (M2) and pairing (M6). Refreshes every minute and when you open it.
- **Links**: each connector with its state in words, the reason when it fails,
  and an On/Off switch that's saved across restarts. **Device info** at the bottom.
- **Device**: battery mV, Wi-Fi, IP, signal, free memory, uptime, version.
- **Touch**: calibrates on first boot and saves it. Hold BOOT while powering on to redo it.
- **BOOT key**: click = next tab (Home, Projects, Files, Atlas, Links), hold = Home.
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

## Projects: set it up

1. On the FAITH server, make a token for this device as your own screen:
   `docker exec faith faith token add client <your-id> dash`.
2. Put it in `include/secrets.h` as `DASH_FAITH_TOKEN`, and rebuild.

The token decides what shows, on FAITH's side: your own project, Family, and
Parents if you're a grown-up. Nobody else's. Remove it with `faith token revoke <name>`
if the device is lost.

**What goes wrong:**
- "Projects are off": no token in `secrets.h`, or Projects is Off in Links.
- "token refused: make a new one": the token was revoked or mistyped.
- "FAITH has no projects: update it": FAITH is older than its projects feature.

## Limits

- The FAITH and Projects checks each block the loop for up to 3 s when FAITH doesn't answer.
  Fine for M1; links move to their own FreeRTOS task with the Hermes WebSocket in M2.
- `https://` FAITH URLs need its CA in `DASH_FAITH_CA_PEM`. The device will not
  skip certificate checks.
