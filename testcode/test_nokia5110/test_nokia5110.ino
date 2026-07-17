#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>

#define PIN_DC   9
#define PIN_CS   10
#define PIN_RST  14

// 하드웨어 SPI(FSPI 기본 핀: SCLK GPIO12, MOSI GPIO11) 사용
Adafruit_PCD8544 display(PIN_DC, PIN_CS, PIN_RST);

const char MESSAGE[] = "Hello World";
int16_t textX;
uint16_t textWidth;

void setup() {
  display.begin();
  display.setRotation(2);  // 화면이 뒤집혀 보이면 180도 회전으로 보정
  display.setContrast(19);
  display.clearDisplay();
  display.display();

  display.setTextSize(1);
  display.setTextColor(BLACK);

  int16_t x1, y1;
  uint16_t textHeight;
  display.getTextBounds(MESSAGE, 0, 0, &x1, &y1, &textWidth, &textHeight);

  textX = display.width();
}

void loop() {
  display.clearDisplay();
  display.setCursor(textX, 20);
  display.print(MESSAGE);
  display.display();

  textX--;
  if (textX < -(int16_t)textWidth) {
    textX = display.width();
  }

  delay(50);
}
