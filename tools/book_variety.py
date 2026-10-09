#!/usr/bin/env python3
"""Print White first-move variety for book seeds 0..N-1 (host diagnostic)."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "tests/host/build/test_opening_book"


def main() -> int:
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 20
    if not BIN.is_file():
        print("Build tests/host first (test_opening_book)", file=sys.stderr)
        return 1
    # Reuse the host test binary via a tiny helper compiled on the fly would be
    # heavy; call python-chess + generated table via gen script instead.
    sys.path.insert(0, str(ROOT / "tools"))
    from gen_opening_book import build_table, parse_lines, fnv1a64, position_key
    import chess

    table = build_table(parse_lines(ROOT / "components/chess_ai/opening_lines.txt"))
    board = chess.Board()
    key = fnv1a64(position_key(board))
    moves = table[key]
    ordered = sorted(moves.items(), key=lambda x: (-x[1], x[0]))

    def pick(seed: int) -> tuple[int, int]:
        if seed == 0:
            best = ordered[0]
            return best[0]
        rng = seed
        total = sum(w for _, w in ordered)
        rng = rng * 1664525 + 1013904223
        roll = (rng >> 8) % total
        for (fr, to), w in ordered:
            if roll < w:
                return fr, to
            roll -= w
        return ordered[-1][0]

    def sq_uci(sq: int) -> str:
        return chr(ord("a") + sq % 8) + chr(ord("1") + sq // 8)

    seen = set()
    for seed in range(n):
        fr, to = pick(seed)
        uci = sq_uci(fr) + sq_uci(to)
        seen.add(uci)
        print("seed %2d: %s" % (seed, uci))
    print("unique first moves (%d seeds): %s" % (n, ", ".join(sorted(seen))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
