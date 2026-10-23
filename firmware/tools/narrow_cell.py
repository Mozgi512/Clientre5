#!/usr/bin/env python3
"""Fit DejaVu Sans Mono's symbols and non-Latin letters into one terminal cell.

    narrow_cell.py <DejaVuSansMono.ttf> <out.ttf>

Many characters a terminal gives one column -- the East Asian "ambiguous"
ones such as ● ○ ◐ ★ ⚠ “ ” α → -- have full-width 24 px glyphs in MaruMinya
and Noto Sans CJK, so the cell clip used to keep only their left half.  The
terminal draws these from this face instead (see fonts.c).  The whole font is
scaled so its advance is one 12 px cell and its em box is centred half an em
above the baseline, like the Nerd Font icons (nerd_cell.py); a glyph wider
than the cell is narrowed further around its own centre.

Prints the lv_font_conv range list on stdout.
"""
import sys
from fontTools.ttLib import TTFont
from fontTools.pens.recordingPen import DecomposingRecordingPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.pens.boundsPen import BoundsPen

from nerd_cell import ranges

# Blocks wanted, intersected with the font's cmap.  Box drawing and block
# elements (U+2500-259F) are drawn by term_view.c and left out.
BLOCKS = [
    (0xA1, 0xBF), (0xD7, 0xD7), (0xF7, 0xF7),   # Latin-1 signs, × ÷
    (0x370, 0x3FF), (0x400, 0x45F),             # Greek, Cyrillic
    (0x2010, 0x205E),                           # general punctuation
    (0x2100, 0x218F),                           # letterlike, number forms
    (0x2190, 0x23FF),                           # arrows, math, technical
    (0x2460, 0x24FF),                           # enclosed alphanumerics
    (0x25A0, 0x27BF),                           # geometric, misc symbols, dingbats
    (0x27F0, 0x27FF), (0x2900, 0x297F),         # supplemental arrows
    (0x2B00, 0x2BFF),                           # misc symbols and arrows
]


def main():
    src_path, out_path = sys.argv[1:3]
    font = TTFont(src_path)
    upem = font['head'].unitsPerEm
    ascent, descent = font['hhea'].ascent, font['hhea'].descent
    cmap = font.getBestCmap()
    advance = font['hmtx'][cmap[ord('M')]][0]
    cell_w, cell_mid = upem // 2, upem / 2
    scale = cell_w / advance
    src_mid = (ascent + descent) / 2

    cps = sorted(cp for cp in cmap if any(a <= cp <= b for a, b in BLOCKS))
    wanted = {cmap[cp] for cp in cps}
    glyph_set = font.getGlyphSet()
    recorded = {}
    for name in wanted:
        rec = DecomposingRecordingPen(glyph_set)
        glyph_set[name].draw(rec)
        bounds = BoundsPen(glyph_set)
        rec.replay(bounds)
        recorded[name] = (rec, bounds.bounds)

    glyf, hmtx = font['glyf'], font['hmtx']
    narrowed = 0
    for name, (rec, bounds) in recorded.items():
        s, dx = scale, 0.0
        if bounds:
            x0, _, x1, _ = bounds
            if (x1 - x0) * scale > cell_w * 0.96:
                # Narrow it around its centre and centre it in the cell.
                s = cell_w * 0.96 / (x1 - x0)
                dx = cell_w / 2 - (x0 + x1) / 2 * s
                narrowed += 1
        pen = TTGlyphPen(None)
        rec.replay(TransformPen(pen, (s, 0, 0, s, dx, cell_mid - src_mid * s)))
        glyph = pen.glyph()
        glyph.recalcBounds(glyf)
        glyf[name] = glyph
        hmtx[name] = (cell_w, getattr(glyph, 'xMin', 0))

    font['hhea'].ascent, font['hhea'].descent, font['hhea'].lineGap = upem, 0, 0
    os2 = font['OS/2']
    os2.sTypoAscender, os2.sTypoDescender, os2.sTypoLineGap = upem, 0, 0
    os2.usWinAscent, os2.usWinDescent = upem, 0
    for tag in ('hdmx', 'LTSH', 'VDMX', 'fpgm', 'prep', 'cvt ', 'gasp', 'kern', 'GPOS', 'GSUB'):
        if tag in font:
            del font[tag]
    font.save(out_path)
    sys.stderr.write('narrow_cell: %d glyphs (%d narrowed further), scale %.3f\n'
                     % (len(cps), narrowed, scale))
    print(ranges(cps))


if __name__ == '__main__':
    main()
