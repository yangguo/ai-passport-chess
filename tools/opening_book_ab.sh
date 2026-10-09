#!/bin/sh
# A/B: main vs new opening book via elo_match (same Stockfish, alternating colors).
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SF=${STOCKFISH:-/tmp/stockfish/stockfish-ubuntu-x86-64-avx2}
MAIN_CLI=${MAIN_CLI:-$ROOT/build/cli-ab/chess-cli-main}
NEW_CLI=${NEW_CLI:-$ROOT/build/cli-ab/chess-cli-new}
GAMES=${GAMES:-100}
NODES=${NODES:-200000}
DEPTH=${DEPTH:-4}

for skill in 0 3; do
  echo "=== skill $skill: main book ==="
  python3 "$ROOT/tools/elo_match.py" --cli "$MAIN_CLI" --stockfish "$SF" \
    --games "$GAMES" --skill "$skill" --nodes "$NODES" --depth "$DEPTH" \
    | tee "/tmp/elo_skill${skill}_main.txt"
  echo "=== skill $skill: new book ==="
  python3 "$ROOT/tools/elo_match.py" --cli "$NEW_CLI" --stockfish "$SF" \
    --games "$GAMES" --skill "$skill" --nodes "$NODES" --depth "$DEPTH" \
    | tee "/tmp/elo_skill${skill}_new.txt"
done

python3 - "$GAMES" <<'PY'
import math, re, sys
games = int(sys.argv[1])

def parse_score(path):
    text = open(path).read()
    m = re.search(r"played (\d+), score ([0-9.]+)/", text)
    if not m:
        raise SystemExit("parse failed: %s" % path)
    n, s = int(m.group(1)), float(m.group(2))
    return s, n

def wilson(p, n, z=1.96):
    if n == 0:
        return 0.0, 0.0
    denom = 1 + z*z/n
    center = (p + z*z/(2*n)) / denom
    margin = z * math.sqrt(p*(1-p)/n + z*z/(4*n*n)) / denom
    return center - margin, center + margin

for skill in (0, 3):
    sm, nm = parse_score(f"/tmp/elo_skill{skill}_main.txt")
    sn, nn = parse_score(f"/tmp/elo_skill{skill}_new.txt")
    pm, pn = sm/nm, sn/nn
    diff = pn - pm
    se = math.sqrt(pm*(1-pm)/nm + pn*(1-pn)/nn)
    lo, hi = diff - 1.96*se, diff + 1.96*se
    print("skill %d: main %.1f/%.0f (%.1f%%) new %.1f/%.0f (%.1f%%) "
          "score-rate diff %+.3f 95%% CI [%+.3f, %+.3f]" % (
              skill, sm, nm, 100*pm, sn, nn, 100*pn, diff, lo, hi))
PY
