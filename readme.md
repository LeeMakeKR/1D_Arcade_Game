WS2815 line LED 를 사용한 1D arcade 게임기 제작 프로젝트


개요

3m 길이의 WS2815 라인 LED 172칸을 화면 삼아, 위치와 색만으로 게임을 표현하는 1차원 아케이드 게임기다
ESP32-S3 한 장에 아케이드 스위치 4개와 ST7735 LCD, I2S 앰프를 물려 입력·점수판·효과음을 함께 처리한다.
같은 펌웨어를 올린 보드 두 대를 ESP-NOW로 연결하면 2인 대전이 되고
수록 게임은 SNAKE INVADER / SOLO PONG / DUEL PONG 세 가지다.


[입력]

- 버튼1: GPIO7 - WS2815 LED 가 결합된 아케이드 스위치
- 버튼2: GPIO15 - WS2815 LED 가 결합된 아케이드 스위치
- 버튼3: GPIO16 - WS2815 LED 가 결합된 아케이드 스위치
- 버튼4: GPIO17 - WS2815 LED 가 결합된 아케이드 스위치
- 버튼5: GPIO18 - 토글 스위치, 선택에 따라 1p/2p 전환. 

미구현
- 조이스틱 X축 (아날로그, ADC1_CH3, 하드웨어 고정): GPIO4
- 조이스틱 Y축 (아날로그, ADC1_CH4, 하드웨어 고정): GPIO5
- 조이스틱 클릭 스위치 (디지털 입력): GPIO6


[출력]

- WS2815 데이터(DI): GPIO8
- WS2815 백업 데이터(BI): GPIO 연결 불필요 — 스트립 입구에서 GND에 연결(플로팅 금지)
- ST7735 TFT LCD (128x160, 4-wire SPI): RS(=DC) GPIO9, CS GPIO10(FSPI 하드웨어 기본 CS0), SDA(=MOSI) GPIO11(FSPI 하드웨어 기본 핀), CLK(=SCLK) GPIO12(FSPI 하드웨어 기본 핀), RST GPIO14, VCC 3.3V, GND GND
  - 모듈에 백라이트 핀(BLK/LED)이 따로 나와 있는 버전이면 3.3V에 연결한다.
  - 핀이 7개(VCC/GND/CLK/SDA/RS/RST/CS)뿐인 보드는 백라이트가 내부에서 VCC에 물려 있다.
  - VCC는 3.3V 권장. 레귤레이터가 실장된 모듈이라 5V도 받지만, 로직 레벨 시프터가 없는 모듈에 5V를 넣으면 3.3V 신호가 불안정해질 수 있다.
- I2S MAX98357A (앰프): BCLK GPIO42, LRCLK(WS) GPIO41, DOUT GPIO40
- 보드 간 통신: ESP-NOW 무선


핀아웃 다이어그램 (WeAct ESP32-S3 N16R8 DevKitC-1, mischianti 핀아웃 이미지 배치 기준)

```text
                                                     ┌─[ANT]──┐
                                      3V3            ●──┤        ├──●  GND
                                      3V3            ●──┤        ├──●  GPIO43         → (USB-UART TXD0, 사용금지)
                                      RST            ●──┤        ├──●  GPIO44         → (USB-UART RXD0, 사용금지)
           IN: 조이스틱 X (ADC1_CH3)  GPIO4          ●──┤        ├──●  GPIO1          → (예비 핀)
           IN: 조이스틱 Y (ADC1_CH4)  GPIO5          ●──┤        ├──●  GPIO2          → (예비 핀)
            IN: 조이스틱 클릭 스위치  GPIO6          ●──┤        ├──●  GPIO42         → OUT: I2S BCLK
                           IN: 버튼1  GPIO7          ●──┤        ├──●  GPIO41         → OUT: I2S LRCLK(WS)
                           IN: 버튼2  GPIO15         ●──┤        ├──●  GPIO40         → OUT: I2S DOUT
                           IN: 버튼3  GPIO16         ●──┤        ├──●  GPIO39         → (JTAG 예비핀, 미사용)
                           IN: 버튼4  GPIO17         ●──┤        ├──●  GPIO38         → (옥탈 PSRAM 내부용, 사용금지)
                           IN: 버튼5  GPIO18         ●──┤        ├──●  GPIO37         → (옥탈 PSRAM 내부용, 사용금지)
                    OUT: WS2815 DATA  GPIO8          ●──┤        ├──●  GPIO36         → (옥탈 PSRAM 내부용, 사용금지)
             (스트랩핀/JTAG, 미사용)  GPIO3          ●──┤        ├──●  GPIO35         → (옥탈 PSRAM 내부용, 사용금지)
                  (스트랩핀, 미사용)  GPIO46         ●──┤        ├──●  GPIO0          → (스트랩핀/BOOT, 미사용)
                     OUT: LCD RS(DC)  GPIO9          ●──┤        ├──●  GPIO45         → (스트랩핀/VDD_SPI, 미사용)
         OUT: LCD CS (FSPI 기본 CS0)  GPIO10         ●──┤        ├──●  GPIO48         → (온보드 WS2812 RGB LED 전용)
     OUT: LCD SDA/MOSI (FSPI 기본핀)  GPIO11         ●──┤        ├──●  GPIO47         → (예비 핀)
     OUT: LCD CLK/SCLK (FSPI 기본핀)  GPIO12         ●──┤        ├──●  GPIO21         → (예비 핀)
 (예비, FSPI MISO 기본핀·LCD 미사용)  GPIO13         ●──┤        ├──●  GPIO20         → (USB D-, 사용금지)
                        OUT: LCD RST  GPIO14         ●──┤        ├──●  GPIO19         → (USB D+, 사용금지)
                                      5V             ●──┤         ├──●  GND
                                      GND            ●──┤        ├──●  GND
                                                     └[UART]──[USB]┘
```



핀 선택 이유 / 특정 핀 사용 주의사항

1. 이 보드(N16R8)는 16MB 플래시 + 8MB 옥탈(Octal) PSRAM 구성으로, PSRAM용 추가 데이터 라인과 DQS 스트로브가 GPIO33~37에 내부 배선되어 있음. GPIO33/34는 아예 헤더로 나오지 않고, GPIO35/36/37은 헤더에 노출되어 있지만 실제로는 PSRAM 전용이라 GPIO로 재사용하면 PSRAM 접근이 깨져 보드가 멎을 수 있음 → 세 핀 모두 사용 금지. 같은 그룹에 인접한 GPIO38도 안전을 위해 예비로 비워둠.
2. 조이스틱 아날로그 입력(X/Y)은 ADC1 전용 핀(GPIO4/5)에 배치. ESP32-S3도 클래식 ESP32와 동일하게 Wi-Fi 활성 시 ADC2 정확도/사용 제한 이슈가 있어 ADC1이 안전함. 같은 조이스틱 신호인 클릭 스위치(GPIO6)도 바로 옆 핀에 배치.
3. GPIO0/3/45/46은 부팅 모드를 결정하는 스트랩핀(GPIO0: BOOT, GPIO3: JTAG 신호 소스 선택, GPIO45: VDD_SPI 전압 선택, GPIO46: ROM 메시지 출력 제어). 외부 소자가 부팅 순간 이 핀들의 전압을 강제로 바꾸면 오동작할 수 있어 미사용으로 비워둠.
4. GPIO43/44는 보드 내장 USB-UART 브릿지(별도 USB-C "UART" 포트)가 사용하는 UART0(TXD0/RXD0) 고정 핀이라 시리얼 모니터/펌웨어 업로드 용도로 남겨두고 다른 용도로 쓰지 않음. GPIO19/20은 칩 내장 네이티브 USB(D-/D+)로 "USB" 포트에 연결되어 있어 마찬가지로 비워둠.
5. GPIO48은 보드에 실장된 온보드 WS2812 RGB LED 전용 데이터 핀이라 외부 신호 용도로 재사용하지 않음. 외부 WS2815 라인은 별도 핀(GPIO8)을 사용.
6. LCD는 SCLK(GPIO12)/MOSI(GPIO11)/CS(GPIO10)를 FSPI(ESP32-S3 기본 SPI) 하드웨어 기본 핀 그대로 사용하는 것을 최우선으로 함(라이브러리 하드웨어 SPI 그대로 사용 가능하고, CS까지 기본 핀에 맞출 수 있어 이전 클래식 ESP32 설계보다 배선이 더 단순함). DC/RST는 자유 GPIO라 이 블록 양옆의 GPIO9/14에 배치했으며, FSPI MISO 기본핀(GPIO13)은 LCD가 읽기 신호를 쓰지 않아 예비 핀으로 남김. ST7735의 RS/SDA/CLK는 각각 DC/MOSI/SCLK와 같은 신호임. ST7735는 128x160x16bit라 전체 화면 한 장이 40KB이므로, SPI 클럭을 하드웨어 SPI 최대치(27~40MHz)로 올리고 화면 전체를 매번 다시 그리는 대신 바뀐 영역만 갱신하는 편이 좋음.
7. 버튼 5개(GPIO7/15/16/17/18)와 조이스틱(GPIO4/5/6)은 보드 좌측 헤더에서 RST 이후로 물리적으로 연속된 핀에 몰아서 배치해 배선을 단순화. 기능적 제약이 없는 핀이라 인접 배치를 우선 적용.
8. UART2(GPIO1/2)와 I2S(GPIO42/41/40)는 클래식 ESP32와 달리 ESP32-S3에서는 GPIO 매트릭스로 완전히 자유롭게 재배치 가능한 주변장치라 하드웨어로 고정된 핀이 아님. 남은 핀 중 보드 우측 헤더에서 서로 인접한 자리를 골라 배치했을 뿐 반드시 이 핀이어야 하는 것은 아님.
9. WS2815는 타이밍에 민감하므로 다른 기능과 공유하지 않는 GPIO8에 단독 배치 권장.
10. GPIO39(JTAG 예비), GPIO47, GPIO21은 하드웨어 제약이 없는 예비 핀으로 남겨둠.
12. 아두이노에서 컴파일 시 확인할 것.
- ESP32-S3 보드 선택
- FLASH SIZE: 16MB
- Partition Scheme: HUGE APP
- UPLOAD SPEED: 921600 으로 하면 실패하는 경우 있음. 115200으로. 


효과음 제작은 
https://sfxr.me/
에서 했으며 sounds.md 문서에 정리해 두었다.

BOM

WS2815
https://aliexpress.com/item/4001331197520.html
[3M 60 IP67]
- 1미터당 60개의 LED 가 배치되어 있으며 WS2815칩을 사용하여 12v를 그대로 사용할 수 있다. 
WS2812 는 테스트 결과 끝단 전압 강하로 추가로 전선 연결이 필요하여 제작이 번거롭다
3미터 총 180개의 LED를 사용하며, 스위치용으로 8개를 절단해 LED 스트립에는 172개를 사용한다

항공잭
https://aliexpress.com/item/1005008859055554.html
GX12, 5Pin, 5Sets (Male Female)
- 2pin 을 구매해도 됨
- 최초 통신 연결용으로 5핀을 사용했으나, ESPNOW 로 통신하게 되어 필요가 없어졌다. 전원공급 2선만 사용해도 충분하다


MAX98357A 사운드 모듈
https://aliexpress.com/item/1005012452360614.html


기타
xh connectors, 
wires

m3 insert nuts, bolts

speakers
-다이소에서 구매한 스피커를 사용했다


power jack
https://aliexpress.com/item/1005007327459848.html

power switch
https://ko.aliexpress.com/item/1005006151890626.html


arcade switch
구매했던 제품은 링크가 사라져 최대한 비슷한 제품을 링크한다
지름 30MM 에 투명한 제품이면 거의 사용 가능
https://ko.aliexpress.com/item/1005008769355244.html


tft lcd
https://aliexpress.com/item/1005012439751456.html
-ST7735 128x160 SPI 모듈 사용
이것도 링크가 사라져 비슷한 제품을 링크한 것인데 이런 류의 LCD 모듈은 제품마다 약간씩 화면 표시가 달라지는 경우가 있다. 
RED TAB, GREEN TAB, BLUE TAB 등으로 구분되며, RED TAB 이 가장 일반적이다.
화면이 이상하게 표시되면 TAB 색상을 확인하고, 라이브러리에서 해당 TAB에 맞는 초기화 코드를 사용해야 한다.



