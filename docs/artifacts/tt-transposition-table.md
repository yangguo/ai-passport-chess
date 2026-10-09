# Transposition table validation (branch `cursor/mcu-max-transposition-table-e483`)

## Root cause fixed

`mcumax_set_fen_position` reset `hash_key` / `hash_key2` to zero on every call.
The adapter loads a fresh FEN before each search, so after move one the board no
longer matched the all-zero Zobrist baseline. TT buckets then mixed unrelated
positions (weak play and inflated node counts). Reseed keys after FEN parse using
the micro-Max scramble sum relative to the calibrated internal start layout.

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
