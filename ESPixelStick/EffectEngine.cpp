#include <Arduino.h>
#include <ArduinoJson.h>
#include "ESPixelStick.h"
#include "EffectEngine.h"

// List of all the supported effects and their names
static const EffectDesc EFFECT_LIST[] = {
    { "Solid",          &EffectEngine::effectSolidColor },
    { "Blink",          &EffectEngine::effectBlink },
    { "Flash",          &EffectEngine::effectFlash },
    { "Rainbow",        &EffectEngine::effectRainbowCycle },
    { "Chase",          &EffectEngine::effectChase },
    { "Fire flicker",   &EffectEngine::effectFireFlicker },
    { "Lightning",      &EffectEngine::effectLightning },
    { "Breathe",      &EffectEngine::effectBreathe }
};

// Effect defaults
const char DEFAULT_EFFECT[] = "Solid";
const CRGB DEFAULT_EFFECT_COLOR = { 255, 255, 255 };
const uint8_t DEFAULT_EFFECT_BRIGHTNESS = 255;
const bool DEFAULT_EFFECT_REVERSE = false;
const bool DEFAULT_EFFECT_MIRROR = false;
const bool DEFAULT_EFFECT_ALLLEDS = false;

EffectEngine::EffectEngine() {
    // Initialize with defaults
    setEffect(DEFAULT_EFFECT);
    setColor(DEFAULT_EFFECT_COLOR);
    setBrightness(DEFAULT_EFFECT_BRIGHTNESS);
    setReverse(DEFAULT_EFFECT_REVERSE);
    setMirror(DEFAULT_EFFECT_MIRROR);
    setAllLeds(DEFAULT_EFFECT_ALLLEDS);
}

void EffectEngine::begin(DRIVER* ledDriver, uint16_t ledCount) {
    _ledDriver = ledDriver;
    _ledCount = ledCount;
    _initialized = true;
}

void EffectEngine::run() {
    if (_initialized && _activeEffect) {
        uint32_t now = millis();
        if (now > _effectTimeout) {
            uint16_t delay = (this->*_activeEffect->func)();
            _effectTimeout = now + max((int)delay, MIN_EFFECT_DELAY);
            _effectCounter++;
        }
    }
}

void EffectEngine::setEffect(const char* effectName) {
    const uint8_t effectCount = sizeof(EFFECT_LIST) / sizeof(EffectDesc);
    for (uint8_t effect = 0; effect < effectCount; effect++) {
        if (strcmp(effectName, EFFECT_LIST[effect].name) == 0) {
            if (_activeEffect != &EFFECT_LIST[effect]) {
                _activeEffect = &EFFECT_LIST[effect];
                _effectTimeout = 0;
                _effectCounter = 0;
                _effectStep = 0;
            }
            return;
        }
    }

    _activeEffect = nullptr;
    clearAll();
}

void EffectEngine::setPixel(uint16_t idx,  CRGB color) {
    _ledDriver->setValue(3 * idx + 0, (color.r * _effectBrightness) >> 8);
    _ledDriver->setValue(3 * idx + 1, (color.g * _effectBrightness) >> 8);
    _ledDriver->setValue(3 * idx + 2, (color.b * _effectBrightness) >> 8);
}

void EffectEngine::setRange(uint16_t first, uint16_t len, CRGB color) {
    for (uint16_t i=first; i < min(uint16_t(first+len), _ledCount); i++) {
        setPixel(i, color);
    }
}

void EffectEngine::clearRange(uint16_t first, uint16_t len) {
    for (uint16_t i=first; i < min(uint16_t(first+len), _ledCount); i++) {
        setPixel(i, {0, 0, 0});
    }
}

void EffectEngine::setAll(CRGB color) {
    setRange(0, _ledCount, color);
}

void EffectEngine::clearAll() {
    clearRange(0, _ledCount);
}

CRGB EffectEngine::colorWheel(uint8_t pos) {
    pos = 255 - pos;
    if (pos < 85) {
        return { 255 - pos * 3, 0, pos * 3};
    } else if (pos < 170) {
        pos -= 85;
        return { 0, pos * 3, 255 - pos * 3 };
    } else {
        pos -= 170;
        return { pos * 3, 255 - pos * 3, 0 };
    }
}

uint16_t EffectEngine::effectSolidColor() {
    for (uint16_t i=0; i < _ledCount; i++) {
        setPixel(i, _effectColor);
    }
    return 32;
}

uint16_t EffectEngine::effectChase() {
    // calculate only half the pixels if mirroring
    uint16_t lc = _ledCount;
    if (_effectMirror) {
        lc = lc / 2;
    }
    // Prevent errors if we come from another effect with more steps
    // or switch from the upper half of non-mirror to mirror mode
    _effectStep = _effectStep % lc;

    for (uint16_t i=0; i < lc; i++) {
        if (i != _effectStep) {
            if (_effectMirror) {
                setPixel(i + lc, {0, 0, 0});
                setPixel(lc - 1 - i, {0, 0, 0});
            } else {
                setPixel(i, {0, 0, 0});
            }
        }
    }
    uint16_t pixel = _effectStep;
    if (_effectReverse) {
      pixel = lc - 1 - pixel;
    }
    if (_effectMirror) {
        setPixel(pixel + lc, _effectColor);
        setPixel(lc - 1 - pixel, _effectColor);
    } else {
        setPixel(pixel, _effectColor);
    }

    _effectStep = ++_effectStep % lc;
    return _effectSpeed / 32;
}

uint16_t EffectEngine::effectRainbowCycle() {
    // calculate only half the pixels if mirroring
    uint16_t lc = _ledCount;
    if (_effectMirror) {
        lc = lc / 2;
    }
    for (uint16_t i=0; i < lc; i++) {
//      CRGB color = colorWheel(((i * 256 / lc) + _effectStep) & 0xFF);

        double hue = 0;
        if (_effectAllLeds) {
            hue = _effectStep*360.0d / 256;	// all same colour
        } else {
            hue = 360.0 * (((i * 256 / lc) + _effectStep) & 0xFF) / 255;
        }
        double sat = 1.0;
        double val = 1.0;
        CRGB color = hsv2rgb ( { hue, sat, val } );

        uint16_t pixel = i;
        if (_effectReverse) {
            pixel = lc - 1 - pixel;
        }
        if (_effectMirror) {
            setPixel(pixel + lc, color);
            setPixel(lc - 1 - pixel, color);
        } else {
            setPixel(pixel, color);
        }
    }

    _effectStep = ++_effectStep & 0xFF;
    return _effectSpeed / 256;
}

uint16_t EffectEngine::effectBlink() {
    // The Blink effect uses two "time slots": on, off
    // Using default speed, a complete sequence takes 2s.
    if (_effectStep % 2) {
      clearAll();
    } else {
      setAll(_effectColor);
    }

    _effectStep = ++_effectStep % 2;
    return _effectSpeed / 1;
}

uint16_t EffectEngine::effectFlash() {
    // The Flash effect uses 6 "time slots": on, off, on, off, off, off
    // Using default speed, a complete sequence takes 2s.
    // Prevent errors if we come from another effect with more steps
    _effectStep = _effectStep % 6;

    switch (_effectStep) {
      case 0:
      case 2:
        setAll(_effectColor);
        break;
      default:
        clearAll();
    }

    _effectStep = ++_effectStep % 6;
    return _effectSpeed / 3;
}

uint16_t EffectEngine::effectFireFlicker() {
  byte rev_intensity = 6; // more=less intensive, less=more intensive
  byte lum = max(_effectColor.r, max(_effectColor.g, _effectColor.b)) / rev_intensity;
  for ( int i = 0; i < _ledCount; i++) {
    byte flicker = random(lum);
    setPixel(i, CRGB { max(_effectColor.r - flicker, 0), max(_effectColor.g - flicker, 0), max(_effectColor.b - flicker, 0) });
  }
  _effectStep = ++_effectStep % _ledCount;
  return _effectSpeed / 10;
}

uint16_t EffectEngine::effectLightning() {
  static byte maxFlashes;
  static int timeslot = _effectSpeed / 1000; // 1ms
  int flashPause = 10; // 10ms
  uint16_t ledStart = random(_ledCount);
  uint16_t ledLen = random(1, _ledCount - ledStart);
  byte intensity; // flash intensity

  if (_effectStep % 2) {
    // odd steps = clear
    clearAll();
    if (_effectStep == 1) {
      // pause after 1st flash is longer
      flashPause = 130;
    } else {
      flashPause = random(50, 151); // pause between flashes 50-150ms
    }
  } else {
    // even steps = flashes
    if (_effectStep == 0) {
      // first flash (weaker and longer pause)
      maxFlashes = random(3, 8); // 2-6 follow-up flashes
      intensity = random(128);
    } else {
      // follow-up flashes (stronger)
      intensity = random(128, 256); // next flashes are stronger
    }
    CRGB temprgb = { _effectColor.r*intensity/256, _effectColor.g*intensity/256, _effectColor.b*intensity/256 };
    setRange(ledStart, ledLen, temprgb );
    flashPause = random(4, 21); // flash duration 4-20ms
  }

  _effectStep++;

  if (_effectStep >= maxFlashes * 2) {
    _effectStep = 0;
    flashPause = random(100, 5001); // between 0.1 and 5s
  }
  return timeslot * flashPause;
}

uint16_t EffectEngine::effectBreathe() {
  /*
   * Subtle "breathing" effect, works best with gamma correction on.
   *
   * The average resting respiratory rate of an adult is 12–18 breaths/minute.
   * We use 12 breaths/minute = 5.0s/breath at the default _effectSpeed.
   * The tidal volume (~0.5l) is much less than the total lung capacity,
   * so we vary only between 75% and 100% of the set brightness.
   *
   * Per default, this is subtle enough to use with a flood, spot, ceiling or
   * even bedside light. If you want more variation, use the values given
   * below for a 33%/67% variation.
   *
   * In the calculation, we use some constants to make it faster:
   * 0.367879441 is: 1/e
   * 0.106364766 is: 0.25/(e-1/e)  [25% brightness variation, use 0.140401491 for 33%]
   * 0.75 is the offset [75% min brightness, use 0.67 for 67%]
   *
   * See also https://sean.voisen.org/blog/2011/10/breathing-led-with-arduino/
   * for a nice explanation of the math.
   */
  // sin() is in radians, so 2*PI rad is a full period; compiler should optimize.
  float val = (exp(sin(millis()/(_effectSpeed*5.0)*2*PI)) - 0.367879441) * 0.106364766 + 0.75;
  setAll({_effectColor.r*val, _effectColor.g*val, _effectColor.b*val});
  return _effectSpeed / 40; // update every 25ms
}


// dCHSV hue 0->360 sat 0->1.0 val 0->1.0
dCHSV EffectEngine::rgb2hsv(CRGB in_int)
{
    dCHSV       out;
    dCRGB       in = {in_int.r, in_int.g, in_int.b};
    double      min, max, delta;

    min = in.r < in.g ? in.r : in.g;
    min = min  < in.b ? min  : in.b;

    max = in.r > in.g ? in.r : in.g;
    max = max  > in.b ? max  : in.b;

    out.v = max;                                // v
    delta = max - min;
    if (delta < 0.00001)
    {
        out.s = 0;
        out.h = 0; // undefined, maybe nan?
        return out;
    }
    if( max > 0.0 ) { // NOTE: if Max is == 0, this divide would cause a crash
        out.s = (delta / max);                  // s
    } else {
        // if max is 0, then r = g = b = 0
        // s = 0, v is undefined
        out.s = 0.0;
        out.h = NAN;                            // its now undefined
        return out;
    }
    if( in.r >= max )                           // > is bogus, just keeps compilor happy
        out.h = ( in.g - in.b ) / delta;        // between yellow & magenta
    else
    if( in.g >= max )
        out.h = 2.0 + ( in.b - in.r ) / delta;  // between cyan & yellow
    else
        out.h = 4.0 + ( in.r - in.g ) / delta;  // between magenta & cyan

    out.h *= 60.0;                              // degrees

    if( out.h < 0.0 )
        out.h += 360.0;

    return out;
}


// dCHSV hue 0->360 sat 0->1.0 val 0->1.0
CRGB EffectEngine::hsv2rgb(dCHSV in)
{
    double      hh, p, q, t, ff;
    long        i;
    dCRGB       out;
    CRGB out_int = {};

    if(in.s <= 0.0) {       // < is bogus, just shuts up warnings
        out.r = in.v;
        out.g = in.v;
        out.b = in.v;
        out_int = {255*out.r, 255*out.g, 255*out.b};
        return out_int;
    }
    hh = in.h;
    if(hh >= 360.0) hh = 0.0;
    hh /= 60.0;
    i = (long)hh;
    ff = hh - i;
    p = in.v * (1.0 - in.s);
    q = in.v * (1.0 - (in.s * ff));
    t = in.v * (1.0 - (in.s * (1.0 - ff)));

    switch(i) {
    case 0:
        out.r = in.v;
        out.g = t;
        out.b = p;
        break;
    case 1:
        out.r = q;
        out.g = in.v;
        out.b = p;
        break;
    case 2:
        out.r = p;
        out.g = in.v;
        out.b = t;
        break;

    case 3:
        out.r = p;
        out.g = q;
        out.b = in.v;
        break;
    case 4:
        out.r = t;
        out.g = p;
        out.b = in.v;
        break;
    case 5:
    default:
        out.r = in.v;
        out.g = p;
        out.b = q;
        break;
    }
    out_int = {255*out.r, 255*out.g, 255*out.b};
    return out_int;
}

