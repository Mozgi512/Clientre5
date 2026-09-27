#!/bin/sh
# Builds the release images: individual partition binaries plus a merged
# full-flash image (flash at 0x0) and the update.json manifest for in-app OTA.
# Run after `idf.py build` in firmware/ and updater/.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
FW="$HERE/.."
OUT="${1:-$FW/release}"
REPO_RAW="${REPO_RAW:-https://github.com/Mozgi512/Clientre5/releases/latest/download}"
mkdir -p "$OUT/partitions" "$OUT/full"
cp "$FW/build/bootloader/bootloader.bin"            "$OUT/partitions/"
cp "$FW/build/partition_table/partition-table.bin"  "$OUT/partitions/partition_table.bin"
cp "$FW/build/ota_data_initial.bin"                 "$OUT/partitions/"
cp "$FW/build/clientre5.bin"                        "$OUT/partitions/"
cp "$FW/updater/build/tab5_factory_updater.bin"     "$OUT/partitions/"
python -m esptool --chip esp32p4 merge_bin --flash_mode dio --flash_size 16MB --flash_freq 80m \
  -o "$OUT/full/clientre5_full_flash.bin" \
  0x2000  "$OUT/partitions/bootloader.bin" \
  0x8000  "$OUT/partitions/partition_table.bin" \
  0xF000  "$OUT/partitions/ota_data_initial.bin" \
  0x20000 "$OUT/partitions/clientre5.bin" \
  0xF80000 "$OUT/partitions/tab5_factory_updater.bin"
# update manifest for the in-app OTA check
APP="$OUT/partitions/clientre5.bin"
UPD="$OUT/partitions/tab5_factory_updater.bin"
VER=$(python3 -c "import sys;d=open('$APP','rb').read();i=d.find(b'clientre5\0');print(d[i-32:i].split(b'\0')[0].decode(errors='ignore'))")
cat > "$OUT/update.json" <<JSON
{
  "project": "clientre5",
  "version": "$VER",
  "url": "$REPO_RAW/clientre5.bin",
  "sha256": "$(shasum -a 256 "$APP" | cut -d' ' -f1)",
  "size": $(stat -f%z "$APP"),
  "notes": "",
  "updater": {
    "version": "1.0.0",
    "url": "$REPO_RAW/tab5_factory_updater.bin",
    "sha256": "$(shasum -a 256 "$UPD" | cut -d' ' -f1)",
    "size": $(stat -f%z "$UPD")
  }
}
JSON
ls -l "$OUT/partitions" "$OUT/full" "$OUT/update.json"
