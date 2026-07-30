/*
  ===========================================================================
  09 - 2P Pong (Duel 전용)
  ===========================================================================
  08test2P-SoundSwitchLED의 ESP-NOW 페어링/사운드 구조를 그대로 가져와, 두 사람이
  라인 LED 하나를 코트로 삼아 공을 주고받는 대전용 Pong을 올린 스케치다.
  1P CPU 모드는 없다(2인 전용).

  ---------------------------------------------------------------------------
  역할 분담 (Game_Logics.md의 통신 원칙 그대로)
  ---------------------------------------------------------------------------
  부팅 시 5번 토글 스위치(GPIO18)를 읽어 역할이 정해진다(HIGH = 1P, LOW = 2P).
  두 보드는 반드시 서로 다른 역할이어야 하며, 같으면 LCD에 ROLE MISMATCH가 뜬다.

    - 1P: authoritative. 공 물리/판정/점수/사운드 결정을 전부 혼자 하고, 라인 LED를 그린다.
          (라인 LED는 1P 보드에만 물려 있다)
    - 2P: 자기 스위치 입력을 1P로 보내기만 한다. 자기 스위치 LED와 LCD, 사운드 재생만 담당.

  통신 방향
    - 2P → 1P : MSG_INPUT (스위치가 눌렸다)
    - 1P → 2P : MSG_STATE(LCD에 그릴 게임 상태 - 주기 + 변화 시 즉시)
                MSG_SFX  (지금 이 소리를 내라 - 이벤트 발생 즉시)
    소리를 1P가 판정해서 내려보내는 이유: 2P가 스위치를 눌러도 그게 유효한 반사인지
    헛스윙인지는 1P만 알기 때문이다. 눌렀다고 2P가 먼저 소리를 내면 헛스윙에도 반사음이 난다.

  ---------------------------------------------------------------------------
  게임 규칙
  ---------------------------------------------------------------------------
  [코트]
    1P 보드에 물린 스트립 180개 중 라인 LED 176개(스트립 5번 = 인덱스 4 ~ 스트립 180번 = 인덱스 179)가
    코트다. 인덱스가 작은 쪽이 1P, 큰 쪽이 2P.
    각 플레이어 쪽 끝에서부터 빨강 1개 / 노랑 3개 / 초록 6개, 총 10칸이 그 사람의 타격 영역이다.

  [반사]
    공이 자기 영역 안에 있을 때 1~4번 아무 스위치나 누르면 반대편으로 반사된다.
    영역 밖에서 누르면 아무 일도 없다(스위치 LED만 깜빡).
      - 초록에서 반사 : 온 속도 그대로
      - 노랑에서 반사 : 약간 빠르게 (REFLECT_GAIN_YELLOW)
      - 빨강에서 반사 : 최저 REFLECT_MIN_RED(노랑보다 약간 빠른 속도)로 나가고,
                        그보다 빠른 공이 왔으면 온 속도에서 10% 가속(REFLECT_GAIN_RED)

  [시작]
    READY? → 양쪽 다 스위치를 누르면 OK가 잠깐 → 코트 전체가 초록/꺼짐/초록/꺼짐/빨강으로
    1초씩 바뀌는 동안 LCD에 큰 숫자로 3,2,1 → START → 중앙에서 랜덤 방향으로 느린 서브.

  [서브]
    서브는 항상 중앙에서 느린 속도로 출발한다.
      - 서브를 놓치면(1번째)  : 그 사람 영역이 주황색으로 점등, 점수 변화 없이 같은 쪽으로 재서브.
      - 두 번째도 놓치면      : 그 사람 영역이 빨간색으로 점등, 실점. 다음 서브는 반대편으로.
      - 서브를 한 번이라도 받아내면 랠리가 되고, 서브 실패 카운트는 초기화된다.

  [랠리 실점]
    반사에 실패하면 코트(5~마지막 LED)를 반으로 갈라 가까운 쪽을 각자의 영역으로 보고,
    이긴 쪽 절반은 초록, 진 쪽 절반은 빨강으로 1초간 점등한 뒤 점수를 갱신한다.
    다음 서브는 점수를 잃은 쪽으로 출발한다.

  [매치]
    MATCH_POINT점을 먼저 내면 승리(팡파레 + 승자 쪽 절반 초록). 아무 스위치나 누르면 처음으로.

  ---------------------------------------------------------------------------
  튜닝 포인트
  ---------------------------------------------------------------------------
  실제로 쳐 보고 조절할 값은 아래 "게임 상수" 블록에 모아 두었다.
  특히 BALL_SPEED_SERVE / REFLECT_* / BALL_SPEED_MAX 네 가지가 체감 난이도를 결정한다.
  존 길이(ZONE_*_LEN)를 바꾸면 반사 판정 창이 그대로 넓어지거나 좁아진다.

  ---------------------------------------------------------------------------
  보드 설정 (Tools 메뉴) - 두 보드 모두 동일하게
  ---------------------------------------------------------------------------
    - Board: ESP32S3 Dev Module
    - Partition Scheme: "Huge APP (3MB No OTA/1MB SPIFFS)" 권장 (Custom이면 빌드 실패)
    - PSRAM: 무관
  ===========================================================================
*/

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>

#include "SoundEngine.h"

// 사운드 헤더는 PCM 배열 정의를 담고 있어서 이 .ino 한 곳에서만 include한다(SoundEngine.h 주석 참고).
#include "sounds/blip01.h"
#include "sounds/blip02.h"
#include "sounds/laser.h"
#include "sounds/pickupCoin01.h"
#include "sounds/pickupCoin02.h"
#include "sounds/explosion01.h"
#include "sounds/powerUp.h"
#include "sounds/fanfare.h"

// ---------------------------------------------------------------------------
// 하드웨어
// ---------------------------------------------------------------------------
#define LED_PIN         8    // WS2815 데이터 핀(프로토콜은 WS2812 호환). 양쪽 보드 모두 같은 핀

// 두 보드의 스트립은 길이가 다르다. 서로 이어져 있지 않고 각자 자기 GPIO8에 물린 별개의 체인이다.
//   1P 보드: 스위치 내장 LED 4개 + 라인 LED 176개 = 180개  ← 코트가 여기에 있다
//   2P 보드: 자기 스위치 내장 LED 4개뿐            = 4개   ← 위 180개에 포함되지 않는 별도 체인
#define NUM_LEDS_1P     180  // 1P 보드 스트립 전체
#define NUM_LEDS_2P     4    // 2P 보드 스트립 전체(스위치 LED만)
#define NUM_SWITCH_LEDS 4    // 양쪽 공통: index 0~3 = 자기 보드 스위치 1~4 내장 LED

#define FIELD_START     NUM_SWITCH_LEDS    // 코트 첫 칸(1P 쪽 끝) = 인덱스 4  → 스트립 5번째 LED
#define FIELD_END       (NUM_LEDS_1P - 1)  // 코트 마지막 칸(2P 쪽 끝) = 인덱스 179 → 스트립 180번째 LED
#define FIELD_LEN       (FIELD_END - FIELD_START + 1)   // 176
// 라인 LED를 늘리거나 줄이면 NUM_LEDS_1P만 고치면 코트/존/반코트 계산이 전부 따라간다.

// ST7735 TFT LCD 핀 (5110과 같은 자리. RS=DC, SDA=MOSI, CLK=SCLK)
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

#define MODE_SWITCH_PIN  18   // 5번 토글 스위치: HIGH = 1P, LOW = 2P

const uint8_t SWITCH_PINS[] = {7, 15, 16, 17};
const uint8_t NUM_SWITCHES = sizeof(SWITCH_PINS) / sizeof(SWITCH_PINS[0]);

// 라인 LED를 한꺼번에 켜는 연출이 있어서 밝기를 보수적으로 잡았다(08번 스케치와 동일한 근거).
// WS2815는 12V에서 픽셀당 약 36mA까지 먹으므로 176개를 풀 밝기로 켜면 6A를 넘는다.
#define BRIGHTNESS     120    // 스트립 전체 밝기
#define FULL_SCALE     0.25f  // 코트 전체를 채우는 연출(카운트다운/득점 표시)에 쓰는 감쇠
#define ZONE_SCALE     0.45f  // 존/공처럼 몇 칸만 켜질 때의 감쇠(20칸 정도라 조금 더 밝게)
#define SWITCH_IDLE    0.10f  // 대기 중 스위치 LED를 은은하게 켜 두는 밝기(Pong 스위치는 흰색)

// ---------------------------------------------------------------------------
// 통신
// ---------------------------------------------------------------------------
#define ESPNOW_CHANNEL      1     // 두 보드가 같은 채널이어야 한다(추후 설정 화면에서 NVS 저장 예정)
#define HELLO_INTERVAL_MS   500   // 페어링 전 브로드캐스트 주기
#define STATE_INTERVAL_MS   120   // 1P가 2P에 상태를 밀어주는 주기(패킷 유실 대비 반복 송신)
#define LCD_SEARCH_MS       300   // 페어링 전 SEARCHING 점 애니메이션 갱신 주기
                                  // (페어링 후에는 내용이 바뀔 때만 그린다 - lcdDirty 참고)

// ---------------------------------------------------------------------------
// 게임 상수 (여기만 만져도 난이도가 다 바뀐다)
// ---------------------------------------------------------------------------
#define ZONE_RED_LEN      1   // 플레이어 쪽 끝에서부터 빨강 1칸
#define ZONE_YELLOW_LEN   3   // 그다음 노랑 3칸
#define ZONE_GREEN_LEN    6   // 그다음 초록 6칸
#define ZONE_LEN          (ZONE_RED_LEN + ZONE_YELLOW_LEN + ZONE_GREEN_LEN)  // 타격 영역 = 10칸

#define BALL_SPEED_SERVE   60.0f   // 서브 속도(LED칸/초). 코트 176칸을 약 3초에 건너간다
#define REFLECT_GAIN_GREEN  1.00f  // 초록 반사: 온 속도 그대로
#define REFLECT_GAIN_YELLOW 1.18f  // 노랑 반사: 약간 빠르게
#define REFLECT_GAIN_RED    1.10f  // 빨강 반사: 온 속도에서 10% 가속
#define REFLECT_MIN_RED     85.0f  // 빨강 반사의 최저 속도(노랑 반사보다 약간 빠른 값)
#define BALL_SPEED_MAX     220.0f  // 상한. 이보다 빠르면 10칸 영역을 45ms만에 지나가 사람이 못 친다

#define MATCH_POINT        7   // 먼저 이 점수에 도달하면 승리

#define OK_MS              700   // "OK" 표시 시간
#define COUNTDOWN_STEPS      5   // 초록/꺼짐/초록/꺼짐/빨강
#define COUNTDOWN_STEP_MS  1000  // 한 스텝 1초
#define START_MS           800   // "START" 표시 후 서브까지의 간격
#define FLASH_MS          1000   // 실점/폴트 연출 점등 시간(1초)
#define MATCH_END_LOCK_MS 2000   // 승리 화면에서 입력을 무시하는 시간(오조작 방지)

#define DEBOUNCE_MS        30   // 스위치 채터링 무시 시간
#define SWITCH_FLASH_MS   120   // 누른 스위치 LED를 밝게 켜 두는 시간
#define FRAME_MS           16   // 코트 렌더 주기(약 60FPS). 180픽셀 show()가 약 5.4ms

// ---------------------------------------------------------------------------
// 사운드
// ---------------------------------------------------------------------------
// soundPlay()의 인자가 곧 이 배열의 인덱스이고, 그 인덱스를 그대로 2P에도 실어 보낸다.
enum SfxId : uint8_t {
  SFX_BOUNCE = 0,   // 초록 반사
  SFX_BOUNCE_FAST,  // 노랑/빨강 반사
  SFX_SERVE,        // 서브 발사
  SFX_FAULT,        // 첫 서브 놓침(주황)
  SFX_POINT,        // 실점
  SFX_TICK,         // 카운트다운 한 칸
  SFX_START,        // START
  SFX_WIN,          // 매치 승리
  NUM_SFX
};

static const SoundClip* const GAME_SOUNDS[NUM_SFX] = {
  &CLIP_blip01,        // SFX_BOUNCE
  &CLIP_laser,         // SFX_BOUNCE_FAST
  &CLIP_pickupCoin01,  // SFX_SERVE
  &CLIP_pickupCoin02,  // SFX_FAULT
  &CLIP_explosion01,   // SFX_POINT
  &CLIP_blip02,        // SFX_TICK
  &CLIP_powerUp,       // SFX_START
  &CLIP_fanfare        // SFX_WIN
};

// 길이는 setup()에서 역할에 따라 updateLength()로 정한다(1P = 180, 2P = 4).
Adafruit_NeoPixel strip(NUM_LEDS_1P, LED_PIN, NEO_GRB + NEO_KHZ800);

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
  // 40KB(160x128x2) 전송이라 24MHz에서 약 14ms 걸린다. 60FPS로 도는 코트 렌더링이
  // 그만큼 밀리므로, 이 게임에서는 화면 내용이 실제로 바뀔 때만 부른다(lcdDirty).
  void display() { tft.drawRGBBitmap(0, 0, getBuffer(), LCD_W, LCD_H); }
};

Lcd display;

// ---------------------------------------------------------------------------
// 패킷
// ---------------------------------------------------------------------------
enum MsgType : uint8_t {
  MSG_HELLO     = 1,
  MSG_HELLO_ACK = 2,
  MSG_INPUT     = 3,  // 2P → 1P: 스위치가 눌렸다
  MSG_STATE     = 4,  // 1P → 2P: LCD에 그릴 상태
  MSG_SFX       = 5,  // 1P → 2P: 이 소리를 내라
};

typedef struct __attribute__((packed)) {
  uint8_t type;
  uint8_t role;    // 1 = 1P, 2 = 2P
  uint8_t arg;     // MSG_INPUT: 스위치 인덱스 / MSG_SFX: SfxId
  uint8_t phase;   // MSG_STATE: 게임 단계
  uint8_t score1;  // MSG_STATE: 1P 점수
  uint8_t score2;  // MSG_STATE: 2P 점수
  uint8_t info;    // MSG_STATE: 단계별 부가 정보(아래 UiState 주석 참고)
} packet_t;

bool isRole1P = false;
bool paired = false;
uint8_t peerMac[6] = {0};
uint32_t lastHelloMs = 0;
uint32_t lastStateMs = 0;
bool roleMismatchDetected = false;
uint32_t roleMismatchAtMs = 0;
uint32_t pairedAtMs = 0;

const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ESP-NOW 수신 콜백은 WiFi 태스크에서 돈다. 여기서는 표시만 남기고 실제 처리는 loop()에서 한다.
// 상대가 여러 번 빠르게 눌러도 놓치지 않도록 비트마스크로 모아 둔다(비트 0~3 = 스위치 1~4).
volatile uint8_t remoteInputMask = 0;

// ---------------------------------------------------------------------------
// 입력 (ISR이 쓰고 loop가 읽으므로 volatile)
// ---------------------------------------------------------------------------
volatile bool checkPending[4] = {false};
volatile uint32_t lastChangeMs[4] = {0};
bool switchPressed[4] = {false};      // 디바운스까지 끝난 확정 상태(loop에서만 접근)
uint32_t switchLedOffAt[4] = {0};     // 0 = 평소 밝기

// ---------------------------------------------------------------------------
// 게임 상태 (1P만 갱신한다)
// ---------------------------------------------------------------------------
enum Phase : uint8_t {
  PHASE_READY = 0,   // info: bit0 = 1P 준비됨, bit1 = 2P 준비됨
  PHASE_OK,          // info: 사용 안 함
  PHASE_COUNTDOWN,   // info: 화면에 띄울 숫자(3/2/1)
  PHASE_START,       // info: 사용 안 함
  PHASE_PLAY,        // info: 1 = 아직 서브 중(랠리 전), 0 = 랠리 중
  PHASE_FAULT,       // info: 서브를 놓친 플레이어(1 또는 2)
  PHASE_POINT,       // info: 하위 4비트 = 이긴 플레이어(1/2), bit4 = 더블 폴트로 난 점수
  PHASE_MATCH_END,   // info: 승자(1 또는 2)
};

uint8_t phase = PHASE_READY;
uint32_t phaseStartMs = 0;
uint8_t score[2] = {0, 0};        // [0] = 1P, [1] = 2P
uint8_t readyMask = 0;            // PHASE_READY에서 누가 준비됐는지

float ballPos = 0.0f;             // 코트 좌표(FIELD_START ~ FIELD_END)
float ballPrevPos = 0.0f;         // 잔상(모션 블러)용 직전 위치
float ballSpeed = 0.0f;           // 칸/초 (항상 양수)
int8_t ballDir = 1;               // +1 = 2P 쪽으로, -1 = 1P 쪽으로
bool serving = false;             // 이 공이 아직 서브인가(한 번도 반사되지 않았는가)
uint8_t serveTarget = 0;          // 서브를 받는 사람 (0 = 1P, 1 = 2P)
uint8_t serveFaults = 0;          // 같은 사람이 연속으로 서브를 놓친 횟수
uint8_t flashPlayer = 0;          // FAULT/POINT 연출의 대상 플레이어 (0/1)
bool pointByFault = false;        // 이번 실점이 더블 폴트로 난 것인가
uint8_t countdownStep = 0;

// LCD는 양쪽이 똑같이 보여야 한다. 1P는 게임 상태에서 채우고, 2P는 받은 패킷으로 채운다.
struct UiState {
  uint8_t phase;
  uint8_t score1;
  uint8_t score2;
  uint8_t info;
};
UiState ui = {PHASE_READY, 0, 0, 0};

// LCD는 한 장을 통째로 밀어넣는 데 약 14ms가 걸려서, 매 주기마다 그리면 코트 렌더링이 밀린다.
// 그래서 내용이 실제로 바뀐 순간에만 이 플래그를 세워 그때만 다시 그린다.
// 랠리가 이어지는 동안에는 점수도 단계도 안 바뀌므로 LCD 전송이 아예 일어나지 않는다.
bool lcdDirty = true;

bool stripDirty = false;
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
uint32_t colOrange() { return strip.Color(255, 80, 0); }
uint32_t colWhite()  { return strip.Color(255, 255, 255); }

void safeShow() {
  strip.show();
  delayMicroseconds(300);  // WS2815 Reset Time (>=280us) 보장
}

// ---------------------------------------------------------------------------
// 코트 좌표 / 존 계산
// ---------------------------------------------------------------------------
// 플레이어 쪽 끝에서 몇 칸 떨어져 있는가. 0 = 자기 끝 LED.
int16_t depthFromPlayer(uint8_t player, int16_t idx) {
  return (player == 0) ? (idx - FIELD_START) : (FIELD_END - idx);
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

// 코트를 반으로 갈랐을 때 이 인덱스가 누구 쪽인가. 0 = 1P 절반, 1 = 2P 절반.
uint8_t halfOwner(int16_t idx) {
  return (idx < FIELD_START + FIELD_LEN / 2) ? 0 : 1;
}

// ---------------------------------------------------------------------------
// ESP-NOW
// ---------------------------------------------------------------------------
bool isExpectedPeerRole(uint8_t remoteRole) {
  return isRole1P ? (remoteRole == 2) : (remoteRole == 1);
}

void addPeer(const uint8_t* mac) {
  if (esp_now_is_peer_exist(mac)) return;
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, mac, 6);
  peerInfo.channel = ESPNOW_CHANNEL;
  peerInfo.ifidx = WIFI_IF_STA;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
}

void sendPacket(const uint8_t* mac, packet_t& pkt) {
  esp_now_send(mac, (uint8_t*)&pkt, sizeof(pkt));
}

// 1P → 2P 상태 송신. 단계가 바뀔 때마다 즉시 부르고, 그 사이에도 주기적으로 반복 송신한다
// (패킷 하나가 유실돼도 다음 주기에 따라잡히도록).
void sendState() {
  if (!isRole1P || !paired) return;
  packet_t pkt = {};
  pkt.type = MSG_STATE;
  pkt.role = 1;
  pkt.phase = ui.phase;
  pkt.score1 = ui.score1;
  pkt.score2 = ui.score2;
  pkt.info = ui.info;
  sendPacket(peerMac, pkt);
  lastStateMs = millis();
}

void OnDataRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len != sizeof(packet_t)) return;
  packet_t pkt;
  memcpy(&pkt, data, sizeof(pkt));

  switch (pkt.type) {
    case MSG_HELLO:
      if (!paired) {
        if (!isExpectedPeerRole(pkt.role)) {  // 두 보드가 같은 역할로 켜진 경우
          roleMismatchDetected = true;
          roleMismatchAtMs = millis();
          break;
        }
        memcpy(peerMac, info->src_addr, 6);
        addPeer(peerMac);
        paired = true;
        pairedAtMs = millis();
        roleMismatchDetected = false;
        lcdDirty = true;   // 탐색 화면에서 게임 화면으로 넘어가야 한다

        packet_t ack = {};
        ack.type = MSG_HELLO_ACK;
        ack.role = isRole1P ? 1 : 2;
        sendPacket(peerMac, ack);
      }
      break;

    case MSG_HELLO_ACK:
      if (!paired) {
        if (!isExpectedPeerRole(pkt.role)) {
          roleMismatchDetected = true;
          roleMismatchAtMs = millis();
          break;
        }
        memcpy(peerMac, info->src_addr, 6);
        addPeer(peerMac);
        paired = true;
        pairedAtMs = millis();
        roleMismatchDetected = false;
        lcdDirty = true;   // 탐색 화면에서 게임 화면으로 넘어가야 한다
      }
      break;

    // 아래 3개는 페어링된 상대가 보낸 것만 받아들인다(다른 보드의 브로드캐스트에 반응하지 않도록)
    case MSG_INPUT:
      if (isRole1P && paired && memcmp(info->src_addr, peerMac, 6) == 0 && pkt.arg < NUM_SWITCHES) {
        remoteInputMask |= (uint8_t)(1 << pkt.arg);  // 실제 처리는 loop()에서
      }
      break;

    case MSG_STATE:
      if (!isRole1P && paired && memcmp(info->src_addr, peerMac, 6) == 0) {
        // 같은 내용이 주기적으로 반복 송신되므로, 실제로 달라졌을 때만 다시 그린다
        if (ui.phase != pkt.phase || ui.score1 != pkt.score1 ||
            ui.score2 != pkt.score2 || ui.info != pkt.info) {
          ui.phase = pkt.phase;
          ui.score1 = pkt.score1;
          ui.score2 = pkt.score2;
          ui.info = pkt.info;
          lcdDirty = true;
        }
      }
      break;

    case MSG_SFX:
      // soundPlay()는 큐에 넣고 바로 리턴하므로 콜백 안에서 불러도 안전하다(블로킹 없음).
      if (!isRole1P && paired && memcmp(info->src_addr, peerMac, 6) == 0 && pkt.arg < NUM_SFX) {
        soundPlay(pkt.arg);
      }
      break;
  }
}

// ---------------------------------------------------------------------------
// 사운드 (1P가 판정하고 양쪽에서 동시에 낸다)
// ---------------------------------------------------------------------------
void playSfx(uint8_t id) {
  soundPlay(id);
  if (isRole1P && paired) {
    packet_t pkt = {};
    pkt.type = MSG_SFX;
    pkt.role = 1;
    pkt.arg = id;
    sendPacket(peerMac, pkt);
  }
}

// ---------------------------------------------------------------------------
// 게임 진행 (1P 전용)
// ---------------------------------------------------------------------------
void publishUi(uint8_t p, uint8_t info) {
  ui.phase = p;
  ui.score1 = score[0];
  ui.score2 = score[1];
  ui.info = info;
  lcdDirty = true;
  sendState();
}

void enterPhase(uint8_t p, uint8_t info) {
  phase = p;
  phaseStartMs = millis();
  publishUi(p, info);
  stripDirty = true;
}

void resetMatch() {
  score[0] = 0;
  score[1] = 0;
  readyMask = 0;
  serveFaults = 0;
  serving = false;
  enterPhase(PHASE_READY, 0);
}

// 중앙에서 serveTarget 쪽으로 느린 공을 발사한다.
void launchServe() {
  ballPos = FIELD_START + FIELD_LEN / 2.0f;
  ballPrevPos = ballPos;
  ballSpeed = BALL_SPEED_SERVE;
  ballDir = (serveTarget == 0) ? -1 : +1;
  serving = true;
  lastPhysicsUs = micros();
  playSfx(SFX_SERVE);
  enterPhase(PHASE_PLAY, 1);
}

// loser가 한 점을 잃는다(= 상대가 한 점을 얻는다).
void concedePoint(uint8_t loser, bool byFault) {
  uint8_t winner = 1 - loser;
  score[winner]++;
  flashPlayer = loser;
  pointByFault = byFault;
  serveFaults = 0;
  serving = false;

  playSfx(SFX_POINT);

  if (score[winner] >= MATCH_POINT) {
    playSfx(SFX_WIN);
    enterPhase(PHASE_MATCH_END, winner + 1);
    return;
  }

  // 랠리에서 졌으면 진 사람에게 서브, 서브를 두 번 놓쳐서 잃은 점수면 반대편으로 서브.
  serveTarget = byFault ? winner : loser;
  enterPhase(PHASE_POINT, (uint8_t)((winner + 1) | (byFault ? 0x10 : 0x00)));
}

// player가 공을 놓쳤다(자기 쪽 끝을 지나쳤다).
void handleMiss(uint8_t player) {
  if (serving) {
    serveFaults++;
    if (serveFaults < 2) {
      // 첫 서브 실패: 점수 변화 없이 같은 사람에게 다시 서브
      flashPlayer = player;
      playSfx(SFX_FAULT);
      enterPhase(PHASE_FAULT, player + 1);
      return;
    }
    concedePoint(player, true);   // 두 번째 서브 실패 → 실점, 서브는 반대편으로
    return;
  }
  concedePoint(player, false);    // 랠리 중 실패 → 실점, 서브는 진 쪽으로
}

// 반사 후 나갈 속도. 규칙은 파일 상단 주석 참고.
float reflectSpeed(int8_t zone, float inSpeed) {
  float out;
  if (zone == 2) {                      // 초록: 온 속도 그대로
    out = inSpeed * REFLECT_GAIN_GREEN;
  } else if (zone == 1) {               // 노랑: 약간 빠르게
    out = inSpeed * REFLECT_GAIN_YELLOW;
  } else {                              // 빨강: 최저 속도가 노랑보다 약간 빠르고,
    out = inSpeed * REFLECT_GAIN_RED;   //       그보다 빠른 공이 왔으면 10% 가속
    if (out < REFLECT_MIN_RED) out = REFLECT_MIN_RED;
  }
  if (out > BALL_SPEED_MAX) out = BALL_SPEED_MAX;
  return out;
}

// 1P/2P 어느 쪽 입력이든 여기로 모인다(1P에서만 호출된다).
void handleGamePress(uint8_t player) {
  switch (phase) {
    case PHASE_READY:
      readyMask |= (uint8_t)(1 << player);
      if (readyMask == 0x03) {
        playSfx(SFX_TICK);
        enterPhase(PHASE_OK, 0);
      } else {
        publishUi(PHASE_READY, readyMask);
      }
      return;

    case PHASE_MATCH_END:
      if (millis() - phaseStartMs >= MATCH_END_LOCK_MS) resetMatch();
      return;

    case PHASE_PLAY:
      break;      // 아래에서 반사 판정

    default:
      return;     // 카운트다운/연출 중에는 입력을 받지 않는다
  }

  // 공이 자기 쪽으로 오는 중이어야 한다.
  // (방금 반사시킨 공이 나가는 동안 또 눌러서 되돌리는 것을 막는다)
  int8_t approachDir = (player == 0) ? -1 : +1;
  if (ballDir != approachDir) return;

  int8_t zone = zoneOfDepth(depthFromPlayer(player, (int16_t)floorf(ballPos)));
  if (zone < 0) return;   // 타격 영역 밖에서 누름 - 헛스윙

  ballSpeed = reflectSpeed(zone, ballSpeed);
  ballDir = -ballDir;
  if (serving) {
    serving = false;      // 서브를 받아냈다 - 이제부터 랠리
    serveFaults = 0;
    publishUi(PHASE_PLAY, 0);
  }
  playSfx(zone == 2 ? SFX_BOUNCE : SFX_BOUNCE_FAST);
}

void updateGame(uint32_t now) {
  switch (phase) {
    case PHASE_OK:
      if (now - phaseStartMs >= OK_MS) {
        countdownStep = 0;
        playSfx(SFX_TICK);
        enterPhase(PHASE_COUNTDOWN, 3);
      }
      break;

    case PHASE_COUNTDOWN: {
      uint8_t step = (uint8_t)((now - phaseStartMs) / COUNTDOWN_STEP_MS);
      if (step >= COUNTDOWN_STEPS) {
        playSfx(SFX_START);
        // 첫 서브 방향은 랜덤
        serveTarget = (uint8_t)random(2);
        serveFaults = 0;
        enterPhase(PHASE_START, 0);
        break;
      }
      if (step != countdownStep) {
        countdownStep = step;
        stripDirty = true;                       // 코트 색이 바뀌는 스텝
        uint8_t digit = 3 - (step / 2);          // 0,1 → 3 / 2,3 → 2 / 4 → 1
        if (step % 2 == 0) playSfx(SFX_TICK);    // 불이 켜지는 스텝에서만 소리
        publishUi(PHASE_COUNTDOWN, digit);
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

      if (ballPos < FIELD_START)     handleMiss(0);
      else if (ballPos > FIELD_END)  handleMiss(1);
      else                           stripDirty = true;
      break;
    }

    case PHASE_FAULT:
      if (now - phaseStartMs >= FLASH_MS) {
        serveTarget = flashPlayer;   // 같은 사람에게 다시 서브
        launchServe();
      }
      break;

    case PHASE_POINT:
      if (now - phaseStartMs >= FLASH_MS) launchServe();
      break;

    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// 렌더링 (1P 전용 - 라인 LED는 1P에만 물려 있다)
// ---------------------------------------------------------------------------
void clearField() {
  for (int16_t i = FIELD_START; i <= FIELD_END; i++) strip.setPixelColor(i, 0);
}

void fillField(uint32_t color) {
  uint32_t c = scaleColor(color, FULL_SCALE);
  for (int16_t i = FIELD_START; i <= FIELD_END; i++) strip.setPixelColor(i, c);
}

// 양쪽 플레이어의 타격 영역(빨강 1 / 노랑 3 / 초록 6)을 그린다.
void drawZones() {
  for (uint8_t p = 0; p < 2; p++) {
    for (int16_t d = 0; d < ZONE_LEN; d++) {
      int16_t idx = (p == 0) ? (FIELD_START + d) : (FIELD_END - d);
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

// 한 플레이어의 타격 영역만 한 색으로 채운다(서브 폴트/더블 폴트 연출).
void fillZone(uint8_t player, uint32_t color) {
  uint32_t c = scaleColor(color, ZONE_SCALE);
  for (int16_t d = 0; d < ZONE_LEN; d++) {
    int16_t idx = (player == 0) ? (FIELD_START + d) : (FIELD_END - d);
    strip.setPixelColor(idx, c);
  }
}

void renderField() {
  clearField();

  switch (phase) {
    case PHASE_READY:
    case PHASE_OK:
      drawZones();   // 자기 영역이 어디인지 미리 보여준다
      break;

    case PHASE_COUNTDOWN:
      // 초록 / 꺼짐 / 초록 / 꺼짐 / 빨강 을 1초씩
      if (countdownStep == 4)            fillField(colRed());
      else if (countdownStep % 2 == 0)   fillField(colGreen());
      break;

    case PHASE_START:
      break;         // START 표시 동안 코트는 비운다

    case PHASE_PLAY:
      drawZones();
      drawBall();
      break;

    case PHASE_FAULT:
      drawZones();
      fillZone(flashPlayer, colOrange());   // 첫 서브를 놓친 쪽 영역을 주황으로
      break;

    case PHASE_POINT:
      if (pointByFault) {
        drawZones();
        fillZone(flashPlayer, colRed());    // 더블 폴트: 그 영역만 빨강
      } else {
        drawHalves(1 - flashPlayer);        // 랠리 실점: 코트를 반으로 갈라 초록/빨강
      }
      break;

    case PHASE_MATCH_END: {
      uint8_t winner = (ui.info == 1) ? 0 : 1;
      drawHalves(winner);
      break;
    }
  }
}

// ---------------------------------------------------------------------------
// 스위치 LED (양쪽 보드 각자 자기 것만)
// ---------------------------------------------------------------------------
// Pong의 스위치는 전부 흰색이다. 평소에는 아주 은은하게 켜 두고, 누르면 잠깐 밝아진다.
void renderSwitchLeds(uint32_t now) {
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    bool bright = (switchLedOffAt[i] != 0 && now < switchLedOffAt[i]);
    if (!bright && switchLedOffAt[i] != 0) switchLedOffAt[i] = 0;
    strip.setPixelColor(i, scaleColor(colWhite(), bright ? 1.0f : SWITCH_IDLE));
  }
}

// ---------------------------------------------------------------------------
// LCD (양쪽 보드가 같은 화면을 그린다)
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

// 점수판: 1P는 노랑, 2P는 자홍으로 구분한다(코트 양 끝 색과 무관하게 두 사람을 구별하는 용도)
void drawScoreBoard() {
  display.setTextSize(2);
  display.setTextColor(ST77XX_YELLOW);
  display.setCursor(22, 2);
  display.print("1P");
  display.setTextColor(ST77XX_MAGENTA);
  display.setCursor(114, 2);
  display.print("2P");

  display.setTextSize(6);   // 숫자 한 자 36x48px
  display.setTextColor(ST77XX_YELLOW);
  display.setCursor(16, 30);
  display.print(ui.score1);
  display.setTextColor(ST77XX_MAGENTA);
  display.setCursor(108, 30);
  display.print(ui.score2);

  display.setTextSize(2);
  display.drawFastVLine(80, 22, 64, LCD_GREY);
}

void drawUi() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(ST77XX_WHITE);

  switch (ui.phase) {
    case PHASE_READY:
      display.setTextColor(ST77XX_WHITE);
      printCentered("READY?", 4, 24);

      display.setTextSize(2);
      display.setCursor(12, 88);
      display.setTextColor((ui.info & 0x01) ? ST77XX_GREEN : LCD_GREY);
      display.print("1P:");
      display.print((ui.info & 0x01) ? "OK" : "--");
      display.setCursor(92, 88);
      display.setTextColor((ui.info & 0x02) ? ST77XX_GREEN : LCD_GREY);
      display.print("2P:");
      display.print((ui.info & 0x02) ? "OK" : "--");
      break;

    case PHASE_OK:
      display.setTextColor(ST77XX_GREEN);
      printCentered("OK!", 6, 40);
      break;

    case PHASE_COUNTDOWN: {
      // 카운트다운 숫자는 멀리서도 보이게 화면을 거의 채운다(크기 12 = 72x96px)
      char buf[2] = {(char)('0' + ui.info), '\0'};
      display.setTextColor(ui.info == 1 ? ST77XX_RED : ST77XX_GREEN);
      printCentered(buf, 12, 16);
      break;
    }

    case PHASE_START:
      display.setTextColor(ST77XX_GREEN);
      printCentered("START", 4, 48);
      break;

    case PHASE_PLAY:
      drawScoreBoard();
      display.setCursor(0, 104);
      display.setTextColor(ui.info ? ST77XX_CYAN : ST77XX_WHITE);
      display.print(ui.info ? "SERVE" : "RALLY");
      break;

    case PHASE_FAULT:
      drawScoreBoard();
      display.setCursor(0, 104);
      display.setTextColor(ST77XX_ORANGE);
      display.print(ui.info == 1 ? "1P" : "2P");
      display.print(" FAULT!");
      break;

    case PHASE_POINT:
      drawScoreBoard();
      display.setCursor(0, 104);
      display.setTextColor(ST77XX_RED);
      display.print((ui.info & 0x0F) == 1 ? "1P" : "2P");
      display.print((ui.info & 0x10) ? " +1 DF" : " POINT!");
      break;

    case PHASE_MATCH_END:
      display.setTextColor(ST77XX_WHITE);
      display.setCursor(0, 0);
      display.print(ui.score1);
      display.print(" - ");
      display.print(ui.score2);

      display.setTextColor(ui.info == 1 ? ST77XX_YELLOW : ST77XX_MAGENTA);
      printCentered(ui.info == 1 ? "1P WIN" : "2P WIN", 4, 40);

      display.setTextSize(2);
      display.setTextColor(LCD_GREY);
      display.setCursor(0, 104);
      display.print("PRESS TO RETRY");
      break;
  }
  display.display();
}

void drawBootScreen() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(ST77XX_CYAN);
  display.setCursor(0, 0);
  display.print("1D PONG 2P");

  // 역할은 부팅 순간 가장 중요한 정보라 크게(크기 5 = 30x40px)
  display.setTextSize(5);
  display.setTextColor(isRole1P ? ST77XX_YELLOW : ST77XX_MAGENTA);
  display.setCursor(56, 44);
  display.print(isRole1P ? "1P" : "2P");

  display.setTextSize(2);
  display.setTextColor(ST77XX_WHITE);
  display.setCursor(0, 104);
  display.print("BOOTING...");
  display.display();
}

void drawSearchingScreen() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(ST77XX_WHITE);
  display.setCursor(0, 0);
  display.print("PONG / ");
  display.setTextColor(isRole1P ? ST77XX_YELLOW : ST77XX_MAGENTA);
  display.print(isRole1P ? "1P" : "2P");
  display.drawFastHLine(0, 24, LCD_W, LCD_GREY);

  display.setCursor(0, 52);
  if (roleMismatchDetected && (millis() - roleMismatchAtMs < 1500)) {
    display.setTextColor(ST77XX_RED);   // 두 보드가 같은 역할 - 한쪽 5번 토글을 바꿔야 한다
    display.print("ROLE");
    display.setCursor(0, 74);
    display.print("MISMATCH");
  } else {
    display.setTextColor(ST77XX_YELLOW);
    display.print("SEARCHING");
    uint8_t dots = (millis() / 400) % 4;
    for (uint8_t i = 0; i < dots; i++) display.print(".");
  }

  display.setTextColor(ST77XX_WHITE);
  display.setCursor(0, 106);
  display.print("CH:");
  display.print(ESPNOW_CHANNEL);
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

// 이 보드에서 스위치를 눌렀을 때. 스위치 LED는 각자 자기 것만 켠다.
void handleLocalPress(uint8_t idx, uint32_t now) {
  switchLedOffAt[idx] = now + SWITCH_FLASH_MS;
  stripDirty = true;

  if (!paired) return;           // 2인 전용이라 페어링 전에는 게임이 시작되지 않는다

  if (isRole1P) {
    handleGamePress(0);          // 1P는 자기 입력을 바로 게임에 넣는다
  } else {
    packet_t pkt = {};           // 2P는 1P에 전달만 한다(판정도 소리도 1P가 결정)
    pkt.type = MSG_INPUT;
    pkt.role = 2;
    pkt.arg = idx;
    sendPacket(peerMac, pkt);
  }
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
  pinMode(MODE_SWITCH_PIN, INPUT_PULLUP);
  isRole1P = (digitalRead(MODE_SWITCH_PIN) == HIGH);  // 토글 위치가 역할을 정한다

  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    pinMode(SWITCH_PINS[i], INPUT_PULLUP);  // 눌리면 GND로 연결되는 배선이라 내부 풀업 사용
  }

  // 2P 보드에는 자기 스위치 LED 4개만 물려 있다(라인 LED는 1P 보드 쪽 체인). 여기서 180개를
  // 다 내보내면 없는 픽셀에 데이터를 흘리며 show() 시간만 길어지므로(약 5.4ms → 0.12ms) 4개로 줄인다.
  strip.updateLength(isRole1P ? NUM_LEDS_1P : NUM_LEDS_2P);
  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  strip.clear();
  safeShow();

  display.begin();
  display.setTextSize(2);
  display.setTextColor(ST77XX_WHITE);
  drawBootScreen();

  bootAnimation();  // 인터럽트를 걸기 전에 실행 - 초기화 도중 스위치 신호로 오작동하는 것을 방지

  soundEngineBegin(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN, GAME_SOUNDS, NUM_SFX);

  randomSeed(esp_random());   // 첫 서브 방향용

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_ps(WIFI_PS_NONE);  // 모뎀 슬립 끔: 입력 전달 지연 스파이크 방지

  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(OnDataRecv);
    addPeer(BROADCAST_MAC);  // 페어링 전에는 브로드캐스트로 서로를 찾는다
  }

  phase = PHASE_READY;
  phaseStartMs = millis();
  lastPhysicsUs = micros();

  // 인터럽트는 다른 초기화가 모두 끝난 뒤 마지막에 붙인다
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    attachInterruptArg(SWITCH_PINS[i], handleSwitchInterrupt, (void*)(uintptr_t)i, CHANGE);
  }
}

void loop() {
  uint32_t now = millis();

  // 페어링 전에는 주기적으로 브로드캐스트를 뿌려 상대를 찾는다
  if (!paired && now - lastHelloMs >= HELLO_INTERVAL_MS) {
    lastHelloMs = now;
    packet_t hello = {};
    hello.type = MSG_HELLO;
    hello.role = isRole1P ? 1 : 2;
    sendPacket(BROADCAST_MAC, hello);
  }

  // 로컬 스위치 입력
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    if (checkPending[i] && (now - lastChangeMs[i] >= DEBOUNCE_MS)) {
      checkPending[i] = false;

      bool pressed = (digitalRead(SWITCH_PINS[i]) == LOW);  // 풀업 배선이라 눌리면 LOW
      if (pressed == switchPressed[i]) continue;            // 디바운스 후에도 실제로 바뀐 경우만
      switchPressed[i] = pressed;
      if (!pressed) continue;                               // 누를 때만 반응

      handleLocalPress(i, now);
    }
  }

  // 2P에서 넘어온 입력(수신 콜백이 표시만 남겨둔 것)을 1P가 처리한다.
  // 먼저 통째로 가져와 비워야 처리하는 사이에 들어온 입력을 덮어쓰지 않는다.
  uint8_t pending = remoteInputMask;
  if (pending) {
    remoteInputMask &= (uint8_t)~pending;
    if (isRole1P) {
      // 거의 동시에 여러 개를 눌러도 게임에는 "2P가 쳤다" 한 번으로 들어간다
      handleGamePress(1);
    }
  }

  if (isRole1P && paired) {
    updateGame(now);

    // 상태 반복 송신(패킷 유실 대비). 단계가 바뀔 때는 enterPhase에서 이미 즉시 보냈다.
    if (now - lastStateMs >= STATE_INTERVAL_MS) sendState();
  }

  // 스트립 갱신. 1P는 매 프레임 코트를 다시 그리고, 2P는 스위치 LED만 바뀔 때 갱신한다.
  if (isRole1P) {
    if (now - lastFrameMs >= FRAME_MS) {
      lastFrameMs = now;
      if (paired) renderField();
      else        clearField();
      renderSwitchLeds(now);
      safeShow();
      stripDirty = false;
    }
  } else if (stripDirty || now - lastFrameMs >= FRAME_MS) {
    lastFrameMs = now;
    renderSwitchLeds(now);
    safeShow();
    stripDirty = false;
  }

  // LCD는 한 장 전송에 약 14ms가 걸린다. 코트를 60FPS로 그리는 중에 매번 부르면 공이 눈에 띄게
  // 끊기므로, 페어링 후에는 화면 내용이 실제로 바뀐 순간에만 그린다.
  // (랠리 중에는 점수도 단계도 안 바뀌어서 전송이 아예 일어나지 않는다)
  static uint32_t lastLcdMs = 0;
  if (!paired) {
    if (now - lastLcdMs >= LCD_SEARCH_MS) {   // 점 애니메이션이 있어 주기적으로 다시 그린다
      lastLcdMs = now;
      drawSearchingScreen();
    }
  } else if (lcdDirty) {
    lcdDirty = false;
    drawUi();
  }
}
