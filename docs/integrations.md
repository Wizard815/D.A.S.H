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
Use a per-person `faith token` for `/v1/chat/completions`. Device pairing like a
wall tablet: `faith pair <name>`. Approvals: `POST /api/chat/confirm/:id`.
Sign the device in as one person (not the household view) so personal data such
as Orb is allowed.

## Pondering Orb
Add `pocket:<token>` to `ORB_API_TOKENS`. Read: tasks, day, chores. Write:
complete a task. Orb's agent work is reported to Studio chats; the device shows
status only.

## Hermes Studio
Subscribe over Socket.IO to `mcu.events.subscribe`; events include
`chat.run.*`, `chat.tool.*`, `chat.approval.requested`,
`chat.clarification.requested`. Studio does not yet accept approve/deny from the
device; that needs a small server addition (see roadmap).
