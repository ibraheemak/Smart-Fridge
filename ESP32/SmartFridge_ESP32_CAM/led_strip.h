#pragma once

#include <FastLED.h>
#include "parameters.h"

static CRGB leds[LED_NUM_LEDS];

void initLEDStrip() {
  FastLED.addLeds<WS2811, LED_DATA_PIN, RGB>(leds, LED_NUM_LEDS);
  FastLED.setBrightness(LED_BRIGHTNESS);
  FastLED.clear(true);
  Serial.println("[LED] LED strip initialized");

  // Startup blink — confirms hardware connection before first scan
  fill_solid(leds, LED_NUM_LEDS, CRGB::White);
  FastLED.show();
  delay(500);
  FastLED.clear(true);
  Serial.println("[LED] Startup blink done");
}

void ledStripOn() {
  fill_solid(leds, LED_NUM_LEDS, CRGB::White);
  FastLED.show();
  Serial.println("[LED] LED strip ON");
}

void ledStripOff() {
  FastLED.clear(true);
  Serial.println("[LED] LED strip OFF");
}
