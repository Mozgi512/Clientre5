#!/bin/sh
# Generates the wide-coverage LVGL bitmap font used as the fallback behind the
# pixel fonts: the simplified and traditional Chinese that PixelMplus does not
# carry, plus the symbol blocks. 24 px so its half-width advance (12 px) matches
# PixelMplus at 24 px and the terminal grid stays intact.
#   $1 = directory containing NotoSansMonoCJKjp-Regular.otf and DejaVuSansMono.ttf
#   $2 = dictionary used for extra kanji coverage: SKK-JISYO (EUC-JP) or the
#        built main/ime/dict/skk_m.bin (optional)
# DejaVu fills in the codepoints Noto Sans CJK has no glyph for -- U+203E and
# U+2252 are in JIS X 0208 yet missing from it -- rescaled by mono_match.py so
# its advance width still matches one terminal cell.
# Requires node + lv_font_conv (npm i lv_font_conv) and fontTools.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
OUT="$HERE/../main/fonts"
FONTDIR="$1"
DICT="${2:-$HERE/../main/ime/dict/skk_m.bin}"
LVFC="${LV_FONT_CONV:-npx lv_font_conv}"
PRIMARY_FONT="$FONTDIR/NotoSansMonoCJKjp-Regular.otf"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
FILL_FONT="$TMP/fill.ttf"
python3 "$HERE/mono_match.py" "$PRIMARY_FONT" "$FONTDIR/DejaVuSansMono.ttf" "$FILL_FONT"
RANGES=$(python3 "$HERE/cjk_ranges.py" "$PRIMARY_FONT" "$FILL_FONT" "$DICT")
PRIMARY=$(echo "$RANGES" | sed -n 1p)
FILL=$(echo "$RANGES" | sed -n 2p)
mkdir -p "$OUT"
$LVFC --font "$PRIMARY_FONT" -r "$PRIMARY" --font "$FILL_FONT" -r "$FILL" \
  --size 24 --bpp 2 --format lvgl --lv-font-name font_cjk --lv-include lvgl.h \
  --no-kerning --no-prefilter -o "$OUT/font_cjk.c"
echo "font_cjk done"
