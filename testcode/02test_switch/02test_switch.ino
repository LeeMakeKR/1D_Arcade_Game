#include <Adafruit_NeoPixel.h>

#define LED_PIN      48
#define NUM_LEDS     1
#define DEBOUNCE_MS  30
#define BRIGHTNESS   10  // 기존 50에서 절반

const uint8_t SWITCH_PINS[] = {7, 15, 16, 17, 18};
const uint8_t NUM_SWITCHES = sizeof(SWITCH_PINS) / sizeof(SWITCH_PINS[0]);

Adafruit_NeoPixel pixel(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

volatile bool switchPressed[NUM_SWITCHES] = {false};
volatile bool checkPending[NUM_SWITCHES] = {false};
volatile uint32_t lastChangeMs[NUM_SWITCHES] = {0};

void IRAM_ATTR handleSwitchInterrupt(void* arg) {
  uint8_t idx = (uint8_t)(uintptr_t)arg;
  lastChangeMs[idx] = millis();
  checkPending[idx] = true;  // 실제 레벨 판정은 debounce 후 loop()에서
}

void setup() {
  Serial.begin(115200);

  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    pinMode(SWITCH_PINS[i], INPUT_PULLUP);
    attachInterruptArg(SWITCH_PINS[i], handleSwitchInterrupt, (void*)(uintptr_t)i, CHANGE);
  }

  pixel.begin();
  pixel.setBrightness(BRIGHTNESS);
  pixel.show();
}

void loop() {
  bool stateChanged = false;

  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    if (checkPending[i] && (millis() - lastChangeMs[i] >= DEBOUNCE_MS)) {
      checkPending[i] = false;

      bool pressed = (digitalRead(SWITCH_PINS[i]) == LOW);  // 풀업 -> 누르면 LOW
      if (pressed != switchPressed[i]) {
        switchPressed[i] = pressed;
        stateChanged = true;
        Serial.printf("GPIO%d %s\n", SWITCH_PINS[i], pressed ? "pressed" : "released");
      }
    }
  }

  if (stateChanged) {
    bool anyPressed = false;
    for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
      if (switchPressed[i]) anyPressed = true;
    }
    pixel.setPixelColor(0, anyPressed ? pixel.Color(0, 255, 0) : pixel.Color(255, 0, 0));
    pixel.show();
  }
}
