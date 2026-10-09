#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SF="${STOCKFISH:-/tmp/stockfish/stockfish-ubuntu-x86-64-avx2}"
LOG="$ROOT/artifacts/elo"
mkdir -p "$LOG"
CLI_NOTT="$ROOT/build/cli-nott/chess-cli"
CLI_TT="$ROOT/build/cli/chess-cli"
run() {
  local tag="$1" cli="$2" games="$3" skill="$4" nodes="$5" depth="$6"
  echo "=== $tag ==="
  python3 "$ROOT/tools/elo_match.py" \
    --cli "$cli" --stockfish "$SF" \
    --games "$games" --skill "$skill" \
    --nodes "$nodes" --depth "$depth" \
    --pgn "$LOG/${tag}.pgn" | tee "$LOG/${tag}.log"
}
run nott-s0-200k-d4 "$CLI_NOTT" 200 0 200000 4
run tt-s0-200k-d4 "$CLI_TT" 200 0 200000 4
run nott-s3-200k-d4 "$CLI_NOTT" 100 3 200000 4
run tt-s3-200k-d4 "$CLI_TT" 100 3 200000 4
run nott-s3-1M-d8 "$CLI_NOTT" 100 3 1000000 8
run tt-s3-1M-d8 "$CLI_TT" 100 3 1000000 8
echo "suite done"
