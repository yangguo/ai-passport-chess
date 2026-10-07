#!/usr/bin/env python3
"""Perft gate over tests/perft/cases.json (Task 4, step 3).

Calls the C perft CLI per case/depth and compares exact leaf counts.
On mismatch, re-runs with --divide so the offending root move is visible.
The C core never parses JSON; this script is host-only glue.

Usage: check_perft_cases.py --perft <cli> --cases <json>
"""
import argparse
import json
import subprocess
import sys


def run_perft(cli, fen, depth, divide=False):
    cmd = [cli, "--fen", fen, "--depth", str(depth)]
    if divide:
        cmd.append("--divide")
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    if proc.returncode != 0:
        raise RuntimeError(
            "perft failed (rc=%d): %s" % (proc.returncode, proc.stderr.strip())
        )
    return proc.stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--perft", required=True)
    parser.add_argument("--cases", required=True)
    args = parser.parse_args()

    with open(args.cases, encoding="utf-8") as handle:
        data = json.load(handle)
    cases = data["cases"]

    failures = 0
    for case in cases:
        case_id = case["id"]
        fen = case["fen"]
        max_depth = case["pr_depth"]
        for depth in range(1, max_depth + 1):
            want = case["nodes"][str(depth)]
            try:
                got = int(run_perft(args.perft, fen, depth).strip())
            except RuntimeError as exc:
                print("FAIL %s d%d: %s" % (case_id, depth, exc))
                failures += 1
                continue
            if got != want:
                failures += 1
                print("FAIL %s d%d: got %d, want %d" % (case_id, depth, got, want))
                print("--- divide (root move subtotals) ---")
                print(run_perft(args.perft, fen, depth, divide=True).strip())
            else:
                print("ok %s d%d = %d" % (case_id, depth, got))

    if failures:
        print("FAILURES: %d" % failures)
        return 1
    print("PASS: all perft cases match")
    return 0


if __name__ == "__main__":
    sys.exit(main())
