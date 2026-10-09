# 测试策略与证据

## 当前与未来

当前已有文档检查、C11 host/CTest、perft CLI、字体覆盖检查、主机 AI 对局探针（`tools/elo_match.py`，`tests/test_elo_match.py`）和固件 CI。`tests/perft/cases.json` 是带来源的输入与期望；实际执行结果以 Actions/host 日志为准。PR #2 的 host/CLI 与固件检查通过，但不得把这些结果说成完整真机验收。主机强度成批测量方法与结果见 [AI 集成](ai-integration.md)。当前边界见[开发状态](status.md)。

## perft 门槛

统计固定深度的叶节点，不因重复/50步游戏结果提前停止；不用 AI 搜索来代替合法着 perft。初期不加缓存，降低掩盖错误风险。错误时输出 FEN、深度与逐根着 UCI divide；对每个根着递归核验，而非改期望凑数。

| 输入 | d1 | d2 | d3 | d4 | d5（扩展） |
|---|---:|---:|---:|---:|---:|
| start | 20 | 400 | 8902 | 197281 | 4865609 |
| kiwipete | 48 | 2039 | 97862 | 4085603 | 193690690 |
| position3 | 14 | 191 | 2812 | 43238 | 674624 |
| position4 | 6 | 264 | 9467 | 422333 | 15833292 |
| position5 | 44 | 1486 | 62379 | 2103487 | 89941194 |

完整 FEN 与核实来源放在 [cases.json](../tests/perft/cases.json)。PR 必过全部 d1..d4；扩展 gate 跑 start d5、position3 d5 与其他较深 position，较贵 Kiwipete d5 放手动/夜间任务。无需真机跑深 perft。

## 回归矩阵

| 类别 | 必测与断言 |
|---|---|
| 王与攻击 | 王相邻非法、双将仅王走、钉住、王不得走入兵攻击格 |
| 易位 | 双翼、路径堵塞、当前/途中将军、原角车被吃、王车回来不恢复 |
| 吃过路 | 双步后可吃、过期、移除正确兵、暴露己王非法、重复 key 的合法 ep |
| 升变 | 白黑两边、推进/吃子、Q/R/B/N 共8变体；取消 UI 不改棋局 |
| 终局 | 将死、逼和、支持的死局、不能自动误判的 NN/异色象 |
| 和棋 | 次数2/3/5、计数99/100/149/150、兵/吃子归零、预选着申报、将死优先 |
| FEN | 六字段 round-trip、非法字符/长度/王/权利/数字溢出、失败状态不变 |
| 可逆性 | 所有 fixture 的合法着 make/unmake 字段一致；历史更新仅正式提交 |
| 容量 | 256着 buffer 边界、151历史边界、计数溢出、JSON/FEN 截断 |

核心随机性质：固定种子，从初始局面走10000组合法序列（每组最多150半步或终局）；每步验证王安全、unmake、FEN round-trip、历史编码。种子/FEN/走法可重放。额外使用固定版本 python-chess（GPL，仅测试进程、不链接或带入设备固件）对 legal UCI 集合与 make 结果差分；锁版本、存种子，不能把它作为游戏运行依赖。复杂死局不以其 insufficient_material 自动证明完整支持。

## model / codec / AI

UI reducer 在 host 无 LVGL 环境做事件序列测试：空候选、wrap、升变、取消、新局确认、长按 release、重复事件、队列 overflow。载入/新局/退出后投递旧 AI 结果，必须拒绝。

codec 用假 NVS backend 模拟 write/commit 前后掉电、bit flip、未知版本、槽缺失、最大长度、序号与 CRC。恢复后的 side、rights、ep、half/fullmove、history、result/settings 与原始等价；三次重复必须跨重启仍可申报。

语言模块在无 LVGL host 测试中覆盖英文/简体中文映射、未知语言回退、来回切换和所有文案非空。`tools/check_chess_font_coverage.py` 验证生成字库覆盖所有翻译汉字，并由 GitHub Actions 执行；真机再验证主菜单切换、整页中文显示、继续对局与重启后语言保持。

AI fake clock 在每次 callback 推进时间；deadline 到达必须停止并返回有效着或明确失败；单独测 CPU yield、取消、转换棋盘方向、引擎非法结果、升变与 underpromotion 局面。真机才能证明实际 deadline 和 watchdog。

开局库 host 测试（`test_opening_book`）：`book_seed == 0` 起始局面为 `e2e4`、对 `e4` 不应默认 `c7c6`、库命中不启动搜索（`callback_count == 0`）、转置同键、Easy 在库内不走 15% 随机、读档路径 `book_enabled` 为 false（`test_app`）。表由 `tools/gen_opening_book.py` 从 `opening_lines.txt` 生成；改线后须重跑生成器并提交 `opening_book.c`。

## 编译与设备

host `-Wall -Wextra -Werror`，Clang ASan/UBSan；不以 sizeof 相等证明二进制序列化可移植。device test 记录屏幕照片/视频、按钮序列、串口日志、固件 SHA256、heap、最大块、stack 最低水位和搜索 p95。官方模拟器可辅助 UI，但不证明 ADC、NVS掉电、内存或RF实机效果。

每次发布在 [验收表](acceptance.md) 填实际结果与证据路径。回归失败必须修规则，禁止跳过特殊走法以使 perft 通过。
