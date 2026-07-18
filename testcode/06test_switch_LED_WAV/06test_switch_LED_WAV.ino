/*
  03SwitchandLEDtest(스위치+LED+LCD) + 05test_wavplay(MAX98357A I2S wav 재생)를
  합친 테스트. 부팅 시 LED 순차 점등은 기존과 동일하고, 이후 스위치1~4를 누르면
  매번 랜덤한 wav를 재생하면서 LCD에 그 파일 이름을 표시한다.

  wav 재생은 반드시 논블로킹이어야 한다(스위치 입력이 실시간으로 계속 반응해야
  하는 게임 코드이기 때문). i2s_write()는 DMA 버퍼가 찰 때까지 호출한 쪽을
  멈춰 세우는 블로킹 함수라서, loop()에서 직접 부르면 재생되는 동안 스위치를
  못 읽는다. 그래서 실제 파일 읽기+i2s_write는 core 0에 별도로 띄운
  audioTask로 넘기고, loop()(core 1에서 도는 기본 Arduino 루프)는 큐에 재생할
  파일 번호만 던져 넣고 바로 다음 일을 한다.

  * 왜 하필 core 0인가:
    Arduino-ESP32 프레임워크는 setup()/loop()를 "loopTask"라는 FreeRTOS
    태스크로 감싸서 실행하는데, 이 태스크는 기본적으로 core 1(APP_CPU)에
    고정되어 있다. 이 스케치는 WiFi/BT를 쓰지 않아서 core 0(PRO_CPU)이
    사실상 놀고 있으므로, 오디오 태스크를 core 0에 배정하면 loop()의
    스위치/LED/LCD 처리와 오디오 재생이 물리적으로 다른 코어에서 진짜
    동시에 돈다. 같은 코어(1)에 둬도 두 태스크 모두 블로킹 지점(큐 대기,
    I2S DMA 대기)에서 스케줄러에 양보하므로 동작 자체는 하겠지만, 다른
    태스크가 CPU를 오래 잡고 있을 때 생기는 지연(jitter) 가능성이 남는다.
    "재생 중 절대 안 멈춰야 한다"는 요구사항을 가장 확실하게 만족시키려고
    아예 코어를 분리했다.

  wav 파일을 보드에 올리는 방법(플러그인 설치, data 폴더, 커스텀 파티션 테이블
  등)은 testcode/05test_wavplay/05test_wavplay.ino 상단 주석에 자세히 적어둔
  것과 동일하다. 이 스케치도 같은 방식이 필요:
    - Tools > Partition Scheme > "Custom" (이 폴더의 partitions.csv가 적용됨)
    - Tools > Flash Size > 16MB
    - data 폴더에 wav 7개가 이미 복사되어 있음
    - 순서: 1) 스케치 Upload(새 파티션 테이블 반영) → 2) Ctrl+Shift+P →
      "Upload LittleFS to Pico/ESP8266/ESP32" (wav 데이터 반영)
*/

#include <Adafruit_NeoPixel.h>  // WS2812 LED 스트립 제어
#include <Adafruit_GFX.h>       // Adafruit_PCD8544가 상속하는 그래픽(텍스트/도형) 베이스 라이브러리
#include <Adafruit_PCD8544.h>   // Nokia 5110 LCD(PCD8544 컨트롤러) 드라이버
#include <LittleFS.h>           // 플래시에 저장된 wav 파일을 읽기 위한 파일시스템
#include <driver/i2s.h>         // MAX98357A로 오디오를 스트리밍하기 위한 ESP-IDF I2S 저수준 드라이버

#define LED_PIN       8     // WS2812 데이터 핀 (readme.md 배선 기준)
#define DEBOUNCE_MS   30     // 스위치 채터링(짧은 시간 내 여러 번 튀는 신호) 무시 시간
#define FLASH_MS      300    // 버튼을 눌렀을 때 해당 LED를 켜 두는 시간(시각적 피드백용, 오디오 길이와 무관하게 고정)
#define BOOT_STEP_MS  1000   // 부팅 애니메이션에서 LED 하나당 점등 유지 시간

#define LCD_DC   9   // LCD Data/Command 선택 핀
#define LCD_CS   10  // LCD SPI Chip Select (FSPI 하드웨어 기본 CS0)
#define LCD_RST  14  // LCD 하드웨어 리셋 핀

#define I2S_BCLK_PIN  42  // I2S 비트클럭 (MAX98357A BCLK)
#define I2S_LRC_PIN   41  // I2S 워드셀렉트/좌우채널클럭 (MAX98357A LRCLK)
#define I2S_DOUT_PIN  40  // I2S 데이터 출력 (MAX98357A DIN)

#define I2S_DMA_BUF_COUNT  4    // I2S DMA 버퍼 개수 - 너무 적으면 언더런(끊김), 너무 많으면 지연/메모리 낭비
#define I2S_DMA_BUF_LEN    256  // DMA 버퍼 하나당 샘플 수
#define READ_CHUNK_BYTES   512  // LittleFS에서 한 번에 읽어오는 바이트 수(스택에 두는 임시 버퍼 크기)

// wav의 fmt 청크에서 뽑아낸, 재생에 필요한 최소한의 정보를 담는 구조체.
// 함수 리턴값 하나로 여러 값(채널수/샘플레이트/비트수/데이터 위치)을 한번에 넘기기 위해 사용.
// * 이 구조체는 반드시 파일 맨 위(#define 블록 바로 다음, 다른 전역 변수보다도 앞)에 있어야 한다.
//   Arduino IDE가 .ino 안의 함수 프로토타입을 자동 생성할 때, 파일의 첫 번째 실행문(예:
//   전역 배열 선언) 바로 앞에 모든 함수 선언을 끼워 넣는다. parseWavHeader()가 이 구조체를
//   매개변수로 받는데, 만약 구조체 정의가 그 자동 삽입 지점보다 아래에 있으면 "WavInfo has
//   not been declared" 에러가 난다
struct WavInfo {
  uint16_t numChannels;
  uint32_t sampleRate;
  uint16_t bitsPerSample;
  uint32_t dataSize;
  uint32_t dataOffset;
};

// 4개는 LED와 매칭되는 스위치, 5번째(GPIO18)는 LCD 표시 전용(LED 없음)
// 배열로 두는 이유: 5개를 각각 변수로 다루지 않고 for문으로 일괄 처리하기 위해
const uint8_t SWITCH_PINS[] = {7, 15, 16, 17, 18};
const uint8_t NUM_SWITCHES = sizeof(SWITCH_PINS) / sizeof(SWITCH_PINS[0]);  // 배열 길이를 직접 계산 - 개수를 바꿔도 다른 코드 수정 불필요
const uint8_t NUM_LEDS = 4;  // 실제 LED 개수(스위치 5개 중 앞 4개만 LED와 매칭됨을 명시)

// LittleFS(내부 플래시)에 올려둔 wav 파일 경로들 - 버튼을 누르면 이 중 하나를 무작위로 재생
const char* SOUND_FILES[] = {
  "/blast.wav",
  "/coin1.wav",
  "/dead.wav",
  "/explosion01.wav",
  "/powerUp.wav",
  "/shot1.wav",
  "/shot2.wav"
};
const uint8_t NUM_SOUNDS = sizeof(SOUND_FILES) / sizeof(SOUND_FILES[0]);  // random(NUM_SOUNDS) 범위 계산용

#define SOUND_QUEUE_LEN  4  // loop()가 짧은 시간에 여러 번 눌러도 몇 개까지 재생 요청을 밀어둘지(가득 차면 이후 요청은 버려짐)

// core1(loop)과 core0(audioTask) 사이에서 "재생할 wav 번호"를 안전하게 주고받기 위한 FreeRTOS 큐.
// 전역 변수를 직접 공유하는 대신 큐를 쓰는 이유: 서로 다른 코어에서 동시에 접근해도
// FreeRTOS가 내부적으로 락을 걸어주므로 레이스 컨디션 없이 안전하게 데이터를 주고받을 수 있음.
QueueHandle_t soundQueue;

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);  // WS2812 4개 제어 객체
Adafruit_PCD8544 display(LCD_DC, LCD_CS, LCD_RST);  // 하드웨어 SPI(FSPI 기본 핀: MOSI11/SCLK12) 사용하는 LCD 객체

// 아래 3개 배열은 모두 "인터럽트(ISR)가 쓰고 loop()가 읽는" 값이라 volatile 필요
// (컴파일러가 최적화 과정에서 값이 안 바뀐다고 착각해 캐싱해버리는 걸 방지)
volatile bool switchPressed[NUM_SWITCHES] = {false};      // 디바운스까지 끝난, 확정된 마지막 스위치 상태(눌림/뗌) - 상태가 실제로 바뀌었는지 판단하는 기준
volatile bool checkPending[NUM_SWITCHES] = {false};       // ISR이 세팅하고 loop()가 처리 후 내리는 플래그 - "이 스위치, 디바운스 시간 지나면 다시 확인해야 함" 표시
volatile uint32_t lastChangeMs[NUM_SWITCHES] = {0};       // 마지막으로 핀 레벨이 바뀐 시각(ms) - 디바운스 타이머의 기준점

bool ledOn[NUM_LEDS] = {false};       // 각 LED가 현재 켜져 있는지 - FLASH_MS 지난 뒤 꺼야 하므로 상태를 기억해둠
uint32_t ledOffAt[NUM_LEDS] = {0};    // 각 LED를 꺼야 할 목표 시각(millis 기준) - delay() 없이 논블로킹으로 자동 소등하기 위한 타이머

bool switch5State = false;    // 5번 스위치(LED 매칭 없는 단순 토글) 현재 상태 - LCD 맨 윗줄에 표시
uint8_t lastPressedNum = 0;   // 마지막으로 눌린 버튼 번호(1~4). 0 = 아직 한 번도 안 눌림 - LCD에 "SW: N" 표시용
char lastWavName[24] = "";    // 마지막으로 재생 요청한 wav 파일 이름(경로/확장자 제거) - LCD 표시용. 84px 좁은 화면에 맞춰 24바이트면 충분

// 버튼 인덱스(0~3)에 대응하는 LED 색을 정해준다.
// 함수로 뺀 이유: bootAnimation()과 실제 버튼 눌림 처리 두 군데에서 같은 색 매핑을 써야 해서 중복 방지
uint32_t switchColor(uint8_t idx) {
  switch (idx) {
    case 0: return strip.Color(255, 0, 0);       // R
    case 1: return strip.Color(0, 255, 0);       // G
    case 2: return strip.Color(0, 0, 255);       // B
    default: return strip.Color(255, 255, 255);  // W
  }
}

// 스위치 핀 레벨이 바뀔 때(CHANGE) 하드웨어 인터럽트로 호출되는 ISR.
// ISR은 최대한 짧고 빠르게 끝내야 하므로, 실제 눌림/뗌 판정과 LED/LCD/사운드 처리는
// 여기서 하지 않고 시각 기록 + 플래그 세팅만 한 뒤 loop()에 위임한다(디바운스도 loop에서 처리).
void IRAM_ATTR handleSwitchInterrupt(void* arg) {
  uint8_t idx = (uint8_t)(uintptr_t)arg;  // attachInterruptArg로 넘긴 스위치 인덱스를 복원
  lastChangeMs[idx] = millis();           // 디바운스 타이머 리셋
  checkPending[idx] = true;               // "나중에 loop()에서 이 스위치 다시 확인해줘" 표시
}

// 전원 켜졌을 때 LED 4개를 한 개씩 순서대로 켰다가 전부 끄는 데모.
// 배선/LED가 정상인지 눈으로 바로 확인할 수 있게 하기 위한 용도.
void bootAnimation() {
  strip.clear();
  strip.show();

  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, switchColor(i));
    strip.show();
    delay(BOOT_STEP_MS);
  }

  strip.clear();
  strip.show();
}

// 현재 상태(switch5State, lastPressedNum, lastWavName)를 LCD에 다시 그린다.
// 상태가 바뀌는 시점마다(스위치 눌림, 5번 토글) 호출해서 화면을 최신 상태로 유지하기 위함.
void redrawLCD() {
  display.clearDisplay();

  display.setCursor(0, 0);
  display.print("SW5: ");
  display.print(switch5State ? "ON" : "OFF");

  display.setCursor(0, 16);
  if (lastPressedNum > 0) {
    display.print("SW: ");
    display.print(lastPressedNum);
  }

  display.setCursor(0, 32);
  display.print(lastWavName);

  display.display();
}

// ---- I2S / WAV 재생 (05test_wavplay.ino와 동일한 로직) ----

// I2S 드라이버를 설치하고 BCLK/LRCLK/DOUT 핀을 연결한다.
// setup()에서 딱 한 번만 호출하면 되고, 파일마다 샘플레이트가 다를 수 있으므로
// 실제 주파수 변경은 i2s_set_clk()로 재생 직전에 다시 맞춘다(아래 playWav 참고).
void setupI2S(uint32_t sampleRate) {
  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),  // ESP32가 마스터로 클럭을 만들어 보내기만 함(수신 없음)
    .sample_rate = sampleRate,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,           // MAX98357A는 16/32bit만 받으므로 8bit wav도 16bit로 변환해서 보냄
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,           // L/R 두 슬롯을 다 채워서 보냄(MAX98357A는 모노라 SD 핀 설정대로 합쳐짐)
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = I2S_DMA_BUF_COUNT,
    .dma_buf_len = I2S_DMA_BUF_LEN,
    .use_apll = false,
    .tx_desc_auto_clear = true                              // 버퍼 언더런 시 자동으로 무음(0)을 채워 잡음 방지
  };

  i2s_pin_config_t pins = {
    .bck_io_num = I2S_BCLK_PIN,
    .ws_io_num = I2S_LRC_PIN,
    .data_out_num = I2S_DOUT_PIN,
    .data_in_num = I2S_PIN_NO_CHANGE  // 마이크 입력은 안 쓰므로 미사용 지정
  };

  i2s_driver_install(I2S_NUM_0, &config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);
}

// RIFF/WAVE 청크를 순서대로 훑으면서 fmt(포맷 정보)와 data(실제 오디오 바이트) 청크를 찾는다.
// 표준 44바이트 헤더라고 가정하지 않고 직접 파싱하는 이유: 파일에 따라 추가 청크(LIST 등)가
// fmt와 data 사이에 끼어 있을 수 있어서, 위치를 고정하지 않고 청크 이름을 보고 찾아야 안전함.
bool parseWavHeader(File &f, WavInfo &info) {
  char chunkId[4];
  uint32_t chunkSize;

  f.seek(0);
  f.read((uint8_t*)chunkId, 4);
  if (memcmp(chunkId, "RIFF", 4) != 0) return false;  // wav 파일이 아니면 실패 처리
  f.seek(f.position() + 4);              // RIFF 전체 크기 필드는 재생에 필요 없어서 건너뜀
  f.read((uint8_t*)chunkId, 4);
  if (memcmp(chunkId, "WAVE", 4) != 0) return false;

  bool haveFmt = false;
  while (f.available() >= 8) {
    f.read((uint8_t*)chunkId, 4);   // 청크 이름(4바이트)
    f.read((uint8_t*)&chunkSize, 4); // 그 청크의 길이(4바이트) - 모르는 청크를 건너뛸 때 이 값으로 건너뜀

    if (memcmp(chunkId, "fmt ", 4) == 0) {
      uint16_t audioFormat;                        // PCM인지 등은 이 테스트에서 별도 검사 안 함(읽기만 하고 버림)
      f.read((uint8_t*)&audioFormat, 2);
      f.read((uint8_t*)&info.numChannels, 2);
      f.read((uint8_t*)&info.sampleRate, 4);
      f.seek(f.position() + 6);          // byteRate(4) + blockAlign(2)는 sampleRate/bitsPerSample로 다시 계산 가능해서 건너뜀
      f.read((uint8_t*)&info.bitsPerSample, 2);

      uint32_t remaining = chunkSize - 16;  // fmt 청크가 표준 16바이트보다 크면(확장 포맷) 남는 부분 건너뜀
      if (remaining > 0) f.seek(f.position() + remaining);
      haveFmt = true;
    } else if (memcmp(chunkId, "data", 4) == 0) {
      info.dataSize = chunkSize;      // 이후 이 바이트 수만큼만 읽어서 재생
      info.dataOffset = f.position(); // 실제 오디오 데이터가 시작하는 파일 위치
      return haveFmt;                 // data를 찾았으면 더 볼 필요 없이 종료
    } else {
      f.seek(f.position() + chunkSize + (chunkSize % 2));  // 관심 없는 청크는 크기만큼 건너뜀(홀수 크기는 1바이트 패딩 포함이라 %2로 보정)
    }
  }
  return false;  // data 청크를 끝까지 못 찾음
}

// path의 wav 파일을 열어 I2S로 끝까지 스트리밍 재생한다.
// 이 함수 자체는 끝날 때까지 블로킹되지만, audioTask 안에서만 호출되므로
// loop()(스위치 처리)에는 전혀 영향을 주지 않는다(파일 상단 core 0 설명 참고).
void playWav(const char* path) {
  File f = LittleFS.open(path, "r");
  if (!f) {
    return;  // 파일 열기 실패(업로드 누락 등) - 조용히 무시
  }

  WavInfo info;
  // 이 테스트는 sounds/ 폴더 파일들(8bit mono)만 다루므로 그 외 포맷은 명시적으로 거부
  if (!parseWavHeader(f, info) || info.bitsPerSample != 8 || info.numChannels != 1) {
    f.close();
    return;
  }

  // 파일마다 샘플레이트가 다를 수 있으므로 재생 직전에 I2S 클럭을 실제 값으로 맞춤
  i2s_set_clk(I2S_NUM_0, info.sampleRate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
  f.seek(info.dataOffset);  // 헤더를 지나 실제 오디오 바이트가 시작하는 위치로 이동

  uint8_t rawBuf[READ_CHUNK_BYTES];        // 파일에서 읽은 원본 8bit 샘플을 담는 임시 버퍼
  int16_t stereoBuf[READ_CHUNK_BYTES * 2]; // I2S로 보낼 16bit 스테레오(L,R 인터리브) 버퍼
  uint32_t remaining = info.dataSize;      // 아직 재생 안 한 남은 바이트 수

  while (remaining > 0) {
    size_t toRead = min((uint32_t)sizeof(rawBuf), remaining);  // 버퍼 크기와 남은 양 중 작은 쪽만큼만 읽음
    size_t bytesRead = f.read(rawBuf, toRead);
    if (bytesRead == 0) break;  // 예상치 못하게 더 못 읽으면 중단(파일 손상 등 방어)

    for (size_t i = 0; i < bytesRead; i++) {
      // wav의 8bit PCM은 unsigned(0~255, 128이 무음)라서, I2S에 16bit signed로 설정해둔 것에 맞춰 변환
      int16_t sample = ((int16_t)rawBuf[i] - 128) << 8;
      stereoBuf[i * 2]     = sample;  // L
      stereoBuf[i * 2 + 1] = sample;  // R (모노 소스라 L/R에 같은 값을 넣어 MAX98357A가 그대로 출력하게 함)
    }

    size_t bytesWritten;
    // portMAX_DELAY: DMA 버퍼에 자리가 날 때까지 기다림(블로킹). audioTask 전용 함수라 문제 없음.
    i2s_write(I2S_NUM_0, stereoBuf, bytesRead * 2 * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
    remaining -= bytesRead;
  }

  f.close();
}

// "/explosion01.wav" -> "explosion01" 처럼 경로/확장자를 뗀 표시용 이름을 lastWavName에 채운다.
// Nokia 5110은 가로 84px(텍스트 크기1 기준 약 14글자)밖에 안 돼서, 그대로 표시하면
// 파일명이 화면 밖으로 잘려나가므로 최대한 짧게 다듬어서 보여주기 위함.
void setLastWavName(const char* path) {
  const char* slash = strrchr(path, '/');       // 마지막 '/' 뒤가 실제 파일명
  const char* name = slash ? slash + 1 : path;

  strncpy(lastWavName, name, sizeof(lastWavName) - 1);  // 고정 크기 char 배열이라 String 대신 strncpy로 오버플로 방지
  lastWavName[sizeof(lastWavName) - 1] = '\0';

  char* dot = strrchr(lastWavName, '.');  // ".wav" 확장자는 어차피 다 같아서 표시할 필요 없음
  if (dot) *dot = '\0';
}

// core 0에 고정되어 도는 전용 태스크(이유는 파일 상단 주석 참고).
// soundQueue에 새 항목이 들어올 때까지 portMAX_DELAY로 대기하다가, 항목이 오면
// 그 인덱스의 wav를 재생하고 다시 큐를 기다리는 무한 루프. playWav()가 오래 블로킹돼도
// 이 태스크만 멈출 뿐 core 1의 loop()는 영향받지 않는 게 이 구조의 핵심.
void audioTask(void* pvParameters) {
  uint8_t idx;
  for (;;) {
    if (xQueueReceive(soundQueue, &idx, portMAX_DELAY) == pdTRUE) {
      playWav(SOUND_FILES[idx]);
    }
  }
}

// loop()가 호출하는 진입점: 랜덤 wav 하나를 고르고, LCD용 이름을 세팅한 뒤,
// 실제 재생은 큐에 맡기고 곧바로 리턴한다(논블로킹). loop()가 절대 멈추면 안 된다는
// 요구사항을 지키기 위해, 이 함수 안에서는 파일 읽기/i2s_write를 절대 하지 않는다.
// 큐가 이미 SOUND_QUEUE_LEN개로 가득 차 있으면 xQueueSend가 실패하고 이번 요청은 버려짐
// (대기시간 0이라 여기서도 블로킹되지 않음).
void requestRandomSound() {
  uint8_t idx = random(NUM_SOUNDS);
  setLastWavName(SOUND_FILES[idx]);
  xQueueSend(soundQueue, &idx, 0);
}

void setup() {
  randomSeed(esp_random());  // ESP32 하드웨어 난수 발생기로 시드를 줘서 매번 다른 랜덤 시퀀스가 나오게 함

  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    pinMode(SWITCH_PINS[i], INPUT_PULLUP);  // 스위치가 눌리면 GND로 연결되는 배선이라 내부 풀업 사용(안 누르면 HIGH)
  }

  strip.begin();
  strip.setBrightness(18);  // 눈부심 방지를 위해 밝기를 낮게 고정
  strip.clear();
  strip.show();

  bootAnimation();  // 인터럽트를 걸기 전에 먼저 실행 - 초기화 도중 스위치 신호로 오작동하는 것을 방지

  display.begin();
  display.setRotation(2);   // 실제 장착 방향이 뒤집혀 있어서 180도 보정
  display.setContrast(20);
  display.setTextSize(1);
  display.setTextColor(BLACK);
  redrawLCD();  // 초기 상태(SW5 OFF, 눌린 버튼 없음)를 화면에 표시

  LittleFS.begin(false);  // false: 마운트 실패해도 자동 포맷하지 않음(파일 업로드 누락 시 조용히 재생만 안 됨)
  setupI2S(11025);  // sounds 폴더 파일들의 공통 샘플레이트로 초기화. 파일별 실제 값은 playWav()의 i2s_set_clk()에서 재설정됨

  soundQueue = xQueueCreate(SOUND_QUEUE_LEN, sizeof(uint8_t));  // core1<->core0 간 재생 요청 전달용 큐 생성
  // 스택 8192바이트: playWav() 안의 지역 버퍼(rawBuf 512B + stereoBuf 2048B)에
  // LittleFS 파일 읽기(SPI flash 접근)와 I2S 드라이버 호출이 겹치면 4096바이트로는
  // 부족해서 스택 오버플로 크래시(→ 자동 리셋 → bootAnimation 재실행)가 났었음
  xTaskCreatePinnedToCore(audioTask, "audioTask", 8192, NULL, 1, NULL, 0);  // core 0에 고정 배치(이유는 파일 상단 주석)

  // 인터럽트는 다른 초기화가 모두 끝난 뒤 마지막에 붙임 - 그래야 초기화 중간에
  // 발생한 핀 변화 때문에 아직 준비 안 된 자료구조(soundQueue 등)를 건드리는 일이 없음
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    attachInterruptArg(SWITCH_PINS[i], handleSwitchInterrupt, (void*)(uintptr_t)i, CHANGE);
  }
}

void loop() {
  uint32_t now = millis();  // 이번 loop 반복에서 공통으로 쓸 기준 시각(호출마다 값이 바뀌는 걸 방지하기 위해 한 번만 읽음)

  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    // checkPending: ISR이 세팅한 "재확인 필요" 표시, 디바운스 시간이 지난 것만 실제로 처리
    if (checkPending[i] && (now - lastChangeMs[i] >= DEBOUNCE_MS)) {
      checkPending[i] = false;  // 이번에 처리하니 플래그 내림

      bool pressed = (digitalRead(SWITCH_PINS[i]) == LOW);  // 풀업 배선이라 눌리면 LOW
      if (pressed != switchPressed[i]) {  // 디바운스 후에도 상태가 실제로 바뀐 경우만 반응(중복 처리 방지)
        switchPressed[i] = pressed;

        if (i < NUM_LEDS) {  // 스위치 0~3: LED + 사운드 매칭
          if (pressed) {
            uint32_t color = switchColor(i);
            strip.setPixelColor(i, color);
            strip.show();
            ledOn[i] = true;
            ledOffAt[i] = now + FLASH_MS;  // FLASH_MS 뒤에 꺼지도록 예약(아래 소등 처리 블록에서 실행)

            lastPressedNum = i + 1;
            requestRandomSound();  // 큐에 재생 요청만 넣고 즉시 리턴(논블로킹) - 실제 재생은 audioTask가 core0에서 처리
            redrawLCD();            // 재생 시작 여부와 무관하게, 요청한 순간 바로 화면 갱신
          }
        } else {  // 스위치 4(5번째, GPIO18): LED/사운드 없이 단순 on/off 토글만
          switch5State = pressed;
          redrawLCD();
        }
      }
    }
  }

  // FLASH_MS가 지난 LED를 순서대로 꺼줌 - delay() 없이 millis() 비교만으로 논블로킹 처리
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    if (ledOn[i] && now >= ledOffAt[i]) {
      strip.setPixelColor(i, strip.Color(0, 0, 0));
      strip.show();
      ledOn[i] = false;
    }
  }
}
