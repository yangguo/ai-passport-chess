# AI Passport Chess Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 从文档仓库逐步交付规则正确、三键可玩的AI Passport本地双人国际象棋，再接入休闲AI。

**Architecture:** chess_core纯C11是规则唯一来源；应用串行拥有game，UI/storage/AI接收快照。ESP-IDF与LVGL在adapter层，host测试不依赖硬件。

**Tech Stack:** C11、CMake/CTest、Clang ASan/UBSan、ESP-IDF5.5.3、官方BSP/LVGL、NVS、mcu-max（M2）。

当前仅设计与文档检查；下列文件和命令属于实施任务。每任务按“先写失败测试→验证失败→最小实现→通过验证→单独commit”；长任务拆成以下逐项步骤，禁止在所有perft通过前接AI。

## Task 1：M0 导入最小ESP-IDF基线

**Files:** 新增根CMakeLists.txt、main/CMakeLists.txt、main/app_main.c、components/bsp/、sdkconfig.defaults、partitions.csv、dependencies.lock、tools/validate_firmware.sh；更新sources.md。

1. 临时检出sources锁定BSP SHA，列导入文件与LICENSE，不导入demo菜单。
2. 导入最小构建和BSP依赖，生成锁，保留USB console/8MiB/24KiB；关闭BLE。
3. app只初始化显示、三键、电量和简单新UI，按钮回调入队，实测长按事件。
4. 移植官方合并验证工具的完整依赖链并更改产物名；模拟不存在/旧产物时必须失败。
5. 在干净IDF5.5.3环境运行build、size、布局检查，记录baseline；提交 `build: establish passport firmware baseline`。

**出口:** F0 smoke；设备测试只有获得刷机授权后做，未执行明确标注。

## Task 2：host harness与位置/FEN

**Files:** 新增tests/host/CMakeLists.txt、test_core.c、components/chess_core/include/chess_core.h、chess_position.c、chess_fen.c；tools/test_host.sh。

1. 先建C11可执行tests，添加初始FEN字段断言与坏FEN不修改输出测试，编译应因缺API失败。
2. 实现枚举、64格position与有界FEN解析/导出。
3. sanitizer开启后跑FEN round-trip、计数溢出、双王/无王/非法字符/缓冲不足。
4. `cmake -S tests/host -B build/host -DCHESS_SANITIZERS=ON`；build；ctest；预期无失败与sanitizer错误。
5. 提交 `feat(core): add validated positions and FEN`。

## Task 3：普通走子与可逆执行

**Files:** 新增chess_moves.c；test_core.c、perft_main.c。

1. 先写兵/马/滑子/王攻击与pin fixture测试，期望合法UCI集合。
2. 实现is_attacked、伪合法、内部make/unmake和己王安全过滤；公共make失败不改状态。
3. 验证start d1=20、d2=400、d3=8902与所有普通着可逆性；缺特殊规则期间不能声明完整perft通过。
4. 为perft CLI增加`--fen --depth --divide`，输出uint64叶数与根着拆分。
5. 提交 `feat(core): generate legal moves with reversible execution`。

## Task 4：特殊走法与完整perft基准

**Files:** 修改chess_moves.c、test_core.c、perft_main.c；现有tests/perft/cases.json只读。

1. 为castle-through-check/rook-capture-rights/ep-pin/全部升变先写失败测试。
2. 按core文档实现易位、ep、Q/R/B/N与undo字段。
3. 增加读取cases.json的host runner（可用Python调用perft CLI，C核心无JSON依赖）。全部d1..d4逐项compare，错误打印divide。
4. 运行ASan/UBSan、定种子make/unmake/FEN性质；添加固定版本python-chess外部差分runner及依赖锁。
5. 全部通过后提交 `feat(core): complete special moves and perft gate`。

## Task 5：game终局、重复与申报

**Files:** 新增chess_game.c、tests/host/test_game.c。

1. 建三次/五次、99/100/149/150、预选申报、ep规范化、权利丢失fixture。
2. 实现34-byte精确key与151历史上限；不可逆边界清理；perft不用game终局。
3. 实现将死/逼和、死局白名单、申报/自动和棋优先级，记录复杂死局限制。
4. 运行随机合法序列与容量测试，确保终局后apply被拒绝。
5. 提交 `feat(core): add game outcomes and draw claims`。

## Task 6：纯C三键model

**Files:** 新增components/chess_ui_model/include/chess_ui_model.h、chess_ui_model.c、tests/host/test_ui_model.c。

1. 写事件序列断言：wrap、空候选、升变cancel、长按release、不提交预选着。
2. 实现reducer与候选index；仅输出command，不改core局面。
3. 在SELECT_TARGET同一promotion目标去重；四种promotion单独菜单。
4. 明确PAUSE预选着申报入口、确认默认取消、错误页与返回。
5. ctest通过，提交 `feat(ui): add deterministic three-button state model`。

## Task 7：存档codec与NVS adapter

**Files:** 新增components/chess_storage/include/chess_storage.h、chess_codec.c、chess_nvs.c、tests/host/test_codec.c。

1. 固定schema/CRC fixture与fake backend，覆盖未知版本、截断、bitflip和commit掉电。
2. 实现显式字节序编码/验证，单槽≤6KiB；包含规范化history和raw ep。
3. 实现A/B最高有效序号恢复，禁止初始化错误全擦NVS。
4. app dirty snapshot→worker→ACK，故障页不把未保存显示成成功。
5. host round-trip/跨重启重复通过；设备最大档500次与掉电测试后更新验收，提交 `feat(storage): add recoverable versioned chess saves`。

## Task 8：M1 UI/app集成

**Files:** 新增main/chess_app.c、chess_ui.c、chess_board_draw.c；assets/pieces/、assets/fonts/、assets/SOURCES.md。

1. 先用fake事件/快照验 app generation、game owner与保存ACK。
2. 建30+240+50页面与单棋盘绘制对象，导入有授权A8/中文字集。
3. 连接model/core/storage，处理ep/易位车格局部刷新；锁内不做I/O。
4. 干净固件构建与资源基准，获得授权后设备双人完整操作、NVS与2小时测试。
5. 更新acceptance实际数据；提交 `feat(app): deliver local two-player chess`，符合门槛后M1 prerelease。

## Task 9：M2引擎适配spike

**Files:** 新增third_party/mcu-max/（含LICENSE）、components/chess_ai/chess_mcumax_adapter.c、tests/host/test_ai_adapter.c。

1. 锁定SHA，hash关闭；用fakeclock写超时/取消/映射/非法候选失败测试。
2. 实现FEN导入、0x88转换、promotion Q转换、core最后校验。
3. 核查stop与完成层语义、callback频率、stack；不满足则单独补丁或换引擎，未验证不接产品。
4. 每次新局/载入/退出注入旧结果，必须拒绝；欠升变fixture证明core支持不被削弱。
5. 提交 `feat(ai): add bounded mcu-max adapter`。

## Task 10：AI产品与发布工具

**Files:** chess_ai.c、main/chess_app.c/chess_ui.c、tools/package_firmware.py、.github/workflows/ci.yml、未来release.yml。

1. 接入worker、ACK、fakeclock/yield、保底着、可重放Easy RNG；Hint不落子。
2. 设备按performance测每档≥30次，搜索+导航/存档/退出验收。
3. CI添加真实host/firmware/adapter job；release仅tag且所有job通过。
4. package输出manifest/校验和/ELF/MAP/分段/full，逐偏移核验同构建一致。
5. 重跑全gate并更新限制，提交 `feat: deliver measured offline AI and release artifacts`。

## 执行纪律

每小步2–5分钟：写一个case、实现一个分支、跑一项测试、审diff、commit；表中复杂任务分多次提交。实现分支建议 `feature/core-rules`、`feature/local-ui`、`feature/mcu-ai`，从main分别按依赖顺序推进。未授权时不刷设备、不投稿社区；已有创建仓库/初始提交授权覆盖D0操作。
