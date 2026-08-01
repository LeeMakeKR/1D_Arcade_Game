#pragma once
#include <Arduino.h>
#include "sounds/SoundClip.h"   // SoundClip 구조체 정의만 들어 있다(PCM 배열은 없음)

// 플래시에 상주하는 PCM 배열을 그대로 읽어 합산(믹싱)해서 I2S로 내보내는 사운드 엔진.
// 사운드 데이터는 프로젝트 루트의 sounds/wav2header.py가 sounds/wav/의 wav로부터 생성해
// 이 스케치의 sounds/ 폴더로 복사해 준 헤더들에 들어 있다.
//
// 이 엔진은 "어떤 소리가 있는지"를 모른다:
//   쓸 소리는 스케치가 골라서 클립 포인터 배열로 넘겨준다(soundEngineBegin의 clips 인자).
//   그래서 게임마다 필요한 소리만 include하면 되고, 쓰지 않는 소리는 플래시를 차지하지 않는다.
//   PCM 배열 정의가 헤더 안에 들어 있어서(Arduino IDE는 스케치 폴더 안의 .cpp만 컴파일하므로
//   데이터를 .cpp로 분리할 수 없다) include하는 파일마다 배열 복사본이 생기는데, 이렇게 두면
//   사운드 헤더를 .ino 한 곳에서만 include하게 되어 중복이 생기지 않는다.
//
//   예) .ino에서
//        #include "sounds/laser.h"
//        static const SoundClip* const MY_SOUNDS[] = { &CLIP_laser, ... };
//        soundEngineBegin(BCLK, LRC, DOUT, MY_SOUNDS, 4);
//        soundPlay(0);   // -> laser 재생
//
// 런타임 비용이 왜 최소인가:
//   - 합성 연산 0 / 파일 I/O 0 / 부팅 처리 0. 재생할 파형이 이미 완성된 PCM으로 들어 있다.
//   - 샘플당 하는 일이 "플래시 로드 1회 + 시프트 1회 + 덧셈 1회"뿐이라, LED 제어나
//     ESP-NOW 통신과 동시에 돌려도 오디오가 CPU를 거의 잡아먹지 않는다.
//
// 재생 방식:
//   기본은 멀티트랙이다. 이미 소리가 나는 중에 다른 소리를 요청해도 앞의 소리가
//   끝나기를 기다리지 않고 즉시 겹쳐서(최대 SOUND_MAX_VOICES개 동시) 재생된다.
//   클립에 alwaysSolo가 켜져 있는 소리(예: fanfare)는 재생되는 순간 다른 트랙을 모두 끊고
//   혼자 재생되며, 끝날 때까지 다른 소리가 섞이지 않는다.

#define SOUND_MAX_VOICES 4  // 동시 멀티트랙 재생 개수

// 출력 볼륨. 8bit PCM을 16bit로 올릴 때의 시프트 양이라 한 단계 낮출 때마다 진폭이 절반이 된다.
//   8 = 원본 그대로 / 7 = 50% / 6 = 25%
// 곱셈이 아닌 시프트라 샘플당 비용은 그대로다.
#define SOUND_GAIN_SHIFT 7

// I2S를 설정하고 오디오 태스크(core 0)를 띄운다. setup()에서 1회 호출.
// clips는 이 스케치가 쓸 소리들의 포인터 배열이며, 그 배열은 프로그램이 끝날 때까지
// 살아 있어야 한다(전역 static으로 두면 된다). 이후 soundPlay()의 인자는 이 배열의 인덱스다.
void soundEngineBegin(int bclkPin, int lrcPin, int doutPin,
                      const SoundClip* const* clips, uint8_t clipCount);

// 재생 요청. 큐에 넣고 즉시 리턴하므로 호출한 쪽(loop)은 절대 블로킹되지 않는다.
void soundPlay(uint8_t idx);

uint8_t soundActiveVoices();   // 현재 동시에 재생 중인 트랙 수
