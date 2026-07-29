#include <Adafruit_NeoPixel.h>
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>

// LED 점등/소등 상태 관리
#define LED_PIN       8
// 스위치 디바운스 시간
#define DEBOUNCE_MS   30
// LED 켜짐 유지 시간
#define FLASH_MS      300
// 부팅 애니메이션 간격 (ms)
#define BOOT_STEP_MS  1000

// LCD 핀 설정
#define LCD_DC   9
#define LCD_CS   10
#define LCD_RST  14

// 스위치 1~4: LED 매칭, 5번: LCD 전용
const uint8_t SWITCH_PINS[] = {7, 15, 16, 17, 18};
// 스위치 개수 자동 계산
const uint8_t NUM_SWITCHES = sizeof(SWITCH_PINS) / sizeof(SWITCH_PINS[0]);
// LED 개수 (1~4번 버튼용)
const uint8_t NUM_LEDS = 4;

// NeoPixel 스트립 객체
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
// LCD 객체
Adafruit_PCD8544 display(LCD_DC, LCD_CS, LCD_RST);

// ISR과 loop() 공유 변수 (volatile)
volatile bool switchPressed[NUM_SWITCHES] = {false};
volatile bool checkPending[NUM_SWITCHES] = {false};  // 인터럽트 발생 플래그
volatile uint32_t lastChangeMs[NUM_SWITCHES] = {0};  // 마지막 핀 상태 변화 시각

bool ledOn[NUM_LEDS] = {false};
uint32_t ledOffAt[NUM_LEDS] = {0};

bool switch5State = false;           // 5번 스위치 상태
uint8_t lastPressedNum = 0;          // 마지막 눌린 스위치 번호 (떼도 화면 유지)
uint8_t lastR = 0, lastG = 0, lastB = 0; // 마지막 눌린 버튼의 RGB 색상 값

// 스위치 번호별 고정 색상 반환
uint32_t switchColor(uint8_t idx) {
  switch (idx) {
    case 0: return strip.Color(255, 0, 0);       // R
    case 1: return strip.Color(0, 255, 0);       // G
    case 2: return strip.Color(0, 0, 255);       // B
    case 3: return strip.Color(255, 255, 255);   // W
    default: return strip.Color(0, 0, 0);        // Off
  }
}

// 24비트 색상 값을 R/G/B로 분리
void colorComponents(uint32_t color, uint8_t &r, uint8_t &g, uint8_t &b) {
  r = (color >> 16) & 0xFF;
  g = (color >> 8) & 0xFF;
  b = color & 0xFF;
}

// 스위치 상태 변화 인터럽트 서비스 루틴 (ISR)
void IRAM_ATTR handleSwitchInterrupt(void* arg) {
  uint8_t idx = (uint8_t)(uintptr_t)arg;  // 스위치 인덱스 복원
  lastChangeMs[idx] = millis();           // 변화 시각 기록
  checkPending[idx] = true;               // loop()에서 재확인하도록 플래그 세팅
}
// WS2815 타이밍 안정화용 show 함수 (인터럽트 전역 차단 제거)
void safeShow() {
  strip.show();
  //delayMicroseconds(300); // WS2815 Reset Time (>=280us) 보장
}

// 부팅 시 LED 순차 점등 테스트
void bootAnimation() {
  strip.clear();
  safeShow();

  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, switchColor(i));
    safeShow();
    delay(BOOT_STEP_MS);
  }

  // 테스트 종료 후 LED 오프
  strip.clear();
  safeShow();
}

// 현재 상태를 LCD 한 장면으로 다시 그린다.
// LCD 화면 업데이트 함수 (5번 상태, 마지막 스위치 번호 및 색상 표시)
void redrawLCD() {
  display.clearDisplay();

  // 상단: 5번 스위치 상태 표시
  display.setCursor(0, 0);
  display.print("SW5: ");
  display.print(switch5State ? "OFF" : "ON");

  // 하단: 마지막 스위치 번호 표시
  display.setCursor(0, 16);
  if (lastPressedNum > 0) {
    display.print("SW: ");
    display.print(lastPressedNum);
  }

  // 하단 둘째 줄: RGB 색상 값 표시
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

// 초기 설정
void setup() {
  // 스위치 핀 입력 설정
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    pinMode(SWITCH_PINS[i], INPUT_PULLUP);
  }

  // NeoPixel 초기화 및 밝기 설정
  strip.begin();
  strip.setBrightness(200);
  strip.clear();
  safeShow();

  // 부팅 테스트 실행
  bootAnimation();

  // LCD 초기화
  display.begin();
  display.setRotation(2);
  display.setContrast(12);
  display.setTextSize(1);
  display.setTextColor(BLACK);
  redrawLCD();

  // 모든 스위치 인터럽트 설정
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    attachInterruptArg(SWITCH_PINS[i], handleSwitchInterrupt, (void*)(uintptr_t)i, CHANGE);
  }
}

// 메인 루프 (인터럽트, 디바운싱, LED/LCD 처리)
void loop() {
  uint32_t now = millis();

  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    // pending 상태가 있으면, 먼저 현재 레벨을 읽어서 눌림일 때는 LED를 즉시 켠다.
    // LCD/상태 확정은 아래의 디바운스 구간에서 처리하지만, LED 반응만큼은 지연을 최소화한다.
    if (checkPending[i]) {
      bool pressedNow = (digitalRead(SWITCH_PINS[i]) == LOW);
      if (pressedNow && i < NUM_LEDS && !switchPressed[i] && !ledOn[i]) {
        uint32_t color = switchColor(i);
        strip.setPixelColor(i, color);
        safeShow();
        ledOn[i] = true;
        ledOffAt[i] = now + FLASH_MS;
      }
    }

    // 디바운스 조건 확인
    if (checkPending[i] && (now - lastChangeMs[i] >= DEBOUNCE_MS)) {
      checkPending[i] = false;

      // 핀 상태 읽기
      bool pressed = (digitalRead(SWITCH_PINS[i]) == LOW);
      if (pressed != switchPressed[i]) {
        switchPressed[i] = pressed;

        if (i < NUM_LEDS) {
          // 1~4번 메인 스위치 처리
          if (pressed) {
            // 버튼 LED 온 및 오프 타이머 세팅
            uint32_t color = switchColor(i);
            strip.setPixelColor(i, color);
            safeShow();
            ledOn[i] = true;
            ledOffAt[i] = now + FLASH_MS;

            // LCD 상태 데이터 갱신 및 화면 출력
            lastPressedNum = i + 1;
            colorComponents(color, lastR, lastG, lastB);
            redrawLCD();
          }
        } else {
          // 5번 스위치 갱신
          switch5State = pressed;
          redrawLCD();
        }
      }
    }
  }

  // LED 자동 소등 타이머 체크
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    if (ledOn[i] && now >= ledOffAt[i]) {
      strip.setPixelColor(i, strip.Color(0, 0, 0));
      safeShow();
      ledOn[i] = false;
    }
  }
}
