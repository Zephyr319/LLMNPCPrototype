# Claude Start Prompt

```text
你现在接手 Unreal Engine 5.7 项目 F:\ue5\LLMNPCPrototype。

不要根据旧聊天重新推测项目状态，也不要一开始读取全部历史报告。请按顺序阅读：
1. CLAUDE.md
2. Docs/PROJECT_STATE.md
3. Docs/CURRENT_TASK.md
4. Docs/DECISIONS.md
5. 当前 git diff，以及本次任务直接相关的源码

代码和以上三份 Docs 文档是当前事实来源；PROJECT_HANDOFF.md、CLAUDE_NEXT_TASK.md 等只作为按需查阅的历史资料。
工作区包含尚未提交的重要修改，禁止 reset、checkout 或覆盖无关改动。

开始时请用不超过 8 条要点说明：当前已实现内容、尚未验证内容、本轮目标和预计修改的文件。然后只执行 CURRENT_TASK.md 的范围，不要重复实现现有系统，也不要擅自扩展功能。

完成后必须：
- 编译 LLMNPCPrototypeEditor Win64 Development；
- 区分“编译通过”和“PIE实际验证通过”；
- 给出简短改动摘要、验证方法和未验证项目；
- 更新 Docs/CURRENT_TASK.md；
- 只有架构或已接受设计确实变化时，才更新 PROJECT_STATE.md 或 DECISIONS.md。
```
