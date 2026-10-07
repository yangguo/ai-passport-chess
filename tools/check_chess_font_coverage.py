#!/usr/bin/env python3
"""Fail if any localized CJK glyph is absent from the generated LVGL font."""

import argparse
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
translations = (ROOT / "components/chess_i18n/chess_i18n.c").read_text(
    encoding="utf-8"
)
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--font", type=Path,
                    default=ROOT / "assets/fonts/chess_zh_18.c")
font_source = parser.parse_args().font.read_text(encoding="utf-8")
if not re.search(r"\.bitmap_format\s*=\s*0\b", font_source):
    raise SystemExit(
        "Chinese font must use raw bitmaps; LVGL compressed-font support is disabled"
    )
if not re.search(r"\.bpp\s*=\s*4\b", font_source):
    raise SystemExit("Chinese font must use raw 4bpp antialiasing")
if not re.search(r"\.line_height\s*=\s*21\b", font_source):
    raise SystemExit("Chinese font must use 18px semibold glyphs (21px line height)")
required = {
    ord(char) for char in translations if "\u4e00" <= char <= "\u9fff"
}
required.update(ord(char) for char in "，。：；！？…")
present = {int(value, 16) for value in re.findall(r"U\+([0-9A-F]{4,6})", font_source)}
missing = sorted(required - present)
if missing:
    chars = "".join(chr(value) for value in missing)
    raise SystemExit(f"Chinese font is missing {len(missing)} glyph(s): {chars}")

# Catch accidentally rasterizing the VF's default Thin outlines, even if the
# file name and bpp look correct. These representative complex glyphs need
# substantial solid strokes rather than almost entirely translucent pixels.
bitmap = re.search(r"glyph_bitmap\[\] = \{(.*?)\n\};", font_source, re.S).group(1)
chunks = re.findall(r'/\* U\+([0-9A-F]+).*?\*/(.*?)(?=/\* U\+|$)', bitmap, re.S)
descriptors = re.search(r"glyph_dsc\[\] = \{(.*?)\n\};", font_source, re.S).group(1)
glyphs = [{key: int(value) for key, value in
           re.findall(r"\.(\w+)\s*=\s*(-?\d+)", item)}
          for item in re.findall(r"\{([^}]+)\}", descriptors)][1:]
for (codepoint, raw), glyph in zip(chunks, glyphs, strict=True):
    char = chr(int(codepoint, 16))
    if char not in "中棋暂":
        continue
    data = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]+)", raw))
    pixels = glyph["box_w"] * glyph["box_h"]
    values = [(data[i // 2] >> (4 if i % 2 == 0 else 0)) & 15
              for i in range(pixels)]
    solid_fraction = sum(value >= 12 for value in values) / pixels
    if solid_fraction < 0.15:
        raise SystemExit(f"Chinese glyph {char} has faint Thin strokes: "
                         f"{solid_fraction:.1%} solid pixels")
print(f"PASS: generated font covers {len(required)} localized Chinese glyphs.")
