#include <Adafruit_NeoPixel.h>

#define LED_PIN  6
#define NUM_LEDS 180

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

void setup() {
  strip.begin();
  strip.setBrightness(255);                      // 최대 밝기
  strip.fill(strip.Color(255, 255, 255));        // 전체 흰색
  strip.show();
}

void loop() {
  // 아무것도 안 함 - 켜진 상태 유지
}