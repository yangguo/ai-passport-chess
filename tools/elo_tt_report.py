#!/usr/bin/env python3
"""Parse elo_match logs and print score + Elo diff + approximate 95% CI."""
import argparse
import math
import re
import sys


def parse_log(path):
    wins = draws = losses = 0
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = re.match(r"game \d+ \([^)]+\): (.+)", line.strip())
            if not m:
                continue
            tag = m.group(1)
            if tag == "1-0 ours":
                wins += 1
            elif tag.startswith("1/2"):
                draws += 1
            elif tag == "0-1 opp":
                losses += 1
    n = wins + draws + losses
    if n == 0:
        return None
    score = (wins + 0.5 * draws) / n
    return {"n": n, "w": wins, "d": draws, "l": losses, "score": score}


def elo_diff(score):
    if score <= 0.0 or score >= 1.0:
        return float("nan")
    return -400.0 * math.log10(1.0 / score - 1.0)


def elo_ci(score, n, z=1.96):
    if n == 0 or score <= 0.0 or score >= 1.0:
        return float("nan"), float("nan")
    # Delta method on logistic Elo mapping.
    se_p = math.sqrt(score * (1.0 - score) / n)
    # dE/dp = 400 / (ln(10) * p * (1-p))
    denom = math.log(10) * score * (1.0 - score)
    se_elo = 400.0 * se_p / denom
    e = elo_diff(score)
    return e - z * se_elo, e + z * se_elo


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--label", default="")
    args = ap.parse_args()
    stats = parse_log(args.log)
    if not stats:
        print("no games in %s" % args.log, file=sys.stderr)
        return 1
    e = elo_diff(stats["score"])
    lo, hi = elo_ci(stats["score"], stats["n"])
    label = args.label or args.log
    print(
        "%s: n=%d W/D/L=%d/%d/%d score=%.1f%% elo=%+.0f [%.0f, %.0f]"
        % (
            label,
            stats["n"],
            stats["w"],
            stats["d"],
            stats["l"],
            100.0 * stats["score"],
            e,
            lo,
            hi,
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
