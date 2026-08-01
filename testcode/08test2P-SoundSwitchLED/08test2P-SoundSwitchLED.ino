/*
  ===========================================================================
  1P/2P 2인 연동 테스트 - 스위치 + 라인 LED + 사운드
  ===========================================================================
  06test_espnow의 ESP-NOW 페어링과 05test_wavheader의 사운드 엔진을 합쳐서,
  두 대의 보드가 스위치 입력을 공유하며 같은 색과 같은 소리로 반응하게 만든 테스트다.

  ---------------------------------------------------------------------------
  역할 분담
  ---------------------------------------------------------------------------
  부팅 시 5번 토글 스위치(GPIO18)를 읽어 역할이 정해진다(HIGH = 1P, LOW = 2P).
  두 보드는 반드시 서로 다른 역할이어야 하며, 같으면 LCD에 ROLE MISMATCH가 뜬다.

    - 1P: 라인 LED 전체(스위치 LED를 제외한 172개)를 담당한다. 자기 스위치 입력이든
          2P에서 넘어온 입력이든, 그 스위치 색으로 라인 전체를 빛낸다.
          그리고 자기 입력이 생기면 2P에게 사운드 재생 신호를 보낸다.
    - 2P: 자기 스위치에 붙은 LED 1~4번과 자기 쪽 사운드 재생을 담당한다.
          라인 LED는 1P에만 물려 있으므로 2P는 건드리지 않는다.

  즉 어느 쪽에서 스위치를 누르든:
    · 누른 사람의 보드에서 그 스위치 LED가 켜진다(스위치 LED는 각자 자기 것만).
    · 1P의 라인 LED 전체가 그 스위치 색으로 빛난다(누가 눌렀든 동일).
    · 1P와 2P 양쪽에서 같은 소리가 난다(스위치별로 정해진 소리, 아래 SWITCH_SOUNDS).

  주고받는 메시지는 "몇 번 스위치가 눌렸다" 하나뿐이다. 스위치 번호 → 색/소리 매핑은
  두 보드가 같은 표를 갖고 있으므로, 색이나 소리 데이터를 실어 보낼 필요가 없다.

  ---------------------------------------------------------------------------
  사운드
  ---------------------------------------------------------------------------
  이 스케치는 스위치 4개에 대응하는 소리 4개만 쓴다. sounds/ 폴더에는 헤더가 전부
  복사되어 있지만, 아래에서 include한 4개만 실행 이미지에 들어간다(나머지는 링커가
  떼어내므로 플래시를 차지하지 않는다). 소리를 바꾸려면 include와 SWITCH_SOUNDS만
  고치면 된다.

  소리를 추가/교체하는 방법:
    1) 프로젝트 루트 sounds/wav/ 에 wav를 넣거나 교체한다.
    2) 루트 sounds/ 폴더에서 `py wav2header.py` 를 실행한다(이 스케치의 sounds/ 폴더로
       자동 복사된다. 스크립트의 COPY_TO 목록에 이 폴더가 있다).
    3) 필요하면 아래 include와 SWITCH_SOUNDS를 고치고 재컴파일한다.

  ---------------------------------------------------------------------------
  보드 설정 (Tools 메뉴) - 두 보드 모두 동일하게
  ---------------------------------------------------------------------------
    - Board: ESP32S3 Dev Module
    - Partition Scheme: "Huge APP (3MB No OTA/1MB SPIFFS)" 권장.
      ★ 예전 LittleFS 테스트 스케치를 올린 뒤라면 "Custom"이 선택된 상태일 수 있는데,
        이 폴더에는 partitions.csv가 없어서 그대로 컴파일하면 빌드가 실패한다.
        반드시 Custom이 아닌 항목으로 바꿀 것.
    - PSRAM: 켜든 끄든 무관하다(이 스케치는 PSRAM을 쓰지 않는다).
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

// 이 스케치가 쓰는 소리만 골라 include한다. 여기 없는 소리는 플래시를 차지하지 않는다.
// (사운드 헤더는 PCM 배열 정의를 담고 있어서 여러 파일에서 include하면 복사본이 생긴다.
//  그래서 스케치 전체에서 이 .ino 한 곳에서만 include한다 - SoundEngine.h 주석 참고)
#include "sounds/laser.h"
#include "sounds/pickupCoin01.h"
#include "sounds/blip01.h"
#include "sounds/powerUp.h"

#define LED_PIN         8    // WS2815 데이터 핀(readme.md 배선 기준, 프로토콜은 WS2812 호환)
#define NUM_LEDS_TOTAL  176  // 스위치 내장 LED 4개 + 라인 LED 172개 (1P 기준)
#define NUM_SWITCH_LEDS 4    // index 0~3: 스위치 1~4 내장 LED
#define LINE_START      NUM_SWITCH_LEDS  // 라인 LED 첫 칸 = 4

// 라인 LED 172개를 한꺼번에 켜기 때문에 밝기를 보수적으로 잡았다.
// WS2815는 12V에서 픽셀당 약 36mA(RGB 전부 점등)까지 먹으므로, 172개를 풀 밝기로 켜면
// 6A를 넘어 전원과 스트립 양쪽에 부담이 된다. 아래 두 값을 곱한 만큼만 흐르게 해서
// (BRIGHTNESS/255 x LINE_LED_SCALE ≈ 12%) 1A 아래로 눌러 두었다.
// 실제 전원 용량을 확인한 뒤 필요하면 올리되, 흰색(스위치 4번)이 가장 많이 먹는다는 점을 감안할 것.
#define BRIGHTNESS       120   // 스트립 전체 밝기
#define LINE_LED_SCALE   0.25f // 라인 LED에만 추가로 적용하는 감쇠(스위치 LED는 원색 그대로)

#define DEBOUNCE_MS        30   // 스위치 채터링(짧은 시간 내 여러 번 튀는 신호) 무시 시간
#define SWITCH_FLASH_MS    300  // 누른 스위치 LED를 켜 두는 시간
#define LINE_FLASH_MS      400  // 라인 LED를 켜 두는 시간(스위치보다 살짝 길게 여운을 준다)
#define BOOT_STEP_MS       150  // 부팅 시 스위치 LED를 하나씩 확인하는 간격

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

#define I2S_BCLK_PIN  42  // I2S 비트클럭 (MAX98357A BCLK)
#define I2S_LRC_PIN   41  // I2S 워드셀렉트/좌우채널클럭 (MAX98357A LRCLK)
#define I2S_DOUT_PIN  40  // I2S 데이터 출력 (MAX98357A DIN)

#define MODE_SWITCH_PIN     18   // 5번 토글 스위치: HIGH = 1P, LOW = 2P
#define HELLO_INTERVAL_MS   500  // 페어링 전 브로드캐스트 주기
#define LCD_REFRESH_MS      200  // LCD 갱신 주기(매 loop마다 그리면 SPI 전송이 낭비된다)

// 두 보드가 반드시 같은 채널을 써야 서로 보인다.
// TODO: 추후 설정 화면(LED 밝기/볼륨과 함께)에서 NVS에 저장된 값으로 대체.
#define ESPNOW_CHANNEL      1

// 인덱스 = 스위치 LED 위치(0~3). 그 자리의 스위치가 물려 있는 GPIO를 적는다.
// 3번/4번 자리의 배선이 GPIO 번호 순서와 반대라 17과 16을 바꿔 넣었다.
const uint8_t SWITCH_PINS[] = {7, 15, 17, 16};
const uint8_t NUM_SWITCHES = sizeof(SWITCH_PINS) / sizeof(SWITCH_PINS[0]);

const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// 스위치 1~4에 대응하는 소리. 두 보드가 같은 표를 갖고 있으므로 스위치 번호만 주고받으면
// 양쪽에서 같은 소리가 난다. soundPlay()의 인자가 곧 이 배열의 인덱스다.
static const SoundClip* const SWITCH_SOUNDS[NUM_SWITCHES] = {
  &CLIP_laser,         // 스위치 1 (빨강)
  &CLIP_pickupCoin01,  // 스위치 2 (초록)
  &CLIP_blip01,        // 스위치 3 (파랑)
  &CLIP_powerUp        // 스위치 4 (흰색)
};

// 색 순서는 RGB다. 데이터시트상 WS2815는 GRB지만 실제 스트립은 R과 G가 반대로 나온다
// (Color(255,0,0)이 초록으로 점등). 스트립을 교체하면 여기부터 확인할 것.
Adafruit_NeoPixel strip(NUM_LEDS_TOTAL, LED_PIN, NEO_RGB + NEO_KHZ800);

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
  // 40KB(160x128x2) 전송이라 24MHz에서 약 14ms 걸린다. 매 loop마다 부르지 말 것.
  void display() { tft.drawRGBBitmap(0, 0, getBuffer(), LCD_W, LCD_H); }
};

Lcd display;

// 스위치 1~4에 대응하는 LCD 표시색(라인 LED 색과 같은 순서)
const uint16_t SWITCH_TEXT_COLORS[4] = {ST77XX_RED, ST77XX_GREEN, ST77XX_BLUE, ST77XX_WHITE};

enum MsgType : uint8_t {
  MSG_HELLO     = 1,
  MSG_HELLO_ACK = 2,
  MSG_PRESS     = 3,  // 양방향: "이 스위치가 눌렸다"
};

typedef struct __attribute__((packed)) {
  uint8_t type;
  uint8_t role;       // 1 = 1P, 2 = 2P
  uint8_t switchIdx;  // 0~3
} packet_t;

bool isRole1P = false;
bool paired = false;
uint8_t peerMac[6] = {0};
uint32_t lastHelloMs = 0;
bool roleMismatchDetected = false;
uint32_t roleMismatchAtMs = 0;
uint32_t pairedAtMs = 0;

// ESP-NOW 수신 콜백은 WiFi 태스크에서 실행되므로, 시간이 걸리는 LED 갱신을 그 안에서
// 하지 않는다. 여기에 표시만 남기고 실제 처리는 loop()에서 한다.
// 스위치 번호 하나만 담아두면 상대가 여러 개를 거의 동시에 눌렀을 때 뒤엣것이 앞엣것을
// 덮어써 소리가 빠진다. 그래서 비트마스크(비트 0~3 = 스위치 1~4)로 받아 전부 처리한다.
volatile uint8_t remotePressMask = 0;

// 아래 2개 배열은 ISR이 쓰고 loop()가 읽는 값이라 volatile 필요
volatile bool checkPending[NUM_SWITCHES] = {false};
volatile uint32_t lastChangeMs[NUM_SWITCHES] = {0};
bool switchPressed[NUM_SWITCHES] = {false};   // 디바운스까지 끝난 확정 상태(loop에서만 접근)

bool switchLedOn[NUM_SWITCHES] = {false};
uint32_t switchLedOffAt[NUM_SWITCHES] = {0};
bool lineLedOn = false;
uint32_t lineLedOffAt = 0;

uint8_t lastLocalSwitch = 0;    // 마지막으로 이 보드에서 누른 스위치 번호(1~4), 0 = 없음
uint8_t lastRemoteSwitch = 0;   // 마지막으로 상대 보드에서 눌린 스위치 번호(1~4), 0 = 없음
uint16_t pressCount = 0;        // 이 보드가 처리한 입력 총 횟수(로컬 + 원격)
bool stripDirty = false;        // 이번 loop에서 strip.show()를 해야 하는지

// 스위치 번호(0~3)에 대응하는 색. 라인 LED도 이 색으로 빛난다.
uint32_t switchColor(uint8_t idx) {
  switch (idx) {
    case 0: return strip.Color(255, 0, 0);       // R
    case 1: return strip.Color(0, 255, 0);       // G
    case 2: return strip.Color(0, 0, 255);       // B
    case 3: return strip.Color(255, 255, 255);   // W
    default: return strip.Color(0, 0, 0);
  }
}

// 색의 각 성분에 배율을 곱한다. setBrightness()는 스트립 전체에 걸리기 때문에,
// 라인 LED만 따로 어둡게 하려면 이렇게 색 값 자체를 낮춰야 한다.
uint32_t scaleColor(uint32_t c, float f) {
  uint8_t r = (uint8_t)(((c >> 16) & 0xFF) * f);
  uint8_t g = (uint8_t)(((c >> 8) & 0xFF) * f);
  uint8_t b = (uint8_t)((c & 0xFF) * f);
  return strip.Color(r, g, b);
}

// 스위치 핀 레벨이 바뀔 때(CHANGE) 호출되는 ISR.
// ISR은 짧게 끝내야 하므로 시각 기록 + 플래그 세팅만 하고, 디바운스 판정과
// LED/사운드/통신 처리는 loop()에 위임한다.
void IRAM_ATTR handleSwitchInterrupt(void* arg) {
  uint8_t idx = (uint8_t)(uintptr_t)arg;
  lastChangeMs[idx] = millis();
  checkPending[idx] = true;
}

void safeShow() {
  strip.show();
  delayMicroseconds(300);  // WS2815 Reset Time (>=280us) 보장
}

// ---- ESP-NOW ----

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
      }
      break;

    case MSG_PRESS:
      // 페어링된 상대가 보낸 것만 받아들인다(다른 보드의 브로드캐스트에 반응하지 않도록)
      if (paired && memcmp(info->src_addr, peerMac, 6) == 0 && pkt.switchIdx < NUM_SWITCHES) {
        remotePressMask |= (uint8_t)(1 << pkt.switchIdx);  // 실제 처리는 loop()에서
      }
      break;
  }
}

// ---- 입력 처리 ----

// 라인 LED 전체를 한 색으로 채운다. 1P에만 라인 LED가 물려 있으므로 1P에서만 의미가 있다.
void setLineColor(uint32_t color) {
  uint32_t scaled = scaleColor(color, LINE_LED_SCALE);
  for (uint16_t i = LINE_START; i < NUM_LEDS_TOTAL; i++) {
    strip.setPixelColor(i, scaled);
  }
}

// 이 보드에서 스위치를 눌렀을 때. 자기 스위치 LED를 켜고, 소리를 내고,
// 1P라면 라인 LED까지 켠 뒤 상대에게 알린다.
void handleLocalPress(uint8_t idx, uint32_t now) {
  lastLocalSwitch = idx + 1;
  pressCount++;

  strip.setPixelColor(idx, switchColor(idx));
  switchLedOn[idx] = true;
  switchLedOffAt[idx] = now + SWITCH_FLASH_MS;

  if (isRole1P) {
    setLineColor(switchColor(idx));
    lineLedOn = true;
    lineLedOffAt = now + LINE_FLASH_MS;
  }
  stripDirty = true;

  soundPlay(idx);  // 큐에 넣고 즉시 리턴(논블로킹) - 실제 재생은 core 0의 오디오 태스크가 처리

  if (paired) {
    packet_t pkt = {};
    pkt.type = MSG_PRESS;
    pkt.role = isRole1P ? 1 : 2;
    pkt.switchIdx = idx;
    sendPacket(peerMac, pkt);
  }
}

// 상대 보드에서 스위치가 눌렸을 때. 소리는 양쪽이 같이 내고,
// 라인 LED는 1P만 켠다(스위치 LED는 누른 사람 쪽에서만 켜지므로 여기서 건드리지 않는다).
void handleRemotePress(uint8_t idx, uint32_t now) {
  lastRemoteSwitch = idx + 1;
  pressCount++;

  if (isRole1P) {
    setLineColor(switchColor(idx));
    lineLedOn = true;
    lineLedOffAt = now + LINE_FLASH_MS;
    stripDirty = true;
  }

  soundPlay(idx);
}

// ---- LCD ----

void drawBootScreen() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(ST77XX_CYAN);
  display.setCursor(0, 0);
  display.print("1D ARCADE");

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
  display.print("ROLE: ");
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

void drawPairedScreen() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(ST77XX_WHITE);
  display.setCursor(0, 0);
  display.print("ROLE: ");
  display.print(isRole1P ? "1P" : "2P");
  display.drawFastHLine(0, 24, LCD_W, LCD_GREY);

  display.setTextSize(3);
  display.setTextColor(ST77XX_GREEN);
  display.setCursor(6, 50);
  display.print("PAIRED!");

  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X%02X%02X%02X%02X%02X",
           peerMac[0], peerMac[1], peerMac[2], peerMac[3], peerMac[4], peerMac[5]);
  display.setTextSize(2);
  display.setTextColor(ST77XX_CYAN);
  display.setCursor(0, 106);   // 12자 x 12px = 144px로 화면 폭에 들어간다
  display.print(macStr);
  display.display();
}

// 페어링 이후의 상태 화면.
//   1줄: 역할 + 라인 LED 담당 여부
//   2줄: 이 보드에서 마지막으로 누른 스위치 / 상대에서 넘어온 스위치
//   3줄: 처리한 입력 횟수와 지금 겹쳐 재생 중인 트랙 수
void drawStatusScreen() {
  display.clearDisplay();
  display.setTextSize(2);

  display.setTextColor(ST77XX_CYAN);
  display.setCursor(0, 0);
  display.print(isRole1P ? "1P LINE+SND" : "2P SND ONLY");
  display.drawFastHLine(0, 24, LCD_W, LCD_GREY);

  // 마지막으로 눌린 스위치는 그 스위치 색으로 표시해서 LED 색과 대조할 수 있게 한다
  display.setTextColor(ST77XX_WHITE);
  display.setCursor(0, 34);
  display.print("MINE:");
  if (lastLocalSwitch > 0) {
    display.setTextColor(SWITCH_TEXT_COLORS[lastLocalSwitch - 1]);
    display.print(lastLocalSwitch);
  } else {
    display.print("-");
  }

  display.setTextColor(ST77XX_WHITE);
  display.setCursor(0, 58);
  display.print("PEER:");
  if (lastRemoteSwitch > 0) {
    display.setTextColor(SWITCH_TEXT_COLORS[lastRemoteSwitch - 1]);
    display.print(lastRemoteSwitch);
  } else {
    display.print("-");
  }

  display.setTextColor(ST77XX_WHITE);
  display.setCursor(0, 90);
  display.print("N:");
  display.print(pressCount);
  display.print(" V:");
  display.print(soundActiveVoices());

  display.display();
}

void redrawLCD() {
  if (!paired) {
    drawSearchingScreen();
  } else if (millis() - pairedAtMs < 2000) {
    drawPairedScreen();  // 페어링 완료 후 2초간 표시
  } else {
    drawStatusScreen();
  }
}

// 부팅 시 스위치 LED 4개를 하나씩 켜 배선을 확인한다.
// 라인 LED는 전류가 커서 여기서 전체 점등은 하지 않고, 첫 입력 때 확인하면 된다.
void bootAnimation() {
  for (uint8_t i = 0; i < NUM_SWITCH_LEDS; i++) {
    strip.clear();
    strip.setPixelColor(i, switchColor(i));
    safeShow();
    delay(BOOT_STEP_MS);
  }
  strip.clear();
  safeShow();
}

void setup() {
  pinMode(MODE_SWITCH_PIN, INPUT_PULLUP);
  isRole1P = (digitalRead(MODE_SWITCH_PIN) == HIGH);  // 토글 위치가 역할을 정한다

  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    pinMode(SWITCH_PINS[i], INPUT_PULLUP);  // 눌리면 GND로 연결되는 배선이라 내부 풀업 사용
  }

  // 라인 LED는 1P에만 물려 있다. 2P에서 176개를 다 내보내면 없는 픽셀에 데이터를 흘리며
  // show() 시간만 길어지므로(약 5.3ms -> 0.12ms), 2P는 스위치 LED 4개로 줄인다.
  strip.updateLength(isRole1P ? NUM_LEDS_TOTAL : NUM_SWITCH_LEDS);
  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  strip.clear();
  safeShow();

  display.begin();
  display.setTextSize(2);
  display.setTextColor(ST77XX_WHITE);
  drawBootScreen();

  bootAnimation();  // 인터럽트를 걸기 전에 실행 - 초기화 도중 스위치 신호로 오작동하는 것을 방지

  soundEngineBegin(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN, SWITCH_SOUNDS, NUM_SWITCHES);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_ps(WIFI_PS_NONE);  // 모뎀 슬립 끔: 입력 전달 지연 스파이크 방지

  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(OnDataRecv);
    addPeer(BROADCAST_MAC);  // 페어링 전에는 브로드캐스트로 서로를 찾는다
  }

  // 인터럽트는 다른 초기화가 모두 끝난 뒤 마지막에 붙임 - 초기화 중간에 발생한
  // 핀 변화 때문에 아직 준비 안 된 자료구조(사운드 큐, ESP-NOW 등)를 건드리는 일이 없게 하려고
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

  // 로컬 스위치 입력. 여러 개를 동시에 눌렀으면 이 루프에서 각각 처리되어
  // 소리도 겹쳐 나고 상대에게도 각각 전달된다.
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    if (checkPending[i] && (now - lastChangeMs[i] >= DEBOUNCE_MS)) {
      checkPending[i] = false;

      bool pressed = (digitalRead(SWITCH_PINS[i]) == LOW);  // 풀업 배선이라 눌리면 LOW
      if (pressed == switchPressed[i]) continue;            // 디바운스 후에도 실제로 바뀐 경우만 반응
      switchPressed[i] = pressed;
      if (!pressed) continue;                               // 누를 때만 반응(뗄 때는 무시)

      handleLocalPress(i, now);
    }
  }

  // 상대 보드에서 넘어온 입력(수신 콜백이 표시만 남겨둔 것)을 여기서 처리한다.
  // 먼저 통째로 가져와 비워야, 처리하는 사이에 새로 들어온 입력을 덮어쓰지 않는다.
  uint8_t pending = remotePressMask;
  if (pending) {
    remotePressMask &= (uint8_t)~pending;
    for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
      if (pending & (1 << i)) handleRemotePress(i, now);
    }
  }

  // 시간이 다 된 LED 소등 - delay() 없이 millis() 비교만으로 논블로킹 처리
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    if (switchLedOn[i] && now >= switchLedOffAt[i]) {
      strip.setPixelColor(i, 0);
      switchLedOn[i] = false;
      stripDirty = true;
    }
  }
  if (lineLedOn && now >= lineLedOffAt) {
    setLineColor(0);
    lineLedOn = false;
    stripDirty = true;
  }

  // 스트립 전송은 이번 loop에서 바뀐 내용이 있을 때 한 번만 한다.
  // (1P는 176픽셀 전송에 약 5.3ms가 걸려서, 매 loop마다 부르면 낭비가 크다)
  if (stripDirty) {
    safeShow();
    stripDirty = false;
  }

  static uint32_t lastLcdMs = 0;
  if (now - lastLcdMs >= LCD_REFRESH_MS) {
    lastLcdMs = now;
    redrawLCD();
  }
}
