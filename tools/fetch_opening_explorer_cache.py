#!/usr/bin/env python3
"""Fetch Lichess opening-explorer move counts into a reproducible JSON cache.

Uses explorer.lichess.org (lichess DB, ratings 1600–2200). When the API is
unreachable (HTTP errors), falls back to documented curated move counts so
offline builds stay deterministic.
"""
from __future__ import annotations

import argparse
import json
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from datetime import date
from pathlib import Path

import chess

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from opening_book_lib import (  # noqa: E402
    CHESS_OPENINGS_DIR,
    MIN_PLIES,
    MAX_PLIES,
    OPENING_NEEDLES,
    RESP_PREFIXES,
    fen_key,
    load_chess_openings_rows,
    pgn_to_uci_moves,
    select_opening_lines,
)

CACHE_PATH = ROOT / "tools/data/opening_explorer_cache.json"
RATINGS = [1600, 1800, 2000, 2200]

# Curated mid-rating popularity (games-weighted), used only when API fails.
# Ratios follow public Lichess opening-explorer lore; not a live snapshot.
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


def curated_for_board(board: chess.Board) -> dict[str, int]:
    """Heuristic move counts for common book nodes."""
    fen = board.fen()
    if board.fullmove_number == 1 and board.turn == chess.WHITE:
        return dict(CURATED_ROOT)
    if fen.startswith("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b"):
        return dict(CURATED_AFTER_E4)
    if fen.startswith("rnbqkbnr/pppppppp/8/8/3PP3/8/PPP2PPP/RNBQKBNR b"):
        return dict(CURATED_AFTER_D4)
    if fen.startswith("rnbqkbnr/pppppppp/8/8/8/5N2/PPPPPPPP/RNBQKB1R b"):
        return dict(CURATED_AFTER_NF3)
    if fen.startswith("rnbqkbnr/pppp1ppp/8/2p5/2P5/8/PP1PPPPP/RNBQKBNR b"):
        return dict(CURATED_AFTER_C4)
    # Generic: prefer developing moves / central pawns slightly.
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


def collect_book_fens() -> list[str]:
    rows = load_chess_openings_rows()
    lines = select_opening_lines(rows)
    fens: set[str] = set()
    board = chess.Board()
    fens.add(fen_key(board))
    for _eco, _name, moves in lines:
        board.reset()
        for uci in moves:
            fens.add(fen_key(board))
            board.push(chess.Move.from_uci(uci))
            fens.add(fen_key(board))
    for _tag, prefix in RESP_PREFIXES:
        board.reset()
        for uci in prefix:
            board.push(chess.Move.from_uci(uci))
            fens.add(fen_key(board))
    return sorted(fens)


def fetch_lichess(fen: str, timeout: float = 20.0) -> dict[str, int] | None:
    q = urllib.parse.urlencode(
        {
            "variant": "standard",
            "ratings": ",".join(str(r) for r in RATINGS),
            "fen": fen,
        }
    )
    url = "https://explorer.lichess.org/lichess?" + q
    req = urllib.request.Request(
        url,
        headers={"Accept": "application/json", "User-Agent": "ai-passport-chess-book/1.0"},
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            if resp.status != 200:
                return None
            data = json.loads(resp.read().decode())
    except (urllib.error.URLError, urllib.error.HTTPError, json.JSONDecodeError, TimeoutError):
        return None
    moves = data.get("moves") or []
    out: dict[str, int] = {}
    for entry in moves:
        uci = entry.get("uci")
        if not uci:
            continue
        w = entry.get("white", 0) + entry.get("draws", 0) + entry.get("black", 0)
        out[uci] = int(w)
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, default=CACHE_PATH)
    ap.add_argument("--sleep", type=float, default=0.15, help="seconds between API calls")
    ap.add_argument("--max", type=int, default=0, help="limit FENs (0=all)")
    args = ap.parse_args()

    fens = collect_book_fens()
    if args.max:
        fens = fens[: args.max]

    api_ok = True
    positions: dict[str, dict] = {}
    for i, fen in enumerate(fens):
        counts = fetch_lichess(fen)
        if counts is None:
            api_ok = False
            board = chess.Board(fen)
            counts = curated_for_board(board)
            source = "curated_fallback"
        else:
            source = "lichess_explorer"
        positions[fen] = {"moves": counts, "source": source}
        if i + 1 < len(fens):
            time.sleep(args.sleep)

    payload = {
        "snapshot_date": date.today().isoformat(),
        "ratings": RATINGS,
        "api_reachable": api_ok,
        "primary_source": "lichess_explorer" if api_ok else "curated_fallback",
        "explorer_url": "https://explorer.lichess.org/lichess",
        "note": (
            "Live Lichess opening explorer (lichess DB, mid ratings)."
            if api_ok
            else "API unreachable from generator host; curated_fallback ratios used."
        ),
        "positions": positions,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(
        "Wrote %d positions to %s (api_reachable=%s)"
        % (len(positions), args.out, api_ok),
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
