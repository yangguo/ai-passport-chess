#!/usr/bin/env python3
"""Host-only: build mcu-max with varied TT sizes and print probe stats."""
import os
import subprocess
import textwrap

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FEN = "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3"
PROBE_C = os.path.join(ROOT, "tools", "tt_probe_main.c")


def build_probe(bits):
    out = os.path.join(ROOT, "build", "tt-probe-%d" % bits)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    cmd = [
        "cc",
        "-std=c11",
        "-O2",
        "-I" + os.path.join(ROOT, "third_party", "mcu-max"),
        "-DMCUMAX_HASH_BITS=%d" % bits,
        PROBE_C,
        os.path.join(ROOT, "third_party", "mcu-max", "mcu-max.c"),
        os.path.join(ROOT, "third_party", "mcu-max", "mcumax_hash_scramble_table.c"),
        "-o",
        out,
    ]
    subprocess.check_call(cmd)
    return out


def main():
    for bits in (12, 14, 16):
        exe = build_probe(bits)
        out = subprocess.check_output([exe, FEN, "1000000", "8"], text=True)
        print("MCUMAX_HASH_BITS=%d\n%s" % (bits, out.strip()))


if __name__ == "__main__":
    main()
