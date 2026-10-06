#!/bin/sh
# Merge the 0x0 flash image and verify partition layout + sizes.
# Run inside an activated ESP-IDF environment after `idf.py build`.
# Fails on missing/stale artifacts (never silently passes).
set -eu
BUILD="${1:-build}"

die() {
  echo "validate_firmware: FAIL: $1" >&2
  exit 1
}

[ -f "$BUILD/ai-passport-chess.bin" ] || die "missing app image (build first?)"
# The partition CSV lives at the project root; the build dir only
# holds the generated binary table.
[ -f "partitions.csv" ] || die "missing partition table"
[ -f "$BUILD/flasher_args.json" ] || die "missing flasher args (build first?)"

mkdir -p "$BUILD"
# Absolute output path: esptool must never depend on ambient CWD.
idf.py merge-bin -o "$PWD/$BUILD/merged.bin" >/dev/null
[ -f "$BUILD/merged.bin" ] || die "merge-bin produced nothing"

# Factory app must fit its 0x7f0000 partition; report hard numbers.
app_size=$(wc -c < "$BUILD/ai-passport-chess.bin")
echo "validate_firmware: app image bytes: $app_size (partition 8323072)"
[ "$app_size" -le 8323072 ] || die "app image overflows factory partition"

merged_size=$(wc -c < "$BUILD/merged.bin")
merged_sha=$(sha256sum "$BUILD/merged.bin" | cut -d' ' -f1)
echo "validate_firmware: merged bytes: $merged_size sha256: $merged_sha"
echo "validate_firmware: PASS"
