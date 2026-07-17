#include <Adafruit_NeoPixel.h>
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>

#define LED_PIN       8
#define DEBOUNCE_MS   30
#define FLASH_MS      300
#define BOOT_STEP_MS  1000

#define LCD_DC   9
#define LCD_CS   10
#define LCD_RST  14

// 4개는 LED와 매칭되는 스위치, 5번째(GPIO18)는 LCD 표시 전용(LED 없음)
const uint8_t SWITCH_PINS[] = {7, 15, 16, 17, 18};
const uint8_t NUM_SWITCHES = sizeof(SWITCH_PINS) / sizeof(SWITCH_PINS[0]);
const uint8_t NUM_LEDS = 4;

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_PCD8544 display(LCD_DC, LCD_CS, LCD_RST);  // 하드웨어 SPI(FSPI 기본 핀)

volatile bool switchPressed[NUM_SWITCHES] = {false};
volatile bool checkPending[NUM_SWITCHES] = {false};
volatile uint32_t lastChangeMs[NUM_SWITCHES] = {0};

bool ledOn[NUM_LEDS] = {false};
uint32_t ledOffAt[NUM_LEDS] = {0};

bool switch5State = false;
uint8_t lastPressedNum = 0;  // 0 = 아직 눌린 적 없음
uint8_t lastR = 0, lastG = 0, lastB = 0;

uint32_t switchColor(uint8_t idx) {
  switch (idx) {
    case 0: return strip.Color(255, 0, 0);       // R
    case 1: return strip.Color(0, 255, 0);       // G
    case 2: return strip.Color(0, 0, 255);       // B
    default: return strip.Color(255, 255, 255);  // W
  }
}

void colorComponents(uint32_t color, uint8_t &r, uint8_t &g, uint8_t &b) {
  r = (color >> 16) & 0xFF;
  g = (color >> 8) & 0xFF;
  b = color & 0xFF;
}

void IRAM_ATTR handleSwitchInterrupt(void* arg) {
  uint8_t idx = (uint8_t)(uintptr_t)arg;
  lastChangeMs[idx] = millis();
  checkPending[idx] = true;  // 실제 레벨 판정은 debounce 후 loop()에서
}

void bootAnimation() {
  strip.clear();
  strip.show();

  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, switchColor(i));
    strip.show();
    delay(BOOT_STEP_MS);
  }

  strip.clear();
  strip.show();
}

void redrawLCD() {
  display.clearDisplay();

  display.setCursor(0, 0);
  display.print("SW5: ");
  display.print(switch5State ? "ON" : "OFF");

  display.setCursor(0, 16);
  if (lastPressedNum > 0) {
    display.print("SW: ");
    display.print(lastPressedNum);
  }

  display.setCursor(0, 32);
  if (lastPressedNum > 0) {
    display.print(lastR);
    display.print(",");
    display.print(lastG);
    display.print(",");
    display.print(lastB);
  }

  display.display();
}

void setup() {
  Serial.begin(115200);

  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    pinMode(SWITCH_PINS[i], INPUT_PULLUP);
  }

  strip.begin();
  strip.setBrightness(18);
  strip.clear();
  strip.show();

  bootAnimation();

  display.begin();
  display.setRotation(2);
  display.setContrast(20);
  display.setTextSize(1);
  display.setTextColor(BLACK);
  redrawLCD();

  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    attachInterruptArg(SWITCH_PINS[i], handleSwitchInterrupt, (void*)(uintptr_t)i, CHANGE);
  }
}

void loop() {
  uint32_t now = millis();

  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    if (checkPending[i] && (now - lastChangeMs[i] >= DEBOUNCE_MS)) {
      checkPending[i] = false;

      bool pressed = (digitalRead(SWITCH_PINS[i]) == LOW);  // 풀업 -> 누르면 LOW
      if (pressed != switchPressed[i]) {
        switchPressed[i] = pressed;

        if (i < NUM_LEDS) {
          if (pressed) {
            uint32_t color = switchColor(i);
            strip.setPixelColor(i, color);
            strip.show();
            ledOn[i] = true;
            ledOffAt[i] = now + FLASH_MS;

            lastPressedNum = i + 1;
            colorComponents(color, lastR, lastG, lastB);
            redrawLCD();

            Serial.printf("GPIO%d pressed\n", SWITCH_PINS[i]);
          }
        } else {
          switch5State = pressed;
          redrawLCD();
        }
      }
    }
  }

  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    if (ledOn[i] && now >= ledOffAt[i]) {
      strip.setPixelColor(i, strip.Color(0, 0, 0));
      strip.show();
      ledOn[i] = false;
    }
  }
}
