#pragma once
/*
  ===========================================================================
  SnakeInvader.h - 지렁이 슈팅 (1P)
  ===========================================================================
  07test_snakeinvader의 게임 로직을 통합 스케치용 모듈로 옮긴 것이다.
  하드웨어/입력/설정은 Arcade.h가 들고 있고, 여기서는 게임만 다룬다.

  모듈 규약(세 게임 공통):
    begin()        진입할 때 한 번. 상태 초기화
    update(now)    매 loop마다. 게임 로직 + LED/LCD 갱신
    wantsExit()    true가 되면 .ino가 메뉴로 되돌린다

  READY 화면에서 Red(1번)를 누르면 메뉴로 나간다. 나머지 버튼은 게임 시작.
  게임 중에는 네 스위치가 각자의 색 총알을 쏘는 원래 역할을 한다.

  색상 규칙
    스위치 색은 LED 위치 순서대로 0~3 = Red / Green / Blue / White.
    - 레벨 1~9  : 흰색 스위치는 꺼지고 눌러도 발사되지 않으며 지렁이에도 흰색이 없다.
    - 레벨 10   : 흰색 해금 연출 후부터 지렁이에 흰색이 섞인다.
    - 레벨 20+  : 매 레벨 시작 전 4색이 무작위로 재배정된다.

  연출(카운트다운/해금 스윕/점멸/플래시)은 원본처럼 delay()로 막고 진행한다.
  게임이 화면을 독점하는 구간이라 메뉴가 멈춰도 문제가 없고, 원본 타이밍을 그대로 옮길 수 있다.
  ===========================================================================
*/

#include "Arcade.h"

namespace SnakeInvader {

// ---- 난이도 ----
// 세 게임이 같은 이름의 상수를 쓰기 때문에(COUNTDOWN_STEP_MS 등) #define 대신
// 네임스페이스 안의 constexpr로 둔다. 매크로는 네임스페이스를 타지 않아 서로 덮어쓴다.
constexpr uint16_t SNAKE_BASE_LEN        = 5;      // 레벨 1 지렁이 길이(칸 수)
constexpr uint16_t SNAKE_LEN_PER_LEVEL   = 1;      // 레벨업마다 늘어나는 길이
constexpr uint16_t MAX_SNAKE_LEN         = 25;    // 배열 크기 상한
constexpr float    SNAKE_BASE_SPEED      = 4.0f;   // 레벨 1 전진 속도(칸/초)
constexpr float    SNAKE_SPEED_PER_LEVEL = 0.5f;   // 레벨업마다 늘어나는 속도
constexpr float    SNAKE_MISS_ACCEL      = 1.15f;  // 총알 색 불일치 시 가속 배율
constexpr float    SNAKE_MAX_SPEED       = 60.0f;  // 무한 가속 방지용 상한
constexpr float    BULLET_SPEED          = 150.0f; // 총알 속도(칸/초)
constexpr uint8_t  MAX_BULLETS           = 8;      // 동시 총알 개수 상한

constexpr uint16_t WHITE_UNLOCK_LEVEL    = 10;   // 흰색이 지렁이에 등장하기 시작하는 레벨
constexpr uint16_t RGBW_RANDOM_LEVEL     = 20;   // 이 레벨부터 매 레벨 스위치 색 랜덤 재배정

constexpr uint16_t COUNTDOWN_STEP_MS     = 1000; // "빨강*3 + 초록" 카운트다운 한 스텝
constexpr uint16_t WHITE_SWEEP_STEP_MS   = 12;   // 흰색 해금 스윕: 한 칸당 시간(172칸 약 2.1초)
constexpr uint16_t UNLOCK_BLINK_MS       = 150;
constexpr uint8_t  UNLOCK_BLINK_TIMES    = 5;
constexpr uint16_t RGBW_BLINK_MS         = 150;
constexpr uint8_t  RGBW_BLINK_TIMES      = 3;

constexpr uint16_t FLASH_MS              = 120;   // 누른 스위치 LED를 밝게 켜 두는 시간
constexpr float    SWITCH_BOOST          = 1.5f;  // 스위치 LED는 필드보다 눈에 띄게
constexpr uint16_t READY_RAINBOW_MS      = 5000;  // READY?에서 무지개로 넘어가는 시간
constexpr uint16_t RAINBOW_PERIOD_MS     = 3000;

// 카운트다운에 쓰는 살짝 죽인 색
const uint32_t COUNTDOWN_RED   = 0x500000;
const uint32_t COUNTDOWN_GREEN = 0x005000;
const uint32_t WAVE_CLEAR_COLOR = 0x003C00;   // 초록
const uint32_t GAME_OVER_COLOR  = 0x3C0000;   // 빨강

enum State : uint8_t { S_READY, S_ARMED, S_PLAY };

State state = S_READY;
bool exitRequested = false;
uint32_t readyEnteredMs = 0;
uint32_t armedEnteredMs = 0;
uint32_t lastRainbowMs = 0;

uint16_t level = 1;
uint32_t score = 0;

uint32_t snakeColors[MAX_SNAKE_LEN];  // index 0 = 선두(플레이어에 가장 가까운 세그먼트)
uint8_t snakeCount = 0;
float headPos = 0;
float snakeSpeed = SNAKE_BASE_SPEED;
long lastHeadCell = 0;                // 전진 소리를 한 칸에 한 번만 내기 위한 직전 칸

uint32_t currentSwitchColor[NUM_SWITCH_LEDS];
bool switchUnlocked[NUM_SWITCH_LEDS];

struct Bullet { bool active; float pos; uint32_t color; };
Bullet bullets[MAX_BULLETS];

uint32_t switchFlashOffAt[NUM_SWITCH_LEDS] = {0};
uint32_t lastFrameMs = 0;

uint16_t lcdShownLevel = 0;
uint32_t lcdShownScore = 0;

// ---------------------------------------------------------------------------
// 색 배정
// ---------------------------------------------------------------------------
void applyDefaultColors() {
  for (uint8_t i = 0; i < NUM_SWITCH_LEDS; i++) currentSwitchColor[i] = btnColor(i);
  switchUnlocked[0] = true;
  switchUnlocked[1] = true;
  switchUnlocked[2] = true;
  // 흰색은 지렁이에 등장하기 시작하는 레벨부터 열린다. 그 전에는 LED가 꺼지고 발사도 안 된다.
  switchUnlocked[3] = (level >= WHITE_UNLOCK_LEVEL);
}

// 레벨 20 이후: 4개 스위치에 R/G/B/W를 겹치지 않게 무작위 재배정(Fisher-Yates).
void randomizeColors() {
  uint32_t pool[4] = {colRed(), colGreen(), colBlue(), colWhite()};
  for (int8_t i = 3; i > 0; i--) {
    int8_t j = random(i + 1);
    uint32_t t = pool[i]; pool[i] = pool[j]; pool[j] = t;
  }
  for (uint8_t i = 0; i < NUM_SWITCH_LEDS; i++) {
    currentSwitchColor[i] = pool[i];
    switchUnlocked[i] = true;
  }
}

// 해금된 스위치 색 중 하나를 무작위로 골라 지렁이 세그먼트 색으로 쓴다.
// 잠금 하나로 "발사 불가"와 "지렁이에 안 나옴"이 같이 지켜진다.
uint32_t randomSnakeColor() {
  uint8_t candidates[NUM_SWITCH_LEDS];
  uint8_t n = 0;
  for (uint8_t i = 0; i < NUM_SWITCH_LEDS; i++) {
    if (switchUnlocked[i]) candidates[n++] = i;
  }
  if (n == 0) return currentSwitchColor[0];
  return currentSwitchColor[candidates[random(n)]];
}

// ---------------------------------------------------------------------------
// LCD
// ---------------------------------------------------------------------------
void drawReadyLcd() {
  display.clearDisplay();
  display.setTextColor(ST77XX_GREEN);
  printCentered("SNAKE", 3, 6);
  printCentered("INVADER", 3, 32);
  display.setTextColor(ST77XX_WHITE);
  printCentered("READY?", 3, 66);
  display.setTextColor(LCD_GREY);
  printCentered("BLUE:START  RED:MENU", 1, 112);
  display.display();
}

// 선택음과 카운트다운 소리가 겹치지 않도록 두는 사이 화면.
void drawArmedLcd() {
  display.clearDisplay();
  display.setTextColor(ST77XX_GREEN);
  printCentered("SNAKE", 3, 6);
  display.setTextColor(ST77XX_WHITE);
  printCentered("GET", 4, 44);
  printCentered("READY", 4, 80);
  display.display();
}

void drawCountdownLcd(const char* text, bool go) {
  display.clearDisplay();
  display.setTextColor(go ? ST77XX_GREEN : ST77XX_RED);
  if (go) printCentered(text, 8, 32);
  else    printCentered(text, 12, 16);
  display.display();
}

void drawMessageLcd(const char* l1, const char* l2, uint16_t color) {
  display.clearDisplay();
  display.setTextColor(color);
  printCentered(l1, 3, 40);
  if (l2) printCentered(l2, 3, 70);
  display.display();
}

void drawGameLcd() {
  display.clearDisplay();
  display.setTextColor(ST77XX_CYAN);
  printCentered("LEVEL", 2, 8);

  char buf[8];
  snprintf(buf, sizeof(buf), "%u", level);
  display.setTextColor(ST77XX_WHITE);
  printCentered(buf, 8, 32);   // 크기 8 = 48x64px

  display.setTextSize(2);
  display.setTextColor(LCD_GREY);
  display.setCursor(0, 108);
  display.print("SCORE ");
  display.print(score);
  display.display();

  lcdShownLevel = level;
  lcdShownScore = score;
}

// ---------------------------------------------------------------------------
// 연출 (원본처럼 delay()로 막고 진행한다)
// ---------------------------------------------------------------------------
void standardCountdown() {
  const uint32_t seq[4] = {COUNTDOWN_RED, COUNTDOWN_RED, COUNTDOWN_RED, COUNTDOWN_GREEN};
  const char* labels[4] = {"3", "2", "1", "GO"};
  for (uint8_t s = 0; s < 4; s++) {
    for (uint16_t i = FIELD_START; i <= FIELD_END; i++) strip.setPixelColor(i, seq[s]);
    safeShow();
    drawCountdownLcd(labels[s], s == 3);
    if (s < 3) soundPlay(SFX_COUNTDOWN);
    delay(COUNTDOWN_STEP_MS);
  }
  clearField();
  safeShow();
}

void whiteUnlockSweep() {
  drawMessageLcd("WHITE", "UNLOCKED", ST77XX_WHITE);
  for (int16_t i = FIELD_END; i >= (int16_t)FIELD_START; i--) {
    strip.setPixelColor(i, colWhite());
    safeShow();
    delay(WHITE_SWEEP_STEP_MS);
  }
  for (uint8_t b = 0; b < UNLOCK_BLINK_TIMES; b++) {
    strip.setPixelColor(3, colWhite()); safeShow(); delay(UNLOCK_BLINK_MS);
    strip.setPixelColor(3, 0);          safeShow(); delay(UNLOCK_BLINK_MS);
  }
  clearField();
  safeShow();
}

void blinkAllSwitches() {
  drawMessageLcd("COLOR", "SHUFFLE", ST77XX_YELLOW);
  for (uint8_t b = 0; b < RGBW_BLINK_TIMES; b++) {
    for (uint8_t i = 0; i < NUM_SWITCH_LEDS; i++) strip.setPixelColor(i, currentSwitchColor[i]);
    safeShow(); delay(RGBW_BLINK_MS);
    for (uint8_t i = 0; i < NUM_SWITCH_LEDS; i++) strip.setPixelColor(i, 0);
    safeShow(); delay(RGBW_BLINK_MS);
  }
}

void flashField(uint32_t color, uint8_t times, uint16_t onMs) {
  for (uint8_t t = 0; t < times; t++) {
    for (uint16_t i = FIELD_START; i <= FIELD_END; i++) strip.setPixelColor(i, color);
    safeShow(); delay(onMs);
    clearField();
    safeShow(); delay(onMs);
  }
}

// ---------------------------------------------------------------------------
// 웨이브
// ---------------------------------------------------------------------------
void clearBullets() {
  for (uint8_t i = 0; i < MAX_BULLETS; i++) bullets[i].active = false;
}

void spawnWave() {
  uint16_t len = SNAKE_BASE_LEN + (level - 1) * SNAKE_LEN_PER_LEVEL;
  if (len > MAX_SNAKE_LEN) len = MAX_SNAKE_LEN;
  snakeCount = len;

  for (uint8_t i = 0; i < snakeCount; i++) snakeColors[i] = randomSnakeColor();

  snakeSpeed = SNAKE_BASE_SPEED + (level - 1) * SNAKE_SPEED_PER_LEVEL;
  if (snakeSpeed > SNAKE_MAX_SPEED) snakeSpeed = SNAKE_MAX_SPEED;

  headPos = (float)(FIELD_END - (snakeCount - 1));   // 꼬리가 정확히 FIELD_END에 오도록
  lastHeadCell = lround(headPos);                    // 스폰 직후 전진음이 울리지 않게
}

// 이번 레벨의 시작 연출을 먼저 재생한 뒤 스폰한다. 매 레벨 진입은 항상 여기를 거친다.
void beginLevel() {
  clearBullets();   // 이전 레벨에서 날아가던 총알이 새 웨이브에 꽂히지 않도록
  applyDefaultColors();

  if (level == WHITE_UNLOCK_LEVEL)        whiteUnlockSweep();
  else if (level >= RGBW_RANDOM_LEVEL)  { randomizeColors(); blinkAllSwitches(); }
  else                                    standardCountdown();

  drawGameLcd();
  spawnWave();
  lastFrameMs = millis();
}

void fireBullet(uint32_t color) {
  for (uint8_t i = 0; i < MAX_BULLETS; i++) {
    if (bullets[i].active) continue;
    bullets[i].active = true;
    bullets[i].pos = (float)FIELD_START;
    bullets[i].color = color;
    soundPlay(SFX_SHOOT);   // 실제로 나간 총알에만 소리를 낸다
    return;
  }
  // 슬롯이 다 찼으면 조용히 버린다(난사 방지용 자연스러운 상한)
}

// 색이 틀린 총알에 대한 벌칙: 꼬리 끝(FIELD_END 쪽)에 무작위 색 세그먼트를 하나 붙인다.
// 배열 상한에 걸리면 길이는 그대로 두고 속도 가속만 적용된다.
void growSnakeTail() {
  if (snakeCount >= MAX_SNAKE_LEN) return;
  snakeColors[snakeCount] = randomSnakeColor();
  snakeCount++;
}

// 헤드를 제거하고 다음 세그먼트를 새 헤드로 승격한다.
void removeSnakeHead() {
  headPos += 1.0f;
  lastHeadCell = lround(headPos);   // 전진한 게 아니라 좌표가 밀린 것이므로 전진음 금지
  for (uint8_t i = 1; i < snakeCount; i++) snakeColors[i - 1] = snakeColors[i];
  snakeCount--;
}

// ---------------------------------------------------------------------------
// 렌더링
// ---------------------------------------------------------------------------
void renderSwitchLeds(uint32_t now) {
  for (uint8_t i = 0; i < NUM_SWITCH_LEDS; i++) {
    if (!switchUnlocked[i]) { strip.setPixelColor(i, 0); continue; }
    bool bright = (switchFlashOffAt[i] != 0 && now < switchFlashOffAt[i]);
    if (!bright && switchFlashOffAt[i] != 0) switchFlashOffAt[i] = 0;
    // 평소엔 1/3 밝기, 누른 직후 FLASH_MS 동안만 원색. 필드보다 눈에 띄게 부스트를 곱한다.
    float f = (bright ? 1.0f : 0.33f) * SWITCH_BOOST;
    strip.setPixelColor(i, scaleColor(currentSwitchColor[i], f));
  }
}

void renderPlay(uint32_t now) {
  clearField();

  for (uint8_t s = 0; s < snakeCount; s++) {
    long idx = lround(headPos) + s;
    if (idx >= FIELD_START && idx <= FIELD_END) strip.setPixelColor(idx, snakeColors[s]);
  }
  for (uint8_t i = 0; i < MAX_BULLETS; i++) {
    if (!bullets[i].active) continue;
    long idx = lround(bullets[i].pos);
    if (idx >= FIELD_START && idx <= FIELD_END) strip.setPixelColor(idx, bullets[i].color);
  }
  renderSwitchLeds(now);
  safeShow();
}

// ---------------------------------------------------------------------------
// 모듈 규약
// ---------------------------------------------------------------------------
void begin() {
  exitRequested = false;
  level = 1;
  score = 0;
  clearBullets();
  applyDefaultColors();
  state = S_READY;
  readyEnteredMs = millis();
  lastRainbowMs = 0;
  Input::flush();
  drawReadyLcd();

  strip.clear();
  for (uint8_t i = 0; i < NUM_SWITCH_LEDS; i++) {
    strip.setPixelColor(i, switchUnlocked[i] ? currentSwitchColor[i] : 0);
  }
  safeShow();
}

void updateReady(uint32_t now, uint8_t pressed) {
  if (pressed & (1 << BTN_BACK)) {      // Red - 메뉴로
    soundPlay(SFX_UI_BACK);
    exitRequested = true;
    return;
  }
  if (pressed) {                        // 나머지 아무 버튼 - 시작
    soundPlay(SFX_UI_SELECT);
    state = S_ARMED;                    // 선택음이 끝날 틈을 준 뒤 카운트다운으로
    armedEnteredMs = now;
    drawArmedLcd();
    strip.clear();
    for (uint8_t i = 0; i < NUM_SWITCH_LEDS; i++) {
      strip.setPixelColor(i, switchUnlocked[i] ? currentSwitchColor[i] : 0);
    }
    safeShow();
    return;
  }

  // 대기가 길어지면 스위치 LED까지 포함해 무지개를 흘린다(어트랙트 모드).
  if (now - readyEnteredMs >= READY_RAINBOW_MS && now - lastRainbowMs >= FRAME_MS) {
    lastRainbowMs = now;
    drawRainbow(now, RAINBOW_PERIOD_MS, 0.25f);
    safeShow();
  }
}

void updatePlay(uint32_t now, uint8_t pressed) {
  float dt = (now - lastFrameMs) / 1000.0f;
  if (dt > 0.05f) dt = 0.05f;   // 연출의 delay()로 프레임이 벌어졌을 때 확 튀는 것 방지
  lastFrameMs = now;

  // ---- 입력: 해금된 스위치만 발사 ----
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    if (!(pressed & (1 << i))) continue;
    if (!switchUnlocked[i]) continue;
    switchFlashOffAt[i] = now + FLASH_MS;
    fireBullet(currentSwitchColor[i]);
  }

  // ---- 총알 전진 ----
  for (uint8_t i = 0; i < MAX_BULLETS; i++) {
    if (!bullets[i].active) continue;
    bullets[i].pos += BULLET_SPEED * dt;
    if (bullets[i].pos > FIELD_END) bullets[i].active = false;
  }

  // ---- 지렁이 전진 ----
  if (snakeCount > 0) {
    headPos -= snakeSpeed * dt;
    long cell = lround(headPos);
    if (cell != lastHeadCell) {   // 칸이 실제로 바뀐 순간에만(속도와 무관하게 한 칸당 한 번)
      lastHeadCell = cell;
      soundPlay(SFX_SNAKE_STEP);
    }
  }

  // ---- 충돌: 총알이 지렁이 선두에 닿았는지만 검사 ----
  if (snakeCount > 0) {
    for (uint8_t i = 0; i < MAX_BULLETS; i++) {
      if (!bullets[i].active || bullets[i].pos < headPos) continue;
      bullets[i].active = false;
      if (bullets[i].color == snakeColors[0]) {
        soundPlay(SFX_BULLET_HIT);
        score += 10;
        removeSnakeHead();
        if (snakeCount == 0) break;
      } else {
        soundPlay(SFX_BULLET_MISS);
        snakeSpeed *= SNAKE_MISS_ACCEL;
        if (snakeSpeed > SNAKE_MAX_SPEED) snakeSpeed = SNAKE_MAX_SPEED;
        growSnakeTail();   // 빨라질 뿐 아니라 꼬리도 한 칸 길어진다
      }
    }
  }

  // ---- 종료 판정 ----
  if (snakeCount == 0) {
    soundPlay(SFX_LEVEL_CLEAR);
    flashField(WAVE_CLEAR_COLOR, 2, 150);
    level++;
    beginLevel();
    return;
  }
  if (headPos <= (float)FIELD_START) {
    soundPlay(SFX_GAME_OVER);
    char overBuf[12];
    snprintf(overBuf, sizeof(overBuf), "LEVEL %u", level);   // 최종 레벨(리셋 전 값)
    drawMessageLcd("GAME OVER", overBuf, ST77XX_RED);
    flashField(GAME_OVER_COLOR, 3, 200);

    // 다시 READY?로. 여기서 Red를 누르면 메뉴로 나갈 수 있다.
    level = 1;
    score = 0;
    clearBullets();
    applyDefaultColors();
    state = S_READY;
    readyEnteredMs = millis();
    Input::flush();
    drawReadyLcd();
    return;
  }

  // ---- LCD: 레벨/점수가 실제로 바뀐 순간에만(한 장 전송에 약 14ms) ----
  if (level != lcdShownLevel || score != lcdShownScore) drawGameLcd();

  renderPlay(now);
}

void update(uint32_t now) {
  uint8_t pressed = Input::takePressed();
  switch (state) {
    case S_READY:
      updateReady(now, pressed);
      break;
    case S_ARMED:
      if (now - armedEnteredMs >= GAME_START_DELAY_MS) {
        state = S_PLAY;
        beginLevel();
      }
      break;
    default:
      updatePlay(now, pressed);
      break;
  }
}

bool wantsExit() { return exitRequested; }

}  // namespace SnakeInvader
