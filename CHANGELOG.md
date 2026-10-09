# Changelog

## Unreleased

- **AI:** Seeded opening book in the mcu-max adapter (~64 positions, hand-written
  lines in `opening_lines.txt`); `book_seed == 0` keeps host Elo harness reproducible.
- **Engine:** PeSTO MG/EG PST in `mcu-max` (`mcumax_pesto_tables.h`, scaled
  `int8`); tapered eval; removed overlapping pawn structure / castling / king-freeze
  bonuses; host `test_pst` + `tools/gen_pesto_pst.py`.

## 0.1.0-design — 2026-10-06

- 建立AI Passport Chess设计与实施契约，锁定参考来源。
- 增加架构、硬件、交互、规则、测试、AI、存档、预算、构建/CI/刷机、验收与路线图。
- 增加5组perft基准输入与文档检查CI；未实现棋局或固件。
