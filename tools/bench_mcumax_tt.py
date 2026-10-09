#!/usr/bin/env python3
"""Bench mcu-max via chess-cli: nodes, wall time, implied depth from output."""
import argparse
import subprocess
import time


def run_cli(cli, fen, nodes, depth):
    proc = subprocess.Popen(
        [cli],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )
    assert proc.stdin and proc.stdout
    proc.stdin.write("loadfen %s\n" % fen)
    proc.stdin.flush()
    t0 = time.perf_counter()
    proc.stdin.write("ai %d %d\n" % (nodes, depth))
    proc.stdin.flush()
    move = None
    for _ in range(500):
        line = proc.stdout.readline()
        if not line:
            break
        line = line.strip()
        if line.startswith("engine: "):
            move = line.split(" ", 1)[1]
            break
    dt = time.perf_counter() - t0
    proc.stdin.write("quit\n")
    proc.stdin.flush()
    proc.wait(timeout=5)
    return move, dt


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--cli", required=True)
    p.add_argument("--label", default="cli")
    p.add_argument(
        "--fen",
        default="rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    )
    p.add_argument("--nodes", type=int, default=200000)
    p.add_argument("--depth", type=int, default=4)
    p.add_argument("--runs", type=int, default=3)
    args = p.parse_args()
    times = []
    move = None
    for i in range(args.runs):
        m, dt = run_cli(args.cli, args.fen, args.nodes, args.depth)
        move = m
        times.append(dt)
        print("run %d: %.3fs move=%s" % (i + 1, dt, m))
    avg = sum(times) / len(times)
    print(
        "%s nodes=%d depth=%d avg=%.3fs nps~%.0f move=%s"
        % (args.label, args.nodes, args.depth, avg, args.nodes / avg, move)
    )


if __name__ == "__main__":
    main()
