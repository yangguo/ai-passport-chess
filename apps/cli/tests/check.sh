#!/bin/sh
# Scripted CLI behavior test: run <binary> with <script> on stdin,
# require every remaining argument to appear in the output.
# Usage: check.sh <binary> <script> <pattern>...
set -eu
bin=$1
script=$2
shift 2
out=$("$bin" < "$script")
failed=0
for pat in "$@"; do
  case "$out" in
    *"$pat"*) ;;
    *)
      printf 'missing pattern: %s\n--- output ---\n%s\n' "$pat" "$out"
      failed=1
      ;;
  esac
done
if [ "$failed" -eq 0 ]; then
  printf 'PASS %s\n' "$script"
fi
exit "$failed"
