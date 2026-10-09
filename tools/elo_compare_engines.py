#!/usr/bin/env python3
"""Paired A/B: main vs PST chess-cli vs the same Stockfish binary."""
from __future__ import annotations

import argparse
import math
import subprocess
import sys

try:
    import chess
    import chess.engine
except ImportError:
    print("python-chess required")
    sys.exit(1)

from elo_match import Cli, play_game  # noqa: E402


def elo_diff(p: float) -> float:
    if p <= 0.0 or p >= 1.0:
        return float("nan")
    return -400.0 * math.log10(1.0 / p - 1.0)


def elo_ci(p: float, n: int, z: float = 1.96) -> tuple[float, float]:
    se = math.sqrt(p * (1.0 - p) / n)
    lo = max(1e-9, p - z * se)
    hi = min(1.0 - 1e-9, p + z * se)
    return elo_diff(hi), elo_diff(lo)


def run_arm(
    label: str,
    cli_path: str,
    stockfish: str,
    games: int,
    skill: int,
    nodes: int,
    depth: int,
):
    budget = "ai %d %d" % (nodes, depth)
    engine = chess.engine.SimpleEngine.popen_uci(stockfish)
    score = 0.0
    played = 0
    aborted = 0
    try:
        engine.configure({"Skill Level": skill, "Threads": 1, "Hash": 16})
        for game in range(games):
            our_color = chess.WHITE if game % 2 == 0 else chess.BLACK
            cli = Cli(cli_path, budget)
            outcome = play_game(cli, engine, our_color, 200)
            if outcome[0] is None:
                cli.close()
                aborted += 1
                continue
            played += 1
            score += outcome[0]
            if (game + 1) % 10 == 0 or game + 1 == games:
                print(
                    "  %s game %d/%d score %.1f/%.0f"
                    % (label, game + 1, games, score, played),
                    flush=True,
                )
    finally:
        engine.quit()
    return score, played, aborted


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--main-cli", required=True)
    ap.add_argument("--pst-cli", required=True)
    ap.add_argument("--stockfish", required=True)
    ap.add_argument("--games", type=int, default=200)
    ap.add_argument("--skill", type=int, default=0)
    ap.add_argument("--nodes", type=int, default=200000)
    ap.add_argument("--depth", type=int, default=4)
    args = ap.parse_args()

    results = {}
    for label, path in ("main", args.main_cli), ("pst", args.pst_cli):
        print("=== %s ===" % label)
        score, played, aborted = run_arm(
            label,
            path,
            args.stockfish,
            args.games,
            args.skill,
            args.nodes,
            args.depth,
        )
        if aborted:
            print("aborted %d games" % aborted)
            return 1
        rate = score / played
        d = elo_diff(rate)
        lo, hi = elo_ci(rate, played)
        print("score %.1f/%.0f rate %.3f elo_vs_sf %+.0f CI [%.0f, %.0f]" % (
            score, played, rate, d, lo, hi
        ))
        results[label] = (rate, played, d, lo, hi)

    rp, _, dp, _, _ = results["pst"]
    rm, _, dm, _, _ = results["main"]
    diff = dp - dm
    # Delta of independent binomial rates (normal approx on Elo scale).
    se = math.sqrt(rp * (1 - rp) / results["pst"][1] + rm * (1 - rm) / results["main"][1])
    z = 1.96
    pst_lo = rp - z * se
    pst_hi = rp + z * se
    diff_lo = elo_diff(pst_hi) - dm
    diff_hi = elo_diff(pst_lo) - dm
    print("=== pst vs main (Elo vs SF) ===")
    print("elo_diff_pst_minus_main %+.0f CI [%.0f, %.0f]" % (diff, diff_lo, diff_hi))
    return 0


if __name__ == "__main__":
    sys.exit(main())
