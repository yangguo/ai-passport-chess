#!/usr/bin/env python3
"""Print White first-move variety for book seeds 0..N-1 (host diagnostic)."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import chess
from gen_opening_book import (  # noqa: E402
    CACHE_PATH,
    LineSpec,
    RESP_PREFIXES,
    add_black_reply_lines,
    add_resp_continuations,
    collect_line_moves,
    trim_root_white,
)
from opening_book_lib import (  # noqa: E402
    build_position_tree,
    build_table_from_tree,
    fen_key,
    fnv1a64,
    load_chess_openings_rows,
    load_explorer_cache,
    position_key,
    select_opening_lines,
)


def main() -> int:
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 20
    cache, _meta = load_explorer_cache(CACHE_PATH)
    picked = select_opening_lines(load_chess_openings_rows())
    line_moves = collect_line_moves(picked)
    add_black_reply_lines(cache, line_moves)
    specs: list[LineSpec] = []
    for eco, name, moves in picked:
        specs.append(LineSpec(100, moves, None, name))
    add_resp_continuations(cache, specs)
    tree = build_position_tree(line_moves, [p for _t, p in RESP_PREFIXES])
    trim_root_white(tree, cache)
    for spec in specs:
        if not spec.prefix:
            continue
        board = chess.Board()
        for uci in spec.prefix:
            board.push(chess.Move.from_uci(uci))
        node_fen = fen_key(board)
        for uci in spec.moves:
            tree[node_fen].add(uci)
    table = build_table_from_tree(tree, cache)

    board = chess.Board()
    key = fnv1a64(position_key(board))
    moves = table[key]
    ordered = sorted(moves.items(), key=lambda x: (-x[1], x[0]))

    def pick(seed: int) -> tuple[int, int]:
        if seed == 0:
            return ordered[0][0]
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
