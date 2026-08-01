#pragma once
/*
  ===========================================================================
  SoloPong.h - 1P Pong (vs MCU)
  ===========================================================================
  10test1P-pong의 게임 로직을 통합 스케치용 모듈로 옮긴 것이다.

  [반사]  공이 자기 영역(끝에서 18칸) 안에 있을 때 아무 스위치나 누르면 반사된다.
          초록 = 속도 유지 / 노랑 = 약간 가속 / 빨강 = 최저 속도 보장 + 가속.
          랠리를 가속하는 것은 플레이어의 타이밍뿐이고, CPU의 리턴은 속도를 바꾸지 않는다.
  [서브]  항상 코트 중앙에서 플레이어 쪽으로 느리게 출발한다(1인용이라 서브권 교대가 없다).
  [레벨]  플레이어가 LEVEL_UP_POINTS점을 먼저 내면 레벨업, CPU가 CPU_MATCH_POINTS점을
          먼저 내면 게임 오버.

  CPU는 공이 자기 영역에 들어온 순간 "언제 스윙할지"를 한 번 정하고 그때 스윙한다.
  그 시각에 공이 이미 지나갔으면 놓친 것이 되므로, 미스는 따로 굴리는 주사위가 아니라
  느린 반응의 결과다. 공이 빠를수록 영역 통과 시간이 짧아져 같은 반응으로도 더 자주 놓친다.
    영역 18칸 통과 시간: 서브 60칸/초 → 300ms / 100칸/초 → 180ms / 상한 220칸/초 → 82ms

  READY 화면에서 Red(1번)를 누르면 메뉴로 나간다.
  ===========================================================================
*/

#include "Arcade.h"

namespace SoloPong {

// 세 게임이 같은 이름의 상수를 쓰기 때문에 #define 대신 네임스페이스 안의 constexpr로 둔다.
// 매크로는 네임스페이스를 타지 않아 서로 덮어쓴다.

// ---- 공 물리 ----
constexpr float BALL_SPEED_SERVE    = 60.0f;
constexpr float REFLECT_GAIN_GREEN  = 1.00f;
constexpr float REFLECT_GAIN_YELLOW = 1.18f;
constexpr float REFLECT_GAIN_RED    = 1.10f;
constexpr float REFLECT_MIN_RED     = 85.0f;
constexpr float BALL_SPEED_MAX      = 220.0f;

constexpr uint8_t LEVEL_UP_POINTS  = 5;   // 플레이어가 이 점수를 먼저 내면 레벨 클리어
constexpr uint8_t CPU_MATCH_POINTS = 5;   // CPU가 이 점수를 먼저 내면 게임 오버

// ---- CPU 난이도 (실제로 붙어 보고 조절할 값은 이 여섯 개다) ----
constexpr uint16_t CPU_REACT_BASE_MS  = 120;  // 레벨 1의 평균 반응 지연
constexpr uint16_t CPU_REACT_STEP_MS  = 10;   // 레벨당 줄어드는 양
constexpr uint16_t CPU_REACT_MIN_MS   = 20;
constexpr uint16_t CPU_JITTER_BASE_MS = 60;   // 레벨 1의 반응 흔들림(±)
constexpr uint16_t CPU_JITTER_STEP_MS = 5;
constexpr uint16_t CPU_JITTER_MIN_MS  = 8;
constexpr uint16_t CPU_WHIFF_BASE_PCT = 20;   // 레벨 1에서 아예 반응하지 않을 확률(%)
constexpr uint16_t CPU_WHIFF_STEP_PCT = 2;
constexpr uint16_t CPU_WHIFF_MIN_PCT  = 2;

// ---- 연출 타이밍 ----
constexpr uint8_t  COUNTDOWN_STEPS   = 5;    // 초록/꺼짐/초록/꺼짐/빨강
constexpr uint16_t COUNTDOWN_STEP_MS = 500;
constexpr uint16_t START_MS          = 800;
constexpr uint16_t POINT_FLASH_MS    = 1000;
constexpr uint16_t LEVEL_UP_MS       = 2500;
constexpr uint16_t OVER_LOCK_MS      = 1500;  // 게임 오버 화면에서 입력을 무시하는 시간
constexpr uint16_t RAINBOW_PERIOD_MS = 3000;

constexpr float    FULL_SCALE      = 0.25f;  // 코트 전체를 채우는 연출의 감쇠
constexpr float    ZONE_SCALE      = 0.45f;  // 존/공처럼 몇 칸만 켜질 때의 감쇠
constexpr float    SWITCH_IDLE     = 0.10f;
constexpr uint16_t SWITCH_FLASH_MS = 120;

enum State : uint8_t {
  S_READY, S_ARMED, S_COUNTDOWN, S_START, S_PLAY, S_POINT, S_LEVEL_UP, S_OVER
};

State state = S_READY;
uint32_t stateMs = 0;
bool exitRequested = false;

uint16_t level = 1;
uint8_t score[2] = {0, 0};      // [0] = 플레이어, [1] = CPU
uint8_t lastPointWinner = 0;

float ballPos = 0, ballPrevPos = 0, ballSpeed = 0;
int8_t ballDir = -1;            // +1 = CPU 쪽, -1 = 플레이어 쪽
uint32_t lastPhysicsUs = 0;

uint8_t countdownStep = 0;
uint8_t countdownDigit = 3;

uint32_t cpuSwingAtMs = 0;      // 0 = 이번 접근에는 스윙하지 않는다
bool cpuDecided = false;

uint32_t switchLedOffAt[NUM_SWITCH_LEDS] = {0};
uint32_t lastFrameMs = 0;
bool lcdDirty = true;

// ---------------------------------------------------------------------------
// CPU 난이도
// ---------------------------------------------------------------------------
// 레벨이 오를수록 base에서 step씩 깎되 minimum 아래로는 내려가지 않는다.
// 세 파라미터가 같은 모양이라 한 함수로 묶었다.
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
// 진행
// ---------------------------------------------------------------------------
void enterState(State s) {
  state = s;
  stateMs = millis();
  lcdDirty = true;
}

void startCountdown() {
  countdownStep = 0xFF;   // 첫 스텝(3)의 소리/표시가 다른 스텝과 같은 경로로 나오게
  countdownDigit = 3;
  enterState(S_COUNTDOWN);
}

// 중앙에서 플레이어 쪽으로 느린 공을 발사한다. 방향은 항상 고정이다.
void launchServe() {
  ballPos = FIELD_START + FIELD_LEN / 2.0f;
  ballPrevPos = ballPos;
  ballSpeed = BALL_SPEED_SERVE;
  ballDir = -1;
  lastPhysicsUs = micros();
  cpuDecided = false;
  cpuSwingAtMs = 0;
  soundPlay(SFX_SERVE);
  enterState(S_PLAY);
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
  lastPointWinner = winner;
  soundPlay(loser == 0 ? SFX_MISS : SFX_MISS_OPPONENT);

  if (score[0] >= LEVEL_UP_POINTS) { soundPlay(SFX_WIN);       enterState(S_LEVEL_UP); return; }
  if (score[1] >= CPU_MATCH_POINTS){ soundPlay(SFX_GAME_OVER); enterState(S_OVER);     return; }
  enterState(S_POINT);
}

void playerSwing() {
  if (ballDir != -1) return;   // 자기 쪽으로 오는 중이어야 한다
  int8_t zone = zoneOfDepth(depthFrom(0, (int16_t)floorf(ballPos)));
  if (zone < 0) return;        // 영역 밖 - 헛스윙
  ballSpeed = reflectSpeed(zone, ballSpeed);
  ballDir = +1;
  soundPlay((zone == 2) ? SFX_HIT_GREEN : (zone == 1) ? SFX_HIT_YELLOW : SFX_HIT_RED);
}

// CPU가 스윙한다. 이 순간 공이 자기 영역 안에 있어야 반사된다.
void cpuSwing() {
  if (ballDir != +1) return;
  int16_t depth = depthFrom(1, (int16_t)floorf(ballPos));
  if (depth < 0 || depth >= ZONE_LEN) return;   // 이미 지나갔다
  ballDir = -1;   // CPU의 리턴은 속도를 바꾸지 않는다
  soundPlay(SFX_CPU_HIT);
}

void updateCpu(uint32_t now) {
  if (ballDir != +1) { cpuDecided = false; cpuSwingAtMs = 0; return; }

  if (!cpuDecided) {
    int16_t depth = depthFrom(1, (int16_t)floorf(ballPos));
    if (depth >= 0 && depth < ZONE_LEN) {
      cpuDecided = true;
      if ((uint16_t)random(100) < cpuWhiffPct()) {
        cpuSwingAtMs = 0;   // 이번엔 아예 반응하지 않는다
      } else {
        int32_t j = (int32_t)cpuJitterMs();
        int32_t d = (int32_t)cpuReactMs() + (int32_t)random(-j, j + 1);
        if (d < 0) d = 0;
        cpuSwingAtMs = now + (uint32_t)d;
      }
    }
  }
  if (cpuSwingAtMs != 0 && now >= cpuSwingAtMs) { cpuSwingAtMs = 0; cpuSwing(); }
}

// ---------------------------------------------------------------------------
// 렌더링
// ---------------------------------------------------------------------------
void fillField(uint32_t color) {
  uint32_t c = scaleColor(color, FULL_SCALE);
  for (int16_t i = FIELD_START; i <= FIELD_END; i++) strip.setPixelColor(i, c);
}

void drawZones() {
  for (uint8_t s = 0; s < 2; s++) {
    for (int16_t d = 0; d < ZONE_LEN; d++) {
      int16_t idx = (s == 0) ? (FIELD_START + d) : (FIELD_END - d);
      strip.setPixelColor(idx, scaleColor(zoneColor(zoneOfDepth(d)), ZONE_SCALE));
    }
  }
}

// 공은 흰색 1칸. 빠를 때 점선처럼 보이지 않도록 직전 위치까지 잔상을 남긴다.
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

void renderSwitchLeds(uint32_t now) {
  for (uint8_t i = 0; i < NUM_SWITCH_LEDS; i++) {
    bool bright = (switchLedOffAt[i] != 0 && now < switchLedOffAt[i]);
    if (!bright && switchLedOffAt[i] != 0) switchLedOffAt[i] = 0;
    strip.setPixelColor(i, scaleColor(colWhite(), bright ? 1.0f : SWITCH_IDLE));
  }
}

void render(uint32_t now) {
  clearField();
  switch (state) {
    case S_READY:
    case S_ARMED:     drawZones(); break;
    case S_COUNTDOWN:
      if (countdownStep == 4)          fillField(colRed());
      else if (countdownStep % 2 == 0) fillField(colGreen());
      break;
    case S_START:     break;
    case S_PLAY:      drawZones(); drawBall(); break;
    case S_POINT:     drawHalves(lastPointWinner); break;
    case S_LEVEL_UP:  drawRainbow(now, RAINBOW_PERIOD_MS, FULL_SCALE); break;
    case S_OVER:      drawHalves(1); break;   // CPU 쪽 절반이 초록
  }
  // 무지개 구간에는 스위치 LED도 무지개의 일부라 덧그리지 않는다.
  if (state != S_LEVEL_UP) renderSwitchLeds(now);
  safeShow();
}

// ---------------------------------------------------------------------------
// LCD
// ---------------------------------------------------------------------------
void drawScoreBoard() {
  display.setTextSize(2);
  display.setTextColor(ST77XX_YELLOW);
  display.setCursor(16, 2);  display.print("YOU");
  display.setTextColor(ST77XX_MAGENTA);
  display.setCursor(104, 2); display.print("CPU");

  display.setTextSize(6);    // 숫자 한 자 36x48px
  display.setTextColor(ST77XX_YELLOW);
  display.setCursor(16, 28);  display.print(score[0]);
  display.setTextColor(ST77XX_MAGENTA);
  display.setCursor(108, 28); display.print(score[1]);
  display.drawFastVLine(80, 20, 60, LCD_GREY);

  char buf[16];
  snprintf(buf, sizeof(buf), "LEVEL %u", level);
  display.setTextColor(ST77XX_CYAN);
  printCentered(buf, 2, 106);
}

void drawLcd() {
  display.clearDisplay();
  char buf[16];

  switch (state) {
    case S_READY:
      display.setTextColor(ST77XX_CYAN);
      printCentered("SOLO PONG", 2, 6);
      display.setTextColor(ST77XX_WHITE);
      printCentered("READY?", 3, 44);
      snprintf(buf, sizeof(buf), "LEVEL %u", level);
      display.setTextColor(LCD_GREY);
      printCentered(buf, 2, 80);
      printCentered("BLUE:START  RED:MENU", 1, 112);
      break;

    case S_ARMED:   // 선택음과 카운트다운 소리가 겹치지 않도록 두는 사이
      display.setTextColor(ST77XX_CYAN);
      printCentered("SOLO PONG", 2, 6);
      display.setTextColor(ST77XX_WHITE);
      printCentered("GET", 4, 40);
      printCentered("READY", 4, 76);
      break;

    case S_COUNTDOWN: {
      char d[2] = {(char)('0' + countdownDigit), '\0'};
      display.setTextColor(countdownDigit == 1 ? ST77XX_RED : ST77XX_GREEN);
      printCentered(d, 12, 16);    // 크기 12 = 72x96px
      break;
    }

    case S_START:
      display.setTextColor(ST77XX_GREEN);
      printCentered("START", 4, 48);
      break;

    case S_PLAY:
    case S_POINT:
      drawScoreBoard();
      break;

    case S_LEVEL_UP:
      display.setTextColor(ST77XX_GREEN);
      printCentered("LEVEL", 4, 22);
      snprintf(buf, sizeof(buf), "%u", level);
      printCentered(buf, 6, 58);
      display.setTextColor(ST77XX_WHITE);
      printCentered("CLEAR!", 2, 108);
      break;

    case S_OVER:
      display.setTextColor(ST77XX_RED);
      printCentered("GAME", 4, 10);
      printCentered("OVER", 4, 44);
      snprintf(buf, sizeof(buf), "LEVEL %u", level);
      display.setTextColor(ST77XX_WHITE);
      printCentered(buf, 2, 84);
      display.setTextColor(LCD_GREY);
      printCentered("ANY:RETRY  RED:MENU", 1, 112);
      break;
  }
  display.display();
}

// ---------------------------------------------------------------------------
// 모듈 규약
// ---------------------------------------------------------------------------
void begin() {
  exitRequested = false;
  level = 1;
  score[0] = 0;
  score[1] = 0;
  Input::flush();
  enterState(S_READY);
}

void update(uint32_t now) {
  uint8_t pressed = Input::takePressed();

  // ---- 입력 ----
  if (pressed) {
    for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
      if (pressed & (1 << i)) switchLedOffAt[i] = now + SWITCH_FLASH_MS;
    }
    if (state == S_READY) {
      if (pressed & (1 << BTN_BACK)) { soundPlay(SFX_UI_BACK); exitRequested = true; return; }
      soundPlay(SFX_UI_SELECT);
      enterState(S_ARMED);   // 선택음이 끝날 틈을 준 뒤 카운트다운으로
    } else if (state == S_OVER) {
      if (now - stateMs >= OVER_LOCK_MS) {
        if (pressed & (1 << BTN_BACK)) { soundPlay(SFX_UI_BACK); exitRequested = true; return; }
        begin();   // 레벨 1부터 다시
      }
    } else if (state == S_PLAY) {
      playerSwing();
    }
    // 카운트다운/연출 중에는 입력을 받지 않는다(스위치 LED만 반응)
  }

  // ---- 진행 ----
  switch (state) {
    case S_ARMED:
      if (now - stateMs >= GAME_START_DELAY_MS) startCountdown();
      break;

    case S_COUNTDOWN: {
      uint8_t step = (uint8_t)((now - stateMs) / COUNTDOWN_STEP_MS);
      if (step >= COUNTDOWN_STEPS) { soundPlay(SFX_START); enterState(S_START); break; }
      if (step != countdownStep) {
        countdownStep = step;
        countdownDigit = 3 - (step / 2);              // 0,1 → 3 / 2,3 → 2 / 4 → 1
        if (step % 2 == 0) soundPlay(SFX_COUNTDOWN);  // 불이 켜지는 스텝에서만
        lcdDirty = true;   // enterState를 쓰면 stateMs가 리셋돼 카운트다운이 처음부터 다시 간다
      }
      break;
    }

    case S_START:
      if (now - stateMs >= START_MS) launchServe();
      break;

    case S_PLAY: {
      uint32_t us = micros();
      float dt = (us - lastPhysicsUs) / 1000000.0f;
      lastPhysicsUs = us;
      if (dt > 0.05f) dt = 0.05f;

      ballPrevPos = ballPos;
      ballPos += ballDir * ballSpeed * dt;
      updateCpu(now);

      if (ballPos < FIELD_START)    concedePoint(0);   // 플레이어가 놓침
      else if (ballPos > FIELD_END) concedePoint(1);   // CPU가 놓침
      break;
    }

    case S_POINT:
      if (now - stateMs >= POINT_FLASH_MS) launchServe();
      break;

    case S_LEVEL_UP:
      if (now - stateMs >= LEVEL_UP_MS) {
        level++;          // 여기서 올려야 LEVEL UP 화면이 "방금 깬 레벨"을 보여준다
        score[0] = 0;
        score[1] = 0;
        startCountdown();
      }
      break;

    default:
      break;
  }

  // ---- 출력 ----
  if (now - lastFrameMs >= FRAME_MS) { lastFrameMs = now; render(now); }
  if (lcdDirty) { lcdDirty = false; drawLcd(); }
}

bool wantsExit() { return exitRequested; }

}  // namespace SoloPong
