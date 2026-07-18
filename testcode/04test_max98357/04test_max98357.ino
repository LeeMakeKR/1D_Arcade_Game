#include <math.h>
#include <driver/i2s.h>

#define I2S_BCLK_PIN  42
#define I2S_LRC_PIN   41
#define I2S_DOUT_PIN  40

#define SAMPLE_RATE    16000
#define TONE_FREQ_HZ   440
#define AMPLITUDE      10000  // 16bit 풀스케일(32767) 대비 작게 잡아 스피커/귀 보호

const int SAMPLES_PER_CYCLE = SAMPLE_RATE / TONE_FREQ_HZ;
const int WRITES_PER_SEC = SAMPLE_RATE / SAMPLES_PER_CYCLE;

int16_t toneBuffer[SAMPLES_PER_CYCLE * 2];      // L,R 인터리브 (SD 핀 설정에 따라 모노로 합쳐짐)
int16_t silenceBuffer[SAMPLES_PER_CYCLE * 2] = {0};

void setupI2S() {
  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 256,
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

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < SAMPLES_PER_CYCLE; i++) {
    int16_t s = (int16_t)(sinf(2.0f * PI * i / SAMPLES_PER_CYCLE) * AMPLITUDE);
    toneBuffer[i * 2]     = s;  // L
    toneBuffer[i * 2 + 1] = s;  // R
  }

  setupI2S();
  Serial.println("MAX98357A I2S 테스트 시작 (440Hz 1초 재생 / 1초 무음 반복)");
}

void loop() {
  size_t bytesWritten;

  for (int i = 0; i < WRITES_PER_SEC; i++) {
    i2s_write(I2S_NUM_0, toneBuffer, sizeof(toneBuffer), &bytesWritten, portMAX_DELAY);
  }

  for (int i = 0; i < WRITES_PER_SEC; i++) {
    i2s_write(I2S_NUM_0, silenceBuffer, sizeof(silenceBuffer), &bytesWritten, portMAX_DELAY);
  }
}
