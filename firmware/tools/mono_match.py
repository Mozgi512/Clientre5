#!/usr/bin/env python3
"""Write a copy of a monospace fallback font whose advance width matches the
primary font's, so the terminal grid stays intact.

    mono_match.py <primary> <fallback> <out.ttf>

lv_font_conv renders each source font at `size / unitsPerEm`, so scaling the
em square of the fallback scales its glyphs and advances together.  DejaVu Sans
Mono is 20% wider than Noto Sans Mono CJK at the same pixel size; without this
every fallback glyph would push the rest of its terminal row to the right.
"""
import sys
from fontTools.ttLib import TTFont


def advance(font):
    cmap = font.getBestCmap()
    return font['hmtx'][cmap[ord('A')]][0] / font['head'].unitsPerEm


def main():
    primary_path, fallback_path, out_path = sys.argv[1:4]
    primary = TTFont(primary_path, lazy=True)
    fallback = TTFont(fallback_path)
    ratio = advance(fallback) / advance(primary)
    upem = round(fallback['head'].unitsPerEm * ratio)
    sys.stderr.write("mono_match: %s em %d -> %d (advance ratio %.4f)\n"
                     % (fallback_path.rsplit('/', 1)[-1],
                        fallback['head'].unitsPerEm, upem, ratio))
    fallback['head'].unitsPerEm = upem
    fallback.save(out_path)


if __name__ == '__main__':
    main()
