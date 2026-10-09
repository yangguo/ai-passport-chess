# Transposition table validation (branch `cursor/mcu-max-transposition-table-e483`)

## Root causes fixed

1. **Zero hash after FEN** — `mcumax_set_fen_position` cleared `hash_key` /
   `hash_key2` while the adapter reloads FEN every search, so TT buckets mixed
   unrelated positions.
2. **FEN virgin flags** — loading pieces with `MCUMAX_PIECE_MOVED` on every
   square broke board bytes vs incremental play; flags are now inferred from the
   internal start layout after parse.
3. **Incremental hash path** — piece-sum “reseed” does not match micro-Max’s
   per-move `Hash()` / `Hash(8)` updates. Normal play keeps keys via
   `mcumax_play_move` (one-ply delta in `chess_ai`); cold FEN loads use replay
   hints or safe zero keys until the search rebuilds along the path.

## TT bounds / mate (audit)

Matches upstream micro-Max: TT cutoffs only on `MCUMAX_INTERNAL_NODE` with
`key2` match and bound flags in `square_from` (`0x8` / `0x80`); root always
restarts depth but keeps move hints. Mate scores use the delayed-loss bonus on
return (`iter_score += iter_score < score`), not separate TT ply storage.

## TT size diagnostic (`artifacts/bench/tt-size-probe.log`)

At 1M nodes / d8 on a midgame FEN, 1024 entries (bits=10) shows higher node
count and `replace_deeper` than 4096/16384 — consistent with thrashing at the
device default; default stays **10** (~12 KiB).

## Host tests (local)

| Check | Result |
|---|---|
| `ctest -R mcumax_hash` | PASS |
| `ctest` (host, sanitizers OFF) | PASS |
| Host sanitizers (`CHESS_SANITIZERS=ON`) | NOT RUN (ASan runtime missing in cloud VM) |
| Core perft (`ctest -R perft`) | PASS (unchanged; TT is engine-only) |
| `tools/compare_tt_moves.py` TT vs no-TT @ 200k/d4 | PASS (4 FENs) |

## Micro-bench (`artifacts/bench/tt-bench.log`)

Opening-book replies dominate start/mid FEN CLI timings (sub-ms). Use host
`mcumax_get_last_search_nodes()` tests for node parity; see `test_mcumax_hash.c`.

## Elo vs Stockfish 17.1 (`artifacts/elo/`)

Logs and PGNs from `tools/run_tt_elo_suite.sh`. Anchors are lore (skill 0≈800,
3≈1000); interpret as supportive only.

| Arm | Games | Skill | Budget | Status |
|---|---:|---:|---|---|
| no-TT vs SF | 200 | 0 | 200k/d4 | pending |
| TT vs SF | 200 | 0 | 200k/d4 | pending |
| no-TT vs SF | 100 | 3 | 200k/d4 | pending |
| TT vs SF | 100 | 3 | 200k/d4 | pending |
| no-TT vs SF | 100 | 3 | 1M/d8 | pending |
| TT vs SF | 100 | 3 | 1M/d8 | pending |

| Smoke TT, skill 3, 1M/d8, n=20 | 4.0/20 | −241 | [−59, −423] |
| Smoke no-TT, skill 3, 1M/d8, n=20 | 10.0/20 | −0 | [+148, −148] |
| Smoke TT, skill 0, 200k/d4, n=30 | 25.5/30 | +301 | [+469, +134] |
| Smoke no-TT, skill 0, 200k/d4, n=30 | 15.5/30 | +12 | [+133, −110] |

Full suite (`run_tt_elo_suite.sh`) running → `artifacts/elo/full-suite.log`.

## Firmware / CI

| Metric | Value |
|---|---|
| TT heap (default 1024 entries) | ~12 KiB |
| ESP-IDF firmware size / RAM | from CI `firmware` job (not re-flashed here) |

## Acceptance (+30 Elo, no significant regression)

Verdict recorded in the draft PR after Elo suite completes.
