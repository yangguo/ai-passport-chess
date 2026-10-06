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

## AI status: wired, live test gated

`ai [white|black]` (default black) attaches the console engine from a
fresh startpos game — mid-game attaches are refused, since the engine
has no setboard. Every reply is diffed, dry-run on a position copy,
and applied only on full match; any mismatch detaches with a message
and the core game stands untouched. Human underpromotion desyncs the
engine board by design (it always queens): detected via the echo
guard, detached loudly, never silently continued.

The engine binary is NOT built here (external-code execution needs a
Bash permission rule in this environment). Vendored source +
protocol notes: `third_party/mcu-max/`. To run the live test:

```sh
! gcc -std=c89 -O2 -Wno-error=implicit-function-declaration \
    -o /tmp/umax third_party/mcu-max/umax4_8.c
! CHESS_ENGINE_BIN=/tmp/umax sh tools/test_cli.sh   # adds cli_ai_live
```

Or interactive: `./build/cli/chess-cli --engine /tmp/umax`
(`CHESS_ENGINE_BIN` env works too). Each engine turn searches up to
~1M nodes (seconds); reads carry a 120 s timeout with kill.
Alternative engine on watch: ripred/MicroChess (MIT, <2K RAM) — see
`third_party/mcu-max/SOURCES.md`.
