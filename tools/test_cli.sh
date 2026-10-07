#!/bin/sh
# Host CLI entry: configure, build, run scripted behavior tests.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cmake -S "$ROOT/apps/cli" -B "$ROOT/build/cli" -DCHESS_SANITIZERS=ON
cmake --build "$ROOT/build/cli"
ctest --test-dir "$ROOT/build/cli" --output-on-failure
