#!/usr/bin/env python3
"""Compare chess-cli best moves with TT on vs off at fixed node/depth budgets."""
import subprocess
import sys

FENS = [
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
    "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3",
    "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
]


def ai_move(cli, fen, nodes, depth):
    script = "loadfen %s\nai %d %d\nquit\n" % (fen, nodes, depth)
    proc = subprocess.run(
        [cli],
        input=script,
        capture_output=True,
        text=True,
        timeout=120,
    )
    for line in proc.stdout.splitlines():
        line = line.strip()
        if line.startswith("engine: "):
            return line.split(" ", 1)[1]
    raise RuntimeError("no engine move from %s: %s" % (cli, proc.stdout[-200:]))


def main():
    if len(sys.argv) != 4:
        print("usage: compare_tt_moves.py <cli-tt> <cli-nott> <nodes>")
        return 2
    tt, nott, nodes = sys.argv[1], sys.argv[2], int(sys.argv[3])
    depth = 4
    failed = 0
    for fen in FENS:
        a = ai_move(tt, fen, nodes, depth)
        b = ai_move(nott, fen, nodes, depth)
        ok = a == b
        tag = "MATCH" if ok else "DIFF"
        print("%s %s %s %s" % (tag, a, b, fen[:48]))
        if not ok:
            failed += 1
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
