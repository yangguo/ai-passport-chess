#!/usr/bin/env bash
# Compare search nodes and completed root iteration depth: main vs PeSTO mcu-max.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MAIN_SRC="/tmp/mcu-max-main-bench.c"
cp "${ROOT}/third_party/mcu-max/mcu-max.c.mainonly" "$MAIN_SRC"
PST_SRC="${ROOT}/third_party/mcu-max/mcu-max.c"
BENCH_SRC="${ROOT}/tools/bench_mcumax_stats.c"
INC="${ROOT}/third_party/mcu-max"
NODES="${1:-200000}"
DEPTH="${2:-4}"

build_bench() {
  local src="$1"
  local out="$2"
  gcc -O2 -I"$INC" "$BENCH_SRC" "$src" -o "$out"
}

build_bench "$MAIN_SRC" /tmp/bench_mcumax_main
build_bench "$PST_SRC" /tmp/bench_mcumax_pst

echo "=== main (micro-Max centre weights) ==="
/tmp/bench_mcumax_main "$NODES" "$DEPTH"
echo "=== pst (PeSTO tapered) ==="
/tmp/bench_mcumax_pst "$NODES" "$DEPTH"
