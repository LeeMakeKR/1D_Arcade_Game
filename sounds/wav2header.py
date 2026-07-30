"""
sounds/wav/ 폴더의 wav 파일들을 C 헤더로 변환해 sounds/header/ 에 넣는다.

생성되는 파일 (sounds/header/):
  <wav파일명>.h  - 소리 하나당 하나. 그 소리의 PCM 배열과 클립 정보만 들어 있다.
                   필요한 소리만 골라 include할 수 있고, include하지 않은 소리는
                   플래시를 전혀 차지하지 않는다(링커가 미사용 데이터를 떼어낸다).
                   예) blip01.wav -> blip01.h  안에 pcm_blip01[] 과 CLIP_blip01
  SoundClip.h    - 위 파일들이 공통으로 쓰는 SoundClip 구조체 정의
  SoundData.h    - 전체를 한꺼번에 쓸 때를 위한 묶음. 개별 헤더를 모두 include하고
                   SoundId enum과 SOUND_CLIPS[] 목록을 만들어 둔다.

생성한 헤더는 아래 COPY_TO에 적힌 위치(스케치 폴더 안의 sounds 하위 폴더)로도 자동 복사된다.
Arduino IDE는 "스케치 폴더 안"의 파일만 컴파일 대상으로 삼기 때문에, 실제로 쓰는
스케치에는 헤더가 그 안에 있어야 한다. sounds/header/ 쪽은 원본 확인용으로 남는다.
사운드를 쓰는 스케치를 새로 만들면 COPY_TO 목록에 그 경로를 추가하면 된다.

  스케치에서는 두 방식 중 하나를 고르면 된다(둘 다 자기 폴더의 복사본을 가리킨다):
    - 전부 다 쓸 때(테스트/랜덤 재생):  #include "sounds/SoundData.h"
    - 몇 개만 쓸 때(실제 게임):        #include "sounds/laser.h" 처럼 필요한 것만 골라서
      골라 쓰면 include하지 않은 소리는 플래시를 전혀 차지하지 않는다
      (실측: laser 하나만 쓰면 +3.4KB, 전체 묶음을 쓰면 +187KB)

왜 헤더로 굽는가:
  wav를 LittleFS에 올려두고 재생할 때마다 읽는 방식은 런타임에 파일시스템 I/O가 걸리고,
  data 폴더 준비 + 커스텀 파티션 테이블 + "Upload LittleFS" 절차까지 필요했다.
  C 배열로 만들어 두면 그 PCM이 그대로 실행 이미지(.rodata)에 들어가 플래시에 상주하므로,
  런타임에는 메모리 배열을 읽는 것뿐이고 스케치 Upload 한 번으로 끝난다.
  PSRAM이나 부팅 시 렌더링도 필요 없다.

변환 규격 (모든 소리를 하나로 통일한다):
  - 22050Hz, mono, 8bit signed(int8_t)
  - 샘플레이트를 통일하는 이유: 재생 중 I2S 클럭을 다시 맞출 일이 없어지고,
    그 덕에 멀티트랙 믹싱이 단순한 덧셈만으로 성립한다.
  - 8bit signed로 두는 이유: 원본 wav 대부분이 이미 8bit라 품질 손실이 없고,
    unsigned(0~255)가 아니라 signed로 미리 옮겨두면 믹서가 뺄셈 없이
    시프트 한 번으로 16bit로 올릴 수 있다(연산 최소화).

왜 .h 하나에 배열까지 담는가:
  Arduino IDE는 "스케치 폴더 안"의 .cpp만 컴파일한다. 그래서 데이터를 .cpp로 분리해
  sounds/header/에 두면 컴파일 대상에서 빠져 링크 에러가 난다. 배열 정의까지 헤더에
  담아두면 스케치가 #include 한 줄로 끌어다 쓸 수 있어서, 사운드 데이터를 스케치마다
  복사하지 않고 이 폴더에서 한 벌만 관리할 수 있다.
  단, include하는 파일마다 배열 복사본이 생기므로 스케치에서는 반드시 한 곳
  (SoundEngine.cpp)에서만 include해야 한다. 생성된 헤더 상단에도 같은 경고를 적어둔다.

사용법:
  이 폴더(sounds/)에서 실행한다. 경로는 스크립트 위치를 기준으로 자동 계산된다.

    py wav2header.py                # sounds/wav/ 를 변환해 sounds/header/ 에 헤더 생성
    py wav2header.py --list         # 변환 결과만 미리 보고 파일은 쓰지 않음
    py wav2header.py --rate 11025   # 목표 샘플레이트를 바꿔서 생성(용량 절반)
    py wav2header.py --out ../어디/다른폴더   # 출력 위치를 바꿔서 생성

  소리를 추가하거나 교체한 뒤에는 이 스크립트를 다시 실행하고 스케치를 재컴파일하면 된다.
  (생성된 .h는 모두 자동 생성 파일이므로 직접 손대지 말 것)

  받아들이는 wav: mono, 8bit 또는 16bit, 샘플레이트는 목표값(22050Hz)의 정수배.
  조건에 맞지 않는 파일은 이유를 출력하고 건너뛴다.

  ※ 단독 재생으로 고정할 소리는 아래 ALWAYS_SOLO에 파일 이름을 적어둔다.
     그 소리는 재생될 때 다른 소리와 절대 섞이지 않는다(승리 음 등).
"""

import argparse
import os
import shutil
import sys
import wave

TARGET_RATE_DEFAULT = 22050

# 항상 단독으로 재생할 소리(파일 이름에서 확장자를 뗀 것).
# 여기에 적힌 소리는 재생 시 다른 트랙을 모두 끊고 혼자 재생되며,
# 끝날 때까지 다른 소리가 섞이지 않는다.
ALWAYS_SOLO = {"fanfare"}

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
WAV_DIR = os.path.join(SCRIPT_DIR, "wav")
OUT_DIR_DEFAULT = os.path.join(SCRIPT_DIR, "header")

# 생성한 헤더를 그대로 복사해 둘 위치 목록(스케치 폴더 안의 sounds 하위 폴더).
# Arduino IDE는 "스케치 폴더 안"의 파일만 컴파일 대상으로 삼기 때문에, 실제로 쓰는
# 스케치에는 헤더가 그 폴더 안에 있어야 한다. 헤더가 11개라 스케치 루트에 두면 지저분해서
# sounds 하위 폴더로 모아두고, 스케치는 #include "sounds/SoundData.h" 처럼 참조한다.
# (헤더만 있고 .cpp가 없으므로 하위 폴더에 둬도 문제없다 - 컴파일 대상이 아니라 include 대상이다)
# 여기 적힌 폴더의 부모(스케치 폴더)가 있으면 sounds 폴더는 없으면 자동으로 만든다.
# 사운드를 쓰는 스케치를 새로 만들면 이 목록에 그 경로를 추가하면 된다.
COPY_TO = [
    os.path.join("..", "testcode", "05test_wavheader", "sounds"),
]


def read_wav_as_int8(path, target_rate):
    """wav 하나를 읽어 목표 레이트 / mono / 8bit signed 샘플 리스트로 돌려준다."""
    with wave.open(path, "rb") as w:
        channels = w.getnchannels()
        width = w.getsampwidth()
        rate = w.getframerate()
        raw = w.readframes(w.getnframes())

    if channels != 1:
        raise ValueError("모노 파일만 지원한다 (%d채널)" % channels)
    if width not in (1, 2):
        raise ValueError("8bit 또는 16bit만 지원한다 (%dbit)" % (width * 8))
    if rate % target_rate != 0:
        raise ValueError("샘플레이트 %dHz가 목표 %dHz의 정수배가 아니다" % (rate, target_rate))

    # 먼저 signed 값으로 펼친다: 8bit wav는 unsigned(128이 무음), 16bit wav는 signed.
    if width == 1:
        samples = [b - 128 for b in raw]
    else:
        samples = [
            int.from_bytes(raw[i:i + 2], "little", signed=True) >> 8
            for i in range(0, len(raw), 2)
        ]

    # 목표 레이트로 정수배 다운샘플. 단순히 건너뛰는 대신 구간 평균을 취해
    # 고주파가 접혀 들어오는(에일리어싱) 잡음을 줄인다.
    step = rate // target_rate
    if step > 1:
        samples = [
            sum(samples[i:i + step]) // step
            for i in range(0, len(samples) - step + 1, step)
        ]

    return [max(-128, min(127, s)) for s in samples]


def c_identifier(name):
    return "".join(c if c.isalnum() else "_" for c in name)


GENERATED_NOTE = [
    "// 이 파일은 sounds/wav2header.py가 sounds/wav/ 의 wav로부터 자동 생성한 것이다.",
    "// 직접 수정하지 말고, 소리를 바꾼 뒤 스크립트를 다시 실행할 것.",
]


def write_file(lines, out_dir, filename):
    path = os.path.join(out_dir, filename)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    return path


def write_clip_struct(target_rate, out_dir):
    """SoundClip.h - 개별 사운드 헤더들이 공통으로 쓰는 구조체 정의"""
    lines = GENERATED_NOTE + [
        "//",
        "// 재생 포맷: %dHz / mono / 8bit signed" % target_rate,
        "",
        "#pragma once",
        "#include <Arduino.h>",
        "",
        "// 소리 하나를 가리키는 정보. pcm은 플래시(.rodata)에 상주하는 배열을 가리킨다.",
        "struct SoundClip {",
        "  const int8_t* pcm;   // 8bit signed PCM (무음 = 0)",
        "  uint32_t len;        // 샘플 수",
        "  bool alwaysSolo;     // true면 다른 소리와 섞이지 않고 항상 단독으로 재생된다",
        "  const char* name;    // LCD 표시용 이름",
        "};",
        "",
        "#define SOUND_SAMPLE_RATE  %d" % target_rate,
        "",
    ]
    return write_file(lines, out_dir, "SoundClip.h")


def write_one_sound(name, samples, target_rate, out_dir):
    """<wav파일명>.h - 소리 하나의 PCM 배열과 클립 정보"""
    ident = c_identifier(name)
    solo = "true" if name in ALWAYS_SOLO else "false"
    seconds = len(samples) / float(target_rate)

    lines = GENERATED_NOTE + [
        "//",
        "// %s.wav -> %dHz / mono / 8bit signed / %d samples (%.2f초, %.1f KB)"
        % (name, target_rate, len(samples), seconds, len(samples) / 1024.0),
        "//",
        "// 이 헤더는 PCM 배열 정의를 직접 담고 있다(static const). Arduino IDE는 스케치 폴더",
        "// 안의 .cpp만 컴파일하므로 데이터를 .cpp로 분리하면 이 위치에서는 링크되지 않기 때문이다.",
        "// 그래서 include하는 파일마다 배열 복사본이 생긴다 - 한 곳에서만 include할 것.",
        "//",
        "// 쓰는 법: 이 소리만 필요하면 이 파일을 include하고 CLIP_%s 를 쓰면 된다." % ident,
        '//         #include "sounds/%s.h"' % name,
        "//         전부 다 쓸 때는 대신 sounds/SoundData.h를 include한다.",
        "",
        "#pragma once",
        '#include "SoundClip.h"',
        "",
        "static const int8_t pcm_%s[%d] = {" % (ident, len(samples)),
    ]
    for i in range(0, len(samples), 24):
        lines.append("  " + ",".join("%d" % s for s in samples[i:i + 24]) + ",")
    lines += [
        "};",
        "",
        'static const SoundClip CLIP_%s = { pcm_%s, %d, %s, "%s" };'
        % (ident, ident, len(samples), solo, name),
        "",
    ]
    return write_file(lines, out_dir, name + ".h")


def write_index(clips, out_dir):
    """SoundData.h - 개별 헤더를 모두 모아 SoundId enum과 SOUND_CLIPS 목록을 만든다"""
    lines = GENERATED_NOTE + [
        "//",
        "// 사운드 전체를 한꺼번에 쓸 때를 위한 묶음 헤더다(테스트 스케치의 랜덤 재생 등).",
        "// 여기에 들어온 소리는 실제로 쓰지 않아도 SOUND_CLIPS[]가 참조하므로 전부 플래시에",
        "// 올라간다. 게임 코드처럼 몇 개만 필요하면 이 파일 대신 개별 헤더를 골라 include할 것.",
        '//   예) #include "sounds/laser.h"  -> CLIP_laser 만 사용 (나머지는 플래시를 차지하지 않는다)',
        "",
        "#pragma once",
        '#include "SoundClip.h"',
        "",
    ]
    for name, _ in clips:
        lines.append('#include "%s.h"' % name)
    lines += [
        "",
        "enum SoundId : uint8_t {",
    ]
    for name, _ in clips:
        lines.append("  SND_%s," % c_identifier(name).upper())
    lines += [
        "  SND_COUNT",
        "};",
        "",
        "static const SoundClip SOUND_CLIPS[SND_COUNT] = {",
    ]
    for name, _ in clips:
        lines.append("  CLIP_%s," % c_identifier(name))
    lines += ["};", ""]
    return write_file(lines, out_dir, "SoundData.h")


def main():
    ap = argparse.ArgumentParser(
        description="sounds/wav/ 의 wav를 C 헤더(SoundData.h)로 변환한다")
    ap.add_argument("--rate", type=int, default=TARGET_RATE_DEFAULT,
                    help="목표 샘플레이트 (기본 %d)" % TARGET_RATE_DEFAULT)
    ap.add_argument("--out", default=OUT_DIR_DEFAULT,
                    help="출력 폴더 (기본 sounds/header)")
    ap.add_argument("--list", action="store_true",
                    help="변환 결과만 출력하고 파일은 쓰지 않는다")
    args = ap.parse_args()

    if not os.path.isdir(WAV_DIR):
        sys.exit("wav 폴더가 없다: %s" % WAV_DIR)

    names = sorted(f[:-4] for f in os.listdir(WAV_DIR) if f.lower().endswith(".wav"))
    if not names:
        sys.exit("wav 파일이 없다: %s" % WAV_DIR)

    clips = []
    total = 0
    print("%-16s %9s %7s" % ("sound", "samples", "solo"))
    print("-" * 35)
    for name in names:
        try:
            samples = read_wav_as_int8(os.path.join(WAV_DIR, name + ".wav"), args.rate)
        except ValueError as e:
            print("%-16s  건너뜀: %s" % (name, e))
            continue
        clips.append((name, samples))
        total += len(samples)
        print("%-16s %9d %7s" % (name, len(samples), "O" if name in ALWAYS_SOLO else ""))

    print("-" * 35)
    print("합계 %d개 / %d bytes (%.1f KB)" % (len(clips), total, total / 1024.0))

    if args.list:
        print("\n--list 모드이므로 파일을 쓰지 않았다.")
        return
    if not clips:
        sys.exit("변환할 수 있는 wav가 없어 파일을 쓰지 않았다.")

    out_dir = args.out if os.path.isabs(args.out) else os.path.join(SCRIPT_DIR, args.out)
    out_dir = os.path.normpath(out_dir)
    if not os.path.isdir(out_dir):
        os.makedirs(out_dir)

    written = [write_clip_struct(args.rate, out_dir)]
    for name, samples in clips:
        written.append(write_one_sound(name, samples, args.rate, out_dir))
    written.append(write_index(clips, out_dir))

    print("\n생성 위치: %s" % out_dir)
    print("  SoundClip.h (공통 구조체) / SoundData.h (전체 묶음)")
    print("  개별 헤더 %d개: %s" % (len(clips), ", ".join(n + ".h" for n, _ in clips)))

    # 스케치 폴더로 복사. Arduino IDE가 스케치 폴더 안의 파일만 보기 때문에 필요하다.
    for rel in COPY_TO:
        dest = os.path.normpath(os.path.join(SCRIPT_DIR, rel))
        # 부모(스케치 폴더)가 없으면 경로 오타일 가능성이 크므로 만들지 않고 알린다.
        if not os.path.isdir(os.path.dirname(dest)):
            print("\n복사 건너뜀(스케치 폴더가 없다): %s" % dest)
            continue
        os.makedirs(dest, exist_ok=True)
        for src in written:
            shutil.copyfile(src, os.path.join(dest, os.path.basename(src)))
        print("\n복사 완료: %s (%d개)" % (dest, len(written)))


if __name__ == "__main__":
    main()
