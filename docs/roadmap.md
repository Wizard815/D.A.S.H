# Roadmap

1. **M0 Paper**: pin map from the vendor schematic, parts list, wiring diagram.
2. **M1 Bring-up**: display + touch + Wi-Fi + battery % on a bare board.
   *Code written and building, with host tests and CI; not yet flashed.* See
   [firmware/README.md](../firmware/README.md) for what to check on first flash.
3. **M2 Hermes link**: Gadget WS client, pairing, hold-to-talk, reply card.
   Move links to their own FreeRTOS task first, so a slow server never freezes the screen.
4. **M3 Inputs**: knob, side buttons, motor, IMU through the expander.
5. **M4 Orb**: Home screen with today + next task, mark done.
6. **M5 Studio events**: Agents screen and approval cards.
7. **M6 FAITH**: pair as a hub screen, PIN sign-in, confirms, voice fallback
   (see [integrations](integrations.md#faith)).
8. **M7 Phone BLE setup**, OTA, enclosure.
