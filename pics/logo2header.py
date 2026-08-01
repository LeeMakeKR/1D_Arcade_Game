#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Logo1DArcade.h 생성기 (RLE 압축판)

기존의 통짜 RGB565 배열(픽셀당 2바이트)을 3종 토큰 RLE로 압축한다.
로고는 배경이 검정(0x0000)으로 크게 비어 있어서, 그 구간을 개수만으로 적으면
용량이 크게 준다. 실측(148x120): 35,520 -> 11,972 바이트 (33.7%).

  입력  : 압축 전 Logo1DArcade.h (LOGO_1D_ARCADE_RGB565 배열이 들어 있는 것)
  출력  : 압축된 Logo1DArcade.h

  사용법:
    python logo2header.py <입력.h> <출력.h>

  다른 이미지를 넣고 싶으면 먼저 아무 도구로든 RGB565 배열 헤더를 만든 뒤
  이 스크립트를 통과시키면 된다.

---------------------------------------------------------------------------
스트림 형식
---------------------------------------------------------------------------
토큰 1바이트 = [상위 2비트 종류][하위 6비트 개수 n(1~63)]

    00 nnnnnn   리터럴  : 뒤에 uint16(LE) n개가 이어진다
    01 nnnnnn   투명    : 0x0000 픽셀 n개 (데이터 없음, 건너뛴다)
    10 nnnnnn   반복    : 뒤의 uint16(LE) 1개를 n번

  0x00은 "리터럴 0개"라 나올 수 없으므로 스트림 끝 표시로 쓴다.

투명 토큰은 그리지 않고 건너뛰기만 한다. 캔버스를 검정으로 지운 뒤 그리면 결과가
같고, 덤으로 배경이 비치는 합성이 공짜로 된다.
"""

import re
import sys
import os

LITERAL, SKIP, REPEAT = 0, 1, 2
MAX_RUN = 63


def parse_header(path):
    src = open(path, encoding='utf-8').read()
    w = int(re.search(r'LOGO_1D_ARCADE_W\s*=\s*(\d+)', src).group(1))
    h = int(re.search(r'LOGO_1D_ARCADE_H\s*=\s*(\d+)', src).group(1))
    body = src.split('RGB565[] = {', 1)[1]
    vals = [int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{4})', body)]
    if len(vals) != w * h:
        raise SystemExit('픽셀 수 불일치: %d개 선언, %d개 발견' % (w * h, len(vals)))
    return w, h, vals


def encode(vals):
    """픽셀 배열을 토큰 스트림(bytearray)으로 압축한다."""
    out = bytearray()
    i, n = 0, len(vals)

    def emit(kind, count, colors=()):
        out.append((kind << 6) | count)
        for c in colors:
            out.append(c & 0xFF)
            out.append((c >> 8) & 0xFF)

    while i < n:
        v = vals[i]
        j = i
        while j < n and vals[j] == v:
            j += 1
        run = j - i

        if v == 0:                      # 투명 구간
            while run > 0:
                k = min(run, MAX_RUN)
                emit(SKIP, k)
                run -= k
            i = j
        elif run >= 3:                  # 같은 색이 3개 이상이면 반복 토큰이 이득
            while run > 0:
                k = min(run, MAX_RUN)
                emit(REPEAT, k, (v,))
                run -= k
            i = j
        else:                           # 잡다한 색이 이어지는 구간은 통째로 리터럴
            j = i
            while j < n:
                v2 = vals[j]
                k = j
                while k < n and vals[k] == v2:
                    k += 1
                if v2 == 0 or (k - j) >= 3:
                    break
                j = k
            while j > i:
                c = min(j - i, MAX_RUN)
                emit(LITERAL, c, vals[i:i + c])
                i += c
    out.append(0x00)                    # 끝 표시
    return out


TEMPLATE = '''#pragma once

// 이 파일은 pics/logo2header.py가 자동 생성한 것이다. 직접 수정하지 말 것.
// 원본 %(src)s 를 3종 토큰 RLE로 압축했다.
//   압축 전 %(raw)d bytes -> 압축 후 %(enc)d bytes (%(pct).1f%%)
//
// 토큰 1바이트 = [상위 2비트 종류][하위 6비트 개수 n]
//     00 n : 리터럴  - 뒤에 uint16(LE) n개
//     01 n : 투명    - 0x0000 픽셀 n개(데이터 없음, 건너뛴다)
//     10 n : 반복    - 뒤의 uint16(LE) 1개를 n번
//   0x00은 "리터럴 0개"라 나올 수 없어 스트림 끝 표시로 쓴다.

#include <Adafruit_GFX.h>

static const uint16_t LOGO_1D_ARCADE_W = %(w)d;
static const uint16_t LOGO_1D_ARCADE_H = %(h)d;
static const uint32_t LOGO_1D_ARCADE_PIXELS = %(px)dUL;

static const uint8_t LOGO_1D_ARCADE_RLE[] = {
%(data)s};

// 압축된 로고를 캔버스에 그린다. (ox, oy)는 로고 왼쪽 위 모서리 위치.
// 투명 픽셀은 건드리지 않으므로, 배경을 먼저 칠해 두면 그대로 비친다.
static void drawLogo1DArcade(GFXcanvas16& canvas, int16_t ox, int16_t oy) {
  const uint8_t* p = LOGO_1D_ARCADE_RLE;
  uint32_t px = 0;

  while (px < LOGO_1D_ARCADE_PIXELS) {
    uint8_t tok = *p++;
    if (tok == 0x00) break;                 // 스트림 끝
    uint8_t n = tok & 0x3F;
    uint8_t kind = tok >> 6;

    if (kind == 1) {                        // 투명 - 건너뛴다
      px += n;
      continue;
    }

    uint16_t color = 0;
    if (kind == 2) {                        // 반복 - 색을 한 번만 읽는다
      color = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
      p += 2;
    }
    for (uint8_t k = 0; k < n; k++) {
      if (kind == 0) {                      // 리터럴 - 픽셀마다 색을 읽는다
        color = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
        p += 2;
      }
      canvas.drawPixel(ox + (int16_t)(px %% LOGO_1D_ARCADE_W),
                       oy + (int16_t)(px / LOGO_1D_ARCADE_W), color);
      px++;
    }
  }
}
'''


def main():
    if len(sys.argv) != 3:
        raise SystemExit('사용법: python logo2header.py <입력.h> <출력.h>')
    src_path, dst_path = sys.argv[1], sys.argv[2]

    w, h, vals = parse_header(src_path)
    data = encode(vals)
    raw = len(vals) * 2

    lines = []
    for i in range(0, len(data), 16):
        lines.append('  ' + ' '.join('0x%02X,' % b for b in data[i:i + 16]))

    open(dst_path, 'w', encoding='utf-8').write(TEMPLATE % {
        'src': os.path.basename(src_path),
        'raw': raw, 'enc': len(data), 'pct': 100.0 * len(data) / raw,
        'w': w, 'h': h, 'px': w * h,
        'data': '\n'.join(lines) + '\n',
    })
    print('%s -> %s' % (src_path, dst_path))
    print('  %d bytes -> %d bytes (%.1f%%, %d bytes 절감)'
          % (raw, len(data), 100.0 * len(data) / raw, raw - len(data)))


if __name__ == '__main__':
    main()
