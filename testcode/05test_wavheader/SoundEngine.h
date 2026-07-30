#pragma once
#include <Arduino.h>

// 플래시에 상주하는 PCM 배열을 그대로 읽어 합산(믹싱)해서 I2S로 내보내는 사운드 엔진.
// 사운드 데이터(SoundData.h와 개별 헤더들)는 프로젝트 루트의 sounds/wav2header.py가
// sounds/wav/의 wav로부터 생성해 sounds/header/에 두고, 이 스케치의 sounds/ 폴더로
// 복사해 준 것이다. 루트 쪽은 원본 확인용이고, 컴파일에 쓰이는 것은 이 스케치의 복사본이다.
//

// 재생 방식:
//   기본은 멀티트랙이다. 이미 소리가 나는 중에 다른 소리를 요청해도 앞의 소리가
//   끝나기를 기다리지 않고 즉시 겹쳐서(최대 SOUND_MAX_VOICES개 동시) 재생된다.
//
//   예외적으로 SoundData.h의 클립에 alwaysSolo가 켜져 있는 소리(현재 fanfare)는
//   재생되는 순간 다른 트랙을 모두 끊고 혼자 재생되며, 끝날 때까지 다른 소리가
//   섞이지 않는다. 승리 음이 효과음에 침범당하지 않게 하려는 것이고, 어떤 소리를
//   그렇게 다룰지는 sounds/wav2header.py의 ALWAYS_SOLO 목록에서 정한다.
//   호출부는 이를 신경 쓸 필요 없이 soundPlay()만 부르면 된다.

#define SOUND_MAX_VOICES 4  // 동시 멀티트랙 재생 개수

// I2S를 설정하고 오디오 태스크(core 0)를 띄운다. setup()에서 1회 호출.
// 샘플레이트는 SoundData.h에 기록된 값을 쓰므로 따로 넘길 필요가 없다.
void soundEngineBegin(int bclkPin, int lrcPin, int doutPin);

// 재생 요청. 큐에 넣고 즉시 리턴하므로 호출한 쪽(loop)은 절대 블로킹되지 않는다.
void soundPlay(uint8_t id);

uint8_t soundCount();                // 사운드 개수 - random(soundCount())로 무작위 선택할 때 사용
const char* soundName(uint8_t id);   // LCD 표시용 이름
uint8_t soundActiveVoices();         // 현재 동시에 재생 중인 트랙 수(멀티트랙 동작 확인용)
uint32_t soundTotalBytes();          // 플래시에 들어 있는 PCM 총 바이트(LCD 표시용)
