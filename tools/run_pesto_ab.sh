#!/usr/bin/env bash
# Regenerate PeSTO tables, rebuild PST CLI, run paired Elo vs main.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PAWN_UNIT="${1:-74}"
GAMES="${2:-200}"
SKILL="${3:-0}"
NODES="${4:-200000}"
DEPTH="${5:-4}"
SF="${SF:-/tmp/stockfish/stockfish-ubuntu-x86-64-avx2}"

python3 "${ROOT}/tools/gen_pesto_pst.py" --pawn-unit "$PAWN_UNIT"
cmake --build "${ROOT}/apps/cli/build" -j"$(nproc)" --target chess-cli

export PYTHONUNBUFFERED=1
python3 "${ROOT}/tools/elo_compare_engines.py" \
  --main-cli "${ROOT}/apps/cli/build-main/chess-cli" \
  --pst-cli "${ROOT}/apps/cli/build/chess-cli" \
  --stockfish "$SF" \
  --games "$GAMES" \
  --skill "$SKILL" \
  --nodes "$NODES" \
  --depth "$DEPTH"
