# Transposition table validation (branch `cursor/mcu-max-transposition-table-e483`)

## Ship decision

| Component | Verdict |
|-----------|---------|
| FEN reload with layout + castling virginity | **Ship** (+100..+210 Elo vs main on box; see [elo-report-2026-10-09.md](elo-report-2026-10-09.md)) |
| TT @ 1024 entries | **Do not ship** |
| TT @ 4096 (optional) | **Unproven gain** — author re-measuring TT vs no-TT vs main; 40-game diagnostics were level with fixnott (wide CIs). Only enable if remeasure shows benefit and heap policy passes. |

Main regressed because it marked every FEN piece moved, which disabled castling. This branch infers moved flags from layout and applies FEN castling rights to home K/R.

## Box Elo vs Stockfish 17.1 (authoritative)

**[elo-report-2026-10-09.md](elo-report-2026-10-09.md)** — fixnott vs main = **+100..+210 Elo** (FEN path only; not one-ply sync).

| setting | TT(1024) − fixnott | fixnott − main |
|---------|-------------------|----------------|
| s0-200k | −403 [−484, −323] | +101 [+16, +186] |
| s3-200k | −337 [−408, −266] | +163 [+103, +222] |
| s3-1M | −584 [−711, −457] | +208 [+119, +297] |

## Integration path

- **Device and CLI:** `chess_ai_suggest` / `chess_ai_run_job` always `mcumax_set_fen_position(fen)` (no incremental engine state between moves).
- **TT lifetime:** `mcumax_init()` inside every `set_fen` clears the table; TT only helps **within a single search** (iterative deepening / revisits in that call).

## TT sizing (compile-time + device)

| `MCUMAX_HASH_BITS` | Entries | Heap (approx.) |
|--------------------|---------|----------------|
| 0 | off | 0 |
| 12 | 4096 | ~48 KiB |

Firmware (`MCUMAX_HASH_BITS=12`). `chess_ai_engine_init()` on ESP32-C3:

1. Largest internal 8-bit free block ≥ table size **+ 25%**
2. After alloc, total internal free heap ≥ **32 KiB**
3. After alloc, largest internal free block ≥ **16 KiB**

Otherwise `mcumax_hash_shutdown()` — search runs with TT off. Host builds allocate when `MCUMAX_HASH_BITS > 0` without the ESP gate.

**Never use 1024 entries (bits=10).**

## Host tests

| Check | Result |
|-------|--------|
| `ctest -R 'mcumax_hash|ai_fen_reload'` | see CI |
| `test_castling_rights_lost_no_short_castle` | box abort FEN |
| `test_partial_castling_fen_reload_stable` | partial `k` rights |
| `test_ai_fen_reload` | KQkq, ep, promoted knight |
| TT warm vs cold | same position, second search without `set_fen` |

## Acceptance

**MET** for FEN moved/castling fix vs main. **NOT MET** for mandatory TT; optional 4096 pending author remeasure.
