# Architecture

```
                    ┌──────────── D.A.S.H device (ESP32) ────────────┐
 mic / buttons /    │  inputs ─► core (state, UI, audio) ─► connectors   │
 knob / IMU / touch │                                                     │
                    └───────┬──────────┬───────────┬───────────┬─────────┘
                            │WS        │HTTPS      │HTTPS      │Socket.IO
                            ▼          ▼           ▼           ▼
                      Hermes gateway  FAITH      Pondering   Hermes Studio
                      (Gadget plugin)            Orb         (/global-agent)
```

## Principles

1. **One core, many connectors.** The core owns UI, audio capture/playback,
   input events and settings. Each connector implements the same small
   interface (`begin`, `loop`, `onEvent`, `status`) and can be disabled.
2. **Hermes is the conversation path.** Voice turns go through the Gadget
   protocol so STT, tools and TTS are the gateway's job. The device does not
   embed an LLM client.
3. **Direct REST for glanceable data.** Orb and FAITH are read straight from
   the device so the home screen works without a model turn.
4. **Approvals are always on the device.** Anything from Hermes, Studio or
   FAITH that needs a human Yes/No shows as a full-screen card with
   touch + side-button confirm. Locks, alarms and garage actions follow
   FAITH's rule: Yes on the device only, never auto-approved.
5. **Secrets stay minimal.** Per-service tokens stored in NVS, entered via
   phone setup; never logged. LAN only in v0; use TLS (wss/https) beyond it.

## Screens

Home (day + next task from Orb) · Talk (hold-to-talk transcript/reply) ·
Approvals · Agents (Studio run activity) · Tasks (Orb) · Settings.

## Input map

| Input | Action |
| --- | --- |
| Side button A (hold) | push-to-talk |
| Side button B | cancel / back |
| Knob turn | scroll list, adjust volume in Talk |
| Knob click | select / confirm |
| IMU | wake on lift, sleep face-down, shake to cancel |
| Motor | short buzz = approval needed, double = reply done |
