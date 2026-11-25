# Integrations

## Hermes Agent: Gadget plugin
Install on the Hermes host:
```
hermes plugins install https://github.com/Adolanium/hermes-gadget-sdk/tree/main/plugin --enable
hermes gateway setup
```
Protocol `hermes-gadget.v1`: JSON text frames + binary frames (channel 1 audio,
2 image, 3 firmware, 4 face). Pair with `hermes gadget pair`. Device-declared
actions here: `haptic.buzz`, `screen.brightness`, `speaker.volume`.
Gotcha: the SDK lists no 320x480 ST7796 board yet, so this project needs a
board port (ESP-IDF, `firmware/esp32/main/board.cpp` upstream).

## FAITH
Pair the device like a wall screen, not with an API token:

1. On FAITH: add the device to `hub.devices`, then make a code with
   `POST /api/admin/pair-code {"device": "dash"}` (or the admin page).
2. On the device: `POST /api/pair {"code": "..."}`. FAITH answers with a
   `faith_device` cookie; keep it in NVS and send it on every request.
3. That alone is the **household** view. To see personal data (and Orb), a
   person signs in on the device with their PIN (`POST /api/auth/pin`), which
   sets a short `faith_session` cookie.

Why not a `faith token`: bearer tokens get FAITH's `api` channel, and
`POST /api/chat/confirm/:id` refuses that channel ("Confirm on a FAITH
screen"). So a token-only device could chat but never answer Yes/No for locks
and alarms. Paired-screen cookies get the `hub` channel, which can.

Endpoints the device uses: `GET /api/health` (M1, done), `GET /api/session`,
`POST /api/chat` (server-sent events), `POST /api/chat/confirm/:id`,
`GET /api/approvals`, `POST /api/approvals/:id`, `POST /api/voice/transcribe`
and `/api/voice/speak` (M6).

## Pondering Orb
Add `pocket:<token>` to `ORB_API_TOKENS`. Read: tasks, day, chores. Write:
complete a task. Orb's agent work is reported to Studio chats; the device shows
status only.

## Hermes Studio
Subscribe over Socket.IO to `mcu.events.subscribe`; events include
`chat.run.*`, `chat.tool.*`, `chat.approval.requested`,
`chat.clarification.requested`. Studio does not yet accept approve/deny from the
device; that needs a small server addition (see roadmap).
