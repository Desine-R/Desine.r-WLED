# Seesaw Encoder Control

Controls WLED with up to 5 [Adafruit I2C QT Rotary Encoder](https://www.adafruit.com/product/4991)
breakouts (Seesaw-based) on one shared I2C bus. Each encoder's knob and its
built-in pushbutton are configured independently from the WLED settings page.

## Hardware

- Up to 5 encoders, daisy-chained with STEMMA QT cables, powered at 3.3V.
- Each encoder needs a unique I2C address, set with the solder jumpers on its back:

  | Encoder | Address | Jumpers bridged |
  |---|---|---|
  | 1 | 0x36 | none |
  | 2 | 0x37 | A0 |
  | 3 | 0x38 | A1 |
  | 4 | 0x39 | A0 + A1 |
  | 5 | 0x3A | A2 |

- Default I2C pins: SDA = GPIO32, SCL = GPIO15 (changeable on the settings page;
  the device reboots to apply). Avoid GPIO12 on ESP32 — it's a boot strapping pin
  and an I2C pull-up will stop the board from booting.

## Settings (Config → Usermods)

Each encoder gets one row: `Enc1 [Rotate ▾] [Short ▾] [Long ▾]`

| Control | Options | Behavior |
|---|---|---|
| **Rotate** | Off, Dimmer, Spd, Int, Palette, Pri Hue, Sec Hue, White | Each click nudges the current value by 4 (out of 255). Hue wraps around; others stop at min/max. Palette steps one palette per click. |
| **Short press** | None, Anim Next, Anim Prev, Palette Next, Palette Prev, On/Off | Fires on release, if released within 0.8 s |
| **Long press** | same as short press | Fires once while held, at 0.8 s (does not also fire the short press) |

Defaults: encoders 1–5 rotate Dimmer / Spd / Int / Pri Hue / Sec Hue; short
press does nothing; long press = Anim Next.

**Anim Next/Prev** steps through a list of up to 8 WLED preset numbers ("Anim 1"
… "Anim 8" fields below the encoders; 0 = unused). Save your favorite looks as
regular WLED presets first, then enter their numbers here.

**White** adjusts only the W channel of the primary and secondary colors, without
changing hue.

Changes affect the first selected segment.

## Build

Add `Seesaw_encoder_control` to `custom_usermods` in `platformio_override.ini`
(see the `custom_esp32_diguno_encoders` env). No other code changes are needed.
Dependencies (Adafruit seesaw, BusIO, SPI) are declared in `library.json`.

## Troubleshooting

At boot the usermod scans the I2C bus and prints every address it finds, then
`encoder N ready` / `not found` for each slot, on the serial port (115200 baud).
If no devices are found, check wiring, power, and the SDA/SCL pin settings.
