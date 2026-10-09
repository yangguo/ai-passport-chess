#!/usr/bin/env python3
"""Curated explorer fallback and generator offline path (host-only)."""
from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import chess

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from opening_book_lib import fen_key  # noqa: E402
from opening_explorer_fallback import (  # noqa: E402
    CURATED_PREFIXES,
    board_after,
    curated_fen_table,
)
from fetch_opening_explorer_cache import collect_book_fens, write_fallback_cache  # noqa: E402

REAL_CACHE = ROOT / "tools/data/opening_explorer_cache.json"
GEN = ROOT / "tools/gen_opening_book.py"
BOOK_C = ROOT / "components/chess_ai/opening_book.c"


class OpeningFallbackTest(unittest.TestCase):
    def test_curated_keys_match_reachable_positions(self) -> None:
        table = curated_fen_table()
        self.assertEqual(len(table), len(CURATED_PREFIXES))
        for prefix, counts in CURATED_PREFIXES:
            board = board_after(prefix)
            fen = fen_key(board)
            self.assertIn(fen, table)
            self.assertEqual(table[fen], counts)
            replay = chess.Board()
            for uci in prefix:
                mv = chess.Move.from_uci(uci)
                self.assertIn(mv, replay.legal_moves)
                replay.push(mv)
            self.assertEqual(replay.fen(), board.fen())

    def test_d4_and_c4_pawn_ranks(self) -> None:
        after_d4 = board_after(["d2d4"])
        self.assertIn("PPP1PPPP", after_d4.fen())
        self.assertNotIn("PPP2PPP", after_d4.fen().split()[0])

        after_c4 = board_after(["c2c4"])
        # 1.c4 — no pawn on c2 (not 1.c4 c5).
        self.assertIn("PP1PPPPP", after_c4.fen())
        self.assertNotIn("2p5", after_c4.fen().split()[0])

    def test_force_fallback_cache_covers_book_fens(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "fallback.json"
            write_fallback_cache(out)
            data = json.loads(out.read_text(encoding="utf-8"))
            self.assertEqual(data["primary_source"], "curated_fallback")
            book_fens = set(collect_book_fens())
            cached = set(data["positions"].keys())
            self.assertTrue(book_fens <= cached)

    def test_generator_with_fallback_only_cache(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            cache = Path(tmp) / "fallback.json"
            lines = Path(tmp) / "lines.txt"
            out_c = Path(tmp) / "opening_book.c"
            write_fallback_cache(cache)
            proc = subprocess.run(
                [
                    sys.executable,
                    str(GEN),
                    "--cache",
                    str(cache),
                    "--lines",
                    str(lines),
                    "--out-c",
                    str(out_c),
                ],
                cwd=ROOT,
                capture_output=True,
                text=True,
            )
            self.assertEqual(proc.returncode, 0, msg=proc.stderr or proc.stdout)
            self.assertTrue(lines.is_file())
            self.assertTrue(out_c.is_file())
            self.assertIn("OPENING_BOOK_ENTRY_COUNT", out_c.read_text(encoding="utf-8"))

    def test_real_cache_regeneration_unchanged(self) -> None:
        before = BOOK_C.read_bytes()
        proc = subprocess.run(
            [sys.executable, str(GEN), "--cache", str(REAL_CACHE)],
            cwd=ROOT,
            capture_output=True,
        )
        self.assertEqual(proc.returncode, 0, msg=proc.stderr.decode())
        after = BOOK_C.read_bytes()
        self.assertEqual(before, after)


if __name__ == "__main__":
    unittest.main()
