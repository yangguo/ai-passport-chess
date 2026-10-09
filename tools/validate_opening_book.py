#!/usr/bin/env python3
"""Validate generated opening book lines with python-chess (test-only)."""
from __future__ import annotations

import sys
from pathlib import Path

import chess

ROOT = Path(__file__).resolve().parents[1]
LINES = ROOT / "components/chess_ai/opening_lines.txt"


def parse_generated(path: Path) -> list[tuple[list[str], list[str] | None]]:
    specs: list[tuple[list[str], list[str] | None]] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if parts[0] == "RESP":
            prefix = [parts[1]]
            moves = [parts[3]]
            specs.append((moves, prefix))
            continue
        moves = parts[1:]
        specs.append((moves, None))
    return specs


def validate_sequence(moves: list[str], prefix: list[str] | None) -> bool:
    board = chess.Board()
    if prefix:
        for uci in prefix:
            mv = chess.Move.from_uci(uci)
            if mv not in board.legal_moves:
                print("illegal prefix %s before %s" % (uci, board.fen()))
                return False
            board.push(mv)
    if prefix and len(moves) == 1:
        mv = chess.Move.from_uci(moves[0])
        if mv not in board.legal_moves:
            print("illegal RESP move %s at %s" % (moves[0], board.fen()))
            return False
        return True
    for uci in moves:
        mv = chess.Move.from_uci(uci)
        if mv not in board.legal_moves:
            print("illegal book move %s in %s" % (uci, board.fen()))
            return False
        board.push(mv)
    return True


def main() -> int:
    specs = parse_generated(LINES)
    errors = sum(1 for moves, prefix in specs if not validate_sequence(moves, prefix))
    if errors:
        print("FAIL: %d illegal sequences" % errors)
        return 1
    print("OK: %d line specs validated" % len(specs))
    return 0


if __name__ == "__main__":
    sys.exit(main())
