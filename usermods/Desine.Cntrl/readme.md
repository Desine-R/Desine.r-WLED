# Desine.Cntrl

Desine LLC's encoder control usermod. Controls WLED with up to 8 [Adafruit I2C QT Rotary Encoder](https://www.adafruit.com/product/4991)
breakouts (Seesaw-based) on one shared I2C bus. Each encoder's knob and its
built-in pushbutton are configured independently from the WLED settings page.

## Hardware

- Up to 8 encoders, daisy-chained with STEMMA QT cables, powered at 3.3V. Keep the
  chain reasonably short; with 8, the encoder LEDs together draw noticeably more
  from the board's 3.3V supply. More than 8 would need an I2C multiplexer.
- Each encoder needs a unique I2C address, set with the solder jumpers on its back:

  | Encoder | Address | Jumpers bridged |
  |---|---|---|
  | 1 | 0x36 | none |
  | 2 | 0x37 | A0 |
  | 3 | 0x38 | A1 |
  | 4 | 0x39 | A0 + A1 |
  | 5 | 0x3A | A2 |
  | 6 | 0x3B | A0 + A2 |
  | 7 | 0x3C | A1 + A2 |
  | 8 | 0x3D | A0 + A1 + A2 |

- Default I2C pins: SDA = GPIO32, SCL = GPIO15 (changeable in the "I2C Pins
  (advanced)" section at the bottom of the settings page; the device reboots to apply). Avoid GPIO12 on ESP32 — it's a boot strapping pin
  and an I2C pull-up will stop the board from booting.

## Settings (Config → Usermods)

The page is split into sections, each with a heading and a divider line:
**Encoder - 1** … **Encoder - 8**, **Encoder LEDs**, **Favorites**, and
**I2C Pins (advanced)** at the very bottom, so the hardware pins aren't the
first thing someone changes. Each encoder section has: I2C address (dropdown, shows
which jumpers to bridge), Rotate, Short press, Long press, and Step.

Each encoder heading says "detected" or "not detected". Undetected encoders are
greyed out. They're still editable, so you can set them up before wiring them in.
Encoders are re-checked every second, so plugging one in or out works without
a reboot (reload the settings page to see the updated status). The Info page
shows "Encoders: N of 8 detected".

| Control | Options | Behavior |
|---|---|---|
| **Rotate** | Off, Dimmer, Spd, Int, Palette, Pri Hue, Sec Hue, White | Each click nudges the current value by the encoder's Step (out of 255). Hue wraps around; others stop at min/max. Palette moves one palette per click within the active palette mode (see below). |
| **Step** | 1–64 (default 8) | How much one click changes the value |
| **Short press** | None, Anim Next, Anim Prev, Palette Next, Palette Prev, On/Off, Palette Mode | Fires on release, if released within 0.8 s |
| **Long press** | same as short press | Fires once while held, at 0.8 s (does not also fire the short press) |

Defaults: encoders 1–5 rotate Dimmer / Spd / Int / Pri Hue / Sec Hue, encoders
6–8 rotate Off; short press does nothing; long press = Anim Next.

**Anim Next/Prev** steps through a list of up to 13 WLED preset numbers ("Anim 1"
… "Anim 13" in the Favorites section; 0 = unused). Save your favorite looks as
regular WLED presets first, then enter their numbers here.

**White** adjusts only the W channel of the primary and secondary colors, without
changing hue.

Changes affect the first selected segment.

### Palette modes

The Palette knob and Palette Next/Prev only step through one of two sets:

| Mode | Palettes | Use |
|---|---|---|
| **Custom** | Your custom palettes only (WLED's built-in palettes are skipped) | Curated looks |
| **Color** | WLED's two-color palettes: "Colors 1&2" (smooth blend from primary to secondary) and "Colors Only" (solid blocks of each) | Colors set live with the Pri Hue / Sec Hue knobs |

Assign **Palette Mode** to a press (e.g. a long press) to switch between them.
Switching back to Custom returns to the custom palette you were on. The mode
follows whatever palette is active, so it survives reboots. If a built-in
palette gets selected (from the web page or a preset), the next Palette
Next/Prev or knob turn jumps into your custom palettes. With no custom palettes
saved, Custom mode does nothing.
### Encoder LEDs

Each encoder's built-in RGB LED can show the current palette.

| Setting | Default | Behavior |
|---|---|---|
| **Encoder LEDs** | Palette gradient | *Palette gradient*: the detected encoders show the palette's gradient together. The first detected encoder shows the start of the palette and the last shows the end, spread evenly however many are connected. *Off*: all LEDs off. |
| **LED brightness (%)** | 50 | Brightness of all encoder LEDs |
| **LEDs follow strip power** | on | When the strip is turned off, the LEDs turn off too, except the first detected encoder, which stays on at 20% of the LED brightness |

The colors follow palette changes and transitions. With multiple WLED
segments, they show the palette of the last segment drawn.

## Build

Add `Desine.Cntrl` to `custom_usermods` in `platformio_override.ini`
(see the `desine_cntrl` env). No other code changes are needed.
Dependencies (Adafruit seesaw, BusIO, SPI) are declared in `library.json`.

## Troubleshooting

At boot the usermod scans the I2C bus and prints every address it finds, then
`encoder N ready` / `not found` for each slot, on the serial port (115200 baud).
If no devices are found, check wiring, power, and the SDA/SCL pin settings.
