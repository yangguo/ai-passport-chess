# UI assets

`fonts/chess_zh_16.c` is an LVGL 9 subset generated from Noto Sans SC variable
font, at 16 px and 2 bits per pixel, with raw (uncompressed) glyph bitmaps so
it works with LVGL's default configuration. It contains only the Simplified
Chinese glyphs used by `components/chess_i18n/chess_i18n.c` plus common Chinese
punctuation; Latin text falls back to LVGL's Montserrat 14 font.

- Font source: [Noto Sans SC](https://github.com/google/fonts/tree/7085eb89a950e85db5b166b7a58d414544b4140c/ofl/notosanssc), pinned repository revision `7085eb89a950e85db5b166b7a58d414544b4140c`.
- Source font SHA-256: `a3041811a78c361b1de50f953c805e0244951c21c5bd412f7232ef0d899af0da`.
- License: SIL Open Font License 1.1, included as `fonts/OFL.txt`.
- Converter: `lv_font_conv` 1.5.3 (MIT).
- Regenerate with `python3 tools/generate_chess_font.py` (Node.js/npm required; no Docker).

The converted C font is about 42 KiB; the full upstream font is downloaded only
to a temporary working asset during conversion and is not committed.
