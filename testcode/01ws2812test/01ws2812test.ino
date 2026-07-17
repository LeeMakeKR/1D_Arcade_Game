#include <Adafruit_NeoPixel.h>

#define LED_PIN   8
#define NUM_LEDS  4

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

void setup() {
  strip.begin();
  strip.setBrightness(50);
  strip.clear();
  strip.show();
}

void loop() {
  strip.clear();
  strip.show();

  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, strip.Color(255, 255, 255));
    strip.show();
    delay(500);
  }

  delay(1000);

  strip.clear();
  strip.show();

  delay(1000);
}
