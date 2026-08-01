/*
  ===========================================================================
  01 - ST7735 TFT LCD (128x160) 표시 테스트
  ===========================================================================
  ST7735 컬러 LCD의 배선/설정 확인용.
  색 확인 → 화면 정보 표시 → 글자 흐르기 순으로 진행한다.

  ---------------------------------------------------------------------------
  배선 (readme.md 기준)
  ---------------------------------------------------------------------------
    ST7735   ESP32-S3
    ------   ------------------------------
    VCC      3.3V
    GND      GND
    CLK      GPIO12  (FSPI 하드웨어 기본 SCK)
    SDA      GPIO11  (FSPI 하드웨어 기본 MOSI)
    RS       GPIO9
    RST      GPIO14
    CS       GPIO10  (FSPI 하드웨어 기본 CS0)

  CLK/SDA/CS가 FSPI 하드웨어 기본 핀이라 SPI 객체를 그대로 쓴다(비트뱅잉 아님).

  ---------------------------------------------------------------------------
  실측으로 정한 값
  ---------------------------------------------------------------------------
  - SPI_HZ = 24MHz
      40MHz로 올리면 화면 아래쪽 일부가 그려지지 않는다. 전체 화면을 채우는 긴 전송의
      뒷부분이 깨지는 증상이라, 듀폰선 길이에서 오는 신호 품질 문제다. 배선을 짧게
      정리하기 전까지는 24MHz로 둘 것.
  - TAB_TYPE = INITR_BLACKTAB
      ST7735는 모듈마다 색 순서(RGB/BGR)와 패널 원점 오프셋이 달라 초기화 코드를 골라야
      하는데, 이 모듈은 BLACKTAB에서 색·위치가 모두 정상이다. 색이 반대로 나오거나 화면이
      1~3px 밀려 테두리가 잘리면 INITR_GREENTAB을 써 볼 것.
      (INITR_REDTAB은 이 라이브러리에서 INITR_144GREENTAB과 상수값이 0x01로 같아서,
       넣으면 1.44" 128x128 설정으로 잡힌다. 1.8"에는 쓰지 말 것)

  ---------------------------------------------------------------------------
  라이브러리
  ---------------------------------------------------------------------------
  라이브러리 매니저에서 "Adafruit ST7735 and ST7789 Library" 설치
  (의존성으로 Adafruit GFX Library, Adafruit BusIO가 같이 깔린다)

  보드 설정: ESP32S3 Dev Module
  ===========================================================================
*/

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>

#define PIN_RS   9    // = DC (Data/Command)
#define PIN_CS   10
#define PIN_RST  14
#define PIN_SDA  11   // = MOSI. 하드웨어 SPI가 알아서 쓰지만 SPI.begin에 명시한다
#define PIN_CLK  12   // = SCLK

#define TAB_TYPE     INITR_BLACKTAB  // 모듈별 초기화 코드(위 주석 참고)
#define TFT_ROTATION 3               // 0,2 = 세로(128x160) / 1,3 = 가로(160x128)
#define SPI_HZ       24000000        // 40MHz는 화면 아래쪽이 깨진다(위 주석 참고)

// 하드웨어 SPI 생성자 인자 순서: (CS, DC, RST)
Adafruit_ST7735 tft = Adafruit_ST7735(PIN_CS, PIN_RS, PIN_RST);

const char MESSAGE[] = "Hello World";

#define TEXT_SIZE     3                  // 글자 크기(기본 폰트 6x8px의 배수)
#define TEXT_H        (8 * TEXT_SIZE)    // 흐르는 글자 띠의 높이
#define SCROLL_STEP   2                  // 한 프레임에 움직이는 픽셀
#define FRAME_DELAY   16                 // 약 60fps

// 글자 띠만 따로 그려서 통째로 전송한다.
// TFT는 프레임버퍼를 들고 있지 않아서, 화면에 직접 지우고 다시 그리면 깜빡인다.
// 캔버스(메모리 상의 작은 프레임버퍼)에 완성한 뒤 한 번에 밀어넣으면 깜빡임이 없다.
GFXcanvas16* band = nullptr;
int16_t textX;
int16_t textWidth;

// 화면 전체를 원색으로 채워 색 순서와 전체 주소지정을 확인한다.
// 글자 이름과 배경색이 다르면 TAB_TYPE이 틀린 것(RGB/BGR 반대).
void colorTest() {
  const uint16_t colors[] = {ST77XX_RED, ST77XX_GREEN, ST77XX_BLUE, ST77XX_WHITE};
  const char* names[]     = {"RED", "GREEN", "BLUE", "WHITE"};

  tft.setTextSize(2);
  tft.setTextColor(ST77XX_BLACK);
  for (uint8_t i = 0; i < 4; i++) {
    tft.fillScreen(colors[i]);
    tft.setCursor(6, 6);
    tft.print(names[i]);
    delay(400);
  }
}

// 테두리 + 해상도/핀맵. 네 변이 다 보이면 화면 크기와 회전 설정이 맞는 것이다.
void infoScreen() {
  tft.fillScreen(ST77XX_BLACK);
  tft.drawRect(0, 0, tft.width(), tft.height(), ST77XX_WHITE);

  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(10, 12);
  tft.print("ST7735");

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(10, 36);
  tft.print(tft.width());
  tft.print("x");
  tft.print(tft.height());
  tft.print("  rot");
  tft.print(TFT_ROTATION);
  tft.setCursor(10, 46);
  tft.print(SPI_HZ / 1000000);
  tft.print("MHz");

  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(10, 64);  tft.print("RS(DC) 9");
  tft.setCursor(10, 74);  tft.print("CS    10");
  tft.setCursor(10, 84);  tft.print("SDA   11");
  tft.setCursor(10, 94);  tft.print("CLK   12");
  tft.setCursor(10, 104); tft.print("RST   14");

  delay(2500);
}

void setup() {
  // 하드웨어 SPI. 핀을 명시해 두면 다른 보드로 옮겼을 때 기본 핀이 달라도 그대로 동작한다.
  SPI.begin(PIN_CLK, -1, PIN_SDA, PIN_CS);   // MISO는 LCD가 쓰지 않아 -1

  tft.initR(TAB_TYPE);
  tft.setSPISpeed(SPI_HZ);
  tft.setRotation(TFT_ROTATION);

  colorTest();
  infoScreen();

  tft.fillScreen(ST77XX_BLACK);

  // 글자 폭을 재서 화면 왼쪽으로 완전히 빠져나가는 시점을 계산한다
  int16_t x1, y1;
  uint16_t w, h;
  tft.setTextSize(TEXT_SIZE);
  tft.getTextBounds(MESSAGE, 0, 0, &x1, &y1, &w, &h);
  textWidth = (int16_t)w;

  band = new GFXcanvas16(tft.width(), TEXT_H);
  band->setTextSize(TEXT_SIZE);
  band->setTextWrap(false);   // 오른쪽 끝에서 줄바꿈되면 흐르는 글자가 깨진다

  textX = tft.width();
}

void loop() {
  // 캔버스에 한 프레임을 완성한 뒤 통째로 전송(깜빡임 없음)
  band->fillScreen(ST77XX_BLACK);
  band->setCursor(textX, 0);
  band->setTextColor(ST77XX_WHITE);
  band->print(MESSAGE);

  tft.drawRGBBitmap(0, (tft.height() - TEXT_H) / 2, band->getBuffer(), band->width(), band->height());

  textX -= SCROLL_STEP;
  if (textX < -textWidth) textX = tft.width();

  delay(FRAME_DELAY);
}
