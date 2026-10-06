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

## AI status: NOT WIRED

`third_party/mcu-max` is vendored (pinned SHA, see SOURCES.md) but no
`ai` command exists yet: driving the engine binary is blocked on a
Bash permission rule for external-code execution in this environment.
To unblock, either add that settings rule or build/run it by hand:

```sh
! gcc -std=c89 -O2 -o /tmp/umax third_party/mcu-max/umax4_8.c
! printf '\n' | timeout 15 /tmp/umax | head -12
```

Once the binary runs, the `ai` hook is: spawn engine at game start,
forward each human move, diff consecutive board prints for the reply,
re-verify placement against the core FEN, and apply only after a
dry-run `chess_make` on a position copy succeeds. Underpromotion by
the human desyncs the engine board — detect via the FEN guard, detach
with a message, never silently continue.
