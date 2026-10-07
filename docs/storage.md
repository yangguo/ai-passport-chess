# NVS 存档与恢复

M1 一局一档及少量设置；使用独立 namespace `chess_v1`，不碰无线/身份等命名空间。ESP-IDF NVS API 依据见 [来源](sources.md)。

## 编码契约（待实现）

不保存 C struct、不存指针/LVGL/引擎状态。codec host 可运行。显式 little-endian 字段、定长边界和 CRC32：

| 字段 | 设计 |
|---|---|
| envelope | magic CHS1、schema u16、header_len u16、payload_len u32、seq u64、payload CRC32 |
| position | 32 bytes nibble board、side、rights、raw ep、u16 halfmove、u32 fullmove |
| game | mode、human_color、result/reason、history_count、不可逆边界标识 |
| history | 最多151个34-byte规范化局面，包含当前局面 |
| settings | 难度、语言（0 英文、1 简体中文）、亮度；未知语言按英文兼容 |

人机颜色直接使用 v1 已有 `human_color`（0 白、1 黑），不改变编码或 schema；旧档仍恢复玩家执白。应用同时恢复已保存的难度，棋盘方向由 mode 和 human_color 推导，不独立存储。

raw ep 用于 FEN，历史 ep 采用 core 合法可吃标准；两者不得混淆。预算单槽 ≤6KiB，A/B ≤12KiB 逻辑数据，24KiB NVS 是否有足够日志/GC空间必须在最大存档下实测。超过长度立即报容量错误，不能截断历史来凑。后续悔棋/多档需重新预算或扩大分区。

## 双槽协议

1. 启动读取 `save_a`、`save_b`。独立检查长度、版本、CRC，再验证 position/history/result 内部一致性（最后历史应等于当前规范化局面）。不能把 CRC 当规则合法证明。
2. 选序号最高的有效支持版本；如果另一槽是更新但未知版本，进入版本不兼容提示，禁止自动覆盖，用户可显式选择兼容旧档。
3. 保存到较旧/无效槽，序号+1；`nvs_set_blob` → `nvs_commit` → read-back 验证；成功才给 app `SAVED(seq,generation)`。
4. 不需要第三个 active 指针。掉电后重新验证两槽；未完成写入不能淘汰另一个已提交档。
5. 无有效档显示“未找到可恢复棋局”，用户可新局；损坏档允许只清理本项目槽。`nvs_flash_init` 返回页满/版本错误时禁止示例式全局 erase，提供诊断与显式恢复。

sequence 使用 u64；接近最大值停止自动保存并提示维护，不无声 wrap。CRC 算法、wire enum 与长度在 fixture 固定，升级 schema 提供迁移测试，旧 schema 读取后仅通过正常新槽保存升级。

## 何时保存

落子、申报/认输终局、语言切换、用户返回首页触发保存。语言沿用现有 settings 字节，不变更 schema；旧档的 0 值保持英文。Flash 写阻塞发生在 storage worker，搜索与写入先串行调度；保存中按键仍处理，完成消息按序号核验。

若用户紧接落子硬关机，最新着可能尚未提交；不能宣称每步无条件掉电不丢。主动睡眠必须先确认保存完成，失败保持唤醒并提示；硬件电源键没有“退出时一定保存”的保证。

host 故障注入覆盖每个写阶段；设备满槽、反复保存500次（最低压力测试）、随机掉电至少20次，保留序号/恢复结果；验收允许回到最近完整提交档，不允许损坏成非法局面。单独测跨重启三次重复、ep、易位与计数。数据包含私人对局也不默认上传。
