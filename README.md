# AI Passport Chess · 掌上国际象棋

面向 **FoloToy AI Passport** 的离线国际象棋游戏：240×320 竖屏，三键操作，纯 C 规则核心，支持本地双人和离线 AI。

**当前状态：** 规则核心、可玩棋局、AI、中文切换、执白/执黑选择、存档及省电功能已合入 `main`。主机回归和 ESP-IDF 构建通过；部分真机项目仍待验收。AI 已在主机上对 Stockfish 17.1 完成 430 局 CCRL 尺度强度对照（非 Chess.com 评级）；Chess.com 快棋 1200 目标仍未做平台验收。详见[开发状态](docs/status.md)和[验收记录](docs/acceptance.md)。

## 当前功能

- 本地双人和人机对弈；人机可选择难度及执白/执黑，玩家一方在棋盘下方，执黑时 AI 白方先走。
- 棋盘 `x=0..239, y=30..269`，每格 30px；上方 30px header，下方 50px footer，避开 BSP 四角遮罩。
- UP/DOWN 循环可走棋子、合法落点；OK 确认；长按 OK 取消/返回；后、车、象、马四种升变。
- 语言切换：长按进入暂停菜单并返回首页，在主菜单选择“语言”可即时切换英文/简体中文；选择会保存到当前棋局存档，重启后保留。
- 规则核心拥有局面与合法走法；UI、存档、AI 只能请求状态转换，不得另行判定合法性。
- 暂停菜单“新对局”：先选双人／人机；“返回首页”明确返回主菜单。
- 人机新局：选择难度后选择“执白／执黑”；玩家一方始终在下方。执黑时 AI 执白先走，重启后保留颜色、难度和棋盘方向。
- 英文/简体中文、亮度调节、无操作自动变暗与休眠；设备唤醒及最新人机/显示交互仍按验收记录逐项确认。

## 尚未开发

悔棋/提示、离线谜题、复盘与 PGN、棋钟、BLE 双机，以及启动器多玩法槽位适配尚未实现。AI 目标棋力也尚未完成平台评级。具体状态和建议顺序见[开发状态](docs/status.md)。

安装可选择完整固件或兼容的多玩法启动器槽位。槽位兼容性尚未验证；完整固件会替换当前玩法。见[安装与运行](docs/deployment-model.md)。

## 阅读顺序

| 文档 | 内容 |
|---|---|
| [开发状态](docs/status.md) | 已实现、已验证、待真机验收和未开发功能 |
| [设计总览](docs/plans/2026-10-06-chess-design.md) | 目标、方案选择、已定决策与阶段范围 |
| [架构](docs/architecture.md) | 模块、目录、所有权、任务与错误路径 |
| [硬件](docs/hardware.md) | 官方基线、显示/输入/总线边界 |
| [安装与运行](docs/deployment-model.md) | 独立固件、多玩法槽位、返回与恢复 |
| [UI/交互](docs/ui-interaction.md) | 布局、状态机、三键、升变与菜单 |
| [规则核心](docs/chess-core.md) | 数据结构、API、特殊规则、和棋与 FEN |
| [测试](docs/testing.md) | perft、回归、差分、存档与设备测试 |
| [AI](docs/ai-integration.md) | mcu-max 限制、期限、取消、降级 |
| [存档](docs/storage.md) | NVS 编码、重复历史、掉电与版本 |
| [资源预算](docs/performance.md) | 内存、延迟、测量与失败阈值 |
| [构建/CI/刷机](docs/build-ci-flash.md) | 现有检查、未来工具链、合并镜像与发布 |
| [实施计划](docs/plans/2026-10-06-implementation-plan.md) | 文件级步骤、测试、提交与依赖 |
| [验收标准](docs/acceptance.md) | 分层证据与发布门槛 |
| [风险与路线图](docs/roadmap.md) | 风险、待决事项、后续功能 |
| [来源](docs/sources.md) | 核实日期、上游 SHA、许可证 |

## 现在可以运行

需要 Python 3.10+；无第三方依赖。

```sh
python3 tools/check_docs.py
```

这条命令检查本地文档链接、必要交付文件、JSON 基准结构和 Markdown 围栏。host 回归可运行 `tools/test_host.sh`，CLI 回归可运行 `tools/test_cli.sh`，固件检查入口为 `tools/validate_firmware.sh`；详细环境与限制见[构建文档](docs/build-ci-flash.md)。

## 开发约定

阅读 [AGENTS.md](AGENTS.md) 和 [CONTRIBUTING.md](CONTRIBUTING.md)。主分支 `main`；实现从 `feature/core-rules` 开始。采用 ESP-IDF **v5.5.3** 与锁定的官方 BSP，保留来源和 MIT 声明；不把官方演示菜单当作产品界面。硬件和引擎依据见 [来源清单](docs/sources.md)。本项目是独立社区项目。

原创代码采用 MIT；已导入 BSP 与 mcu-max，第三方引擎、字体和图形素材的来源及授权分别记录。尚未导入离线谜题数据。
