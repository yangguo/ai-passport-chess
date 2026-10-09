"""Curated explorer fallback keyed by python-chess positions (host-only)."""
from __future__ import annotations

import chess

from opening_book_lib import fen_key

# Mid-rating popularity ratios (not a live snapshot); used when API is down.
CURATED_ROOT = {
    "e2e4": 44_000_000,
    "d2d4": 34_000_000,
    "g1f3": 11_000_000,
    "c2c4": 9_000_000,
    "b1c3": 1_200_000,
    "f2f4": 800_000,
}

CURATED_AFTER_E4 = {
    "e7e5": 18_000_000,
    "c7c5": 14_000_000,
    "e7e6": 6_500_000,
    "c7c6": 4_000_000,
    "g8f6": 3_500_000,
    "d7d5": 2_800_000,
    "g7g6": 900_000,
}

CURATED_AFTER_D4 = {
    "d7d5": 12_000_000,
    "g8f6": 10_000_000,
    "e7e6": 4_000_000,
    "f7f5": 1_500_000,
    "c7c5": 1_200_000,
}

CURATED_AFTER_NF3 = {
    "d7d5": 8_000_000,
    "g8f6": 7_500_000,
    "c7c5": 2_000_000,
    "e7e6": 1_800_000,
}

CURATED_AFTER_C4 = {
    "e7e5": 5_500_000,
    "c7c5": 4_500_000,
    "e7e6": 3_000_000,
    "g8f6": 2_500_000,
    "c7c6": 1_500_000,
}

# (UCI prefix from start, move weights at that node)
CURATED_PREFIXES: list[tuple[list[str], dict[str, int]]] = [
    ([], CURATED_ROOT),
    (["e2e4"], CURATED_AFTER_E4),
    (["d2d4"], CURATED_AFTER_D4),
    (["g1f3"], CURATED_AFTER_NF3),
    (["c2c4"], CURATED_AFTER_C4),
]


def board_after(moves: list[str]) -> chess.Board:
    board = chess.Board()
    for uci in moves:
        board.push(chess.Move.from_uci(uci))
    return board


def curated_fen_table() -> dict[str, dict[str, int]]:
    """FEN string -> move counts for each curated book node."""
    table: dict[str, dict[str, int]] = {}
    for prefix, counts in CURATED_PREFIXES:
        table[fen_key(board_after(prefix))] = dict(counts)
    return table


CURATED_BY_FEN: dict[str, dict[str, int]] = curated_fen_table()


def curated_for_board(board: chess.Board) -> dict[str, int]:
    """Heuristic move counts for common book nodes."""
    hit = CURATED_BY_FEN.get(fen_key(board))
    if hit is not None:
        return dict(hit)
    out: dict[str, int] = {}
    for mv in board.legal_moves:
        uci = mv.uci()
        score = 1_000_000
        piece = board.piece_at(mv.from_square)
        if piece and piece.piece_type == chess.PAWN:
            score += 500_000
        if piece and piece.piece_type in (chess.KNIGHT, chess.BISHOP):
            score += 300_000
        out[uci] = score
    return out


def build_fallback_cache_payload(
    fens: list[str],
    *,
    snapshot_date: str,
    explorer_url: str,
) -> dict:
    positions: dict[str, dict] = {}
    for fen in fens:
        board = chess.Board(fen)
        positions[fen] = {
            "moves": curated_for_board(board),
            "source": "curated_fallback",
        }
    return {
        "snapshot_date": snapshot_date,
        "ratings": [1600, 1800, 2000, 2200],
        "api_reachable": False,
        "primary_source": "curated_fallback",
        "explorer_url": explorer_url,
        "note": "Synthetic cache: curated_fallback only (no API).",
        "positions": positions,
    }
