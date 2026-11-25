# Open questions

- Keep the 320x480 ESP32-32E (port the SDK) or move to a supported ESP32-S3 board?
  For now: PlatformIO + Arduino on the 32E. The Gadget SDK is ESP-IDF, so M2
  either ports its protocol to Arduino or moves the build to ESP-IDF. No PSRAM
  means audio buffers must stay small (about 32 KB).
- Does the board have a battery connector or charge path?
- Which FAITH person signs in on the device, and for how long. Paired-screen
  sessions are short by design, so pocket use may want a longer hub session
  setting in FAITH.
- Bluetooth to phone: setup only, or live notifications/keyboard link?
