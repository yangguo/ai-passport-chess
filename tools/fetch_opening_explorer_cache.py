#!/usr/bin/env python3
"""Fetch Lichess opening-explorer move counts into a reproducible JSON cache.

Uses explorer.lichess.ovh/lichess (lichess DB, ratings 1600–2200). When the API
is unreachable (HTTP errors), falls back to documented curated move counts so
offline builds stay deterministic.
"""
from __future__ import annotations

import argparse
import json
import os
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
    RESP_PREFIXES,
    fen_key,
    load_chess_openings_rows,
    select_opening_lines,
)
from opening_explorer_fallback import (  # noqa: E402
    build_fallback_cache_payload,
    curated_for_board,
)

CACHE_PATH = ROOT / "tools/data/opening_explorer_cache.json"
RATINGS = [1600, 1800, 2000, 2200]
EXPLORER_URL = os.environ.get(
    "LICHESS_EXPLORER_URL", "https://explorer.lichess.ovh/lichess"
)


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


def fetch_lichess(fen: str, timeout: float = 20.0, retries: int = 5) -> dict[str, int] | None:
    q = urllib.parse.urlencode(
        {
            "variant": "standard",
            "ratings": ",".join(str(r) for r in RATINGS),
            "fen": fen,
        }
    )
    url = EXPLORER_URL + "?" + q
    headers = {"Accept": "application/json", "User-Agent": "ai-passport-chess-book/1.0"}
    token = os.environ.get("LICHESS_API_TOKEN")
    if token:
        headers["Authorization"] = "Bearer " + token
    data = None
    for _attempt in range(retries):
        req = urllib.request.Request(url, headers=headers)
        try:
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                if resp.status != 200:
                    print("explorer HTTP %d" % resp.status, file=sys.stderr)
                    return None
                data = json.loads(resp.read().decode())
            break
        except urllib.error.HTTPError as e:
            if e.code == 429:
                print("explorer HTTP 429; backing off 60s", file=sys.stderr)
                time.sleep(60)
                continue
            print("explorer HTTP %d" % e.code, file=sys.stderr)
            return None
        except (urllib.error.URLError, json.JSONDecodeError, TimeoutError) as e:
            print("explorer error: %s" % type(e).__name__, file=sys.stderr)
            time.sleep(5)
            continue
    else:
        return None
    if data is None:
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


def write_fallback_cache(out: Path) -> int:
    fens = collect_book_fens()
    payload = build_fallback_cache_payload(
        fens,
        snapshot_date=date.today().isoformat(),
        explorer_url=EXPLORER_URL,
    )
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(
        "Wrote %d fallback positions to %s" % (len(fens), out),
        file=sys.stderr,
    )
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, default=CACHE_PATH)
    ap.add_argument(
        "--sleep",
        type=float,
        default=1.1,
        help="seconds between API calls (>=1 req/s limit)",
    )
    ap.add_argument("--max", type=int, default=0, help="limit FENs (0=all)")
    ap.add_argument(
        "--force-fallback",
        action="store_true",
        help="skip API; write curated_fallback cache for all book FENs",
    )
    args = ap.parse_args()

    if args.force_fallback:
        return write_fallback_cache(args.out)

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
        "explorer_url": EXPLORER_URL,
        "note": (
            "Real Lichess opening explorer data (lichess DB, ratings 1600-2200), fetched %s."
            % date.today().isoformat()
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
