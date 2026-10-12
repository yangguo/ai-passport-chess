# Changelog

## Unreleased

- **AI:** Lichess CC0 opening book by real frequency (#8/#10); FEN castling/moved flags and optional 4K-entry TT per search (#11); PeSTO evaluation fix with paired pentanomial SPRT in `tools/elo_compare_engines.py` (#15); PST orientation/capture fix (#17); busy-flag fix so AI continues after a book reply on Normal/Hard (#19).
- **Docs:** 2026-10-12 host Elo retest vs Stockfish 17.1 (`movetime` aligned to firmware); supersede 2026-10-08 node-proxy table; archive engine-strength and PST round-2 plans with outcome summaries.
- **Community:** FoloToy AI Passport play 1013 revision 2614 submitted for review (pending; not public until approved).

## 0.1.0-design — 2026-10-06

- 建立AI Passport Chess设计与实施契约，锁定参考来源。
- 增加架构、硬件、交互、规则、测试、AI、存档、预算、构建/CI/刷机、验收与路线图。
- 增加5组perft基准输入与文档检查CI；未实现棋局或固件。
