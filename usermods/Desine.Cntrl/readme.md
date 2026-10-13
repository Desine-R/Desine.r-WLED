# Desine.Cntrl

Desine LLC's encoder control usermod. Controls WLED with up to 8 [Adafruit I2C QT Rotary Encoder](https://www.adafruit.com/product/4991)
breakouts (Seesaw-based) on one shared I2C bus.

- **Encoder 1** is the master control with fixed functions.
- **Encoders 2–8** each have two sets of actions, **Mode 1** and **Mode 2**,
  and encoder 1 switches between them.

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
  (advanced)" section at the bottom of the settings page; the device reboots to
  apply). Avoid GPIO12 on ESP32 — it's a boot strapping pin and an I2C pull-up
  will stop the board from booting.

## Controls

### Encoder 1 (fixed)

| Control | Action |
|---|---|
| Rotate | Dimmer (master brightness) |
| Short press | Switch encoders 2–8 between Mode 1 and Mode 2. All encoder LEDs flash: **orange = Mode 1**, **teal = Mode 2** |
| Long press | Lights on/off |

The device starts in Mode 1 after power-up. The Info page shows the active mode.

### Encoders 2–8 (Mode 1 and Mode 2 each)

| Control | Options | Behavior |
|---|---|---|
| **Rotate** | Off, Dimmer, Spd, Int, Palette, Pri Hue, Sec Hue, Warm White | Each click nudges the value by Step (out of 255). Hue wraps around; others stop at min/max. Palette moves one palette per click. |
| **Short press** | None, Anim Next, Anim Prev, Palette Next, Palette Prev, On/Off | Fires on release, if released within 0.8 s |
| **Long press** | same as short press | Fires once while held, at 0.8 s (does not also fire the short press) |
| **Step** | 1–64 (default 8) | How much one click changes the value, set separately per mode |

Defaults (Mode 1 / Mode 2 rotate): encoder 2 = Palette / Pri Hue, encoder 3 =
Spd / Sec Hue, encoder 4 = Int / Warm White, encoders 5–8 = Off. Encoder 2's long
press = Anim Next in both modes; all other presses = None.

### Color schemes

There are three color schemes, and only one is active at a time. **Turning a
knob that belongs to a scheme switches to it:**

| Scheme | Activated by | What it does |
|---|---|---|
| **Custom palette** | Palette knob, Palette Next/Prev | Steps through your custom palettes only; WLED's built-in palettes are skipped. Coming back from another scheme returns to the custom palette you were on. |
| **Pri/Sec hue** | Pri Hue or Sec Hue knob | Two colors set by the hue knobs, blended along the strip (WLED's "Colors 1&2" palette). White LEDs off. |
| **Warm white** | Warm White knob | The strip's white LEDs plus an amber tint: turn up for warmer (toward candle-like), down for neutral white. Animations keep running in warm tones. |

With no custom palettes saved, the Palette knob does nothing.

**Anim Next/Prev** steps through a list of up to 13 WLED preset numbers ("Anim 1"
… "Anim 13" in the Favorites section; 0 = unused). Save your favorite looks as
regular WLED presets first, then enter their numbers here. A preset can change
the palette and colors; the next knob turn puts its scheme back in charge.

Changes affect the first selected segment.

## Settings page (Config → Usermods)

Sections, each with a heading and divider line: **Encoder - 1** … **Encoder - 8**,
**Encoder LEDs**, **Favorites**, and **I2C Pins (advanced)** at the very bottom.
Each encoder section has its I2C address (dropdown showing which jumpers to
bridge); encoder 1 adds its Step, the others add their Mode 1 and Mode 2 sets.

Each encoder heading says "detected" or "not detected". Undetected encoders are
greyed out but still editable, so you can set them up before wiring them in.
Encoders are re-checked every second, so plugging one in or out works without
a reboot (reload the settings page to see the updated status). The Info page
shows "Encoders: N of 8 detected".

### Encoder LEDs

| Setting | Default | Behavior |
|---|---|---|
| **Mode** | Palette gradient | *Palette gradient*: the detected encoders show the palette's gradient together. The first detected encoder shows the start of the palette and the last shows the end, spread evenly however many are connected. *Off*: all LEDs off (no mode-switch flash either). |
| **Brightness (%)** | 50 | Brightness of all encoder LEDs |
| **Follow strip power** | on | When the strip is turned off, the LEDs turn off too, except the first detected encoder, which stays on at 20% of the LED brightness |

The colors follow palette changes and transitions. With multiple WLED
segments, they show the palette of the last segment drawn.

## Build

Add `Desine.Cntrl` to `custom_usermods` in `platformio_override.ini`
(see the `desine_cntrl` env). No other code changes are needed.
Dependencies (Adafruit seesaw, BusIO, SPI) are declared in `library.json`.

Warm white tuning (amber ratio, how far the white LEDs dim at full warmth) and
the mode flash colors are `#define`s at the top of `usermod_desine_cntrl.h`.

## Troubleshooting

At boot the usermod scans the I2C bus and prints every address it finds, then
`encoder N ready` / `not found` for each slot, on the serial port (115200 baud).
If no devices are found, check wiring, power, and the SDA/SCL pin settings.
