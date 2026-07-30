/*
  Snake Invader(지렁이 슈팅) 게임 로직 테스트 스케치.
  Game_Logics.md / games.md의 Snake Invader 규칙을 실제 하드웨어에서 돌려보기 위한
  용도.
  
  LED 구성: 스위치 1~4에 내장된 WS281x 4개 + 그 뒤로 이어붙인 WS2815 라인 LED 176개가
  전기적으로 한 줄(GPIO8)로 연결되어 있어 총 180개다.
    - index 0~3  : 스위치 1~4에 내장된 LED (버튼 색 표시용)
    - index 4~179: 실제 게임 필드(지렁이/총알이 움직이는 176칸)
  필드 인덱스가 클수록(179에 가까울수록) 스위치에서 먼 "지렁이 출현 쪽" 끝이고,
  작을수록(4에 가까울수록) "플레이어 쪽" 끝이다. 지렁이는 큰 인덱스에서 출현해
  작은 인덱스(플레이어) 방향으로 전진하고, 총알은 그 반대 방향으로 발사된다.

  색상 해금 규칙:
    - 레벨 1~9  : 스위치1=Blue, 스위치2=Green, 스위치4=Red 3색만 사용(스위치3은 잠김/소등).
    - 레벨 10   : 스위치3이 White로 해금. 해금 순간 필드 반대편 끝에서 흰색이 플레이어
                  쪽으로 쭉 채워지는 연출 후, 스위치3이 흰색으로 5회 점멸하고 시작.
    - 레벨 20+  : 매 레벨 시작 전 4개 스위치의 색(R/G/B/W)이 매번 무작위로 재배정되고,
                  새 색으로 스위치 4개가 동시에 3회 점멸한 뒤 시작(레벨 10 이전까지 쓰던
                  "빨강 빨강 빨강 초록" 카운트다운을 대신함).
  모든 레벨(1레벨 포함) 시작 전에는 위 특수 연출이 없는 한 기본값으로 필드 전체가
  빨강 1초, 빨강 1초, 빨강 1초, 초록 1초 순서로 점등된 뒤 지렁이가 출현한다.

  총알 색 불일치 페널티는 스펙에 "가속 또는 꼬리 증가" 둘 다 허용되어 있는데, 배열
  크기를 고정길이로 단순하게 유지하려고 가속 쪽을 선택했다.
*/

#include <Adafruit_NeoPixel.h>

#define LED_PIN        8    // WS2815 데이터 핀(readme.md 배선 기준, 프로토콜은 WS2812 호환)
#define LED_TYPE   (NEO_GRB + NEO_KHZ800)  // WS2812/WS2815 공용 800KHz 1-wire 프로토콜
#define NUM_LEDS_TOTAL 180  // 스위치 내장 LED 4개 + 라인 LED 176개  60개/1미터짜리를 3미터 구입후, 4개를 스위치용으로 분리하여 사용
#define NUM_SWITCH_LEDS 4   // index 0~3: 스위치 1~4 내장 LED
#define FIELD_START    NUM_SWITCH_LEDS         // 필드 첫 칸(플레이어 쪽 끝) = 4
#define FIELD_END      (NUM_LEDS_TOTAL - 1)    // 필드 마지막 칸(지렁이 출현 쪽 끝) = 179

#define DEBOUNCE_MS    30   // 스위치 채터링 무시 시간
#define FLASH_MS       120  // 버튼을 눌렀을 때 해당 스위치 LED를 밝게 켜 두는 시간(시각 피드백)
#define BRIGHTNESS     255   // 180개라 전류/눈부심 부담이 커서 낮게 고정

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
#define WHITE_SWEEP_STEP_MS     12   // 흰색 해금 연출: 필드 한 칸 채우는 데 걸리는 시간(176칸 전체 약 2.1초)
#define SWITCH_UNLOCK_BLINK_MS  150  // 흰색 해금 후 스위치3 점멸 on/off 각각의 시간
#define SWITCH_UNLOCK_BLINK_TIMES 5  // 흰색 해금 후 스위치3 점멸 횟수
#define RGBW_BLINK_MS            150 // 레벨20+ 색 재배정 후 스위치 4개 동시 점멸 on/off 시간
#define RGBW_BLINK_TIMES         3   // 레벨20+ 색 재배정 후 점멸 횟수

#define SWITCH_LED_BOOST         1.5f // 스위치 LED는 필드보다 눈에 잘 띄어야 해서 기본 밝기의 1.5배로 표시

const uint8_t SWITCH_PINS[] = {7, 15, 16, 17};  // 스위치 0-3 (현재 배선 순서 0,1,2,3)
const uint8_t NUM_SWITCHES = sizeof(SWITCH_PINS) / sizeof(SWITCH_PINS[0]);

Adafruit_NeoPixel strip(NUM_LEDS_TOTAL, LED_PIN, LED_TYPE);

// 원색(풀 밝기) - 스위치 기본색/스네이크 세그먼트/총알에 사용
const uint32_t COLOR_RED   = strip.Color(255, 0, 0);
const uint32_t COLOR_GREEN = strip.Color(0, 255, 0);
const uint32_t COLOR_BLUE  = strip.Color(0, 0, 255);
const uint32_t COLOR_WHITE = strip.Color(255, 255, 255);

// 매 레벨 시작 전 기본 카운트다운("빨강*3 + 초록")에 쓰는 살짝 죽인 색
const uint32_t COUNTDOWN_RED   = strip.Color(80, 0, 0);
const uint32_t COUNTDOWN_GREEN = strip.Color(0, 80, 0);

// 웨이브클리어/게임오버 이벤트 피드백 색. 실제 하드웨어에서 두 이벤트의 표시색이
// 뒤바뀌어 보인다는 피드백을 반영해, 값 자체를 서로 바꿔 넣었다(웨이브클리어에
// 원래 게임오버용 빨강값을, 게임오버에 원래 웨이브클리어용 초록값을 사용).
const uint32_t WAVE_CLEAR_FLASH_COLOR = strip.Color(60, 0, 0);
const uint32_t GAME_OVER_FLASH_COLOR  = strip.Color(0, 60, 0);

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

// 기본 색 배정(요청 반영): 스위치 인덱스 0~3 = Red, Green, Blue, White 고정.
void applyDefaultColors() {
  currentSwitchColor[0] = COLOR_RED;
  currentSwitchColor[1] = COLOR_GREEN;
  currentSwitchColor[2] = COLOR_BLUE;
  currentSwitchColor[3] = COLOR_WHITE;
  switchUnlocked[0] = true;
  switchUnlocked[1] = true;
  switchUnlocked[2] = true;
  switchUnlocked[3] = true;
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
  for (uint8_t b = 0; b < times; b++) {
    for (uint8_t i = 0; i < NUM_SWITCH_LEDS; i++) strip.setPixelColor(i, currentSwitchColor[i]);
    strip.show();
    delay(onMs);
    for (uint8_t i = 0; i < NUM_SWITCH_LEDS; i++) strip.setPixelColor(i, 0);
    strip.show();
    delay(onMs);
  }
}

// 기본 웨이브 시작 전 카운트다운: 필드 전체가 빨강-빨강-빨강-초록 순으로 1초씩 유지.
// LCD가 없어서 "3,2,1" 숫자 대신 색으로 시작 타이밍을 알려주는 용도.
void standardCountdown() {
  const uint32_t sequence[4] = {COUNTDOWN_RED, COUNTDOWN_RED, COUNTDOWN_RED, COUNTDOWN_GREEN};
  for (uint8_t s = 0; s < 4; s++) {
    for (uint16_t i = FIELD_START; i <= FIELD_END; i++) strip.setPixelColor(i, sequence[s]);
    strip.show();
    delay(COUNTDOWN_STEP_MS);
  }
  for (uint16_t i = FIELD_START; i <= FIELD_END; i++) strip.setPixelColor(i, 0);
  strip.show();
}

// 레벨 10 진입 시 1회성 연출: 필드 반대편(먼 쪽, FIELD_END)에서 플레이어 쪽으로
// 흰색이 쭉 채워지고, 다 채워지면 White 스위치(index 3)가 5회 점멸한다.
void whiteUnlockSweepAndBlink() {
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

// 현재 해금된 스위치 색 중 하나를 무작위로 골라 지렁이 세그먼트 색으로 사용한다.
uint32_t randomSnakeColor() {
  uint8_t candidates[NUM_SWITCH_LEDS];
  uint8_t n = 0;
  for (uint8_t i = 0; i < NUM_SWITCH_LEDS; i++) {
    if (switchUnlocked[i]) candidates[n++] = i;
  }
  return currentSwitchColor[candidates[random(n)]];
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
}

// 이번 레벨에 맞는 시작 연출(기본 카운트다운 / 흰색 해금 / RGBW 재배정)을 먼저 재생한 뒤 스폰한다.
// 매 레벨(레벨1 포함) 진입 시 항상 이 함수를 거치도록 한다.
void beginLevel() {
  applyDefaultColors();

  if (level == WHITE_UNLOCK_LEVEL) {
    whiteUnlockSweepAndBlink();
  } else if (level >= RGBW_RANDOM_LEVEL) {
    randomizeAllSwitchColorsRGBW();
    blinkAllSwitchesCurrent(RGBW_BLINK_TIMES, RGBW_BLINK_MS);
  } else {
    standardCountdown();
  }

  spawnWave();
}

void fireBullet(uint32_t color) {
  for (uint8_t i = 0; i < MAX_BULLETS; i++) {
    if (!bullets[i].active) {
      bullets[i].active = true;
      bullets[i].pos = (float)FIELD_START;
      bullets[i].color = color;
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

void resetGame() {
  level = 1;
  score = 0;
  for (uint8_t i = 0; i < MAX_BULLETS; i++) bullets[i].active = false;
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

  bootAnimation();  // 인터럽트 걸기 전에 먼저 실행(초기화 도중 오작동 방지)

  resetGame();

  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    // FALLING만 사용해 릴리즈 엣지/노이즈에 의한 ISR 과다 발생을 줄인다.
    attachInterruptArg(SWITCH_PINS[i], handleSwitchInterrupt, (void*)(uintptr_t)i, FALLING);
  }

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
  }

  // ---- 충돌 판정: 총알이 지렁이 "선두"에 닿았는지만 검사(스펙 그대로) ----
  if (snakeCount > 0) {
    for (uint8_t i = 0; i < MAX_BULLETS; i++) {
      if (!bullets[i].active) continue;
      if (bullets[i].pos >= headPos) {
        bullets[i].active = false;
        if (bullets[i].color == snakeColors[0]) {
          score += 10;
          removeSnakeHead();
          if (snakeCount == 0) break;  // 이번 프레임에 더 처리할 세그먼트 없음
        } else {
          snakeSpeed *= SNAKE_MISS_ACCEL;
          if (snakeSpeed > SNAKE_MAX_SPEED) snakeSpeed = SNAKE_MAX_SPEED;
        }
      }
    }
  }

  // ---- 종료 판정 ----
  if (snakeCount == 0) {
    flashField(WAVE_CLEAR_FLASH_COLOR, 2, 150);  // 웨이브 클리어 피드백
    level++;
    beginLevel();  // 다음 레벨의 시작 연출(카운트다운/해금/색 재배정) 후 스폰
  } else if (headPos <= (float)FIELD_START) {
    flashField(GAME_OVER_FLASH_COLOR, 3, 200);  // 게임오버 피드백
    resetGame();
  }

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
