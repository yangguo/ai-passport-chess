# Opening book sources

The position-keyed book in `opening_book.c` is generated from
`opening_lines.txt` via `tools/gen_opening_book.py`. Move sequences are
hand-written for this repository; they follow well-known opening **names**
(Italian Game, Queen's Gambit, Sicilian, Caro-Kann, etc.) but are not
copied from Polyglot binaries, Lichess PGN dumps, Sunfish, or commercial
books.

Host validation uses [python-chess](https://github.com/niklasf/python-chess)
(GPL, test-only; see `docs/sources.md`). The device firmware does not link
python-chess.

Regenerate after editing lines:

```bash
python3 tools/gen_opening_book.py
```
