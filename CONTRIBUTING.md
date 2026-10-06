# 参与开发

先读 README、AGENTS 和实施计划；实现从 `feature/*` 分支开始。每次提交围绕一个规则/接口/测试目标，先最小失败case再修复。保持第三方导入与原创修改可追踪。

文档阶段检查 `python3 tools/check_docs.py`。实现后提交说明给出触发问题、行为改变、host/perft/构建检查及设备NOT RUN项；复杂功能更新设计和验收记录。不要修改公开perft期望来迁就实现。

新增素材附许可与来源，资产生成结果可复现。错误报告含commit、FEN/UCI序列、模式、硬件/IDF、日志、期望与实际；私人设备数据先脱敏。

代码格式和clang-format配置在M0/M1首次引入C代码时确定；不在文档初版虚构已配置的格式检查。发布规则见docs/build-ci-flash.md。
