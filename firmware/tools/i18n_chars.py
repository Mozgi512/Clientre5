#!/usr/bin/env python3
"""Collect every non-ASCII character used in the firmware sources so the UI
fonts can be generated as an exact subset.

    i18n_chars.py <dir>... [--font <path>]

Walks each directory for .c/.h files -- the translations live in the X-macro
table in i18n/i18n.h, not only in i18n.c -- and skips the generated fonts.
With --font, only the characters that font actually has are printed, so a
partial font (a pixel font covering JIS X 0208 but not the simplified Chinese
in the zh translations) can still be generated and left to fall back.
"""
import os, sys

argv = sys.argv[1:]
font = None
if "--font" in argv:
    i = argv.index("--font")
    font = argv[i + 1]
    argv = argv[:i] + argv[i + 2:]

chars = set()
for root_dir in argv:
    for root, _dirs, files in os.walk(root_dir):
        for name in files:
            if not name.endswith((".c", ".h")) or name.startswith("font_"):
                continue
            with open(os.path.join(root, name), encoding="utf-8", errors="replace") as f:
                for ch in f.read():
                    if ord(ch) > 0x7E and ch != "�":
                        chars.add(ch)
if font:
    from fontTools.ttLib import TTFont
    cmap = TTFont(font, lazy=True).getBestCmap()
    dropped = {c for c in chars if ord(c) not in cmap}
    if dropped:
        sys.stderr.write("%d characters not in %s, left to the fallback: %s\n"
                         % (len(dropped), os.path.basename(font), "".join(sorted(dropped))))
    chars -= dropped
print("".join(sorted(chars)))
