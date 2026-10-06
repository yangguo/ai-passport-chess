#!/bin/sh
# Host test entry for chess_core (Task 2): configure, build, run CTest.
# Sanitizers on by default; pass CHESS_SANITIZERS=OFF to cmake manually
# for a plain build.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cmake -S "$ROOT/tests/host" -B "$ROOT/build/host" -DCHESS_SANITIZERS=ON
cmake --build "$ROOT/build/host"
ctest --test-dir "$ROOT/build/host" --output-on-failure
