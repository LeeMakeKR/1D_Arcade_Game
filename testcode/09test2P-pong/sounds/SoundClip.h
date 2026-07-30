// 이 파일은 sounds/wav2header.py가 sounds/wav/ 의 wav로부터 자동 생성한 것이다.
// 직접 수정하지 말고, 소리를 바꾼 뒤 스크립트를 다시 실행할 것.
//
// 재생 포맷: 22050Hz / mono / 8bit signed

#pragma once
#include <Arduino.h>

// 소리 하나를 가리키는 정보. pcm은 플래시(.rodata)에 상주하는 배열을 가리킨다.
struct SoundClip {
  const int8_t* pcm;   // 8bit signed PCM (무음 = 0)
  uint32_t len;        // 샘플 수
  bool alwaysSolo;     // true면 다른 소리와 섞이지 않고 항상 단독으로 재생된다
  const char* name;    // LCD 표시용 이름
};

#define SOUND_SAMPLE_RATE  22050
