#!/usr/bin/env python3
"""Fit Symbols Nerd Font Mono into one terminal cell and print its ranges.

    nerd_cell.py <SymbolsNerdFontMono-Regular.ttf> <out.ttf>

The Mono symbols font draws every icon in a square em: advance = unitsPerEm,
centred between its ascender and descender.  A terminal cell here is half as
wide as it is tall (12 x 24 px at 24 px), so each icon is halved and centred
on a cell whose middle is half an em above the baseline -- the middle of the
MaruMinya box; fonts.c shifts the glyphs at run time for the Noto face.  The
Powerline range is stretched to the full cell height instead, so segments of a
prompt meet the row above and below; term_view.c also draws E0B0-E0BF itself
to cover the extra line spacing.

Only the private-use codepoints are kept: the few standard characters the
font carries (U+2665, U+26A1, ...) already have glyphs in the base faces.
Prints the lv_font_conv range list on stdout.
"""
import sys
from fontTools.ttLib import TTFont
from fontTools.pens.recordingPen import DecomposingRecordingPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.ttGlyphPen import TTGlyphPen

POWERLINE = (0xE0B0, 0xE0D7)


def private_use(cp):
    return 0xE000 <= cp <= 0xF8FF or cp >= 0xF0000


def ranges(cps):
    out, start, prev = [], cps[0], cps[0]
    for cp in cps[1:] + [None]:
        if cp is not None and cp == prev + 1:
            prev = cp
            continue
        out.append('0x%X' % start if start == prev else '0x%X-0x%X' % (start, prev))
        if cp is not None:
            start = prev = cp
    return ','.join(out)


def main():
    src_path, out_path = sys.argv[1:3]
    font = TTFont(src_path)
    upem = font['head'].unitsPerEm
    ascent, descent = font['hhea'].ascent, font['hhea'].descent
    src_mid = (ascent + descent) / 2
    cell_w, cell_mid = upem // 2, upem / 2

    cmap = font.getBestCmap()
    cps = sorted(cp for cp in cmap if private_use(cp))
    stretched = {cmap[cp] for cp in cps if POWERLINE[0] <= cp <= POWERLINE[1]}
    wanted = {cmap[cp] for cp in cps}

    # Record every outline before replacing any: a composite would otherwise
    # be decomposed from components that were already transformed.
    glyph_set = font.getGlyphSet()
    recorded = {}
    for name in wanted:
        rec = DecomposingRecordingPen(glyph_set)
        glyph_set[name].draw(rec)
        recorded[name] = rec
    glyf, hmtx = font['glyf'], font['hmtx']
    for name, rec in recorded.items():
        sy = 1.0 if name in stretched else 0.5
        pen = TTGlyphPen(None)
        rec.replay(TransformPen(pen, (0.5, 0, 0, sy, 0, cell_mid - src_mid * sy)))
        glyph = pen.glyph()
        glyph.recalcBounds(glyf)
        glyf[name] = glyph
        hmtx[name] = (cell_w, getattr(glyph, 'xMin', 0))

    # The cell box: baseline at the bottom, one em tall.
    font['hhea'].ascent, font['hhea'].descent, font['hhea'].lineGap = upem, 0, 0
    os2 = font['OS/2']
    os2.sTypoAscender, os2.sTypoDescender, os2.sTypoLineGap = upem, 0, 0
    os2.usWinAscent, os2.usWinDescent = upem, 0
    for tag in ('hdmx', 'LTSH', 'VDMX', 'fpgm', 'prep', 'cvt ', 'gasp'):
        if tag in font:
            del font[tag]
    font.save(out_path)
    sys.stderr.write('nerd_cell: %d private-use glyphs, advance %d of %d\n'
                     % (len(cps), cell_w, upem))
    print(ranges(cps))


if __name__ == '__main__':
    main()
