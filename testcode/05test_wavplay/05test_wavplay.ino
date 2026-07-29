/*
  ===========================================================================
  MAX98357A WAV 재생 테스트
  ===========================================================================
  sounds/ 폴더의 wav 파일들(8bit PCM mono, 11025Hz)을 ESP32-S3 내부 플래시의
  LittleFS 파일시스템에 올려두고, 시리얼 모니터에 번호를 입력하면 해당 파일을
  I2S로 재생한다. 핀 배정은 readme.md 기준: BCLK GPIO42, LRCLK GPIO41, DOUT GPIO40.

  ---------------------------------------------------------------------------
  wav 파일을 ESP32로 업로드하는 방법 (Arduino IDE 2.x 기준)
  ---------------------------------------------------------------------------
  스케치 코드는 시리얼로 업로드되지만, wav 같은 데이터 파일은 별도의 "파일시스템
  업로드" 과정을 거쳐야 보드 플래시 안의 LittleFS 파티션에 들어간다. 절차:

  1) 업로드 플러그인 설치 (최초 1회만)
     - https://github.com/earlephilhower/arduino-littlefs-upload 의 Releases
       페이지에서 최신 .vsix 파일을 다운로드한다.
     - 아래 폴더에 그 파일을 그대로 복사한다 (폴더가 없으면 직접 생성):
         Windows: C:\Users\<사용자명>\.arduinoIDE\plugins\
     - Arduino IDE 2.x를 재시작한다.

  2) data 폴더 준비
     - 이 .ino 파일이 있는 스케치 폴더(05test_wavplay) 바로 밑에 "data"라는
       폴더를 만들고, 그 안에 재생할 wav 파일들을 넣는다.
       (이 저장소에는 이미 testcode/05test_wavplay/data/ 에 sounds/ 폴더의
        wav 7개가 복사되어 있으니 별도 작업 없이 바로 업로드하면 됨)
     - 폴더 구조 예:
         testcode/05test_wavplay/05test_wavplay.ino
         testcode/05test_wavplay/data/blast.wav
         testcode/05test_wavplay/data/coin1.wav  ... 등

  3) 보드 설정 확인 (Tools 메뉴) - 커스텀 파티션 테이블 사용
     - 이 스케치 폴더에 이미 "partitions.csv"(파일명이 반드시 이 이름이어야
       Custom 스킴이 인식함. 스케치 이름과 맞출 필요는 없음)라는 파티션
       테이블 파일을 만들어 두었다. spiffs 파티션 하나만 깔끔하게
       정의되어 있어서, 이름이 겹치는 파티션이 있는 기본 제공 스킴들에서
       발생하는 문제를 피할 수 있다.
     - Board: 사용 중인 ESP32-S3 보드 (예: "ESP32S3 Dev Module")
     - Flash Size: 16MB (WeAct N16R8 기준)
     - Partition Scheme: "Custom" 선택 → 위 05test_wavplay.csv가 자동 적용됨
     - Partition Scheme을 바꾼 뒤에는 먼저 평소처럼 스케치를 한 번 Upload해야
       새 파티션 테이블이 보드에 실제로 기록된다(파티션 테이블은 스케치
       업로드 시에만 같이 구워짐, LittleFS 업로드만으로는 안 바뀜).

  4) 파일시스템 업로드 실행
     - 먼저 이 스케치를 평소처럼 한 번 보드에 업로드(Upload)해서 보드와
       포트 연결을 확인한다.
     - Ctrl+Shift+P (명령 팔레트) → "Upload LittleFS to Pico/ESP8266/ESP32"
       선택. 이 작업은 스케치 코드와는 별개로 data 폴더 내용만 통째로
       플래시의 LittleFS 파티션에 굽는다(시리얼 업로드보다 오래 걸릴 수 있음).
     - 완료되면 보드가 자동 리셋된다.

  5) 동작 확인
     - 시리얼 모니터(115200bps)를 열면 목록이 뜬다. 1~7 숫자를 입력하고
       엔터를 누르면 해당 wav가 재생된다.
     - "LittleFS 마운트 실패" 메시지가 뜨면 4번의 파일시스템 업로드를
       빼먹은 것이니 그 단계를 다시 수행한다.

  * 참고: data 폴더 내용을 바꾼 뒤에는 반드시 파일시스템 업로드를 다시
    실행해야 보드에 반영된다. 스케치 코드만 Upload해서는 파일이 갱신되지 않음.
  ===========================================================================
*/

#include <LittleFS.h>
#include <driver/i2s.h>

#define I2S_BCLK_PIN  42
#define I2S_LRC_PIN   41
#define I2S_DOUT_PIN  40

#define I2S_DMA_BUF_COUNT  4
#define I2S_DMA_BUF_LEN    256
#define READ_CHUNK_BYTES   512  // 파일에서 한 번에 읽어오는 바이트 수(8bit 샘플 기준)

const char* SOUND_FILES[] = {
  "/blast.wav",
  "/coin1.wav",
  "/dead.wav",
  "/explosion01.wav",
  "/powerUp.wav",
  "/shot1.wav",
  "/shot2.wav"
};
const uint8_t NUM_SOUNDS = sizeof(SOUND_FILES) / sizeof(SOUND_FILES[0]);

struct WavInfo {
  uint16_t numChannels;
  uint32_t sampleRate;
  uint16_t bitsPerSample;
  uint32_t dataSize;
  uint32_t dataOffset;
};

void setupI2S(uint32_t sampleRate) {
  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = sampleRate,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = I2S_DMA_BUF_COUNT,
    .dma_buf_len = I2S_DMA_BUF_LEN,
    .use_apll = false,
    .tx_desc_auto_clear = true
  };

  i2s_pin_config_t pins = {
    .bck_io_num = I2S_BCLK_PIN,
    .ws_io_num = I2S_LRC_PIN,
    .data_out_num = I2S_DOUT_PIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM_0, &config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);
}

// RIFF/WAVE 청크를 순회하며 fmt 정보와 data 청크 위치를 찾는다.
// (표준 44바이트 헤더가 아니어도 동작하도록 청크 단위로 탐색)
bool parseWavHeader(File &f, WavInfo &info) {
  char chunkId[4];
  uint32_t chunkSize;

  f.seek(0);
  f.read((uint8_t*)chunkId, 4);
  if (memcmp(chunkId, "RIFF", 4) != 0) return false;
  f.seek(f.position() + 4);              // RIFF 전체 크기 필드는 사용 안 함
  f.read((uint8_t*)chunkId, 4);
  if (memcmp(chunkId, "WAVE", 4) != 0) return false;

  bool haveFmt = false;
  while (f.available() >= 8) {
    f.read((uint8_t*)chunkId, 4);
    f.read((uint8_t*)&chunkSize, 4);

    if (memcmp(chunkId, "fmt ", 4) == 0) {
      uint16_t audioFormat;
      f.read((uint8_t*)&audioFormat, 2);
      f.read((uint8_t*)&info.numChannels, 2);
      f.read((uint8_t*)&info.sampleRate, 4);
      f.seek(f.position() + 6);          // byteRate(4) + blockAlign(2) 건너뜀
      f.read((uint8_t*)&info.bitsPerSample, 2);

      uint32_t remaining = chunkSize - 16;  // fmt 청크가 16바이트보다 크면 나머지 skip
      if (remaining > 0) f.seek(f.position() + remaining);
      haveFmt = true;
    } else if (memcmp(chunkId, "data", 4) == 0) {
      info.dataSize = chunkSize;
      info.dataOffset = f.position();
      return haveFmt;
    } else {
      f.seek(f.position() + chunkSize + (chunkSize % 2));  // 알 수 없는 청크 skip(홀수 크기 패딩 포함)
    }
  }
  return false;
}

void playWav(const char* path) {
  File f = LittleFS.open(path, "r");
  if (!f) {
    Serial.printf("파일 열기 실패: %s\n", path);
    return;
  }

  WavInfo info;
  if (!parseWavHeader(f, info)) {
    Serial.printf("WAV 헤더 파싱 실패: %s\n", path);
    f.close();
    return;
  }

  if (info.bitsPerSample != 8 || info.numChannels != 1) {
    Serial.printf("지원하지 않는 포맷(%ubit, %uch) - 이 테스트는 8bit mono 전용\n",
                  info.bitsPerSample, info.numChannels);
    f.close();
    return;
  }

  Serial.printf("재생: %s (%luHz, %ubit, %uch, %lu bytes)\n",
                path, info.sampleRate, info.bitsPerSample, info.numChannels, info.dataSize);

  i2s_set_clk(I2S_NUM_0, info.sampleRate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);

  f.seek(info.dataOffset);

  uint8_t rawBuf[READ_CHUNK_BYTES];
  int16_t stereoBuf[READ_CHUNK_BYTES * 2];
  uint32_t remaining = info.dataSize;

  while (remaining > 0) {
    size_t toRead = min((uint32_t)sizeof(rawBuf), remaining);
    size_t bytesRead = f.read(rawBuf, toRead);
    if (bytesRead == 0) break;

    for (size_t i = 0; i < bytesRead; i++) {
      int16_t sample = ((int16_t)rawBuf[i] - 128) << 8;  // 8bit unsigned(0~255) -> 16bit signed 변환
      stereoBuf[i * 2]     = sample;  // L
      stereoBuf[i * 2 + 1] = sample;  // R (MAX98357A는 모노라 SD 핀 설정대로 합쳐짐)
    }

    size_t bytesWritten;
    i2s_write(I2S_NUM_0, stereoBuf, bytesRead * 2 * sizeof(int16_t), &bytesWritten, portMAX_DELAY);

    remaining -= bytesRead;
  }

  f.close();
}

void printMenu() {
  Serial.println();
  Serial.println("=== WAV 재생 테스트 ===");
  for (uint8_t i = 0; i < NUM_SOUNDS; i++) {
    Serial.printf("  %u: %s\n", i + 1, SOUND_FILES[i]);
  }
  Serial.println("시리얼 모니터에 번호(1~7)를 입력하고 엔터를 누르세요.");
}

void setup() {
  Serial.begin(115200);
  delay(500);

  if (!LittleFS.begin(false)) {  // false: 마운트 실패해도 자동 포맷하지 않음(업로드 누락을 바로 알아채기 위함)
    Serial.println("LittleFS 마운트 실패! 상단 주석의 '업로드 방법'대로 wav 파일을 먼저 업로드하세요.");
    return;
  }

  setupI2S(11025);  // 초기값. 실제 재생 시 파일의 샘플레이트로 i2s_set_clk()에서 재설정됨
  printMenu();
}

void loop() {
  if (Serial.available()) {
    int idx = Serial.parseInt() - 1;
    while (Serial.available()) Serial.read();  // 남은 개행 등 버퍼 비우기

    if (idx >= 0 && idx < NUM_SOUNDS) {
      playWav(SOUND_FILES[idx]);
    } else {
      printMenu();
    }
  }
}
