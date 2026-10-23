#!/bin/sh
# Generates the terminal's one-column symbol face: DejaVu Sans Mono's symbols,
# punctuation, Greek and Cyrillic fitted to one 12 x 24 px cell by
# narrow_cell.py, for the characters the base faces draw full width.
#   $1 = DejaVuSansMono.ttf (https://github.com/dejavu-fonts/dejavu-fonts/releases, 2.37)
# Requires node + lv_font_conv (npm i lv_font_conv) and fontTools.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
OUT="$HERE/../main/fonts"
LVFC="${LV_FONT_CONV:-npx lv_font_conv}"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
RANGES=$(python3 "$HERE/narrow_cell.py" "$1" "$TMP/narrow.ttf")
mkdir -p "$OUT"
$LVFC --font "$TMP/narrow.ttf" -r "$RANGES" \
  --size 24 --bpp 2 --format lvgl --lv-font-name font_narrow --lv-include lvgl.h \
  --no-kerning --no-prefilter -o "$OUT/font_narrow.c"
echo "font_narrow done"
