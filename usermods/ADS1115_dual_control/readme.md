# ADS1115 Dual Control

Controls WLED with up to 8 potentiometers and/or buttons read through two
ADS1115 ADC boards on one shared I2C bus, plus two GPIO buttons for raw
next/previous effect cycling.

## Hardware

| Board | Address | ADDR pin | Channels |
|---|---|---|---|
| ADS1115 #1 | 0x48 | GND | Ch1–Ch4 (A0–A3) |
| ADS1115 #2 | 0x49 | 3.3V | Ch5–Ch8 (A0–A3) |

Default pins (QuinLED Dig-Uno): SDA = GPIO32, SCL = GPIO15, next button =
GPIO0, prev button = GPIO2. All are changeable on the settings page. Avoid
GPIO12 for I2C on ESP32 — it's a boot strapping pin and the ADS1115's pull-up
will stop the board from booting.

## Settings (Config → Usermods)

Each channel gets one row: `Ch1 [Mode ▾] [Action ▾]`. The Action list changes
based on the Mode:

| Mode | Actions |
|---|---|
| **Off** | — |
| **Pot** | Dimmer, Spd, Int, Palette, Pri Hue, Sec Hue, White, Anim (pot position selects across the favorites list) |
| **Button** | Palette Next, Palette Prev, Anim Next, Anim Prev |

Button mode detects a press at either end of the ADC range (wiring polarity
doesn't matter), with hysteresis so each press fires once.

Defaults: Ch1–Ch6 = Pot (Dimmer, Spd, Int, Palette, Pri Hue, Sec Hue);
Ch7–Ch8 = Off.

**Anim** uses a list of up to 8 WLED preset numbers ("Anim 1" … "Anim 8"; 0 =
unused). Save looks as regular WLED presets, then enter their numbers here.

**White** adjusts only the W channel of the primary and secondary colors.

## Build

Add `ADS1115_dual_control` to `custom_usermods` in `platformio_override.ini`
(see the `custom_esp32_diguno_ads1115` env). Dependencies are in `library.json`.
