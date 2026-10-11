# 离线引擎棋力改进计划

> 日期：2026-10-09。状态：**只规划，本文件不改引擎**。基线树：`b119fe6`（`main`）。
> 证据来自当前源码、构建配置，以及 2026-10-08 的 430 局 host 对局包（`analysis.txt`）。设备 nodes/s、栈水位、heap 仍是 **NOT RUN**。

**目标：** 在不削弱 `chess_core` 规则、不打开上游巨表、不把 Flash 当 RAM 的前提下，提高 FoloToy AI Passport（ESP32-C3，单核，无 PSRAM）上 mcu-max 的休闲棋力，并去掉「永远 1.c3 / 应答永远 c6」的确定性开局。

**架构：** 规则仍只在 `chess_core`。引擎仍是全局、同步、欠升变的 mcu-max；adapter 负责 FEN、时限、core 复验。开局库放在 adapter，不放进搜索。PST 与小置换表是引擎内部补丁，沿用现有增量评估，不另写一套局面扫描。

**明确不做：** 本计划不授权刷机、不授权社区发布、不改 `docs/ai-integration.md`、`docs/status.md`，也不改 `tools/elo_match.py` 里的 Elo 锚点表（另一条 PR 在改这三处）。不引入 NNUE、不换引擎、不把 Sunfish/Polyglot 源码或 GPL 数据拷进 MIT 树。

## 结论（先看这个）

| 提议 | 代码里的事实 | 采用 |
|---|---|---|
| 1. 加 PST，马占中心、兵升变加分，增量更新 | 已有**一张共用**中心权重，以及 micro-Max 的兵结构/通路兵/升变加分。车和后**没有**位置分。增量更新已经在走子时做完。缺的是分棋种子表，不是从零加评估 | 替换共用中心项为分棋种 `int8` 表（兵、马、象、车、后、王）。保留现有兵结构、易位 +50、王的冻结罚分和升变转换。不叠 PeSTO 全盘扫描 |
| 2. 确认 alpha-beta、迭代加深、置换表在固件里开着 | alpha-beta 和迭代加深**始终编译进** host 与固件，没有单独的关闭宏。置换表宏被注释掉；打开上游宏会链进约 192MiB 表。host 与固件是同一份 `mcu-max.c` | 保持前两项不动。小置换表作为**显式补丁**：2 的幂、编译期断言、默认 1024 项（约 12KiB BSS）。heap 门槛不过就保持设备关闭 |
| 3. 几十步开局库，兼带多样性，harness 可复现 | 430 局里，凡是没有 Easy 随机的配置，执白第一步 **100% 是 c3**，执黑第一步 **100% 是 c6**（对方先走 c4/e4/d4/e3 都一样）。加深到 4M 节点 / 深度护栏 12 也不变 | adapter 内位置键开局库，约 1–2KiB Flash。`book_seed == 0` 走权重最高的主着，CLI / Elo 默认就是 0。产品对局用**开局时固定**的种子，不用每步都变的 `generation` |

预期（都是**未测区间**，不是承诺）：开局库 +30～+100 Elo（同时解决多样性）；分棋种 PST 若能打掉「所有兵各走一格」则 +50～+150，标错会回退；1K–4K 置换表 +20～+80。三者相加仍不能写成 Chess.com 快棋 1200。1200 仍要真人、同用时陪练，本计划不排进前三个 PR。

## 现状

### 引擎与集成

锁定 Gissio/mcu-max 1.0.6，SHA `aa03caffce50729566b5db6965bf735c31f33eea`，MIT，基于 H.G. Muller 的 micro-Max 4.8。本地只加了「完整根层结束才在截止时保留着法」的补丁，见 `third_party/mcu-max/SOURCES.md`。头文件写明欠升变；adapter 把升变一律标成后，再交给 `chess_make`。四种升变仍由 core 保证。

搜索入口是 `mcumax_search`（`third_party/mcu-max/mcu-max.c`）。下面这些**没有** `#ifdef`，host CLI（`apps/cli/CMakeLists.txt`）和固件（`components/chess_ai/CMakeLists.txt`）编的是同一文件，两边都没有 `-DMCUMAX_HASHING_ENABLED`。

| 机制 | 位置 | 现在的行为 |
|---|---|---|
| alpha-beta | 形参 `alpha,beta`；窗口收缩在 190–191 行；`iter_score >= beta` 时 `goto cutoff`（355–357 行） | 始终开启。另有空着裁剪（约 257–277 行）和浅层 futility（445–465 行的重搜循环） |
| 迭代加深 | 230–241 行，注释写 Min depth = 2 | 每个节点内部都在加深，根节点另外受到 `node_max` 与 `depth_max` 约束。不是「只在根上加深一层」的开关 |
| 上一层最佳着优先 | 246–250、312–317、542–548 行 | 有。没有置换表时，这是主要排序 |
| 吃子静止 | 364–365 行：`(iter_depth - !capture_piece) > 1` | 剩余深度 2 只走吃子。这是 micro-Max 4.8 的 all-captures QS，不是独立的 SEE 静止函数 |
| 叶节点 MVV/LVA | 359–362 行，仅 `iter_depth == 1` | 吃子分减去走动子的类型。**不是**内部节点的着法表排序 |
| 晚着削减（LMR） | 430–435 行 | 深度 > 5、非兵、非吃、不是上一层最佳着时多减 1 层，失败再重搜 |
| 将军延伸 | 437–443 行 | 仅在 `non_pawn_material <= 30`、空着证明在被将军、深度 ≥ 3，且着法不是「非王吃子」时，把子节点深度设成**不减**。范围窄，并且有不下降的递归，见下文栈风险 |
| 置换表 | 20 行宏被注释；106 行 `MCUMAX_HASH_TABLE_SIZE (1 << 24)`；118–127 行 `HashEntry` | **关闭**。打开宏才会编译 |

`mcumax_search_best_move` 把调用方的 `depth_max` **再加上 3** 才传入（916 行）。产品档位的 6/8/12 不是「保证走满 6/8/12 个完整棋步」。时间截止才是主限制（`components/chess_ai/include/chess_ai.h` 文件头，`chess_ai.c` 的 callback）。

回调在**每次进入** `mcumax_search` 时触发（150–151 行），也就是每个节点，不是每 N 个节点。`chess_ai.c` 里 yield 按 3ms 节流，但 `esp_timer_get_time()` 仍会每节点跑一次。节点速率未测，callback 占时比例也未测。

### 评估现在算什么

子力（76–77 行，再乘 37，344 行）：

| 子 | 表值 | ×37 后的引擎单位 |
|---|---:|---:|
| 兵 | 2 | 74 |
| 马 | 7 | 259 |
| 象 | 8 | 296 |
| 车 | 12 | 444 |
| 后 | 23 | 851 |
| 王被吃 | -1 | 负分，直接当胜负 |

中心权重写在 0x88 棋盘右半（641–643 行）：

```text
board[16*y + x + 8] = (x-4)^2 + (y-4)*(y-3)
```

`y = 0` 是 FEN 第 8 行。中心格权重大约 0，角上大约 28。走子时位置分是 `board[from+8] - board[to+8]`（367–371 行），所以已经是增量，代价是两次字节读取。

`scan_piece_type < 6` 才使用这张表。棋子编码（`mcu-max.h` 46–55 行）是兵 1/2、马 3、王 4、象 5、车 6、后 7。因此：

- 兵、马、王、象共用**同一张**二次曲面，不是「马喜欢中心、兵喜欢升变」两套表。
- 车和后的位置分是 0。车只在易位时额外 +50（380–384 行）。
- 王在 `non_pawn_material <= 30` 时每走一步再 -20（387–391 行）。该计数从 0 开始，只在 `MCUMAX_PLAY_MOVE` 里按被吃子力累加（486 行），开局几乎全程都在罚王。注释「Freeze king in mid-game」和这个条件是对得上的；它不是残局才罚。

兵的专用项在 393–416 行，原样来自 micro-Max：左右结构/无根、贴王、残局推进（`non_pawn_material >> 2`）、到第 8 行时 `647 - 兵种` 的升变加分（同时把子力码加上去，完成升变），以及通路兵的小加分。所以「兵越靠近升变越值钱」**已经有**，只是和共用中心表缠在一起，而且 430 局说明它没有把开局从 `c3`/`c6` 里拉出来。

不要把 Michniewski 的「第 7 行兵 +50」再加到 647 的升变项上，会双计。

### 档位预算（源码，不是 430 局那次）

`chess_ai_level_budgets`（`components/chess_ai/chess_ai.c` 11–32 行）：

| 档 | 墙钟 | 节点护栏 | 深度护栏 |
|---|---:|---:|---:|
| Easy | 100ms | 500000 | 6 |
| Normal | 1500ms | 5000000 | 8 |
| Hard | 5000ms | 20000000 | 12 |

CLI 默认是另一套：`apps/cli/chess_cli.c` 22–23 行，200000 节点 / 深度 4。`tools/elo_match.py` 的 `--nodes/--depth` 默认也是这对。对方 Stockfish 的用时在 `play_game` 里写死为 `Limit(time=0.2)`，本方**不受**这 0.2 秒约束。

`sdkconfig.defaults` 没有 `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ`。硬件文档只说最高 160MHz，并要求 M0 记录实际频率。计划里的设备耗时都不能假设已经锁在 160MHz。

### 内存与栈（预算，未在本树测到剩余 heap）

ESP32-C3 数据手册：400KB SRAM，其中 16KB 作 cache，另有 8KB RTC SRAM；无 PSRAM。`docs/hardware.md`、`docs/performance.md` 与此一致。factory 分区 `0x10000/0x7f0000`（约 7.9MiB），PST 和开局库的 Flash 可以忽略。约束在内部 RAM。

`docs/performance.md` 的数字是门槛，不是测量：

- AI 任务栈起点 16KiB。`main/chess_ai_task.c` 19 行是 `16384`。同文件注释写明设备上 8KiB 在搜索后的合法着校验里溢出。`main/chess_ai_task.h` 30 行仍写「8 KiB」，注释过时，实现 PR 顺手改掉，不必单开文档 PR。
- AI 置换表预算是 0。路线图风险表：默认巨表会无法链接或 OOM；小表必须补丁并重测。
- 最坏 UI/存档场景内部 free heap ≥ 32KiB，最大 8-bit 块 ≥ 16KiB。本固件的实际 free heap：**NOT RUN**（`docs/acceptance.md` 对应栏为空）。

`HashEntry` 在本机 gcc 13、LP64 上是 **12 字节**（`uint32` + `int32` + 3×`uint8`，按 4 对齐）。RISC-V ILP32 预期相同，实现时用 `_Static_assert`，不要把 12 当成已在 IDF 上测过。

| 方案 | 项数 | BSS | 结论 |
|---|---:|---:|---|
| 上游 `(1<<24)` | 16777216 | 12×16777216 = **201326592 字节（192MiB）** | 禁止。另加 1035 字节 scramble。`mcumax_init` 会 `memset` 整张表 |
| 256 | 256 | 3072 | 只能当排序提示，残局几乎没用 |
| **1024（建议的设备默认）** | 1024 | **12288** | 先编进固件做 MAP；heap 门槛失败就改回 0 |
| 2048 | 2048 | 24576 | host A/B 的第二档 |
| 4096 | 4096 | 49152 | 只在 host 或门槛明显有余量时 |
| 8192 | 8192 | 98304 | 不作为设备候选，除非测量推翻 32KiB 门槛 |

表长必须是 2 的幂：索引是 `& (SIZE - 1)`（195–198 行）。

scramble 表现在在 `mcumax_init` 里用 `srand(1)` + `rand()` 填（660–666 行），而且赋给 `uint8_t`，高 24 位被截掉，但每次仍消耗 4 次 `rand()`。newlib 和 glibc 的 `rand()` 不同。小表补丁要把这 1035 字节做成 **提交进仓库的 `const`**，放 Flash，host 与设备同一张表。不要在设备上调用 `srand`。

哈希键在 `mcumax_init` 里清零，而每次搜索前 adapter 都 `mcumax_set_fen_position` → `mcumax_init`。根局面的键因此总是 0，不同 FEN 会别名到同一槽。Muller 自己写过：这种增量键不是局面的绝对 Zobrist，换根必须清表或换代。第一版小表：**每次 `set_fen` 清掉小表，只服务这一次搜索内部的置换**。跨着保留要另做从 FEN 算出的根键，不放进第一个 TT PR。清 12KiB 相对 1.5s 思考可以忽略；清 192MiB 不行，这也是不能只翻宏的原因。

### 栈

本机 x86-64、gcc 13.3 对**当前**（哈希关闭）源码：

| 优化 | `mcumax_search` 栈帧（`-fstack-usage`） |
|---|---:|
| `-Os` | 160 字节，`static` |
| `-O2` | 192 字节，`static` |

这不是 RISC-V，也不是 IDF 的 `-Og`/`-Os`。只能当数量级。16KiB 任务栈还要放 `chess_ai_run_job` 的 `chess_move legal[256]`（约 1KiB）和局面拷贝。按 160 字节一帧，深度 15（Hard 的 `12+3`）大约 2.4KiB，看起来装得下。将军延伸那条路径把子深度设成不减（440 行 `step_depth = iter_depth`），连续被将时递归深度不受 `depth_max` 约束，节点护栏和 `stop_search` 才能停，但栈可能先溢出。实现任何「加宽将军延伸」之前，先在设备上对连续将军局面读 `uxTaskGetStackHighWaterMark`。IDF 构建加 `-fstack-usage` 作为记录，不作为棋力改动。

设备上一次搜索的节点数、完成层、callback 次数、栈剩余：**NOT RUN**。`chess_ai_task.c` 61–63 行已经会打 `stack_free`，缺的是按档位归档，不是再造一个日志点。

### 2026-10-08 host 基线（430 局）

来源：对局包里的 `analysis.txt` / `results.jsonl` / `run_matches.py`。对手 Stockfish 17.1，`Skill Level` 0 或 3，`Threads=1`，`Hash=16`，每步 `time=0.2`。本方用 `tools/elo_match.py` 的裁判（python-chess、三次重复/五十步可申报即终局、200 步封顶记和）。0 局中止。分数的 95% 区间是分析脚本对局分做的正态近似，再套进 Elo 差，不是 bootstrap。

`run_matches.py` 的节点护栏和**今天的** `chess_ai.c` 不一致：

| 配置名 | 那次实际设置 | 今天源码 |
|---|---|---|
| `cli-default-200k-d4` | 200k / 深度 4 | 与 CLI 默认一致 |
| `easy-500k-d6-rand15` | 500k / 深度 6，另加 15% 非最佳合法着 | 节点/深度与 Easy 一致。15% 是 Python 仿 `CHESS_AI_EASY_RANDOM_PCT`，**不是** `chess_ai_run_job` 本身；合法着顺序可能和 core 不同 |
| `normal-docproxy-1M-d8` | **1M** / 深度 8 | 源码是 **5M** / 深度 8 / 1500ms |
| `hard-docproxy-4M-d12` | **4M** / 深度 12 | 源码是 **20M** / 深度 12 / 5000ms |

因此 430 局不能当成「当前 Normal/Hard 节点护栏」的评级。深度护栏 8/12 倒是一致。Easy 以外的随机着/局都是 0.0。

| 配置 | skill | n | 胜/和/负 | 得分 | 对对方 Elo 差 [95%] | 秒/局 |
|---|---:|---:|---|---:|---|---:|
| cli 200k/4 | 0 | 100 | 76/17/7 | 84.5% | +295 [+227, +388] | 13 |
| cli 200k/4 | 3 | 50 | 11/11/28 | 33.0% | -123 [-223, -40] | 14 |
| easy 500k/6 +15% | 0 | 100 | 30/24/46 | 42.0% | -56 [-118, +3] | 27 |
| normal 1M/8 | 0 | 60 | 48/10/2 | 88.3% | +352 [+264, +497] | 38 |
| normal 1M/8 | 3 | 40 | 12/7/21 | 38.8% | -80 [-190, +17] | 44 |
| hard 4M/12 | 0 | 50 | 41/8/1 | 90.0% | +382 [+286, +561] | 120 |
| hard 4M/12 | 3 | 30 | 12/11/7 | 58.3% | +58 [-39, +166] | 137 |

开局普查（同一批 PGN，SAN）：

- 无随机的配置：执白第一步全部是 `c3`（100/100、25/25、30/30、20/20、25/25、15/15）。执黑第一步全部是 `c6`。
- 对方执白时先手分布（CLI vs skill 0）是 c4×18、e4×14、d4×9、e3×9，我方应答**全是 c6**。不是「只对 1.e4 下卡罗」，是对这四种第一步都走 c 兵一格。
- Easy 有随机：执白 50 局里 37 次仍是 `c3`，其余是 Nh3、Nf3、d4 等。执黑 45/50 仍是 `c6`。
- 白方前几个自己的着法反复出现 `c3 d3 e3 f3` 这种一路兵各进一步。4M/深度 12 仍是 `c3`。加节点没有治好开局；它只提高了中局得分（CLI vs skill 3 是 33%，Hard 4M vs skill 3 是 58%）。`docs/ai-integration.md` 里「加节点只能输慢」对这批数据已经不成立。该段是 2026-10-07 的小样本，文件自己也写了不能当可复现评级。本计划不改那个文件。

锚点怎么读：

- `tools/elo_match.py` 的 `ANCHORS`（skill 0≈800、3≈1000、5≈1200）在脚本注释里写明是 lore，不是计量。用它会把 CLI 200k 说成大约 1100 / 880。这和 2026-10-07 文档里的 700–900 是同一套低估锚。
- 分析脚本另把 Stockfish 里 `UCI_Elo → Skill` 的三次多项式反解（系数 37.2473 / -40.8525 / 22.2943 / -0.311438，Elo 端点 1320 与 3190）。反解约是 skill 0 → 1347、skill 3 → 1729。Stockfish PR 4341 的对局表是 skill 0 ≈ 1320、skill 3 ≈ 1742，校准用时不是 0.2 秒/步。本次 harness **只设了 Skill Level，没有开 `UCI_LimitStrength`**。Skill 是在完整搜索之后按概率选次优着；0.2 秒里的 SF 17.1 仍然搜得很深。所以「SF-anchor 1600–1800」是解释，不是 CCRL 证书，也不是 Chess.com 快棋分。
- 报告时以胜/和/负和「相对这位对手、这个用时的 Elo 差」为主。两种锚都只放在备注里。

host 速率（从 CLI vs skill 0 反推，不是 profiler）：约 13 秒/局、平均 84 步、我方约一半步数、每步 200k 节点，大约 **6×10^5 节点/秒**（那台跑对局的机器）。ESP32-C3 单核 160MHz 通常会慢一个数量级以上，但是 **设备 nodes/s = NOT RUN**。在测到之前，不要把 host 的 1M/4M/5M 节点护栏换算成 1.5s/5s 里能走完的层数。

## 外部引擎里值得搬、不值得搬的

| 来源 | 许可结论 | 用什么 |
|---|---|---|
| micro-Max 4.8，H.G. Muller | 已通过 mcu-max 的 MIT 派生进入本仓库。Muller 页面允许改 `U` 缩小哈希，并写明 6 层搜索上大表大约 2.5 倍，残局差异更大。那是 **16M 项** 的数字，不能套到 1K 项 | 保留现有搜索。小表沿用他的「必须是 2 的幂、换根要失效」 |
| Michniewski，CPW *Simplified Evaluation Function* | 原帖在波兰 progszach 列表。CPW 转载了表。**没有找到独立的公有领域或 MIT 授权**。可以当形状参考，不整表抄进仓库 | 手写 `int8` 表，注释里指向 CPW，不粘贴那 6×64 的原文数字 |
| PeSTO / RofChade，Ronald Friederich | Talkchess 与 CPW 上广泛转载，作者页面邀请下载引擎。**没有单独的 MIT/PD 句子**。CPW 记载 Pawel Koziol 用它给 TSCP 大约 +200 Elo；那是 TSCP 的弱评估，不是 micro-Max | 不做第一版。全量中局/残局插值要动递归传递的 `score`，风险高于预期收益 |
| Sunfish | GPL-3。PST 和它的开局数据都不拷 | 拒绝 |
| Polyglot / 商业开局库 / Lichess 对局库 | 二进制或 PGN 派生数据许可不清。`docs/sources.md` 已写明 python-chess 是 GPL 测试工具，不能进设备代码 | 开局着法由我们手写。python-chess 只在 host 生成脚本和 Elo 裁判里使用 |
| Vice / TSCP / MinimalChess / Leorik | 各自许可不同（TSCP 有自己的许可，Leorik 等需逐个看）。它们的杀手着法、历史启发、MVV-LVA **思想**可用，因为不复制源码 | 杀手着法列为可选。不导入这些仓库的文件 |
| Stockfish / Fruit | 搜索思想（空着、LMR、将军延伸）micro-Max 4.8 已经有简化版 | 不移植 aspiration、历史表或 NNUE |

## 项 1：分棋种 PST

### 方案

| 方案 | 内容 | 取舍 |
|---|---|---|
| A. 维持现状 | 共用中心表 + 现有兵项 | 430 局已证明开局不随深度变好 |
| B. 在现有中心项上再加一套 cp PST | 双计，尺度混用（兵=74 对 Michniewski 兵=100） | 拒绝 |
| **C. 换掉共用中心项** | 分棋种 `int8` 增量：`pst[type][to] - pst[type][from]`，仍写在今天 `step_score` 的位置 | **采用** |
| D. PeSTO 中局/残局插值，每节点扫全盘 | Flash 约 1.5KiB，但破坏「只更新走动的子」 | 可选后续，不在前三个 PR |

采用 C 的具体约定：

- 表放 `static const int8_t`，ESP-IDF 放进 DROM（Flash），不占 BSS。6 种 × 64 = 384 字节；王若分「子力计数 ≤30 / >30」两张，再加 64 字节。合计 < 512 字节 Flash。
- 单位是引擎单位，不是厘兵。单格绝对值建议 ≤ 20（兵的子力是 74）。马到 c3/f3、象出中心、后轻微居中、车占开放线的形状可以参考 Michniewski，**数字手写**并在 `third_party/mcu-max/SOURCES.md` 记「形状参考 CPW，数值为本仓库原创，未整表复制」。
- 兵：用一张鼓励 e4/d4/c5/e5/d5 一类格子、不鼓励 a3/h3/c3 这种边兵一格的表，**替换**兵今天从共用中心表拿到的增量。393–416 行的结构、通路兵、升变 **保持不动**。
- 车、后：今天是 0，各给一张幅度很小的表（后居中、车占 d/e 线），避免后被 PST 支配子力。
- 王：子力计数 ≤30 时奖励易位后的 g1/c1（以及黑方对应格），>30 时改用中心表。阈值沿用 391 行，不新造 phase。
- 右半棋盘的权重字节可以不再写入。`board[]` 仍要保留 0x88 填充，RAM 几乎不降。这不是优化点。
- 不改走子生成、不改欠升变、不改截止补丁。

### 集成点

只改 `third_party/mcu-max/mcu-max.c` 的初始化权重（641–643 行）和 367–371 行的位置增量，外加文件顶部的 `const` 表。`components/chess_ai` 不用改。host 与固件自动一起变。

### 风险

- 尺度过大：引擎会为了位置分送子。单测要求起始局面在固定预算下的着法落在允许集，并且「一步将死、避免一步送后」fixture 仍走战术着。
- 尺度过小：Elo 不动。那就是负结果，表留在分支上不要硬合。
- 兵项双计：若 A/B 出现无意义的冲兵，下一步是减小兵 PST，而不是再加第 7 行加分。
- 位置分仍是「暂态」：micro-Max 被吃子时不退还位置分（Muller 的 eval 说明）。保持这个不一致，不要在第一版「修正确」，否则和子力尺度缠在一起，A/B 无法归因。

### 成本

Flash < 0.5KiB。每节点两次 `const` 读取，相对现有两次 RAM 读取可忽略。RAM 不变。速度成本按 **未测、预期在噪声内** 写进 PR；用同一局面、同一节点上限比较 host 耗时，偏差 >10% 就要查表是否被放进了 RAM。

## 项 2：搜索功能与小置换表

### 不要做的事

不要「打开 alpha-beta」或「打开迭代加深」。它们已经在跑。不要定义上游的 `MCUMAX_HASHING_ENABLED` 而不改表长。不要为了 host Elo 把设备编成另一套宏：Elo 要解释设备将要跑的那一档。host 可以额外编 2048/4096 做对照，默认与设备候选一致（1024）。

### 采用的补丁

- 新宏，例如 `MCUMAX_HASH_BITS`，默认 10（1024 项）。`0` 表示关闭，行为与今天相同。
- `HashEntry` 布局保持上游（key2、score、from、to、depth），`_Static_assert(sizeof == 12)` 与 `表长为 2 的幂`。
- scramble 1035 字节 `const`，提交进仓库。
- 替换策略先保持现在的「几乎总是覆盖，只有 depth==`MCUMAX_DEPTH_MAX` 的和棋锁保留」（595–608 行）。小表上做深度优先替换可以留到测量之后。
- `set_fen` / `mcumax_init` 只 `memset` 这张小表，并继续把 `completed_move` 清掉。截止补丁的语义不变：未完成层不能覆盖检查点。
- 链接 MAP 必须出现预期的 `.bss` 增量，不能出现 `(1<<24)`。固件 size 报告随 PR 贴出。
- 设备是否真正打开 1024：看测量门槛（下一节）。门槛失败则设备宏为 0，host 实验表不进 `sdkconfig`。

### 为何仍然值得做

没有表时，每个节点的 `iter_square_from` 从 0 开始（223–227 行），只靠本节点的迭代加深记住最佳着。表提供的是**别的节点**写下的最佳着，用来减少 alpha-beta 的第一个着法失败。Muller 的 2.5 倍是大表 + 6 层。1K 项在开局冲突很高，收益主要是中局排序和少量截断。预期 +20～+80 Elo，或同等节点下多完成一小段深度。残局「10 层变 30 层」不会在 12KiB 上发生。

### 集成点

`third_party/mcu-max/mcu-max.c` 与两边的 CMake（允许 `-DMCUMAX_HASH_BITS=11` 做 host 对照）。不改公共 API。adapter 不用知道表的存在。

### 风险

- 哈希键错误会返回非法着。core 复验仍会拒绝，但会变成 timeout/fallback，Elo 变差且用户看到保底着。单测：随机合法序列上，引擎着法必须 `chess_make` 成功；mate-in-1 仍吃。
- 表未清、上一局的着法漏进新局。每个 `set_fen` 清表，并用「新搜索立即截止不得复用旧检查点」的现有测试盖住。
- BSS 吃掉 32KiB heap 门槛。这是设备开关，不是 host 开关。
- 将军延伸 + 更深的有效搜索增加栈。TT PR 的设备部分要记录 Hard、连续将军、升变三个局面的栈水位。未授权刷机则设备栏写 NOT RUN，host 部分仍可合并。

## 项 3：开局库

### 为何不能只靠 PST

深度护栏 12、4M 节点仍然 1.c3。PST 也许能改变评估，但在它被 A/B 证明之前，用户看到的第一步仍是 c3。库在 adapter 里，搜索一行都不用改，失败时退回搜索。

### 方案

| 方案 | 取舍 |
|---|---|
| 完整 Polyglot 解析 | 拒绝。格式、随机权重和外部书的许可都超出需求 |
| 从起始局面走的着法序列 trie | 转置（1.e4 e5 2.Nf3 与 1.Nf3 e5 2.e4）会 miss。能用，但位置键更小也更稳 |
| **位置键，线性表** | **采用**。约 60–90 个局面，每项 8 字节键 + 最多 4 个 `(from,to,weight)`。大约 1–2KiB Flash |

键：对 `chess_position_key` 的 34 字节做 FNV-1a 64 位（core 已有这个键，不新造 Zobrist）。生成期断言无重复键。运行期冲突或非法着：当 miss，进入搜索。

内容手写，UCI，两边都写，每个局面我们只存**我方**要走的着。建议覆盖到双方合计约 10 个半回合，而不是 20 步理论：

- 白方主变（seed 0 的第一步必须是 `e2e4`）：意大利/西班牙方向的 `e4 e5 Nf3 Nc6`，以及 `e4 c5`、`e4 e6`、`e4 c6` 各一条短线；`d4`、`Nf3`、`c4` 各一条。
- 白方第一步权重：e4 最高，然后 d4、Nf3、c4。seed 0 选权重最大者，保证 harness 稳定。
- 黑方对 e4：e5、c5、e6、c6 四选（seed 0 固定其中一个，建议 e5，避免「库只是把 c6 写成正式卡罗」却仍没有后续）。对 d4：d5 与 Nf6。对 c4：e5 与 Nf6。对 Nf3：Nf6。
- 对 e3、c3 和其他第一步：**不要**指望搜索，库里放一个应答（d5 或 Nf6）。430 局说明搜索会走 c6。
- 不收录弃子开局。每一着生成时用 core 或 python-chess 确认合法；运行时再 `chess_make` 一次。

多样性：

- `chess_ai_request` 增加 `uint32_t book_seed`。`memset` 的 0 就是「主变」。
- CLI 的 `chess_ai_suggest` 不新增参数，内部 seed 0。`tools/elo_match.py` **不用改**。
- 产品在 `start_ai_game`（`main/chess_app.c`）生成一次种子，存在 RAM，整局不变。`maybe_request_ai` 今天用 `s_generation * 2654435761` 填 `easy_seed`（449 行），generation 每步都变，**不能**拿来当开局种子，否则第一步 e4、第二步又跳到另一条库。
- 第一版不改存档 schema。读档后续走时 seed 视为 0（主变）。若要续盘仍走同一随机分支，留到后面的存储 PR。这是开放问题，不阻塞库本身。
- 现有 LCG（`chess_ai.c` 86–88 行）可以复用，按权重区间取着。单测锁住 seed 0 和 seed 1 的着法。

Easy：先查库，再走现有 15% 替换（213–230 行）。Easy 仍会偶尔走出库。这是让子，不是事故。若希望 Easy 也稳定开局，把「库命中则不做 15%」写成后续开关，默认保持今天的让子行为。

命中库时不再调用搜索，墙钟和节点都省下。未命中与今天完全一样。Hint 若走 `chess_ai_run_job`，同样查库；当前枚举里没有单独的 Hint 档（`chess_ai.h` 19–23 行），文档表格里的 400ms 不在 `chess_ai_level_budgets` 里。实现时以实际调用点为准，不要新造一档。

### 集成点

- 新文件：`components/chess_ai/opening_lines.txt`（手写着法，文件头写明原创、无 Polyglot）、`tools/gen_opening_book.py`（host，可调用已有 python-chess 依赖生成 C 表）、生成的 `opening_book.c`。
- `chess_ai_suggest` 与 `chess_ai_run_job` 在 `mcumax_search_best_move` 之前查表。
- `main/chess_app.c` 的 `start_ai_game` / `maybe_request_ai` 填稳定种子。
- 来源说明放 `components/chess_ai/BOOK_SOURCES.md`，不写进 UI 的 `assets/SOURCES.md`。
- 不改 `third_party/mcu-max`，从而 perft 的规则面不变。

### 风险

- 库着法在升变或易位上写错：core 拒绝后必须搜索，不能 fallback 到 `legal[0]` 除非搜索也失败。
- 种子每步变化：用单测固定「同一 seed、连续两步仍在同一条线」。
- 库太浅：第 11 步又回到兵海。所以 PST 仍是下一 PR，库不替代评估。
- 生成表与 txt 漂移：测试里重算键集合，或把生成物和 txt 的哈希写进测试。

### 成本

Flash 1–2KiB。RAM 0（`const`）。命中时省掉整个搜索；未命中多一次 34 字节哈希和几十次比较，相对 100ms 可忽略。

## 可选改进（按预期 Elo / 代价，都不在前三个 PR）

预期仍是判断，不是测量。排在已采用的三项之后。

| 序 | 项 | 预期 | 代价 | 说明 |
|---|---|---|---|---|
| 1 | 杀手着法，每层 2 个，静态数组，层数封顶 | +10～+40 | < 256 字节 BSS | 表 miss 时补排序。不要放进递归帧。层索引必须封顶，避免将军延伸写爆数组 |
| 2 | 扫描两遍：先吃子再安静着 | +10～+40 | 热循环改动，节点变慢 | micro-Max 不生成着法表。两遍扫描比重写生成器便宜。先有 PST 和 TT 的 A/B 再决定 |
| 3 | 放宽将军延伸 | 战术局面 +20～+60，也可能超时 | 栈和时限 | 现有延伸又窄又可能不减深度。没有设备栈数据之前不动 |
| 4 | 完整 PeSTO 替换 | 未知；TSCP 的 +200 不能引用为本引擎 | 改增量 `score` 约定，许可未清 | 仅当前三项的 CI 下限仍低于目标时再开题 |
| 5 | 内部节点 MVV-LVA 排序、独立静止搜索 | 低 | 高 | 叶节点 MVV 和吃子静止已经在。再做一层生成器不符合这只引擎的结构 |
| 6 | 历史启发、aspiration 窗口 | +0～+30 | 中 | 现有窗口只做了 `alpha < score` 时减 1。完整 aspiration 容易 fail-low 后丢着 |
| 7 | 换引擎或 NNUE | 棋力上限明显更高 | 超出 RAM/Flash 与 M2 范围 | 不在本计划 |

## 里程碑（各自一个小 PR）

顺序是依赖，不是工期。每步 host 测试绿了再开下一步。设备测量单列，没有刷机授权就保持 NOT RUN。

### PR1 — 开局库

**文件：** `components/chess_ai/opening_lines.txt`、`opening_book.c`、`BOOK_SOURCES.md`、`tools/gen_opening_book.py`、`components/chess_ai/chess_ai.c`、`include/chess_ai.h`、`main/chess_app.c`、`tests/host/test_ai_worker.c`（或新的 `test_opening_book.c`）及 host CMake。

1. 先写失败测试：起始局面 seed 0 得到 `e2e4`；对 e4/d4/c4/e3 的 seed 0 应答不是 `c7c6`（除非该线的主变故意是 c6，本计划主变不是）；非法库着法退回搜索；seed 0 连续两步留在同一条线；seed 1 在有多个权重时可以不同，且着法合法；Easy 15% 仍可离开库（锁住现有让子）。
2. 生成表并接入 `suggest` 与 `run_job`。CLI 不增加参数。
3. `start_ai_game` 写一次稳定 `book_seed`，不要用每步 generation。
4. 跑 host ctest（含 perft）、`python3 tools/check_docs.py`。文档检查不是 perft，也不是固件。
5. Elo：同一台机器、Stockfish 17.1、0.2 秒、skill 0 与 skill 3。本方至少跑 CLI 默认 200k/4（与 2026-10-08 可直接比）和 **当前** Normal 护栏 5M/8。每格 ≥200 局 skill 0、≥100 局 skill 3。另记一列 1M/8，只为对照旧的 docproxy，不作为产品档。
6. 提交信息类似 `feat(ai): add a seeded opening book in front of search`。

**出口：** 单测锁住 e4 与非 c6 应答；perft 仍绿；Elo 报告贴在 PR，允许增益落在区间里。点估计相对同机重跑的基线下降超过 50 Elo 且区间不跨 0，则不合。

### PR2 — 分棋种 PST

**文件：** `third_party/mcu-max/mcu-max.c`、`third_party/mcu-max/SOURCES.md`、host 战术/开局测试。不改 core。

1. 失败测试：在 **库关闭** 的构建或直接调 `mcumax_search_best_move` 时，起始局面 200k/4 的着法属于 `{e2e4,d2d4,g1f3,b1c3,c2c4}` 之一。今天这个测试会失败（c2c3），用来证明表真的改了评估，而不是库在兜底。
2. 保留 393–416 行兵逻辑和截止补丁。替换 367–371 行。
3. 库打开时的产品路径仍走 PR1。PST 测试要有一条不经库的路径，否则看不出评估变化。
4. 同一 Elo 协议。和 PR1 的结果比，不和 2026-10-07 的 10 局比。
5. 一步将死、避免一步被将杀、截止后保留吃后（`docs/ai-integration.md` 已有的 Rxe4 局面）保持通过。

**出口：** 无库时开局离开 c3；战术 fixture 不回退；Elo 点估计不显著变差。

### PR3 — 小置换表

**文件：** `mcu-max.c`、CMake、`SOURCES.md`、一张 host 测试。默认 `MCUMAX_HASH_BITS=10`。

1. 关闭（bits=0）时着法与 PR2 的固定种子局面一致，证明宏没把搜索改坏。
2. 打开后：mate-in-1、随机着法合法性、截止检查点、`set_fen` 之后立即停止不得沿用旧着。
3. host 对照 0 / 10 / 12 bits，固定局面记录完成层与节点。Elo 用 bits=10，和 PR2 比。
4. 固件：`idf.py size` / MAP。`.bss` 增量约 12KiB + 对齐，不得出现巨表。
5. 设备（**仅当任务明确授权刷机**）：Easy/Normal/Hard 各 ≥30 次，局面含开局、中局、残局、升变、连续将军。记录 CPU 频率、nodes、完成层、墙钟、callback 次数、`uxTaskGetStackHighWaterMark`、`heap_caps_get_free_size` / largest block（8-bit）。门槛：最坏 free heap 仍 ≥32KiB，最大块 ≥16KiB，栈剩余 ≥1KiB 且 ≥配置的 25%，搜索超调仍满足 `docs/performance.md`。任一失败就把设备默认改回 bits=0，host 默认可以保留已证明有 Elo 的档，但 PR 里写明两边不一致。

**出口：** host 合法性测试绿；MAP 无巨表；设备要么过门槛，要么明确关闭并写 NOT RUN / FAIL。未授权则设备整段 NOT RUN，host 部分可以先合。

### PR4 — 可选杀手着法

仅当 PR1–PR3 之后、对 skill 3 的 Elo 差点估计仍比 PR3 低，且用户还要继续压搜索。单独 PR，便于回滚。不做 MVV 重写。

## 测试计划

规则与引擎分开报告。

| 层 | 跑什么 | 通过标准 |
|---|---|---|
| 文档 | `python3 tools/check_docs.py` | PASS。这不是 perft，也不是固件 |
| 规则 | 现有 host perft + ASan/UBSan ctest | 全绿。PR1–PR3 不改 `components/chess_core`。若 diff 碰到 core，perft 必须重跑并说明原因 |
| 引擎单测 | 开局 seed、非法库着、TT 关闭回归、mate-in-1、避免送后、截止保留 Rxe4、取消不落子、generation 过期拒绝、欠升变仍由 core 接受 | 与今天 adapter 测试同一套，另加上面的新断言 |
| host Elo | `tools/elo_match.py`，不改锚点表 | 见下 |
| 固件构建 | 现有 ESP-IDF CI / `idf.py size` | PR3 起贴 size。构建通过 ≠ 棋力通过 |
| 设备 | 频率、nodes、层、栈、heap、超调 | 未授权 = NOT RUN。不要用 host Elo 填设备栏 |

Elo 协议（与 2026-10-08 可比，并覆盖今天的护栏）：

- 二进制：release 优化的 `chess-cli`，不要用 ASan 那次当评级（2026-10-07 的 skill 5 样本就是被 ASan 拖慢后中止的）。
- 对手：Stockfish **17.1**，skill 0 与 3，每步 0.2 秒，Threads 1，Hash 16。版本写进 PR。
- 本方：`--nodes/--depth`。PR 里至少包含 `200000 4`（旧基线）和该 PR 想代表的产品护栏。时间预算在设备上才是主限制；host Elo 测的是节点/深度护栏。
- 颜色按对局序号交替，和现有脚本一致。
- 局数：skill 0 ≥ 200，skill 3 ≥ 100。30 局的 Hard vs skill 3 区间是 [-39, +166]，盖不住 50 Elo 的改动。200 局大约能把「+80 Elo 量级」和噪声分开；+40 仍可能不显著。不把「区间必须完全高于 0」当合入条件，避免小幅真增益永远合不进去。合入条件是：没有中止局；点估计相对**同机**基线跌幅不超过 50 且该跌幅的区间排除 0 时才算回退。
- 开局库 A/B 使用 seed 0。另用 20 个不同 seed 做多样性测试（至少 2 种白方第一步，全部合法），不把这 20 局混进 Elo。
- 输出胜/和/负、得分、相对对手的 Elo 差和 95% 区间。lore 锚和 SF 多项式反解可以附注，不能写成 Chess.com 或 FIDE。
- 任一局中止：保留 PGN，拒绝出整组评级（脚本已如此）。

设备节点测量方法（授权之后）：

1. 日志里打印 `esp_clk_cpu_freq()`，不要从本仓库 `sdkconfig.defaults` 推断。
2. 固定三个 FEN（起始、一个中局、一个残局），Normal 与 Hard 各跑，callback 开/关各一次，记录节点与墙钟。关闭 callback 的那次只在实验固件，不进产品。
3. 用「有 callback 的 nodes/s × 档位毫秒」估计设备上真正走完的层，和 host 200k/1M/5M 对照。这个数 measured 之前，产品棋力以设备栏 NOT RUN 为准。

## 开放问题

1. 下一阶段的成功标准，是「开局不再固定 c3/c6，且 host 对 SF skill 3 的得分高于 2026-10-08」，还是仍以 Chess.com 快棋 1200 的真人陪练为合入线？本计划把 1200 留在测量协议之外。
2. Easy 的 15% 是否允许推翻开局库？计划默认允许，以保持让子。
3. 读档后开局种子归零是否可接受？第一版不改 NVS schema。
4. 12KiB BSS 若挤占 32KiB heap 门槛，是否接受设备无置换表、只保留 host 表？计划默认接受，并保持两边宏显式写明。
5. PST 数值用原创小表，还是等 Michniewski/PeSTO 的书面许可后再整表导入？计划默认原创，避免 GPL 和未授权转载。
6. 刷机授权是否会单开给 PR3 的栈/heap/nodes 测量？没有授权，设备列保持 NOT RUN。

## 参考

- 本仓库：`third_party/mcu-max/mcu-max.c`、`mcu-max.h`、`SOURCES.md`；`components/chess_ai/chess_ai.c`；`apps/cli/chess_cli.c`；`main/chess_ai_task.c`；`sdkconfig.defaults`；`partitions.csv`；`docs/hardware.md`；`docs/performance.md`；`docs/ai-integration.md`；`docs/architecture.md`；`docs/roadmap.md` 风险表「默认 hash 巨表」。
- H.G. Muller，micro-Max 说明（哈希大小、2.5 倍与残局、中心分是暂态项）：<https://home.hccnet.nl/h.g.muller/download.html>，<https://home.hccnet.nl/h.g.muller/progress.html>，<https://home.hccnet.nl/h.g.muller/eval.html>。
- CPW *Simplified Evaluation Function*、*PeSTO's Evaluation Function*、*Piece-Square Tables*。形状参考，不复制表。
- Stockfish `Skill` / `UCI_Elo` 多项式与 PR 4341 的校准对局。用于解释 2026-10-08 分析脚本，不作为本引擎的等级证书。
- ESP32-C3 数据手册：400KB SRAM（16KB cache）。IDF heap 文档：`MALLOC_CAP_8BIT` 的 free 与 largest block。
- 2026-10-08 对局包：430 局，0 中止，摘要即上文表格。PGN 不入库。
