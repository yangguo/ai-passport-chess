#!/usr/bin/env python3
"""Fail if any localized CJK glyph is absent from the generated LVGL font."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
translations = (ROOT / "components/chess_i18n/chess_i18n.c").read_text(
    encoding="utf-8"
)
font_source = (ROOT / "assets/fonts/chess_zh_18.c").read_text(encoding="utf-8")
if not re.search(r"\.bitmap_format\s*=\s*0\b", font_source):
    raise SystemExit(
        "Chinese font must use raw bitmaps; LVGL compressed-font support is disabled"
    )
if not re.search(r"\.bpp\s*=\s*4\b", font_source):
    raise SystemExit("Chinese font must use 4bpp grayscale for smoother strokes")
if not re.search(r"\.line_height\s*=\s*19\b", font_source):
    raise SystemExit("Chinese font must use 18px glyphs (19px line height)")
required = {
    ord(char) for char in translations if "\u4e00" <= char <= "\u9fff"
}
required.update(ord(char) for char in "，。：；！？…")
present = {int(value, 16) for value in re.findall(r"U\+([0-9A-F]{4,6})", font_source)}
missing = sorted(required - present)
if missing:
    chars = "".join(chr(value) for value in missing)
    raise SystemExit(f"Chinese font is missing {len(missing)} glyph(s): {chars}")
print(f"PASS: generated font covers {len(required)} localized Chinese glyphs.")
