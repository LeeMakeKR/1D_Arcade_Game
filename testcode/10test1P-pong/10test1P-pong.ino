/*
  ===========================================================================
  10 - 1P Pong (Solo, vs MCU)
  ===========================================================================
  09test2P-pong의 공 물리/존/연출을 그대로 쓰되, 반대편 사람을 MCU가 대신하는 1인용이다.
  보드 한 대로 끝나므로 ESP-NOW도 역할 토글도 없다.

  ---------------------------------------------------------------------------
  코트
  ---------------------------------------------------------------------------
  스트립 176개 중 라인 LED 172개(인덱스 4 ~ 175)가 코트다.
  인덱스가 작은 쪽이 플레이어, 큰 쪽이 CPU.
  각자 끝에서부터 빨강 3 / 노랑 6 / 초록 9, 총 18칸이 타격 영역이다.

  ---------------------------------------------------------------------------
  규칙
  ---------------------------------------------------------------------------
  [반사]
    공이 자기 영역 안에 있을 때 1~4번 아무 스위치나 누르면 반대편으로 반사된다.
      - 초록에서 반사 : 온 속도 그대로
      - 노랑에서 반사 : 약간 빠르게 (REFLECT_GAIN_YELLOW)
      - 빨강에서 반사 : 최저 REFLECT_MIN_RED로 나가고, 그보다 빠른 공이 왔으면 10% 가속
    즉 랠리를 가속하는 것은 플레이어의 타이밍뿐이다. CPU의 리턴은 속도를 바꾸지 않는다
    (games.md 2.2: 정확도에 따른 가속은 플레이어 측 판정으로만 정의되어 있다).

  [실점]
    공을 놓치면 상대가 1점. 코트 전체가 반으로 갈려 얻은 쪽 초록 / 잃은 쪽 빨강으로
    FLASH_MS 동안 점등된 뒤 다시 서브한다. 서브 폴트 규칙은 없다.

  [서브]
    서브는 항상 코트 중앙에서 느린 속도로 플레이어 쪽을 향해 출발한다. 득점 상황과
    무관하게 방향이 고정이다(1인용이라 서브권을 주고받을 상대가 없다).

  [레벨]
    플레이어가 LEVEL_UP_POINTS점을 먼저 내면 레벨 클리어 → 레벨이 오르고 점수는 0-0으로
    초기화된다. 레벨이 오를수록 CPU의 반응이 빨라지고 정확해진다.
    CPU가 CPU_MATCH_POINTS점을 먼저 내면 게임 오버.

  ---------------------------------------------------------------------------
  CPU가 공을 치는 방식
  ---------------------------------------------------------------------------
  CPU는 "공이 내 영역에 들어온 순간"에 언제 스윙할지를 한 번 정하고, 그 시각이 되면
  스윙한다. 그때 공이 아직 영역 안에 있으면 반사되고, 이미 지나갔으면 놓친 것이 된다.

  그래서 미스는 따로 굴리는 주사위가 아니라 느린 반응의 결과다. 공이 빠를수록 영역을
  지나가는 시간이 짧아져 같은 반응 속도로도 더 자주 놓치게 되고, 이것이 랠리가 빨라질수록
  CPU도 흔들리는 자연스러운 곡선을 만든다.

    반응 지연  = CPU_REACT_BASE_MS  에서 레벨마다 CPU_REACT_STEP_MS  씩 감소 (하한 있음)
    흔들림(±)  = CPU_JITTER_BASE_MS 에서 레벨마다 CPU_JITTER_STEP_MS 씩 감소 (하한 있음)
    무반응 확률 = CPU_WHIFF_BASE_PCT 에서 레벨마다 CPU_WHIFF_STEP_PCT 씩 감소 (하한 있음)

  영역 18칸을 지나가는 데 걸리는 시간이 판단 기준이다.
    서브 속도 60칸/초 → 300ms / 100칸/초 → 180ms / 상한 220칸/초 → 82ms
  레벨 1의 반응 지연이 이 값에 가깝도록 잡아야 "초반에는 곧잘 놓치는" 상대가 된다.

  ---------------------------------------------------------------------------
  소리
  ---------------------------------------------------------------------------
    상황                   소리
    --------------------   ------------
    READY에서 스위치 누름  pickupCoin02
    카운트다운 3 / 2 / 1   blip01
    START                  blip02
    서브 출발              pickupCoin02
    초록 영역에서 반사     pickupCoin01
    노랑 영역에서 반사     pickupCoin02
    빨강 영역에서 반사     blip01
    CPU가 받아침           laser
    플레이어가 놓침        explosion02
    CPU가 놓침(득점)       pickupCoin02
    레벨 클리어            fanfare
    게임 오버              explosion01

  ---------------------------------------------------------------------------
  보드 설정 (Tools 메뉴)
  ---------------------------------------------------------------------------
    - Board: ESP32S3 Dev Module
    - Partition Scheme: 이 스케치는 WiFi/ESP-NOW를 쓰지 않아 09번보다 훨씬 작다.
      기본값으로도 들어가지만, 다른 스케치와 맞추려면 "Huge APP"을 그대로 써도 된다.
  ===========================================================================
*/

#include <Adafruit_NeoPixel.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>

#include "SoundEngine.h"
#include "Logo1DArcade.h"

// 사운드 헤더는 PCM 배열 정의를 담고 있어서 이 .ino 한 곳에서만 include한다(SoundEngine.h 주석 참고).
#include "sounds/blip01.h"
#include "sounds/blip02.h"
#include "sounds/laser.h"
#include "sounds/pickupCoin01.h"
#include "sounds/pickupCoin02.h"
#include "sounds/explosion01.h"
#include "sounds/explosion02.h"
#include "sounds/fanfare.h"

// ---------------------------------------------------------------------------
// 하드웨어
// ---------------------------------------------------------------------------
#define LED_PIN         8    // WS2815 데이터 핀(프로토콜은 WS2812 호환)

#define NUM_LEDS        176  // 스위치 내장 LED 4개 + 라인 LED 172개
#define NUM_SWITCH_LEDS 4    // index 0~3 = 스위치 1~4 내장 LED

#define FIELD_START     NUM_SWITCH_LEDS   // 코트 첫 칸(플레이어 쪽 끝) = 인덱스 4
#define FIELD_END       (NUM_LEDS - 1)    // 코트 마지막 칸(CPU 쪽 끝) = 인덱스 175
#define FIELD_LEN       (FIELD_END - FIELD_START + 1)   // 172
// 라인 LED를 늘리거나 줄이면 NUM_LEDS만 고치면 코트/존/반코트 계산이 전부 따라간다.

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

// 라이브러리에 회색 상수가 없어 직접 정의(RGB565 50% 회색). 구분선/비활성 글자용
#define LCD_GREY     0x7BEF

#define I2S_BCLK_PIN  42
#define I2S_LRC_PIN   41
#define I2S_DOUT_PIN  40

// 인덱스 = 스위치 LED 위치(0~3). 그 자리의 스위치가 물려 있는 GPIO를 적는다.
// 3번/4번 자리의 배선이 GPIO 번호 순서와 반대라 17과 16을 바꿔 넣었다.
const uint8_t SWITCH_PINS[] = {7, 15, 17, 16};
const uint8_t NUM_SWITCHES = sizeof(SWITCH_PINS) / sizeof(SWITCH_PINS[0]);

// 라인 LED를 한꺼번에 켜는 연출이 있어서 밝기를 보수적으로 잡았다.
// WS2815는 12V에서 픽셀당 약 36mA까지 먹으므로 172개를 풀 밝기로 켜면 6A를 넘는다.
#define BRIGHTNESS     120    // 스트립 전체 밝기
#define FULL_SCALE     0.25f  // 코트 전체를 채우는 연출(카운트다운/득점 표시)에 쓰는 감쇠
#define ZONE_SCALE     0.45f  // 존/공처럼 몇 칸만 켜질 때의 감쇠
#define SWITCH_IDLE    0.10f  // 대기 중 스위치 LED를 은은하게 켜 두는 밝기

// ---------------------------------------------------------------------------
// 게임 상수 (여기만 만져도 난이도가 다 바뀐다)
// ---------------------------------------------------------------------------
#define ZONE_RED_LEN      3   // 플레이어 쪽 끝에서부터 빨강 3칸
#define ZONE_YELLOW_LEN   6   // 그다음 노랑 6칸
#define ZONE_GREEN_LEN    9   // 그다음 초록 9칸
#define ZONE_LEN          (ZONE_RED_LEN + ZONE_YELLOW_LEN + ZONE_GREEN_LEN)  // 타격 영역 = 18칸

#define BALL_SPEED_SERVE   60.0f   // 서브 속도(LED칸/초). 코트 172칸을 약 3초에 건너간다
#define REFLECT_GAIN_GREEN  1.00f  // 초록 반사: 온 속도 그대로
#define REFLECT_GAIN_YELLOW 1.18f  // 노랑 반사: 약간 빠르게
#define REFLECT_GAIN_RED    1.10f  // 빨강 반사: 온 속도에서 10% 가속
#define REFLECT_MIN_RED     85.0f  // 빨강 반사의 최저 속도(노랑 반사보다 약간 빠른 값)
#define BALL_SPEED_MAX     220.0f  // 상한. 이보다 빠르면 18칸 영역을 82ms만에 지나간다

#define LEVEL_UP_POINTS    5   // 플레이어가 이 점수를 먼저 내면 레벨 클리어
#define CPU_MATCH_POINTS   5   // CPU가 이 점수를 먼저 내면 게임 오버

// ---------------------------------------------------------------------------
// CPU 난이도 (레벨이 오를수록 반응이 빨라지고 흔들림이 줄어든다)
// ---------------------------------------------------------------------------
// 실제로 붙어 보고 조절할 값은 여기 여섯 개다. 파일 상단의 "CPU가 공을 치는 방식" 참고.
#define CPU_REACT_BASE_MS   120  // 레벨 1의 평균 반응 지연
#define CPU_REACT_STEP_MS    10  // 레벨당 줄어드는 양
#define CPU_REACT_MIN_MS     20  // 반응 지연 하한

#define CPU_JITTER_BASE_MS   60  // 레벨 1의 반응 흔들림(±)
#define CPU_JITTER_STEP_MS    5  // 레벨당 줄어드는 양
#define CPU_JITTER_MIN_MS     8  // 흔들림 하한

#define CPU_WHIFF_BASE_PCT   20  // 레벨 1에서 아예 반응하지 않을 확률(%)
#define CPU_WHIFF_STEP_PCT    2  // 레벨당 줄어드는 양
#define CPU_WHIFF_MIN_PCT     2  // 하한. 최고 레벨에서도 완전무결하지는 않다

// ---------------------------------------------------------------------------
// 연출 타이밍
// ---------------------------------------------------------------------------
#define COUNTDOWN_STEPS      5   // 초록/꺼짐/초록/꺼짐/빨강
#define COUNTDOWN_STEP_MS  500   // 한 스텝 0.5초
#define START_MS           800   // "START" 표시 후 서브까지의 간격
#define FLASH_MS          1000   // 실점 연출 점등 시간
#define LEVEL_UP_MS       2500   // 레벨 클리어 표시 시간
#define GAME_OVER_LOCK_MS 2000   // 게임 오버 화면에서 입력을 무시하는 시간(오조작 방지)
#define BOOT_LOGO_MS      3000   // 부팅 로고를 보여주는 시간

#define RAINBOW_PERIOD_MS 3000   // 레벨 클리어 무지개가 코트를 한 바퀴 흐르는 시간

#define DEBOUNCE_MS        30   // 스위치 채터링 무시 시간
#define SWITCH_FLASH_MS   120   // 누른 스위치 LED를 밝게 켜 두는 시간
#define FRAME_MS           16   // 코트 렌더 주기(약 60FPS). 176픽셀 show()가 약 5.3ms

// ---------------------------------------------------------------------------
// 사운드
// ---------------------------------------------------------------------------
// 이름은 "어떤 소리인가"가 아니라 "언제 나는가"로 붙였다. 같은 클립을 두 상황이 나눠 써도
// 호출부를 읽을 때 상황이 드러나고, 나중에 한쪽만 다른 소리로 바꾸기도 쉽다.
enum SfxId : uint8_t {
  SFX_READY_PRESS = 0,  // READY에서 스위치를 누름
  SFX_COUNTDOWN,        // 카운트다운 한 칸(3/2/1)
  SFX_START,            // START
  SFX_SERVE,            // 서브 출발
  SFX_HIT_GREEN,        // 초록 영역에서 반사
  SFX_HIT_YELLOW,       // 노랑 영역에서 반사
  SFX_HIT_RED,          // 빨강 영역에서 반사
  SFX_CPU_HIT,          // CPU가 받아침
  SFX_PLAYER_MISS,      // 플레이어가 놓침
  SFX_CPU_MISS,         // CPU가 놓침(플레이어 득점)
  SFX_LEVEL_UP,         // 레벨 클리어
  SFX_GAME_OVER,        // 게임 오버
  NUM_SFX
};

static const SoundClip* const GAME_SOUNDS[NUM_SFX] = {
  &CLIP_pickupCoin02,  // SFX_READY_PRESS
  &CLIP_blip01,        // SFX_COUNTDOWN
  &CLIP_blip02,        // SFX_START
  &CLIP_pickupCoin02,  // SFX_SERVE
  &CLIP_pickupCoin01,  // SFX_HIT_GREEN
  &CLIP_pickupCoin02,  // SFX_HIT_YELLOW
  &CLIP_blip01,        // SFX_HIT_RED
  &CLIP_laser,         // SFX_CPU_HIT
  &CLIP_explosion02,   // SFX_PLAYER_MISS
  &CLIP_pickupCoin02,  // SFX_CPU_MISS
  &CLIP_fanfare,       // SFX_LEVEL_UP
  &CLIP_explosion01    // SFX_GAME_OVER
};

// ---------------------------------------------------------------------------
// 표시 장치
// ---------------------------------------------------------------------------
// 색 순서는 RGB다. 데이터시트상 WS2815는 GRB지만 실제 스트립은 R과 G가 반대로 나온다
// (Color(255,0,0)이 초록으로 점등). 스트립을 교체하면 여기부터 확인할 것.
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_RGB + NEO_KHZ800);

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
  // 40KB(160x128x2) 전송이라 24MHz에서 약 14ms 걸린다. 60FPS로 도는 코트 렌더링이
  // 그만큼 밀리므로, 화면 내용이 실제로 바뀔 때만 부른다(lcdDirty).
  void display() { tft.drawRGBBitmap(0, 0, getBuffer(), LCD_W, LCD_H); }
};

Lcd display;

// ---------------------------------------------------------------------------
// 입력 (ISR이 쓰고 loop가 읽으므로 volatile)
// ---------------------------------------------------------------------------
volatile bool checkPending[4] = {false};
volatile uint32_t lastChangeMs[4] = {0};
bool switchPressed[4] = {false};      // 디바운스까지 끝난 확정 상태(loop에서만 접근)
uint32_t switchLedOffAt[4] = {0};     // 0 = 평소 밝기

// ---------------------------------------------------------------------------
// 게임 상태
// ---------------------------------------------------------------------------
enum Phase : uint8_t {
  PHASE_READY = 0,
  PHASE_COUNTDOWN,   // info 대신 countdownStep으로 코트를, ui.info로 화면 숫자를 그린다
  PHASE_START,
  PHASE_PLAY,
  PHASE_POINT,       // 실점 연출
  PHASE_LEVEL_UP,    // 레벨 클리어 연출
  PHASE_GAME_OVER,
};

uint8_t phase = PHASE_READY;
uint32_t phaseStartMs = 0;

uint16_t level = 1;
uint8_t score[2] = {0, 0};        // [0] = 플레이어, [1] = CPU
uint8_t lastPointWinner = 0;      // 실점 연출에서 초록으로 칠할 쪽

float ballPos = 0.0f;             // 코트 좌표(FIELD_START ~ FIELD_END)
float ballPrevPos = 0.0f;         // 잔상(모션 블러)용 직전 위치
float ballSpeed = 0.0f;           // 칸/초 (항상 양수)
int8_t ballDir = 1;               // +1 = CPU 쪽으로, -1 = 플레이어 쪽으로

uint8_t countdownStep = 0;
uint8_t countdownDigit = 3;

// CPU가 이번 접근에 대해 내린 결정.
uint32_t cpuSwingAtMs = 0;        // 0 = 이번 접근에는 스윙하지 않는다
bool cpuDecided = false;          // 이번 접근에 대해 이미 결정을 내렸는가

// LCD는 한 장을 통째로 밀어넣는 데 약 14ms가 걸려서, 매 주기마다 그리면 코트 렌더링이 밀린다.
// 그래서 내용이 실제로 바뀐 순간에만 이 플래그를 세워 그때만 다시 그린다.
// 랠리가 이어지는 동안에는 점수도 단계도 안 바뀌므로 LCD 전송이 아예 일어나지 않는다.
bool lcdDirty = true;

uint32_t lastFrameMs = 0;
uint32_t lastPhysicsUs = 0;

// ---------------------------------------------------------------------------
// 색 유틸
// ---------------------------------------------------------------------------
// 색의 각 성분에 배율을 곱한다. setBrightness()는 스트립 전체에 걸리므로,
// 일부 구간만 어둡게 하려면 색 값 자체를 낮춰야 한다.
uint32_t scaleColor(uint32_t c, float f) {
  uint8_t r = (uint8_t)(((c >> 16) & 0xFF) * f);
  uint8_t g = (uint8_t)(((c >> 8) & 0xFF) * f);
  uint8_t b = (uint8_t)((c & 0xFF) * f);
  return strip.Color(r, g, b);
}

uint32_t colRed()    { return strip.Color(255, 0, 0); }
uint32_t colYellow() { return strip.Color(255, 170, 0); }
uint32_t colGreen()  { return strip.Color(0, 255, 0); }
uint32_t colWhite()  { return strip.Color(255, 255, 255); }

void safeShow() {
  strip.show();
  delayMicroseconds(300);  // WS2815 Reset Time (>=280us) 보장
}

// ---------------------------------------------------------------------------
// 코트 좌표 / 존 계산
// ---------------------------------------------------------------------------
// 플레이어(0) 또는 CPU(1) 쪽 끝에서 몇 칸 떨어져 있는가. 0 = 자기 끝 LED.
int16_t depthFrom(uint8_t side, int16_t idx) {
  return (side == 0) ? (idx - FIELD_START) : (FIELD_END - idx);
}

// 깊이를 존 단계로 바꾼다. 0 = 빨강, 1 = 노랑, 2 = 초록, -1 = 타격 영역 밖.
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

// 코트를 반으로 갈랐을 때 이 인덱스가 누구 쪽인가. 0 = 플레이어 절반, 1 = CPU 절반.
uint8_t halfOwner(int16_t idx) {
  return (idx < FIELD_START + FIELD_LEN / 2) ? 0 : 1;
}

// ---------------------------------------------------------------------------
// CPU 난이도
// ---------------------------------------------------------------------------
// 레벨이 오를수록 base에서 step씩 깎되 minimum 아래로는 내려가지 않는다.
// 세 파라미터가 전부 같은 모양이라 한 함수로 묶었다.
uint16_t rampDown(uint16_t base, uint16_t step, uint16_t minimum) {
  if (base <= minimum) return minimum;
  uint32_t drop = (uint32_t)(level - 1) * step;
  if (drop >= (uint32_t)(base - minimum)) return minimum;
  return (uint16_t)(base - drop);
}

uint16_t cpuReactMs()  { return rampDown(CPU_REACT_BASE_MS,  CPU_REACT_STEP_MS,  CPU_REACT_MIN_MS); }
uint16_t cpuJitterMs() { return rampDown(CPU_JITTER_BASE_MS, CPU_JITTER_STEP_MS, CPU_JITTER_MIN_MS); }
uint16_t cpuWhiffPct() { return rampDown(CPU_WHIFF_BASE_PCT, CPU_WHIFF_STEP_PCT, CPU_WHIFF_MIN_PCT); }

// ---------------------------------------------------------------------------
// 게임 진행
// ---------------------------------------------------------------------------
void playSfx(uint8_t id) { soundPlay(id); }

void enterPhase(uint8_t p, uint8_t digit) {
  phase = p;
  phaseStartMs = millis();
  countdownDigit = digit;
  lcdDirty = true;
}

void resetMatch() {
  level = 1;
  score[0] = 0;
  score[1] = 0;
  enterPhase(PHASE_READY, 0);
}

// 카운트다운 시작. countdownStep을 있을 수 없는 값으로 두어, 첫 스텝(3)의 소리와 표시가
// updateGame()의 스텝 처리에서 다른 스텝과 똑같이 나오게 한다(시작 소리를 따로 부르지 않는다).
void startCountdown() {
  countdownStep = 0xFF;
  enterPhase(PHASE_COUNTDOWN, 3);
}

// 중앙에서 느린 공을 발사한다. 서브는 득점 상황과 무관하게 항상 플레이어 쪽으로 간다
// (인덱스가 작아지는 방향). 1인용이라 서브권을 주고받을 상대가 없기 때문이다.
void launchServe() {
  ballPos = FIELD_START + FIELD_LEN / 2.0f;
  ballPrevPos = ballPos;
  ballSpeed = BALL_SPEED_SERVE;
  ballDir = -1;
  lastPhysicsUs = micros();

  cpuDecided = false;      // 새 공이므로 CPU의 지난 결정은 버린다
  cpuSwingAtMs = 0;

  playSfx(SFX_SERVE);
  enterPhase(PHASE_PLAY, 0);
}

// 반사 후 나갈 속도. 규칙은 파일 상단 주석 참고.
float reflectSpeed(int8_t zone, float inSpeed) {
  float out;
  if (zone == 2) {                      // 초록: 온 속도 그대로
    out = inSpeed * REFLECT_GAIN_GREEN;
  } else if (zone == 1) {               // 노랑: 약간 빠르게
    out = inSpeed * REFLECT_GAIN_YELLOW;
  } else {                              // 빨강: 최저 속도가 노랑 반사보다 약간 빠르고,
    out = inSpeed * REFLECT_GAIN_RED;   //       그보다 빠른 공이 왔으면 10% 가속
    if (out < REFLECT_MIN_RED) out = REFLECT_MIN_RED;
  }
  if (out > BALL_SPEED_MAX) out = BALL_SPEED_MAX;
  return out;
}

// loser가 한 점을 잃는다(= 상대가 한 점을 얻는다). 0 = 플레이어, 1 = CPU.
void concedePoint(uint8_t loser) {
  uint8_t winner = 1 - loser;
  score[winner]++;
  lastPointWinner = winner;

  playSfx(loser == 0 ? SFX_PLAYER_MISS : SFX_CPU_MISS);

  if (score[0] >= LEVEL_UP_POINTS) {
    playSfx(SFX_LEVEL_UP);
    enterPhase(PHASE_LEVEL_UP, 0);
    return;
  }
  if (score[1] >= CPU_MATCH_POINTS) {
    playSfx(SFX_GAME_OVER);
    enterPhase(PHASE_GAME_OVER, 0);
    return;
  }

  enterPhase(PHASE_POINT, 0);
}

// 플레이어가 스위치를 눌렀다(게임 중).
void handlePlayerSwing() {
  // 공이 자기 쪽으로 오는 중이어야 한다.
  // (방금 반사시킨 공이 나가는 동안 또 눌러서 되돌리는 것을 막는다)
  if (ballDir != -1) return;

  int8_t zone = zoneOfDepth(depthFrom(0, (int16_t)floorf(ballPos)));
  if (zone < 0) return;   // 타격 영역 밖에서 누름 - 헛스윙

  ballSpeed = reflectSpeed(zone, ballSpeed);
  ballDir = +1;
  playSfx((zone == 2) ? SFX_HIT_GREEN
        : (zone == 1) ? SFX_HIT_YELLOW
                      : SFX_HIT_RED);
}

// CPU가 스윙한다. 이 순간 공이 자기 영역 안에 있어야 반사되고, 늦었으면 그대로 놓친다.
void cpuSwing() {
  if (ballDir != +1) return;
  int16_t depth = depthFrom(1, (int16_t)floorf(ballPos));
  if (depth < 0 || depth >= ZONE_LEN) return;   // 이미 지나갔다 - 헛스윙

  // CPU의 리턴은 속도를 바꾸지 않는다. 랠리를 가속하는 건 플레이어의 타이밍뿐이다.
  ballDir = -1;
  playSfx(SFX_CPU_HIT);
}

// 공이 CPU 영역에 들어오는 순간 스윙 시각을 한 번 정하고, 그 시각이 되면 스윙한다.
void updateCpu(uint32_t now) {
  if (ballDir != +1) {          // 공이 플레이어 쪽으로 가는 동안은 다음 접근을 위해 비워 둔다
    cpuDecided = false;
    cpuSwingAtMs = 0;
    return;
  }

  if (!cpuDecided) {
    int16_t depth = depthFrom(1, (int16_t)floorf(ballPos));
    if (depth >= 0 && depth < ZONE_LEN) {
      cpuDecided = true;
      if ((uint16_t)random(100) < cpuWhiffPct()) {
        cpuSwingAtMs = 0;       // 이번엔 아예 반응하지 않는다
      } else {
        int32_t jitter = (int32_t)cpuJitterMs();
        int32_t delayMs = (int32_t)cpuReactMs() + (int32_t)random(-jitter, jitter + 1);
        if (delayMs < 0) delayMs = 0;
        cpuSwingAtMs = now + (uint32_t)delayMs;
      }
    }
  }

  if (cpuSwingAtMs != 0 && now >= cpuSwingAtMs) {
    cpuSwingAtMs = 0;
    cpuSwing();
  }
}

void updateGame(uint32_t now) {
  switch (phase) {
    case PHASE_COUNTDOWN: {
      uint8_t step = (uint8_t)((now - phaseStartMs) / COUNTDOWN_STEP_MS);
      if (step >= COUNTDOWN_STEPS) {
        playSfx(SFX_START);
        enterPhase(PHASE_START, 0);
        break;
      }
      if (step != countdownStep) {
        countdownStep = step;
        countdownDigit = 3 - (step / 2);              // 0,1 → 3 / 2,3 → 2 / 4 → 1
        if (step % 2 == 0) playSfx(SFX_COUNTDOWN);    // 불이 켜지는 스텝에서만 소리
        lcdDirty = true;   // enterPhase를 쓰면 phaseStartMs가 리셋돼 카운트다운이 처음부터 다시 간다
      }
      break;
    }

    case PHASE_START:
      if (now - phaseStartMs >= START_MS) launchServe();
      break;

    case PHASE_PLAY: {
      uint32_t nowUs = micros();
      float dt = (nowUs - lastPhysicsUs) / 1000000.0f;
      lastPhysicsUs = nowUs;
      if (dt > 0.05f) dt = 0.05f;   // 오래 멈췄다 돌아온 경우 공이 순간이동하지 않도록 제한

      ballPrevPos = ballPos;
      ballPos += ballDir * ballSpeed * dt;

      updateCpu(now);

      if (ballPos < FIELD_START)     concedePoint(0);   // 플레이어가 놓침
      else if (ballPos > FIELD_END)  concedePoint(1);   // CPU가 놓침
      break;
    }

    case PHASE_POINT:
      if (now - phaseStartMs >= FLASH_MS) launchServe();
      break;

    case PHASE_LEVEL_UP:
      if (now - phaseStartMs >= LEVEL_UP_MS) {
        level++;          // 여기서 올려야 LEVEL_UP 화면이 "방금 깬 레벨"을 보여준다
        score[0] = 0;
        score[1] = 0;
        startCountdown();
      }
      break;

    default:
      break;
  }
}

// 스위치를 눌렀을 때. 단계에 따라 하는 일이 다르다.
void handlePress(uint8_t idx, uint32_t now) {
  switchLedOffAt[idx] = now + SWITCH_FLASH_MS;

  switch (phase) {
    case PHASE_READY:
      playSfx(SFX_READY_PRESS);
      startCountdown();
      return;

    case PHASE_GAME_OVER:
      if (now - phaseStartMs >= GAME_OVER_LOCK_MS) resetMatch();
      return;

    case PHASE_PLAY:
      handlePlayerSwing();
      return;

    default:
      return;   // 카운트다운/연출 중에는 입력을 받지 않는다
  }
}

// ---------------------------------------------------------------------------
// 렌더링
// ---------------------------------------------------------------------------
void clearField() {
  for (int16_t i = FIELD_START; i <= FIELD_END; i++) strip.setPixelColor(i, 0);
}

void fillField(uint32_t color) {
  uint32_t c = scaleColor(color, FULL_SCALE);
  for (int16_t i = FIELD_START; i <= FIELD_END; i++) strip.setPixelColor(i, c);
}

// 양쪽의 타격 영역(빨강 1 / 노랑 3 / 초록 6)을 그린다.
void drawZones() {
  for (uint8_t s = 0; s < 2; s++) {
    for (int16_t d = 0; d < ZONE_LEN; d++) {
      int16_t idx = (s == 0) ? (FIELD_START + d) : (FIELD_END - d);
      strip.setPixelColor(idx, scaleColor(zoneColor(zoneOfDepth(d)), ZONE_SCALE));
    }
  }
}

// 공은 흰색 1칸. 빠를 때는 한 프레임에 여러 칸을 건너뛰어 점선처럼 보이므로,
// 직전 위치까지의 구간을 어둡게 채워 잔상(모션 블러)을 남긴다.
void drawBall() {
  int16_t head = (int16_t)lroundf(ballPos);
  int16_t tail = (int16_t)lroundf(ballPrevPos);
  if (head < FIELD_START) head = FIELD_START;
  if (head > FIELD_END)   head = FIELD_END;
  if (tail < FIELD_START) tail = FIELD_START;
  if (tail > FIELD_END)   tail = FIELD_END;

  int16_t step = (tail > head) ? -1 : 1;
  for (int16_t i = tail; i != head; i += step) {
    strip.setPixelColor(i, scaleColor(colWhite(), ZONE_SCALE * 0.35f));
  }
  strip.setPixelColor(head, scaleColor(colWhite(), ZONE_SCALE));
}

// 코트를 반으로 갈라 이긴 쪽은 초록, 진 쪽은 빨강.
void drawHalves(uint8_t winner) {
  uint32_t win  = scaleColor(colGreen(), FULL_SCALE);
  uint32_t lose = scaleColor(colRed(), FULL_SCALE);
  for (int16_t i = FIELD_START; i <= FIELD_END; i++) {
    strip.setPixelColor(i, (halfOwner(i) == winner) ? win : lose);
  }
}

// 레벨 클리어 축하용. 스위치 LED까지 포함해 스트립 전체를 흐르는 무지개로 채운다.
void drawRainbow(uint32_t now) {
  uint16_t base = (uint16_t)(((now % RAINBOW_PERIOD_MS) * 65536UL) / RAINBOW_PERIOD_MS);
  for (uint16_t i = 0; i < NUM_LEDS; i++) {
    uint16_t hue = base + (uint16_t)((uint32_t)i * 65536UL / NUM_LEDS);
    strip.setPixelColor(i, scaleColor(strip.gamma32(strip.ColorHSV(hue)), FULL_SCALE));
  }
}

// 무지개가 스위치 LED까지 덮는 구간인가(그 위에 스위치 LED를 덧그리지 않기 위해).
bool rainbowActive() {
  return phase == PHASE_LEVEL_UP;
}

void renderField() {
  clearField();

  switch (phase) {
    case PHASE_READY:
      drawZones();   // 자기 영역이 어디인지 미리 보여준다
      break;

    case PHASE_COUNTDOWN:
      // 초록 / 꺼짐 / 초록 / 꺼짐 / 빨강 을 한 스텝씩
      if (countdownStep == 4)            fillField(colRed());
      else if (countdownStep % 2 == 0)   fillField(colGreen());
      break;

    case PHASE_START:
      break;         // START 표시 동안 코트는 비운다

    case PHASE_PLAY:
      drawZones();
      drawBall();
      break;

    case PHASE_POINT:
      drawHalves(lastPointWinner);
      break;

    case PHASE_LEVEL_UP:
      drawRainbow(millis());
      break;

    case PHASE_GAME_OVER:
      drawHalves(1);   // CPU 쪽 절반이 초록
      break;
  }
}

// ---------------------------------------------------------------------------
// 스위치 LED
// ---------------------------------------------------------------------------
// 스위치는 전부 흰색이다. 평소에는 아주 은은하게 켜 두고, 누르면 잠깐 밝아진다.
void renderSwitchLeds(uint32_t now) {
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    bool bright = (switchLedOffAt[i] != 0 && now < switchLedOffAt[i]);
    if (!bright && switchLedOffAt[i] != 0) switchLedOffAt[i] = 0;
    strip.setPixelColor(i, scaleColor(colWhite(), bright ? 1.0f : SWITCH_IDLE));
  }
}

// ---------------------------------------------------------------------------
// LCD
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

// 점수판: 플레이어는 노랑, CPU는 자홍. 아래에 현재 레벨.
void drawScoreBoard() {
  display.setTextSize(2);
  display.setTextColor(ST77XX_YELLOW);
  display.setCursor(16, 2);
  display.print("YOU");
  display.setTextColor(ST77XX_MAGENTA);
  display.setCursor(104, 2);
  display.print("CPU");

  display.setTextSize(6);   // 숫자 한 자 36x48px
  display.setTextColor(ST77XX_YELLOW);
  display.setCursor(16, 28);
  display.print(score[0]);
  display.setTextColor(ST77XX_MAGENTA);
  display.setCursor(108, 28);
  display.print(score[1]);

  display.drawFastVLine(80, 20, 60, LCD_GREY);

  char buf[16];
  snprintf(buf, sizeof(buf), "LEVEL %u", level);
  display.setTextColor(ST77XX_CYAN);
  printCentered(buf, 2, 106);
}

void drawUi() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(ST77XX_WHITE);

  switch (phase) {
    case PHASE_READY: {
      display.setTextColor(ST77XX_CYAN);
      printCentered("1P PONG", 2, 4);
      display.setTextColor(ST77XX_WHITE);
      printCentered("READY?", 4, 40);

      char buf[16];
      snprintf(buf, sizeof(buf), "LEVEL %u", level);
      display.setTextColor(LCD_GREY);
      printCentered(buf, 2, 84);
      printCentered("PRESS ANY BUTTON", 1, 112);
      break;
    }

    case PHASE_COUNTDOWN: {
      // 카운트다운 숫자는 멀리서도 보이게 화면을 거의 채운다(크기 12 = 72x96px)
      char buf[2] = {(char)('0' + countdownDigit), '\0'};
      display.setTextColor(countdownDigit == 1 ? ST77XX_RED : ST77XX_GREEN);
      printCentered(buf, 12, 16);
      break;
    }

    case PHASE_START:
      display.setTextColor(ST77XX_GREEN);
      printCentered("START", 4, 48);
      break;

    case PHASE_PLAY:
    case PHASE_POINT:
      drawScoreBoard();   // 실점 연출은 코트 LED가 알려주므로 화면은 점수판 그대로 둔다
      break;

    case PHASE_LEVEL_UP: {
      display.setTextColor(ST77XX_GREEN);
      printCentered("LEVEL", 4, 22);

      char buf[8];
      snprintf(buf, sizeof(buf), "%u", level);
      printCentered(buf, 6, 58);

      display.setTextColor(ST77XX_WHITE);
      printCentered("CLEAR!", 2, 108);
      break;
    }

    case PHASE_GAME_OVER: {
      display.setTextColor(ST77XX_RED);
      printCentered("GAME", 4, 10);
      printCentered("OVER", 4, 44);

      char buf[16];
      snprintf(buf, sizeof(buf), "LEVEL %u", level);
      display.setTextColor(ST77XX_WHITE);
      printCentered(buf, 2, 84);

      display.setTextColor(LCD_GREY);
      printCentered("PRESS TO RETRY", 1, 112);
      break;
    }
  }
  display.display();
}

void drawBootScreen() {
  display.clearDisplay();
  int16_t logoX = (LCD_W - (int16_t)LOGO_1D_ARCADE_W) / 2;
  int16_t logoY = (LCD_H - (int16_t)LOGO_1D_ARCADE_H) / 2;
  drawLogo1DArcade(display, logoX, logoY);   // RLE 압축 로고(Logo1DArcade.h 주석 참고)
  display.display();
}

// ---------------------------------------------------------------------------
// 입력
// ---------------------------------------------------------------------------
// ISR은 짧게 끝내야 하므로 시각 기록 + 플래그만 세우고 판정은 loop()에 위임한다.
void IRAM_ATTR handleSwitchInterrupt(void* arg) {
  uint8_t idx = (uint8_t)(uintptr_t)arg;
  lastChangeMs[idx] = millis();
  checkPending[idx] = true;
}

// 부팅 시 스위치 LED 4개를 하나씩 켜 배선을 확인한다.
void bootAnimation() {
  for (uint8_t i = 0; i < NUM_SWITCH_LEDS; i++) {
    strip.clear();
    strip.setPixelColor(i, colWhite());
    safeShow();
    delay(120);
  }
  strip.clear();
  safeShow();
}

// ---------------------------------------------------------------------------
void setup() {
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    pinMode(SWITCH_PINS[i], INPUT_PULLUP);  // 눌리면 GND로 연결되는 배선이라 내부 풀업 사용
  }

  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  strip.clear();
  safeShow();

  display.begin();
  soundEngineBegin(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN, GAME_SOUNDS, NUM_SFX);

  bootAnimation();  // 인터럽트를 걸기 전에 실행 - 초기화 도중 스위치 신호로 오작동하는 것을 방지

  randomSeed(esp_random());   // 서브 방향과 CPU 반응 흔들림에 쓴다

  drawBootScreen();
  delay(BOOT_LOGO_MS);

  phase = PHASE_READY;
  phaseStartMs = millis();
  lastPhysicsUs = micros();
  lcdDirty = true;

  // 인터럽트는 다른 초기화가 모두 끝난 뒤 마지막에 붙인다
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    attachInterruptArg(SWITCH_PINS[i], handleSwitchInterrupt, (void*)(uintptr_t)i, CHANGE);
  }
}

void loop() {
  uint32_t now = millis();

  // 스위치 입력
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    if (checkPending[i] && (now - lastChangeMs[i] >= DEBOUNCE_MS)) {
      checkPending[i] = false;

      bool pressed = (digitalRead(SWITCH_PINS[i]) == LOW);  // 풀업 배선이라 눌리면 LOW
      if (pressed == switchPressed[i]) continue;            // 디바운스 후에도 실제로 바뀐 경우만
      switchPressed[i] = pressed;
      if (!pressed) continue;                               // 누를 때만 반응

      handlePress(i, now);
    }
  }

  updateGame(now);

  // 스트립 갱신. 무지개 구간에는 스위치 LED도 무지개의 일부라 덧그리지 않는다.
  if (now - lastFrameMs >= FRAME_MS) {
    lastFrameMs = now;
    renderField();
    if (!rainbowActive()) renderSwitchLeds(now);
    safeShow();
  }

  // LCD는 한 장 전송에 약 14ms가 걸린다. 코트를 60FPS로 그리는 중에 매번 부르면 공이 눈에 띄게
  // 끊기므로, 화면 내용이 실제로 바뀐 순간에만 그린다.
  if (lcdDirty) {
    lcdDirty = false;
    drawUi();
  }
}
