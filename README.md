# D.A.S.H: Direct Agent System Hub

A pocket-sized ESP32 companion that talks to your whole homelab agent stack:
hold a button, speak, and the answer, approvals and your day appear on a small
touchscreen. Formerly called PocketAgent.

## Connects to

| System | Role on the device | Transport |
| --- | --- | --- |
| **Hermes Agent** (Gadget plugin) | voice chat, display cards, device actions, command approvals | WebSocket `hermes-gadget.v1` on `:8765/gadget` |
| **FAITH** | family/household view, Yes/No for held actions (locks, alarms), voice fallback | HTTPS, paired like a wall screen: `/api/chat`, `/api/voice/*`, `/api/chat/confirm/:id` |
| **Pondering Orb** | tasks, today, chores, projects glance; mark done | HTTPS REST + token from `ORB_API_TOKENS` |
| **Hermes Studio** | live agent activity (runs, tools, approvals) | Socket.IO `/global-agent`, `mcu.events.subscribe` |

| **Atlas** (truck homelab, a mod) | OBD2 gauges, check-engine codes in words, sync, weather | ESP-NOW and MQTT on the truck Wi-Fi ([docs/mods/atlas.md](docs/mods/atlas.md)) |
| **T.I.T.A.N** (homelab status, a mod) | what's up, what's down and why | `GET /api/glance` with a read-only token ([docs/mods/titan.md](docs/mods/titan.md)) |
| **Home Assistant** (a mod) | a few entities, alerts on the light and motor, optional toggles | REST `GET /api/states/<id>` with a long-lived token ([docs/mods/ha.md](docs/mods/ha.md)) |

Every connector is optional and has its own on/off switch in device settings.

**Files:** DASH keeps files on its microSD card and shares them over its own
Wi-Fi with a QR code: people nearby view, download and upload from a browser,
and only you can delete ([docs/files.md](docs/files.md)).

**Phone:** DASH sends Atlas alerts, each mod's summary and its battery to
the paired DASH phone app over Bluetooth, no Wi-Fi needed
([docs/ble-reports.md](docs/ble-reports.md)).

**Mods** add integrations for other apps and devices, each with its own
screen, settings and Home card ([docs/mods/README.md](docs/mods/README.md)).
The first is Atlas, the truck homelab.

## Hardware (v0)

ESP32-32E 4.0" 320x480 resistive-touch board, 18500 Li-ion cell + USB-C
charge/boost board, INMP441 I2S mic, vibration motor, IMU, rotary encoder with
click, two recessed side buttons. Wi-Fi for all links; BLE for phone setup.
See [docs/hardware.md](docs/hardware.md).

## Status

M1 bring-up firmware is written and builds (screen, touch, Wi-Fi, battery,
buttons, a FAITH health link), with host unit tests in CI. Nothing is flashed or
tested on hardware yet. See [firmware/README.md](firmware/README.md),
[docs/roadmap.md](docs/roadmap.md) and [docs/open-questions.md](docs/open-questions.md).

## Docs

- [Architecture](docs/architecture.md)
- [Hardware and wiring](docs/hardware.md)
- [Integrations](docs/integrations.md)
- [Roadmap](docs/roadmap.md)
- [Open questions](docs/open-questions.md)
- [Files](docs/files.md)
- [Mods](docs/mods/README.md), [Atlas](docs/mods/atlas.md), [Atlas protocol](docs/mods/atlas-protocol.md), [T.I.T.A.N](docs/mods/titan.md), [Home Assistant](docs/mods/ha.md)
- [Bluetooth live reports](docs/ble-reports.md) (to the phone, paired with a passkey)
