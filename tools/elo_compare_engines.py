#!/usr/bin/env python3
"""Compare two chess-cli builds.

Versus Stockfish (default when --main-cli, --pst-cli and --stockfish
are set): each build plays its own games against the same opponent.
The per-arm 95% interval uses trinomial win/draw/loss variance and a
bound transformation into logistic Elo. The difference of the two Elos
uses sqrt(se_a^2+se_b^2). That difference is conservative and is not
an acceptance test. See tools/elo_stats.py.

Head-to-head (--h2h): the candidate plays the baseline directly.
Openings are paired and colors are swapped. Results are pentanomial
(LL, LD, DD/WL, WD, WW). The run reports logistic and normalized Elo
with 95% intervals, and a pentanomial GSPRT (Fishtest logistic LLR)
with Wald bounds. It stops when the LLR crosses a bound, or at
--pairs. --state resumes by pair index. It stores the SHA256 of both
binaries, the budget, the openings-file hash, elo0/elo1/alpha/beta,
and the opening order. A mismatch refuses the resume. A failed pair
is retried; a saved index is not replayed. --concurrency is the number
of games in flight. --movetime and --nodes/--depth are the two budgets;
a node cap is checked between root iterations, so acceptance matches
should prefer --movetime.

  python3 tools/elo_compare_engines.py --h2h \
      --candidate build/cli/chess-cli \
      --baseline build-main/cli/chess-cli \
      --movetime 1500 --concurrency 4 \
      --state /tmp/pst-sprt.json
"""
from __future__ import annotations

import argparse
import os
import sys
from concurrent.futures import ThreadPoolExecutor

try:
    import chess
    import chess.engine
    import chess.pgn
except ImportError:
    print("python-chess required")
    sys.exit(1)

try:
    from elo_match import Cli, play_game
    from elo_stats import (
        DIFF_NOTE,
        arm_elo,
        cli_ai_command,
        elo_difference,
        fresh_state,
        llr_logistic,
        match_config,
        missing_pair_indices,
        pair_bin,
        pentanomial_summary,
        read_state,
        record_pair,
        resume_mismatches,
        sha256_file,
        sha256_text,
        sprt_bounds,
        sprt_decision,
        write_state,
        completed_indices,
    )
except ImportError:
    from tools.elo_match import Cli, play_game
    from tools.elo_stats import (
        DIFF_NOTE,
        arm_elo,
        cli_ai_command,
        completed_indices,
        elo_difference,
        fresh_state,
        llr_logistic,
        match_config,
        missing_pair_indices,
        pair_bin,
        pentanomial_summary,
        read_state,
        record_pair,
        resume_mismatches,
        sha256_file,
        sha256_text,
        sprt_bounds,
        sprt_decision,
        write_state,
    )


# White to move. Cycled when there are more pairs than openings.
BUILTIN_OPENINGS = (
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4",
    "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq c6 0 2",
    "rnbqkbnr/ppp2ppp/4p3/3p4/3PP3/8/PPP2PPP/RNBQKBNR w KQkq - 0 3",
    "rnbqkb1r/ppp2ppp/4pn2/3p4/2PP4/2N5/PP2PPPP/R1BQKBNR w KQkq - 2 4",
    "rnbqkbnr/pp2pppp/2p5/3p4/3PP3/8/PPP2PPP/RNBQKBNR w KQkq - 0 3",
    "rnbqkb1r/pppppppp/5n2/8/2P5/2N5/PP1PPPPP/R1BQKBNR w KQkq - 2 3",
    "rnbqkb1r/pppppp1p/5np1/8/2PP4/8/PP2PPPP/RNBQKBNR w KQkq - 0 3",
)

BIN_NAMES = ("LL", "LD", "DD/WL", "WD", "WW")


def load_openings(path: str | None) -> list[str]:
    if path is None:
        return list(BUILTIN_OPENINGS)
    found = []
    with open(path, encoding="utf-8") as fh:
        for raw in fh:
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            board = chess.Board(line)
            if board.turn != chess.WHITE:
                raise ValueError("opening must be white to move: %s" % line)
            found.append(line)
    if not found:
        raise ValueError("opening file has no FENs")
    return found


def play_two_clis(white: Cli, black: Cli, fen: str, max_plies: int):
    """One game. Returns 1/0.5/0 for White, or None if a CLI aborts."""
    board = chess.Board(fen)
    game_node = chess.pgn.Game()
    game_node.setup(board)
    game_node.headers["White"] = "white-cli"
    game_node.headers["Black"] = "black-cli"
    node = game_node
    try:
        white.loadfen(fen)
        black.loadfen(fen)
    except RuntimeError as exc:
        print("  abort (loadfen): %s" % exc)
        return None
    players = {chess.WHITE: white, chess.BLACK: black}
    try:
        for _ in range(max_plies):
            if board.is_checkmate():
                game_node.headers["Result"] = (
                    "0-1" if board.turn == chess.WHITE else "1-0"
                )
                return 0.0 if board.turn == chess.WHITE else 1.0
            if (
                board.is_stalemate()
                or board.is_insufficient_material()
                or board.is_seventyfive_moves()
                or board.is_fivefold_repetition()
                or board.can_claim_threefold_repetition()
                or board.can_claim_fifty_moves()
            ):
                game_node.headers["Result"] = "1/2-1/2"
                return 0.5
            mover = players[board.turn]
            other = players[not board.turn]
            try:
                uci = mover.ai_move()
            except RuntimeError as exc:
                print("  abort (ai): %s" % exc)
                return None
            try:
                move = board.parse_uci(uci)
                board.push(move)
                node = node.add_variation(move)
            except ValueError:
                print("  abort (referee rejects %s)" % uci)
                return None
            try:
                if not other.play(uci):
                    print("  abort (opponent cli rejects %s)" % uci)
                    return None
            except RuntimeError as exc:
                print("  abort (cli link): %s" % exc)
                return None
        game_node.headers["Result"] = "1/2-1/2"
        return 0.5
    finally:
        del node


def _one_game(white_path, black_path, fen, budget, plies):
    white = Cli(white_path, budget)
    black = Cli(black_path, budget)
    try:
        return play_two_clis(white, black, fen, plies)
    finally:
        white.close()
        black.close()


def _fmt_ci(ci: tuple[float, float]) -> str:
    def one(x: float) -> str:
        if x == float("inf"):
            return "+inf"
        if x == float("-inf"):
            return "-inf"
        if x != x:
            return "nan"
        text = "%+.0f" % x
        if text == "-0":
            return "+0"
        return text

    return "[%s, %s]" % (one(ci[0]), one(ci[1]))


def _print_h2h(state: dict, llr: float, lower: float, upper: float, decision: str) -> None:
    summary = pentanomial_summary(state["counts"])
    counts = " ".join(
        "%s=%d" % (name, n) for name, n in zip(BIN_NAMES, state["counts"])
    )
    print(
        "pairs %d %s score %.3f logistic %+.1f CI %s normalized %+.1f CI %s"
        % (
            summary["pairs"],
            counts,
            summary["score"],
            summary["logistic_elo"],
            _fmt_ci(summary["logistic_ci"]),
            summary["normalized_elo"],
            _fmt_ci(summary["normalized_ci"]),
        ),
        flush=True,
    )
    print(
        "LLR %+.4f bounds [%+.4f, %+.4f] %s"
        % (llr, lower, upper, decision),
        flush=True,
    )


def run_h2h(args, budget: str, play_pair=None) -> int:
    """Play the head-to-head match.

    play_pair(index, fen, candidate, baseline, budget, plies) returns a
    pentanomial bin 0..4, or None when that pair failed. The default
    plays two real chess-cli games. Tests pass a fake.
    """
    try:
        if args.openings:
            openings_sha = sha256_file(args.openings)
        openings = load_openings(args.openings)
    except (OSError, ValueError) as exc:
        print("openings: %s" % exc, file=sys.stderr)
        return 1
    for fen in openings:
        # Reject illegal FENs before any process starts.
        chess.Board(fen)
    if not args.openings:
        openings_sha = sha256_text("".join(fen + "\n" for fen in openings))
    candidate = os.path.abspath(args.candidate)
    baseline = os.path.abspath(args.baseline)
    try:
        candidate_sha = sha256_file(candidate)
        baseline_sha = sha256_file(baseline)
    except OSError as exc:
        print("cannot hash engine binary: %s" % exc, file=sys.stderr)
        return 1
    config = match_config(
        candidate,
        baseline,
        budget,
        openings,
        args.elo0,
        args.elo1,
        args.alpha,
        args.beta,
        args.plies,
        candidate_sha,
        baseline_sha,
        openings_sha,
    )
    resuming = bool(args.state and os.path.exists(args.state))
    if resuming:
        try:
            state = read_state(args.state)
        except (OSError, ValueError) as exc:
            print("refusing to resume: %s" % exc, file=sys.stderr)
            return 1
        reasons = resume_mismatches(state, config)
        if reasons:
            print("refusing to resume:", file=sys.stderr)
            for reason in reasons:
                print("  %s" % reason, file=sys.stderr)
            return 1
        done = sorted(completed_indices(state))
        if len(done) <= 8:
            shown = str(done)
        else:
            hole_list = [i for i in range(done[-1] + 1) if i not in set(done)]
            shown = "%d pairs through %d, holes %s" % (
                len(done),
                done[-1],
                hole_list[:12],
            )
        print("resume completed %s" % shown, flush=True)
    else:
        state = fresh_state(config)
        if args.state:
            write_state(args.state, state)
    lower, upper = sprt_bounds(args.alpha, args.beta)
    print(
        "h2h budget %s elo0 %g elo1 %g alpha %g beta %g bounds [%+.4f, %+.4f]"
        % (budget, args.elo0, args.elo1, args.alpha, args.beta, lower, upper),
        flush=True,
    )
    print("openings %d concurrency %d" % (len(openings), args.concurrency), flush=True)
    limit = args.pairs if args.pairs is not None else 10**9
    already = completed_indices(state)
    past = sorted(index for index in already if index >= limit)
    if past:
        print(
            "refusing to resume: saved pair index %d is past --pairs %d"
            % (past[0], limit),
            file=sys.stderr,
        )
        return 1
    use_sprt = not args.no_sprt
    # One batch is concurrency//2 pairs (at least one). Both games of a
    # pair are started together when concurrency >= 2. The SPRT is
    # rechecked after the batch, so a batch already in flight can cross
    # the bound by the rest of that batch. Holes (a failed pair whose
    # sibling was saved) are filled before that check.
    batch_pairs = max(1, args.concurrency // 2)
    workers = max(1, min(args.concurrency, batch_pairs * 2))

    def save_pair(pid: int, bin_id: int) -> None:
        record_pair(state, pid, bin_id)
        if args.state:
            write_state(args.state, state)
        llr = llr_logistic(args.elo0, args.elo1, state["counts"])
        decision = sprt_decision(llr, args.alpha, args.beta) if use_sprt else "fixed"
        _print_h2h(state, llr, lower, upper, decision)

    def real_outcomes(ids, pool):
        jobs = []
        for pid in ids:
            fen = openings[pid % len(openings)]
            jobs.append((pid, "white", fen))
            jobs.append((pid, "black", fen))
        results = {}
        if workers == 1:
            for pid, side, fen in jobs:
                if side == "white":
                    results[(pid, side)] = _one_game(
                        candidate, baseline, fen, budget, args.plies
                    )
                else:
                    results[(pid, side)] = _one_game(
                        baseline, candidate, fen, budget, args.plies
                    )
        else:
            futs = {}
            for pid, side, fen in jobs:
                if side == "white":
                    futs[(pid, side)] = pool.submit(
                        _one_game, candidate, baseline, fen, budget, args.plies
                    )
                else:
                    futs[(pid, side)] = pool.submit(
                        _one_game, baseline, candidate, fen, budget, args.plies
                    )
            results = {key: fut.result() for key, fut in futs.items()}
        outcomes = {}
        for pid in ids:
            white_score = results[(pid, "white")]
            black_score = results[(pid, "black")]
            if white_score is None or black_score is None:
                outcomes[pid] = None
            else:
                # black_score is White's result in the game where the
                # candidate is Black, so the candidate's score is the complement.
                outcomes[pid] = pair_bin(white_score, 1.0 - black_score)
        return outcomes

    def fake_outcomes(ids, pool):
        def run(pid):
            fen = openings[pid % len(openings)]
            return play_pair(pid, fen, candidate, baseline, budget, args.plies)

        if workers == 1:
            return {pid: run(pid) for pid in ids}
        futs = {pid: pool.submit(run, pid) for pid in ids}
        return {pid: fut.result() for pid, fut in futs.items()}

    with ThreadPoolExecutor(max_workers=workers) as pool:
        while True:
            done = completed_indices(state)
            ids = missing_pair_indices(done, limit, batch_pairs)
            if not ids:
                break
            filling = bool(done) and min(ids) <= max(done)
            if use_sprt and done and not filling:
                llr_now = llr_logistic(args.elo0, args.elo1, state["counts"])
                if sprt_decision(llr_now, args.alpha, args.beta) != "continue":
                    break
            if play_pair is None:
                outcomes = real_outcomes(ids, pool)
            else:
                outcomes = fake_outcomes(ids, pool)
            failed = []
            for pid in ids:
                outcome = outcomes[pid]
                if outcome is None:
                    failed.append(pid)
                    continue
                try:
                    save_pair(pid, outcome)
                except ValueError as exc:
                    print("refusing to record: %s" % exc, file=sys.stderr)
                    return 1
            if failed:
                print(
                    "aborted pair index %s; saved indices %s; "
                    "resume retries the missing indices and does not replay saved ones"
                    % (failed, sorted(completed_indices(state))),
                    file=sys.stderr,
                )
                return 1
        if not completed_indices(state):
            print("no pairs played", file=sys.stderr)
            return 1
        llr = llr_logistic(args.elo0, args.elo1, state["counts"])
        if use_sprt:
            decision = sprt_decision(llr, args.alpha, args.beta)
            if decision == "continue":
                print("SPRT continue (pair cap reached)", flush=True)
            else:
                print("SPRT %s" % decision, flush=True)
    return 0


def run_arm(label, cli_path, stockfish, games, skill, budget, plies):
    engine = chess.engine.SimpleEngine.popen_uci(stockfish)
    wins = draws = losses = 0
    aborted = 0
    try:
        engine.configure({"Skill Level": skill, "Threads": 1, "Hash": 16})
        for game in range(games):
            our_color = chess.WHITE if game % 2 == 0 else chess.BLACK
            cli = Cli(cli_path, budget)
            outcome = play_game(cli, engine, our_color, plies)
            if outcome[0] is None:
                aborted += 1
                continue
            result = outcome[0]
            if result == 1.0:
                wins += 1
            elif result == 0.0:
                losses += 1
            else:
                draws += 1
            played = wins + draws + losses
            if (game + 1) % 10 == 0 or game + 1 == games:
                print(
                    "  %s game %d/%d score %.1f/%d"
                    % (label, game + 1, games, wins + 0.5 * draws, played),
                    flush=True,
                )
    finally:
        engine.quit()
    return wins, draws, losses, aborted


def run_versus_stockfish(args, budget: str) -> int:
    results = {}
    for label, path in ("main", args.main_cli), ("pst", args.pst_cli):
        print("=== %s ===" % label, flush=True)
        wins, draws, losses, aborted = run_arm(
            label, path, args.stockfish, args.games, args.skill, budget, args.plies
        )
        if aborted:
            print("aborted %d games" % aborted)
            return 1
        arm = arm_elo(wins, draws, losses)
        lo, hi = arm["ci"]
        print(
            "score %.1f/%d W/D/L %d/%d/%d rate %.3f elo_vs_sf %+.0f "
            "CI [%.0f, %.0f] se %.1f"
            % (
                arm["score"] * arm["n"],
                arm["n"],
                wins,
                draws,
                losses,
                arm["score"],
                arm["elo"],
                lo,
                hi,
                arm["se_elo"],
            ),
            flush=True,
        )
        results[label] = arm
    diff = elo_difference(results["pst"], results["main"])
    print("=== pst vs main (Elo vs SF) ===", flush=True)
    print(
        "elo_diff_pst_minus_main %+.1f CI [%+.1f, %+.1f] se %.1f"
        % (diff["diff"], diff["ci"][0], diff["ci"][1], diff["se"]),
        flush=True,
    )
    print(DIFF_NOTE, flush=True)
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Compare two chess-cli engines.")
    ap.add_argument("--h2h", action="store_true", help="candidate vs baseline")
    ap.add_argument("--candidate", help="chess-cli under test")
    ap.add_argument("--baseline", help="chess-cli to beat")
    ap.add_argument("--main-cli", help="baseline cli for the Stockfish arms")
    ap.add_argument("--pst-cli", help="candidate cli for the Stockfish arms")
    ap.add_argument("--stockfish")
    ap.add_argument("--games", type=int, default=200)
    ap.add_argument("--skill", type=int, default=0)
    ap.add_argument("--nodes", type=int, default=None)
    ap.add_argument("--depth", type=int, default=None)
    ap.add_argument(
        "--movetime",
        type=int,
        default=None,
        help="milliseconds per move (ai movetime). Not combined with nodes.",
    )
    ap.add_argument("--plies", type=int, default=200)
    ap.add_argument("--pairs", type=int, default=None, help="max color-swapped pairs")
    ap.add_argument("--concurrency", type=int, default=1)
    ap.add_argument(
        "--state",
        help="JSON resume file: engine SHA256, openings hash, pair indices",
    )
    ap.add_argument("--openings", help="FEN file, one white-to-move line each")
    ap.add_argument("--elo0", type=float, default=0.0, help="H0 logistic Elo (default 0)")
    ap.add_argument(
        "--elo1",
        type=float,
        default=10.0,
        help="H1 logistic Elo (default 10, a coarse acceptance margin)",
    )
    ap.add_argument("--alpha", type=float, default=0.05)
    ap.add_argument("--beta", type=float, default=0.05)
    ap.add_argument(
        "--no-sprt",
        action="store_true",
        help="play exactly --pairs and do not stop on the LLR",
    )
    args = ap.parse_args(argv)

    if args.movetime is not None and (args.nodes is not None or args.depth is not None):
        ap.error("--movetime cannot be combined with --nodes or --depth")
    nodes = 200000 if args.nodes is None else args.nodes
    depth = 4 if args.depth is None else args.depth
    try:
        budget = cli_ai_command(args.movetime, nodes, depth)
    except ValueError as exc:
        ap.error(str(exc))

    if args.h2h:
        if not args.candidate or not args.baseline:
            ap.error("--h2h requires --candidate and --baseline")
        if args.concurrency < 1:
            ap.error("--concurrency must be >= 1")
        if args.no_sprt and args.pairs is None:
            ap.error("--no-sprt requires --pairs")
        if args.pairs is not None and args.pairs < 1:
            ap.error("--pairs must be >= 1")
        if not args.elo0 < args.elo1:
            ap.error("--elo0 must be below --elo1")
        return run_h2h(args, budget)

    if not args.main_cli or not args.pst_cli or not args.stockfish:
        ap.error("versus-Stockfish mode requires --main-cli, --pst-cli and --stockfish")
    if args.games < 1:
        ap.error("--games must be >= 1")
    return run_versus_stockfish(args, budget)


if __name__ == "__main__":
    sys.exit(main())
