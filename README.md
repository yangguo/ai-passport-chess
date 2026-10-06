# AI Passport Chess · 掌上国际象棋

面向 **FoloToy AI Passport** 的离线国际象棋项目：240×320 竖屏，三键循环选择，完整走子规则，纯 C 规则核心，先本地双人，再加入 MCU 端休闲 AI。

**当前状态：设计与开发契约 v0.1，尚无棋局实现、可构建固件或真机验收。** 初始仓库的 CI 仅验证文档、链接与 perft 数据结构；绿色结果不代表规则测试通过。设计中的接口、路径、预算及未来命令均明确标记为待实现。

## 产品范围

- M1：本地双人，共用设备；白方在下，棋盘不自动翻转；无限时。
- 棋盘 `x=0..239, y=30..269`，每格 30px；上方 30px header，下方 50px footer，避开 BSP 四角遮罩。
- UP/DOWN 循环可走棋子、合法落点；OK 确认；长按 OK 取消/返回；后、车、象、马四种升变。
- 规则核心拥有局面与合法走法；UI、存档、AI 只能请求状态转换，不得另行判定合法性。
- M2：mcu-max 适配与提示。AI 返回走法仍须经核心验证；不承诺 Elo 或固定搜索层数。
- M3/M4：有来源的谜题、BLE 双机、复盘与 PGN，独立验收后开放。

## 阅读顺序

| 文档 | 内容 |
|---|---|
| [设计总览](docs/plans/2026-10-06-chess-design.md) | 目标、方案选择、已定决策与阶段范围 |
| [架构](docs/architecture.md) | 模块、目录、所有权、任务与错误路径 |
| [硬件](docs/hardware.md) | 官方基线、显示/输入/总线边界 |
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

这条命令检查本地文档链接、必要交付文件、JSON 基准结构和 Markdown 围栏。后续 host tests 与固件命令见构建文档，它们目前尚不存在。

## 开发约定

阅读 [AGENTS.md](AGENTS.md) 和 [CONTRIBUTING.md](CONTRIBUTING.md)。主分支 `main`；实现从 `feature/core-rules` 开始。采用 ESP-IDF **v5.5.3** 与锁定的官方 BSP，保留来源和 MIT 声明；不把官方演示菜单当作产品界面。硬件和引擎依据见 [来源清单](docs/sources.md)。本项目是独立社区项目。

源文档与后续原创代码采用 MIT；第三方引擎、BSP、字体、棋子图形及谜题的授权独立记录。当前未导入第三方源码或素材。
