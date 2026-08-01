/*
  Snake Invader(지렁이 슈팅) 게임 로직 테스트 스케치.
  Game_Logics.md / games.md의 Snake Invader 규칙을 실제 하드웨어에서 돌려보기 위한
  용도.
  
  LED 구성: 스위치 1~4에 내장된 WS281x 4개 + 그 뒤로 이어붙인 WS2815 라인 LED 172개가
  전기적으로 한 줄(GPIO8)로 연결되어 있어 총 176개다.
    - index 0~3  : 스위치 1~4에 내장된 LED (버튼 색 표시용)
    - index 4~175: 실제 게임 필드(지렁이/총알이 움직이는 172칸)
  필드 인덱스가 클수록(175에 가까울수록) 스위치에서 먼 "지렁이 출현 쪽" 끝이고,
  작을수록(4에 가까울수록) "플레이어 쪽" 끝이다. 지렁이는 큰 인덱스에서 출현해
  작은 인덱스(플레이어) 방향으로 전진하고, 총알은 그 반대 방향으로 발사된다.

  색상 규칙:
    스위치 색은 LED 위치 순서대로 0~3 = Red / Green / Blue / White로 고정이다.
    - 레벨 1~9  : 흰색 스위치는 LED가 꺼지고 눌러도 발사되지 않으며, 지렁이에도 흰색이
                  나오지 않는다. 실질적으로 R/G/B 3색 게임.
    - 레벨 10   : 흰색 해금. 필드 반대편 끝에서 흰색이 플레이어 쪽으로 쭉 채워지는 연출 후
                  White 스위치가 5회 점멸하고 시작하며, 이때부터 지렁이에 흰색이 섞인다.
    - 레벨 20+  : 매 레벨 시작 전 4개 스위치의 색(R/G/B/W)이 매번 무작위로 재배정되고,
                  새 색으로 스위치 4개가 동시에 3회 점멸한 뒤 시작(레벨 10 이전까지 쓰던
                  "빨강 빨강 빨강 초록" 카운트다운을 대신함).
  모든 레벨(1레벨 포함) 시작 전에는 위 특수 연출이 없는 한 기본값으로 필드 전체가
  빨강 1초, 빨강 1초, 빨강 1초, 초록 1초 순서로 점등된 뒤 지렁이가 출현한다.

  총알 색 불일치 페널티는 스펙에 "가속 또는 꼬리 증가" 둘 다 허용되어 있는데, 배열
  크기를 고정길이로 단순하게 유지하려고 가속 쪽을 선택했다.

  ---------------------------------------------------------------------------
  진행 순서 (ST7735 LCD)
  ---------------------------------------------------------------------------
    부팅 로고(3초) → 스위치 LED 점검 → READY? → 아무 스위치나 누름 → 카운트다운 → 게임
  게임 중에는 LCD에 현재 레벨과 점수가 뜬다. 게임오버가 나면 다시 READY?로 돌아온다.
  READY?에서 아무도 누르지 않고 READY_RAINBOW_MS가 지나면 필드에 무지개가 흐른다(어트랙트 모드).

  LCD 갱신 비용: 한 장을 통째로 밀어넣는 데 약 14ms가 걸린다. 게임 루프는 매 반복마다
  strip.show()(약 5.3ms)를 부르므로, LCD는 레벨이나 점수가 실제로 바뀐 순간에만 다시 그린다.

  ---------------------------------------------------------------------------
  소리
  ---------------------------------------------------------------------------
    상황                      소리
    -----------------------   ------------
    카운트다운 3 / 2 / 1      blip01
    총알 발사                 laser
    명중 - 색이 맞음          explosion01
    명중 - 색이 틀림          blip02
    지렁이가 한 칸 전진       pickupCoin01
    레벨 클리어               powerUp
    레벨 실패(게임오버)       explosion02

  사운드 엔진은 core 0의 별도 태스크에서 돌기 때문에, 이 스케치가 연출 중에 delay()로
  멈춰 있어도 재생은 그대로 이어진다.
*/

#include <Adafruit_NeoPixel.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>

#include "SoundEngine.h"
#include "Logo1DArcade.h"

// 사운드 헤더는 PCM 배열 정의를 담고 있어서 이 .ino 한 곳에서만 include한다(SoundEngine.h 주석 참고).
#include "sounds/blip01.h"
#include "sounds/laser.h"
#include "sounds/explosion01.h"
#include "sounds/explosion02.h"
#include "sounds/blip02.h"
#include "sounds/pickupCoin01.h"
#include "sounds/powerUp.h"

#define LED_PIN        8    // WS2815 데이터 핀(readme.md 배선 기준, 프로토콜은 WS2812 호환)
// 색 순서는 RGB다. 데이터시트상 WS2815는 GRB지만 실제로 들어온 스트립은 R과 G가 반대로 나와서
// (Color(255,0,0)이 초록으로 점등) NEO_RGB로 맞췄다. 스트립을 교체하면 여기부터 확인할 것.
#define LED_TYPE   (NEO_RGB + NEO_KHZ800)  // 800KHz 1-wire 프로토콜
#define NUM_LEDS_TOTAL 176  // 스위치 내장 LED 4개 + 라인 LED 172개  60개/1미터짜리를 3미터 구입후, 4개를 스위치용으로 분리하여 사용
#define NUM_SWITCH_LEDS 4   // index 0~3: 스위치 1~4 내장 LED
#define FIELD_START    NUM_SWITCH_LEDS         // 필드 첫 칸(플레이어 쪽 끝) = 4
#define FIELD_END      (NUM_LEDS_TOTAL - 1)    // 필드 마지막 칸(지렁이 출현 쪽 끝) = 175

// ST7735 TFT LCD 핀 (RS=DC, SDA=MOSI, CLK=SCLK)
#define LCD_DC   9   // ST7735의 RS 핀 (Data/Command 선택)
#define LCD_CS   10  // LCD SPI Chip Select (FSPI 하드웨어 기본 CS0)
#define LCD_RST  14  // LCD 하드웨어 리셋 핀
#define LCD_SDA  11  // = MOSI (FSPI 하드웨어 기본 핀)
#define LCD_CLK  12  // = SCLK (FSPI 하드웨어 기본 핀)

#define LCD_W        160
#define LCD_H        128
#define LCD_ROTATION 3          // 0,2 = 세로(128x160) / 1,3 = 가로(160x128)
#define LCD_SPI_HZ   24000000   // 40MHz는 화면 아래쪽이 깨진다(01test_ST7735 주석 참고)

// 라이브러리에 회색 상수가 없어 직접 정의(RGB565 50% 회색). 보조 문구용
#define LCD_GREY     0x7BEF

#define I2S_BCLK_PIN  42
#define I2S_LRC_PIN   41
#define I2S_DOUT_PIN  40

#define BOOT_LOGO_MS   3000  // 부팅 로고를 보여주는 시간

// READY?에서 아무도 누르지 않고 이 시간이 지나면 필드에 무지개가 흐른다(어트랙트 모드).
#define READY_RAINBOW_MS   5000
#define RAINBOW_PERIOD_MS  3000   // 무지개가 필드를 한 바퀴 흐르는 데 걸리는 시간
#define RAINBOW_FRAME_MS     20   // 무지개 갱신 주기(약 50FPS)
#define RAINBOW_SCALE      0.25f  // 필드 전체가 켜지므로 전류를 감안해 낮춰서 표시한다

#define DEBOUNCE_MS    30   // 스위치 채터링 무시 시간
#define FLASH_MS       120  // 버튼을 눌렀을 때 해당 스위치 LED를 밝게 켜 두는 시간(시각 피드백)
#define BRIGHTNESS     255   // 176개라 전류/눈부심 부담이 커서 낮게 고정

// ---- 난이도 변수 (Game_Logics.md: "길이/속도는 디버깅용 변수로 조정 가능") ----
#define SNAKE_BASE_LEN        5     // 레벨 1 지렁이 길이(칸 수)
#define SNAKE_LEN_PER_LEVEL   3     // 레벨업마다 늘어나는 길이
#define MAX_SNAKE_LEN         100   // 배열 크기 상한(레벨 20대까지 감안해 여유 확보)
#define SNAKE_BASE_SPEED      6.0f  // 레벨 1 전진 속도(필드 칸/초) - 기존 12.0f의 50%
#define SNAKE_SPEED_PER_LEVEL 1.0f  // 레벨업마다 늘어나는 속도 - 기존 2.0f의 50%
#define SNAKE_MISS_ACCEL      1.15f // 총알 색 불일치 시 가속 배율(스펙의 "가속" 선택지)
#define SNAKE_MAX_SPEED       60.0f // 무한 가속 방지용 상한
#define BULLET_SPEED          150.0f // 총알 속도(칸/초)
#define MAX_BULLETS            8    // 동시 총알 개수 상한

#define WHITE_UNLOCK_LEVEL      10  // 흰색 연출 레벨(색상 해금과는 무관)
#define RGBW_RANDOM_LEVEL       20  // 이 레벨부터 매 레벨 스위치 색 랜덤 재배정

#define COUNTDOWN_STEP_MS       1000 // 기본 "빨강*3 + 초록" 카운트다운 한 스텝당 유지 시간
#define WHITE_SWEEP_STEP_MS     12   // 흰색 해금 연출: 필드 한 칸 채우는 데 걸리는 시간(172칸 전체 약 2.1초)
#define SWITCH_UNLOCK_BLINK_MS  150  // 흰색 해금 후 스위치3 점멸 on/off 각각의 시간
#define SWITCH_UNLOCK_BLINK_TIMES 5  // 흰색 해금 후 스위치3 점멸 횟수
#define RGBW_BLINK_MS            150 // 레벨20+ 색 재배정 후 스위치 4개 동시 점멸 on/off 시간
#define RGBW_BLINK_TIMES         3   // 레벨20+ 색 재배정 후 점멸 횟수

#define SWITCH_LED_BOOST         1.5f // 스위치 LED는 필드보다 눈에 잘 띄어야 해서 기본 밝기의 1.5배로 표시

// ---- 사운드 ----
// soundPlay()의 인자가 곧 아래 배열의 인덱스다. 이름은 "언제 나는가" 기준으로 붙였다.
enum SfxId : uint8_t {
  SFX_COUNTDOWN = 0,  // 시작 카운트다운 3 / 2 / 1
  SFX_SHOOT,          // 총알 발사
  SFX_HIT,            // 명중 - 색이 맞음
  SFX_MISS,           // 명중 - 색이 틀림
  SFX_SNAKE_STEP,     // 지렁이가 한 칸 전진
  SFX_LEVEL_CLEAR,    // 레벨 클리어
  SFX_LEVEL_FAIL,     // 레벨 실패(게임오버)
  NUM_SFX
};

static const SoundClip* const GAME_SOUNDS[NUM_SFX] = {
  &CLIP_blip01,        // SFX_COUNTDOWN
  &CLIP_laser,         // SFX_SHOOT
  &CLIP_explosion01,   // SFX_HIT
  &CLIP_blip02,        // SFX_MISS
  &CLIP_pickupCoin01,  // SFX_SNAKE_STEP
  &CLIP_powerUp,       // SFX_LEVEL_CLEAR
  &CLIP_explosion02    // SFX_LEVEL_FAIL
};

// 인덱스 = LED 위치(0~3 = Red/Green/Blue/White). 그 자리의 스위치가 물려 있는 GPIO를 적는다.
// 3번(Blue)과 4번(White) 자리의 배선이 GPIO 번호 순서와 반대라, 실물 위치에 맞춰 17과 16을 바꿔 넣었다.
// 이 순서가 틀어지면 파란 LED 자리를 눌렀는데 흰색이 발사되는 식으로 어긋난다.
const uint8_t SWITCH_PINS[] = {7, 15, 17, 16};
const uint8_t NUM_SWITCHES = sizeof(SWITCH_PINS) / sizeof(SWITCH_PINS[0]);

Adafruit_NeoPixel strip(NUM_LEDS_TOTAL, LED_PIN, LED_TYPE);

// 하드웨어 SPI 생성자 인자 순서: (CS, DC, RST)
Adafruit_ST7735 tft = Adafruit_ST7735(LCD_CS, LCD_DC, LCD_RST);

// ST7735는 프레임버퍼를 들고 있지 않아서 화면에 직접 그리면 깜빡인다. 그래서 화면과 같은 크기의
// 캔버스에 그린 뒤 통째로 전송한다. clearDisplay()로 지우고 display()로 한 번에 내보내면 된다.
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

// 원색(풀 밝기) - 스위치 기본색/스네이크 세그먼트/총알에 사용
const uint32_t COLOR_RED   = strip.Color(255, 0, 0);
const uint32_t COLOR_GREEN = strip.Color(0, 255, 0);
const uint32_t COLOR_BLUE  = strip.Color(0, 0, 255);
const uint32_t COLOR_WHITE = strip.Color(255, 255, 255);

// 매 레벨 시작 전 기본 카운트다운("빨강*3 + 초록")에 쓰는 살짝 죽인 색
const uint32_t COUNTDOWN_RED   = strip.Color(80, 0, 0);
const uint32_t COUNTDOWN_GREEN = strip.Color(0, 80, 0);

// 웨이브클리어/게임오버 이벤트 피드백 색.
// 예전에는 두 색을 서로 바꿔 넣어 두었는데, 그건 스트립 색 순서(NEO_RGB) 문제를 여기서
// 보정하고 있던 것이라 LED_TYPE을 고치면서 값을 제자리로 되돌렸다.
const uint32_t WAVE_CLEAR_FLASH_COLOR = strip.Color(0, 60, 0);   // 초록
const uint32_t GAME_OVER_FLASH_COLOR  = strip.Color(60, 0, 0);   // 빨강

// ---- 스위치 입력 (기존 테스트 코드와 동일한 ISR + 디바운스 패턴) ----
volatile bool switchPressed[NUM_SWITCHES] = {false};
volatile bool checkPending[NUM_SWITCHES] = {false};
volatile uint32_t lastChangeMs[NUM_SWITCHES] = {0};

bool switchFlashOn[NUM_SWITCHES] = {false};
uint32_t switchFlashOffAt[NUM_SWITCHES] = {0};

void IRAM_ATTR handleSwitchInterrupt(void* arg) {
  uint8_t idx = (uint8_t)(uintptr_t)arg;
  lastChangeMs[idx] = millis();
  checkPending[idx] = true;  // 실제 레벨 판정/디바운스는 loop()에서
}

// ---- 게임 상태 ----
uint16_t level = 1;
uint32_t score = 0;

uint32_t snakeColors[MAX_SNAKE_LEN];  // index 0 = 선두(헤드, 플레이어에 가장 가까운 세그먼트)
uint8_t snakeCount = 0;
float headPos = 0;      // 헤드의 필드 내 위치(float, FIELD_START~FIELD_END 범위)
float snakeSpeed = SNAKE_BASE_SPEED;
long lastHeadCell = 0;  // 전진 소리를 한 칸에 한 번만 내기 위해 기억해 두는 직전 칸

// 스위치 0~3(=버튼1~4)이 현재 어떤 색인지, 그리고 사용 가능(해금)한지.
// 레벨 20부터는 이 배열 자체가 매 레벨 무작위로 재배정된다.
uint32_t currentSwitchColor[NUM_SWITCH_LEDS];
bool switchUnlocked[NUM_SWITCH_LEDS];

struct Bullet {
  bool active;
  float pos;
  uint32_t color;
};
Bullet bullets[MAX_BULLETS];

uint32_t lastFrameMs = 0;

// ---- LCD 화면 ----
// 게임 화면에 마지막으로 그려 넣은 값. 이 값과 달라졌을 때만 다시 전송한다(위 주석의 14ms 참고).
uint16_t lcdShownLevel = 0;
uint32_t lcdShownScore = 0;

void printCentered(const char* text, uint8_t size, int16_t y) {
  display.setTextSize(size);
  int16_t w = (int16_t)strlen(text) * 6 * size;   // 기본 폰트 글자폭 6px
  int16_t x = (LCD_W - w) / 2;
  if (x < 0) x = 0;
  display.setCursor(x, y);
  display.print(text);
  display.setTextSize(1);
}

// 부팅 화면은 로고만 표시한다.
void drawBootScreen() {
  display.clearDisplay();
  int16_t logoX = (LCD_W - (int16_t)LOGO_1D_ARCADE_W) / 2;
  int16_t logoY = (LCD_H - (int16_t)LOGO_1D_ARCADE_H) / 2;
  drawLogo1DArcade(display, logoX, logoY);   // RLE 압축 로고(Logo1DArcade.h 주석 참고)
  display.display();
}

// 스위치 입력을 기다리는 화면.
void drawReadyScreen() {
  display.clearDisplay();
  display.setTextColor(ST77XX_GREEN);
  printCentered("SNAKE", 3, 4);
  printCentered("INVADER", 3, 30);
  display.setTextColor(ST77XX_WHITE);
  printCentered("READY?", 4, 62);
  display.setTextColor(LCD_GREY);
  printCentered("PRESS ANY BUTTON", 1, 112);
  display.display();
}

// 카운트다운 한 칸(3 / 2 / 1 / GO). 멀리서도 보이게 화면을 거의 채운다.
void drawCountdownScreen(const char* text, bool go) {
  display.clearDisplay();
  display.setTextColor(go ? ST77XX_GREEN : ST77XX_RED);
  if (go) printCentered(text, 8, 32);   // 크기 8 = 48x64px
  else    printCentered(text, 12, 16);  // 크기 12 = 72x96px
  display.display();
}

// 레벨 연출/게임오버처럼 잠깐 띄우는 두 줄짜리 안내.
void drawMessageScreen(const char* line1, const char* line2, uint16_t color) {
  display.clearDisplay();
  display.setTextColor(color);
  printCentered(line1, 3, 40);
  if (line2 != nullptr) printCentered(line2, 3, 70);
  display.display();
}

// 게임 중 화면: 현재 레벨을 크게, 점수를 아래에.
void drawGameScreen() {
  display.clearDisplay();

  display.setTextColor(ST77XX_CYAN);
  printCentered("LEVEL", 2, 8);

  char buf[8];
  snprintf(buf, sizeof(buf), "%u", level);
  display.setTextColor(ST77XX_WHITE);
  printCentered(buf, 8, 32);   // 크기 8 = 48x64px, 세 자리까지 화면에 들어간다

  display.setTextSize(2);
  display.setTextColor(LCD_GREY);
  display.setCursor(0, 108);
  display.print("SCORE ");
  display.print(score);
  display.display();

  lcdShownLevel = level;
  lcdShownScore = score;
}

// 기본 색 배정(요청 반영): 스위치 인덱스 0~3 = Red, Green, Blue, White 고정.
void applyDefaultColors() {
  currentSwitchColor[0] = COLOR_RED;
  currentSwitchColor[1] = COLOR_GREEN;
  currentSwitchColor[2] = COLOR_BLUE;
  currentSwitchColor[3] = COLOR_WHITE;
  switchUnlocked[0] = true;
  switchUnlocked[1] = true;
  switchUnlocked[2] = true;
  // 흰색은 지렁이에 등장하기 시작하는 레벨부터 열린다. 그 전에는 LED가 꺼지고 눌러도 발사되지 않는다.
  switchUnlocked[3] = (level >= WHITE_UNLOCK_LEVEL);
}

// 레벨 20 이후: 4개 스위치에 R/G/B/W를 겹치지 않게 무작위로 재배정(Fisher-Yates 셔플).
void randomizeAllSwitchColorsRGBW() {
  uint32_t pool[4] = {COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_WHITE};
  for (int8_t i = 3; i > 0; i--) {
    int8_t j = random(i + 1);
    uint32_t tmp = pool[i];
    pool[i] = pool[j];
    pool[j] = tmp;
  }
  for (uint8_t i = 0; i < NUM_SWITCH_LEDS; i++) {
    currentSwitchColor[i] = pool[i];
    switchUnlocked[i] = true;
  }
}

// 스위치 4개를 동시에 현재 색으로 점멸(레벨20+ 색 재배정 알림용).
void blinkAllSwitchesCurrent(uint8_t times, uint16_t onMs) {
  drawMessageScreen("COLOR", "SHUFFLE", ST77XX_YELLOW);
  for (uint8_t b = 0; b < times; b++) {
    for (uint8_t i = 0; i < NUM_SWITCH_LEDS; i++) strip.setPixelColor(i, currentSwitchColor[i]);
    strip.show();
    delay(onMs);
    for (uint8_t i = 0; i < NUM_SWITCH_LEDS; i++) strip.setPixelColor(i, 0);
    strip.show();
    delay(onMs);
  }
}

// 기본 웨이브 시작 전 카운트다운: 필드 전체가 빨강-빨강-빨강-초록 순으로 1초씩 유지되고,
// LCD에는 같은 박자로 3 / 2 / 1 / GO가 뜬다.
void standardCountdown() {
  const uint32_t sequence[4] = {COUNTDOWN_RED, COUNTDOWN_RED, COUNTDOWN_RED, COUNTDOWN_GREEN};
  const char* labels[4] = {"3", "2", "1", "GO"};
  for (uint8_t s = 0; s < 4; s++) {
    for (uint16_t i = FIELD_START; i <= FIELD_END; i++) strip.setPixelColor(i, sequence[s]);
    strip.show();
    drawCountdownScreen(labels[s], s == 3);
    if (s < 3) soundPlay(SFX_COUNTDOWN);   // 숫자가 뜨는 3 / 2 / 1 에만(GO는 무음)
    delay(COUNTDOWN_STEP_MS);
  }
  for (uint16_t i = FIELD_START; i <= FIELD_END; i++) strip.setPixelColor(i, 0);
  strip.show();
}

// 레벨 10 진입 시 1회성 연출: 필드 반대편(먼 쪽, FIELD_END)에서 플레이어 쪽으로
// 흰색이 쭉 채워지고, 다 채워지면 White 스위치(index 3)가 5회 점멸한다.
void whiteUnlockSweepAndBlink() {
  drawMessageScreen("WHITE", "UNLOCKED", ST77XX_WHITE);
  for (int16_t i = FIELD_END; i >= (int16_t)FIELD_START; i--) {
    strip.setPixelColor(i, COLOR_WHITE);
    strip.show();
    delay(WHITE_SWEEP_STEP_MS);
  }

  for (uint8_t b = 0; b < SWITCH_UNLOCK_BLINK_TIMES; b++) {
    strip.setPixelColor(3, COLOR_WHITE);
    strip.show();
    delay(SWITCH_UNLOCK_BLINK_MS);
    strip.setPixelColor(3, 0);
    strip.show();
    delay(SWITCH_UNLOCK_BLINK_MS);
  }

  for (uint16_t i = FIELD_START; i <= FIELD_END; i++) strip.setPixelColor(i, 0);  // 다음 웨이브 스폰 전 필드 정리
  strip.show();
}

// 해금된 스위치 색 중 하나를 무작위로 골라 지렁이 세그먼트 색으로 사용한다.
// 해금 여부는 applyDefaultColors()가 레벨을 보고 정한다. 흰색이 잠긴 레벨에서는
// 발사도 안 되고 지렁이에도 나오지 않아, 잠금 하나로 두 규칙이 같이 지켜진다.
uint32_t randomSnakeColor() {
  uint8_t candidates[NUM_SWITCH_LEDS];
  uint8_t n = 0;
  for (uint8_t i = 0; i < NUM_SWITCH_LEDS; i++) {
    if (switchUnlocked[i]) candidates[n++] = i;
  }
  if (n == 0) return currentSwitchColor[0];   // 후보가 하나도 없는 상황 방지(random(0) 회피)
  return currentSwitchColor[candidates[random(n)]];
}

void clearBullets() {
  for (uint8_t i = 0; i < MAX_BULLETS; i++) bullets[i].active = false;
}

// 새 웨이브를 스폰한다: 길이/속도를 레벨에 맞게 정하고, 지렁이가 필드 끝(FIELD_END)에
// 꼬리를 딱 맞춰 등장하도록 헤드 위치를 계산한다(스폰 시점에 필드 밖으로 나가지 않게).
void spawnWave() {
  uint16_t len = SNAKE_BASE_LEN + (level - 1) * SNAKE_LEN_PER_LEVEL;
  if (len > MAX_SNAKE_LEN) len = MAX_SNAKE_LEN;
  snakeCount = len;

  for (uint8_t i = 0; i < snakeCount; i++) {
    snakeColors[i] = randomSnakeColor();
  }

  snakeSpeed = SNAKE_BASE_SPEED + (level - 1) * SNAKE_SPEED_PER_LEVEL;
  if (snakeSpeed > SNAKE_MAX_SPEED) snakeSpeed = SNAKE_MAX_SPEED;

  headPos = (float)(FIELD_END - (snakeCount - 1));  // 꼬리(마지막 세그먼트)가 정확히 FIELD_END에 위치
  lastHeadCell = lround(headPos);                   // 스폰 직후 전진음이 울리지 않도록 맞춰 둔다
}

// 이번 레벨에 맞는 시작 연출(기본 카운트다운 / 흰색 해금 / RGBW 재배정)을 먼저 재생한 뒤 스폰한다.
// 매 레벨(레벨1 포함) 진입 시 항상 이 함수를 거치도록 한다.
void beginLevel() {
  // 이전 레벨에서 날아가던 총알을 정리한다. 남겨 두면 새 웨이브가 스폰되자마자
  // 지난 레벨의 총알이 지렁이에 꽂힌다.
  clearBullets();
  applyDefaultColors();

  if (level == WHITE_UNLOCK_LEVEL) {
    whiteUnlockSweepAndBlink();
  } else if (level >= RGBW_RANDOM_LEVEL) {
    randomizeAllSwitchColorsRGBW();
    blinkAllSwitchesCurrent(RGBW_BLINK_TIMES, RGBW_BLINK_MS);
  } else {
    standardCountdown();
  }

  drawGameScreen();   // 연출이 끝났으니 LCD를 게임 화면으로 되돌린다
  spawnWave();
}

void fireBullet(uint32_t color) {
  for (uint8_t i = 0; i < MAX_BULLETS; i++) {
    if (!bullets[i].active) {
      bullets[i].active = true;
      bullets[i].pos = (float)FIELD_START;
      bullets[i].color = color;
      soundPlay(SFX_SHOOT);   // 실제로 나간 총알에만 소리를 낸다
      return;
    }
  }
  // 슬롯이 다 찼으면 이번 발사 요청은 조용히 버림(총알 난사 방지용 자연스러운 상한)
}

// 헤드 세그먼트를 제거하고 다음 세그먼트를 새 헤드로 승격한다.
// 이 순간 시간은 흐르지 않았으므로 새 헤드의 위치는 "예전 헤드 위치 + 1"과 같다
// (세그먼트들은 항상 헤드 기준 +0,+1,+2... 오프셋으로 늘어서 있으므로).
// color의 각 채널에 factor를 곱하되 255를 넘지 않게 클램프한다(오버플로우 방지).
uint32_t scaleColorClamped(uint32_t color, float factor) {
  uint16_t r = (uint16_t)(((color >> 16) & 0xFF) * factor);
  uint16_t g = (uint16_t)(((color >> 8) & 0xFF) * factor);
  uint16_t b = (uint16_t)((color & 0xFF) * factor);
  if (r > 255) r = 255;
  if (g > 255) g = 255;
  if (b > 255) b = 255;
  return strip.Color((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

void removeSnakeHead() {
  headPos += 1.0f;
  // 지렁이가 전진한 게 아니라 머리가 사라져 좌표가 밀린 것이므로 전진음을 내지 않는다.
  lastHeadCell = lround(headPos);
  for (uint8_t i = 1; i < snakeCount; i++) {
    snakeColors[i - 1] = snakeColors[i];
  }
  snakeCount--;
}

// 필드 전체를 깜빡여 게임오버/웨이브클리어를 시각적으로 알린다(LCD가 없어서 LED로 대체).
void flashField(uint32_t color, uint8_t times, uint16_t onMs) {
  for (uint8_t t = 0; t < times; t++) {
    for (uint16_t i = FIELD_START; i <= FIELD_END; i++) strip.setPixelColor(i, color);
    strip.show();
    delay(onMs);
    for (uint16_t i = FIELD_START; i <= FIELD_END; i++) strip.setPixelColor(i, 0);
    strip.show();
    delay(onMs);
  }
}

// 어느 버튼이 살아 있는지 보여준다. 잠긴 스위치(초반 레벨의 흰색)는 꺼진 채로 둔다.
void showReadySwitchLeds() {
  for (uint8_t i = 0; i < NUM_SWITCH_LEDS; i++) {
    strip.setPixelColor(i, switchUnlocked[i] ? currentSwitchColor[i] : 0);
  }
}

// 대기가 길어질 때 흘리는 무지개(어트랙트 모드).
// 스위치 LED(0~3)까지 포함해 스트립 전체를 쓴다. 색상환을 스트립 길이에 한 바퀴 펼쳐 놓고,
// 시작 색을 시간에 따라 돌려 흐르는 것처럼 보이게 한다.
void drawReadyRainbow(uint32_t now) {
  uint16_t base = (uint16_t)(((now % RAINBOW_PERIOD_MS) * 65536UL) / RAINBOW_PERIOD_MS);
  for (uint16_t i = 0; i < NUM_LEDS_TOTAL; i++) {
    uint16_t hue = base + (uint16_t)((uint32_t)i * 65536UL / NUM_LEDS_TOTAL);
    strip.setPixelColor(i, scaleColorClamped(strip.gamma32(strip.ColorHSV(hue)), RAINBOW_SCALE));
  }
}

// READY? 화면을 띄우고 아무 스위치나 눌릴 때까지 기다린다.
// 이 스케치는 시작 연출을 전부 delay()로 처리하므로, 여기서도 같은 방식으로 막고 기다린다.
// READY_RAINBOW_MS 동안 아무 입력이 없으면 필드에 무지개를 흘려 둔다.
void waitForStartPress() {
  drawReadyScreen();

  strip.clear();
  showReadySwitchLeds();
  strip.show();

  // 게임오버 직전에 누른 흔적이 남아 있으면 그대로 통과해 버리므로 먼저 지운다.
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    checkPending[i] = false;
    switchPressed[i] = false;
    switchFlashOn[i] = false;
  }

  uint32_t enteredAt = millis();
  uint32_t lastRainbowMs = 0;

  for (;;) {
    for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
      if (digitalRead(SWITCH_PINS[i]) != LOW) continue;
      delay(DEBOUNCE_MS);
      if (digitalRead(SWITCH_PINS[i]) != LOW) continue;   // 채터링이었다

      // 손을 뗄 때까지 기다린다. 그러지 않으면 이 누름이 게임 시작 직후 총알 발사로 이어진다.
      while (digitalRead(SWITCH_PINS[i]) == LOW) delay(5);
      for (uint8_t k = 0; k < NUM_SWITCHES; k++) checkPending[k] = false;

      // 무지개가 스트립 전체에 남아 있으므로, 필드를 비우고 스위치는 제 색으로 되돌리고 나간다.
      strip.clear();
      showReadySwitchLeds();
      strip.show();
      return;
    }

    uint32_t now = millis();
    if (now - enteredAt >= READY_RAINBOW_MS && now - lastRainbowMs >= RAINBOW_FRAME_MS) {
      lastRainbowMs = now;
      drawReadyRainbow(now);   // 스위치 LED까지 무지개에 포함된다
      strip.show();
    }
    delay(5);
  }
}

void resetGame() {
  level = 1;
  score = 0;
  clearBullets();
  applyDefaultColors();   // READY? 화면에서 스위치 색을 미리 보여주기 위해 먼저 정해 둔다
  waitForStartPress();
  beginLevel();
}

// 전원 켜졌을 때 스위치 LED 4개를 순서대로 켜 하드웨어가 정상인지 눈으로 확인하는 데모.
// 게임의 색 해금 상태와 무관하게 항상 4개 전부(Red/Green/Blue/White) 점검한다.
void bootAnimation() {
  strip.clear();
  strip.show();

  const uint32_t demoColors[NUM_SWITCH_LEDS] = {COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_WHITE};
  for (uint8_t i = 0; i < NUM_SWITCH_LEDS; i++) {
    strip.setPixelColor(i, demoColors[i]);
    strip.show();
    delay(200);
  }
  delay(300);

  strip.clear();
  strip.show();
}

void setup() {
  randomSeed(esp_random());

  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    pinMode(SWITCH_PINS[i], INPUT_PULLUP);  // 눌리면 GND로 떨어지는 배선이라 풀업
  }

  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  strip.clear();
  strip.show();

  display.begin();
  soundEngineBegin(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN, GAME_SOUNDS, NUM_SFX);

  bootAnimation();  // 인터럽트 걸기 전에 먼저 실행(초기화 도중 오작동 방지)

  drawBootScreen();
  delay(BOOT_LOGO_MS);

  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    // FALLING만 사용해 릴리즈 엣지/노이즈에 의한 ISR 과다 발생을 줄인다.
    attachInterruptArg(SWITCH_PINS[i], handleSwitchInterrupt, (void*)(uintptr_t)i, FALLING);
  }

  resetGame();   // READY? 대기 → 카운트다운 → 첫 웨이브 스폰

  lastFrameMs = millis();
}

void loop() {
  uint32_t now = millis();
  float dt = (now - lastFrameMs) / 1000.0f;
  if (dt > 0.05f) dt = 0.05f;  // beginLevel()의 delay() 등으로 프레임이 크게 벌어졌을 때 한 번에 확 튀는 것 방지
  lastFrameMs = now;

  // ---- 스위치 입력: 디바운스 후 확정된 눌림만 발사로 처리(잠긴 스위치는 무시) ----
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    if (checkPending[i]) {
      bool pressedNow = (digitalRead(SWITCH_PINS[i]) == LOW);
      if (pressedNow && switchUnlocked[i] && !switchPressed[i] && !switchFlashOn[i]) {
        switchFlashOn[i] = true;
        switchFlashOffAt[i] = now + FLASH_MS;
      }
    }

    if (checkPending[i] && (now - lastChangeMs[i] >= DEBOUNCE_MS)) {
      checkPending[i] = false;

      bool pressed = (digitalRead(SWITCH_PINS[i]) == LOW);
      if (pressed != switchPressed[i]) {
        switchPressed[i] = pressed;
        if (pressed && switchUnlocked[i]) {
          fireBullet(currentSwitchColor[i]);
          switchFlashOn[i] = true;
          switchFlashOffAt[i] = now + FLASH_MS;
        }
      }
    }
  }

  // ---- 총알 전진 ----
  for (uint8_t i = 0; i < MAX_BULLETS; i++) {
    if (!bullets[i].active) continue;
    bullets[i].pos += BULLET_SPEED * dt;
    if (bullets[i].pos > FIELD_END) {
      bullets[i].active = false;  // 지렁이를 못 맞추고 필드를 완전히 지나감(예: 웨이브 클리어 직후)
    }
  }

  // ---- 지렁이 전진 ----
  if (snakeCount > 0) {
    headPos -= snakeSpeed * dt;

    // 칸이 실제로 바뀐 순간에만 전진음을 낸다(속도와 무관하게 한 칸당 한 번).
    long cell = lround(headPos);
    if (cell != lastHeadCell) {
      lastHeadCell = cell;
      soundPlay(SFX_SNAKE_STEP);
    }
  }

  // ---- 충돌 판정: 총알이 지렁이 "선두"에 닿았는지만 검사(스펙 그대로) ----
  if (snakeCount > 0) {
    for (uint8_t i = 0; i < MAX_BULLETS; i++) {
      if (!bullets[i].active) continue;
      if (bullets[i].pos >= headPos) {
        bullets[i].active = false;
        if (bullets[i].color == snakeColors[0]) {
          soundPlay(SFX_HIT);
          score += 10;
          removeSnakeHead();
          if (snakeCount == 0) break;  // 이번 프레임에 더 처리할 세그먼트 없음
        } else {
          soundPlay(SFX_MISS);
          snakeSpeed *= SNAKE_MISS_ACCEL;
          if (snakeSpeed > SNAKE_MAX_SPEED) snakeSpeed = SNAKE_MAX_SPEED;
        }
      }
    }
  }

  // ---- 종료 판정 ----
  if (snakeCount == 0) {
    soundPlay(SFX_LEVEL_CLEAR);
    flashField(WAVE_CLEAR_FLASH_COLOR, 2, 150);  // 웨이브 클리어 피드백
    level++;
    beginLevel();  // 다음 레벨의 시작 연출(카운트다운/해금/색 재배정) 후 스폰
  } else if (headPos <= (float)FIELD_START) {
    soundPlay(SFX_LEVEL_FAIL);
    drawMessageScreen("GAME", "OVER", ST77XX_RED);
    flashField(GAME_OVER_FLASH_COLOR, 3, 200);  // 게임오버 피드백
    resetGame();                                // 다시 READY?부터
  }

  // ---- LCD: 레벨/점수가 실제로 바뀐 순간에만 다시 그린다(한 장 전송에 약 14ms) ----
  if (level != lcdShownLevel || score != lcdShownScore) drawGameScreen();

  // ---- 렌더링 ----
  for (uint16_t i = FIELD_START; i <= FIELD_END; i++) strip.setPixelColor(i, 0);

  for (uint8_t s = 0; s < snakeCount; s++) {
    long idx = lround(headPos) + s;
    if (idx >= FIELD_START && idx <= FIELD_END) {
      strip.setPixelColor(idx, snakeColors[s]);
    }
  }

  for (uint8_t i = 0; i < MAX_BULLETS; i++) {
    if (!bullets[i].active) continue;
    long idx = lround(bullets[i].pos);
    if (idx >= FIELD_START && idx <= FIELD_END) {
      strip.setPixelColor(idx, bullets[i].color);
    }
  }

  for (uint8_t i = 0; i < NUM_SWITCH_LEDS; i++) {
    if (switchFlashOn[i] && now >= switchFlashOffAt[i]) {
      switchFlashOn[i] = false;
    }

    if (!switchUnlocked[i]) {
      strip.setPixelColor(i, 0);  // 잠긴 스위치는 소등
      continue;
    }

    // 평소엔 1/3 밝기로 버튼 색을 표시하고, 누른 직후 FLASH_MS 동안만 원색으로 밝게 표시.
    // 스위치는 필드보다 눈에 잘 띄어야 하므로 최종적으로 SWITCH_LED_BOOST(1.5배)를 추가로 곱한다.
    uint32_t c = currentSwitchColor[i];
    if (!switchFlashOn[i]) {
      uint8_t r = (uint8_t)(((c >> 16) & 0xFF) / 3);
      uint8_t g = (uint8_t)(((c >> 8) & 0xFF) / 3);
      uint8_t b = (uint8_t)((c & 0xFF) / 3);
      c = strip.Color(r, g, b);
    }
    strip.setPixelColor(i, scaleColorClamped(c, SWITCH_LED_BOOST));
  }

  strip.show();
}
