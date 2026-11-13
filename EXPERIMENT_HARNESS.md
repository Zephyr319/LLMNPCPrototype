# 实验脚手架（最小版）

日期：2026-11-13。状态：**代码已写、静态检查通过，尚未编译和运行验证。** 统计脚本已用旧日志和合成数据试跑。

目的：在同一个构建里切换"基线 / 完整机制"，用固定输入重复试验，并从结构化日志自动算出每次试验的结果。取代 `CLAUDE_NEXT_TASK.md` 中范围过大的部分（六节点完整快照、12 案例文档、大报告暂缓）。

## 1. 条件开关

`UNPCAutonomyComponent::ExperimentCondition`（细节面板 "NPC Autonomy|Experiment"）：

| 开关 | Baseline | Full | 含义 |
| --- | --- | --- | --- |
| `bEnablePlayerReleaseResume` | 关 | 开 | 玩家放行后恢复挂起计划 |
| `bEnableOrderWaitSilencePrompt` | 关 | 开 | 沉默后按挂念强度自行告退 / 催单 |
| `bEnableConcernPressureTrigger` | 关 | 开 | 挂念压力触发规划 |
| `bPlantReminderTriggersPlanning` | 开 | 关 | 植物越线即触发规划（旧行为） |
| `bHoldNewStepsWhileReplyPending` | 关 | 开 | 回复生成期间不开始新步骤 |
| `bReplaceStaleGuideReply` | 关 | 开 | 已入座时替换过期的带位回复 |
| `bEnableConcernCompetition` | 关 | 开 | 挂起/丢弃/恢复按挂念分数竞争（见 `WateringMoisture.md` 第 13 节） |

`Custom`（默认）不改动各开关。**不受开关控制、两种条件下都生效的修正**：浇水绑定同一株与预约、点单状态丢失修正、玩家自己落座时跟随、坐姿动画无缝交接。论文中须如实说明基线包含这些修正。

## 2. 控制台命令（PIE 中）

- `Cafe.Condition [baseline|full|custom]`：切换条件；不带参数打印当前开关。
- `Cafe.Trial.Begin <TrialID> [baseline|full|custom]`：开始一次试验，之后的日志事件都带该 ID。
- `Cafe.Trial.End [备注]`：结束试验。PIE 结束时未结束的试验会自动记为 `session_ended_before_trial_end`。
- `Cafe.Say <文字>`：注入一句玩家的话，走与输入框"发送"相同的路径。

## 3. 结构化日志（`Saved/ResearchLogs/*.jsonl`，schema 2）

每条事件新增根字段：`seq`（会话内序号）、`world_time`、`trial_id`、`condition`、`interaction_id`（玩家每说一句加一，之后的事件沿用到下一句）。

新增事件：`trial_started`、`trial_ended`、`experiment_condition_applied`、`interaction_started`（说话时的活动、计划状态、服务状态、绑定植物）、`player_utterance`、`interactive_reply_received`（决策、动作、回复、是否过期/被替换）、`interaction_reply_finished`、`plan_created / plan_suspended / plan_resumed / plan_completed / plan_cleared`、`service_state_changed`、`order_started`、`order_served`。

限制：`interaction_id` 是"最近一次玩家话语"的编号，不表示该事件由这句话引起；语音输入若不经过策略请求入口则不会计数（未核对）。

## 4. 统计脚本

```
python Tools/analyze_trials.py                 # 全部日志
python Tools/analyze_trials.py <文件或目录>
python Tools/analyze_trials.py --csv trials.csv
```

每次试验一行：时长、玩家话语数、计划的创建/挂起/恢复/完成数、其他清除原因、放行来源、浇水打断/完成数、是否回到同一株、点单数/上菜数、点单到上菜耗时、点单未送达期间玩家又说了几句、过期回复数与被替换数。旧日志（无试验标记）按整个会话算一行，其玩家话语数是近似值。

## 5. 首次验证步骤

1. 编译，进入 PIE，输入 `Cafe.Condition`，应打印六个开关。
2. `Cafe.Trial.Begin T01 full` → `Cafe.Plant.Set Plant_01 30` → 等她去浇水 → 走近触发接待 → `Cafe.Say 是的` → 自己落座 → 沉默等她告退 → 她浇水时 `Cafe.Say 来一份拉面` → 上菜后 `Cafe.Trial.End`。
3. 同样流程用 `Cafe.Trial.Begin T02 baseline` 再跑一次：预期她在点单等待中不回去浇水（F7）。
4. 结束 PIE 后运行脚本，核对两行结果与实际看到的是否一致。

## M 键与策略模式（2026-11-13）

- M 键在蓝图 `BP_LLM_NPC_VRM` 里切换变量 `bUseStrategyMode`：`false` = 普通对话（`SendDeepSeekMessage`），`true` = 策略模式（`SendStrategyMessage`）。点单、玩家放行、过期引导回复替换、`player_utterance` 事件等机制都只在策略模式下起作用，`Cafe.Say` 也经过同一个判断。
- 该蓝图变量原默认值为 `false`，每次开 PIE 都要先按 M，漏按则整次试验悄悄失效。现在 `UNPCProactiveMenuHelpComponent::BeginPlay` 会通过反射把它设为 `true`（开关 `bStartInStrategyMode`，默认 true），日志 `[StrategyMode] Started in Strategy Mode`。**开局即策略模式；按一次 M 会切到普通模式。** 跑试验时不要按 M。
- C++ 组件里还有一份同名的 `bUseStrategyMode`（只管主动菜单帮助），与蓝图那份是两套状态；统一放到发表之后。
