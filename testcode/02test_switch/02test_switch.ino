#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>

// 스위치 디바운스 시간
#define DEBOUNCE_MS  30

// ST7735 TFT LCD 핀 설정 (5110과 같은 자리. RS=DC, SDA=MOSI, CLK=SCLK)
#define LCD_DC   9    // ST7735의 RS 핀
#define LCD_CS   10
#define LCD_RST  14
#define LCD_SDA  11   // = MOSI (FSPI 하드웨어 기본 핀)
#define LCD_CLK  12   // = SCLK (FSPI 하드웨어 기본 핀)

#define LCD_W        160
#define LCD_H        128
#define LCD_ROTATION 3          // 0,2 = 세로(128x160) / 1,3 = 가로(160x128)
#define LCD_SPI_HZ   24000000   // 40MHz는 화면 아래쪽이 깨진다(01test_ST7735 주석 참고)

// 라이브러리에 회색 상수가 없어 직접 정의(RGB565 50% 회색). 구분선/비활성 글자용
#define LCD_GREY     0x7BEF

// LCD 하단 영역(1~4번 스위치 번호 표시용) 기준 좌표
#define LCD_BOTTOM_AREA_Y  28
#define LCD_BOTTOM_AREA_H  (LCD_H - LCD_BOTTOM_AREA_Y)
#define BIG_TEXT_SIZE      8    // 기본 폰트 6x8px의 배수 → 48x64px

// 스위치 핀 목록 (1~4: 메인 스위치, 5: 상태표시 전용)
const uint8_t SWITCH_PINS[] = {7, 15, 16, 17, 18};
// 스위치 개수 자동 계산
const uint8_t NUM_SWITCHES = sizeof(SWITCH_PINS) / sizeof(SWITCH_PINS[0]);

// 하드웨어 SPI 생성자는 5110과 인자 순서가 다르다: (CS, DC, RST)
Adafruit_ST7735 tft = Adafruit_ST7735(LCD_CS, LCD_DC, LCD_RST);

// 5110은 라이브러리가 프레임버퍼를 들고 있어서 clearDisplay()로 지우고 display()로 한 번에
// 내보내는 방식이었다. ST7735에는 프레임버퍼가 없어 화면에 직접 그리면 깜빡이므로, 같은 크기의
// 캔버스에 그린 뒤 통째로 전송한다. 덕분에 그리는 코드는 5110 때와 똑같이 쓸 수 있다.
class Lcd : public GFXcanvas16 {
public:
  Lcd() : GFXcanvas16(LCD_W, LCD_H) {}
  void begin() {
    SPI.begin(LCD_CLK, -1, LCD_SDA, LCD_CS);   // MISO는 LCD가 쓰지 않아 -1
    tft.initR(INITR_BLACKTAB);
    tft.setSPISpeed(LCD_SPI_HZ);
    tft.setRotation(LCD_ROTATION);
    tft.fillScreen(ST77XX_BLACK);
  }
  void clearDisplay() { fillScreen(ST77XX_BLACK); }
  // 40KB(160x128x2) 전송이라 24MHz에서 약 14ms 걸린다. 매 loop마다 부르지 말 것.
  void display() { tft.drawRGBBitmap(0, 0, getBuffer(), LCD_W, LCD_H); }
};

// LCD 제어 객체
Lcd display;

// 스위치 1~4에 대응하는 색(다른 테스트 스케치와 같은 순서)
const uint16_t SWITCH_COLORS[4] = {ST77XX_RED, ST77XX_GREEN, ST77XX_BLUE, ST77XX_WHITE};

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

  display.setTextSize(BIG_TEXT_SIZE);
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

  // 상단: 5번 스위치 상태 표시(ON = 초록, OFF = 회색)
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.setTextColor(ST77XX_WHITE);
  display.print("SW5:");
  display.setTextColor(switch5State ? LCD_GREY : ST77XX_GREEN);
  display.print(switch5State ? "OFF" : "ON");

  display.drawFastHLine(0, LCD_BOTTOM_AREA_Y - 6, LCD_W, LCD_GREY);

  // 하단: 마지막으로 누른 메인 스위치 번호를 그 스위치 색으로 크게 표시
  if (lastPressedMainSwitch > 0) {
    display.setTextColor(SWITCH_COLORS[lastPressedMainSwitch - 1]);
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
  display.setTextColor(ST77XX_WHITE);

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
