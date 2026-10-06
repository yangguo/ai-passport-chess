# AI 与 mcu-max 集成

M2 才接入。选择 [Gissio/mcu-max](https://github.com/Gissio/mcu-max)，MIT；核实 SHA 见 [来源](sources.md)。原引擎不是本项目规则权威，不直接复制 micro-Max 默认资源设置。

## 已核实的接口与风险

- `mcumax_set_fen_position` 返回 void，不提供输入校验；仅接受 core 导出的已验证 FEN。
- `mcumax_search_best_move(node_max, depth_max)` 同步运行；有内部加深逻辑。`mcumax_set_callback` 注册周期回调，回调中可 `mcumax_stop_search()`。
- `mcumax_move` 只有 from/to，没有 promotion 字段；头文件注明不支持 underpromotion。候选涉及升变时 adapter 明确附 Q，若 Q 非合法/不合产品战术需求则降级，绝不删除 core 中的 R/B/N。
- 引擎坐标采用从 FEN 第8行开始的0x88表示。核心 `a1=0` 转换 `((7-core_rank)<<4)|file`；不得误以为同一个 rank。a1/h8/双翼易位/ep 用测试验证。
- 状态是全局的，仅一个 worker 调用。FEN 每次搜索前重建，欠升变后仍能载入局面里的 N/B/R；不能靠连续调用 `play_move` 维持真实棋局。
- 源码 hash 默认关闭；启用时 `MCUMAX_HASH_TABLE_SIZE=(1<<24)`，不适合本设备。M2 保持关闭。日后小 TT 需显式补丁、编译期大小断言、链接 MAP 与实测，不仅翻转宏。
- FEN 能重建局面，不能传全部重复历史。引擎评分对重复/50步的理解需单独核实；应用终局与申报仍由 core 决定。

## 适配流程

1. app 生成不可变 position/FEN、合法根着、generation、预算；无合法着直接由 core 判终局。
2. worker 初始化并导入 FEN，注册带 context 的 callback，设置绝对 monotonic deadline。
3. 先保留一枚 core 合法的保底着；调用搜索，节点/深度仅作为第二道资源护栏，**时间截止是主限制**。
4. callback 以 clock 检查 deadline/cancel；按实测节奏让出 CPU（目标每2–5ms至少一次可阻塞 yield，不在每节点无条件 delay）。不能在 callback 里打印、绘制、写 NVS。
5. 返回转换坐标和 promotion；app 只有在 generation 相同、轮到AI、非终局时才检查精确合法着并提交。
6. 越界、无结果、引擎不支持、超时取消均有显式码；保底合法着只能在非用户取消且当前局面有效时采用。用户取消后不偷偷落子。

上游“截止时返回是否总为完整层最佳着”、实际 callback 间隔、stop 后 unwind 时延都未由接口保证。M2 先做 spike 检验；不能满足目标则打小补丁保留 completed iteration 或换自有小型 alpha-beta（复用 core），记录补丁/许可证。不得在每次外层加深重启后丢弃上一次好结果。

## 难度与提示（初始调参值，未经真机验证）

| 档位 | 墙钟预算 | 策略 |
|---|---:|---|
| Easy | 100ms | 正常搜索后小概率选择其他合法着；seed 可重放 |
| Normal | 300ms | 最后完成层最佳着 |
| Hard | 1000ms | 相同规则，较长预算；不是“保证4层” |
| Hint | 400ms | 仅显示建议，不执行；玩家走后清除 |

mcu-max 不暴露多候选评分，不虚构 top-N 接口。Easy 初版随机从合法着采样；后续若需要“次优”，必须加 root scoring adapter 和测试。先做强制一步将死、避免一步被将死等核心战术 fixture；再以固定开局、相同种子/预算自对弈评估调参。不把四子棋 nodes/s 换算成象棋性能，也不凭深度声称 Elo。

提示及搜索不保存引擎全局内存。每次退出关闭 callback，确认 worker ACK 后释放数据。MIT LICENSE 与修改 diff 随集成提交，第三方版本升级重跑 adapter、deadline、perft（core 未变也测接口）与设备预算。
