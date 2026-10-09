# Transposition table validation (branch `cursor/mcu-max-transposition-table-e483`)

## Ship decision

| Component | Verdict |
|-----------|---------|
| Move-by-move `chess_ai` sync + FEN virgin/castling fixes | **Ship** (+100..+210 Elo vs main on box; see [elo-report-2026-10-09.md](elo-report-2026-10-09.md)) |
| TT @ 1024 entries | **Do not ship** (−337..−584 Elo vs correct no-TT) |
| TT @ 4096+ entries | **Optional** if heap allows (diagnostics level with fixnott; 40-game CIs include 0) |

## Box Elo vs Stockfish 17.1 (authoritative)

Full tables and methodology: **[elo-report-2026-10-09.md](elo-report-2026-10-09.md)** (local box run, not CI).

| setting | TT(1024) − fixnott | fixnott − main |
|---------|-------------------|----------------|
| s0-200k | −403 [−484, −323] | +101 [+16, +186] |
| s3-200k | −337 [−408, −266] | +163 [+103, +222] |
| s3-1M | −584 [−711, −457] | +208 [+119, +297] |

| diag TT size | vs fixnott (40 games) |
|--------------|------------------------|
| 4096 (bits=12) | +13..+46 Elo (CIs wide) |
| 65536 (bits=16) | +22..+123 Elo (CIs wide) |

## Bugs fixed on branch

1. **Moved flags without TT** — `mcumax_apply_moved_flags_from_layout()` runs after every FEN parse (not only when hashing is on).
2. **Castling rights** — home K/R virginity follows the FEN castling field; `-` marks K/R moved even on e1/h1 (fixes illegal `e1g1` aborts).
3. **Hash keys** — incremental `play_move` path in `chess_ai`; replay hint for tests; no 1024-entry default.

## TT sizing (compile-time + device)

| `MCUMAX_HASH_BITS` | Entries | Heap (approx.) |
|--------------------|---------|----------------|
| 0 | off | 0 |
| 12 | 4096 | ~48 KiB |
| 16 | 65536 | ~768 KiB |

- **Never use 10 (1024)** — thrashes at depth/node budgets used in play.
- **Firmware** (`components/chess_ai/CMakeLists.txt`): `MCUMAX_HASH_BITS=12`. `chess_ai_engine_init()` allocates only if allocation succeeds **and** internal free heap remains ≥ **32 KiB after** the table (`CHESS_AI_TT_MIN_FREE_HEAP`); otherwise `mcumax_hash_shutdown()` and search runs with TT off.
- **Host CLI default**: bits=12 (`apps/cli/CMakeLists.txt`); override with `-DMCUMAX_HASH_BITS=…`.

## Host tests

| Check | Result |
|-------|--------|
| `ctest -R mcumax_hash` | PASS (local, sanitizers OFF) |
| `ctest -R perft` | PASS |
| `test_castling_rights_lost_no_short_castle` | FEN from box abort report |
| CI host + sanitizers | see PR #11 |

## Acceptance (+30 Elo, no regression)

**NOT MET** for TT(1024). **MET** for engine integration fixes vs main. TT optional at 4096+ when heap policy allows.
