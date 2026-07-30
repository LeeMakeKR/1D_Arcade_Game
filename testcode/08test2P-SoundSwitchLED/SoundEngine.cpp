#include "SoundEngine.h"
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <string.h>

// 이 파일은 사운드 데이터 헤더를 include하지 않는다.
// 쓸 소리는 스케치가 soundEngineBegin()으로 넘겨준 포인터 배열로만 접근한다(이유는 SoundEngine.h 참고).

#define MIX_FRAMES     128  // 믹싱 한 틱에서 처리하는 프레임 수(22050Hz에서 약 5.8ms - 버튼 반응 지연을 작게 유지)
#define REQ_QUEUE_LEN    8  // 짧은 시간에 여러 번 눌러도 밀어둘 수 있는 재생 요청 개수

// 동시 재생 슬롯 하나. 어느 PCM 배열을 몇 번째 샘플까지 재생했는지만 들고 있으면 된다.
struct Voice {
  const int8_t* pcm;   // nullptr = 빈 슬롯
  uint32_t len;
  uint32_t pos;
  bool solo;           // 이 슬롯이 단독 재생 중인지(끝날 때 잠금을 풀어야 하므로 기억해둔다)
};

static Voice voices[SOUND_MAX_VOICES];
static QueueHandle_t reqQueue;
static i2s_port_t i2sPort = I2S_NUM_0;
static volatile uint8_t activeVoices = 0;
static bool soloLocked = false;  // audioTask 안에서만 읽고 쓰므로 동기화 불필요

static const SoundClip* const* clipTable = nullptr;  // 스케치가 넘겨준 클립 목록
static uint8_t clipTableCount = 0;

// 활성 슬롯 수를 다시 센다.
// 이 값은 audioTask가 "믹싱을 계속 돌릴지 큐에서 잠들지"를 판단하는 기준이라,
// 슬롯을 건드린 직후에는 반드시 갱신해야 한다.
// (갱신을 빠뜨리면 슬롯에 소리를 넣어놓고도 태스크가 그대로 잠들어 아무 소리가 안 난다)
static void refreshActiveCount() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < SOUND_MAX_VOICES; i++) {
    if (voices[i].pcm != nullptr) n++;
  }
  activeVoices = n;
}

// 재생 요청을 빈 슬롯에 배정한다. audioTask(core 0)에서만 호출된다.
static void startVoice(uint8_t idx) {
  if (clipTable == nullptr || idx >= clipTableCount) return;

  const SoundClip* clip = clipTable[idx];
  if (clip == nullptr || clip->pcm == nullptr || clip->len == 0) return;

  if (clip->alwaysSolo) {
    // 단독 재생 지정 소리: 섞여 있던 트랙을 모두 끊고 0번 슬롯 하나만 사용
    for (uint8_t i = 0; i < SOUND_MAX_VOICES; i++) voices[i].pcm = nullptr;
    voices[0] = { clip->pcm, clip->len, 0, true };
    soloLocked = true;
    refreshActiveCount();
    return;
  }

  if (soloLocked) return;  // 단독 재생이 끝나기 전에는 다른 소리를 섞지 않는다

  int8_t slot = -1;
  for (uint8_t i = 0; i < SOUND_MAX_VOICES; i++) {
    if (voices[i].pcm == nullptr) { slot = i; break; }
  }
  if (slot < 0) {
    // 빈 슬롯이 없으면 가장 끝에 가까운(곧 끝날) 트랙을 희생시켜 새 소리를 받아준다.
    // 버튼을 연타했을 때 새 입력이 무시되는 것보다 이 편이 반응이 자연스럽다.
    uint32_t leastRemaining = UINT32_MAX;
    for (uint8_t i = 0; i < SOUND_MAX_VOICES; i++) {
      uint32_t remaining = voices[i].len - voices[i].pos;
      if (remaining < leastRemaining) { leastRemaining = remaining; slot = i; }
    }
  }
  voices[slot] = { clip->pcm, clip->len, 0, false };
  refreshActiveCount();
}

// 활성 슬롯들의 PCM을 한 청크만큼 더해서 I2S로 내보낸다.
// 이 함수가 런타임 오디오 비용의 전부이며, 샘플당 하는 일은
// "플래시 로드 1회 + 시프트 1회 + 덧셈 1회"뿐이다.
static void mixTick() {
  static int32_t mix[MIX_FRAMES];        // 합산용(여러 트랙을 더하면 16bit를 넘길 수 있어 32bit로 받는다)
  static int16_t out[MIX_FRAMES * 2];    // I2S로 보낼 L/R 인터리브 버퍼

  memset(mix, 0, sizeof(mix));

  for (uint8_t i = 0; i < SOUND_MAX_VOICES; i++) {
    Voice &v = voices[i];
    if (v.pcm == nullptr) continue;

    uint32_t n = v.len - v.pos;
    if (n > MIX_FRAMES) n = MIX_FRAMES;

    const int8_t* src = v.pcm + v.pos;
    // PCM을 8bit signed로 구워뒀기 때문에 부호 보정 없이 시프트만으로 16bit로 올라간다
    for (uint32_t j = 0; j < n; j++) mix[j] += (int32_t)src[j] << 8;

    v.pos += n;
    if (v.pos >= v.len) {
      v.pcm = nullptr;
      if (v.solo) soloLocked = false;  // 단독 재생이 끝났으니 다시 멀티트랙 허용
    }
  }
  refreshActiveCount();  // 이번 틱에 끝난 슬롯을 반영(0이 되면 다음 반복에서 태스크가 잠든다)

  for (uint32_t j = 0; j < MIX_FRAMES; j++) {
    int32_t s = mix[j];
    if (s > 32767) s = 32767;         // 여러 트랙을 더하면 범위를 넘을 수 있어 clamp(클리핑)
    else if (s < -32768) s = -32768;
    out[j * 2]     = (int16_t)s;      // L
    out[j * 2 + 1] = (int16_t)s;      // R (MAX98357A는 모노라 SD 핀 설정대로 합쳐짐)
  }

  size_t written;
  // portMAX_DELAY로 DMA 버퍼에 자리가 날 때까지 기다리는 것이 곧 재생 속도를 맞추는 페이싱 역할을 한다.
  i2s_write(i2sPort, out, sizeof(out), &written, portMAX_DELAY);
}

// core 0에 고정해 돌리는 오디오 태스크.
// 재생 중인 트랙이 없으면 큐에서 요청이 올 때까지 무한 대기하므로 유휴 시 CPU를 쓰지 않고,
// 재생 중이면 매 틱마다 큐를 즉시 확인(대기 0)해서 새 요청을 곧바로 섞어 넣는다.
//
// core 0을 쓰는 이유: Arduino의 setup()/loop()는 core 1에 고정된 태스크에서 돌기 때문에,
// 오디오를 core 0에 두면 스위치/LED/통신 처리와 물리적으로 다른 코어에서 진짜 동시에 돈다.
static void audioTask(void*) {
  uint8_t idx;
  for (;;) {
    TickType_t wait = (activeVoices == 0) ? portMAX_DELAY : 0;
    while (xQueueReceive(reqQueue, &idx, wait) == pdTRUE) {
      startVoice(idx);
      wait = 0;
    }
    if (activeVoices > 0) mixTick();
  }
}

void soundEngineBegin(int bclkPin, int lrcPin, int doutPin,
                      const SoundClip* const* clips, uint8_t clipCount) {
  clipTable = clips;
  clipTableCount = clipCount;

  // 모든 소리가 같은 샘플레이트로 구워져 있어서 재생 중 i2s_set_clk()로 클럭을 바꿀 일이 없다.
  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SOUND_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,  // MAX98357A는 16/32bit만 받으므로 8bit PCM을 16bit로 올려 보낸다
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 6,    // LED show()나 ESP-NOW 처리가 잠깐 CPU를 잡아도 언더런되지 않을 정도의 여유
    .dma_buf_len = 128,
    .use_apll = false,
    .tx_desc_auto_clear = true  // 언더런 시 자동으로 무음을 채워 잡음 방지
  };
  i2s_pin_config_t pins = {
    .bck_io_num = bclkPin,
    .ws_io_num = lrcPin,
    .data_out_num = doutPin,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  i2s_driver_install(i2sPort, &config, 0, NULL);
  i2s_set_pin(i2sPort, &pins);

  reqQueue = xQueueCreate(REQ_QUEUE_LEN, sizeof(uint8_t));
  // 파일 I/O도 없고 믹싱 버퍼도 static이라 스택은 4096으로 충분하다
  xTaskCreatePinnedToCore(audioTask, "audioTask", 4096, NULL, 1, NULL, 0);
}

void soundPlay(uint8_t idx) {
  xQueueSend(reqQueue, &idx, 0);  // 큐가 가득 차면 버린다(호출한 쪽을 절대 블로킹하지 않는 것이 우선)
}

uint8_t soundActiveVoices() {
  return activeVoices;
}
