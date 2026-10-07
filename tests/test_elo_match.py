import unittest
import io
import os
import tempfile
from itertools import cycle
from types import SimpleNamespace
from unittest import mock
from contextlib import redirect_stdout

import chess

from tools import elo_match
from tools.elo_match import play_game


class CycleCli:
    def __init__(self):
        self.moves = cycle(("g1f3", "f3g1"))
        self.played = []

    def ai_move(self):
        return next(self.moves)

    def play(self, uci):
        self.played.append(uci)
        return True

    def close(self):
        pass


class CycleEngine:
    def __init__(self):
        self.moves = cycle(("g8f6", "f6g8"))

    def play(self, board, limit):
        return SimpleNamespace(move=chess.Move.from_uci(next(self.moves)))


class EloMatchTests(unittest.TestCase):
    def test_stops_when_threefold_draw_can_be_claimed(self):
        cli = CycleCli()
        result, pgn = play_game(cli, CycleEngine(), chess.WHITE, max_plies=16)

        self.assertEqual(result, 0.5)
        game = chess.pgn.read_game(io.StringIO(pgn))
        self.assertEqual(sum(1 for _ in game.mainline_moves()), 7)

    def test_refuses_rating_when_any_game_aborts(self):
        engine = mock.Mock()
        output = io.StringIO()
        with tempfile.TemporaryDirectory() as temp_dir:
            pgn_path = os.path.join(temp_dir, "match.pgn")
            with mock.patch.object(elo_match.sys, "argv", [
                "elo_match.py", "--cli", "unused", "--stockfish", "unused",
                "--games", "2", "--pgn", pgn_path,
            ]), mock.patch.object(
                elo_match.chess.engine.SimpleEngine,
                "popen_uci",
                return_value=engine,
            ), mock.patch.object(
                elo_match,
                "play_game",
                side_effect=[(1.0, "complete pgn"), (None, "partial pgn")],
            ), mock.patch.object(
                elo_match, "Cli", return_value=CycleCli()
            ), redirect_stdout(output):
                result = elo_match.main()

            with open(pgn_path, encoding="utf-8") as pgn_file:
                saved_pgn = pgn_file.read()

        self.assertEqual(result, 1)
        self.assertIn("ABORTED", output.getvalue())
        self.assertNotIn("implied ours", output.getvalue())
        self.assertIn("partial pgn", saved_pgn)


if __name__ == "__main__":
    unittest.main()
