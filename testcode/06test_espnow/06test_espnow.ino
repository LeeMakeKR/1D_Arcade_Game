#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>

// ST7735 TFT LCD 핀 (RS=DC, SDA=MOSI, CLK=SCLK)
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

#define MODE_SWITCH_PIN     18  // 토글 스위치: High = 1P, Low = 2P
#define DEBOUNCE_MS         30
#define HELLO_INTERVAL_MS   500
#define LCD_REFRESH_MS      200

// TODO: 추후 설정 화면(LED 밝기/볼륨과 함께)에서 NVS에 저장된 값으로 대체.
// 두 보드가 반드시 같은 채널을 써야 서로 보인다.
#define ESPNOW_CHANNEL      1

const uint8_t INPUT_SWITCH_PINS[] = {7, 15, 16, 17};
const uint8_t NUM_INPUT_SWITCHES = sizeof(INPUT_SWITCH_PINS) / sizeof(INPUT_SWITCH_PINS[0]);

const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

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

enum MsgType : uint8_t {
  MSG_HELLO     = 1,
  MSG_HELLO_ACK = 2,
  MSG_INPUT     = 3,  // 2P -> 1P: 스위치 입력 이벤트
  MSG_STATE     = 4,  // 1P -> 2P: 점수 등 표시용 상태 + RTT 에코
};

typedef struct __attribute__((packed)) {
  uint8_t  type;
  uint8_t  role;       // 1 = 1P, 2 = 2P
  uint32_t seq;        // RTT 측정용: MSG_STATE에서는 원본 sendMs를 그대로 실어 되돌려줌
  uint32_t sendMs;
  uint8_t  switchIdx;
  uint8_t  pressed;
  uint16_t score;
} espnow_packet_t;

bool isRole1P = false;
bool paired = false;
uint8_t peerMac[6] = {0};
uint32_t lastHelloMs = 0;
uint32_t helloSeq = 0;
bool roleMismatchDetected = false;
uint32_t roleMismatchAtMs = 0;

uint16_t txCount = 0;
uint16_t rxCount = 0;
uint16_t score = 0;
uint32_t lastRttMs = 0;
uint32_t pairedAtMs = 0;   // 페어링 완료 화면을 잠시 보여주기 위한 시각
bool pairedShown = false;  // PAIRED! 화면을 이미 지나갔는지

uint8_t lastRemoteSwitchIdx = 255;  // 2P에서 마지막으로 들어온 스위치 번호(0~3), 255 = 아직 없음
uint32_t lastRemoteSwitchAtMs = 0;  // 마지막 2P 입력이 들어온 시각

volatile bool switchPressed[4] = {false, false, false, false};
volatile bool checkPending[4] = {false, false, false, false};
volatile uint32_t lastChangeMs[4] = {0, 0, 0, 0};

bool isExpectedPeerRole(uint8_t remoteRole) {
  return isRole1P ? (remoteRole == 2) : (remoteRole == 1);
}

bool isSameMac(const uint8_t *a, const uint8_t *b) {
  return memcmp(a, b, 6) == 0;
}

void addPeer(const uint8_t *mac) {
  if (esp_now_is_peer_exist(mac)) return;
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, mac, 6);
  peerInfo.channel = ESPNOW_CHANNEL;
  peerInfo.ifidx = WIFI_IF_STA;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
}

void sendPacket(const uint8_t *mac, espnow_packet_t &pkt) {
  esp_now_send(mac, (uint8_t *)&pkt, sizeof(pkt));
  txCount++;
}

void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  if (status != ESP_NOW_SEND_SUCCESS) {
    Serial.println("send fail");
  }
}

void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(espnow_packet_t)) return;
  espnow_packet_t pkt;
  memcpy(&pkt, data, sizeof(pkt));
  rxCount++;

  switch (pkt.type) {
    case MSG_HELLO:
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

        espnow_packet_t ack = {};
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

    case MSG_INPUT:
      if (isRole1P && paired && isSameMac(info->src_addr, peerMac) && pkt.role == 2) {
        lastRemoteSwitchIdx = pkt.switchIdx;
        lastRemoteSwitchAtMs = millis();

        score++;
        espnow_packet_t state = {};
        state.type = MSG_STATE;
        state.role = 1;
        state.seq = pkt.sendMs;  // RTT 계산을 위해 원본 송신 시각을 그대로 에코
        state.score = score;
        sendPacket(peerMac, state);
      }
      break;

    case MSG_STATE:
      if (!isRole1P && paired && isSameMac(info->src_addr, peerMac) && pkt.role == 1) {
        score = pkt.score;
        lastRttMs = millis() - pkt.seq;
      }
      break;
  }
}

void IRAM_ATTR handleSwitchInterrupt(void *arg) {
  uint8_t idx = (uint8_t)(uintptr_t)arg;
  lastChangeMs[idx] = millis();
  checkPending[idx] = true;
}

// 부팅 직후: 역할 표시 화면
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

// 페어링 탐색 중: 점 애니메이션으로 진행 중임을 표시
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
  display.print("CH:"); display.print(ESPNOW_CHANNEL);
  display.print(" TX:"); display.print(txCount);
  display.display();
}

// 페어링 직후 잠깐 보여주는 완료 화면
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

// 페어링 이후 통신 테스트 상태 화면
void drawStatusScreen() {
  display.clearDisplay();
  display.setTextSize(2);

  display.setTextColor(ST77XX_WHITE);
  display.setCursor(0, 0);
  display.print("ROLE: ");
  display.print(isRole1P ? "1P" : "2P");
  display.setTextColor(ST77XX_GREEN);
  display.print(" OK");
  display.drawFastHLine(0, 24, LCD_W, LCD_GREY);

  display.setTextColor(ST77XX_WHITE);
  display.setCursor(0, 34);
  display.print("TX:"); display.print(txCount);
  display.print(" RX:"); display.print(rxCount);

  display.setCursor(0, 58);
  display.print("SCORE:");
  display.setTextColor(ST77XX_YELLOW);
  display.print(score);

  display.setTextColor(ST77XX_CYAN);
  display.setCursor(0, 90);
  if (isRole1P) {
    if (lastRemoteSwitchIdx != 255) {
      display.print("P2:SW");
      display.print(lastRemoteSwitchIdx + 1);
    } else {
      display.print("P2:WAIT");
    }
  } else {
    display.print("RTT:"); display.print(lastRttMs); display.print("ms");
  }

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

void setup() {
  Serial.begin(115200);

  pinMode(MODE_SWITCH_PIN, INPUT_PULLUP);
  isRole1P = (digitalRead(MODE_SWITCH_PIN) == HIGH);

  for (uint8_t i = 0; i < NUM_INPUT_SWITCHES; i++) {
    pinMode(INPUT_SWITCH_PINS[i], INPUT_PULLUP);
  }

  display.begin();
  display.setTextSize(2);
  display.setTextColor(ST77XX_WHITE);
  drawBootScreen();
  delay(1000);  // 역할(1P/2P) 확인용 부팅 화면 잠시 유지

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_ps(WIFI_PS_NONE);  // 모뎀 슬립 끔: 지연 스파이크 방지

  if (esp_now_init() != ESP_OK) {
    Serial.println("esp_now_init failed");
    return;
  }
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);
  addPeer(BROADCAST_MAC);

  for (uint8_t i = 0; i < NUM_INPUT_SWITCHES; i++) {
    attachInterruptArg(INPUT_SWITCH_PINS[i], handleSwitchInterrupt, (void *)(uintptr_t)i, CHANGE);
  }

  Serial.print("MAC: "); Serial.println(WiFi.macAddress());
  Serial.print("Role: "); Serial.println(isRole1P ? "1P" : "2P");
}

void loop() {
  uint32_t now = millis();

  if (!paired && now - lastHelloMs >= HELLO_INTERVAL_MS) {
    lastHelloMs = now;
    espnow_packet_t hello = {};
    hello.type = MSG_HELLO;
    hello.role = isRole1P ? 1 : 2;
    hello.seq = helloSeq++;
    sendPacket(BROADCAST_MAC, hello);
  }

  if (paired && !isRole1P) {
    for (uint8_t i = 0; i < NUM_INPUT_SWITCHES; i++) {
      if (checkPending[i] && (now - lastChangeMs[i] >= DEBOUNCE_MS)) {
        checkPending[i] = false;

        bool pressed = (digitalRead(INPUT_SWITCH_PINS[i]) == LOW);
        if (pressed != switchPressed[i]) {
          switchPressed[i] = pressed;
          if (pressed) {
            espnow_packet_t pkt = {};
            pkt.type = MSG_INPUT;
            pkt.role = 2;
            pkt.switchIdx = i;
            pkt.pressed = 1;
            pkt.sendMs = now;
            sendPacket(peerMac, pkt);
          }
        }
      }
    }
  }

  static uint32_t lastLcdMs = 0;
  if (now - lastLcdMs >= LCD_REFRESH_MS) {
    lastLcdMs = now;
    redrawLCD();
  }
}
