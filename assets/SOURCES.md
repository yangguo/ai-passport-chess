# UI assets

`fonts/chess_zh_18.c` is an LVGL 9 subset generated from Noto Sans SC variable
font, explicitly instantiated at weight 600 (SemiBold), at 18 px and 4 bits per
pixel, with raw (uncompressed) glyph bitmaps so
it works with LVGL's default configuration. It contains only the Simplified
Chinese glyphs used by `components/chess_i18n/chess_i18n.c` plus common Chinese
punctuation; Latin text falls back to LVGL's Montserrat 14 font.

- Font source: [Noto Sans SC](https://github.com/google/fonts/tree/7085eb89a950e85db5b166b7a58d414544b4140c/ofl/notosanssc), pinned repository revision `7085eb89a950e85db5b166b7a58d414544b4140c`.
- Source font SHA-256: `a3041811a78c361b1de50f953c805e0244951c21c5bd412f7232ef0d899af0da`.
- License: SIL Open Font License 1.1, included as `fonts/OFL.txt`.
- Converter: `lv_font_conv` 1.5.3 (MIT).
- Variable-font instancer: FontTools 4.66.1 (MIT), pinned `wght=600`. The upstream
  font defaults to weight 100; using it directly produces faint Thin strokes.
- Regenerate with `python3 tools/generate_chess_font.py` (uv and Node.js/npm required; no Docker).

The subset contains 12,842 bytes of raw glyph bitmaps stored in Flash, plus
descriptors and maps. Its 21px line height fits the 30px header and menu rows;
two footer lines fit the 50px footer. Source-file size is not the firmware RAM
footprint. The full upstream font and static instance are temporary conversion
inputs and are not committed. The coverage check also rejects representative
glyphs with less than 15% solid-stroke pixels, preventing a return to Thin.
