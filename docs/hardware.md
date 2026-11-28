# Hardware

Board: Freenove ESP32 Display 4.0" 320x480 resistive touch (ESP32-WROOM-32E N4:
dual core, 520 KB SRAM, 4 MB flash, no PSRAM; Wi-Fi + Bluetooth 4.2 LE).
Freenove repo: https://github.com/Freenove/Freenove_ESP32_Display (sketches,
TFT_eSPI setups, schematic PDF).

Confidence: **sketch** = a pin defined in Freenove's own example code (high).
**schematic** = read from text extracted from Freenove's schematic PDF (medium;
the PDF extraction is messy). Verify anything marked schematic with a meter or
the PDF before soldering.

## Onboard pins

| Function | GPIO | Source |
| --- | --- | --- |
| LCD (ST7796, HSPI) | MISO 12, MOSI 13, SCLK 14, CS 15, DC 2, backlight 27, RST not used (board reset) | sketch (`FNK0114S_4.0_320x480_ST7796.h`) |
| Touch (XPT2046) | shares the LCD SPI bus; CS 33; IRQ 36 | sketch (CS), schematic (IRQ) |
| TF card (VSPI) | SCK 18, MISO 19, MOSI 23, CS 5 | sketch |
| RGB LED | R 22, G 16, B 17, active low | sketch |
| Speaker amp | amp enable IO4 (sketch drives it LOW to play); audio in from DAC IO26 | sketch (IO4), schematic (IO26) |
| Battery voltage | IO34, ADC; battery mV = reading x 2 | sketch |
| BOOT key | IO0 (usable as a button) | sketch |
| USB serial / UART0 | TX IO1, RX IO3 | schematic |

## Connectors

| Connector | Pins | Notes |
| --- | --- | --- |
| Input pin port | IO35, IO39 | input only, no internal pull-up |
| SPEAKER | SP+, SP- | onboard amp (SC8002B) |
| SPI port | IO23 (MOSI), IO19 (MISO), IO18 (SCK), IO21 (CS) | same lines as the TF slot, except IO21 |
| I2C port | 3.3V, IO32 (SDA), IO25 (SCL), GND | **free**: touch is on the LCD bus, not these |
| UART port | 5V, TXD, RXD, GND | UART0, same as USB serial |
| BAT port | BAT+, GND | see Battery |
| TF card slot | via the SPI pins above | |

Correction to earlier notes: touch is **not** on IO32/IO25, so the I2C port is free.

## Battery

The schematic shows a **TP4054** single-cell Li-ion charger on the USB-C VBUS
line, feeding BAT+, with an ME6217C33 3.3 V regulator. So the board should
charge a 4.2 V Li-ion/LiPo cell from its own USB-C, as you wanted, with no extra
charger board. To check before connecting the 18500 cell:

- **Polarity**: the BAT connector has a "+" marked; confirm with a multimeter
  before plugging in. Wrong polarity can destroy the board.
- Charge current is set by a resistor we have not read; TP4054 maxes at about
  500 mA, fine for 1500 mAh.
- The cell should have a protection circuit. The cell in the photo has a loose
  red wire and no visible protection board, so verify.
- No boost converter appears in the schematic text, so the 5 V rail (UART 5V pin)
  likely exists only while USB is plugged in. Do not power the motor or other 5 V
  parts from it if you want battery operation. Not yet tested.

## Plan for the add-ons

The microSD slot stays in use (Files), so the knob, side buttons and motor
go on an **MCP23017** I/O expander (about $2) on the free I2C port.

| Part | Pins | Why |
| --- | --- | --- |
| microSD (Files) | SCK 18, MISO 19, MOSI 23, CS 5 | the built-in TF slot on VSPI |
| MCP23017 expander | SDA IO32, SCL IO25, address 0x20, INTA to IO35 (10k pull-up) | frees the SD pins; one interrupt line for all inputs |
| IMU (I2C) | SDA IO32, SCL IO25 | same bus as the expander |
| INMP441 mic | BCLK IO21, WS IO22, DIN IO39 | I2S1; the speaker DAC uses I2S0. DIN is an input, so an input-only pin works. Gives up the red LED. |
| Rotary encoder | expander GPA0 (A), GPA1 (B), GPA2 (click) | internal pull-ups on the expander |
| Side button A (push-to-talk) | expander GPA3 | internal pull-up |
| Side button B (cancel) | expander GPA4; IO0 (BOOT key) until then | |
| Vibration motor | expander GPB0 via N-MOSFET + flyback diode | power from the battery side, not 5 V |
| Slider as volume (optional) | IO39 | ADC1; conflicts with the mic DIN, so choose one |

The green and blue LED pins (IO16, IO17) stay free with this plan. The
expander driver lands with the inputs milestone (M3); until then the firmware
keeps the BOOT key and the optional IO36 button.

Rules: use only ADC1 pins (32-39) for analog reads, because ADC2 stops working
with Wi-Fi on. Pins 34-39 are input only and have no pull-ups. Not yet verified
on the board: audio amp enable polarity, charge current and battery-powered
operation.
