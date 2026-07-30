// 이 파일은 sounds/wav2header.py가 sounds/wav/ 의 wav로부터 자동 생성한 것이다.
// 직접 수정하지 말고, 소리를 바꾼 뒤 스크립트를 다시 실행할 것.
//
// 사운드 전체를 한꺼번에 쓸 때를 위한 묶음 헤더다(테스트 스케치의 랜덤 재생 등).
// 여기에 들어온 소리는 실제로 쓰지 않아도 SOUND_CLIPS[]가 참조하므로 전부 플래시에
// 올라간다. 게임 코드처럼 몇 개만 필요하면 이 파일 대신 개별 헤더를 골라 include할 것.
//   예) #include "sounds/laser.h"  -> CLIP_laser 만 사용 (나머지는 플래시를 차지하지 않는다)

#pragma once
#include "SoundClip.h"

#include "blip01.h"
#include "blip02.h"
#include "explosion01.h"
#include "explosion02.h"
#include "fanfare.h"
#include "laser.h"
#include "pickupCoin01.h"
#include "pickupCoin02.h"
#include "powerUp.h"

enum SoundId : uint8_t {
  SND_BLIP01,
  SND_BLIP02,
  SND_EXPLOSION01,
  SND_EXPLOSION02,
  SND_FANFARE,
  SND_LASER,
  SND_PICKUPCOIN01,
  SND_PICKUPCOIN02,
  SND_POWERUP,
  SND_COUNT
};

static const SoundClip SOUND_CLIPS[SND_COUNT] = {
  CLIP_blip01,
  CLIP_blip02,
  CLIP_explosion01,
  CLIP_explosion02,
  CLIP_fanfare,
  CLIP_laser,
  CLIP_pickupCoin01,
  CLIP_pickupCoin02,
  CLIP_powerUp,
};
