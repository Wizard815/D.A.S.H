# Bluetooth live reports

DASH sends live status to the DASH phone app over Bluetooth LE, so the phone
knows what DASH knows without any Wi-Fi: Atlas alerts the moment they arrive,
each mod's one-line summary, and DASH's battery. Read-only: the phone can't
change anything on DASH this way.

Build flag `DASH_BLE_REPORTS` (default 1, in `firmware/platformio.ini` and
`src/config.h`). With 0 there is no Bluetooth code at all. Code:
`firmware/src/ble_reports.cpp` (radio), `firmware/lib/dash_core/src/report.cpp`
(frames, tested on the computer).

## Contract

Fixed: the Android app is written against it.

| What | Value |
| --- | --- |
| Name | `DASH-` and the last 4 hex digits of DASH's Bluetooth MAC, e.g. `DASH-A10B` (in the scan response) |
| Service | `6a7c0101-4a1e-4d4b-9a54-44415348aa01` (in the advertisement) |
| Characteristic | `6a7c0102-4a1e-4d4b-9a54-44415348aa01`: read and notify. A read returns the newest frame sent. |
| Security | bonding, MITM, LE Secure Connections; DASH is display-only, so the phone types a 6-digit passkey shown on DASH |
| MTU | ask for 247 after connecting. With Android's default 23, notifications are cut to 20 bytes. |
| One phone | one connection at a time; a connection that hasn't paired within 30 s is dropped |

Each notify is one dash-mod v1 frame: one JSON object, at most 240 bytes,
`"m"` the mod, `"t"` the type. `"ts"` is unix seconds, left out while DASH's
clock isn't set.

| Frame | When |
| --- | --- |
| `{"m":"atlas","t":"alert","s":88,"id":"a_17","k":"cooler","lvl":2,"msg":"...","ts":...}` | at once, as Atlas sent it ([atlas-protocol.md](mods/atlas-protocol.md)); here `"s"` is Atlas's sequence number. One over 240 bytes is rebuilt with `id`, `k`, `lvl`, `msg` (cut), `snap`, `ts`. The newest open alert is sent again when a phone subscribes. |
| `{"m":"atlas","t":"sum","s":"Asleep, since 3 h ago","ts":...}` | the Atlas Home card line |
| `{"m":"titan","t":"sum","s":"All 12 up"}` | the T.I.T.A.N Home card line |
| `{"m":"faith","t":"sum","s":"..."}` | the FAITH / Projects Home card line |
| `{"m":"dash","t":"bat","v":3.92,"pct":71,"ts":...}` | DASH's cell, volts and percent |

In `sum` frames `"s"` is the text, cut with `...` to keep the frame within
240 bytes. Summaries and the battery go out when they change, at most once
per 10 s each; everything goes out at once when a phone subscribes. Only a
phone paired with the passkey gets notifications: an unencrypted or
"just works" link gets nothing and can't read the characteristic.

## Pairing

1. In the DASH app, add DASH and pick `DASH-xxxx` (DASH's own name is on
   Links, Device info, **Phone**).
2. DASH lights up with **Pair a phone** and a 6-digit code. Type it on the
   phone. A wrong code or **Not mine** drops the phone.
3. Done: Links, Device info, **Phone** says "sending to phone". The phone
   reconnects by itself next time, without a code.

DASH keeps up to 3 paired phones; a fourth replaces the oldest. To forget
them all (a lost phone): Links, Device info, tap **Phone** twice within 5 s.

## What goes wrong

| You see | Why | Fix |
| --- | --- | --- |
| The phone doesn't find `DASH-xxxx` | another phone is connected (one at a time), or the build has `DASH_BLE_REPORTS=0` | disconnect the other phone; check Device info, Phone |
| Pairing fails right after you type the code | wrong code, or the phone kept an old bond after DASH forgot it or was erased | forget DASH in the phone's Bluetooth settings and pair again |
| Pairs, then drops after 30 s | the phone paired without a code ("just works"), which DASH refuses | use the DASH app, which asks for a code |
| Frames arrive cut short (20 bytes) | the phone didn't ask for a bigger MTU | the app must request MTU 247 |
| ESP-NOW from the Atlas bridge gets patchier | Wi-Fi and Bluetooth share one radio and take turns | expected a little; Atlas over MQTT on the truck Wi-Fi doesn't mind. Set `DASH_BLE_REPORTS=0` if it matters. |
| Device info, Memory, is lower than before | the Bluetooth controller and NimBLE take heap | see below |

## Memory and the radio

DASH is an ESP32-WROOM without PSRAM, so this uses NimBLE (h2zero/NimBLE-Arduino
1.4.2), much lighter than Bluedroid, trimmed to one connection and the
peripheral role. `pio run -e freenove_40` with it on: static RAM 92,988 bytes
(was 82,612; +10.4 KB), flash 1,411,141 bytes (was 1,169,241; +236 KB) of the
1,966,080-byte app slot in `min_spiffs.csv`, so 72% used and no partition
change. The controller and host also take heap when they start (not in those
numbers); check Device info, Memory, on the device and expect tens of KB
less than with `DASH_BLE_REPORTS=0`. Wi-Fi keeps its default modem sleep,
which coexistence needs.
