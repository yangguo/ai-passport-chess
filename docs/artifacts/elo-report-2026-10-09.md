# PR #11 transposition table: A/B Elo vs Stockfish 17.1 (box run, 2026-10-09 21:58 SGT)

Branch `cursor/mcu-max-transposition-table-e483` @ 2ecee8a, worktree /workspace/tt-wt (contains main 3f93fa2, same opening book).
Stockfish 17.1 (/usr/games/stockfish) for every arm, 0.2 s/move, harness tools/elo_match.py (alternating colours; local copy
elo_match_local.py only adds `--start N` for resume and a 600 s engine timeout, game logic unchanged). Nothing pushed.

## Builds
- **tt**: branch, `MCUMAX_HASH_BITS=10` (1024 entries, ~12 KiB), i.e. build/cli as in tools/run_tt_elo_suite.sh.
- **broken-nott**: branch, `MCUMAX_HASH_BITS=0` (build/cli-nott as in the suite script). **Unusable**: the branch stopped setting
  MCUMAX_PIECE_MOVED in mcumax_set_piece and only re-derives moved flags inside `#ifdef MCUMAX_HASHING_ENABLED`, so in a no-TT build
  every FEN-loaded piece is "virgin"; the engine proposes illegal moves and the adapter returns engine-failed. 8 of 10 vs-skill-3 games
  aborted; arms killed after a few minutes, logs in broken-nott/.
- **fixnott** (correct no-TT baseline for TT − no-TT): branch at `MCUMAX_HASH_BITS=0` + one-hunk patch (fixnott.patch) that runs the
  same moved-flag-from-layout pass in the #else branch that the TT build already runs. Identical code path to tt except the table.
- **main**: origin/main 3f93fa2 CLI (hashing off, FEN reload every move): the merge-target baseline, added after broken-nott failed.
- **diag-tt12 / diag-tt16**: branch with `MCUMAX_HASH_BITS=12` (4096 entries, ~48 KiB) / `16` (65536, ~768 KiB). Diagnostic, 40 games.

## Results (scores exclude aborted games, as elo_match does; 95% CI = delta method on per-game score)
| arm | games played | scored | W/D/L | score | aborts | sec/game mean / median | status |
|---|---|---|---|---|---|---|---|
| diag-tt12-s0-200k | 40 | 39 | +34 =4 -1 | 36.0/39 (92.3%) | 1 | 7.1 / 6.7 | complete |
| diag-tt12-s3-1M | 40 | 40 | +27 =9 -4 | 31.5/40 (78.8%) | 0 | 35.5 / 35.1 | complete |
| diag-tt16-s0-200k | 40 | 40 | +34 =5 -1 | 36.5/40 (91.2%) | 0 | 6.8 / 7.1 | complete |
| diag-tt16-s3-1M | 40 | 40 | +32 =6 -2 | 35.0/40 (87.5%) | 0 | 32.9 / 32.2 | complete |
| fixnott-s0-200k | 200 | 199 | +168 =23 -8 | 179.5/199 (90.2%) | 1 | 9.9 / 9.6 | complete |
| fixnott-s3-1M | 100 | 100 | +66 =23 -11 | 77.5/100 (77.5%) | 0 | 37.0 / 36.0 | complete |
| fixnott-s3-200k | 200 | 196 | +92 =52 -52 | 118.0/196 (60.2%) | 4 | 13.5 / 12.3 | complete |
| main-s0-200k | 200 | 200 | +149 =37 -14 | 167.5/200 (83.8%) | 0 | 12.5 / 11.2 | complete |
| main-s3-1M | 100 | 100 | +38 =26 -36 | 51.0/100 (51.0%) | 0 | 43.0 / 41.6 | complete |
| main-s3-200k | 200 | 200 | +49 =51 -100 | 74.5/200 (37.2%) | 0 | 15.5 / 14.2 | complete |
| tt-s0-200k | 200 | 195 | +77 =31 -87 | 92.5/195 (47.4%) | 5 | 11.2 / 10.0 | complete |
| tt-s3-1M | 77 | 75 | +4 =8 -63 | 8.0/75 (10.7%) | 2 | 98.7 / 96.7 | PARTIAL (running) |
| tt-s3-200k | 200 | 196 | +23 =24 -149 | 35.0/196 (17.9%) | 4 | 11.7 / 10.4 | complete |

| setting | TT − fixnott (correct no-TT) | same, aborts=losses | TT − main | fixnott − main |
|---|---|---|---|---|
| s0-200k | -403 [-484, -323] | -403 [-482, -324] | -303 [-372, -233] | +101 [+16, +186] |
| s3-200k | -337 [-408, -266] | -333 [-403, -262] | -175 [-245, -104] | +163 [+103, +222] |
| s3-1M | -584 [-711, -457] | -589 [-716, -462] | -376 [-499, -253] | +208 [+119, +297] |

| diag (TT size) | vs fixnott | vs main |
|---|---|---|
| diag-tt12-s0-200k | +46 [-131, +223] | +147 [-25, +319] |
| diag-tt12-s3-1M | +13 [-114, +139] | +221 [+98, +343] |
| diag-tt16-s0-200k | +22 [-141, +185] | +122 [-35, +280] |
| diag-tt16-s3-1M | +123 [-25, +271] | +331 [+187, +475] |

Aborts: every abort (tt 11, fixnott 5, diag-tt12 1) is the same bug: K and R on home squares without that castling right
(e.g. king walked back) -> after a FEN load the layout pass marks them virgin, engine plays an illegal castle -> engine-failed.
main never aborts. Counting aborts as losses barely changes the Elo numbers (column 3).

Infra: the box stalled several times, which caused Stockfish TimeoutErrors in the stock harness; affected arms resumed with --start and
parts combined (*.partN.log). Mean sec/game includes stall gaps, so use the median.

## Verdict
Acceptance (>= +30 Elo in one setting, none significantly negative): **NOT MET**. TT(1024) is significantly negative in every setting
(-340 to -590 Elo vs fixnott; -175 to -380 vs main). tt-s3-1M is partial (see table), but the result cannot flip.
Diagnostics point to table size: 4096/65536 entries are at or above fixnott (CIs include 0, 40 games) instead of -400.
Separately, fixnott itself is +100..+210 Elo over main (castling-aware incremental play), so that part of the branch is a real gain.
