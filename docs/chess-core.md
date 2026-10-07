# chess_core：规则唯一真相源

纯 C11、可重入、固定容量；相同逻辑用于 host 与 ESP32。规则核心与下列主要接口已经实现；实现状态及验收边界见[开发状态](status.md)，详细测试证据见[验收记录](acceptance.md)。本文接口约定用于说明规则边界，源码是实际行为的最终依据。

## 数据与接口

棋盘采用 64 格 mailbox，`a1=0`，空/颜色/类型用明确枚举；历史编码时使用 nibble，不直接依赖 enum ABI。`position` 含棋盘、side、4-bit castling、ep 或 NONE、halfmove、fullmove、王位置。`move` 含 from/to/promotion，flags 由 core 计算而非信任调用方。`undo` 保存所有被修改格、王位置及之前权利/计数，不保存指向临时内存的指针。

```c
/* implemented public API; see chess_core.h */
chess_error chess_position_from_fen(chess_position *out, const char *fen);
chess_error chess_position_to_fen(const chess_position *pos, char *buf, size_t cap);
chess_error chess_generate_legal(const chess_position *pos, chess_move *out,
                                size_t cap, size_t *count);
bool chess_is_attacked(const chess_position *pos, uint8_t sq, chess_color by);
chess_error chess_make(chess_position *pos, chess_move move, chess_undo *undo);
void chess_unmake(chess_position *pos, const chess_undo *undo);
chess_error chess_game_apply(chess_game *game, chess_move move);
chess_status chess_game_status(const chess_game *game);
chess_error chess_game_claim_draw(chess_game *game, const chess_move *intended);
uint64_t chess_perft(chess_position *pos, unsigned depth);
```

`chess_make` 公共入口只接受完整合法着，失败不修改状态；内部 `_make_unchecked` 仅给生成器/perft 使用。合法着数组上限初定 256，溢出必须报错；测试任意 API 容量不足时输出不越界且不提交局面。深度0 perft=1。FEN buffer 128 bytes，解析六字段、八行八列、各一个王、合法 piece/count/计数范围；导入失败保留原局。拒绝王相邻、双方同时将军、背离行棋权的显然非法局面；不承诺证明任意 FEN 的历史可达性。FEN 不包含完整重复历史，外部 FEN 开始新历史并告知用户。

## 生成与 make/unmake

1. 生成伪合法走法（兵、跳马、射线、王），升变分别生成 Q/R/B/N。
2. 内部执行；检查己王没有被攻击；撤销；仅输出合法着。
3. 攻击检测独立于合法着生成，避免递归与“被钉住棋子不算攻击”的错误；兵攻击不等于兵推进，王攻击不生成易位。
4. 对每个合法着做 make/unmake round-trip；字段与规范化编码必须一致。绝不生成吃王走法。

特殊规则：易位仅标准国际象棋 e1/e8 与角车；王当前、经过、到达格都不可被攻击，路径为空，权利与对应王车存在；王车移动/原角车被吃立即失权，回来也不恢复。后翼 b1/b8 必须为空但无需不受攻击。吃过路仅紧接双步后的一个回合；make 同时移除被吃兵并检查暴露王。兵双步受起始 rank 与两格阻挡约束。升变必须带 Q/R/B/N，不允许留兵或默认为后。

## 终局和申报

规则依据：[FIDE Laws](https://handbook.fide.com/chapter/e012023)，具体产品支持见验收文档。

- 无合法着 + 被将军 = 将死；无合法着 + 未将军 = 逼和。
- 三次同局面、100 halfmoves：可申报；同局面五次、150 halfmoves：自动。支持当前与预选下一着申报，非法预选着无效。兵走/吃子重置 halfmove，fullmove 在黑方走后加1。
- 先检测将死/逼和，再支持的死局、五次重复、75步；将死优先于75步。
- M1 确定死局白名单：K v K、K+B v K、K+N v K、仅王和全部位于同色格的象。K+NN v K、异色象、K+N v K+N 不能误判。封闭兵链等复杂死局需要额外算法；未覆盖前标记规则限制，不宣称全部 FIDE 死局识别。
- 双方同意和棋与认输是 game 结果，不是 position 的合法着属性。无时钟，也无超时输棋规则。

## 重复局面与有限内存

重复 key 包含棋子分布、side、castling，以及**实际有合法吃过路兵着时**的 ep file；计数器、UI、mode 不计入。被钉住的 ep 不能造成不同 key。使用 34-byte 精确规范化快照（32棋盘+1 side/rights+1 ep），比较全部字节，避免只靠 hash 碰撞。

只保留最后不可逆边界之后的历史：兵走、吃子、易位权利变化都让之前局面不再能重现，可重置窗口。75步自动和棋意味着最多保存初局+150半步，共151条，5134 bytes；达到容量时先做终局判断，禁止丢弃未完成局面的必要历史。合法外部 FEN 若 halfmove 已达150且非将死，直接判自动终局；计数导入不得绕过容量门槛。

重复历史只由 `chess_game_apply` 更新；perft 的 make/unmake 不触发游戏终局与申报。载入必须恢复历史，否则提示历史未知并按新局面处理；禁止声称仍能识别载入前重复。

后续悔棋另设16半步窗口。撤销需恢复被不可逆着重置的重复历史；不能只减计数器。该额外内存与存档成本未计入 M1，须先设计 checkpoint/replay 或显式限制跨边界撤销。
