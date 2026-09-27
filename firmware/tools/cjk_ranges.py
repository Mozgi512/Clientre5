#!/usr/bin/env python3
"""Print the codepoint lists used for the wide-coverage CJK terminal font.

    cjk_ranges.py <primary.otf> [fill.ttf] [dictionary]

Line 1 is the range list for the primary (Noto Sans Mono CJK) font, line 2 the
list for the fill font (DejaVu Sans Mono) holding the wanted codepoints the
primary font has no glyph for -- U+203E and U+2252 are in JIS X 0208 yet absent
from Noto Sans CJK, and the technical/dingbat blocks are only partly covered.

Coverage is the union of: ASCII/Latin (incl. Extended-B and IPA), Greek,
Cyrillic, punctuation, super/subscripts, arrows, math, technical, box drawing,
block elements, geometric shapes, dingbats, CJK radicals, kana (full *and*
half width), enclosed and squared CJK forms, fullwidth forms, compatibility
ideographs, every Han character the font has in the Unified Ideographs block,
Extension A limited to Big5 / JIS X 0213, and any extra character used by the
bundled IME dictionary.
"""
import sys

# Blocks wanted in full; each is intersected with the font's cmap afterwards.
RANGES = [
    (0x20, 0x7E), (0xA0, 0x17F), (0x180, 0x2FF), (0x370, 0x3FF), (0x400, 0x45F),
    (0x2000, 0x206F), (0x2070, 0x209F), (0x20A0, 0x20BF),
    (0x2100, 0x214F), (0x2150, 0x218F), (0x2190, 0x21FF), (0x2200, 0x22FF),
    (0x2300, 0x23FF), (0x2400, 0x2426), (0x2460, 0x24FF),
    (0x2500, 0x259F), (0x25A0, 0x25FF), (0x2600, 0x27BF), (0x2B00, 0x2BFF),
    (0x2E80, 0x2EFF), (0x2F00, 0x2FDF),
    (0x3000, 0x303F), (0x3041, 0x309F), (0x30A0, 0x30FF),
    (0x3105, 0x312F), (0x3131, 0x318E), (0x3190, 0x31EF),
    (0x3200, 0x33FF),
    (0xF900, 0xFAFF),
    (0xFE10, 0xFE6F),
    (0xFF01, 0xFF60), (0xFF61, 0xFF9F), (0xFFE0, 0xFFE6),
]


def encodable(cp, codec):
    try:
        chr(cp).encode(codec)
        return True
    except Exception:
        return False


def cmap_of(path):
    from fontTools.ttLib import TTFont
    return set(TTFont(path, lazy=True).getBestCmap())


def dict_chars(path):
    """Characters used by a dictionary: SKK-JISYO text (EUC-JP) or an IMD1 blob."""
    with open(path, 'rb') as f:
        raw = f.read()
    text = raw.decode('utf-8', 'ignore') if raw[:4] == b'IMD1' else raw.decode('euc_jp', 'ignore')
    return {ord(c) for c in text}


def compact(cps):
    out, start, prev = [], None, None
    for cp in sorted(cps):
        if start is None:
            start = prev = cp
        elif cp == prev + 1:
            prev = cp
        else:
            out.append(f"0x{start:X}-0x{prev:X}" if start != prev else f"0x{start:X}")
            start = prev = cp
    if start is not None:
        out.append(f"0x{start:X}-0x{prev:X}" if start != prev else f"0x{start:X}")
    return ",".join(out)


def main():
    primary_path = sys.argv[1]
    fill_path = sys.argv[2] if len(sys.argv) > 2 and sys.argv[2] else None
    dict_path = sys.argv[3] if len(sys.argv) > 3 and sys.argv[3] else None

    wanted = set()
    for a, b in RANGES:
        wanted.update(range(a, b + 1))
    # Han: everything the font offers in the Unified Ideographs block, plus the
    # Extension A characters reachable from Big5 or JIS X 0213.
    wanted.update(range(0x4E00, 0x9FFF + 1))
    for cp in range(0x3400, 0x4DBF + 1):
        if encodable(cp, 'big5') or encodable(cp, 'euc_jis_2004'):
            wanted.add(cp)
    # JIS X 0213 levels 3/4 (names, place names) outside the blocks above.
    for cp in range(0x2E80, 0x10000):
        if encodable(cp, 'euc_jis_2004'):
            wanted.add(cp)
    if dict_path:
        try:
            wanted |= {cp for cp in dict_chars(dict_path) if cp > 0x7E}
        except Exception as e:
            sys.stderr.write(f"dictionary scan skipped: {e}\n")

    primary = cmap_of(primary_path)
    have = wanted & primary
    fill = set()
    if fill_path:
        fill = (wanted - primary) & cmap_of(fill_path)
    sys.stderr.write(f"primary: {len(have)} codepoints; fill: {len(fill)}; "
                     f"unavailable: {len(wanted - primary - fill)}\n")
    print(compact(have))
    print(compact(fill))


if __name__ == '__main__':
    main()
