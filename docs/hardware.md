# Hardware

> Pin numbers below are **unverified**. They come from memory of this board
> family, not from the vendor schematic. Confirm against the schematic for
> your exact board before soldering.

## Parts

- ESP32-32E 4.0" 320x480 resistive-touch display board (ESP32-WROOM-32E N4:
  4 MB flash, likely no PSRAM)
- 18500 Li-ion, 1500 mAh (check it has a protection circuit)
- USB-C charge + 5 V boost module (the board shows no battery connector; check
  the edge near its USB-C port before assuming it can charge)
- INMP441 I2S microphone
- Small vibration motor + N-MOSFET + flyback diode
- IMU (QMI8658 is the Hermes Gadget SDK's reference chip)
- Rotary encoder with push switch (EC11)
- Two recessed side buttons

## Pin budget

The display, touch, TF slot, RGB LED, speaker DAC and battery ADC use most
GPIOs. Plan:

1. Give up the TF slot and RGB LED to free their pins.
2. Mic on I2S1 (the DAC speaker is on I2S0).
3. IMU, expander on one I2C bus.
4. Encoder, buttons and motor on an I2C GPIO expander (e.g. MCP23017) with one
   interrupt line.
5. Analog reads only on ADC1 (ADC2 stops working with Wi-Fi on).
6. The "I2C" header pins may be shared with touch on this board; verify before
   using them.

## Power

USB-C -> charge/boost module -> 5 V into the board, with a slide switch.
Battery voltage via divider to an ADC1 pin for the battery %.
