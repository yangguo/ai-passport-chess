#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOG="$ROOT/artifacts/bench"
mkdir -p "$LOG"
CLI_TT="$ROOT/build/cli/chess-cli"
CLI_NOTT="$ROOT/build/cli-nott/chess-cli"
FEN="rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
MID="r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3"

bench() {
  local tag="$1" cli="$2" nodes="$3" depth="$4" fen="$5"
  python3 "$ROOT/tools/bench_mcumax_tt.py" \
    --cli "$cli" --label "$tag" --fen "$fen" \
    --nodes "$nodes" --depth "$depth" --runs 3 \
    | tee -a "$LOG/tt-bench.log"
}

for fen in "$FEN" "$MID"; do
  for nodes depth in "200000 4" "1000000 8"; do
    bench "nott-${nodes}-d${depth}" "$CLI_NOTT" "$nodes" "$depth" "$fen"
    bench "tt-${nodes}-d${depth}" "$CLI_TT" "$nodes" "$depth" "$fen"
  done
done
echo "bench log: $LOG/tt-bench.log"
