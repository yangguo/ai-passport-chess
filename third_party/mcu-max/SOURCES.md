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

## Local patch 2026-10-10: PeSTO tapered PST (PST round 3)

Replaced the shared incremental centre weights (`board[square+8]`, now
left zero) with **PeSTO** middlegame/endgame piece-square tables (PST
component only; no PeSTO material duplicated — micro-Max still scores
captures via `37×piece`).

| Item | Detail |
|---|---|
| Source | Ronald Friederich **PeSTO** tables as published on the [Chess Programming Wiki](https://www.chessprogramming.org/PeSTO%27s_Evaluation_Function) and mirrored in `tools/pesto_source.py` (Karls-Sun/pesto.py, MIT) |
| Generator | `tools/gen_pesto_pst.py --pawn-unit 52` → `mcumax_pesto_tables.h` (`int8`, `round(cp×52/100)`). Rows are stored as published. The generator does not flip them. |
| Storage | 6 piece types × 64 × 2 (MG+EG) ≈ 768 bytes `.rodata` |
| Squares | mcu-max `0xRF` has rank nibble 0 on rank 8. PeSTO is a8=0, so the white index is `(R<<3)\|F`. White flip is 0. Black flip is 56 (`sq^56`). See `mcumax_pesto_square_index`. |
| Taper | `phase` 0–24 from non-pawn material (PeSTO `gamephaseInc`); `(mg×phase + eg×(24−phase))/24` |
| Frozen phase | `mcumax_start_search` freezes that blend for the whole tree. A capture or promotion does not retune unmoved pieces, so the incremental PST delta equals a full-board sum at the root phase. |
| Increment | Mover: `pst(piece now on to)−pst(piece that left from)`. Capture and en passant add the captured piece's PST (it left the opponent's side), using `capture_square` for the pawn. Castling adds the rook's `pst(to)−pst(from)` as well as the king's. Promotion removes the pawn and scores the piece actually placed (engine queen, or N/B/R when a host probe asks). |
| Also kept | micro-Max pawn structure `9×` (neighbors / undefended / king-cling), endgame pawn-push `non_pawn_material>>2`, castling `+50`, king freeze `−20`, promotion / passer material (`647−type`) |
| FEN castling | PR #11 virginity / moved flags unchanged |

A PST has no isolated or doubled pawn term, so the `9×` structure
expression is restored in full. The push term `(non_pawn_material>>2)`
is the other half of that original expression. It stays at the original
weight: search does not recompute `non_pawn_material` from the board, so
the term is 0 until a move is actually played. PeSTO's endgame pawn
ranks do reward advancement, and after played captures the push can
stack with them; that overlap is not large enough to delete or halve
the term without an Elo measurement. Castling `+50` is a flat bonus on
top of the king and rook square deltas (PeSTO does move those pieces).
King freeze `−20` is a flat middlegame penalty for moving the king, not
the king square table. Centre weights stay removed; PeSTO replaces them.

Host tests: `tests/host/test_pst.c` (`MCUMAX_EXPOSE_EVAL`). The
incremental check calls `mcumax_eval_probe_pst_delta`, which records the
PST delta inside `mcumax_search`, and compares it to a full-board sum
written in the test from the raw board bytes. `mcumax_eval_pst_score`
is a live side-to-move total at the position's own phase; it is not the
oracle for that check.
