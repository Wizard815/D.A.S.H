# Mods

A mod is an integration for another app or device: its own screen, data
source, settings and Home card. There are four so far: **FAITH** (its health
check and the family's projects), **Atlas**, the truck homelab
([atlas.md](atlas.md)), **T.I.T.A.N**, the homelab status board
([titan.md](titan.md)), and **Home Assistant**, a few entities and alerts
([ha.md](ha.md)). Overseer and Argos fit the same shape.
The DASH phone app (planned) carries the same mods, so there is one app for
DASH and its mods; FAITH stays its own server and family app.

Mods are compiled in, because an ESP32 can't load code at run time. Each has
a build flag (`DASH_MOD_<NAME>=1` in `platformio.ini`) and an On/Off switch in
**Links**, saved on the device. Files on the SD card are built in, not a mod.

## What a mod gets

| Part | How |
| --- | --- |
| A row in Links | it's a link (`dash::Connector`): name, state in words, On/Off |
| Settings | a list of `dash::ModSetting` choices; tap its row in Links. Saved per mod. |
| A screen | `dash::ModView`: `draw`, `tap`, `dirty`, `opened`, drawn with `src/gfx.h` |
| A tab | up to two mods with a screen get their own tabs; with three or more the rest share a **Mods** tab |
| A Home card | `homeCard()`: a title and one line, with "needs a look" said in words |
| Frames | `onFrame()` gets every dash-mod frame whose `"m"` is its id, from ESP-NOW, MQTT or anything else |

## The phone app

DASH is one app: the device plus a phone app (first version in
[`android/`](../../android), see [../app/README.md](../app/README.md)). You install mods in
the app; the app turns them on and sets them up on the device, and sends the
device a firmware update when a mod needs code it doesn't have yet (an ESP32
can't load code at run time, so device-side mods arrive as an update). The
phone side of a mod does what the device can't: Atlas's owner tools, playing
video, bigger screens.

## dash-mod frames (v1)

The same small message works over every transport, so a mod doesn't care
whether its peer is an ESP32 or a Pi.

- One JSON object, at most **240 bytes** (ESP-NOW carries 250).
- `"m"`: the mod id (`"atlas"`). `"t"`: the type. `"s"`: the sender's sequence number.
- Optional `"ts"`: unix seconds, so DASH can tell time with no internet.
- Data fields are optional; a missing field means "unknown", never zero.
- Metric units on the wire. DASH converts for the screen.

Transports:

| Transport | Shape |
| --- | --- |
| ESP-NOW | the JSON is the whole payload. Encrypted with a shared 16-character key (`DASH_ESPNOW_KEY`). DASH listens on its Wi-Fi channel. Once a peer MAC is set, other senders are ignored. |
| MQTT | topic `dash/v1/<mod>/<t>`, retained, so DASH gets the latest of each type on connect. DASH sends on `dash/v1/<mod>/dash`. |
| UART | one frame per line, 115200 baud (the Atlas bridge: Pi to ESP-NOW). |

## Add a mod

1. Pure logic in `firmware/lib/dash_core/src/<name>.{h,cpp}`: parse its frames
   or REST replies into a fixed-size struct, plus words for the screen. Test it
   in `firmware/test/test_core` (`pio test -e native`).
2. The mod in `firmware/src/mods/<name>_mod.{h,cpp}`: a class that is both
   `dash::Mod` and `dash::ModView`. See `atlas_mod.*`.
3. Add it in `firmware/src/mods/mods.cpp` under `#if DASH_MOD_<NAME>`, and the
   flag in `platformio.ini`.
4. Docs in `docs/mods/<name>.md`: what it shows, setup, and what goes wrong.

Rules that carry over from the rest of DASH: state in words, never colour
alone; touch targets 44 px or more; no secrets in logs; read-only until a
confirm step exists for anything that acts (unlocking, remote start).

## Ideas

| Mod | Data source | Screen |
| --- | --- | --- |
| Home Assistant (more) | MQTT on the home broker, for pushed states | Yes/No for held actions through FAITH |
| Overseer | `GET /api/readiness` and health | collector status, training readiness |
| Argos | its HTTP API | model routing, which models are loaded |
