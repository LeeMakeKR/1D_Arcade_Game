/*
  ===========================================================================
  1D Arcade V1.0 - 통합 스케치
  ===========================================================================
  지금까지의 테스트 코드를 하나로 묶은 본체다. 부팅 → 로고 → 메뉴 → 게임 순으로 돌고,
  게임에서 나오면 다시 메뉴로 돌아온다.

  이 파일이 하는 일은 구조뿐이다.
    - 하드웨어 초기화
    - 메뉴 상태 기계(게임 고르기 / 설정)
    - 고른 게임 모듈을 돌리고, 나가겠다고 하면 메뉴로 되돌리기
  게임 로직은 전부 헤더로 분리되어 있다.
    SnakeInvader.h / SoloPong.h / DuelPong.h
  공용 하드웨어·입력·설정·좌표 계산은 Arcade.h에 있다.

  ---------------------------------------------------------------------------
  버튼
  ---------------------------------------------------------------------------
    1번 Red    뒤로 / 메뉴로 나가기
    2번 Green  위로
    3번 Blue   선택
    4번 White  아래로
  게임 안에서는 네 버튼이 게임 나름의 역할을 하지만, Red는 어디서나 "메뉴로"다.

  ---------------------------------------------------------------------------
  메뉴
  ---------------------------------------------------------------------------
    MAIN ─┬─ GAME ─┬─ 1P ─┬─ SNAKE INVADER
          │        │      └─ SOLO PONG
          │        └─ 2P ── DUEL PONG
          └─ SETUP ─┬─ LED BRIGHTNESS
                    └─ SOUND VOLUME
  설정 항목은 Blue로 편집에 들어가 Green/White로 값을 올리고 내린다. 다시 Blue나 Red를
  누르면 편집이 끝나면서 값이 NVS에 저장되어 다음 부팅에도 유지된다. 밝기는 편집 중에 스트립 전체가 무지개로 돌아 실제 밝기를 보여주고,
  볼륨은 라인 LED에 파란 막대로 값을 보여준다.

  ---------------------------------------------------------------------------
  보드 두 대
  ---------------------------------------------------------------------------
  같은 펌웨어를 두 보드에 올린다. BOARD_ID_PIN(5번 토글)이 라인 LED가 달린 보드인지를
  나타내고, 그 값이 Duel Pong의 1P/2P 역할이 된다. 토글은 Duel Pong에 들어갈 때마다
  다시 읽으므로 재부팅 없이 역할을 바꿀 수 있다. 현재 상태는 메인 메뉴에 표시된다.
  렌더링은 이 값을 보지 않고 늘 스트립 전체를 그린다(Arcade.h의 hasLineLeds 주석 참고).

  ---------------------------------------------------------------------------
  보드 설정 (Tools 메뉴)
  ---------------------------------------------------------------------------
    - Board: ESP32S3 Dev Module
    - Partition Scheme: "Huge APP (3MB No OTA/1MB SPIFFS)"
      (세 게임의 사운드와 로고가 모두 들어가고 WiFi 스택까지 얹히므로 기본값으로는 빠듯하다)
  ===========================================================================
*/

#include "Arcade.h"

// 사운드 헤더는 PCM 배열 정의를 담고 있어서 이 .ino 한 곳에서만 include한다.
#include "sounds/blip01.h"
#include "sounds/blip02.h"
#include "sounds/laser.h"
#include "sounds/pickupCoin01.h"
#include "sounds/pickupCoin02.h"
#include "sounds/explosion01.h"
#include "sounds/explosion02.h"
#include "sounds/powerUp.h"
#include "sounds/fanfare.h"

// Arcade.h의 SfxId 순서와 정확히 같아야 한다.
static const SoundClip* const GAME_SOUNDS[NUM_SFX] = {
  &CLIP_blip01,        // SFX_UI_MOVE
  &CLIP_pickupCoin02,  // SFX_UI_SELECT
  &CLIP_blip02,        // SFX_UI_BACK
  &CLIP_powerUp,       // SFX_READY_BOTH
  &CLIP_blip01,        // SFX_COUNTDOWN
  &CLIP_blip02,        // SFX_START
  &CLIP_pickupCoin02,  // SFX_SERVE
  &CLIP_pickupCoin01,  // SFX_HIT_GREEN
  &CLIP_pickupCoin02,  // SFX_HIT_YELLOW
  &CLIP_blip01,        // SFX_HIT_RED
  &CLIP_laser,         // SFX_CPU_HIT
  &CLIP_explosion02,   // SFX_MISS
  &CLIP_pickupCoin02,  // SFX_MISS_OPPONENT
  &CLIP_laser,         // SFX_SHOOT
  &CLIP_explosion01,   // SFX_BULLET_HIT
  &CLIP_blip02,        // SFX_BULLET_MISS
  &CLIP_pickupCoin01,  // SFX_SNAKE_STEP
  &CLIP_powerUp,       // SFX_LEVEL_CLEAR
  &CLIP_fanfare,       // SFX_WIN
  &CLIP_explosion01    // SFX_GAME_OVER
};

// 게임 모듈은 Arcade.h와 사운드 표가 준비된 뒤에 include한다.
#include "SnakeInvader.h"
#include "SoloPong.h"
#include "DuelPong.h"

// ---------------------------------------------------------------------------
// 앱 상태
// ---------------------------------------------------------------------------
enum App : uint8_t { APP_MENU, APP_SNAKE, APP_SOLO, APP_DUEL };
App app = APP_MENU;

enum Page : uint8_t { P_MAIN, P_MODE, P_LIST_1P, P_LIST_2P, P_SETUP };
Page page = P_MAIN;

uint8_t cursor = 0;          // 현재 페이지의 커서 위치
bool editing = false;        // 설정 페이지에서 값 편집 중인가
bool menuDirty = true;       // LCD를 다시 그려야 하는가
uint32_t lastMenuFrameMs = 0;

// 페이지별 항목. 화면 그리기와 커서 범위를 한 곳에서 얻기 위해 표로 둔다.
//
// 항목 이름 길이 제한: 크기 2 글자는 12px이고 x=8에서 커서(">")까지 붙여 그리므로
// 화면 160px에 들어가려면 이름이 최대 11자다. 설정 페이지는 오른쪽에 값을 같이 그려서
// (SETTING_VALUE_X) 더 짧아야 한다. 이름은 games.md의 정식 표기를 따른다.
struct PageDef {
  const char* title;
  const char* items[3];
  uint8_t count;
};

const PageDef PAGES[] = {
  { "1D ARCADE", { "GAME",  "SETUP", nullptr }, 2 },   // P_MAIN
  { "GAME",      { "1P",    "2P",    nullptr }, 2 },   // P_MODE
  { "1 PLAYER",  { "SNAKE", "SOLO PONG", nullptr }, 2 },   // P_LIST_1P
  { "2 PLAYER",  { "DUEL PONG", nullptr, nullptr }, 1 },   // P_LIST_2P
  { "SETUP",     { "BRIGHT", "VOLUME", nullptr }, 2 },     // P_SETUP
};

// 설정 값 숫자를 그리는 x. 이름이 여기까지 침범하지 않도록 위 길이 제한을 지킬 것.
#define SETTING_VALUE_X  (LCD_W - 28)

// 밝기 조절 중에 스트립 전체를 채우는 무지개.
// 감쇠를 게임의 전체 점등 연출과 같은 0.25로 맞췄다. 여기서 보이는 밝기가 곧 게임에서
// 코트 전체가 켜질 때의 밝기라 미리보기가 정직해지고, 172칸을 세 채널로 켜는
// 가장 무거운 화면이라 전류도 같이 눌러진다.
#define MENU_RAINBOW_PERIOD_MS  4000
#define MENU_RAINBOW_SCALE      0.25f

// ---------------------------------------------------------------------------
// 메뉴 그리기
// ---------------------------------------------------------------------------
void drawMenuLcd() {
  const PageDef& p = PAGES[page];

  display.clearDisplay();
  display.setTextColor(ST77XX_CYAN);
  printCentered(p.title, 2, 4);
  display.drawFastHLine(0, 24, LCD_W, LCD_GREY);

  for (uint8_t i = 0; i < p.count; i++) {
    int16_t y = 34 + i * 26;
    bool sel = (i == cursor);

    if (sel) display.fillRect(0, y - 4, LCD_W, 24, editing ? 0x0208 : 0x2104);

    display.setTextSize(2);
    display.setTextColor(sel ? ST77XX_WHITE : LCD_GREY);
    display.setCursor(8, y);
    display.print(sel ? ">" : " ");
    display.print(p.items[i]);

    // 설정 페이지는 항목 오른쪽에 현재 값을 같이 보여준다
    if (page == P_SETUP) {
      uint8_t v = (i == 0) ? settingBrightness : settingVolume;
      display.setTextColor(sel ? ST77XX_YELLOW : LCD_GREY);
      display.setCursor(SETTING_VALUE_X, y);
      display.print(v);
    }
  }

  // 메인 화면에는 5번 토글 상태를 같이 보여준다. Duel Pong의 1P/2P가 여기서 갈리는데,
  // 게임에 들어가기 전에는 확인할 방법이 없어서 토글이 죽어도 알아채지 못한다.
  if (page == P_MAIN) {
    display.setTextSize(1);
    display.setTextColor(LCD_GREY);
    display.setCursor(LCD_W - 58, 100);
    display.print("BOARD: ");
    display.print(readBoardIsMain() ? "1P" : "2P");
  }

  display.setTextColor(LCD_GREY);
  if (page == P_SETUP && editing) printCentered("GRN/WHT:ADJUST  BLU:OK", 1, 116);
  else if (page == P_MAIN)        printCentered("GRN/WHT:MOVE  BLU:SELECT", 1, 116);
  else                            printCentered("BLU:SELECT  RED:BACK", 1, 116);

  display.display();
}

void drawMenuStrip(uint32_t now) {
  // 밝기를 조절하는 동안에는 스트립 전체(스위치 LED 포함)를 무지개로 돌린다.
  // 값을 막대로 보여주는 것보다, 세 채널이 다 켜진 화면을 직접 보는 편이 밝기 판단에 낫다.
  // 값 자체는 LCD 숫자로 확인할 수 있다.
  if (page == P_SETUP && editing && cursor == 0) {
    drawRainbow(now, MENU_RAINBOW_PERIOD_MS, MENU_RAINBOW_SCALE);
    safeShow();
    return;
  }

  strip.clear();
  drawMenuSwitchLeds();

  // 볼륨은 귀로 듣는 값이라 눈으로 볼 게 없어서, 라인 LED에 값을 막대로 보여준다.
  if (page == P_SETUP && editing) {
    int16_t n = (int16_t)((uint32_t)FIELD_LEN * settingVolume / SETTING_STEPS);
    for (int16_t i = 0; i < n; i++) {
      strip.setPixelColor(FIELD_START + i, scaleColor(colBlue(), 0.35f));
    }
  }
  safeShow();
}

// ---------------------------------------------------------------------------
// 메뉴 동작
// ---------------------------------------------------------------------------
void gotoPage(Page p) {
  page = p;
  cursor = 0;
  editing = false;
  menuDirty = true;
}

void startGame(App a) {
  app = a;
  switch (a) {
    case APP_SNAKE: SnakeInvader::begin(); break;
    case APP_SOLO:  SoloPong::begin();     break;
    case APP_DUEL:  DuelPong::begin();     break;
    default: break;
  }
}

void returnToMenu() {
  if (app == APP_DUEL) DuelPong::end();   // ESP-NOW 콜백/피어 정리
  app = APP_MENU;
  Input::flush();
  applyBrightness();   // 게임이 밝기를 건드렸을 수 있으니 설정값으로 되돌린다
  menuDirty = true;
}

// 현재 페이지에서 Blue(선택)를 눌렀을 때.
void menuSelect() {
  switch (page) {
    case P_MAIN:
      soundPlay(SFX_UI_SELECT);
      gotoPage(cursor == 0 ? P_MODE : P_SETUP);
      break;

    case P_MODE:
      soundPlay(SFX_UI_SELECT);
      gotoPage(cursor == 0 ? P_LIST_1P : P_LIST_2P);
      break;

    case P_LIST_1P:
      soundPlay(SFX_UI_SELECT);
      startGame(cursor == 0 ? APP_SNAKE : APP_SOLO);
      break;

    case P_LIST_2P:
      soundPlay(SFX_UI_SELECT);
      startGame(APP_DUEL);
      break;

    case P_SETUP:
      soundPlay(SFX_UI_SELECT);
      editing = !editing;            // Blue로 편집에 들어가고 Blue로 나온다
      if (!editing) Settings::save();  // 값이 확정되는 지점
      menuDirty = true;
      break;
  }
}

// 현재 페이지에서 Red(뒤로)를 눌렀을 때.
void menuBack() {
  if (page == P_SETUP && editing) {
    editing = false;
    Settings::save();              // Red로 빠져나와도 되돌리기는 없으므로 그대로 확정한다
    soundPlay(SFX_UI_BACK);
    menuDirty = true;
    return;
  }

  switch (page) {
    case P_MAIN:                              break;   // 최상단이라 갈 곳이 없다
    case P_MODE:
    case P_SETUP:     soundPlay(SFX_UI_BACK); gotoPage(P_MAIN); break;
    case P_LIST_1P:
    case P_LIST_2P:   soundPlay(SFX_UI_BACK); gotoPage(P_MODE); break;
  }
}

// 편집 중이면 값을, 아니면 커서를 움직인다. delta: +1 = 위/증가, -1 = 아래/감소.
void menuMove(int8_t delta) {
  if (page == P_SETUP && editing) {
    uint8_t& v = (cursor == 0) ? settingBrightness : settingVolume;
    int16_t nv = (int16_t)v + delta;
    if (nv < 0) nv = 0;
    if (nv > SETTING_STEPS) nv = SETTING_STEPS;
    if ((uint8_t)nv == v) return;
    v = (uint8_t)nv;

    if (cursor == 0) applyBrightness();
    else             applyVolume();
    Settings::markDirty();
    soundPlay(SFX_UI_MOVE);   // 볼륨은 이 소리로 바뀐 크기를 바로 확인할 수 있다
    menuDirty = true;
    return;
  }

  const PageDef& p = PAGES[page];
  if (p.count <= 1) return;
  int8_t nc = (int8_t)cursor - delta;              // 위로가 인덱스 감소
  if (nc < 0) nc = (int8_t)p.count - 1;            // 끝에서 반대편으로 감는다
  if (nc >= (int8_t)p.count) nc = 0;
  cursor = (uint8_t)nc;
  soundPlay(SFX_UI_MOVE);
  menuDirty = true;
}

void menuUpdate(uint32_t now) {
  uint8_t pressed = Input::takePressed();

  // 토글을 움직이면 메인 화면 표시가 바로 따라가야 확인이 된다
  static bool lastBoardMain = true;
  bool boardMain = readBoardIsMain();
  if (boardMain != lastBoardMain) { lastBoardMain = boardMain; menuDirty = true; }

  if (pressed & (1 << BTN_UP))     menuMove(+1);
  if (pressed & (1 << BTN_DOWN))   menuMove(-1);
  if (pressed & (1 << BTN_SELECT)) menuSelect();
  if (pressed & (1 << BTN_BACK))   menuBack();

  if (now - lastMenuFrameMs >= FRAME_MS) {
    lastMenuFrameMs = now;
    drawMenuStrip(now);
  }
  if (menuDirty) { menuDirty = false; drawMenuLcd(); }
}

// ---------------------------------------------------------------------------
void setup() {
  pinMode(BOARD_ID_PIN, INPUT_PULLUP);
  hasLineLeds = readBoardIsMain();   // Duel Pong 진입 시 다시 읽는다

  Input::begin();

  // 라인 LED가 없는 보드에서 176개를 다 내보내면 없는 픽셀에 데이터를 흘리며
  // show() 시간만 길어지므로(약 5.3ms → 0.12ms) 스위치 LED 4개로 줄인다.
  strip.updateLength(NUM_LEDS);
  strip.begin();
  strip.clear();
  safeShow();

  display.begin();
  soundEngineBegin(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN, GAME_SOUNDS, NUM_SFX);

  Settings::begin();   // 저장된 설정을 먼저 읽고 하드웨어에 반영한다
  applyBrightness();
  applyVolume();

  bootAnimation();   // 인터럽트를 붙이기 전에 실행

  randomSeed(esp_random());

  // ESP-NOW는 Duel Pong에서만 쓰지만, 초기화는 한 번만 해 두고 게임 진입 시
  // 콜백과 피어만 붙였다 뗀다. 반복 초기화보다 안정적이다.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_ps(WIFI_PS_NONE);   // 모뎀 슬립 끔: 입력 전달 지연 스파이크 방지
  esp_now_init();

  drawBootScreen();
  delay(BOOT_LOGO_MS);

  Input::attach();
  gotoPage(P_MAIN);
}

void loop() {
  uint32_t now = millis();
  Input::poll(now);

  switch (app) {
    case APP_MENU:
      menuUpdate(now);
      break;

    case APP_SNAKE:
      SnakeInvader::update(now);
      if (SnakeInvader::wantsExit()) returnToMenu();
      break;

    case APP_SOLO:
      SoloPong::update(now);
      if (SoloPong::wantsExit()) returnToMenu();
      break;

    case APP_DUEL:
      DuelPong::update(now);
      if (DuelPong::wantsExit()) returnToMenu();
      break;
  }
}
