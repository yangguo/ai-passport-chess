# Host strength retest vs Stockfish 17.1 (2026-10-12)

Baseline: `main` @ `8e72ee4` (includes opening book, FEN/castling fix, 4K TT gate, PeSTO/orientation fixes, book busy-flag fix #19).

Harness: `tools/elo_match.py` with `--movetime` aligned to firmware wall clocks (100 / 1500 / 5000 ms). Easy uses the same 15% random non-best legal policy as the device. Stockfish: skill as noted, 0.2 s/move, 1 thread, 16 MiB hash.

**Caveats:** Host CPU is faster than ESP32-C3; these numbers are **upper bounds** on device strength. Skill→Elo uses Stockfish’s internal curve; treat absolute Elo as uncertain by about **±100–150**. Not Chess.com, FIDE, or human ratings.

## Per-setting results

| Setting | SF skill | n | W/D/L | Score | Implied Elo vs skill 95% CI |
|---|---:|---:|---|---:|---|
| Easy 100 ms | 0 | 150 | 61/36/53 | 52.7% | 1365 [1317, 1415] |
| Easy 100 ms | 1 | 100 | 30/16/54 | 38.0% | 1359 [1291, 1421] |
| Normal 1500 ms | 5 | 150 | 74/46/30 | 64.7% | 2302 [2256, 2352] |
| Normal 1500 ms | 6 | 100 | 37/34/29 | 54.0% | 2410 [2355, 2467] |
| Hard 5000 ms | 5 | 120 | 75/30/15 | 75.0% | 2388 [2333, 2452] |
| Hard 5000 ms | 7 | 120 | 31/52/37 | 47.5% | 2501 [2453, 2548] |

Compact JSON: [host-elo-2026-10-12.json](../benchmarks/host-elo-2026-10-12.json).

## SPRT: PeSTO-fix vs previous `main` (PST round 2)

Direct head-to-head via `tools/elo_compare_engines.py` (paired colours per opening):

| Metric | Value |
|---|---|
| Pairs completed | 108 |
| Pentanomial (LL/LD/DD-WL/WD/WW) | 0 / 10 / 65 / 30 / 3 |
| Score | 56.0% |
| Elo delta | +42 [+21, +64] |
| LLR | 2.97 (bounds ±2.94; H0 0 / H1 +10 Elo; α=β=5%) |
| Budget | 200 ms/move |
| Openings | Lichess 1378-position set |
| Verdict | **H1 accepted** |

Supersedes the 2026-10-08 host table (~1290 / 1625 / 1670 / 1755 on node proxies); keep that batch as history only ([host-elo-2026-10-08.json](../benchmarks/host-elo-2026-10-08.json)).
