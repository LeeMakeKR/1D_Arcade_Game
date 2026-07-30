/*
  ===========================================================================
  스위치 + LED + 사운드 멀티트랙 테스트 (wav를 C 배열로 구워 플래시에서 바로 재생)
  ===========================================================================
  프로젝트 루트 sounds/wav/ 의 wav를 sounds/wav2header.py로 변환해 만든 헤더의 PCM
  배열을 그대로 재생한다. 그 헤더들(SoundData.h, SoundClip.h, 소리별 <이름>.h)은 이
  스케치 폴더 안의 sounds/ 에 들어 있다. Arduino IDE는 스케치 폴더 안의 파일만 컴파일
  대상으로 삼기 때문이다. 프로젝트 루트 sounds/ 의 wav와 header는 원본 확인용으로
  남겨둔 것이고, 실제로 컴파일되는 것은 이 스케치 안의 복사본이다.

  소리를 추가/교체하는 방법:
    1) 루트 sounds/wav/ 에 wav를 넣거나 교체한다(mono, 8/16bit, 22050Hz의 정수배).
    2) 루트 sounds/ 폴더에서 `py wav2header.py` 를 실행한다. 헤더가 다시 만들어지고
       이 스케치의 sounds/ 폴더로도 자동 복사된다(스크립트의 COPY_TO 목록 참고).
    3) 스케치를 재컴파일해서 Upload한다.
    자세한 사용법과 옵션은 sounds/wav2header.py 상단 주석에 적어두었다.

  이 테스트는 랜덤 재생을 하므로 sounds/SoundData.h(전체 묶음)를 쓴다. 실제 게임 코드처럼
  소리를 몇 개만 쓸 때는 개별 헤더를 골라 include하면 나머지는 플래시를 차지하지 않는다
  (실측: laser 하나만 쓰면 +3.4KB, 전체 묶음은 +187KB).

  ---------------------------------------------------------------------------
  동작
  ---------------------------------------------------------------------------
    - 스위치 1~4 (GPIO7/15/16/17)를 누르면 해당 LED가 켜지고 랜덤 사운드가 재생된다.
      5번 스위치(GPIO18)는 이 테스트에서 사용하지 않는다(핀을 건드리지 않음).
    - 여러 스위치를 동시에(또는 소리가 나는 중에) 눌러도 앞의 소리가 끝나기를
      기다리지 않고 즉시 겹쳐서 난다. 최대 SOUND_MAX_VOICES개까지 동시 재생되며,
      LCD의 "V:n"이 지금 몇 개가 겹쳐 나고 있는지 보여준다.
    - 예외로 fanfare는 재생될 때 다른 소리를 모두 끊고 혼자 재생된다(승리 음이
      효과음에 침범당하지 않도록 SoundData의 클립에 alwaysSolo로 지정되어 있다).
      랜덤으로 fanfare가 뽑히면 그 동안 V:1로 고정되는 것이 정상 동작이다.
      대상은 sounds/wav2header.py의 ALWAYS_SOLO 목록에서 정한다.

  ---------------------------------------------------------------------------
  보드 설정 (Tools 메뉴) - 이것만 맞으면 스케치 Upload 한 번으로 끝
  ---------------------------------------------------------------------------
    - Board: ESP32S3 Dev Module (사용 중인 ESP32-S3 보드)
    - Partition Scheme: "Huge APP (3MB No OTA/1MB SPIFFS)" 권장.
      PCM이 실행 이미지에 함께 들어가므로 앱 공간이 넉넉한 쪽이 안전하다.
    - PSRAM: 켜든 끄든 무관하다(이 스케치는 PSRAM을 쓰지 않는다).
  ===========================================================================
*/

#include <Adafruit_NeoPixel.h>  // WS2815 LED 스트립 제어
#include <Adafruit_GFX.h>       // Adafruit_ST7735가 상속하는 그래픽(텍스트/도형) 베이스 라이브러리
#include <Adafruit_ST7735.h>    // ST7735 TFT LCD(128x160) 드라이버
#include <SPI.h>
#include "SoundEngine.h"        // 플래시 PCM 멀티트랙 믹서

#define LED_PIN       8      // WS2812 데이터 핀 (readme.md 배선 기준)
#define DEBOUNCE_MS   30     // 스위치 채터링(짧은 시간 내 여러 번 튀는 신호) 무시 시간
#define FLASH_MS      300    // 버튼을 눌렀을 때 해당 LED를 켜 두는 시간(시각적 피드백용, 소리 길이와 무관)
#define BOOT_STEP_MS  1000   // 부팅 애니메이션에서 LED 하나당 점등 유지 시간

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

#define I2S_BCLK_PIN  42  // I2S 비트클럭 (MAX98357A BCLK)
#define I2S_LRC_PIN   41  // I2S 워드셀렉트/좌우채널클럭 (MAX98357A LRCLK)
#define I2S_DOUT_PIN  40  // I2S 데이터 출력 (MAX98357A DIN)

// 이 테스트는 1~4번 스위치만 쓴다(5번 GPIO18은 토글 스위치라 순간 트리거로 쓸 수 없어 제외).
// LED와 1:1로 대응하므로 스위치 개수가 곧 LED 개수다.
const uint8_t SWITCH_PINS[] = {7, 15, 16, 17};
const uint8_t NUM_SWITCHES = sizeof(SWITCH_PINS) / sizeof(SWITCH_PINS[0]);

Adafruit_NeoPixel strip(NUM_SWITCHES, LED_PIN, NEO_GRB + NEO_KHZ800);  // WS2815 4개 제어 객체

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
  // 40KB(160x128x2) 전송이라 24MHz에서 약 14ms 걸린다. 매 loop마다 부르지 말 것.
  void display() { tft.drawRGBBitmap(0, 0, getBuffer(), LCD_W, LCD_H); }
};

Lcd display;

// 스위치 1~4에 대응하는 LCD 표시색(LED 색과 같은 순서)
const uint16_t SWITCH_TEXT_COLORS[4] = {ST77XX_RED, ST77XX_GREEN, ST77XX_BLUE, ST77XX_WHITE};

// 아래 2개 배열은 ISR이 쓰고 loop()가 읽는 값이라 volatile 필요
// (컴파일러가 최적화 과정에서 값이 안 바뀐다고 착각해 캐싱해버리는 걸 방지)
volatile bool checkPending[NUM_SWITCHES] = {false};   // "디바운스 시간 지나면 이 스위치 다시 확인해야 함" 표시
volatile uint32_t lastChangeMs[NUM_SWITCHES] = {0};   // 마지막으로 핀 레벨이 바뀐 시각(ms) - 디바운스 타이머 기준점
bool switchPressed[NUM_SWITCHES] = {false};           // 디바운스까지 끝난 확정 상태(loop에서만 접근)

bool ledOn[NUM_SWITCHES] = {false};       // 각 LED가 현재 켜져 있는지
uint32_t ledOffAt[NUM_SWITCHES] = {0};    // 각 LED를 꺼야 할 목표 시각(millis 기준) - delay() 없이 논블로킹 소등

uint8_t lastPressedNum = 0;      // 마지막으로 눌린 버튼 번호(1~4). 0 = 아직 없음
const char* lastSoundName = "";  // 마지막으로 재생 요청한 사운드 이름 - SoundData의 static 문자열을 가리킴
char pcmInfo[16] = "";           // 플래시에 들어 있는 PCM 총 크기 - LCD 첫 줄에 표시
uint8_t shownVoices = 0;         // LCD에 마지막으로 그린 활성 트랙 수 - 값이 바뀔 때만 다시 그리기 위함
bool lcdDirty = true;            // 이번 loop에서 LCD를 다시 그려야 하는지(한 loop에 한 번만 그리도록)

// 버튼 인덱스(0~3)에 대응하는 LED 색.
// bootAnimation()과 버튼 눌림 처리 두 군데에서 같은 매핑을 써야 해서 함수로 분리.
uint32_t switchColor(uint8_t idx) {
  switch (idx) {
    case 0: return strip.Color(255, 0, 0);       // R
    case 1: return strip.Color(0, 255, 0);       // G
    case 2: return strip.Color(0, 0, 255);       // B
    case 3: return strip.Color(255, 255, 255);   // W
    default: return strip.Color(0, 0, 0);        // Off
  }
}

// 스위치 핀 레벨이 바뀔 때(CHANGE) 호출되는 ISR.
// ISR은 짧게 끝내야 하므로 시각 기록 + 플래그 세팅만 하고, 디바운스 판정과
// LED/LCD/사운드 처리는 loop()에 위임한다.
void IRAM_ATTR handleSwitchInterrupt(void* arg) {
  uint8_t idx = (uint8_t)(uintptr_t)arg;  // attachInterruptArg로 넘긴 스위치 인덱스를 복원
  lastChangeMs[idx] = millis();
  checkPending[idx] = true;
}

// WS2815 타이밍 안정화용 show 함수 (인터럽트 전역 차단 제거)
void safeShow() {
  strip.show();
  delayMicroseconds(300);  // WS2815 Reset Time (>=280us) 보장
}

// 전원 켜졌을 때 LED 4개를 하나씩 순서대로 켰다가 전부 끄는 데모(배선/LED 확인용)
void bootAnimation() {
  strip.clear();
  safeShow();

  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    strip.setPixelColor(i, switchColor(i));
    safeShow();
    delay(BOOT_STEP_MS);
  }

  strip.clear();
  safeShow();
}

// 현재 상태를 LCD에 다시 그린다.
//   1줄: 플래시에 구워진 PCM 총 크기
//   2줄: 동시 재생 중인 트랙 수(V:n)와 마지막으로 누른 버튼 번호
//   3줄: 마지막으로 재생 요청한 사운드 이름
void redrawLCD() {
  display.clearDisplay();
  display.setTextSize(2);

  display.setTextColor(ST77XX_CYAN);
  display.setCursor(0, 0);
  display.print(pcmInfo);

  display.drawFastHLine(0, 24, LCD_W, LCD_GREY);

  display.setTextColor(ST77XX_WHITE);
  display.setCursor(0, 34);
  display.print("V:");
  display.print(shownVoices);
  if (lastPressedNum > 0) {
    display.print(" SW:");
    display.setTextColor(SWITCH_TEXT_COLORS[lastPressedNum - 1]);
    display.print(lastPressedNum);
  }

  // 사운드 이름은 최대 12자 정도라 크기 2(글자폭 12px)로 화면 폭에 딱 들어간다
  display.setTextColor(ST77XX_YELLOW);
  display.setCursor(0, 62);
  display.print(lastSoundName);

  // 동시 재생 슬롯을 칸으로 표시 - 멀티트랙이 실제로 겹쳐 도는지 한눈에 보인다
  const int16_t boxW = LCD_W / SOUND_MAX_VOICES;
  for (uint8_t i = 0; i < SOUND_MAX_VOICES; i++) {
    int16_t x = i * boxW;
    if (i < shownVoices) display.fillRect(x + 2, 98, boxW - 4, 26, ST77XX_GREEN);
    else                 display.drawRect(x + 2, 98, boxW - 4, 26, LCD_GREY);
  }

  display.display();
}

void setup() {
  randomSeed(esp_random());  // ESP32 하드웨어 난수로 시드를 줘서 매번 다른 랜덤 시퀀스가 나오게 함

  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    pinMode(SWITCH_PINS[i], INPUT_PULLUP);  // 눌리면 GND로 연결되는 배선이라 내부 풀업 사용(안 누르면 HIGH)
  }

  strip.begin();
  strip.setBrightness(200);
  strip.clear();
  safeShow();

  soundEngineBegin(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN);  // 렌더링 단계가 없어 즉시 끝난다
  snprintf(pcmInfo, sizeof(pcmInfo), "PCM %uK", soundTotalBytes() / 1024);

  display.begin();
  display.setTextSize(2);
  display.setTextColor(ST77XX_WHITE);

  bootAnimation();  // 인터럽트를 걸기 전에 실행 - 초기화 도중 스위치 신호로 오작동하는 것을 방지
  redrawLCD();

  // 인터럽트는 다른 초기화가 모두 끝난 뒤 마지막에 붙임 - 초기화 중간에 발생한
  // 핀 변화 때문에 아직 준비 안 된 자료구조(사운드 큐 등)를 건드리는 일이 없게 하려고
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    attachInterruptArg(SWITCH_PINS[i], handleSwitchInterrupt, (void*)(uintptr_t)i, CHANGE);
  }
}

void loop() {
  uint32_t now = millis();  // 이번 반복에서 공통으로 쓸 기준 시각(호출마다 값이 바뀌는 걸 방지)

  // 스위치를 하나씩 훑는다. 여러 개를 동시에 눌렀으면 이 루프에서 각각 처리되어
  // 재생 요청이 여러 개 큐에 들어가고, 오디오 태스크가 그것들을 겹쳐서 재생한다.
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    // ISR이 세팅한 "재확인 필요" 표시 중, 디바운스 시간이 지난 것만 실제로 처리
    if (checkPending[i] && (now - lastChangeMs[i] >= DEBOUNCE_MS)) {
      checkPending[i] = false;

      bool pressed = (digitalRead(SWITCH_PINS[i]) == LOW);  // 풀업 배선이라 눌리면 LOW
      if (pressed == switchPressed[i]) continue;            // 디바운스 후에도 실제로 바뀐 경우만 반응
      switchPressed[i] = pressed;
      if (!pressed) continue;                               // 누를 때만 반응(뗄 때는 무시)

      lastPressedNum = i + 1;
      strip.setPixelColor(i, switchColor(i));
      safeShow();
      ledOn[i] = true;
      ledOffAt[i] = now + FLASH_MS;  // FLASH_MS 뒤에 꺼지도록 예약(아래 소등 블록에서 실행)

      uint8_t id = random(soundCount());
      lastSoundName = soundName(id);
      // 큐에 요청만 넣고 즉시 리턴(논블로킹) - 실제 믹싱은 core 0의 오디오 태스크가 처리.
      // 이미 다른 소리가 나는 중이면 기다리지 않고 겹쳐서 재생된다.
      soundPlay(id);

      lcdDirty = true;
    }
  }

  // FLASH_MS가 지난 LED를 꺼줌 - delay() 없이 millis() 비교만으로 논블로킹 처리
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    if (ledOn[i] && now >= ledOffAt[i]) {
      strip.setPixelColor(i, strip.Color(0, 0, 0));
      safeShow();
      ledOn[i] = false;
    }
  }

  // 동시 재생 트랙 수가 바뀌었을 때만 화면을 갱신한다(LCD SPI 전송이 매 loop마다 일어나지 않도록)
  uint8_t nowVoices = soundActiveVoices();
  if (nowVoices != shownVoices) {
    shownVoices = nowVoices;
    lcdDirty = true;
  }

  if (lcdDirty) {
    redrawLCD();
    lcdDirty = false;
  }
}
