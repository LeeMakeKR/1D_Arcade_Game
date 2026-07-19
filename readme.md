ws2812 line led 를 사용한 1d arcade 게임기 제작 프로젝트


하드웨어
RGB LED (ws2812 line led)
RGB 스위치 (3개 혹은 4개)
스피커 - 앰프 등 아두이노에서 소리 재생가능한 모듈
 - 멀티트랙 재생이 가능해야 함

하드웨어 구성
2개의 시스템이 항공잭으로 연결됨 
(길이에 따른 전압 강하 체크할것)
i2s MAX98357A 모듈(앰프 포함) - 멀티트랙 재생 가능


[입력]

- 조이스틱 X축 (아날로그, ADC1_CH3, 하드웨어 고정): GPIO4
- 조이스틱 Y축 (아날로그, ADC1_CH4, 하드웨어 고정): GPIO5
- 조이스틱 클릭 스위치 (디지털 입력): GPIO6
- 버튼1: GPIO7 - WS2812 LED 가 결합된 아케이드 스위치
- 버튼2: GPIO15 - WS2812 LED 가 결합된 아케이드 스위치
- 버튼3: GPIO16 - WS2812 LED 가 결합된 아케이드 스위치
- 버튼4: GPIO17 - WS2812 LED 가 결합된 아케이드 스위치
- 버튼5: GPIO18 - 토글 스위치, 선택에 따라 1p/2p 전환. 



[출력]

- WS2812 데이터: GPIO8
- Nokia 5110 LCD: DC GPIO9, CS GPIO10(FSPI 하드웨어 기본 CS0), DIN(MOSI) GPIO11(FSPI 하드웨어 기본 핀), SCLK GPIO12(FSPI 하드웨어 기본 핀), RST GPIO14
- UART2 (보드 간 통신): TX GPIO1, RX GPIO2
- I2S MAX98357A (앰프): BCLK GPIO42, LRCLK(WS) GPIO41, DOUT GPIO40


핀아웃 다이어그램 (WeAct ESP32-S3 N16R8 DevKitC-1, mischianti 핀아웃 이미지 배치 기준)

```text
                                                     ┌─[ANT]──┐
                                      3V3            ●──┤        ├──●  GND
                                      3V3            ●──┤        ├──●  GPIO43         → (USB-UART TXD0, 사용금지)
                                      RST            ●──┤        ├──●  GPIO44         → (USB-UART RXD0, 사용금지)
           IN: 조이스틱 X (ADC1_CH3)  GPIO4          ●──┤        ├──●  GPIO1          → OUT: UART2 TX
           IN: 조이스틱 Y (ADC1_CH4)  GPIO5          ●──┤        ├──●  GPIO2          → OUT: UART2 RX
            IN: 조이스틱 클릭 스위치  GPIO6          ●──┤        ├──●  GPIO42         → OUT: I2S BCLK
                           IN: 버튼1  GPIO7          ●──┤        ├──●  GPIO41         → OUT: I2S LRCLK(WS)
                           IN: 버튼2  GPIO15         ●──┤        ├──●  GPIO40         → OUT: I2S DOUT
                           IN: 버튼3  GPIO16         ●──┤        ├──●  GPIO39         → (JTAG 예비핀, 미사용)
                           IN: 버튼4  GPIO17         ●──┤        ├──●  GPIO38         → (옥탈 PSRAM 내부용, 사용금지)
                           IN: 버튼5  GPIO18         ●──┤        ├──●  GPIO37         → (옥탈 PSRAM 내부용, 사용금지)
                    OUT: WS2812 DATA  GPIO8          ●──┤        ├──●  GPIO36         → (옥탈 PSRAM 내부용, 사용금지)
             (스트랩핀/JTAG, 미사용)  GPIO3          ●──┤        ├──●  GPIO35         → (옥탈 PSRAM 내부용, 사용금지)
                  (스트랩핀, 미사용)  GPIO46         ●──┤        ├──●  GPIO0          → (스트랩핀/BOOT, 미사용)
                         OUT: LCD DC  GPIO9          ●──┤        ├──●  GPIO45         → (스트랩핀/VDD_SPI, 미사용)
         OUT: LCD CS (FSPI 기본 CS0)  GPIO10         ●──┤        ├──●  GPIO48         → (온보드 WS2812 RGB LED 전용)
     OUT: LCD DIN/MOSI (FSPI 기본핀)  GPIO11         ●──┤        ├──●  GPIO47         → (예비 핀)
         OUT: LCD SCLK (FSPI 기본핀)  GPIO12         ●──┤        ├──●  GPIO21         → (예비 핀)
 (예비, FSPI MISO 기본핀·LCD 미사용)  GPIO13         ●──┤        ├──●  GPIO20         → (USB D-, 사용금지)
                        OUT: LCD RST  GPIO14         ●──┤        ├──●  GPIO19         → (USB D+, 사용금지)
                                      5V             ●──┤        ├──●  GND
                                      GND            ●──┤        ├──●  GND
                                                     └[UART]──[USB]┘
```



핀 선택 이유 / 특정 핀 사용 주의사항

1. 이 보드(N16R8)는 16MB 플래시 + 8MB 옥탈(Octal) PSRAM 구성으로, PSRAM용 추가 데이터 라인과 DQS 스트로브가 GPIO33~37에 내부 배선되어 있음. GPIO33/34는 아예 헤더로 나오지 않고, GPIO35/36/37은 헤더에 노출되어 있지만 실제로는 PSRAM 전용이라 GPIO로 재사용하면 PSRAM 접근이 깨져 보드가 멎을 수 있음 → 세 핀 모두 사용 금지. 같은 그룹에 인접한 GPIO38도 안전을 위해 예비로 비워둠.
2. 조이스틱 아날로그 입력(X/Y)은 ADC1 전용 핀(GPIO4/5)에 배치. ESP32-S3도 클래식 ESP32와 동일하게 Wi-Fi 활성 시 ADC2 정확도/사용 제한 이슈가 있어 ADC1이 안전함. 같은 조이스틱 신호인 클릭 스위치(GPIO6)도 바로 옆 핀에 배치.
3. GPIO0/3/45/46은 부팅 모드를 결정하는 스트랩핀(GPIO0: BOOT, GPIO3: JTAG 신호 소스 선택, GPIO45: VDD_SPI 전압 선택, GPIO46: ROM 메시지 출력 제어). 외부 소자가 부팅 순간 이 핀들의 전압을 강제로 바꾸면 오동작할 수 있어 미사용으로 비워둠.
4. GPIO43/44는 보드 내장 USB-UART 브릿지(별도 USB-C "UART" 포트)가 사용하는 UART0(TXD0/RXD0) 고정 핀이라 시리얼 모니터/펌웨어 업로드 용도로 남겨두고 다른 용도로 쓰지 않음. GPIO19/20은 칩 내장 네이티브 USB(D-/D+)로 "USB" 포트에 연결되어 있어 마찬가지로 비워둠.
5. GPIO48은 보드에 실장된 온보드 WS2812 RGB LED 전용 데이터 핀이라 외부 신호 용도로 재사용하지 않음. 외부 WS2812 라인은 별도 핀(GPIO8)을 사용.
6. LCD는 SCLK(GPIO12)/MOSI(GPIO11)/CS(GPIO10)를 FSPI(ESP32-S3 기본 SPI) 하드웨어 기본 핀 그대로 사용하는 것을 최우선으로 함(라이브러리 하드웨어 SPI 그대로 사용 가능하고, CS까지 기본 핀에 맞출 수 있어 이전 클래식 ESP32 설계보다 배선이 더 단순함). DC/RST는 자유 GPIO라 이 블록 양옆의 GPIO9/14에 배치했으며, FSPI MISO 기본핀(GPIO13)은 LCD가 읽기 신호를 쓰지 않아 예비 핀으로 남김.
7. 버튼 5개(GPIO7/15/16/17/18)와 조이스틱(GPIO4/5/6)은 보드 좌측 헤더에서 RST 이후로 물리적으로 연속된 핀에 몰아서 배치해 배선을 단순화. 기능적 제약이 없는 핀이라 인접 배치를 우선 적용.
8. UART2(GPIO1/2)와 I2S(GPIO42/41/40)는 클래식 ESP32와 달리 ESP32-S3에서는 GPIO 매트릭스로 완전히 자유롭게 재배치 가능한 주변장치라 하드웨어로 고정된 핀이 아님. 남은 핀 중 보드 우측 헤더에서 서로 인접한 자리를 골라 배치했을 뿐 반드시 이 핀이어야 하는 것은 아님.
9. WS2812는 타이밍에 민감하므로 다른 기능과 공유하지 않는 GPIO8에 단독 배치 권장.
10. GPIO39(JTAG 예비), GPIO47, GPIO21은 하드웨어 제약이 없는 예비 핀으로 남겨둠.
11. 두 보드 UART 연결 시 TX-RX 교차 연결, GND 공통, 전압 레벨(둘 다 3.3V인지) 확인 필수.





효과음 제작은 
https://sfxr.me/



사운드
지렁이의 전진 소리
총알 발사 소리
피해를 입었을 때 소리
GAME OVER 소리
GAME CLEAR 소리
GAME START 소리
