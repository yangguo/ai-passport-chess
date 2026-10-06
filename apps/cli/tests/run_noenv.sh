#!/bin/sh
# Run a behavior check with CHESS_ENGINE_BIN unset, so the no-engine
# gate never depends on the ambient environment.
# Usage: run_noenv.sh <binary> <script> <pattern>...
set -eu
unset CHESS_ENGINE_BIN
dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec sh "$dir/check.sh" "$@"
