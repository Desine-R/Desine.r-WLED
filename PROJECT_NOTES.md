# Desine.Cntrl — Project Notes

Desine.Cntrl is Desine LLC's custom WLED firmware for rotary-encoder control.

This is the single source of truth for this project. It's meant to be read by
humans and by any AI coding assistant (Claude, Copilot, Cursor, Codex, ...).
**Keep it updated** when decisions are made or status changes — chat history
and tool-specific memory don't travel between machines or tools; this file does.

This folder is a clone of upstream WLED (`wled/WLED`, tag `v16.0.1`) with our
own usermods and build config added. Upstream's coding rules are in
[AGENTS.md](AGENTS.md) (below our project pointer at the top).

---

## 1. What we're building

Custom WLED firmware for a client lighting install: one SK6812 RGBW strip,
controlled by physical knobs over I2C, on a QuinLED Dig-Uno with Ethernet.

Two interchangeable control front-ends exist, each its own usermod and its own
build environment — only one is compiled into any given firmware:

| Usermod | Hardware | Build env | Status |
|---|---|---|---|
| [Desine.Cntrl](usermods/Desine.Cntrl/readme.md) | 5× Adafruit I2C QT Rotary Encoders | `desine_cntrl` | **Current client direction.** Flashed, encoder 1 confirmed on hardware |
| [ADS1115_dual_control](usermods/ADS1115_dual_control/readme.md) | 2× ADS1115 ADCs (8 pots/buttons) + 2 buttons | `custom_esp32_diguno_ads1115` | Earlier design, kept available. Flashed, boots clean |

The client pivoted from pots (ADS1115) to rotary encoders. The ADS1115 build is
kept rather than deleted so it stays an option.

## 2. Hardware

**Controller:** QuinLED Dig-Uno v3.1 r2 on a QuinLED-ESP32 base module +
QuinLED-ESP32-Ethernet add-on.

| GPIO | Use | Notes |
|---|---|---|
| 16 | LED data (SK6812 RGBW) | `DATA_PINS=16`, `LED_TYPES=30` |
| 32 (Q4) | I2C SDA | See GPIO12 warning below |
| 15 (Q1) | I2C SCL | |
| 0 | Next-effect button (ADS1115 build only) | Board's native button pin, pulled high |
| 2 (Q3) | Prev-effect button (ADS1115 build only) | |
| 12 (Q2) | **Spare — do not use with anything that has a pull-up** | |
| 13 | Onboard DS18B20 temp sensor | Untouched |
| 36 (A0) | Onboard analog mic input | Not wired yet; audioreactive pins disabled in build flags until it is |
| 5/17/18/19/21/22/23/25/26/27 | Ethernet | Reserved by WLED's built-in QuinLED-ESP32-Ethernet profile (`WLED_ETH_DEFAULT=4`) |

No relay on this board (`RLYPIN=-1`).

**GPIO12 warning:** GPIO12 is an ESP32 flash-voltage strapping pin, sampled at
every reset. I2C pull-up resistors (e.g. on the ADS1115 breakout) hold it HIGH,
which makes the bootloader pick the wrong flash voltage and the board fails to
boot. Reproduced 100% of the time. That's why SDA moved from GPIO12 to GPIO32.

**Earlier hardware, abandoned:** GLEDOPTO Elite 4D-EXMU (GL-C-618WL). Its 4
"spare" GPIO connectors all sit behind a 74HCT125 buffer — one-way 5V LED-data
outputs, not bidirectional GPIO — so I2C can never work on them. Don't revisit.

## 3. Build & flash

**Prerequisites:** Node.js LTS (required: WLED's build script aborts without it)
and Git. On Windows: `winget install OpenJS.NodeJS.LTS` and `winget install Git.Git`,
run one at a time (two installers at once fails with exit code 1618).

**Toolchain:** PlatformIO. Easiest install is the PlatformIO extension for
VS Code, which puts PlatformIO Core at
`%USERPROFILE%\.platformio\penv\Scripts\platformio.exe` (that's the path to use
from a terminal; `pio`/`python` are typically not on PATH on our machines).

```powershell
# one-time per checkout: web UI headers are generated, not committed
npm ci
npm run build

# build
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e desine_cntrl

# build + flash (check Device Manager for the COM port)
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e desine_cntrl -t upload --upload-port COM6

# serial boot log
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" device monitor -p COM6 -b 115200
```

Build envs are defined in `platformio_override.ini` (note: upstream WLED's
`.gitignore` excludes this file — make sure it's version-controlled anyway).
`default_envs` is `desine_cntrl`, so a plain build/upload makes the encoder
firmware; pass `-e custom_esp32_diguno_ads1115` for the pot build.

**Client-facing names** (build flags in the `desine_cntrl` env): device name
`Desine.Cntrl` (`SERVERNAME`), setup hotspot `Desine.Cntrl-AP` (`WLED_AP_SSID`),
firmware file `WLED_16.0.1_Desine.Cntrl.bin` (`WLED_RELEASE_NAME`). Wi-Fi (OTA)
updates are rejected unless the release name matches the running firmware, so
keep it `Desine.Cntrl`. A board still running an older build needs a USB flash
or "Ignore firmware validation" ticked once on the update page.

**When to erase flash first:** only if a usermod's saved config values change
meaning (e.g. enum values renumbered). Otherwise new config keys just fall back
to compiled-in defaults and a normal upload is fine.

### Build pitfalls already solved (don't re-debug these)

- **Custom envs must `extends = env:esp32dev`**, not `extends = esp32`.
  Otherwise flash mode defaults to `dout`, and every file fails with
  `sdkconfig.h: No such file or directory` — looks like a broken toolchain, isn't.
- **`SPI.h: No such file`** from Adafruit BusIO: list `SPI` *and*
  `Adafruit BusIO` as direct dependencies in the usermod's `library.json`.
- **Usermod silently missing from firmware:** `REGISTER_USERMOD()` must be in a
  compiled `.cpp`. A header-only usermod is never compiled. Each usermod here
  has a one-line `.cpp` that just `#include`s its `.h`.
  `validate_modules.py` reports `No symbols from [...] found` when this happens.
- **I2C pin allocation:** must use
  `PinManager::allocateMultiplePins(pins, 2, PinOwner::HW_I2C, true)`.
  Single-pin `allocatePin()` rejects `HW_I2C` and the usermod just says
  "disabled". `PinManager` is a namespace, not an object.
- `getPaletteCount()` is a free function, not a `strip.` method.
- **`platformio.exe` "blocked by an Application Control policy"** (Windows 11):
  Windows Smart App Control blocks unsigned executables, including PlatformIO and
  the ESP32 compiler. Confirm in Event Viewer → Microsoft-Windows-CodeIntegrity/
  Operational. Fix: Windows Security → App & browser control → Smart App Control
  → Off (one-way; can't be re-enabled without a Windows reset). Alternative:
  build in GitHub Actions and flash via WLED's web update page.
- **Debugging silent failures:** add `-D WLED_DEBUG` to the env's build_flags,
  reflash, read the serial log — it prints exact pin-allocation failures.
- **Version mismatch crash-loop:** an old `cfg.json` from a different WLED
  version (e.g. stock firmware) can crash `deserializeConfig()` at boot. We
  standardized on WLED 16.0.1 partly for this reason; erase flash if
  switching from another version.

## 4. WLED settings-page gotchas (for usermod dropdowns)

Learned building the per-channel/per-encoder dropdown rows. Relevant to any
usermod settings UI on the Config → Usermods page (`wled00/data/settings_um.htm`):

- Nested objects in a usermod's config (one level deep) become titled sections
  with a divider line; field names become `Usermod:section:field`, and the save
  handler in `set.cpp` supports that. Rename headings in JS by walking back from
  a field to the `<p>` (`_encTitle`).
- `addDropdown()` converts one input per field *name*. For N independent
  dropdowns, use N separately named fields (`rot0`..`rot4`), not an array.
- Save dropdown values as strings in `addToConfig()` (`(String)value`) or the
  saved value won't show as selected.
- Non-array fields get a hidden type-marker `<input>` before them, so
  `addInfo(name, 0, ...)` targets the wrong element. To relabel, walk
  `previousSibling` to the auto-generated text node and overwrite it
  (`_encSetLabel` / `_adsSetLabel` helpers).
- Don't set `.disabled` on usermod settings fields to "grey them out": the form is a
  plain POST, disabled fields aren't submitted, and the missing values reset to
  defaults on save. Dim them with CSS opacity instead (`_encMarkMissing`).
- Every field ends with a hard `<br>`. To put several dropdowns on one row,
  remove the `<br>` after each one except the last (`_encJoinRow` / `_adsJoinRow`).

## 5. Shared design decisions

- **"Anim" favorites are WLED Presets.** Save looks as normal presets, then list
  up to 8 preset numbers on the usermod settings page. Next/Prev steps through them.
- **White channel:** the usermod keeps primary hue, secondary hue, and white level
  as separate values and recombines them into the segment's RGBW color on each
  change, so adjusting one never wipes the others.
- The two usermods intentionally duplicate this logic rather than share code —
  different clients/hardware, not worth the coupling.

## 6. Status & next steps

_Last updated 2026-10-11._

**Seesaw encoders**
- [x] Built and flashed; boot log shows device at 0x36 / encoder 0 ready (2026-08-15)
- [ ] Wire encoders 2–5 and confirm at 0x37–0x3A
- [ ] Check the settings-page dropdown rows in a browser
- [x] Added (2026-10-11, builds clean, **not yet tested on hardware**): encoder LEDs
  show the palette gradient across detected encoders, LED brightness, follow strip
  power (first encoder stays at 20%); hot-plug detection with greyed-out rows for
  undetected encoders; per-encoder Step size (default raised 4 → 8)
- [x] Settings page reorganized into titled sections (Encoder - 1..5, Encoder LEDs,
  Favorites, then "I2C Pins (advanced)" at the very bottom per client request); favorites raised to 13; I2C address is now a dropdown.
  Config keys changed (nested `enc1`..`enc5` / `leds` / `favorites` / `i2c` objects), so
  settings saved by older builds revert to defaults once. Page layout verified
  in a browser mockup built from WLED's own settings_um.htm functions.
- [x] Renamed to **Desine.Cntrl** (2026-10-11): usermod folder `usermods/Desine.Cntrl`,
  settings-page section header, env `desine_cntrl` (now the default env), device
  name, setup hotspot, firmware file name. Config key changed to `Desine.Cntrl`,
  so settings saved under `SeesawEncoderControl` revert to defaults once.
- [x] Flashed + tested on hardware (2026-10-11/13): renamed build boots, encoders at
  0x36/0x37 detected; user confirmed settings page, LEDs, and hot-plug working
- [x] Up to 8 encoders (full jumper range 0x36-0x3D); "Palette Mode" press action
  switches the palette knob/buttons between custom palettes and the two-color
  palettes (3 "Colors 1&2", 5 "Colors Only"). Fixed bug: palette cycling assumed
  palettes were numbered 0..N-1 and never reached custom palettes (IDs 200, 199, ...).
  Built 2026-10-13, **not yet flashed** (board was unplugged)
- [x] Control redesign (2026-10-13, per client direction; supersedes the "Palette
  Mode" press action above): encoder 1 fixed (rotate = Dimmer, short = switch
  Mode 1/2, long = On/Off); encoders 2-8 get Mode 1 + Mode 2 action sets, each
  with its own Step; three exclusive color schemes (custom palette / pri-sec hue /
  warm white), switched by turning that scheme's knob; "White" rotate option
  replaced by "Warm White" (warmth); mode switch flashes encoder LEDs (orange =
  Mode 1, teal = Mode 2). Config keys changed (`m1rot`... per encoder), so saved
  encoder settings revert to defaults once. Builds clean; settings page verified
  in the browser mockup. **Not yet flashed.**
- [ ] Flash and test on the device: mode switching + flash, all three color
  schemes, warm white tuning by eye (`WARM_AMBER_*` defines)
- [ ] **Ask the client/user once the encoder count is decided (3 or 5):** how the
  active mode should be indicated long-term (current placeholder: brief
  orange/teal flash of all encoder LEDs)

**ADS1115**
- [ ] One-row "Ch1 [Pot ▾] [Dimmer ▾]" layout fix not yet re-verified in a browser
- [ ] Not yet retested with ADS1115 boards wired on the Dig-Uno

**Project housekeeping**
- [x] In git: private repo https://github.com/LotusConsume/Desine.r-WLED, branch
  `desine` (2026-10-11). Remote `upstream` = wled/WLED for pulling WLED updates.
  New machine: `git clone -b desine https://github.com/LotusConsume/Desine.r-WLED.git`
- [ ] Move PlatformIO build output (`.pio/`) out of Google Drive (planned)
- [ ] Audioreactive / analog mic config pending mic wiring decision
- [ ] Remove `-D WLED_DEBUG` from the ADS1115 env once no longer needed
