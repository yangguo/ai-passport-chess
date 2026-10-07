#!/usr/bin/env python3
"""Rebuild the compact LVGL CJK subset from the pinned Noto Sans SC source."""

import hashlib
import subprocess
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FONT = ROOT / "assets/fonts/.NotoSansSC-VF.ttf"
SOURCE = (
    "https://raw.githubusercontent.com/google/fonts/"
    "7085eb89a950e85db5b166b7a58d414544b4140c/"
    "ofl/notosanssc/NotoSansSC%5Bwght%5D.ttf"
)
EXPECTED_SHA256 = (
    "a3041811a78c361b1de50f953c805e0244951c21c5bd412f7232ef0d899af0da"
)


def main() -> None:
    i18n_source = (ROOT / "components/chess_i18n/chess_i18n.c").read_text(
        encoding="utf-8"
    )
    symbols = sorted({char for char in i18n_source if "\u4e00" <= char <= "\u9fff"})
    symbols.extend("，。：；！？…")
    FONT.parent.mkdir(parents=True, exist_ok=True)
    try:
        urllib.request.urlretrieve(SOURCE, FONT)
        digest = hashlib.sha256(FONT.read_bytes()).hexdigest()
        if digest != EXPECTED_SHA256:
            raise SystemExit(f"unexpected Noto Sans SC SHA-256: {digest}")
        subprocess.run(
            [
                "npx",
                "--yes",
                "lv_font_conv@1.5.3",
                "--font",
                str(FONT.relative_to(ROOT)),
                "--symbols",
                "".join(symbols),
                "--size",
                "18",
                "--bpp",
                "4",
                "--no-compress",
                "--format",
                "lvgl",
                "--lv-include",
                "lvgl.h",
                "--lv-font-name",
                "chess_font_noto_sc_18",
                "--lv-fallback",
                "lv_font_montserrat_14",
                "-o",
                "assets/fonts/chess_zh_18.c",
            ],
            cwd=ROOT,
            check=True,
        )
        output = ROOT / "assets/fonts/chess_zh_18.c"
        output.write_text(output.read_text(encoding="utf-8").rstrip() + "\n",
                          encoding="utf-8")
    finally:
        FONT.unlink(missing_ok=True)


if __name__ == "__main__":
    main()
