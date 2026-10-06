# mcu-max engine provenance (M2 spike input)

- Upstream: https://github.com/Spitfire1900/umax (mirror of micro-Max 4.8
  by H.G. Muller; canonical background:
  https://www.chessprogramming.org/Micro-Max)
- Pinned commit: `e9d32309ae70e5903d99b0d8149a39533be095d9` (master)
- File: `umax4_8.c` (9357 bytes, verbatim, no local patches)
- License: public domain by author dedication (micro-Max was released by
  H.G. Muller into the public domain; the mirror carries no separate
  LICENSE file, so this note stands in for it — re-verify before any
  firmware release build)

## Verified properties (source read + design match)

- 0x88 mailbox board, global mutable state (NOT reentrant as-is)
- Negamax + quiescence + iterative deepening, ~16M-entry hash table
  (~192 MiB static) — MUST be disabled or shrunk for firmware
- "Full FIDE rules (expt under-promotion)": always promotes to queen;
  the core re-validates every engine move, so rules are never reduced
  to fit the engine (all four promotions stay core-supported)
- Console main loop only (prints board, reads coordinate moves);
  there is no setboard/API entry — host use is black-box subprocess
  with board-diff extraction, FEN re-sync guard, and core validation

## Firmware adapter risks (Task 9/10 must resolve, not assumed)

Stop/cancel semantics, callback frequency, stack depth, and the
generation check for stale results are unverified on-device. If the
adapter cannot meet them: patch locally (recorded here) or switch
engines — never ship an unverified AI path.

## Alternative under evaluation: ripred/MicroChess (MIT, <2K RAM)

https://github.com/ripred/MicroChess — MIT-licensed (cleaner than the
mirror above) and designed for embedded RAM budgets, which directly
answers the hash-table caveat. Not vendored yet: if the umax console
protocol proves awkward live, or firmware RAM forces the issue, spike
MicroChess next and record the verdict here.
