# chess-cli — host playable frontend

Local two-player chess in the terminal over the tested core. Build and
run the scripted behavior tests:

```sh
sh tools/test_cli.sh        # cmake + build + ctest (ASan/UBSan)
./build/cli/chess-cli       # interactive: type help
```

Single-file saves (`save`/`load`) are raw codec images with history,
so threefold claims survive a restart. The A/B NVS recovery protocol
itself is covered by `test_codec` with the fake backend.

## AI: in-process mcu-max, always on

`ai` moves for the side to move from any ongoing position: our FEN
into the pinned engine (`third_party/mcu-max`, MIT), bounded search
(200k nodes / depth 4), reply converted back and core-validated
before it lands. `ai <nodes> [depth]` overrides that budget. The
default command is unchanged.

`ai movetime <ms>` uses the firmware deadline instead of a node cap.
A monotonic clock is checked from the engine callback; when it passes
the deadline, `mcumax_stop_search` runs and the last fully completed
root iteration is kept. A partial iteration does not replace it. Node
and depth guardrails are the CLI maxima (100000000 nodes, depth 64),
so wall time is what stops a normal search. Opening-book hits return
immediately, as they do for `ai`. Device levels are 100 ms, 1500 ms,
and 5000 ms. `cli_movetime` covers a short out-of-book deadline.

Promotion replies are queens (engine limit); the
core still accepts all four. No subprocess, no session, no setboard
limits — and `cli_ai` covers the default command in the normal suite.

Timed Elo (same deadline on every move; Stockfish still uses its own
limit inside the harness):

```sh
python3 tools/elo_match.py --cli build/cli/chess-cli \
    --stockfish /path/to/stockfish \
    --games 10 --skill 5 --movetime 1500
```
