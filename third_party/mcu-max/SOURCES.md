# mcu-max engine provenance (M2) + host AI driver note

## Pinned engine: Gissio/mcu-max 1.0.6 (CORRECT per docs/sources.md)

- Upstream: https://github.com/Gissio/mcu-max
- Pinned commit: `aa03caffce50729566b5db6965bf735c31f33eea`
- Files: `mcu-max.h`, `LICENSE` unchanged; `mcu-max.c` includes the local
  completed-iteration deadline patch described below.
- License: MIT, (c) 2022-2025 Gissio — full text in `LICENSE`, keep it
  on every import or firmware release build

## Verified properties (header + source read)

- In-process C API (not a console loop): `mcumax_set_fen_position`,
  `mcumax_search_best_move(node_max, depth_max)`,
  `mcumax_search_valid_moves`, `mcumax_play_move`
- Cancellation path exists: `mcumax_set_callback` +
  `mcumax_stop_search` (callback fires periodically during search) —
  this is what the firmware worker (Task 10) builds its deadline on
- Local patch: `mcumax_init` clears interface fields (callback,
  valid-move buffer pointers, stop flag) on every `set_fen` so a
  search cannot leave stale globals for the next adapter call
- Transposition table: compile-time `MCUMAX_HASH_BITS` (0 = off; device
  builds use **12** → 4096 entries, **49152 bytes**). Each entry is 12 bytes
  and naturally aligned (`uint32` key, `int32` score, three `uint8` fields,
  one explicit pad byte). It is not packed. Never use 1024 entries
  (bits=10): host Elo showed large regressions. Upstream
  `MCUMAX_HASHING_ENABLED` without resizing pulled 2^24 entries — never
  use that on-device. Scramble keys live in `mcumax_hash_scramble_table.c`
  (Flash) as a byte table. The Zobrist step reads 4 bytes with `memcpy`
  (host) or explicit little-endian byte loads (RISC-V). A `uint32_t`
  dereference of that table is a misaligned load. Firmware allocates the
  49152-byte table with `heap_caps_aligned_alloc(8, …, INTERNAL|8BIT)` only
  when the largest internal block fits alloc+25%, ≥32 KiB total heap remains
  after alloc, and the largest remaining block is ≥16 KiB. Host `aligned_alloc`
  uses the same 8-byte alignment. The table is cleared on every
  `mcumax_set_fen_position` (`mcumax_init`). FEN load sets piece moved flags
  and castling virginity; hash keys restart at zero.
- Square code 0xRF with rank 0 = rank 8 (FLIPPED vs our a1=0):
  adapter converts both ways; FEN goes in verbatim
- "Compliant with FIDE laws (except for underpromotion)": replies
  that promote are always queens — the core re-validates every
  engine move, so rules are never reduced to fit the engine

## Correction 2026-10-07: umax4_8.c was a misidentification — REMOVED

`umax4_8.c` (Spitfire1900/umax mirror of micro-Max) was vendored on
the guess "mcu-max ≈ micro-Max" from the shared 0x88 trait, then
driven live once as a black-box subprocess to prove the CLI loop.
The design pins Gissio/mcu-max instead, whose real bounded-search
API made the subprocess obsolete: removed with the pty driver in
the adapter switch. Nothing ships from that episode except the
lesson (block-buffered stdout needs a pty).

## Alternative evaluated 2026-10-07: ripred/MicroChess — keep for firmware

https://github.com/ripred/MicroChess (MIT + LICENSE file).

| axis | mcu-max (choice) | MicroChess (firmware candidate) |
|---|---|---|
| license | MIT + LICENSE ✅ | MIT + LICENSE ✅ |
| footprint | hash off by default; base RAM small | <2 KiB RAM ✅✅ |
| promotion | queen-only | queen-only — tie |
| language/port | C, dependency-free, real API | C++ with hard `#include "Arduino.h"` — needs a shim |
| host status | adapter in progress | one unit test; no toolchain here |

Decision: host goes mcu-max (proper bounded-search + stop API, no
protocol parsing). Spike the MicroChess adapter only if mcu-max
fails verification on the firmware path.

## Firmware adapter risks (Task 9/10 must resolve, not assumed)

Callback frequency under time pressure, stack depth at depth_max on
ESP32-C3, worst-case nodes-per-deadline calibration, and the
generation check for stale results are unverified on-device. Never
ship an unverified AI path.

## Local patch 2026-10-07: retain completed search on deadline

The pinned engine previously returned an invalid move when its callback
stopped a search before the final replay phase. The adapter then discarded
any deadline result and the app used the first core-legal move.

The patch records the best move after each fully completed root iteration
(at internal depth >= 3), resets it for every search, and returns that
checkpoint on stop. Interrupted recursion unwinds after restoring the
board; partial iterations never replace the checkpoint. The adapter still
validates the result through core. User cancellation always discards it.
No hash table, allocation, public engine API, or depth-limit change.

Regression: real engine + deterministic clock at a deadline must retain
Rxe4 against an exposed queen; expiration before a completed iteration
still falls back, and user cancellation never plays a move.

## Local patch 2026-10-10: capped knight/bishop/king PST (PST round 2)

Adds four `int8` planes in flash (knight MG, bishop MG, king MG/EG) on top
of the existing micro-Max terms: pawn centre table + structure, castling
`+50`, king freeze `−20`, promotion/passers unchanged. Knight, bishop, and
king quiet moves use **centre delta + (nbk(to)−nbk(from))**; the cap applies
to the NBK component only.

| Item | Detail |
|---|---|
| Source shape | **PeSTO** PST values via `tools/pesto_source.py` (MIT, Karls-Sun/pesto.py) |
| Generator | `tools/gen_nbk_pst.py` → `mcumax_nbk_tables.h` (engine units, scaled to cap) |
| Cap | `MCUMAX_NBK_PST_DELTA_CAP` 32 engine units per quiet move (`|pst(to)−pst(from)|`) |
| King phase | Endgame king plane when `non_pawn_material > 30` (same gate as king freeze) |
| Black | Rank mirror `square ^ 0x70` at lookup |
| RAM | 0 (`.rodata` only) |

Host tests: `tests/host/test_pst_nbk.c` (`MCUMAX_EXPOSE_EVAL`).
