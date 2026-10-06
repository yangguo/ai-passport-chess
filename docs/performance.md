# 性能与内存预算

下表为**工程预算与发布门槛**，不是已测结果。ESP32-C3 没有 PSRAM；所有新增 static/stack/heap 都计入内部 RAM。仅“总 heap 还多”不够，必须看最大连续块、capability与LVGL独立池。

## M1 / M2 初始预算

| 资源 | 初始目标 | 方法 |
|---|---:|---|
| BSP LCD DMA | 9600B 单 buffer | 沿用基线；不用双全帧 |
| LVGL pool | 24KiB，不提高默认值 | 稳态 used ≤18KiB；峰值 ≤21KiB |
| core position+game history | ≤6KiB static | 151×34历史约5KiB；sizeof/MAP核对 |
| 合法着与生成 scratch | ≤4KiB | 固定容量，禁止递归每层256着数组 |
| app task stack | 起点4KiB | ESP-IDF stack单位核对；测最低余量 |
| storage stack+编码缓冲 | 起点3KiB + ≤6KiB | 只保留一个编码 buffer，不多复制大快照 |
| AI task stack | 起点8KiB | 搜索递归必须测；不是安全保证 |
| AI TT | 0（M2） | hashing disabled；MAP不得出现巨表 |
| 棋子 | 5400B基础mask，Flash | 不复制全部像素到heap |
| 全屏 RGB565 canvas | 0 | 240×320×2=153600B，禁止分配 |

预算不含 BSP/IDF 固有占用，表不能相加就声称“可装下”。实际固件的 .bss/.data、任务栈、系统heap与LVGL是统一RAM压力；NVS缓存也计入。M0测干净baseline，M1/M2逐项记录差值。

## 可量化门槛

- M1 最坏 UI/存档场景内部 free heap ≥32KiB，最大8-bit块 ≥16KiB；若未来有更大分配，最大块至少该分配需求+25%余量。发布门槛随真实分配证据修订，不准为过线只改数字。
- LVGL pool剩余 ≥3KiB；每任务 stack最低剩余 ≥1KiB且≥配置25%；启用 stack overflow check，持续2小时无panic/watchdog/OOM。
- 物理短按到焦点可见：p95 ≤100ms；确认到显示新局面 p95 ≤150ms（不计等待松开的按键时间，另记按下到CLICK时延）。至少100次测量。
- AI超调 p95 ≤max(50ms,预算×10%)，最大≤预算+200ms；取消到ACK p95≤100ms，最大≤200ms。若上游达不到，修适配或换引擎，不能隐藏长停顿。
- 保存完成 p95≤500ms，最大≤1s；期间焦点输入仍符合目标。搜索不与Flash写同时跑是初期可接受策略，记录排队时间。
- 2小时脚本化/手动运行后 free heap相对热身后下降≤1KiB，最大块不持续退化；连续进出菜单100次无资源泄漏。

## 测量计划

日志采样点：boot、UI创建、最大合法着局面、保存编码/写入、搜索首层/最深、退出、恢复。使用 `heap_caps_get_free_size`、`heap_caps_get_largest_free_block`、`heap_caps_get_minimum_free_size`（记录capability），LVGL pool监视接口、task stack高水位（记录单位换算）及链接 MAP。

AI固定初始局面+中局+残局+升变+将军应对，Easy/Normal/Hard各至少30次，记录实际CPU频率、nodes、完成层、总墙钟、yield/取消延迟。不限时性能对比只在host；设备仍有deadline与watchdog护栏。

局部刷新只 invalidate 焦点旧/新格、上步格、被吃兵/易位车格和需要改变的header/footer；关闭多余动画、阴影、透明大图层。优化每次先量测瓶颈，不以提高pool/关闭watchdog作修复。电量和功耗测量另记亮度、无线开关、电池状态；不预先承诺续航小时数。
