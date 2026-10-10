#!/usr/bin/env python3
"""Generate capped knight/bishop/king PST tables for mcu-max (engine units in flash)."""
from __future__ import annotations

import argparse
import textwrap
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "tools" / "pesto_source.py"
OUT = ROOT / "third_party" / "mcu-max" / "mcumax_nbk_tables.h"

PESTO_PAWN_CP = 100
MCUMAX_PST_DELTA_CAP = 32

# 0x88 step deltas used by mcu-max (knight + king/queen rays for cap checks).
KNIGHT_STEPS = (14, 18, 31, 33)
KING_STEPS = (1, 16, 15, 17, -1, -16, -15, -17)
BISHOP_STEPS = (1, 16, 15, 17, -1, -16, -15, -17)


def clamp8(v: int) -> int:
    return max(-127, min(127, v))


def load_pesto_planes() -> dict[str, list[int]]:
    text = SRC.read_text()
    mg: dict[str, list[int]] = {}
    eg: dict[str, list[int]] = {}
    import ast
    import re

    for piece in ("knight", "bishop", "king"):
        m = re.search(
            rf"mg_{piece}_table\s*=\s*\[(.*?)\]",
            text,
            re.DOTALL,
        )
        if not m:
            raise SystemExit(f"missing mg_{piece}_table in pesto_source.py")
        mg[piece] = ast.literal_eval("[" + m.group(1) + "]")
        m = re.search(
            rf"eg_{piece}_table\s*=\s*\[(.*?)\]",
            text,
            re.DOTALL,
        )
        if not m:
            raise SystemExit(f"missing eg_{piece}_table in pesto_source.py")
        eg[piece] = ast.literal_eval("[" + m.group(1) + "]")
    return {"mg": mg, "eg": eg}


def pesto_a8_to_0x88(pesto_idx: int) -> int:
    """PeSTO a8=0 -> mcu-max 0xRF (rank nibble 0 = rank 8).

    Dense index ((sq>>4)<<3)|(sq&7) equals the published PeSTO index.
    Black is flipped at lookup (square ^ 0x70), not stored in the table.
    """
    file = pesto_idx & 7
    rank = pesto_idx >> 3
    return (rank << 4) | file


def table_64_to_0x88(values: list[int]) -> dict[int, int]:
    out: dict[int, int] = {}
    for pesto_idx, v in enumerate(values):
        out[pesto_a8_to_0x88(pesto_idx)] = v
    return out


def scale_pesto(v: int, pawn_unit: int) -> int:
    return clamp8(round(v * pawn_unit / PESTO_PAWN_CP))


def max_one_move_delta(board: dict[int, int], steps: tuple[int, ...], slide: bool) -> int:
    best = 0
    for sq, val in board.items():
        if sq & 0x88:
            continue
        for step in steps:
            if slide:
                to = sq + step
                while not (to & 0x88) and to in board:
                    best = max(best, abs(board[to] - val))
                    to += step
            else:
                to = sq + step
                if to & 0x88 or to not in board:
                    continue
                best = max(best, abs(board[to] - val))
    return best


def scale_board_to_cap(
    board: dict[int, int], steps: tuple[int, ...], cap: int, slide: bool
) -> dict[int, int]:
    peak = max_one_move_delta(board, steps, slide)
    if peak <= cap or peak == 0:
        return board
    factor = cap / peak
    return {sq: clamp8(round(v * factor)) for sq, v in board.items()}


def fmt_plane(name: str, board: dict[int, int]) -> str:
    lines = [
        f"static const int8_t {name}[64] = {{",
        "    /* dense index: ((sq >> 4) << 3) | (sq & 7), sq valid 0x88 */",
    ]
    for rank in range(8):
        row = []
        for file in range(8):
            sq = (rank << 4) | file
            row.append(str(board.get(sq, 0)))
        lines.append("    " + ", ".join(row) + ",")
    lines.append("};")
    return "\n".join(lines)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--pawn-unit",
        type=int,
        default=52,
        help="mcu-max pawn units per PeSTO 100cp pawn before capping (default 52)",
    )
    ap.add_argument(
        "--cap",
        type=int,
        default=MCUMAX_PST_DELTA_CAP,
        help="max |pst(to)-pst(from)| per quiet move (default 32)",
    )
    args = ap.parse_args()

    pesto = load_pesto_planes()
    planes = {
        "mcumax_nbk_knight": scale_board_to_cap(
            table_64_to_0x88(
                [scale_pesto(v, args.pawn_unit) for v in pesto["mg"]["knight"]]
            ),
            KNIGHT_STEPS,
            args.cap,
            False,
        ),
        "mcumax_nbk_bishop": scale_board_to_cap(
            table_64_to_0x88(
                [scale_pesto(v, args.pawn_unit) for v in pesto["mg"]["bishop"]]
            ),
            BISHOP_STEPS,
            args.cap,
            True,
        ),
        "mcumax_nbk_king_mg": scale_board_to_cap(
            table_64_to_0x88(
                [scale_pesto(v, args.pawn_unit) for v in pesto["mg"]["king"]]
            ),
            KING_STEPS,
            args.cap,
            False,
        ),
        "mcumax_nbk_king_eg": scale_board_to_cap(
            table_64_to_0x88(
                [scale_pesto(v, args.pawn_unit) for v in pesto["eg"]["king"]]
            ),
            KING_STEPS,
            args.cap,
            False,
        ),
    }

    for name, board in planes.items():
        if "knight" in name:
            steps, slide = KNIGHT_STEPS, False
        elif "bishop" in name:
            steps, slide = BISHOP_STEPS, True
        else:
            steps, slide = KING_STEPS, False
        peak = max_one_move_delta(board, steps, slide)
        if peak > args.cap:
            raise SystemExit(f"{name} cap check failed: peak {peak}")

    chunks = [
        "/* Auto-generated by tools/gen_nbk_pst.py — do not edit. */",
        "/* Knight/bishop/king MG/EG (king only) PST in engine units, flash-only.",
        "   Shapes derived from PeSTO (tools/pesto_source.py, MIT) then scaled to",
        f"   one-move |delta| <= {args.cap}. PeSTO rows are a8=0 and are not",
        "   flipped or mirrored here. Dense index ((sq>>4)<<3)|(sq&7) is",
        "   that index (mcu-max rank nibble 0 = rank 8). Black uses",
        "   square ^ 0x70 at lookup. */",
        "",
        f"#define MCUMAX_NBK_PST_DELTA_CAP {args.cap}",
        "",
    ]
    for name, board in planes.items():
        chunks.append(fmt_plane(name, board))
        chunks.append("")

    OUT.write_text("\n".join(chunks) + "\n")
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
