/*
  Arduino Duemilanove + Nokia 5110 (PCD8544) LCD 테스트 코드

  필요 라이브러리 (Library Manager에서 검색/설치):
    - Adafruit PCD8544 Nokia 5110 LCD library
    - Adafruit GFX Library

  배선 (Duemilanove 하드웨어 SPI 기준)
    5110 핀        Duemilanove 핀      비고
    ----------------------------------------------------------------
    RST       ->   D8                 임의 디지털 핀, 코드와 맞으면 변경 가능
    CE (CS)   ->   D7                 임의 디지털 핀, 코드와 맞으면 변경 가능
    DC        ->   D6                 임의 디지털 핀, 코드와 맞으면 변경 가능
    DIN(MOSI) ->   D11                하드웨어 SPI 고정 핀
    CLK(SCK)  ->   D13                하드웨어 SPI 고정 핀
    VCC       ->   3.3V               5V 연결 시 모듈 손상 가능 (레귤레이터 내장 브레이크아웃이면 5V 가능,
                                       모듈 실크에 표기 확인)
    BL(백라이트) -> 3.3V 또는 220~330Ω 저항 통해 5V
    GND       ->   GND

  주의: 순정 PCD8544는 로직 레벨이 3.3V. Duemilanove(ATmega328)는 5V 로직이므로,
        레벨시프터/레귤레이터가 없는 베어 모듈이라면 SCLK/DIN/DC/CE/RST 각 라인에
        5V->3.3V 레벨시프팅(저항 분압 등)이 필요. 대부분의 "브레이크아웃 보드" 형태
        (핀 8개, 뒷면에 레귤레이터 IC 있는 것)는 5V 직결 가능.
*/

#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>

// 하드웨어 SPI 사용: SCLK=13, DIN/MOSI=11 은 고정, DC/CE/RST 핀만 지정
Adafruit_PCD8544 display = Adafruit_PCD8544(/*DC=*/6, /*CE=*/7, /*RST=*/8);

unsigned long counter = 0;

void setup() {
  Serial.begin(9600);

  display.begin();
  display.setContrast(35); // 화면이 안 보이거나 너무 진하면 40~60 범위에서 조정

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(BLACK);
  display.setCursor(0, 0);
  display.println("Nokia 5110");
  display.println("Test OK!");
  display.display();
  delay(2000);
}

void loop() {
  display.clearDisplay();

  display.setCursor(0, 0);
  display.println("Duemilanove");
  display.print("Count: ");
  display.println(counter++);

  // 간단한 그래픽 테스트 (사각형 + 대각선)
  display.drawRect(0, 20, 84, 28, BLACK);
  display.drawLine(0, 20, 84, 48, BLACK);


  display.display();

  display.drawRect(0, 20, 84, 28, WHITE);
  display.drawLine(0, 20, 84, 48, WHITE);

  display.display();
}
