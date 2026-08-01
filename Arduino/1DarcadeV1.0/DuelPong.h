#pragma once
/*
  ===========================================================================
  DuelPong.h - 2P Pong (보드 두 대 대전)
  ===========================================================================
  09test2P-pong의 게임 로직을 통합 스케치용 모듈로 옮긴 것이다.

  역할
    라인 LED가 물려 있는 보드가 1P(authoritative)다. 공 물리/판정/점수/사운드를 혼자
    결정하고 코트를 그린다. 반대 보드는 2P로, 자기 입력을 1P에 보내고 받은 상태를 그린다.
    어느 쪽인지는 부팅 시 BOARD_ID_PIN(5번 토글)으로 정해지며 Arcade.h의 hasLineLeds가 들고 있다.
    양쪽 보드가 같은 역할로 켜져 있으면 LCD에 ROLE MISMATCH가 뜬다.

  통신
    2P → 1P : MSG_INPUT (스위치가 눌렸다)
    1P → 2P : MSG_STATE (LCD에 그릴 상태 - 주기 + 변화 시 즉시)
              MSG_SFX   (지금 이 소리를 내라)
    소리를 1P가 판정해 내려보내는 이유: 2P가 눌러도 그게 유효한 반사인지 헛스윙인지는
    1P만 안다. 눌렀다고 2P가 먼저 소리를 내면 헛스윙에도 반사음이 난다.

  소리는 양쪽에서 늘 같이 나지 않는다. 자기 행동의 결과만 자기 쪽에서 들린다.
    READY 누름 / 반사 / 놓침 → 그 사람만,  준비 완료 / 카운트다운 / START / 서브 → 양쪽

  페어링 대기 화면이나 READY에서 Red(1번)를 누르면 메뉴로 나간다.
  ===========================================================================
*/

#include "Arcade.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

namespace DuelPong {

// 세 게임이 같은 이름의 상수를 쓰기 때문에 #define 대신 네임스페이스 안의 constexpr로 둔다.
// 매크로는 네임스페이스를 타지 않아 서로 덮어쓴다.
constexpr uint16_t HELLO_INTERVAL_MS = 500;  // 페어링 전 브로드캐스트 주기
constexpr uint16_t STATE_INTERVAL_MS = 120;  // 1P가 2P에 상태를 밀어주는 주기(유실 대비)
constexpr uint16_t LCD_SEARCH_MS     = 300;  // 페어링 전 SEARCHING 애니메이션 갱신 주기

constexpr float BALL_SPEED_SERVE    = 60.0f;
constexpr float REFLECT_GAIN_GREEN  = 1.00f;
constexpr float REFLECT_GAIN_YELLOW = 1.18f;
constexpr float REFLECT_GAIN_RED    = 1.10f;
constexpr float REFLECT_MIN_RED     = 85.0f;
constexpr float BALL_SPEED_MAX      = 220.0f;

constexpr uint8_t MATCH_POINT = 10;   // 먼저 이 점수에 도달하면 승리

constexpr uint16_t OK_MS             = 2000;
constexpr uint8_t  COUNTDOWN_STEPS   = 5;
constexpr uint16_t COUNTDOWN_STEP_MS = 500;
constexpr uint16_t START_MS          = 800;
constexpr uint16_t POINT_FLASH_MS    = 1000;
constexpr uint16_t MATCH_LOCK_MS     = 2000;  // 승리 화면에서 입력을 무시하는 시간
constexpr uint16_t MATCH_RAINBOW_MS  = 5000;  // 승리 화면이 무지개로 바뀌는 시점
constexpr uint16_t RAINBOW_PERIOD_MS = 3000;

constexpr float    FULL_SCALE      = 0.25f;
constexpr float    ZONE_SCALE      = 0.45f;
constexpr float    SWITCH_IDLE     = 0.10f;
constexpr uint16_t SWITCH_FLASH_MS = 120;

// ---------------------------------------------------------------------------
// 패킷
// ---------------------------------------------------------------------------
enum MsgType : uint8_t {
  MSG_HELLO = 1, MSG_HELLO_ACK = 2, MSG_INPUT = 3, MSG_STATE = 4, MSG_SFX = 5,
};

typedef struct __attribute__((packed)) {
  uint8_t type;
  uint8_t role;    // 1 = 1P, 2 = 2P
  uint8_t arg;     // MSG_INPUT: 스위치 인덱스 / MSG_SFX: SfxId
  uint8_t phase;
  uint8_t score1;
  uint8_t score2;
  uint8_t info;
} packet_t;

const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

bool isRole1P = true;
bool paired = false;
uint8_t peerMac[6] = {0};
uint32_t lastHelloMs = 0, lastStateMs = 0;
bool roleMismatch = false;
uint32_t roleMismatchMs = 0;

// 수신 콜백은 WiFi 태스크에서 돈다. 여기서는 표시만 남기고 처리는 update()에서 한다.
volatile uint8_t remoteInputMask = 0;

// ---------------------------------------------------------------------------
// 상태
// ---------------------------------------------------------------------------
enum Phase : uint8_t {
  PHASE_READY = 0,   // info: bit0 = 1P 준비됨, bit1 = 2P 준비됨
  PHASE_OK, PHASE_COUNTDOWN, PHASE_START, PHASE_PLAY, PHASE_POINT, PHASE_MATCH_END,
};

uint8_t phase = PHASE_READY;
uint32_t phaseStartMs = 0;
uint8_t score[2] = {0, 0};
uint8_t readyMask = 0;
uint8_t flashPlayer = 0;

float ballPos = 0, ballPrevPos = 0, ballSpeed = 0;
int8_t ballDir = 1;             // +1 = 2P 쪽, -1 = 1P 쪽
uint8_t serveTarget = 0;
uint8_t countdownStep = 0;

// LCD는 1P는 게임 상태에서, 2P는 받은 패킷에서 채운다.
struct UiState { uint8_t phase, score1, score2, info; };
UiState ui = {PHASE_READY, 0, 0, 0};
uint32_t uiPhaseSinceMs = 0;    // 무지개 전환 시점을 두 보드가 같이 계산하는 데 쓴다

bool exitRequested = false;
bool lcdDirty = true;
uint32_t lastFrameMs = 0, lastPhysicsUs = 0, lastLcdMs = 0;
uint32_t switchLedOffAt[NUM_SWITCH_LEDS] = {0};

// ---------------------------------------------------------------------------
// ESP-NOW
// ---------------------------------------------------------------------------
bool expectedPeerRole(uint8_t r) { return isRole1P ? (r == 2) : (r == 1); }

void addPeer(const uint8_t* mac) {
  if (esp_now_is_peer_exist(mac)) return;
  esp_now_peer_info_t info = {};
  memcpy(info.peer_addr, mac, 6);
  info.channel = ESPNOW_CHANNEL;
  info.ifidx = WIFI_IF_STA;
  info.encrypt = false;
  esp_now_add_peer(&info);
}

void sendPacket(const uint8_t* mac, packet_t& p) {
  esp_now_send(mac, (uint8_t*)&p, sizeof(p));
}

void sendState() {
  if (!isRole1P || !paired) return;
  packet_t p = {};
  p.type = MSG_STATE; p.role = 1;
  p.phase = ui.phase; p.score1 = ui.score1; p.score2 = ui.score2; p.info = ui.info;
  sendPacket(peerMac, p);
  lastStateMs = millis();
}

void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len != sizeof(packet_t)) return;
  packet_t p;
  memcpy(&p, data, sizeof(p));

  switch (p.type) {
    case MSG_HELLO:
      if (!paired) {
        if (!expectedPeerRole(p.role)) { roleMismatch = true; roleMismatchMs = millis(); break; }
        memcpy(peerMac, info->src_addr, 6);
        addPeer(peerMac);
        paired = true; roleMismatch = false; lcdDirty = true;
        packet_t ack = {};
        ack.type = MSG_HELLO_ACK;
        ack.role = isRole1P ? 1 : 2;
        sendPacket(peerMac, ack);
      }
      break;

    case MSG_HELLO_ACK:
      if (!paired) {
        if (!expectedPeerRole(p.role)) { roleMismatch = true; roleMismatchMs = millis(); break; }
        memcpy(peerMac, info->src_addr, 6);
        addPeer(peerMac);
        paired = true; roleMismatch = false; lcdDirty = true;
      }
      break;

    // 아래 셋은 페어링된 상대가 보낸 것만 받는다
    case MSG_INPUT:
      if (isRole1P && paired && memcmp(info->src_addr, peerMac, 6) == 0 && p.arg < NUM_SWITCHES) {
        remoteInputMask |= (uint8_t)(1 << p.arg);
      }
      break;

    case MSG_STATE:
      if (!isRole1P && paired && memcmp(info->src_addr, peerMac, 6) == 0) {
        if (ui.phase != p.phase || ui.score1 != p.score1 ||
            ui.score2 != p.score2 || ui.info != p.info) {
          if (ui.phase != p.phase) uiPhaseSinceMs = millis();
          ui.phase = p.phase; ui.score1 = p.score1; ui.score2 = p.score2; ui.info = p.info;
          lcdDirty = true;
        }
      }
      break;

    case MSG_SFX:
      // soundPlay()는 큐에 넣고 바로 리턴하므로 콜백 안에서 불러도 안전하다
      if (!isRole1P && paired && memcmp(info->src_addr, peerMac, 6) == 0 && p.arg < NUM_SFX) {
        soundPlay(p.arg);
      }
      break;
  }
}

// 소리는 상황에 따라 한쪽 보드에서만 난다. player 0 = 1P 보드, 1 = 2P 보드.
void playSfxOn(uint8_t player, uint8_t id) {
  if (player == 0) {
    soundPlay(id);
  } else if (paired) {
    packet_t p = {};
    p.type = MSG_SFX; p.role = 1; p.arg = id;
    sendPacket(peerMac, p);
  }
}
void playSfxBoth(uint8_t id) { playSfxOn(0, id); playSfxOn(1, id); }

// ---------------------------------------------------------------------------
// 진행 (1P 전용)
// ---------------------------------------------------------------------------
void publishUi(uint8_t p, uint8_t info) {
  if (ui.phase != p) uiPhaseSinceMs = millis();
  ui.phase = p; ui.score1 = score[0]; ui.score2 = score[1]; ui.info = info;
  lcdDirty = true;
  sendState();
}

void enterPhase(uint8_t p, uint8_t info) {
  phase = p;
  phaseStartMs = millis();
  publishUi(p, info);
}

void resetMatch() {
  score[0] = score[1] = 0;
  readyMask = 0;
  enterPhase(PHASE_READY, 0);
}

void launchServe() {
  ballPos = FIELD_START + FIELD_LEN / 2.0f;
  ballPrevPos = ballPos;
  ballSpeed = BALL_SPEED_SERVE;
  ballDir = (serveTarget == 0) ? -1 : +1;
  lastPhysicsUs = micros();
  playSfxBoth(SFX_SERVE);
  enterPhase(PHASE_PLAY, 0);
}

float reflectSpeed(int8_t zone, float in) {
  float out;
  if (zone == 2)      out = in * REFLECT_GAIN_GREEN;
  else if (zone == 1) out = in * REFLECT_GAIN_YELLOW;
  else {
    out = in * REFLECT_GAIN_RED;
    if (out < REFLECT_MIN_RED) out = REFLECT_MIN_RED;
  }
  if (out > BALL_SPEED_MAX) out = BALL_SPEED_MAX;
  return out;
}

void concedePoint(uint8_t loser) {
  uint8_t winner = 1 - loser;
  score[winner]++;
  flashPlayer = loser;

  if (score[winner] >= MATCH_POINT) {
    playSfxOn(winner, SFX_WIN);   // 팡파레는 이긴 쪽에서만
    enterPhase(PHASE_MATCH_END, winner + 1);
    return;
  }
  serveTarget = loser;   // 점수를 잃은 쪽으로 다시 서브
  enterPhase(PHASE_POINT, (uint8_t)(winner + 1));
}

// player가 자기 쪽 끝을 지나쳐 공을 놓쳤다.
void handleMiss(uint8_t player) {
  playSfxOn(player, SFX_MISS);
  playSfxOn(1 - player, SFX_MISS_OPPONENT);
  concedePoint(player);
}

// 1P/2P 어느 쪽 입력이든 여기로 모인다(1P에서만 호출된다).
void handleGamePress(uint8_t player) {
  switch (phase) {
    case PHASE_READY:
      readyMask |= (uint8_t)(1 << player);
      if (readyMask == 0x03) {
        playSfxBoth(SFX_READY_BOTH);          // 준비 완료음이 누름음을 대신한다
        enterPhase(PHASE_OK, 0);
      } else {
        playSfxOn(player, SFX_UI_SELECT);     // 아직 한 명 - 누른 쪽에서만
        publishUi(PHASE_READY, readyMask);
      }
      return;

    case PHASE_MATCH_END:
      if (millis() - phaseStartMs >= MATCH_LOCK_MS) resetMatch();
      return;

    case PHASE_PLAY:
      break;

    default:
      return;   // 카운트다운/연출 중에는 입력을 받지 않는다
  }

  // 공이 자기 쪽으로 오는 중이어야 한다(방금 친 공을 되돌리는 것을 막는다)
  int8_t approach = (player == 0) ? -1 : +1;
  if (ballDir != approach) return;

  int8_t zone = zoneOfDepth(depthFrom(player, (int16_t)floorf(ballPos)));
  if (zone < 0) return;   // 영역 밖 - 헛스윙

  ballSpeed = reflectSpeed(zone, ballSpeed);
  ballDir = -ballDir;
  // 반사음은 받아친 쪽에서만
  playSfxOn(player, (zone == 2) ? SFX_HIT_GREEN : (zone == 1) ? SFX_HIT_YELLOW : SFX_HIT_RED);
}

void updateGame(uint32_t now) {
  switch (phase) {
    case PHASE_OK:
      if (now - phaseStartMs >= OK_MS) {
        countdownStep = 0xFF;
        enterPhase(PHASE_COUNTDOWN, 3);
      }
      break;

    case PHASE_COUNTDOWN: {
      uint8_t step = (uint8_t)((now - phaseStartMs) / COUNTDOWN_STEP_MS);
      if (step >= COUNTDOWN_STEPS) {
        playSfxBoth(SFX_START);
        serveTarget = (uint8_t)random(2);
        enterPhase(PHASE_START, 0);
        break;
      }
      if (step != countdownStep) {
        countdownStep = step;
        uint8_t digit = 3 - (step / 2);
        if (step % 2 == 0) playSfxBoth(SFX_COUNTDOWN);
        publishUi(PHASE_COUNTDOWN, digit);   // phaseStartMs는 건드리지 않는다
      }
      break;
    }

    case PHASE_START:
      if (now - phaseStartMs >= START_MS) launchServe();
      break;

    case PHASE_PLAY: {
      uint32_t us = micros();
      float dt = (us - lastPhysicsUs) / 1000000.0f;
      lastPhysicsUs = us;
      if (dt > 0.05f) dt = 0.05f;

      ballPrevPos = ballPos;
      ballPos += ballDir * ballSpeed * dt;

      if (ballPos < FIELD_START)    handleMiss(0);
      else if (ballPos > FIELD_END) handleMiss(1);
      break;
    }

    case PHASE_POINT:
      if (now - phaseStartMs >= POINT_FLASH_MS) launchServe();
      break;

    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// 렌더링 (라인 LED는 1P 보드에만 있다)
// ---------------------------------------------------------------------------
bool rainbowActive() {
  return (ui.phase == PHASE_MATCH_END) && (millis() - uiPhaseSinceMs >= MATCH_RAINBOW_MS);
}

void fillField(uint32_t color) {
  uint32_t c = scaleColor(color, FULL_SCALE);
  for (int16_t i = FIELD_START; i <= FIELD_END; i++) strip.setPixelColor(i, c);
}

void drawZones() {
  for (uint8_t p = 0; p < 2; p++) {
    for (int16_t d = 0; d < ZONE_LEN; d++) {
      int16_t idx = (p == 0) ? (FIELD_START + d) : (FIELD_END - d);
      strip.setPixelColor(idx, scaleColor(zoneColor(zoneOfDepth(d)), ZONE_SCALE));
    }
  }
}

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

void drawHalves(uint8_t winner) {
  uint32_t win  = scaleColor(colGreen(), FULL_SCALE);
  uint32_t lose = scaleColor(colRed(), FULL_SCALE);
  for (int16_t i = FIELD_START; i <= FIELD_END; i++) {
    strip.setPixelColor(i, (halfOwner(i) == winner) ? win : lose);
  }
}

void renderField(uint32_t now) {
  clearField();
  switch (phase) {
    case PHASE_READY:
    case PHASE_OK:       drawZones(); break;
    case PHASE_COUNTDOWN:
      if (countdownStep == 4)          fillField(colRed());
      else if (countdownStep % 2 == 0) fillField(colGreen());
      break;
    case PHASE_START:    break;
    case PHASE_PLAY:     drawZones(); drawBall(); break;
    case PHASE_POINT:    drawHalves(1 - flashPlayer); break;
    case PHASE_MATCH_END:
      if (rainbowActive()) drawRainbow(now, RAINBOW_PERIOD_MS, FULL_SCALE);
      else                 drawHalves((ui.info == 1) ? 0 : 1);
      break;
  }
}

void renderSwitchLeds(uint32_t now) {
  for (uint8_t i = 0; i < NUM_SWITCH_LEDS; i++) {
    bool bright = (switchLedOffAt[i] != 0 && now < switchLedOffAt[i]);
    if (!bright && switchLedOffAt[i] != 0) switchLedOffAt[i] = 0;
    strip.setPixelColor(i, scaleColor(colWhite(), bright ? 1.0f : SWITCH_IDLE));
  }
}

// ---------------------------------------------------------------------------
// LCD (양쪽 보드가 같은 화면을 그린다. 승패 표시만 보는 사람 기준)
// ---------------------------------------------------------------------------
void drawScoreBoard() {
  display.setTextSize(2);
  display.setTextColor(ST77XX_YELLOW);
  display.setCursor(22, 2);  display.print("1P");
  display.setTextColor(ST77XX_MAGENTA);
  display.setCursor(114, 2); display.print("2P");

  display.setTextSize(6);
  display.setTextColor(ST77XX_YELLOW);
  display.setCursor(16, 30);  display.print(ui.score1);
  display.setTextColor(ST77XX_MAGENTA);
  display.setCursor(108, 30); display.print(ui.score2);
  display.drawFastVLine(80, 22, 64, LCD_GREY);
}

void drawSearching() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(ST77XX_WHITE);
  display.setCursor(0, 0);
  display.print("DUEL / ");
  display.setTextColor(isRole1P ? ST77XX_YELLOW : ST77XX_MAGENTA);
  display.print(isRole1P ? "1P" : "2P");
  display.drawFastHLine(0, 24, LCD_W, LCD_GREY);

  display.setCursor(0, 52);
  if (roleMismatch && (millis() - roleMismatchMs < 1500)) {
    display.setTextColor(ST77XX_RED);   // 두 보드가 같은 역할 - 한쪽 토글을 바꿔야 한다
    display.print("ROLE");
    display.setCursor(0, 74);
    display.print("MISMATCH");
  } else {
    display.setTextColor(ST77XX_YELLOW);
    display.print("SEARCHING");
    uint8_t dots = (millis() / 400) % 4;
    for (uint8_t i = 0; i < dots; i++) display.print(".");
  }
  display.setTextColor(LCD_GREY);
  printCentered("RED:MENU", 1, 112);
  display.display();
}

void drawLcd() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(ST77XX_WHITE);

  switch (ui.phase) {
    case PHASE_READY:
      printCentered("READY?", 4, 20);
      display.setTextSize(2);
      display.setCursor(12, 78);
      display.setTextColor((ui.info & 0x01) ? ST77XX_GREEN : LCD_GREY);
      display.print("1P:"); display.print((ui.info & 0x01) ? "OK" : "--");
      display.setCursor(92, 78);
      display.setTextColor((ui.info & 0x02) ? ST77XX_GREEN : LCD_GREY);
      display.print("2P:"); display.print((ui.info & 0x02) ? "OK" : "--");
      display.setTextColor(LCD_GREY);
      printCentered("RED:MENU", 1, 112);
      break;

    case PHASE_OK:
      display.setTextColor(ST77XX_GREEN);
      printCentered("OK!", 6, 40);
      break;

    case PHASE_COUNTDOWN: {
      char d[2] = {(char)('0' + ui.info), '\0'};
      display.setTextColor(ui.info == 1 ? ST77XX_RED : ST77XX_GREEN);
      printCentered(d, 12, 16);
      break;
    }

    case PHASE_START:
      display.setTextColor(ST77XX_GREEN);
      printCentered("START", 4, 48);
      break;

    case PHASE_PLAY:
      drawScoreBoard();
      break;

    case PHASE_POINT:
      drawScoreBoard();
      display.setTextSize(2);
      display.setCursor(0, 104);
      display.setTextColor(ST77XX_RED);
      display.print(ui.info == 1 ? "1P" : "2P");
      display.print(" POINT!");
      break;

    case PHASE_MATCH_END: {
      display.setTextColor(ST77XX_WHITE);
      display.setCursor(0, 0);
      display.print(ui.score1); display.print(" - "); display.print(ui.score2);

      // 이 화면만은 두 보드가 서로 다른 글자를 띄운다(보는 사람 기준).
      bool iWon = ((ui.info == 1) == isRole1P);
      display.setTextColor(iWon ? ST77XX_GREEN : ST77XX_RED);
      printCentered("YOU", 4, 34);
      printCentered(iWon ? "WIN" : "LOSE", 4, 68);
      display.setTextColor(LCD_GREY);
      printCentered("ANY:RETRY  RED:MENU", 1, 112);
      break;
    }
  }
  display.display();
}

// ---------------------------------------------------------------------------
// 모듈 규약
// ---------------------------------------------------------------------------
void begin() {
  exitRequested = false;
  isRole1P = hasLineLeds;   // 라인 LED가 달린 보드가 1P
  paired = false;
  roleMismatch = false;
  remoteInputMask = 0;
  readyMask = 0;
  score[0] = score[1] = 0;
  phase = PHASE_READY;
  phaseStartMs = millis();
  ui = {PHASE_READY, 0, 0, 0};
  uiPhaseSinceMs = millis();
  lastPhysicsUs = micros();
  lcdDirty = true;
  Input::flush();

  esp_now_register_recv_cb(onRecv);
  addPeer(BROADCAST_MAC);   // 페어링 전에는 브로드캐스트로 서로를 찾는다
}

void end() {
  esp_now_unregister_recv_cb();
  if (paired) esp_now_del_peer(peerMac);
  esp_now_del_peer(BROADCAST_MAC);
}

void update(uint32_t now) {
  uint8_t pressed = Input::takePressed();

  // 페어링 전에는 주기적으로 브로드캐스트를 뿌려 상대를 찾는다
  if (!paired && now - lastHelloMs >= HELLO_INTERVAL_MS) {
    lastHelloMs = now;
    packet_t hello = {};
    hello.type = MSG_HELLO;
    hello.role = isRole1P ? 1 : 2;
    sendPacket(BROADCAST_MAC, hello);
  }

  // ---- 로컬 입력 ----
  if (pressed) {
    for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
      if (pressed & (1 << i)) switchLedOffAt[i] = now + SWITCH_FLASH_MS;
    }
    // Red는 대기/준비/승리 화면에서 메뉴로 나가는 길로 남겨 둔다
    bool atRestPoint = (!paired || ui.phase == PHASE_READY || ui.phase == PHASE_MATCH_END);
    if ((pressed & (1 << BTN_BACK)) && atRestPoint) {
      soundPlay(SFX_UI_BACK);
      exitRequested = true;
      return;
    }
    if (paired) {
      if (isRole1P) {
        handleGamePress(0);          // 1P는 자기 입력을 바로 게임에 넣는다
      } else {
        packet_t p = {};             // 2P는 1P에 전달만 한다
        p.type = MSG_INPUT; p.role = 2;
        p.arg = 0;                   // 어느 스위치든 게임에는 "쳤다" 한 번으로 들어간다
        sendPacket(peerMac, p);
      }
    }
  }

  // ---- 2P에서 넘어온 입력 ----
  uint8_t remote = remoteInputMask;
  if (remote) {
    remoteInputMask &= (uint8_t)~remote;
    if (isRole1P && paired) handleGamePress(1);
  }

  // ---- 게임 진행은 1P만 ----
  if (isRole1P && paired) {
    updateGame(now);
    if (now - lastStateMs >= STATE_INTERVAL_MS) sendState();
  }

  // ---- 스트립 ----
  if (now - lastFrameMs >= FRAME_MS) {
    lastFrameMs = now;
    if (paired) renderField(now);
    else        clearField();
    // 무지개 구간에는 스위치 LED도 무지개의 일부라 덧그리지 않는다
    if (paired && rainbowActive()) drawRainbow(now, RAINBOW_PERIOD_MS, FULL_SCALE);
    else                           renderSwitchLeds(now);
    safeShow();
  }

  // ---- LCD ----
  if (!paired) {
    if (now - lastLcdMs >= LCD_SEARCH_MS) { lastLcdMs = now; drawSearching(); }
  } else if (lcdDirty) {
    lcdDirty = false;
    drawLcd();
  }
}

bool wantsExit() { return exitRequested; }

}  // namespace DuelPong
