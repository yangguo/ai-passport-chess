# PST round 2 — plan only

> Date: 2026-10-10. Status: **plan only**. This file does not change the engine, the adapter, or `tools/elo_match.py`.
> Baseline: `main` at `9386ded` (PR #11, FEN castling / moved flags, optional 4096-entry transposition table).
> Previous attempt: PR #9, branch `cursor/pst-piece-square-tables-3824` @ `1dae232`, based on `3399574` (before PR #11).
> Device nodes/s, stack, and heap for any new table: **NOT RUN**. No flash and no community publish.

**Goal.** Decide whether a piece-square table (PST) can raise mcu-max strength on the ESP32-C3, after the castling fix, without a fatter search that spends the node budget on worse moves.

**Non-goals.** No NNUE, no engine swap, no change to `chess_core` rules, no new search feature in the same patch as a new table. Four promotions stay in core. This plan does not authorize flashing or a release.

## Conclusion

PR #9's losses are not a verdict on "PST" in the abstract. They are a verdict on **large positional deltas inside micro-Max's incremental score**, measured on a binary that **could not castle**. The search spends that score as the futility key, the stand-pat score, the iterative-deepening best-move key, and the late-move-reduction (LMR) re-search trigger. A quiet-move swing of one or two pawns makes more moves survive futility and makes the previous iteration's best move wrong. Nodes per depth go up. At a fixed node cap the engine finishes less useful work, and the eval itself prefers the wrong quiet moves.

The first implementation step is a **remeasure**, not a new table: rebase PR #9's PeSTO variant onto `9386ded` and run the protocol below at the same pawn-unit (52) that produced −73 Elo. Castling `+50` and the king tables were inert in that match, because every FEN piece was marked moved.

If that remeasure stays negative, do not retune PeSTO at pawn-unit 52–74. The next code change is a **small** knight / bishop / king table whose one-move delta stays inside the span of today's center table (about 28 engine units, under half a pawn), **added on top of** the pawn-structure term, castling `+50`, and the king-freeze `−20`. Full PeSTO replacement, margin retunes, and Texel fitting come after that, each in its own PR, and only if the previous PR says why.

## What already failed

Numbers below are host games versus Stockfish 17.1. The handmade scale results are the outcome reported for the first two commits on PR #9 (`48531dd` scale 4, `8835912` scale 3). The PR body and `docs/ai-integration.md` on that branch tabulate the later PeSTO arms; they do not include a game log for the handmade matches. Treat the handmade Elo as the task record of that experiment. The PeSTO −73 is in the PR.

| Attempt | What changed in the score | Result at 200k / depth 4 vs skill 0 |
|---|---|---|
| Handmade per-piece `int8`, multiplier 4, existing pawn structure, castling `+50`, and king freeze kept | Largest entry is `+40`. A knight from the rim (`−10`) to that square is `50 × 4 = 200` engine units, about 2.7 pawns. Rooks and queens got a table for the first time. King middlegame / endgame switched on `non_pawn_material > 30`. | About **−95 Elo** |
| Same tables, multiplier 3 | Same shape, `50 × 3 = 150` (about 2 pawns). `40 × 3 = 120` fits in `int8`; `40 × 4 = 160` does not. The lookup widens to `int32` before the multiply, so the overflow is real only if a later store narrows to `int8`. | Reported **neutral** |
| PeSTO middlegame / endgame, pawn-unit 52, tapered, black `sq ^ 56` | Removed pawn-structure `9 × …`, the endgame pawn push, castling `+50`, and king freeze `−20`. Kept promotion / passer (`647 − type`). Rook castling scored as a rook PST delta. | **−73 Elo [−141, +12]**, 200 games. Pawn-unit 62 and 74 at 50 games were also negative (wide intervals). |

Paired node bench on that branch (`tools/bench_mcumax_pair.sh`, PeSTO pawn-unit 74 versus the then-current center weights):

| Budget | Arm | Average nodes | Average `iter_depth` | Nodes/s |
|---|---|---:|---:|---:|
| 200k / d4 | main | 18 899 | 7.0 | 4.11 M |
| 200k / d4 | PeSTO | 46 298 | 7.0 | 3.30 M |
| 1M / d8 | main | 7 593 002 | 10.0 | 3.91 M |
| 1M / d8 | PeSTO | 4 538 283 | 10.0 | 3.27 M |

At 200k both arms finished depth 7, and PeSTO used about **2.45×** the nodes (`46298 / 18899`). Nodes/s fell about 20% (the per-move phase scan and the divide by 24). The 1M pair does **not** repeat the 2.45× ratio, so a remeasure has to log nodes and completed depth. Do not quote a single multiplier as a property of PeSTO.

Those matches ran on `3399574`. On that tree `mcumax_set_piece` set `MCUMAX_PIECE_MOVED` on every piece written from the FEN (the bug PR #11 fixed). The engine treated every piece as having already moved, so it never castled. Castling `+50` never fired. A king PST never scored `e1 → g1`. Removing `+50` was a no-op in the games. The −73 mixes three things: the tables, the deleted pawn-structure and king-freeze terms, and a search that could not castle. It does not isolate any one of them.

PR #11 then landed on `9386ded`. [docs/artifacts/elo-report-2026-10-09.md](../artifacts/elo-report-2026-10-09.md) and [docs/artifacts/tt-transposition-table.md](../artifacts/tt-transposition-table.md) record the castling fix at about **+100 to +210 Elo** versus the old main, and they record a 1024-entry table as a large loss. The 4096-entry table is optional, heap-gated on device, and **unproven**. CLI and firmware now compile `MCUMAX_HASH_BITS=12` (`apps/cli/CMakeLists.txt`, `components/chess_ai/CMakeLists.txt`). Host allocates that table; the device turns it off if the 32 KiB free-heap / 16 KiB largest-block gate fails. A rebase that only "turns PeSTO back on" also turns the 4096-entry table on relative to PR #9. The protocol below separates those.

The opening book (PR #8 / #10) was already in the PR #9 matches (`book_seed == 0`). It still is. Book hits hide the old `1.c3` / `1…c6` symptom. PST value, if any, is in the moves after the book, including castling.

The earlier strength writeup (`docs/plans/2026-10-09-engine-strength-plan.md` on `cursor/engine-strength-plan-55dd` @ `a54598a`, not on this tree) already said: keep the pawn-structure, castling, and king-freeze terms; treat a full PeSTO import as later and license-sensitive; do not cite the TSCP +200 Elo anecdote as this engine's expectation. Round 1 did the opposite on the terms and lost. This plan follows that earlier constraint and adds the castling confound plus the margin analysis.

## 1. How the evaluation meets the search

mcu-max 1.0.6 is Gissio's C port of H.G. Muller's micro-Max 4.8. There is no separate static-eval function. `mcumax_search` (`third_party/mcu-max/mcu-max.c`) receives the side-to-move score and updates it while generating the move. Muller describes the center term as **transient**: it updates the tree score, and it is not taken back when the piece is captured (`https://home.hccnet.nl/h.g.muller/eval.html`). Captures therefore prefer "lose the piece on a good square." He treats that as acceptable at center-table scale. It is not acceptable when the positional swing exceeds a minor-piece difference.

### Score scale

Material is applied only when a capture is made, as `37 * piece_value` (`mcu-max.c` around the `capture_piece_value` assignment, values in `mcumax_capture_values`):

| Piece | Table value | ×37, engine units |
|---|---:|---:|
| Pawn | 2 | 74 |
| Knight | 7 | 259 |
| Bishop | 8 | 296 |
| Rook | 12 | 444 |
| Queen | 23 | 851 |
| King | −1 | negative, the search treats it as the game ending |

The running score starts at 0 (`mcumax_init`). Equal material is implicit. A positional term is a fraction of a pawn only if it is a fraction of 74.

The shared center table lives in the unused 0x88 squares, `board[square + 8] = (x-4)^2 + (y-4)*(y-3)` (init loop). Values run from 0 near the center to **28** in the corner. The move delta is `board[from+8] - board[to+8]`, and only for `scan_piece_type < 6` (pawn, knight, king, bishop). Rooks and queens get 0. One move changes the score by at most 28, about **0.4 pawn**.

Other terms on that same scale, all applied in the make path for `iter_depth` high enough to play the move:

| Term | Size | Role |
|---|---:|---|
| Castling | `+50` when the rook is placed | about 0.7 pawn. This is the whole bonus; the king has no extra term. |
| King freeze | `−20` when the king moves and `non_pawn_material <= 30` | about 0.3 pawn. Discourages walking the king. |
| Pawn structure | `9 *` (missing neighbor / off-board / king-cling), typically 0–18, plus `non_pawn_material >> 2` as an endgame push | under 0.3 pawn at the start. |
| Promotion | `647 - piece_type` added to the capture value, and the same addend wrapped onto the piece byte so the low bits become a queen | about 8.7 pawns. This is the material conversion. A PST must not replace it. |
| Passer | `2 * (piece & (to+0x10) & 0x20)` when the move is not a promotion | small. |

`non_pawn_material` starts at 0 and grows only in `MCUMAX_PLAY_MOVE`, by `capture_piece_value >> 7` (a knight adds 2, a rook 3, a queen 6). It does **not** follow captures inside the tree. The king-freeze gate and the null-move zugzwang gate (`> 35`) see the material captured in the played game, not the material on the node. A PeSTO-style phase has to be computed from the board. PR #9 did that by scanning all 64 squares on every made move. That scan is the avoidable part of the 20% nodes/s drop.

### Margins that read the same score

These are the search decisions a PST changes, because they compare or order the score in engine units. Ply reductions that do not look at the score are listed so a later "rescale the margins" PR does not retune the wrong constant.

| Mechanism | Where | What it compares |
|---|---|---|
| Window nudge ("delay bonus") | `alpha -= alpha < score` and `beta -= beta <= score` at entry | **1** engine unit. A one-unit tempo. Invisible next to a 100-point PST. |
| Delayed-loss bonus | `return iter_score += iter_score < score` | **1** unit the other way, so a side that is behind keeps the move. Muller moved this to the return so exchanges are not taxed (`https://home.hccnet.nl/h.g.muller/progress.html`). |
| Null move | Side flipped, then `search(-beta, 1-beta, -score, …, iter_depth-3)` | Reduction of **2** plies versus a normal `iter_depth-1` child. The window is **1** unit wide, sitting on the parent's beta. On a fail-high, and when `non_pawn_material <= 35`, the node's best score becomes the null score and the move loop cutoffs. The `> 35` gate is captured-material accounting, not an eval margin. Do not multiply it when the PST scale changes. Muller: `https://home.hccnet.nl/h.g.muller/null.html`. |
| Futility | Recursive call only if legal-move generation, or `step_depth > 2`, or `step_score > step_alpha`; otherwise the static `step_score` is the value. A hit above alpha re-searches one ply deeper. | Margin is **0**. `step_score` already includes the positional delta of this move. A larger PST pushes more quiet moves over alpha, so they are searched. Muller added this in 4.1 and measured about +40 Elo against 4.0 in self-play (`https://home.hccnet.nl/h.g.muller/futile.html`). That gain assumes the static score is a tight bound. A noisy PST spends the gain back. |
| LMR | `step_depth = iter_depth - 1 - (iter_depth > 5 && not a pawn && not a capture && not the previous best)` | One extra ply off. The same `while (score > alpha) deepen` loop re-searches. A noisier score fail-highs more often after the reduction, so the re-search runs. |
| Check extension | Under a narrow guard (`non_pawn_material <= 30`, null-move saw a check, depth ≥ 3, and the move is not a non-king capture), `step_depth = iter_depth` | No eval margin. Stack risk if it chains; out of scope here. |
| Horizon ordering | At `iter_depth == 1`, `step_score = capture_value - piece_type` | MVV/LVA in **material** units. The positional delta is not added. Captures still sort by victim at the last ply. |
| Stand-pat | If the null move does not fail high and `iter_depth == 2`, the best score starts as the incoming `score` | The positional stock in `score` moves the stand-pat cutoff. |
| Iterative deepening best move | The scan starts at `iter_square_from`. A move with a higher `step_score` replaces the best from/to. The next iteration tries that move first. | For a futility-pruned move, `step_score` **is** the static eval. The PST directly chooses the move that will be tried first at the next depth. |
| Root node cap | `mcumax_search_best_move` calls the search with `depth_max + 3`. The root loop keeps going while `node_count < node_max` and `iter_depth <= depth_max`. | A fatter tree finishes fewer iterations before the cap. The caller depths 4 and 8 are not "4 and 8 plies of proof." |
| Mate range | `MCUMAX_SCORE_MAX` is 8000 | About 108 pawns. A legal PST cannot approach it. |

Transposition-table scores are in the same units and are cleared on every `mcumax_set_fen_position` (`docs/artifacts/tt-transposition-table.md`). A PST does not poison a later search's table. Inside one search, a wilder score changes which moves the table stores. Measure PST with the table **on and off**, same bits on both arms. Never use 1024 entries (`MCUMAX_HASH_BITS=10`); that size lost hundreds of Elo in the PR #11 report.

### Why a PST raises node counts and loses at fixed nodes

Three mechanisms, all visible in the make path:

1. **Futility.** Today's quiet delta is at most ~28, plus at most ~20 of king or pawn terms, plus 50 only on the castling move. A handmade knight swing of 150–200, or a PeSTO knight delta of similar size at pawn-unit 52–74, is larger than the gap between a knight (259) and a bishop (296). Many quiet moves that used to fall under alpha now clear it. Each one is a recursive call.
2. **Ordering.** The first move of the next iteration is whichever move won the shallow score. If the shallow winner is "knight to a pretty square" rather than the move that holds the position, beta cutoffs fail and the node explodes. Depth-1 MVV does not save the internal nodes; PST is applied from depth 2 up.
3. **LMR re-search.** The reduced score is more likely to exceed alpha when the eval jitters by a pawn, so the full-depth re-search runs.

At 200k / depth 4 the PeSTO bench still **reached** depth 7, with 2.45× the nodes and a lower nodes/s. The Elo loss there is the eval and the wider tree, not a missing ply. On the device the wall clock dies inside an iteration. A 2× node tree at a 20% slower nodes/s is roughly half the effective depth per second. That is the fixed-node (and fixed-time) harm. The 1M bench did not show the same node ratio, which is why every arm logs both.

The sticky-capture quirk scales with the table. A knight that gained 200 points on the way to its square keeps those 200 points in the opponent's deficit after the knight is taken. The opponent is paid the knight's 259 material and is still charged the 200. At center-table scale (≤ 28) Muller calls this tolerable. At scale 4 it dominates minor-piece trades. Refunding the victim's PST is a precondition for any table whose one-move delta is allowed to exceed ~30, and it is optional while the delta stays at the center-table span.

PR #9's phase scan (walk the board, then `(mg * phase + eg * (24 - phase)) / 24` per endpoint) is independent of the node-count rise. It explains the nodes/s drop. Any tapered table that ships has to keep phase in a byte and adjust it by the captured piece's increment on make and unmake. No per-move board walk, no divide on the hot path (lerp with a shift, or pick one table).

## 2. How other small engines add a PST

The useful split is: engines whose eval is a **sum of table entries**, and micro-Max, whose eval is a **delta that forgets the victim**. Copying a sum-table into the delta path is how round 1 was built.

| Engine | What the PST actually is | Scale | License / reuse |
|---|---|---|---|
| **micro-Max 4.8** (H.G. Muller). Source notes: `https://home.hccnet.nl/h.g.muller/pcsqr.html`, `eval.html`, `dwnldpage.html`. | One center table for every piece except the rook and queen, stored in `b[x+8]`. Muller writes that kings are pulled to the center in the middlegame, which is the known defect of the shared table. Positional points are transient. | Small integers, here 0–28 before any extra `+50` / `−20`. | Already in-tree via mcu-max (MIT, Gissio). Keep the structure. |
| **Fairy-Max 4.8** (same author). `fmax.ini` loads move tables and piece values. Talkchess thread `https://talkchess.com/viewtopic.php?t=17213`. Overview: `https://home.hccnet.nl/h.g.muller/CVfairy.html`. | **No per-piece PST.** Muller kept the shared center bias on purpose, so fairy pieces are not judged against orthodox pieces that own a tuned table. `fmax.ini` does not carry positional tables. | Same center idea as micro-Max. | Cite as the counterexample. Do not expect a Fairy-Max table file to import. |
| **Sunfish** (Thomas Ahle). `https://github.com/thomasahle/sunfish/blob/master/sunfish.py`. | A real sum. Each square stores **material plus** a positional adjustment. Bases in the current file: P 100, N 280, B 320, R 479, Q 929, K 60000. The loader does `x + piece[k]` on every square, pads to a 10×12 mailbox, and the position score is the sum. Make/unmake adds `pst[piece][to] - pst[piece][from]` and drops the victim. One table, no middlegame/endgame split. Search is MTD-bi with null move; the README points at MVV/LVA as future ordering, which is the same hole micro-Max has above depth 1. | Centipawns, pawn ≈ 100. Positional entries are tens of centipawns, on top of the piece base. Because the base is on every square, a **delta** cancels material. A **sum** used as the root score would stack Sunfish material on top of mcu-max's `37 * value`. | **GPL-3.** Do not copy numbers into this MIT tree. A hand-written table may follow the shape: positional residual only, pawn ≈ 74, one-move delta capped. |
| **PeSTO / RofChade** (Ronald Friederich). `https://rofchade.nl/?p=307`, CPW `https://www.chessprogramming.org/PeSTO%27s_Evaluation_Function`. | Sum of tapered tables. Material and PST are added per square, then `(mg * phase + eg * (24-phase)) / 24`. Phase counts N/B = 1, R = 2, Q = 4, both colors; the start position is 24. Tempo bonus aside, that is the whole eval. Tuned by Texel's method. Friederich writes that the engine depends on alpha-beta because the knowledge is only the tables. CPW records Pawel Koziol's about **+200 Elo** on TSCP, and Maksim Korzh's use in Wukong JS. | Centipawns. The knight middlegame plane in PR #9's pawn-unit 74 header runs from −124 to +95, which inverts to about −168…+128 centipawns and already sits on the `int8` rails. | No MIT/public-domain grant turned up on the author page or CPW. `tools/pesto_source.py` on PR #9 attributes a mirror to Karls-Sun `pesto.py` (MIT); confirm that file's license **before** a merge. An experiment branch may carry the numbers with a source note. `main` should not gain them until `docs/sources.md` records the grant. The TSCP +200 is a conventional eval that **sums** the table. It is not a prediction for this search. |
| **Michniewski simplified eval** (CPW `Simplified Evaluation Function`). | The usual hand PST: pawn advancement, knight center, bishop diagonals, rook files, queen a little, king shelter vs centralize. Material separate. | Centipawns, pawn 100, positional mostly inside ±50. | No public-domain or MIT sentence found. Shape reference only. The handmade PR #9 tables were original and already in that shape; the failure was the multiplier, not the idea of a knight-center table. |
| **Toledo Nanochess** (Óscar Toledo G.). `https://nanochess.org/chess3.html`. | A full chess program in about 1257 non-blank C characters (2009), evolved from his IOCCC entry. The published point of the program is that a tiny eval which knows where pieces belong is what makes the search play. The source header is "all rights reserved." | Packed into the byte budget of that program. | Cite the design pressure (eval must be tiny and must be the same function the search sees). Do not copy the source. The JS chess on the same site is non-commercial with attribution; that grant is not a grant for the C program. |
| **Texel** (Peter Österlund). `https://www.chessprogramming.org/Texel%27s_Tuning_Method`. | Not a PST layout. Logistic fit of eval weights to game results: minimize squared error between the sigmoid of the quiescence score and the game outcome. Österlund used fast self-play, dropped book plies and mates, and wanted a deterministic q-score. Texel 1.03 gained on the order of 100 Elo from the tuning, on Texel's search. | Whatever units that eval already used. | **GPL.** Use the method on host. Do not import Texel weights. |
| **Teaching engines (Vice / TSCP)** | A normal `evaluate()` sums material and PST at the leaf. Futility margins are written in the **same centipawns** as the tables (a margin of one or two pawns times depth is the usual textbook form). | Pawn = 100. | Read for the margin lesson. TSCP's PeSTO patch is the +200 anecdote above. Vice's source is a tutorial, not a donor of tables. |

What transfers to mcu-max:

- Sunfish and PeSTO both **remove the victim's table entry** on capture, and they **include every piece's table entry in the root score**. micro-Max does neither. A delta-only port of a folded table accidentally cancels the material component (`base+pos(to) - (base+pos(from))`) and still forgets the victim's positional points. That is safe only while those positional points stay small.
- Fairy-Max is the reminder that Muller himself did not move from the shared center table to per-piece tables inside this search.
- The king-in-the-center complaint on `pcsqr.html` is the argument for a king middlegame table now that castling works.
- Nobody who got a large Elo gain from PeSTO did it by deleting their only pawn-structure term and dropping the result into a futility test with margin 0.

## 3. Candidate approaches

Costs are for the ESP32-C3 (single core, up to 160 MHz, 400 KB SRAM of which 16 KB is cache, no PSRAM, 8 MiB flash). Flash size is not the constraint. Internal RAM, the 16 KiB AI stack, and the hot path are. Figures are engineering estimates until a map file and a host node bench say otherwise. Device timing stays **NOT RUN** until a task authorizes a flash. See [docs/hardware.md](../hardware.md) and [docs/performance.md](../performance.md).

Shared constraints for every candidate:

- `static const` tables, so they sit in `.rodata` (flash, via the cache), not `.bss`. The implementation PR pastes `idf.py size` / the map line. A 10% host slowdown on a fixed position at a fixed node cap means the table landed in RAM or the hot path grew a scan.
- No new array in `mcumax_search`'s stack frame.
- One-move `|pst(to) - pst(from)|` capped, in engine units, by a host check over the piece's step vectors. The cap for the first content PR is **32** (just above the center table's 28, under half a pawn).
- Keep promotion `647` and the passer term. They are the queen conversion, not a duplicate of a rank bonus.
- Keep the deadline / completed-iteration patch.
- Tactical host tests stay: mate in one, do not drop the queen in one, deadline keeps the completed capture.
- Opening-book behavior stays. A no-book unit test still asserts that a fixed budget from the start position is a legal developing move, so the table is visible when the book is off.

### (a) PeSTO or Sunfish residual, small weight, existing terms kept

Add `weight * residual(to, from)` on top of the current center delta, pawn structure, castling `+50`, and king freeze. Residual means PeSTO's PST component, or Sunfish's table minus the piece base. Do not add PeSTO/Sunfish material.

**Risk.** Medium if the weight is chosen so the cap of 32 holds. High if pawn-unit 52–74 is reused: that is round 1 again, plus double counting the center table. King middlegame tables that already contain a castling preference will stack on `+50`; keep the king weight inside the cap so castling is not worth two pawns. License risk blocks a merge even if Elo passes.

**Flash / RAM / CPU.** 6×64×2 = 768 bytes flash if both phases are stored, or 6×64 = 384 if only the middlegame residual is used. RAM 0 if `const`. CPU: two cached byte loads and a multiply per made move. No board scan. Expect the node-rate change inside measurement noise when the cap holds. A tapered lerp with a divide, or a phase scan, is out of this candidate; that cost belonged to PR #9 and is not required to blend a small weight.

**Tuning.** Pre-register **one** weight: the largest integer weight whose max one-move residual delta is ≤ 32 engine units. Compute it once on the host from the residual tables and the step vectors. If that arm is significantly negative or nodes-to-depth rise more than 25%, the follow-up is **half** that weight, not a sweep of 52/62/74. Three looked-at weights and a merge of the best one is a multiple-comparison. Sunfish numbers stay off `main` (GPL). PeSTO numbers stay off `main` until the license line in `docs/sources.md` is real.

### (b) Knight, bishop, and king tables only — preferred content change

Leave pawns on the current center table plus the structure / passer / promotion code. Leave rooks and queens at 0 (today's behavior for `type >= 6`). Replace the center delta for the knight, the bishop, and the king.

- Knight: penalize the rim, pay the center. The center table already does a quadratic version of this; the dedicated table is a rim penalty the quadratic understates (the handmade `+40` outpost was the overstatement).
- Bishop: a mild diagonal / development bonus, smaller than the knight.
- King middlegame: shelter on the castled squares, a penalty on `d4`/`e4`/`e5`. This is the term Muller's shared table gets wrong, and it is the term the castling fix makes reachable.
- King endgame: centralize. Select it with the **existing** `non_pawn_material > 30` gate that already turns the freeze off, so the king does not use two phase models. Document that the gate is played-game material, not node material. Do not scan the board to compute a PeSTO phase for this.
- Keep castling `+50` and king freeze `−20` in this PR. The king-table `e1→g1` delta stays small (target ≤ 15) so it does not replace `+50` until a later A/B says the table covers castling on its own.

**Risk.** Low, if the cap of 32 is a `_Static_assert` on the stored bytes (store engine units directly, no runtime multiplier). The handmade scale-4 loss is the version of this idea without the cap. Scale 3 (`150` for that knight) was the same idea still over the cap; "neutral" there is not permission to ship 150-point deltas.

**Flash / RAM / CPU.** Four planes × 64 `int8` = 256 bytes flash (knight, bishop, king middlegame, king endgame). RAM 0. CPU: two flash loads per knight, bishop, or king move, in the 16 KB cache after the first nodes. Pawns keep the two SRAM loads they have today. No multiply, no divide, no scan.

**Tuning.** Hand-write original bytes, each entry in `[-20, +20]`, so a one-move delta is at most 40 before a review and the host cap check rejects anything over 32. Mirror black by rank (`square ^ 0x70` in 0x88, which is the same rank flip PeSTO does with `sq ^ 56` on a1=0). One table, one PR, no weight sweep. If Elo is flat and nodes are flat, stop or go to (c). If Elo is negative, shrink the knight plane first; do not add rooks to compensate.

### (c) Texel-style fit of a small `int8` PST, offline on the host

Fit the same planes as (b), or the residual weights of (a), on the host. The objective is the Texel squared error: sigmoid of the **score this search actually propagates**, against a label. Peter Österlund's writeup is the method (`https://www.chessprogramming.org/Texel%27s_Tuning_Method`). Wukong's notes are a readable checklist for the quiet-position filter.

Labels, in the order to try:

1. A quiet-labeled external set. Lichess games are a reasonable source if the dump stays **out of git** (size, and no new license surprise in the commit). Keep a position when a strong engine's shallow score and a deeper score differ by less than a pawn, drop the first 8 plies, drop mates, and split by **game** so two positions from one game do not land on opposite sides of the holdout. Game result (1 / ½ / 0) is the label.
2. If game results from amateur games mostly teach the PST to imitate blunders, switch the target to the strong engine's quiet eval, scaled so one pawn equals 74. That is supervised regression, not Texel's original self-play. Say so in the PR.

Do not fit against a from-scratch sum that the search does not use. Round 1's tables were tuned for a summing eval and then applied as a sticky delta. Freeze the representation first: either "delta, victim not refunded" or "delta, victim refunded, root score seeded," and fit that.

**Risk.** Medium. A lower mean squared error is not +Elo. Self-play of this engine labels the winner of two weak games; prefer the external quiet set. `int8` quantization and the cap of 32 are constraints of the fit, not a round-off step at the end. Over-fitting six full PeSTO planes (768 free bytes) will memorize the sample; (b)'s four planes with symmetry (about 32×4 free numbers if left/right mirrors are tied) is the parameter budget.

**Flash / RAM / CPU.** Same as the plane set that was fitted. The fit itself is host-only. Device cost is the table.

**Tuning.** Coordinate descent on one entry by ±1, inside the cap, is enough at this size. Hold out 20% of games. Ship the candidate only if holdout error drops **and** the Elo protocol passes against the unfitted (b) table, not against a mythical +200.

### (d) Rescale the search margins with the eval

Only if a table that is already known to be positionally better still loses Elo **and** nodes-to-depth rose more than 25%. The constants that are in eval units are the futility comparison, the 1-unit delay bonus, the 1-unit delayed-loss bonus, and the 1-unit null window. The constants that are **not** eval units, and must be left alone by this PR, are: null-move reduction (2 plies), LMR (1 ply), the check extension, `non_pawn_material` gates (35 and 30), MVV/LVA, and `37 * piece_value`.

A principled futility change when positional noise grows is a **wider** margin (search the move unless `step_score + margin <= alpha`), which prunes **less** and spends **more** nodes. That is the conservative direction, and it makes the node problem worse. The direction that restores the old node count is to **shrink the PST** until the existing margin of 0 is valid again. Prefer that. A margin PR that multiplies the delay bonus by the PST weight will tax every exchange; Muller already moved that bonus once to stop exactly that (`progress.html`).

**Risk.** High. This couples search and eval. A good result cannot be attributed. Easy to "fix" the node count by pruning tactics.

**Flash / RAM / CPU.** No table cost. CPU moves with the prune rate. Unbounded until measured.

**Tuning.** Do not grid-search margins. If (and only if) shrinking the table is refused for a written reason, set the futility margin to the measured max one-move PST delta of that table, run the protocol once, and stop.

### (e) Better than a bigger table

**e1. Refund the victim and seed the root score.** On capture, subtract the victim's positional term; on `set_fen`, set the running score to the positional sum of the side to move minus the opponent. Do this first at the **current center table**, as its own A/B, before any large weight. Muller already says the missing refund causes strange trades and is only harmless because the center term is small. Outcome to record: Elo of refund-on versus refund-off at today's table. If that is significantly negative, the search has adapted to the sticky score and a large PST will keep failing until (d) is honestly faced. If it is flat, later large weights must use the refund or they reintroduce the bug the small table was hiding. Cost: a few arithmetic operations, no flash, no RAM. Risk: low at center-table scale (the correction is ≤ 28 per capture).

**e2. Incremental phase, no scan.** Required hygiene for any tapered table. Not a strength change by itself. Phase byte, `+inc` / `−inc` on make and unmake, match a from-scratch count in a unit test.

**e3. Captures before quiet moves, still without a move list.** Two passes over the same 0x88 scan: captures, then quiets. The strength plan ranked this at about +10 to +40 Elo, unmeasured, and ranked it **after** a PST. Round 1 says the interaction is the other way: PST damage is largely an ordering damage. Run e3 as its **own** PR, with the protocol, before any second attempt at pawn-unit ≥ 16. Do not combine it with a new table. Cost: the hot loop gets a second scan, so nodes/s may drop; the bet is fewer nodes per depth. RAM 0. Stack 0 if the pass index is a local that already exists in the frame. Risk: medium, because every cutoff changes.

**e4. Do not fold material into the PST.** Sunfish can, because capture replaces one summand with another. Doing it here without e1 double-counts every capture. A delta of a folded table cancels the base and looks innocent; seeding the root with the same table does not.

## 4. A/B protocol

Runner: `tools/elo_match.py`. It alternates colours (`game % 2`), asks our CLI for `ai <nodes> <depth>`, plays Stockfish with `chess.engine.Limit(time=0.2)`, and refuses a rating if any game aborts. Stockfish **17.1**. Log `id name`. Configure `Skill Level`, and also `Threads=1` and `Hash=16` (the PR #9 helper and the PR #11 report both fixed those; the script on `main` sets skill only, and 17.x defaults are already 1 and 16 — log the configured values so a future default cannot silently move).

Both arms: release `chess-cli` (not ASan), same compiler flags, same opening book, `book_seed == 0` (CLI default). Baseline is `main` at the SHA recorded in the match log, rebuilt on that machine. Candidate is the patch on top of that SHA. PGN is kept with the log and is not committed (game dumps do not belong in git).

Hash bits, both arms the same:

| Pair | `MCUMAX_HASH_BITS` | Why |
|---|---|---|
| Primary | 12 (4096 entries) | What CLI and firmware compile now. |
| Diagnostic, required on the **first** remeasure, optional later if primary is unchanged | 0 | PR #9 had hashing off. This is the pair that isolates the castling fix from the new table. |
| Forbidden | 10 | 1024 entries lost heavily in the PR #11 report. |

Three settings, each is two full matches (baseline vs Stockfish, candidate vs Stockfish). The reported number is the difference of the two Elo-versus-Stockfish figures.

| Id | Games per arm (floor) | Skill | Our budget |
|---|---:|---:|---|
| S0-200k | 200 | 0 | `--nodes 200000 --depth 4` |
| S3-200k | 200 | 3 | `--nodes 200000 --depth 4` |
| S3-1M | 100 | 3 | `--nodes 1000000 --depth 8` |

Colour schedule matches across arms (game 1 white for both). Stockfish skill moves are stochastic, so the games are independent, not paired ply by ply. Subtracting two Elo estimates is the comparison PR #9 already used.

**Elo and the interval.** For a match with per-game scores `x_i` in `{0, 0.5, 1}`, let `p` be the mean and `s^2` the sample variance. Elo versus the opponent is

```text
f(p) = -400 * log10(1/p - 1)
```

which is what `elo_match.py` prints. The 95% interval uses the delta method, the same idea as [docs/artifacts/elo-report-2026-10-09.md](../artifacts/elo-report-2026-10-09.md):

```text
se(p) = sqrt(s^2 / n)
f'(p) = 400 / (ln(10) * p * (1-p))
CI(f) = f(p) ± 1.96 * |f'(p)| * se(p)
```

For the difference `d = f(p_candidate) - f(p_baseline)`, add the variances. Use the sample variance, not the binomial `p(1-p)` shortcut in PR #9's `elo_compare_engines.py`: that shortcut is wider whenever games are drawn, and it is not on `main`. If `p` is 0 or 1, print the win/draw/loss line and mark the Elo undefined; do not clamp.

Around a 50% score and n = 200, a rough half-width is ±40 to ±50 Elo (draws make it narrower; a lopsided skill-0 score makes the Elo interval wider). **+30 Elo will often have an interval that still covers 0.** That is expected. The acceptance rule below uses the point estimate for the +30 and the interval only for "significantly negative," which matches the written bar and does not pretend 200 games can certify +30.

**Stopping.**

- Fixed floor above is mandatory. Do not stop a setting early because it looks good.
- After the floor, extend **only** a setting whose point estimate is in `(0, +30)` and whose interval covers both 0 and +30. Extend that one setting by batches of 50 games, cap 400 (200k settings) or 200 (the 1M setting). Stop the extension when the interval lies entirely above 0 and the point estimate is ≥ +30, or the interval's upper bound is < 0, or the cap is hit.
- A setting whose interval upper bound is already < 0 at the floor has failed. Do not add games to talk it into a pass.
- This is a one-sided sequential rule on top of a fixed sample, not a claim of an optimal SPRT. A full SPRT (H0: 0 Elo, H1: +30 Elo, α = β = 0.05, logistic likelihood ratio, boundary `ln(19) ≈ 2.94`) is acceptable **instead of** the batch extension, with the same floors: the likelihood ratio is not allowed to accept H1 before the floor, and it is allowed to accept H0 (no gain) only after the floor. Do not change the boundaries after seeing the games.

**Acceptance, all required.**

1. Point estimate of candidate minus baseline ≥ **+30 Elo** in at least one of the three settings, at a sample at least as large as the floor.
2. No setting has a 95% interval whose upper bound is < 0.
3. Zero aborted games in every arm. One abort voids that arm's rating.
4. Node diagnostic, same binary: at least the start position plus seven FENs (two open positions, two castled middlegames, two endgames, one tactical position with a piece hanging). Record mean nodes and mean completed `iter_depth` at 200k / depth 4. If the candidate's mean nodes at equal completed depth exceed the baseline by more than **25%**, the Elo bar must **also** be met at `--nodes floor(200000 * nodes_baseline / nodes_candidate)` (and the analogous scaled 1M budget). A +30 that only appears because the tree got fatter does not ship.
5. Report W/D/L, score, Elo versus this Stockfish at this time control, the interval, aborts, median seconds per game, hash bits, and the node diagnostic. The lore anchors in `elo_match.py` (skill 0 ≈ 800, skill 3 ≈ 1000) stay in a footnote. They are not the result. Do not convert to a Chess.com or FIDE number.

**First step, before any new table.** Rebase PR #9's PeSTO pawn-unit **52** patch onto `9386ded` with no weight change and no restoration of the deleted terms. Run primary (bits 12) and diagnostic (bits 0) through the three settings. Interpretation:

| Remeasure | What it means | Next PR |
|---|---|---|
| Bits 0 becomes ≥ +30 and bits 12 is not significantly negative | The old −73 was the castling confound (and maybe the table-off search). The PeSTO-with-terms-removed patch is a merge candidate only if the node rule passes, and only with the license line. | Stop, or try restoring pawn structure as a follow-up. |
| Bits 0 stays negative, bits 12 anything | Castling was not the whole loss. Large PeSTO deltas and the deleted terms are still a loss on a binary that can castle. | Do not merge. Go to (b), then (e1) if (b) is flat. |
| Bits 12 and bits 0 disagree by a wide margin | The table and the 4096-entry TT interact. | Do not merge a PST that only wins with one hash size. Record both. The shipping default is 12, subject to the device heap gate. |

Skill 3 / 1M was still queued when PR #9's description was last updated. Those cells are **not** evidence. The remeasure fills them.

## 5. Sequence of small PRs

Ranked by expected value per unit of search risk. Each PR is one hypothesis. Elo is host-only until a flash is authorized. `python3 tools/check_docs.py` on doc edits; that script does not play chess and does not build firmware.

| Order | PR | Hypothesis | Expected value | Stop if |
|---|---|---|---|---|
| 1 | Rebase PR #9 PeSTO pawn-unit 52 onto `9386ded`. No new numbers. | The −73 was the missing castling (and the hash-off search). | Information. A pass is the highest Elo available in one step, because the patch already exists. A fail retires pawn-unit 52–74. | Merge only on the acceptance rule. A second fail closes full-weight PeSTO for this search. |
| 2 | **(b)** Knight, bishop, king middlegame / endgame, entries in engine units, cap 32, existing terms kept. | A king shelter and a knight rim penalty, at the scale the futility test was written for, are worth about +30 once castling exists. | Best risk-adjusted chance of a real gain. 256 bytes flash, no RAM, no scan. | Significantly negative, or nodes-to-depth > +25% and the scaled-budget rerun fails. Flat and node-neutral is an acceptable stop. |
| 3 | **(e1)** Refund victim PST and seed the root score, **current center table only**, no new planes. | The sticky score is harmless at ≤ 28 and the search still works when the score becomes a real sum. | Small Elo either way. High value as a gate: it tells every later PR whether a large table is even representable. | Skip if PR 2 already passed and nodes were flat. Do this before any weight above the cap. |
| 4 | **(e3)** Two-pass captures-then-quiets. No PST change. | Ordering, not knowledge, is the remaining hole, and it is what made large PST deltas expensive. | The strength plan's +10 to +40, still unmeasured. Also the precondition for another look at a bigger weight. | Node rate drops more than the node count falls, and Elo is flat or negative. |
| 5 | **(c)** Texel / quiet-label fit of the PR 2 planes, host only, one candidate. | The hand bytes in PR 2 are the right shape and the wrong heights. | A few tens of Elo if PR 2 was flat for magnitude reasons. Zero if the search cannot use a better static score (PR 3 failed). | Holdout error does not drop, or Elo versus PR 2 fails the protocol. |
| 6 | **(a)** at the single pre-registered small weight, existing terms kept, license cleared. | An externally tuned residual still has signal after (b) and (c), once the weight respects the cap. | Low until PR 1 shows a sign change, or PR 2/5 saturate. 384–768 bytes flash. | Any weight chosen after looking at several Elo runs. Any import of GPL Sunfish numbers. |
| 7 | **(d)** margin touch. | Only after a written, measured reason that shrinking the table is worse than teaching futility a new margin. | Low. Easy to destroy the +40 Elo Muller measured for futility. | It is the first idea anyone reaches for. It should not be. |

Out of this sequence until the table above is exhausted: rook and queen planes, deleting pawn structure, deleting castling `+50`, a 1024-entry TT, NNUE, a different engine.

## Tests and evidence

| Layer | What | Pass |
|---|---|---|
| Docs | `python3 tools/check_docs.py` | Pass. This is not perft, not Elo, not firmware. |
| Rules | Existing host perft, unchanged core | Green. A PST PR that diffs `components/chess_core` has failed its own scope. |
| Engine | Mate in one, do not drop a queen, deadline keeps the completed capture, PST delta matches a from-scratch sum on a scratch board, black mirror of a white move, cap check on one-move deltas | Green before any match is quoted. |
| Host Elo | Section 4 | Acceptance rule. Otherwise the PR states **NOT MET** and does not merge for strength. |
| Firmware build | `idf.py size` when a table is added | `.rodata` grows by about the table size. `.bss` does not. Build pass is not an Elo pass. |
| Device | Frequency, nodes, depth, stack high-water, heap | **NOT RUN** until a task authorizes a flash. Do not fill the cell from host nodes/s. |

## Open points

1. PeSTO's license for a merge: confirm the Karls-Sun `pesto.py` MIT claim against the file, or get a grant from Ronald Friederich, or keep the numbers on the experiment branch forever. Handmade bytes in PR 2 do not wait on this.
2. Lichess dump location and filter, if PR 5 happens. The dump is not committed. Game-level holdout is part of the method, not an afterthought.
3. Whether the device heap gate leaves the 4096-entry table on during a real game. PST host results at bits 12 match the CLI. They match the device only when the gate keeps the table. That comparison is **NOT RUN**.
4. Restoring pawn structure on top of a PeSTO residual is a separate PR after PR 1, and only if PR 1 is non-negative. It is not part of the rebase.
