#pragma once
/*
  ===========================================================================
  Arcade.h - 통합 스케치의 공용 계층
  ===========================================================================
  메뉴와 세 게임이 같이 쓰는 것들만 모아 둔다.
    - 하드웨어 객체(LED 스트립, LCD)
    - 스위치 입력(디바운스 + 이번 프레임에 새로 눌린 버튼)
    - 설정값(LED 밝기 / 사운드 볼륨)
    - 코트 좌표와 색 유틸

  이 파일은 .ino 한 곳에서만 include된다(게임 헤더들은 이 파일을 include한다).
  헤더에 정의까지 들어 있는 것은 사운드 데이터 헤더와 같은 이유다 - 아두이노 IDE는
  스케치 폴더의 .cpp만 컴파일하므로 번역 단위가 .ino 하나뿐이다.

  ---------------------------------------------------------------------------
  버튼 규약 (메뉴와 게임이 공유한다)
  ---------------------------------------------------------------------------
    인덱스 0 = 스위치 1 = Red   = 뒤로/취소
    인덱스 1 = 스위치 2 = Green = 위로
    인덱스 2 = 스위치 3 = Blue  = 선택
    인덱스 3 = 스위치 4 = White = 아래로
  게임 안에서는 네 개를 다른 뜻으로 쓰기도 하지만(퐁은 전부 "치기"), Red는 어디서나
  "메뉴로 돌아가기"로 남겨 둔다.
  ===========================================================================
*/

#include <Adafruit_NeoPixel.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <Preferences.h>

#include "SoundEngine.h"
#include "Logo1DArcade.h"

// ---------------------------------------------------------------------------
// 하드웨어
// ---------------------------------------------------------------------------
#define LED_PIN         8    // WS2815 데이터 핀(프로토콜은 WS2812 호환)

#define NUM_LEDS        176  // 스위치 내장 LED 4개 + 라인 LED 172개
#define NUM_SWITCH_LEDS 4    // index 0~3 = 스위치 1~4 내장 LED

#define FIELD_START     NUM_SWITCH_LEDS   // 필드 첫 칸(플레이어 쪽 끝) = 인덱스 4
#define FIELD_END       (NUM_LEDS - 1)    // 필드 마지막 칸(반대쪽 끝) = 인덱스 175
#define FIELD_LEN       (FIELD_END - FIELD_START + 1)   // 172

// ST7735 TFT LCD 핀 (RS=DC, SDA=MOSI, CLK=SCLK)
#define LCD_DC   9
#define LCD_CS   10
#define LCD_RST  14
#define LCD_SDA  11
#define LCD_CLK  12

#define LCD_W        160
#define LCD_H        128
#define LCD_ROTATION 3          // 0,2 = 세로(128x160) / 1,3 = 가로(160x128)
#define LCD_SPI_HZ   24000000   // 40MHz는 화면 아래쪽이 깨진다(01test_ST7735 주석 참고)
#define LCD_GREY     0x7BEF     // 라이브러리에 없는 RGB565 50% 회색

#define I2S_BCLK_PIN  42
#define I2S_LRC_PIN   41
#define I2S_DOUT_PIN  40

#define BOARD_ID_PIN  18   // 5번 토글: HIGH = 라인 LED가 달린 보드(1P), LOW = 스위치만 있는 보드(2P)

// ESP-NOW 채널. 게임이 아니라 보드 설정이라 여기 둔다(.ino의 WiFi 초기화와 DuelPong이 같이 쓴다).
// 두 보드가 같은 채널이어야 서로 보인다. 추후 설정 화면에서 NVS로 저장할 후보.
constexpr uint8_t ESPNOW_CHANNEL = 1;

// 인덱스 = 스위치 LED 위치(0~3). 그 자리의 스위치가 물려 있는 GPIO를 적는다.
// 3번/4번 자리의 배선이 GPIO 번호 순서와 반대라 17과 16을 바꿔 넣었다.
static const uint8_t SWITCH_PINS[4] = {7, 15, 17, 16};
#define NUM_SWITCHES 4

// 버튼 역할 이름. 인덱스를 그대로 쓰지 않고 이름을 거쳐 읽도록 한다.
enum Btn : uint8_t {
  BTN_BACK   = 0,   // Red
  BTN_UP     = 1,   // Green
  BTN_SELECT = 2,   // Blue
  BTN_DOWN   = 3,   // White
};

#define DEBOUNCE_MS   30    // 스위치 채터링 무시 시간
#define FRAME_MS      16    // 스트립 렌더 주기(약 60FPS). 176픽셀 show()가 약 5.3ms
#define BOOT_LOGO_MS  3000  // 부팅 로고를 보여주는 시간

// 게임을 고르고 시작 버튼을 누른 뒤 카운트다운이 시작되기까지의 사이.
// 이 틈이 없으면 선택음과 카운트다운 첫 소리가 겹쳐 뭉개진다.
#define GAME_START_DELAY_MS 2000

// ---------------------------------------------------------------------------
// 객체
// ---------------------------------------------------------------------------
// 색 순서는 RGB다. 데이터시트상 WS2815는 GRB지만 실제 스트립은 R과 G가 반대로 나온다
// (Color(255,0,0)이 초록으로 점등). 스트립을 교체하면 여기부터 확인할 것.
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_RGB + NEO_KHZ800);

// 하드웨어 SPI 생성자 인자 순서: (CS, DC, RST)
Adafruit_ST7735 tft = Adafruit_ST7735(LCD_CS, LCD_DC, LCD_RST);

// ST7735는 프레임버퍼를 들고 있지 않아서 화면에 직접 그리면 깜빡인다. 그래서 화면과 같은 크기의
// 캔버스에 그린 뒤 통째로 전송한다. 한 장이 40KB라 24MHz에서 약 14ms 걸리므로,
// 내용이 실제로 바뀐 순간에만 display()를 부른다.
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
  void display() { tft.drawRGBBitmap(0, 0, getBuffer(), LCD_W, LCD_H); }
};

Lcd display;

// 5번 토글을 지금 읽는다. HIGH = 라인 LED가 달린 보드(Duel Pong의 1P).
// 부팅 때 한 번만 읽으면 메뉴에서 토글을 바꿔도 반영되지 않아 재부팅해야 하므로,
// 값이 필요한 시점마다 이 함수로 읽는다.
bool readBoardIsMain() { return digitalRead(BOARD_ID_PIN) == HIGH; }

// 이 보드에 라인 LED가 물려 있는가. 게임에 들어갈 때 readBoardIsMain()으로 갱신한다.
//
// 이 값은 Duel Pong의 1P/2P 역할을 정하는 데에만 쓴다. 렌더링은 이 값을 보지 않고 늘
// 스트립 전체를 그린다. 토글이 반대로 놓여 있다고 해서 라인 LED가 통째로 죽으면
// 메뉴에서는 원인을 알 방법이 없기 때문이다. 없는 픽셀에 데이터를 흘리는 비용은
// show() 시간뿐이고(약 5.3ms), 스위치만 있는 보드는 어차피 그릴 게 거의 없다.
bool hasLineLeds = true;

// ---------------------------------------------------------------------------
// 사운드
// ---------------------------------------------------------------------------
// 세 게임과 메뉴가 쓰는 소리를 한 표에 모았다. 실제 클립 배열은 .ino에 있다.
// 이름은 "어떤 소리인가"가 아니라 "언제 나는가" 기준이다.
enum SfxId : uint8_t {
  SFX_UI_MOVE = 0,    // 메뉴 커서 이동
  SFX_UI_SELECT,      // 메뉴 선택
  SFX_UI_BACK,        // 뒤로 가기
  SFX_READY_BOTH,     // 양쪽 플레이어가 다 준비됨(2P 전용)
  SFX_COUNTDOWN,      // 카운트다운 한 칸
  SFX_START,          // START
  SFX_SERVE,          // 서브 출발
  SFX_HIT_GREEN,      // 초록 영역에서 반사
  SFX_HIT_YELLOW,     // 노랑 영역에서 반사
  SFX_HIT_RED,        // 빨강 영역에서 반사
  SFX_CPU_HIT,        // CPU가 받아침
  SFX_MISS,           // 공을 놓침
  SFX_MISS_OPPONENT,  // 상대가 놓침
  SFX_SHOOT,          // 총알 발사
  SFX_BULLET_HIT,     // 총알 명중 - 색이 맞음
  SFX_BULLET_MISS,    // 총알 명중 - 색이 틀림
  SFX_SNAKE_STEP,     // 지렁이가 한 칸 전진
  SFX_LEVEL_CLEAR,    // 레벨 클리어
  SFX_WIN,            // 승리
  SFX_GAME_OVER,      // 게임 오버
  NUM_SFX
};

// ---------------------------------------------------------------------------
// 설정 (설정 메뉴에서 바꾸고 모든 게임이 따른다)
// ---------------------------------------------------------------------------
// 단계로 들고 있다가 하드웨어 값으로 변환한다. 화면에 막대로 보여주기 쉽고,
// 나중에 NVS에 저장할 때도 작은 정수 하나면 된다.
#define SETTING_STEPS     10    // 밝기/볼륨 공통 단계 수 (0 = 최소)
#define BRIGHTNESS_MIN    20    // 0단계에서의 스트립 밝기(완전히 끄지는 않는다)
#define BRIGHTNESS_MAX   200    // 최고 단계에서의 스트립 밝기

// 기본값은 한가운데. 단계 수가 바뀌어도 체감 기본이 유지되도록 절반 지점으로 둔다.
uint8_t settingBrightness = SETTING_STEPS / 2;   // 0 ~ SETTING_STEPS
uint8_t settingVolume     = SETTING_STEPS / 2;   // 0 ~ SETTING_STEPS (0 = 무음)

void applyBrightness() {
  uint16_t span = BRIGHTNESS_MAX - BRIGHTNESS_MIN;
  uint8_t v = (uint8_t)(BRIGHTNESS_MIN + (uint32_t)span * settingBrightness / SETTING_STEPS);
  strip.setBrightness(v);
}

void applyVolume() {
  soundSetGain((uint16_t)((uint32_t)SOUND_GAIN_UNITY * settingVolume / SETTING_STEPS));
}

// ---------------------------------------------------------------------------
// 설정 저장 (NVS)
// ---------------------------------------------------------------------------
// ESP32에서는 EEPROM 라이브러리도 결국 NVS 위의 에뮬레이션이라, 권장 방식인
// Preferences를 그대로 쓴다. 단계 값 두 개(각 1바이트)만 저장하면 된다.
namespace Settings {

Preferences prefs;
bool dirty = false;   // 마지막 저장 이후 값이 바뀌었는가

void begin() {
  prefs.begin("arcade", false);   // 네임스페이스 이름은 15자 이내여야 한다
  settingBrightness = prefs.getUChar("bright", SETTING_STEPS / 2);
  settingVolume     = prefs.getUChar("vol",    SETTING_STEPS / 2);

  // 저장된 값이 손상됐거나 단계 수를 줄인 뒤 처음 켠 경우를 막는다
  if (settingBrightness > SETTING_STEPS) settingBrightness = SETTING_STEPS / 2;
  if (settingVolume     > SETTING_STEPS) settingVolume     = SETTING_STEPS / 2;
}

void markDirty() { dirty = true; }

// 값 편집을 빠져나올 때 한 번만 기록한다. 버튼을 누를 때마다 쓰면 플래시 수명을 깎고,
// 조절 중에 쓰기가 끼어들어 반응도 나빠진다.
// 편집 중에는 다른 데로 이동할 수 없으므로, 이 지점이 값이 확정되는 유일한 통로다.
void save() {
  if (!dirty) return;
  dirty = false;
  prefs.putUChar("bright", settingBrightness);   // 값이 같으면 NVS가 알아서 쓰지 않는다
  prefs.putUChar("vol",    settingVolume);
}

}  // namespace Settings

// ---------------------------------------------------------------------------
// 색 유틸
// ---------------------------------------------------------------------------
// 색의 각 성분에 배율을 곱한다. setBrightness()는 스트립 전체에 걸리므로,
// 일부 구간만 어둡게 하려면 색 값 자체를 낮춰야 한다.
uint32_t scaleColor(uint32_t c, float f) {
  uint16_t r = (uint16_t)(((c >> 16) & 0xFF) * f);
  uint16_t g = (uint16_t)(((c >> 8) & 0xFF) * f);
  uint16_t b = (uint16_t)((c & 0xFF) * f);
  if (r > 255) r = 255;
  if (g > 255) g = 255;
  if (b > 255) b = 255;
  return strip.Color((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

uint32_t colRed()    { return strip.Color(255, 0, 0); }
uint32_t colGreen()  { return strip.Color(0, 255, 0); }
uint32_t colBlue()   { return strip.Color(0, 0, 255); }
uint32_t colYellow() { return strip.Color(255, 170, 0); }
uint32_t colWhite()  { return strip.Color(255, 255, 255); }

// 버튼 인덱스에 대응하는 색. 메뉴 조작 규약(Red/Green/Blue/White)이 곧 이 순서다.
uint32_t btnColor(uint8_t idx) {
  switch (idx) {
    case 0:  return colRed();
    case 1:  return colGreen();
    case 2:  return colBlue();
    default: return colWhite();
  }
}

void safeShow() {
  strip.show();
  delayMicroseconds(300);  // WS2815 Reset Time (>=280us) 보장
}

void clearField() {
  for (int16_t i = FIELD_START; i <= FIELD_END; i++) strip.setPixelColor(i, 0);
}

// 스트립 전체(스위치 LED 포함)를 흐르는 무지개로 채운다.
void drawRainbow(uint32_t now, uint16_t periodMs, float scale) {
  uint16_t base = (uint16_t)(((now % periodMs) * 65536UL) / periodMs);
  for (uint16_t i = 0; i < strip.numPixels(); i++) {
    uint16_t hue = base + (uint16_t)((uint32_t)i * 65536UL / NUM_LEDS);
    strip.setPixelColor(i, scaleColor(strip.gamma32(strip.ColorHSV(hue)), scale));
  }
}

// ---------------------------------------------------------------------------
// 코트 좌표 / 존 계산 (두 퐁이 공유한다)
// ---------------------------------------------------------------------------
#define ZONE_RED_LEN      3   // 자기 쪽 끝에서부터 빨강 3칸
#define ZONE_YELLOW_LEN   6   // 그다음 노랑 6칸
#define ZONE_GREEN_LEN    9   // 그다음 초록 9칸
#define ZONE_LEN          (ZONE_RED_LEN + ZONE_YELLOW_LEN + ZONE_GREEN_LEN)

// side 0 = 인덱스가 작은 쪽, 1 = 큰 쪽. 자기 끝에서 몇 칸 떨어져 있는가.
int16_t depthFrom(uint8_t side, int16_t idx) {
  return (side == 0) ? (idx - FIELD_START) : (FIELD_END - idx);
}

// 0 = 빨강, 1 = 노랑, 2 = 초록, -1 = 타격 영역 밖.
int8_t zoneOfDepth(int16_t depth) {
  if (depth < 0 || depth >= ZONE_LEN) return -1;
  if (depth < ZONE_RED_LEN) return 0;
  if (depth < ZONE_RED_LEN + ZONE_YELLOW_LEN) return 1;
  return 2;
}

uint32_t zoneColor(int8_t zone) {
  switch (zone) {
    case 0:  return colRed();
    case 1:  return colYellow();
    default: return colGreen();
  }
}

uint8_t halfOwner(int16_t idx) {
  return (idx < FIELD_START + FIELD_LEN / 2) ? 0 : 1;
}

// ---------------------------------------------------------------------------
// 입력
// ---------------------------------------------------------------------------
// ISR은 시각만 남기고, 디바운스 판정과 눌림 확정은 poll()에서 한다.
// 게임과 메뉴는 takePressed()로 "이번에 새로 눌린 버튼"만 비트마스크로 받아 간다.
namespace Input {

volatile bool checkPending[NUM_SWITCHES] = {false};
volatile uint32_t lastChangeMs[NUM_SWITCHES] = {0};
bool held[NUM_SWITCHES] = {false};       // 디바운스까지 끝난 확정 상태
uint8_t pressedMask = 0;                 // 아직 아무도 가져가지 않은 눌림

void IRAM_ATTR onChange(void* arg) {
  uint8_t idx = (uint8_t)(uintptr_t)arg;
  lastChangeMs[idx] = millis();
  checkPending[idx] = true;
}

void begin() {
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    pinMode(SWITCH_PINS[i], INPUT_PULLUP);   // 눌리면 GND로 떨어지는 배선
  }
}

// 인터럽트는 다른 초기화가 모두 끝난 뒤에 붙인다(초기화 도중 오작동 방지).
void attach() {
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    attachInterruptArg(SWITCH_PINS[i], onChange, (void*)(uintptr_t)i, CHANGE);
  }
}

void poll(uint32_t now) {
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    if (!checkPending[i] || (now - lastChangeMs[i] < DEBOUNCE_MS)) continue;
    checkPending[i] = false;

    bool down = (digitalRead(SWITCH_PINS[i]) == LOW);
    if (down == held[i]) continue;     // 디바운스 후에도 실제로 바뀐 경우만
    held[i] = down;
    if (down) pressedMask |= (uint8_t)(1 << i);   // 누를 때만 이벤트
  }
}

// 이번에 새로 눌린 버튼 비트마스크. 읽으면 비워지므로 한 번만 소비된다.
uint8_t takePressed() {
  uint8_t m = pressedMask;
  pressedMask = 0;
  return m;
}

void flush() { pressedMask = 0; }

bool isHeld(uint8_t idx) { return held[idx]; }

}  // namespace Input

// ---------------------------------------------------------------------------
// LCD 공용 그리기
// ---------------------------------------------------------------------------
void printCentered(const char* text, uint8_t size, int16_t y) {
  display.setTextSize(size);
  int16_t w = (int16_t)strlen(text) * 6 * size;   // 기본 폰트 글자폭 6px
  int16_t x = (LCD_W - w) / 2;
  if (x < 0) x = 0;
  display.setCursor(x, y);
  display.print(text);
  display.setTextSize(1);
}

void drawBootScreen() {
  display.clearDisplay();
  int16_t logoX = (LCD_W - (int16_t)LOGO_1D_ARCADE_W) / 2;
  int16_t logoY = (LCD_H - (int16_t)LOGO_1D_ARCADE_H) / 2;
  drawLogo1DArcade(display, logoX, logoY);   // RLE 압축 로고(Logo1DArcade.h 주석 참고)
  display.display();
}

// 부팅 시 스위치 LED 4개를 각자 색으로 하나씩 켜 배선을 확인한다.
void bootAnimation() {
  for (uint8_t i = 0; i < NUM_SWITCH_LEDS; i++) {
    strip.clear();
    strip.setPixelColor(i, btnColor(i));
    safeShow();
    delay(150);
  }
  strip.clear();
  safeShow();
}

// 메뉴에서 늘 켜 두는 스위치 LED. 각 버튼의 역할 색을 그대로 보여준다.
void drawMenuSwitchLeds() {
  for (uint8_t i = 0; i < NUM_SWITCH_LEDS; i++) {
    strip.setPixelColor(i, scaleColor(btnColor(i), 0.35f));
  }
}
