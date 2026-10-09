# 来源、核实记录与第三方许可

来源核实基线：2026-10-06；BSP 与 mcu-max 已按下列固定版本导入。上游后续变化须重新审阅，禁止仅跟随 main 漂移。当前实现与验收范围见[开发状态](status.md)。

| 来源 | 锁定/用途 |
|---|---|
| [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport/tree/33d3d1d93a1125b356b47b6d83a7a60121be801e) | `33d3d1d93a1125b356b47b6d83a7a60121be801e`，BSP/构建基线，MIT |
| [pin definitions](https://github.com/FoloToy/ai-passport/blob/33d3d1d93a1125b356b47b6d83a7a60121be801e/components/bsp/include/bsp_pins.h) | GPIO、SPI速度、按键窗口与时序的源码真相 |
| [display header](https://github.com/FoloToy/ai-passport/blob/33d3d1d93a1125b356b47b6d83a7a60121be801e/components/bsp/include/bsp_display.h) | 半径30、LVGL初始化/lock契约 |
| [hardware guide](https://github.com/FoloToy/ai-passport/blob/33d3d1d93a1125b356b47b6d83a7a60121be801e/docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md) | buffer/pool、共享总线、无PSRAM限制 |
| [build guide](https://github.com/FoloToy/ai-passport/blob/33d3d1d93a1125b356b47b6d83a7a60121be801e/docs/development/engineering/build-and-test.md) | IDF5.5.3、干净验证、0x0镜像与NVS风险 |
| [mcu-max header](https://github.com/Gissio/mcu-max/blob/aa03caffce50729566b5db6965bf735c31f33eea/src/mcu-max.h) | `aa03caffce50729566b5db6965bf735c31f33eea`，1.0.6 API、欠升变限制 |
| [mcu-max source](https://github.com/Gissio/mcu-max/blob/aa03caffce50729566b5db6965bf735c31f33eea/src/mcu-max.c) | 回调/停止、FEN0x88方向、hash默认关闭与巨表 |
| [mcu-max LICENSE](https://github.com/Gissio/mcu-max/blob/aa03caffce50729566b5db6965bf735c31f33eea/LICENSE) | MIT；集成时保留Gissio版权全文 |
| [FIDE Laws](https://handbook.fide.com/chapter/e012023) | Article3走子、5终局、9申报/自动和棋；无需复制全文 |
| [Perft Results](https://www.chessprogramming.org/Perft_Results) | benchmark FEN与节点值；非引擎依赖，不复制文章 |
| [ESP-IDF NVS v5.5.3](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/api-reference/storage/nvs_flash.html) | set_blob/commit、错误、空间与掉电语义 |
| [AI Passport plays](https://ai-passport.folotoy.cn/plays/) | 社区目标；本次未对所有现有玩法逐项查重 |
| [lichess-org/chess-openings](https://github.com/lichess-org/chess-openings/tree/a6189a30dc273ccb21fc2536a9a2fefd5592a67a) | `a6189a30…`，CC0 开局名/PGN 线源；`tools/data/chess-openings/` |
| [Lichess opening explorer](https://explorer.lichess.org/lichess) | 书节点着法权重；快照 `tools/data/opening_explorer_cache.json`（2026-10-09；生成时 API 不可达则用文档化 fallback） |

## 事实与工程选择

官方源码确认240×320、8MiB/无PSRAM、9600B DMA、24KiB pool、30px遮罩；本项目选择30+240+50、C11 core、自绘棋盘、双槽NVS、难度时间预算与具体性能门槛。工程选择尚未真机证实。

mcu-max README说可配置hash不意味着当前巨大默认值适合本设备；头文件明确欠升变限制。接口没有完整层结果保障、多候选评分或线程安全承诺，设计保留验证spike和fallback。

## 导入与发布许可清单

导入BSP与引擎时各自带LICENSE，记录路径/SHA/修改；字体选择允许嵌入再分发的字体并保留许可证；棋子采用原创或明确许可的mask。第三方素材不自动归本仓库MIT。

Lichess谜题许可与数据版本在M3才核实；本次未下载/分发题库。python-chess只作外部测试工具，GPL不能被当成设备MIT代码。社区品牌仅用于兼容说明，不暗示官方背书。
