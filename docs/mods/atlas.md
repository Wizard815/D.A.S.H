# Atlas mod (the truck)

DASH shows the truck homelab: OBD2 gauges, check-engine codes in plain words,
sync status, weather, the Pi's own battery and GPS. Read-only.
Message format: [atlas-protocol.md](atlas-protocol.md).

```
 WiCAN (OBD2) ──MQTT──┐
                      ▼
            Pi 3B: truckd + Mosquitto ──── truck Wi-Fi (ch 6) ──── DASH
                      │ UART /dev/ttyAMA0                           ▲
                      ▼                                              │
            ESP32-C3 bridge ─────────── ESP-NOW (ch 6) ─────────────┘
```

DASH uses both paths when it can (**Link: Auto**). MQTT needs DASH on the
truck Wi-Fi. ESP-NOW works with only the bridge, for example when DASH is not
on the truck network.

## On DASH

In `firmware/include/secrets.h`:

```c
#define DASH_ATLAS_WIFI_SSID "Atlas"        // the truck network
#define DASH_ATLAS_WIFI_PASS "..."
#define DASH_ATLAS_PEER_MAC "AA:BB:CC:DD:EE:FF"  // the bridge, printed at its start
#define DASH_ESPNOW_KEY "16-characters..."  // exactly 16, same as the bridge
```

DASH tries home Wi-Fi first, then the truck's, so in the truck it moves over by
itself in about 30 s. The broker is the truck network's router (the Pi), so no
address is needed. Set `DASH_ATLAS_MQTT_HOST` only for a broker elsewhere.

Settings (Links, tap **Atlas**): **Units** US or Metric; **Link** Auto,
ESP-NOW only, or Wi-Fi only.

The Atlas tab has three pages: **Gauges** (coolant, oil, transmission, intake,
battery, fuel, MPG now and trip, range), **Codes** (check-engine light, each
code with words, "Stop soon" on overheating, misfire and oil pressure), **Trip**
(sync, weather, the Pi's battery and mode, position).

## On the Pi

- Mosquitto on the truck network. truckd publishes the frames retained to
  `dash/v1/atlas/<t>` and writes the same lines to the UART.
- UART: add `dtoverlay=miniuart-bt` to `/boot/firmware/config.txt` and
  `enable_uart=1`, take the serial console off (`raspi-config`, Interface,
  Serial: no login shell, hardware on). Use `/dev/ttyAMA0` at 115200.
- An "awake" GPIO that truckd (or a systemd unit) drives high while the Pi
  runs. The bridge watches it.
- hostapd pinned to `channel=6`.

Try it without the truck: `tools/atlas-sim.sh mqtt <broker>` publishes a
drive's worth of frames, and `tools/atlas-sim.sh lines` prints them for a UART.

## The bridge (ESP32-C3 SuperMini)

Firmware in [`bridge/`](../../bridge). Copy `include/bridge_config.example.h`
to `bridge_config.h`, fill in DASH's MAC (Links, Device info), the key and your
home Wi-Fi names, then `cd bridge && pio run -t upload`.

| C3 pin | To |
| --- | --- |
| 20 (RX) | Pi GPIO14 (TXD) |
| 21 (TX) | Pi GPIO15 (RXD) |
| 3 | the Pi's awake GPIO (high while running) |
| 4 | Witty Pi switch line (Pi header GPIO4): pulled low to wake, like its button |
| 5V, GND | an always-on 5 V supply, not the Witty Pi output, or scan mode can't run |

- **Pi on:** relays UART lines to ESP-NOW and back.
- **Pi off:** if the last `sync` it relayed wasn't `synced`, it looks for a
  home Wi-Fi name every 10 minutes and wakes the Pi when it sees one. It
  sleeps between looks (about 10 uA) and wakes at once when the Pi turns on.
  After 10 wakes with no good sync it slows to hourly.

## What goes wrong

| You see | Why | Fix |
| --- | --- | --- |
| Links: "listening on ESP-NOW and truck Wi-Fi" | nothing heard yet | Is the truck on? Is DASH on the truck Wi-Fi (status bar "Truck Wi-Fi")? |
| "Asleep, since 3 h ago" | the Pi said bye when the ignition went off | normal while parked |
| "No word since 2 min ago" | frames stopped without a bye | the Pi lost power or truckd stopped |
| "bad DASH_ATLAS_PEER_MAC" | the MAC in secrets.h doesn't parse | use `AA:BB:CC:DD:EE:FF` |
| ESP-NOW silent, MQTT fine | key or channel differ | same 16-character key both sides; DASH must be on channel 6 (on the truck Wi-Fi, or not joined to another network) |
| Codes show only an area ("Engine/transmission: emissions") | a maker-specific code DASH has no words for | have truckd send a `dtx` frame with the words |
