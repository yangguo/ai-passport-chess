# AI 与 mcu-max 集成

已接入 [Gissio/mcu-max](https://github.com/Gissio/mcu-max)，MIT；锁定 SHA 见[来源](sources.md)。原引擎不是本项目规则权威，不直接复制 micro-Max 默认资源设置。AI 与主机集成测试、固件构建已通过；Chess.com 快棋 1200 仍为独立平台验收目标，见下文。

## 已核实的接口与风险

- `mcumax_set_fen_position` 返回 void，不提供输入校验；仅接受 core 导出的已验证 FEN。
- `mcumax_search_best_move(node_max, depth_max)` 同步运行；有内部加深逻辑。`mcumax_set_callback` 注册周期回调，回调中可 `mcumax_stop_search()`。
- `mcumax_move` 只有 from/to，没有 promotion 字段；头文件注明不支持 underpromotion。候选涉及升变时 adapter 明确附 Q，若 Q 非合法/不合产品战术需求则降级，绝不删除 core 中的 R/B/N。
- 引擎坐标采用从 FEN 第8行开始的0x88表示。核心 `a1=0` 转换 `((7-core_rank)<<4)|file`；不得误以为同一个 rank。a1/h8/双翼易位/ep 用测试验证。
- 状态是全局的，仅一个 worker 调用。FEN 每次搜索前重建，欠升变后仍能载入局面里的 N/B/R；不能靠连续调用 `play_move` 维持真实棋局。
- 上游默认 hash 巨表不适合本设备。固件可选 **4096 项**置换表（`MCUMAX_HASH_BITS=12`，约 48 KiB），经 heap 门槛分配；**禁止** 1024 项档。详见 [tt-transposition-table.md](artifacts/tt-transposition-table.md)。
- FEN 能重建局面，不能传全部重复历史。引擎评分对重复/50步的理解需单独核实；应用终局与申报仍由 core 决定。

## 开局库（adapter）

在 `mcumax_search_best_move` 之前，adapter 用 `chess_position_key` 的 34 字节做
FNV-1a 64 位哈希，查一张编译进 Flash 的线性表（约 87 个局面、≤4 个加权候选；
由 Lichess **CC0** `chess-openings` + opening explorer **真实频率**快照生成，见 `BOOK_SOURCES.md`）。
`book_seed == 0` 永远走权重最高的主变（host CLI / `tools/elo_match.py` 默认即 0，无需改 harness）。
产品新开局在 `start_ai_game` 时生成一次 `book_seed` 并整局保持不变；读档续走视为**已离开开局库**
（`book_enabled = false`，不改存档 schema）。库着法经 `chess_make` 复验，非法则退回搜索。
Easy 的 15% 随机着仅在离开库后生效。来源与再生成见 `components/chess_ai/BOOK_SOURCES.md`。

## 适配流程

1. app 生成不可变 position/FEN、合法根着、generation、预算；无合法着直接由 core 判终局。
2. worker 初始化并导入 FEN，注册带 context 的 callback，设置绝对 monotonic deadline。
3. 先保留一枚 core 合法的保底着；若开局库命中则直接采用库着，否则调用搜索，节点/深度仅作为第二道资源护栏，**时间截止是主限制**。
4. callback 以 clock 检查 deadline/cancel；按实测节奏让出 CPU（目标每2–5ms至少一次可阻塞 yield，不在每节点无条件 delay）。不能在 callback 里打印、绘制、写 NVS。
5. 返回转换坐标和 promotion；app 只有在 generation 相同、轮到AI、非终局时才检查精确合法着并提交。
6. 到达思考时限且存在完整层合法结果时正常返回最佳着；越界、无结果、引擎不支持、无完整层结果的超时及用户取消均有显式码；保底合法着只能在非用户取消且当前局面有效时采用。用户取消后不偷偷落子。

Host CLI 的 `ai movetime <ms>` 走同一条完成层截止：单调时钟到期后回调里 `mcumax_stop_search()`，已完成的根迭代保留，未完成的一层不替换它。节点 100000000 与深度 64 只是护栏。默认 `ai` 仍是 200000 节点 / 深度 4。`tools/elo_match.py --movetime <ms>` 发出这条命令（设备档位 100 / 1500 / 5000 ms）。

已增加本地补丁：在完整根层结束后保存最佳着，stop 时返回该检查点，未完成层不能覆盖；详见第三方 SOURCES.md。实际 callback 间隔、stop 后 unwind 时延与栈余量仍需在设备上测量。不得在每次外层加深重启后丢弃上一次好结果。

**PR #19（busy 标志）：** 开局库着在 worker 内同步完成时，若在 `xQueueSend` **之后**才置 `busy`，会覆盖 worker 已清掉的 busy，导致普通/困难在库应手后不再发起第二次搜索。现于入队前置 busy，入队失败则回滚。

## 难度与提示（固件预算）

| 档位 | 墙钟 | 节点护栏 | 深度护栏 | 策略 |
|---|---:|---:|---:|---|
| Easy | 100 ms | 500 000 | 6 | 搜索后 15% 概率改选其他合法着；seed 可重放 |
| Normal | 1500 ms | 5 000 000 | 8 | 最后完成层最佳着 |
| Hard | 5000 ms | 20 000 000 | 12 | 相同规则，较长预算 |
| Hint | 400 ms | （同搜索路径） | — | 仅显示建议，不执行；玩家走后清除 |

mcu-max 不暴露多候选评分，不虚构 top-N 接口。不要把四子棋 nodes/s 换算成象棋性能，也不凭深度单独声称 Elo。

## 引擎棋力改进里程碑（PR #7 / #12 计划摘要）

[2026-10-09 离线引擎棋力计划](plans/2026-10-09-engine-strength-plan.md)（规划文档，**结果以已合并 PR 为准**）提出开局库、分棋种 PST、小置换表三条线。截至 `8e72ee4` 的落地与测量结论：

| 主题 | 合入 PR | 结果摘要 |
|---|---|---|
| Lichess CC0 开局库（真实频率加权） | #8 / #10 | 去掉确定性 `1.c3`/`…c6`；`book_seed==0` 保持 harness 可复现 |
| FEN 易位/已动子修复 + 可选 4K TT（单次搜索内） | #11 | 相对旧 main 的 FEN 路径约 **+100～+210 Elo**（box）；1024 项 TT **不采用**；4096 项可选且受 heap 门槛约束 |
| PeSTO 评估修正 + 成对 SPRT 工具 | #15 | `tools/elo_compare_engines.py` 五结果桶 + pentanomial GSPRT；PeSTO-fix 对上一版 main **+42 Elo**（108 对，H1 接受），见下节 |
| PST 方向/吃子捕获修正 | #17 | 与 #15 一并消除错误增量 PST；配合严格 UBSan 对齐 TT 读 |
| 库应手后 busy 挂起 | #19 | 修复普通/困难在库应手后只走一步的问题 |

[2026-10-10 PST 第二轮计划](plans/2026-10-10-pst-round2-plan.md) 记录 PR #9 在缺失易位修复时 PeSTO 大表负 Elo 的原因，以及「先重测 castling 修复基线、再小表 / SPRT」的顺序。#15 的 SPRT 与 #17 的方向修正完成了该计划中的关键验收路径；全盘大权重 PeSTO 仍受 futility/增量分数耦合约束，未宣称 Chess.com 1200。

## 实测强度（host，2026-10-12，`main` @ `8e72ee4`）

**尺度说明：** 下表 Elo 为相对所选 Stockfish **Skill Level** 的分差，经 Stockfish 内部 skill→Elo 曲线换算（CCRL 闪电战尺度）。**换算不确定约 ±100～150**；**不是** Chess.com/FIDE/人类等级。主机 CPU 快于 ESP32-C3，同档 `movetime` 在设备上通常更弱（设备 nodes/s：**NOT RUN**）。

**方法：** Stockfish 17.1，对手每步 0.2 s；本方 `tools/elo_match.py --movetime` 对齐固件墙钟（Easy 含 15% 随机）。完整表与 SPRT 见 [elo-report-2026-10-12.md](artifacts/elo-report-2026-10-12.md)、[host-elo-2026-10-12.json](benchmarks/host-elo-2026-10-12.json)。

| 档位 | SF Skill | n | 胜/和/负 | 得分 | 隐含我方 Elo 95% 区间 |
|---|---:|---:|---|---:|---|
| Easy 100 ms | 0 | 150 | 61/36/53 | 52.7% | 1365 [1317, 1415] |
| Easy 100 ms | 1 | 100 | 30/16/54 | 38.0% | 1359 [1291, 1421] |
| Normal 1500 ms | 5 | 150 | 74/46/30 | 64.7% | 2302 [2256, 2352] |
| Normal 1500 ms | 6 | 100 | 37/34/29 | 54.0% | 2410 [2355, 2467] |
| Hard 5000 ms | 5 | 120 | 75/30/15 | 75.0% | 2388 [2333, 2452] |
| Hard 5000 ms | 7 | 120 | 31/52/37 | 47.5% | 2501 [2453, 2548] |

**合并印象（同尺度，粗估）：** Easy ≈**1360**；Normal ≈**2300–2410**；Hard ≈**2390–2500**。

### PeSTO-fix vs 上一版 `main`（SPRT，#15）

| 指标 | 值 |
|---|---|
| 工具 | `tools/elo_compare_engines.py --h2h` |
| 对局对数 | 108 |
| 五结果桶 (LL/LD/DD-WL/WD/WW) | 0 / 10 / 65 / 30 / 3 |
| 得分 | 56.0% |
| Elo 差 | +42 [+21, +64] |
| LLR | 2.97（界 ±2.94；H0 0 / H1 +10 Elo；α=β=5%） |
| 预算 | 200 ms/步 |
| 开局 | Lichess 1378 局面集 |
| 结论 | **接受 H1** |

### 历史测量（勿作当前棋力）

| 批次 | 摘要 | 证据 |
|---|---|---|
| 2026-10-08 | 430 局、节点代用护栏；Easy ≈1290，CLI 默认 ≈1625，Normal 代用 ≈1670，Hard 代用 ≈1755 | [host-elo-2026-10-08.json](benchmarks/host-elo-2026-10-08.json) |
| 2026-10-07 | 6–10 局/组 + 旧 skill 传闻锚点；“天花板 700–900” | 已废止，见下表 |

**已废止：2026-10-07 小样本诊断（勿作当前结论）**

| 我方配置 | 对手 | 比分 | 当时误读 |
|---|---|---|---|
| CLI 默认 200k/4 | skill 5 | 0/10 | “远低于 1200” |
| CLI 默认 200k/4 | skill 3 | 1/8 | 约 650–750（lore 锚点） |
| Normal 1M/8 | skill 5 | 0.5/6 | 约 780 |
| Hard 4M/12 | skill 3 | 1/6 | 约 720 |
| Hard 4M/12 | skill 5 | 0–2 后停 | 明显被动 |

废止原因：样本过小、锚点与 SF17.1 不符、早期 runner 和棋处理与 ASan 构建干扰。2026-10-12 `movetime` 重测取代上述结论。

提示及搜索不保存引擎全局内存。每次退出关闭 callback，确认 worker ACK 后释放数据。MIT LICENSE 与修改 diff 随集成提交，第三方版本升级重跑 adapter、deadline、perft（core 未变也测接口）与设备预算。

## 普通难度 Chess.com 快棋 1200 验收目标

用户指定参照为 Chess.com 快棋 1200；不等同于 FIDE、其他平台或
参考引擎的 UCI_Elo 设置。1200 是目标，不是当前评级。主机已对 Stockfish 17.1 完成成批对照（见上表），**不能**直接写成“已达 Chess.com 1200”。

评级需注明参考等级体系、对手和版本、用时、硬件与开局。建议使用
固定开局成对白黑交换对局，至少 100 局起步并报告胜和负与置信区间。
降低等级的参考引擎只能作工程对照，不能直接当作 Chess.com 快棋评级。
最终还需与该平台目标水平的棋手在明确用时下进行陪练评估，
不得为此自动登录平台或代用户发起线上对局。
战术测试、主机自对弈、ESP32 真机对局分别记录，不能混用结果。

Chess.com 快棋 1200 **平台/陪练评级：NOT RUN**；主机 Stockfish 对照（2026-10-12）：**已测**；#19 后设备应手/栈余量：**NOT RUN**。

### 2026-10-07 截止修复记录（源码 00ba318）

- 本地 ASan/UBSan host：15/15；CLI：5/5；文档及字体覆盖通过。
- 截止保留吃后回归：`6k1/8/8/8/4q3/8/8/4R1K1 w - - 0 1` 新版返回 `e1e4` 吃后。
- 设备预算验收与平台 1200：NOT RUN（见 2026-10-12 主机表）。
