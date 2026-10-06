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

## Alternative evaluated 2026-10-07: ripred/MicroChess — keep for firmware

https://github.com/ripred/MicroChess (MIT + LICENSE file).

| axis | umax (host choice) | MicroChess (firmware candidate) |
|---|---|---|
| license | public-domain dedication, no file | MIT + LICENSE ✅ |
| footprint | ~192 MiB hash (host fine, firmware impossible) | <2 KiB RAM ✅✅ |
| promotion | queen-only | queen-only (`last_was_pawn_promotion … to a Queen`) — tie |
| language/port | C89, dependency-free | C++ with hard `#include "Arduino.h"` (PROGMEM) — needs a shim for ESP-IDF |
| host status | LIVE green via black-box driver | one `unit_test_001.cpp`; no arduino toolchain here; protocol unknown |
| strength | club level | casual 6-ply (fits "no strength gate") |

Decision: host stays on umax (verified live 2026-10-07, 7/7 CLI green
incl. `cli_ai_live`). Spike the MicroChess adapter when firmware work
(Task 1/8) starts — its RAM budget and MIT license directly answer
umax's two firmware blockers, and the Arduino shim + on-device
measurement belong to that phase anyway.
