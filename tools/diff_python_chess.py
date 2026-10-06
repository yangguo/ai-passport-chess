#!/usr/bin/env python3
"""External differential runner: our perft depth-1 root set vs python-chess.

Plays seeded random games from the cases.json FENs; at every ply compares
the C core's legal UCI set (`perft --divide --depth 1` root tokens) with
python-chess's legal move set. Any divergence fails loudly with the FEN
and both sets.

python-chess is GPL and runs in the test process only: it is never linked
into firmware or the C core. Pinned version in chess_diff_requirements.txt.

Usage: diff_python_chess.py --perft <cli> --cases <json> [--games N]
    [--plies N] [--seed N]
"""
import argparse
import json
import random
import subprocess
import sys

try:
    import chess
except ImportError:
    print("SKIP: python-chess not installed (see chess_diff_requirements.txt)")
    sys.exit(0)


def our_legal_set(cli, fen):
    proc = subprocess.run(
        [cli, "--fen", fen, "--depth", "1", "--divide"],
        capture_output=True, text=True, timeout=120,
    )
    if proc.returncode != 0:
        raise RuntimeError("perft failed: %s" % proc.stderr.strip())
    moves = set()
    for line in proc.stdout.splitlines():
        line = line.strip()
        if not line or " " not in line:
            continue
        uci, _ = line.split(" ", 1)
        moves.add(uci)
    return moves


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--perft", required=True)
    parser.add_argument("--cases", required=True)
    parser.add_argument("--games", type=int, default=4)
    parser.add_argument("--plies", type=int, default=40)
    parser.add_argument("--seed", type=int, default=20261006)
    args = parser.parse_args()

    with open(args.cases, encoding="utf-8") as handle:
        fens = [case["fen"] for case in json.load(handle)["cases"]]

    rng = random.Random(args.seed)
    checked = 0
    for game in range(args.games):
        board = chess.Board(fens[game % len(fens)])
        for _ in range(args.plies):
            legal = sorted(m.uci() for m in board.legal_moves)
            if not legal:
                break
            try:
                ours = our_legal_set(args.perft, board.fen())
            except RuntimeError as exc:
                print("FAIL %s: %s" % (board.fen(), exc))
                return 1
            if set(legal) != ours:
                print("FAIL %s" % board.fen())
                print("  python-chess only: %s" % sorted(set(legal) - ours))
                print("  core only:         %s" % sorted(ours - set(legal)))
                return 1
            checked += 1
            board.push(rng.choice(list(board.legal_moves)))

    print("PASS: differential %d positions, seed %d" % (checked, args.seed))
    return 0


if __name__ == "__main__":
    sys.exit(main())
