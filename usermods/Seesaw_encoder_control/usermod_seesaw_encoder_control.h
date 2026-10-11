#pragma once

#include "wled.h"
#include <Adafruit_seesaw.h>

// ---------------------------------------------------------------------------
// Seesaw Rotary Encoder Control usermod
//
// Reads up to 5 Adafruit I2C QT Rotary Encoder breakouts (Seesaw-based,
// STEMMA QT chainable, solder-jumper addressed 0x36-0x3A) on one shared I2C
// bus. Each encoder has only its own integrated pushbutton -- no separate
// physical buttons, unlike the ADS1115 pot usermod this replicates.
//
// Every encoder gets three independent dropdowns on the WLED Usermod
// settings page (Config -> Usermods), all on one row -- "Enc1 [Rotate v]
// [Short v] [Long v]":
//
//   Rotate:      what continuous attribute rotation adjusts (relative to
//                its live value -- there's no absolute position to map,
//                unlike a potentiometer), or Off to disable rotation.
//   Short press: a discrete action fired on release, if released before
//                LONG_PRESS_MS -- or None.
//   Long press:  a discrete action fired once, while still held, once
//                LONG_PRESS_MS is reached -- or None.
//
// Short/long press share the same action list: None, Anim Next/Prev,
// Palette Next/Prev, or toggle power On/Off.
//
// "Anim Next/Prev" steps through a short list of WLED Preset numbers
// (configured below the encoder grid) -- same favorites-via-Presets idea as
// the ADS1115 usermod. "White" controls only the W component of the
// primary/secondary segment colors, independent of hue.
//
// I2C pins (SDA/SCL) are runtime-configurable on the settings page, same as
// the ADS1115 usermod, and default to the same GPIO32/GPIO15 pair already
// proven on this board (GPIO12 is avoided -- flash-voltage strapping pin,
// see the ADS1115 usermod header for the full story).
//
// No usermods_list.cpp edit needed -- REGISTER_USERMOD() at the bottom
// self-registers this class when its folder name is added to
// custom_usermods in platformio_override.ini.
// ---------------------------------------------------------------------------

// Seesaw GPIO pin the built-in pushbutton is wired to on this board (per
// Adafruit's own example code for the I2C QT Rotary Encoder), and the
// lowest of the 5 solder-jumper-selectable addresses (0x36 + 0/1/2/3/4).
#define SS_SWITCH       24
#define BASE_ENCODER_ADDR 0x36

// defaults -- see ADS1115_dual_control's header comment for why GPIO32/15
// were chosen (GPIO12 is an ESP32 flash-voltage strapping pin that the
// ADS1115's I2C pull-up broke; same bus, same fix applies here).
#define DEFAULT_SDA_PIN   32
#define DEFAULT_SCL_PIN   15

#define NOTIFY_INTERVAL_MS   150
#define STEP_8BIT            4     // per-detent step for 0-255 attributes
#define LONG_PRESS_MS         800
#define DEBOUNCE_MS            40

class SeesawEncoderControlUsermod : public Usermod {
  public:
    void setup() override {
      allocateAndBegin();
      initDone = true;
    }

    void loop() override {
      if (!initDone || !pinsValid || strip.isUpdating()) return;

      for (uint8_t i = 0; i < ENCODER_COUNT; i++) {
        if (!encoders[i].ok) continue;
        handleEncoderRotation(i);
        handleEncoderButton(i);
      }

      maybeNotify();
    }

    // ---- Usermod settings page ----
    // "pin" array: index 0 = SDA, 1 = SCL.
    // "addr[]": each encoder's I2C address (plain number inputs, so a
    // single array is fine here -- no dropdown-conversion needed).
    // "rot0".."rot4" / "sp0".."sp4" / "lp0".."lp4": each encoder's rotate /
    // short-press / long-press action -- individually named fields (not
    // arrays), since addDropdown() only converts one input per field
    // *name* -- an array would only ever yield one dropdown (same gotcha
    // documented in the ADS1115 usermod's per-channel mode/action fields).
    // "fav": up to MAX_FAVORITES WLED preset numbers (0 = unused slot).
    void addToConfig(JsonObject& root) override {
      JsonObject top = root.createNestedObject(FPSTR(_name));
      JsonArray pinArray = top.createNestedArray("pin");
      pinArray.add(sdaPin);
      pinArray.add(sclPin);

      JsonArray addrArray = top.createNestedArray("addr");
      for (uint8_t i = 0; i < ENCODER_COUNT; i++) addrArray.add(encoderAddr[i]);

      for (uint8_t i = 0; i < ENCODER_COUNT; i++) {
        // cast to String: required for the settings-page dropdown to show
        // the saved value as selected (same pattern as ADS1115_dual_control).
        top[String("rot") + i] = (String)rotateAction[i];
        top[String("sp") + i] = (String)shortPressAction[i];
        top[String("lp") + i] = (String)longPressAction[i];
      }

      JsonArray favArray = top.createNestedArray("fav");
      for (uint8_t i = 0; i < MAX_FAVORITES; i++) favArray.add(favoritePresets[i]);
    }

    bool readFromConfig(JsonObject& root) override {
      JsonObject top = root[FPSTR(_name)];
      bool configComplete = !top.isNull();

      int8_t newSda = sdaPin, newScl = sclPin;
      configComplete &= getJsonValue(top["pin"][0], newSda, DEFAULT_SDA_PIN);
      configComplete &= getJsonValue(top["pin"][1], newScl, DEFAULT_SCL_PIN);
      bool pinsChanged = (newSda != sdaPin) || (newScl != sclPin);
      sdaPin = newSda; sclPin = newScl;

      for (uint8_t i = 0; i < ENCODER_COUNT; i++) {
        configComplete &= getJsonValue(top["addr"][i], encoderAddr[i], DEFAULT_ENCODER_ADDR[i]);
        configComplete &= getJsonValue(top[String("rot") + i], rotateAction[i], DEFAULT_ROTATE_ACTION[i]);
        configComplete &= getJsonValue(top[String("sp") + i], shortPressAction[i], (uint8_t)PRESS_NONE);
        configComplete &= getJsonValue(top[String("lp") + i], longPressAction[i], (uint8_t)PRESS_ANIM_NEXT);
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

    // Builds the per-pin labels, the address labels for each encoder, and
    // the rotate/short-press/long-press dropdown trio for each encoder.
    void appendConfigData(Print& s) override {
      printPinLabel(s, 0, "SDA (I2C)");
      printPinLabel(s, 1, "SCL (I2C)");

      for (uint8_t i = 0; i < ENCODER_COUNT; i++) {
        s.print(F("addInfo('")); s.print(FPSTR(_name)); s.print(F(":addr[]',")); s.print(i);
        s.print(F(",'Encoder ")); s.print(i + 1); s.print(F(" I2C addr (0x36-0x3A)');"));
      }

      printSharedOptionListsJs(s);
      for (uint8_t i = 0; i < ENCODER_COUNT; i++) printEncoderDropdowns(s, i);

      for (uint8_t i = 0; i < MAX_FAVORITES; i++) {
        s.print(F("addInfo('")); s.print(FPSTR(_name)); s.print(F(":fav[]',")); s.print(i);
        s.print(F(",'Anim ")); s.print(i + 1); s.print(F(" (preset #)');"));
      }
    }

  private:
    static const char _name[];

    bool initDone = false;
    bool pinsValid = false;

    int8_t sdaPin = DEFAULT_SDA_PIN;
    int8_t sclPin = DEFAULT_SCL_PIN;

    static const uint8_t ENCODER_COUNT = 5;
    static const uint8_t DEFAULT_ENCODER_ADDR[ENCODER_COUNT];
    uint8_t encoderAddr[ENCODER_COUNT] = {
      BASE_ENCODER_ADDR, BASE_ENCODER_ADDR + 1, BASE_ENCODER_ADDR + 2,
      BASE_ENCODER_ADDR + 3, BASE_ENCODER_ADDR + 4
    };

    // Rotate action IDs -- what an encoder's rotation continuously adjusts.
    enum : uint8_t {
      ROT_OFF = 0, ROT_BRI, ROT_SPEED, ROT_INTENSITY, ROT_PALETTE,
      ROT_HUE1, ROT_HUE2, ROT_WHITE
    };

    // Press action IDs -- shared by both short-press and long-press
    // dropdowns (meaning of shortPressAction[i] / longPressAction[i]).
    enum : uint8_t {
      PRESS_NONE = 0, PRESS_ANIM_NEXT, PRESS_ANIM_PREV,
      PRESS_PALETTE_NEXT, PRESS_PALETTE_PREV, PRESS_TOGGLE_POWER
    };

    // Defaults spread rotate across the first 5 attributes so out-of-the-
    // box each encoder does something different; short press defaults to
    // doing nothing until explicitly configured, long press defaults to
    // advancing through favorites.
    static const uint8_t DEFAULT_ROTATE_ACTION[ENCODER_COUNT];
    uint8_t rotateAction[ENCODER_COUNT] = { ROT_BRI, ROT_SPEED, ROT_INTENSITY, ROT_HUE1, ROT_HUE2 };
    uint8_t shortPressAction[ENCODER_COUNT] = { PRESS_NONE, PRESS_NONE, PRESS_NONE, PRESS_NONE, PRESS_NONE };
    uint8_t longPressAction[ENCODER_COUNT] = {
      PRESS_ANIM_NEXT, PRESS_ANIM_NEXT, PRESS_ANIM_NEXT, PRESS_ANIM_NEXT, PRESS_ANIM_NEXT
    };

    struct EncoderUnit {
      Adafruit_seesaw ss;
      bool ok = false;
      int32_t lastPosition = 0;
      bool haveLastPosition = false;
      bool lastButtonRaw = HIGH;
      bool buttonDebounced = HIGH;
      unsigned long lastButtonChange = 0;
      unsigned long pressStart = 0;
      bool longFired = false;
    };
    EncoderUnit encoders[ENCODER_COUNT];

    // Tracked color state -- combined into full RGBW on every hue/white
    // change so the two never stomp on each other (WLED segment colors are
    // single 32-bit RGBW values, not independently-settable channels).
    uint8_t primaryHue = 0;
    uint8_t secondaryHue = 0;
    uint8_t whiteLevel = 0;

    // Favorites: a short list of WLED Preset numbers (1-250; 0 = unused
    // slot), configured on the settings page below the encoder grid.
    static const uint8_t MAX_FAVORITES = 8;
    uint8_t favoritePresets[MAX_FAVORITES] = {0};
    int8_t lastFavoriteIndex = -1;

    unsigned long lastNotify = 0;
    bool pendingStateNotify = false;
    bool pendingColorNotify = false;

    void allocateAndBegin() {
      pinsValid = true;

      // HW I2C pins must be allocated together via allocateMultiplePins() --
      // the single-pin allocatePin() rejects PinOwner::HW_I2C outright,
      // since I2C is a shared bus.
      const int8_t i2cPins[2] = { sdaPin, sclPin };
      if (!PinManager::allocateMultiplePins(i2cPins, 2, PinOwner::HW_I2C, true)) pinsValid = false;

      if (!pinsValid) {
        Serial.println(F("[SeesawEncoderControl] configured I2C pins are already in use -- usermod disabled"));
        return;
      }

      Wire.begin(sdaPin, sclPin);

      // Full I2C bus scan at boot -- useful ongoing diagnostic for hardware
      // bring-up (confirms addresses actually present on the wire).
      Serial.println(F("[SeesawEncoderControl] scanning I2C bus..."));
      uint8_t foundCount = 0;
      for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
          Serial.print(F("[SeesawEncoderControl] found device at 0x"));
          Serial.println(addr, HEX);
          foundCount++;
        }
      }
      if (foundCount == 0) Serial.println(F("[SeesawEncoderControl] scan complete -- no devices found on the bus"));
      else { Serial.print(F("[SeesawEncoderControl] scan complete -- ")); Serial.print(foundCount); Serial.println(F(" device(s) found")); }

      for (uint8_t i = 0; i < ENCODER_COUNT; i++) {
        encoders[i].ok = encoders[i].ss.begin(encoderAddr[i]);
        if (encoders[i].ok) {
          encoders[i].ss.pinMode(SS_SWITCH, INPUT_PULLUP);
          encoders[i].lastPosition = encoders[i].ss.getEncoderPosition();
          encoders[i].haveLastPosition = true;
          Serial.print(F("[SeesawEncoderControl] encoder ")); Serial.print(i); Serial.println(F(" ready"));
        } else {
          Serial.print(F("[SeesawEncoderControl] encoder ")); Serial.print(i); Serial.println(F(" not found"));
        }
      }
    }

    void handleEncoderRotation(uint8_t i) {
      EncoderUnit &e = encoders[i];
      int32_t pos = e.ss.getEncoderPosition();
      if (!e.haveLastPosition) { e.lastPosition = pos; e.haveLastPosition = true; return; }
      int32_t delta = pos - e.lastPosition;
      if (delta == 0) return;
      e.lastPosition = pos;
      applyRotateDelta(rotateAction[i], delta);
    }

    // Edge-triggered debounce, same pattern as the ADS1115 usermod's
    // physical buttons, plus short-vs-long-press timing on top: a short
    // press (release before LONG_PRESS_MS) fires shortPressAction[i] on
    // release; a long press (held past LONG_PRESS_MS) fires
    // longPressAction[i] once, while still held, and is not also treated
    // as a short press on release.
    void handleEncoderButton(uint8_t i) {
      EncoderUnit &e = encoders[i];
      bool raw = e.ss.digitalRead(SS_SWITCH);
      if (raw != e.lastButtonRaw) {
        e.lastButtonChange = millis();
        e.lastButtonRaw = raw;
      }
      if ((millis() - e.lastButtonChange) > DEBOUNCE_MS && raw != e.buttonDebounced) {
        e.buttonDebounced = raw;
        if (e.buttonDebounced == LOW) {
          e.pressStart = millis();
          e.longFired = false;
        } else if (!e.longFired) {
          applyPressFn(shortPressAction[i]);
        }
      }
      if (e.buttonDebounced == LOW && !e.longFired && (millis() - e.pressStart) >= LONG_PRESS_MS) {
        e.longFired = true;
        applyPressFn(longPressAction[i]);
      }
    }

    // Nudges the live value of whichever attribute is assigned to
    // rotation, by delta detents -- unlike the ADS1115 pot usermod, there's
    // no absolute position to map, so every attribute is read-modify-
    // written from its current value. Hue wraps (it's a color wheel);
    // everything else clamps.
    void applyRotateDelta(uint8_t action, int32_t delta) {
      if (delta == 0 || action == ROT_OFF) return;
      Segment& seg = strip.getSegment(strip.getFirstSelectedSegId());
      switch (action) {
        case ROT_BRI: {
          int v = (int)bri + delta * STEP_8BIT;
          bri = (uint8_t)constrain(v, 0, 255);
          pendingStateNotify = true;
          break;
        }
        case ROT_SPEED: {
          int v = (int)seg.speed + delta * STEP_8BIT;
          seg.speed = (uint8_t)constrain(v, 0, 255);
          pendingStateNotify = true;
          break;
        }
        case ROT_INTENSITY: {
          int v = (int)seg.intensity + delta * STEP_8BIT;
          seg.intensity = (uint8_t)constrain(v, 0, 255);
          pendingStateNotify = true;
          break;
        }
        case ROT_PALETTE: {
          uint8_t n = getPaletteCount();
          if (n > 0) {
            int v = ((int)seg.palette + (int)delta) % (int)n;
            if (v < 0) v += n;
            seg.setPalette((uint8_t)v);
            pendingStateNotify = true;
          }
          break;
        }
        case ROT_HUE1: {
          int v = (((int)primaryHue + (int)delta * STEP_8BIT) % 256 + 256) % 256;
          primaryHue = (uint8_t)v;
          applyRGBWSlot(0);
          pendingColorNotify = true;
          break;
        }
        case ROT_HUE2: {
          int v = (((int)secondaryHue + (int)delta * STEP_8BIT) % 256 + 256) % 256;
          secondaryHue = (uint8_t)v;
          applyRGBWSlot(1);
          pendingColorNotify = true;
          break;
        }
        case ROT_WHITE: {
          int v = (int)whiteLevel + delta * STEP_8BIT;
          whiteLevel = (uint8_t)constrain(v, 0, 255);
          applyRGBWSlot(0);
          applyRGBWSlot(1);
          pendingColorNotify = true;
          break;
        }
      }
    }

    void applyPressFn(uint8_t action) {
      Segment& seg = strip.getSegment(strip.getFirstSelectedSegId());
      switch (action) {
        case PRESS_NONE:
          break;
        case PRESS_ANIM_NEXT: advanceFavorite(1); break;
        case PRESS_ANIM_PREV: advanceFavorite(-1); break;
        case PRESS_PALETTE_NEXT: {
          uint8_t n = getPaletteCount();
          if (n > 0) { seg.setPalette((seg.palette + 1) % n); pendingStateNotify = true; }
          break;
        }
        case PRESS_PALETTE_PREV: {
          uint8_t n = getPaletteCount();
          if (n > 0) { seg.setPalette((seg.palette + n - 1) % n); pendingStateNotify = true; }
          break;
        }
        case PRESS_TOGGLE_POWER:
          toggleOnOff();
          pendingStateNotify = true;
          break;
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

    void printPinLabel(Print& s, uint8_t idx, const char* label) {
      s.print(F("addInfo('")); s.print(FPSTR(_name)); s.print(F(":pin[]',")); s.print(idx);
      s.print(F(",'")); s.print(label); s.print(F("');"));
    }

    // Declares the rotate/press option lists and the label-rewrite/row-join
    // helpers once, shared by all 5 encoders below. WLED auto-generates a
    // raw-field-name text label before every non-array field (e.g. "rot0")
    // -- rewrite that text node in place instead of appending a second
    // label alongside it -- and ends every field's row with a literal
    // <br> -- remove the ones between the three dropdowns so they render
    // on one row (both fixes ported from ADS1115_dual_control's
    // _adsSetLabel/_adsJoinRow).
    void printSharedOptionListsJs(Print& s) {
      s.print(F(
        "var _encRotOpts=[['Off',0],['Dimmer',1],['Spd',2],['Int',3],"
        "['Palette',4],['Pri Hue',5],['Sec Hue',6],['White',7]];"
        "var _encPressOpts=[['None',0],['Anim Next',1],['Anim Prev',2],"
        "['Palette Next',3],['Palette Prev',4],['On/Off',5]];"
        "function _encSetLabel(sel,text){"
          "var n=sel.previousSibling;"
          "while(n){if(n.nodeType===3){n.nodeValue=text;return;}n=n.previousSibling;}"
        "}"
        "function _encJoinRow(sel){"
          "var n=sel.nextSibling;"
          "while(n){if(n.nodeName==='BR'){n.parentNode.removeChild(n);return;}n=n.nextSibling;}"
        "}"
        "function _encFillOpts(sel,opts){"
          "for(var i=0;i<opts.length;i++)addOption(sel,opts[i][0],opts[i][1]);"
        "}"
      ));
    }

    // Rotate + short-press + long-press dropdown trio for one encoder, on
    // one row reading "Enc1 [Rotate v] [Short v] [Long v]".
    void printEncoderDropdowns(Print& s, uint8_t idx) {
      s.print(F("(function(){"));
      s.print(F("var r=addDropdown('")); s.print(FPSTR(_name));
      s.print(F("','rot")); s.print(idx); s.print(F("');"));
      s.print(F("_encSetLabel(r,'Enc")); s.print(idx + 1); s.print(F(" ');"));
      s.print(F("_encJoinRow(r);"));
      s.print(F("_encFillOpts(r,_encRotOpts);"));

      s.print(F("var sp=addDropdown('")); s.print(FPSTR(_name));
      s.print(F("','sp")); s.print(idx); s.print(F("');"));
      s.print(F("_encSetLabel(sp,'');"));
      s.print(F("_encJoinRow(sp);"));
      s.print(F("_encFillOpts(sp,_encPressOpts);"));

      s.print(F("var lp=addDropdown('")); s.print(FPSTR(_name));
      s.print(F("','lp")); s.print(idx); s.print(F("');"));
      s.print(F("_encSetLabel(lp,'');"));
      s.print(F("_encFillOpts(lp,_encPressOpts);"));
      s.print(F("})();"));
    }
};

const char SeesawEncoderControlUsermod::_name[] PROGMEM = "SeesawEncoderControl";
const uint8_t SeesawEncoderControlUsermod::DEFAULT_ENCODER_ADDR[SeesawEncoderControlUsermod::ENCODER_COUNT] = {
  BASE_ENCODER_ADDR, BASE_ENCODER_ADDR + 1, BASE_ENCODER_ADDR + 2,
  BASE_ENCODER_ADDR + 3, BASE_ENCODER_ADDR + 4
};
const uint8_t SeesawEncoderControlUsermod::DEFAULT_ROTATE_ACTION[SeesawEncoderControlUsermod::ENCODER_COUNT] = {
  SeesawEncoderControlUsermod::ROT_BRI, SeesawEncoderControlUsermod::ROT_SPEED,
  SeesawEncoderControlUsermod::ROT_INTENSITY, SeesawEncoderControlUsermod::ROT_HUE1,
  SeesawEncoderControlUsermod::ROT_HUE2
};

static SeesawEncoderControlUsermod seesaw_encoder_control;
REGISTER_USERMOD(seesaw_encoder_control);
