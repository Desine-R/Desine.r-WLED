#pragma once

#include "wled.h"
#include <Adafruit_ADS1X15.h>

// ---------------------------------------------------------------------------
// ADS1115 Dual-Board Pot/Button Control usermod
//
// Reads up to 8 potentiometers OR buttons (any mix) across two ADS1115 ADCs
// (A0-A3 each) on one shared I2C bus, plus two dedicated GPIO buttons for
// raw effect cycling. Every channel gets two dropdowns on the WLED Usermod
// settings page (Config -> Usermods): first Off/Potentiometer/Button, then
// -- populated dynamically based on that choice -- which attribute it
// controls. No recompiling needed to change what a given pot/button does.
// GPIO pin assignments (SDA/SCL/buttons) are likewise runtime-configurable.
//
//   ADS1115 #1 (addr 0x48, ADDR pin -> GND)     -> channels 0-3 (A0-A3)
//   ADS1115 #2 (addr 0x49, ADDR pin -> 3.3V)    -> channels 4-7 (A0-A3)
//
// Potentiometer channels map the ADC's continuous 0-255 reading directly
// onto one of: Dimmer, Spd (effect speed), Int (effect intensity), Palette,
// Pri Hue, Sec Hue, White (SK6812 W channel), Anim (animation select).
//
// Button channels treat the same ADC channel as a threshold-triggered
// digital input (wired as a voltage divider/switch, same idea as WLED's
// native "analog button" support), with hysteresis so it only fires once
// per press, and detection at *both* extremes of the range since the wiring
// polarity of an arbitrary button isn't known. Button channels currently
// support four actions: Palette Next/Prev and Anim Next/Prev -- kept
// deliberately small for now; more can be added the same way later.
//
// "Anim" (pot: proportional select) / "Anim Next/Prev" (button) cycle
// through a short list of WLED Preset numbers (configured below the channel
// grid on the settings page) rather than raw effect IDs -- save your
// favorite looks as ordinary WLED Presets first, then list their preset
// numbers here. A pot maps its position proportionally across the
// configured list; buttons step forward/backward through it.
//
// "White" controls only the W component of the primary/secondary segment
// colors, independent of hue -- useful for RGBW strips where you want a
// dedicated white-level knob separate from color.
//
// Two dedicated GPIO buttons (not on the ADS1115) remain for raw next/prev
// effect cycling, unchanged from before.
//
// No usermods_list.cpp edit needed -- REGISTER_USERMOD() at the bottom
// self-registers this class when its folder name is added to
// custom_usermods in platformio_override.ini.
// ---------------------------------------------------------------------------

#define ADS1_ADDR     0x48
#define ADS2_ADDR     0x49

// defaults, matching the QuinLED Dig-Uno v3.1 r2 (+ QuinLED-ESP32-Ethernet)
// header -- overridden by whatever is saved in the Usermod settings page.
// SDA moved off GPIO12 (Q2) to GPIO32 (Q4) -- GPIO12 is an ESP32 flash-voltage
// strapping pin (MTDI/VDD_SDIO), sampled on every reset; the ADS1115's onboard
// I2C pull-up holds it HIGH, which makes the ROM bootloader select the wrong
// flash voltage (1.8V instead of 3.3V) and fail to read flash at all --
// reproduced 100% of the time with the ADS1115 connected. GPIO32 has no
// strapping function and is otherwise identical (documented external GPIO).
// Next button uses the native GPIO0 "Button" pin (pulled high, matches our
// active-LOW button logic); prev uses GPIO2 (Q3), also a documented external
// GPIO. See platformio_override.ini for the full pin plan and rationale.
#define DEFAULT_SDA_PIN   32
#define DEFAULT_SCL_PIN   15
#define DEFAULT_BTN_NEXT  0
#define DEFAULT_BTN_PREV  2

#define NOTIFY_INTERVAL_MS   150
#define DEADBAND             2

class ADS1115DualControlUsermod : public Usermod {
  public:
    void setup() override {
      allocateAndBegin();
      initDone = true;
    }

    void loop() override {
      if (!initDone || !pinsValid || strip.isUpdating()) return;

      handleButtons();

      if (!(ads1Ok || ads2Ok)) return;

      Adafruit_ADS1115* activeAds = channelIsOnAds1(activeChannel) ? &ads1 : &ads2;
      bool activeBoardOk = channelIsOnAds1(activeChannel) ? ads1Ok : ads2Ok;

      if (!activeBoardOk) {
        advanceChannel();
        startChannelRead();
        return;
      }

      if (!activeAds->conversionComplete()) return;

      int16_t raw = activeAds->getLastConversionResults();
      float volts = activeAds->computeVolts(raw);
      uint8_t val8 = voltsTo8Bit(volts);

      applyChannel(activeChannel, val8);

      advanceChannel();
      startChannelRead();

      maybeNotify();
    }

    // ---- Usermod settings page ----
    // "pin" array: index 0 = SDA, 1 = SCL, 2 = Next button, 3 = Prev button.
    // Using the literal key "pin" is what triggers WLED's built-in GPIO
    // dropdown + conflict checking on the Usermod settings page.
    // "ch0m".."ch7m": per-channel mode (Off/Pot/Button).
    // "ch0a".."ch7a": per-channel action -- meaning depends on that
    // channel's mode (see the Pot*/Btn* enums below). Each gets its own
    // named field, not an array, so appendConfigData() can convert each
    // into its own independent dropdown -- addDropdown() only converts one
    // field per name, so an 8-element array would only yield one dropdown.
    // "fav": up to MAX_FAVORITES WLED preset numbers (0 = unused slot).
    void addToConfig(JsonObject& root) override {
      JsonObject top = root.createNestedObject(FPSTR(_name));
      JsonArray pinArray = top.createNestedArray("pin");
      pinArray.add(sdaPin);
      pinArray.add(sclPin);
      pinArray.add(btnNextPin);
      pinArray.add(btnPrevPin);

      for (uint8_t i = 0; i < CHANNEL_COUNT; i++) {
        // cast to String: required for the settings-page dropdown to show
        // the saved value as selected (see usermods/Battery for the same
        // pattern).
        top[String("ch") + i + "m"] = (String)channelMode[i];
        top[String("ch") + i + "a"] = (String)channelAction[i];
      }

      JsonArray favArray = top.createNestedArray("fav");
      for (uint8_t i = 0; i < MAX_FAVORITES; i++) favArray.add(favoritePresets[i]);
    }

    bool readFromConfig(JsonObject& root) override {
      JsonObject top = root[FPSTR(_name)];
      bool configComplete = !top.isNull();

      int8_t newSda = sdaPin, newScl = sclPin, newNext = btnNextPin, newPrev = btnPrevPin;

      configComplete &= getJsonValue(top["pin"][0], newSda, DEFAULT_SDA_PIN);
      configComplete &= getJsonValue(top["pin"][1], newScl, DEFAULT_SCL_PIN);
      configComplete &= getJsonValue(top["pin"][2], newNext, DEFAULT_BTN_NEXT);
      configComplete &= getJsonValue(top["pin"][3], newPrev, DEFAULT_BTN_PREV);

      bool pinsChanged = (newSda != sdaPin) || (newScl != sclPin) ||
                          (newNext != btnNextPin) || (newPrev != btnPrevPin);

      sdaPin = newSda; sclPin = newScl; btnNextPin = newNext; btnPrevPin = newPrev;

      for (uint8_t i = 0; i < CHANNEL_COUNT; i++) {
        configComplete &= getJsonValue(top[String("ch") + i + "m"], channelMode[i], DEFAULT_CHANNEL_MODE[i]);
        configComplete &= getJsonValue(top[String("ch") + i + "a"], channelAction[i], DEFAULT_CHANNEL_ACTION[i]);
      }

      for (uint8_t i = 0; i < MAX_FAVORITES; i++) {
        configComplete &= getJsonValue(top["fav"][i], favoritePresets[i], (uint8_t)0);
      }

      // I2C pins can't be re-pointed live once Wire.begin() has run -- ask
      // WLED to reboot so new pins take effect cleanly.
      if (initDone && pinsChanged) {
        doReboot = true;
      }

      return configComplete;
    }

    // Builds the per-pin labels, the mode+action dropdown pair for each
    // ADS1115 channel (with the action list swapped dynamically based on
    // the mode choice), and labels for the favorites list.
    void appendConfigData(Print& s) override {
      printPinLabel(s, 0, "SDA (I2C)");
      printPinLabel(s, 1, "SCL (I2C)");
      printPinLabel(s, 2, "Next effect button");
      printPinLabel(s, 3, "Prev effect button");

      printSharedActionListsJs(s);

      for (uint8_t i = 0; i < CHANNEL_COUNT; i++) {
        printChannelDropdowns(s, i);
      }

      for (uint8_t i = 0; i < MAX_FAVORITES; i++) {
        s.print(F("addInfo('")); s.print(FPSTR(_name)); s.print(F(":fav[]',")); s.print(i);
        s.print(F(",'Anim ")); s.print(i + 1); s.print(F(" (preset #)');"));
      }
    }

  private:
    static const char _name[];

    Adafruit_ADS1115 ads1;
    Adafruit_ADS1115 ads2;
    bool ads1Ok = false;
    bool ads2Ok = false;
    bool initDone = false;
    bool pinsValid = false;

    int8_t sdaPin = DEFAULT_SDA_PIN;
    int8_t sclPin = DEFAULT_SCL_PIN;
    int8_t btnNextPin = DEFAULT_BTN_NEXT;
    int8_t btnPrevPin = DEFAULT_BTN_PREV;

    static const uint8_t CHANNEL_COUNT = 8;
    uint8_t activeChannel = 0;

    enum : uint8_t { MODE_OFF = 0, MODE_POT = 1, MODE_BTN = 2 };

    // Pot action IDs (0-7) -- meaning of channelAction[i] when
    // channelMode[i] == MODE_POT.
    enum : uint8_t {
      POT_BRI = 0, POT_SPEED, POT_INTENSITY, POT_PALETTE,
      POT_HUE1, POT_HUE2, POT_WHITE, POT_FAVORITE
    };

    // Button action IDs (0-3) -- meaning of channelAction[i] when
    // channelMode[i] == MODE_BTN.
    enum : uint8_t {
      BTN_PALETTE_NEXT = 0, BTN_PALETTE_PREV,
      BTN_FAVORITE_NEXT, BTN_FAVORITE_PREV
    };

    // Defaults reproduce the usermod's original fixed mapping (brightness,
    // speed, intensity, palette, primary hue, secondary hue as pots on the
    // first six channels) so an existing hardware setup keeps working
    // unchanged after this upgrade; the last two channels default to Off.
    static const uint8_t DEFAULT_CHANNEL_MODE[CHANNEL_COUNT];
    static const uint8_t DEFAULT_CHANNEL_ACTION[CHANNEL_COUNT];
    uint8_t channelMode[CHANNEL_COUNT]   = {MODE_POT, MODE_POT, MODE_POT, MODE_POT, MODE_POT, MODE_POT, MODE_OFF, MODE_OFF};
    uint8_t channelAction[CHANNEL_COUNT] = {POT_BRI, POT_SPEED, POT_INTENSITY, POT_PALETTE, POT_HUE1, POT_HUE2, 0, 0};

    // Potentiometer (continuous) smoothing state, per channel.
    uint8_t lastValue[CHANNEL_COUNT] = {0};
    bool haveValue[CHANNEL_COUNT] = {false};

    // Button (threshold/hysteresis) state, per channel. A "press" is
    // detected at *either* extreme of the range, not just the high end --
    // we don't know the wiring polarity of an arbitrary button/pot-as-button
    // (pull-up-normally-high-press-pulls-low, or the reverse), and a channel
    // already explicitly set to Button mode isn't also being read as a
    // continuous value, so treating either extreme as "pressed" is safe and
    // works regardless of which way it's wired.
    static const uint8_t BTN_ENGAGE_LOW   = 35;   // at/below this = pressed (low-wired)
    static const uint8_t BTN_ENGAGE_HIGH  = 220;  // at/above this = pressed (high-wired)
    static const uint8_t BTN_REARM_LOW    = 75;   // must return above this...
    static const uint8_t BTN_REARM_HIGH   = 180;  // ...and below this to re-arm
    bool latched[CHANNEL_COUNT] = {false};

    // Tracked color state -- combined into full RGBW on every hue/white
    // change so the two never stomp on each other (WLED segment colors are
    // single 32-bit RGBW values, not independently-settable channels).
    uint8_t primaryHue = 0;
    uint8_t secondaryHue = 0;
    uint8_t whiteLevel = 0;

    // Favorites: a short list of WLED Preset numbers (1-250; 0 = unused
    // slot), configured on the settings page below the channel grid.
    static const uint8_t MAX_FAVORITES = 8;
    uint8_t favoritePresets[MAX_FAVORITES] = {0};
    int8_t lastFavoriteIndex = -1;

    unsigned long lastNotify = 0;
    bool pendingStateNotify = false;
    bool pendingColorNotify = false;

    bool lastNextRaw = HIGH, lastPrevRaw = HIGH;
    bool nextDebounced = HIGH, prevDebounced = HIGH;
    unsigned long lastNextChange = 0, lastPrevChange = 0;
    static const unsigned long DEBOUNCE_MS = 40;

    const uint16_t MUX_BY_CHANNEL[4] = {
      ADS1X15_REG_CONFIG_MUX_SINGLE_0,
      ADS1X15_REG_CONFIG_MUX_SINGLE_1,
      ADS1X15_REG_CONFIG_MUX_SINGLE_2,
      ADS1X15_REG_CONFIG_MUX_SINGLE_3
    };

    void allocateAndBegin() {
      pinsValid = true;

      // HW I2C pins must be allocated together via allocateMultiplePins() -- the
      // single-pin allocatePin() rejects PinOwner::HW_I2C outright, since I2C is
      // a shared bus (SDA+SCL always come as a pair, and this call is what lets
      // other I2C consumers share the same two pins instead of conflicting).
      const int8_t i2cPins[2] = { sdaPin, sclPin };
      if (!PinManager::allocateMultiplePins(i2cPins, 2, PinOwner::HW_I2C, true)) pinsValid = false;
      if (!PinManager::allocatePin(btnNextPin, false, PinOwner::UM_Unspecified)) pinsValid = false;
      if (!PinManager::allocatePin(btnPrevPin, false, PinOwner::UM_Unspecified)) pinsValid = false;

      if (!pinsValid) {
        Serial.println(F("[ADS1115DualControl] one or more configured pins are already in use -- usermod disabled"));
        return;
      }

      Wire.begin(sdaPin, sclPin);

      // Full I2C bus scan at boot -- useful ongoing diagnostic for hardware
      // bring-up (confirms addresses actually present on the wire, not just
      // whether the specific 0x48/0x49 begin() calls below succeeded).
      Serial.println(F("[ADS1115DualControl] scanning I2C bus..."));
      uint8_t foundCount = 0;
      for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
          Serial.print(F("[ADS1115DualControl] found device at 0x"));
          Serial.println(addr, HEX);
          foundCount++;
        }
      }
      if (foundCount == 0) Serial.println(F("[ADS1115DualControl] scan complete -- no devices found on the bus"));
      else { Serial.print(F("[ADS1115DualControl] scan complete -- ")); Serial.print(foundCount); Serial.println(F(" device(s) found")); }

      ads1.setGain(GAIN_ONE);
      ads2.setGain(GAIN_ONE);

      ads1Ok = ads1.begin(ADS1_ADDR);
      ads2Ok = ads2.begin(ADS2_ADDR);

      if (!ads1Ok) Serial.println(F("[ADS1115DualControl] ADS1115 #1 (0x48) not found"));
      if (!ads2Ok) Serial.println(F("[ADS1115DualControl] ADS1115 #2 (0x49) not found"));

      pinMode(btnNextPin, INPUT_PULLUP);
      pinMode(btnPrevPin, INPUT_PULLUP);

      if (ads1Ok || ads2Ok) startChannelRead();
    }

    bool channelIsOnAds1(uint8_t ch) { return ch < 4; }

    void advanceChannel() {
      activeChannel++;
      if (activeChannel >= CHANNEL_COUNT) activeChannel = 0;
    }

    void startChannelRead() {
      Adafruit_ADS1115* ads = channelIsOnAds1(activeChannel) ? &ads1 : &ads2;
      bool ok = channelIsOnAds1(activeChannel) ? ads1Ok : ads2Ok;
      if (!ok) return;

      uint8_t localCh = channelIsOnAds1(activeChannel) ? activeChannel : (activeChannel - 4);
      ads->startADCReading(MUX_BY_CHANNEL[localCh], /*continuous=*/false);
    }

    static uint8_t voltsTo8Bit(float volts) {
      float scaled = (volts / 3.3f) * 255.0f;
      if (scaled < 0) scaled = 0;
      if (scaled > 255) scaled = 255;
      return (uint8_t)scaled;
    }

    void applyChannel(uint8_t idx, uint8_t val8) {
      uint8_t mode = channelMode[idx];
      if (mode == MODE_OFF) return;

      if (mode == MODE_BTN) {
        // Threshold/hysteresis edge trigger -- fires the step action exactly
        // once per "press" (crossing into either extreme band), regardless
        // of how long it sits there, and re-arms once it returns to the
        // middle band. Checking both extremes means it works no matter which
        // way an actual button is wired (see BTN_ENGAGE_* comment above).
        bool atExtreme = (val8 <= BTN_ENGAGE_LOW) || (val8 >= BTN_ENGAGE_HIGH);
        bool inRearmZone = (val8 > BTN_REARM_LOW) && (val8 < BTN_REARM_HIGH);
        if (atExtreme) {
          if (!latched[idx]) {
            latched[idx] = true;
            applyButtonFn(channelAction[idx]);
          }
        } else if (inRearmZone) {
          latched[idx] = false;
        }
        return;
      }

      // MODE_POT
      if (haveValue[idx] && abs((int)val8 - (int)lastValue[idx]) < DEADBAND) return;
      lastValue[idx] = val8;
      haveValue[idx] = true;
      applyPotFn(channelAction[idx], val8);
    }

    void applyPotFn(uint8_t action, uint8_t val8) {
      Segment& seg = strip.getSegment(strip.getFirstSelectedSegId());
      switch (action) {
        case POT_BRI:
          bri = val8;
          pendingStateNotify = true;
          break;
        case POT_SPEED:
          seg.speed = val8;
          pendingStateNotify = true;
          break;
        case POT_INTENSITY:
          seg.intensity = val8;
          pendingStateNotify = true;
          break;
        case POT_PALETTE: {
          uint8_t n = getPaletteCount();
          if (n > 0) {
            seg.setPalette(map(val8, 0, 255, 0, n - 1));
            pendingStateNotify = true;
          }
          break;
        }
        case POT_HUE1:
          primaryHue = val8;
          applyRGBWSlot(0);
          pendingColorNotify = true;
          break;
        case POT_HUE2:
          secondaryHue = val8;
          applyRGBWSlot(1);
          pendingColorNotify = true;
          break;
        case POT_WHITE:
          whiteLevel = val8;
          applyRGBWSlot(0);
          applyRGBWSlot(1);
          pendingColorNotify = true;
          break;
        case POT_FAVORITE:
          selectFavoriteByPosition(val8);
          break;
      }
    }

    void applyButtonFn(uint8_t action) {
      Segment& seg = strip.getSegment(strip.getFirstSelectedSegId());
      switch (action) {
        case BTN_PALETTE_NEXT: {
          uint8_t n = getPaletteCount();
          if (n > 0) { seg.setPalette((seg.palette + 1) % n); pendingStateNotify = true; }
          break;
        }
        case BTN_PALETTE_PREV: {
          uint8_t n = getPaletteCount();
          if (n > 0) { seg.setPalette((seg.palette + n - 1) % n); pendingStateNotify = true; }
          break;
        }
        case BTN_FAVORITE_NEXT: advanceFavorite(1); break;
        case BTN_FAVORITE_PREV: advanceFavorite(-1); break;
      }
    }

    // Recomputes segment color slot 0 (primary) or 1 (secondary) from the
    // tracked hue + white level, so setting one never wipes out the other.
    void applyRGBWSlot(uint8_t slot) {
      uint8_t hue = (slot == 0) ? primaryHue : secondaryHue;
      CRGB c = CHSV(hue, 255, 255);
      Segment& seg = strip.getSegment(strip.getFirstSelectedSegId());
      seg.setColor(slot, RGBW32(c.r, c.g, c.b, whiteLevel));
    }

    uint8_t favoriteCount() {
      uint8_t n = 0;
      for (uint8_t i = 0; i < MAX_FAVORITES; i++) if (favoritePresets[i] > 0) n++;
      return n;
    }

    // idx here is a position within the *compacted* list of non-zero
    // favorite slots (0 = first configured favorite), not a raw array index.
    void applyFavoriteSlot(uint8_t idx) {
      uint8_t seen = 0;
      for (uint8_t i = 0; i < MAX_FAVORITES; i++) {
        if (favoritePresets[i] == 0) continue;
        if (seen == idx) {
          applyPreset(favoritePresets[i]);
          return;
        }
        seen++;
      }
    }

    void selectFavoriteByPosition(uint8_t val8) {
      uint8_t n = favoriteCount();
      if (n == 0) return;
      uint8_t idx = map(val8, 0, 255, 0, n - 1);
      if ((int8_t)idx == lastFavoriteIndex) return;
      lastFavoriteIndex = (int8_t)idx;
      applyFavoriteSlot(idx);
    }

    void advanceFavorite(int8_t dir) {
      uint8_t n = favoriteCount();
      if (n == 0) return;
      int idx = (lastFavoriteIndex < 0) ? 0 : (int)lastFavoriteIndex;
      idx = (idx + dir + (int)n) % (int)n;
      lastFavoriteIndex = (int8_t)idx;
      applyFavoriteSlot((uint8_t)idx);
    }

    void maybeNotify() {
      if (!pendingStateNotify && !pendingColorNotify) return;
      if (millis() - lastNotify < NOTIFY_INTERVAL_MS) return;

      if (pendingColorNotify) colorUpdated(CALL_MODE_DIRECT_CHANGE);
      else stateUpdated(CALL_MODE_DIRECT_CHANGE);

      pendingStateNotify = false;
      pendingColorNotify = false;
      lastNotify = millis();
    }

    void handleButtons() {
      handleOneButton(btnNextPin, lastNextRaw, nextDebounced, lastNextChange, true);
      handleOneButton(btnPrevPin, lastPrevRaw, prevDebounced, lastPrevChange, false);
    }

    // Edge-triggered debounce: fires cycleEffect() exactly once per press,
    // on the transition to a stable LOW, regardless of loop() timing.
    void handleOneButton(int8_t pin, bool &lastRaw, bool &debounced, unsigned long &lastChange, bool goNext) {
      bool raw = digitalRead(pin);
      if (raw != lastRaw) {
        lastChange = millis();
        lastRaw = raw;
      }
      if ((millis() - lastChange) > DEBOUNCE_MS && raw != debounced) {
        debounced = raw;
        if (debounced == LOW) cycleEffect(goNext);
      }
    }

    void cycleEffect(bool goNext) {
      Segment& seg = strip.getSegment(strip.getFirstSelectedSegId());
      uint8_t modeCount = strip.getModeCount();
      if (modeCount == 0) return;

      int newMode = (int)seg.mode + (goNext ? 1 : -1);
      if (newMode < 0) newMode = modeCount - 1;
      if (newMode >= (int)modeCount) newMode = 0;

      seg.setMode((uint8_t)newMode);
      stateUpdated(CALL_MODE_DIRECT_CHANGE);
    }

    void printPinLabel(Print& s, uint8_t idx, const char* label) {
      s.print(F("addInfo('")); s.print(FPSTR(_name)); s.print(F(":pin[]',")); s.print(idx);
      s.print(F(",'")); s.print(label); s.print(F("');"));
    }

    // Declares the pot/button action option lists and the cascading-dropdown
    // helper once, shared by all 8 channels below, rather than re-emitting
    // the same option list 8 times.
    void printSharedActionListsJs(Print& s) {
      s.print(F(
        "var _adsPotOpts=[['Dimmer',0],['Spd',1],['Int',2],['Palette',3],"
        "['Pri Hue',4],['Sec Hue',5],['White',6],['Anim',7]];"
        "var _adsBtnOpts=[['Palette Next',0],['Palette Prev',1],"
        "['Anim Next',2],['Anim Prev',3]];"
        "function _adsFillAction(as,ms,initial){"
          "var savedVal=as.dataset.val;"
          "while(as.options.length)as.remove(0);"
          "var opts=(ms.value=='2')?_adsBtnOpts:_adsPotOpts;"
          "for(var i=0;i<opts.length;i++){"
            "var opt=document.createElement('option');"
            "opt.value=opts[i][1];opt.text=opts[i][0];as.appendChild(opt);"
            "if(initial&&(''+opts[i][1])==savedVal)as.selectedIndex=as.options.length-1;"
          "}"
        "}"
        // WLED auto-generates a raw-field-name text label before every
        // non-array field (e.g. "Ch0m"), plus a hidden type-marker <input>
        // between that label and the actual control -- rewrite the label
        // text node in place (walking back past the hidden input) instead
        // of appending a second label alongside the auto-generated one.
        "function _adsSetLabel(sel,text){"
          "var n=sel.previousSibling;"
          "while(n){if(n.nodeType===3){n.nodeValue=text;return;}n=n.previousSibling;}"
        "}"
        // WLED also ends every field's row with a literal <br> -- remove the
        // one right after `sel` so the next field (the action dropdown)
        // continues on the same visual row instead of starting a new one.
        "function _adsJoinRow(sel){"
          "var n=sel.nextSibling;"
          "while(n){if(n.nodeName==='BR'){n.parentNode.removeChild(n);return;}n=n.nextSibling;}"
        "}"
      ));
    }

    // Mode dropdown (Off/Pot/Button) + action dropdown for one channel, on
    // one row reading "Ch1 [Pot v] [Dimmer v]" -- the action dropdown's
    // option list is (re)populated by _adsFillAction(), called once
    // immediately (restoring the saved selection) and again on every mode
    // change (resetting to the first option, since the two option lists
    // mean entirely different things).
    void printChannelDropdowns(Print& s, uint8_t idx) {
      s.print(F("(function(){"));
      s.print(F("var ms=addDropdown('")); s.print(FPSTR(_name));
      s.print(F("','ch")); s.print(idx); s.print(F("m');"));
      s.print(F("_adsSetLabel(ms,'Ch")); s.print(idx + 1); s.print(F(" ');"));
      s.print(F("_adsJoinRow(ms);"));
      s.print(F("addOption(ms,'Off',0);addOption(ms,'Pot',1);addOption(ms,'Button',2);"));
      s.print(F("var as=addDropdown('")); s.print(FPSTR(_name));
      s.print(F("','ch")); s.print(idx); s.print(F("a');"));
      s.print(F("_adsSetLabel(as,'');"));
      s.print(F("_adsFillAction(as,ms,true);"));
      s.print(F("ms.addEventListener('change',function(){_adsFillAction(as,ms,false);});"));
      s.print(F("})();"));
    }
};

const char ADS1115DualControlUsermod::_name[] PROGMEM = "ADS1115DualControl";
const uint8_t ADS1115DualControlUsermod::DEFAULT_CHANNEL_MODE[ADS1115DualControlUsermod::CHANNEL_COUNT] =
  {ADS1115DualControlUsermod::MODE_POT, ADS1115DualControlUsermod::MODE_POT, ADS1115DualControlUsermod::MODE_POT,
   ADS1115DualControlUsermod::MODE_POT, ADS1115DualControlUsermod::MODE_POT, ADS1115DualControlUsermod::MODE_POT,
   ADS1115DualControlUsermod::MODE_OFF, ADS1115DualControlUsermod::MODE_OFF};
const uint8_t ADS1115DualControlUsermod::DEFAULT_CHANNEL_ACTION[ADS1115DualControlUsermod::CHANNEL_COUNT] =
  {ADS1115DualControlUsermod::POT_BRI, ADS1115DualControlUsermod::POT_SPEED, ADS1115DualControlUsermod::POT_INTENSITY,
   ADS1115DualControlUsermod::POT_PALETTE, ADS1115DualControlUsermod::POT_HUE1, ADS1115DualControlUsermod::POT_HUE2, 0, 0};

static ADS1115DualControlUsermod ads1115_dual_control;
REGISTER_USERMOD(ads1115_dual_control);
