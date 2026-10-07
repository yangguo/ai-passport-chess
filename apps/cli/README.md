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
before it lands. Promotion replies are queens (engine limit); the
core still accepts all four. No subprocess, no session, no setboard
limits — and `cli_ai` covers it in the normal suite.
