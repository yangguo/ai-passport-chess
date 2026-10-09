#!/usr/bin/env python3
"""Elo probe: our CLI engine (`ai`) vs Stockfish at a fixed skill level.

Method: full games through two independent legs. python-chess is the
referee (rules, termination); our chess-cli drives OUR moves via its
`ai` command (core-validated, exactly as shipped); Stockfish replies
at --skill. Both sides' moves are applied to both boards; any
rejection aborts that game loudly instead of silently biasing.

Score maps to an Elo difference against the ASSUMED anchor Elo of the
chosen Stockfish skill level (table below is lore, not metrology):
  skill 0 ~= 800, 3 ~= 1000, 5 ~= 1200, 8 ~= 1400, 10 ~= 1600.
Read the result as "supports / does not support 1200", never as a
certificate. Short TC, few games, wide error bars by construction.

Requires: python-chess (test dep, already pinned) and a Stockfish
binary (user-provided --stockfish; NOT vendored, NOT firmware).

Usage:
  python3 tools/elo_match.py --cli build/cli/chess-cli \
      --stockfish /opt/homebrew/bin/stockfish \
      --games 10 --skill 5
"""
import argparse
import math
import subprocess
import sys

try:
    import chess
    import chess.engine
except ImportError:
    print("SKIP: python-chess not installed")
    sys.exit(0)

ANCHORS = {0: 800, 3: 1000, 5: 1200, 8: 1400, 10: 1600}


class Cli:
    """Line-protocol driver for chess-cli (see apps/cli/README.md)."""

    def __init__(self, path, budget="ai"):
        self.proc = subprocess.Popen(
            [path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        assert self.proc.stdin is not None
        assert self.proc.stdout is not None
        self.budget = budget

    def ai_move(self):
        """Ask the engine to move; returns UCI or raises."""
        self.proc.stdin.write(self.budget + "\n")
        self.proc.stdin.flush()
        for _ in range(200):
            line = self.proc.stdout.readline()
            if not line:
                raise RuntimeError("cli EOF during ai")
            line = line.strip()
            if line.startswith("engine: "):
                return line.split(" ", 1)[1]
            if line.startswith("error"):
                raise RuntimeError("cli refused ai: %s" % line)
        raise RuntimeError("ai gave no move")

    def play(self, uci):
        """Play a move; returns True iff accepted (`ok`)."""
        self.proc.stdin.write("play %s\n" % uci)
        self.proc.stdin.flush()
        for _ in range(200):
            line = self.proc.stdout.readline()
            if not line:
                raise RuntimeError("cli EOF during play")
            line = line.strip()
            if line.startswith("ok ") or line.startswith("error"):
                return line.startswith("ok ")
        raise RuntimeError("play got no verdict")

    def close(self):
        try:
            self.proc.stdin.write("quit\n")
            self.proc.stdin.flush()
            self.proc.wait(timeout=10)
        except Exception:
            self.proc.kill()


def play_game(cli, engine, our_color, max_plies):
    """Returns (result, pgn); result 1.0/0.5/0.0 ours, None on abort."""
    import chess.pgn

    board = chess.Board()
    game_node = chess.pgn.Game()
    game_node.headers["White"] = "ours" if our_color == chess.WHITE else "sf"
    game_node.headers["Black"] = "sf" if our_color == chess.WHITE else "ours"
    node = game_node
    try:
        for _ in range(max_plies):
            if board.is_checkmate():
                # board.turn is the mated side; headers are White POV.
                game_node.headers["Result"] = (
                    "0-1" if board.turn == chess.WHITE else "1-0"
                )
                return (1.0 if board.turn != our_color else 0.0,
                        str(game_node))
            if (
                board.is_stalemate()
                or board.is_insufficient_material()
                or board.is_seventyfive_moves()
                or board.is_fivefold_repetition()
                or board.can_claim_threefold_repetition()
                or board.can_claim_fifty_moves()
            ):
                game_node.headers["Result"] = "1/2-1/2"
                return 0.5, str(game_node)
            if board.turn == our_color:
                try:
                    uci = cli.ai_move()
                except RuntimeError as exc:
                    print("  abort (our engine): %s" % exc)
                    return None, str(game_node)
                try:
                    move = board.parse_uci(uci)
                    board.push(move)
                    node = node.add_variation(move)
                except ValueError:
                    print("  abort (referee rejects %s)" % uci)
                    return None, str(game_node)
            else:
                result = engine.play(board, chess.engine.Limit(time=0.2))
                uci = result.move.uci()
                board.push(result.move)
                node = node.add_variation(result.move)
                try:
                    if not cli.play(uci):
                        print("  abort (cli rejects %s)" % uci)
                        return None, str(game_node)
                except RuntimeError as exc:
                    print("  abort (cli link): %s" % exc)
                    return None, str(game_node)
        game_node.headers["Result"] = "1/2-1/2"
        return 0.5, str(game_node)  # overlong: adjudicated draw
    finally:
        cli.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", required=True)
    parser.add_argument("--stockfish", required=True)
    parser.add_argument("--games", type=int, default=10)
    parser.add_argument("--skill", type=int, default=5)
    parser.add_argument("--plies", type=int, default=200)
    parser.add_argument("--nodes", type=int, default=200000)
    parser.add_argument("--depth", type=int, default=4)
    parser.add_argument("--pgn", default=None)
    args = parser.parse_args()

    budget = "ai %d %d" % (args.nodes, args.depth)
    pgn_path = args.pgn
    pgn_file = open(pgn_path, "w") if pgn_path else None
    engine = chess.engine.SimpleEngine.popen_uci(args.stockfish)
    try:
        engine.configure({"Skill Level": args.skill})
        score = 0.0
        played = 0
        aborted = 0
        for game in range(args.games):
            our_color = chess.WHITE if game % 2 == 0 else chess.BLACK
            side = "white" if our_color == chess.WHITE else "black"
            cli = Cli(args.cli, budget)
            outcome = play_game(cli, engine, our_color, args.plies)
            result, pgn = outcome
            if pgn_file is not None:
                pgn_file.write(pgn)
                pgn_file.write("\n\n")
                pgn_file.flush()
            if result is None:
                print("game %d (%s): ABORTED" % (game + 1, side))
                aborted += 1
                continue
            played += 1
            score += result
            tag = "1-0 ours" if result == 1.0 else (
                "1/2 " if result == 0.5 else "0-1 opp"
            )
            print("game %d (%s): %s" % (game + 1, side, tag))
    finally:
        if pgn_file is not None:
            pgn_file.close()
        engine.quit()

    if aborted:
        print("incomplete match: %d game(s) aborted; refusing to report rating"
              % aborted)
        return 1

    print("played %d, score %.1f/%.0f" % (played, score, played))
    if played == 0:
        return 1
    rate = score / played
    anchor = ANCHORS.get(args.skill, None)
    if anchor is not None:
        print("anchor assumption: stockfish skill %d ~= %d Elo (lore)"
              % (args.skill, anchor))
    if rate <= 0.0 or rate >= 1.0:
        print("elo diff: out of range (all wins or all losses)")
        print("score rate %.3f over %d games (95%% CI: n/a)" % (rate, played))
    else:
        diff = -400.0 * math.log10(1.0 / rate - 1.0)
        # Wilson interval for binomial score rate (wins + 0.5 draws) / games.
        wins = score
        z = 1.959963984540054
        n = float(played)
        p = wins / n
        denom = 1.0 + z * z / n
        center = (p + z * z / (2.0 * n)) / denom
        margin = (
            z
            * math.sqrt((p * (1.0 - p) + z * z / (4.0 * n)) / n)
            / denom
        )
        lo = max(0.0, center - margin)
        hi = min(1.0, center + margin)
        if lo <= 0.0 or hi >= 1.0:
            elo_lo = elo_hi = None
        else:
            elo_lo = -400.0 * math.log10(1.0 / hi - 1.0)
            elo_hi = -400.0 * math.log10(1.0 / lo - 1.0)
        print("elo diff vs opponent: %+.0f" % diff)
        if elo_lo is not None:
            print(
                "95%% CI (score rate %.3f, Elo diff): [%+.0f, %+.0f]"
                % (rate, elo_lo, elo_hi)
            )
        else:
            print("95%% CI (score rate): [%.3f, %.3f]" % (lo, hi))
        if anchor is not None:
            print("implied ours: ~%.0f Elo (anchor %+.0f)" % (anchor + diff, diff))
    return 0


if __name__ == "__main__":
    sys.exit(main())
