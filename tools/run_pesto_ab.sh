#!/usr/bin/env bash
# Paired Elo vs main using the committed PeSTO tables (pawn-unit 52).
# Does not rewrite mcumax_pesto_tables.h unless --regen is passed.
#
#   tools/run_pesto_ab.sh [games [skill [nodes [depth]]]]
#   tools/run_pesto_ab.sh --regen [pawn-unit] [games [skill [nodes [depth]]]]
#
# --regen defaults to pawn-unit 52, matching the committed header.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REGEN=0
PAWN_UNIT=52
if [[ "${1:-}" == "--regen" ]]; then
  REGEN=1
  shift
  if [[ "${1:-}" =~ ^[0-9]+$ ]]; then
    PAWN_UNIT="$1"
    shift
  fi
fi
GAMES="${1:-200}"
SKILL="${2:-0}"
NODES="${3:-200000}"
DEPTH="${4:-4}"
SF="${SF:-/tmp/stockfish/stockfish-ubuntu-x86-64-avx2}"

if [[ "$REGEN" == 1 ]]; then
  echo "regenerating PeSTO tables at pawn-unit ${PAWN_UNIT}"
  python3 "${ROOT}/tools/gen_pesto_pst.py" --pawn-unit "$PAWN_UNIT"
else
  echo "using committed PeSTO tables (pawn-unit 52); not regenerating"
fi

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
