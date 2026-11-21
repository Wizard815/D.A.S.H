# D.A.S.H: Direct Agent System Hub

A pocket-sized ESP32 companion that talks to your whole homelab agent stack:
hold a button, speak, and the answer, approvals and your day appear on a small
touchscreen. Formerly called PocketAgent.

## Connects to

| System | Role on the device | Transport |
| --- | --- | --- |
| **Hermes Agent** (Gadget plugin) | voice chat, display cards, device actions, command approvals | WebSocket `hermes-gadget.v1` on `:8765/gadget` |
| **FAITH** | family/household view, Yes/No for held actions (locks, alarms), voice fallback | HTTPS: `/v1/chat/completions`, `/api/voice/*`, `/api/chat/confirm/:id` |
| **Pondering Orb** | tasks, today, chores, projects glance; mark done | HTTPS REST + token from `ORB_API_TOKENS` |
| **Hermes Studio** | live agent activity (runs, tools, approvals) | Socket.IO `/global-agent`, `mcu.events.subscribe` |

Every connector is optional and has its own on/off switch in device settings.

## Hardware (v0)

ESP32-32E 4.0" 320x480 resistive-touch board, 18500 Li-ion cell + USB-C
charge/boost board, INMP441 I2S mic, vibration motor, IMU, rotary encoder with
click, two recessed side buttons. Wi-Fi for all links; BLE for phone setup.
See [docs/hardware.md](docs/hardware.md).

## Status

Planning. Nothing here is flashed or tested on hardware yet. See
[docs/roadmap.md](docs/roadmap.md) and [docs/open-questions.md](docs/open-questions.md).

## Docs

- [Architecture](docs/architecture.md)
- [Hardware and wiring](docs/hardware.md)
- [Integrations](docs/integrations.md)
- [Roadmap](docs/roadmap.md)
- [Open questions](docs/open-questions.md)
