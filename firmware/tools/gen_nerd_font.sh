#!/bin/sh
# Generates the terminal's Nerd Font fallback: every private-use icon of
# Symbols Nerd Font Mono (Powerline, Font Awesome, Material Design, Devicons,
# Octicons, Codicons, ...) fitted to one 12 x 24 px cell by nerd_cell.py.
#   $1 = SymbolsNerdFontMono-Regular.ttf from NerdFontsSymbolsOnly.zip
#        (https://github.com/ryanoasis/nerd-fonts/releases, v3.5.1)
# Requires node + lv_font_conv (npm i lv_font_conv) and fontTools.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
OUT="$HERE/../main/fonts"
LVFC="${LV_FONT_CONV:-npx lv_font_conv}"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
RANGES=$(python3 "$HERE/nerd_cell.py" "$1" "$TMP/nerd.ttf")
mkdir -p "$OUT"
$LVFC --font "$TMP/nerd.ttf" -r "$RANGES" \
  --size 24 --bpp 2 --format lvgl --lv-font-name font_nerd --lv-include lvgl.h \
  --no-kerning --no-prefilter -o "$OUT/font_nerd.c"
echo "font_nerd done"
