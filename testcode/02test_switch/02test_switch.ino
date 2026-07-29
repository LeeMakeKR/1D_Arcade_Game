#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>

// 스위치 디바운스 시간
#define DEBOUNCE_MS  30

// Nokia 5110 LCD 핀 설정 (하드웨어 고정값)
#define LCD_DC   9
#define LCD_CS   10
#define LCD_RST  14

// LCD 하단 영역(1~4번 스위치 표시용) 기준 좌표
#define LCD_BOTTOM_AREA_Y 10
#define LCD_BOTTOM_AREA_H  38

// 스위치 핀 목록 (1~4: 메인 스위치, 5: 상태표시 전용)
const uint8_t SWITCH_PINS[] = {7, 15, 16, 17, 18};
// 스위치 개수 자동 계산
const uint8_t NUM_SWITCHES = sizeof(SWITCH_PINS) / sizeof(SWITCH_PINS[0]);

// LCD 제어 객체
Adafruit_PCD8544 display(LCD_DC, LCD_CS, LCD_RST);

// ISR과 loop() 공유 변수 (최적화 방지 volatile 선언)
volatile bool switchPressed[NUM_SWITCHES] = {false}; // 스위치 확정 상태
volatile bool checkPending[NUM_SWITCHES] = {false};  // 인터럽트 발생 플래그
volatile uint32_t lastChangeMs[NUM_SWITCHES] = {0};  // 마지막 상태 변화 시각 (디바운스 기준)

bool switch5State = false;           // 5번 스위치 개별 상태 변수
uint8_t lastPressedMainSwitch = 0;   // 마지막으로 눌린 메인 스위치 번호 (화면 유지용)
bool lcdDirty = true;                // LCD 갱신 필요 플래그 (깜빡임 방지)

// 스위치 인터럽트 서비스 루틴 (ISR)
void IRAM_ATTR handleSwitchInterrupt(void* arg) {
  uint8_t idx = (uint8_t)(uintptr_t)arg;
  lastChangeMs[idx] = millis();
  checkPending[idx] = true;  // loop()에서 디바운스 확인 요청
}

// 텍스트를 화면 중앙에 정렬하여 그리는 함수
void drawCenteredLargeText(const char* text) {
  int16_t x1, y1;
  uint16_t w, h;

  display.setTextSize(4);
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);

  // 중앙 배치 좌표 계산
  int16_t x = (display.width() - (int16_t)w) / 2 - x1;
  int16_t y = LCD_BOTTOM_AREA_Y + ((LCD_BOTTOM_AREA_H - (int16_t)h) / 2) - y1;

  display.setCursor(x, y);
  display.print(text);
}

// LCD 화면 갱신 함수 (5번 상태 및 마지막 누른 버튼 번호 표시)
void redrawLCD() {
  char bigText[4] = "";

  if (lastPressedMainSwitch > 0) {
    snprintf(bigText, sizeof(bigText), "%u", lastPressedMainSwitch);
  }

  display.clearDisplay();

  // 상단: 5번 스위치 상태 표시
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("SW5: ");
  display.print(switch5State ? "OFF" : "ON");

  // 하단: 메인 스위치 번호 표시
  if (lastPressedMainSwitch > 0) {
    drawCenteredLargeText(bigText);
  }

  display.display();
}

// 초기 설정
void setup() {
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    pinMode(SWITCH_PINS[i], INPUT_PULLUP);
    attachInterruptArg(SWITCH_PINS[i], handleSwitchInterrupt, (void*)(uintptr_t)i, CHANGE);
  }

  display.begin();
  display.setRotation(2);   // 180도 회전
  display.setContrast(13);  // 대비 설정
  display.setTextColor(BLACK);
  
  redrawLCD();
  lcdDirty = false;
}

// 메인 루프 (인터럽트 처리, 디바운싱 및 화면 갱신)
void loop() {
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    // 디바운스 시간 경과 확인
    if (checkPending[i] && (millis() - lastChangeMs[i] >= DEBOUNCE_MS)) {
      checkPending[i] = false;

      bool pressed = (digitalRead(SWITCH_PINS[i]) == LOW);
      if (pressed != switchPressed[i]) {
        switchPressed[i] = pressed;
        if (i == 4) {
          // 5번 스위치 상태 변경
          if (switch5State != pressed) {
            switch5State = pressed;
            lcdDirty = true;
          }
        } else if (pressed) {
          // 1~4번 스위치 눌림 (떼도 화면 유지)
          uint8_t nextMainSwitch = i + 1;
          if (lastPressedMainSwitch != nextMainSwitch) {
            lastPressedMainSwitch = nextMainSwitch;
            lcdDirty = true;
          }
        }
      }
    }
  }

  // 화면 업데이트가 필요한 경우에만 렌더링
  if (lcdDirty) {
    redrawLCD();
    lcdDirty = false;
  }
}
