# 植物湿润度与浇水流程（第一轮：核心）

状态：2026-11-12 第一轮已编译并在 PIE 中部分验证（衰减、滞回、提醒、LLM 选择、绑定同一株、按秒浇水、完成释放、被接客打断时挂起并记录，均已在日志确认；"浇到一半被打断后回到同一株"和"恢复前有效性检查"尚未实测）。第 9 节（等点单期间的放行与沉默处理）已实现、**尚未编译**。第二轮（头顶进度条、玩家按 F 浇花、水壶）未开始。

## 1. 设计一览

| 项目 | 取值 | 位置 |
|---|---|---|
| 湿润度 | `moisture = 100 - WaterNeed`（0 干、100 满；0 没有任何后果） | `AWaterablePlantActor::GetMoisture()` |
| 衰减 | 0.1 点/秒（`MoistureDecayPerSecond`，控制台 `Cafe.Plant.DecayScale` 可整体缩放，0 = 冻结） | 植物 Tick，间隔 0.25 s |
| 初始值 | 按 `PlantID` 哈希错开，湿润度 69–100（`bRandomizeInitialMoisture`，默认开，会覆盖关卡里摆放的 WaterNeed） | `BeginPlay` |
| 待浇水状态（滞回） | 湿润度 **< 60** 进入；**≥ 95** 才解除；中间不改变。59→70 被打断后仍是"待浇水" | `UpdateNeedsWaterState()` |
| 显式解除 | `ClearNeedsWater(Reason)`：之后要再降 10 点（或回到 60 以上再降破）才会重新进入 | 植物 |
| 浇水速度 | NPC 浇水动画**实际播放期间** +5/s（`WateringMoisturePerSecond`），走路、暂停时不加 | 自主组件 0.1 s 定时器 |
| 动画循环数 | 按"到 95 还差多少 ÷ 5 ÷ 单次动画时长"向上取整，上限 8（`MaxWateringAnimationLoops`） | `StartActivity` |
| 提醒 | 湿润度下降跨过 60 / 40 / 20 且处于待浇水时，`MarkMeaningfulContextChange("plant_moisture:<ID>:below_<N>")`，**不打断**当前活动、不强制决策 | `NotifyPlantMoistureReminder` |
| 任务压力 | `HasOutstandingTask()` 排除 `water_flower`：花不产生 Duty 压力，是否浇水完全由 LLM 选择 | 自主组件 |
| 可用性 | 没有任何"待浇水且未预约"的植物时，`water_flower` 不在可选动作里（沿用 `IsActivityAvailable` → `HasSceneTargetTag` → `IsAvailableForTag`） | 既有逻辑 |

## 2. 绑定目标与预约（修复 F1：动画在 A 株、浇的却是 B 株）

- `StartActivity` 开头先 `BindWaterTarget()`：从"启用、未被预约、待浇水"的植物中选一株并 `TryReserve`。优先级：续做中断的那一株 > `Priority` 高 > 更干 > 更近。日志 `[Watering] bound plant=… why=… driest=…` 同时记录被选中的和全场最干的，用来评估选择质量。
- 移动用 `id:<PlantID>`（`MoveToSceneTargetByTag` 识别前缀并转到 `MoveToSceneTargetByID`），不再按标签重选；是否"已经在旁边"也只看绑定的那一株（`IsMaidNearPlant`）。
- 一次性 `WaterPlant(100)` 已移除。`ApplyActivitySuccess` 只释放绑定并刷新全局标签；动机/精力公式未改。
- 预约在这些路径全部释放：`FinishActivity`、`InterruptAutonomy`、`EndPlay`、成功完成，以及 `EvaluateAutonomyNow` 里的看门狗（绑定还在但活动已不是 `water_flower` 时释放，日志 `watchdog_orphaned`）。预约者被销毁时植物也不再视为被占用。

## 3. 中断与恢复

- 浇水中被打断且植物仍待浇水：计划按"可恢复"挂起（`ShouldSuspendActivePlan` 新增该条件），记下 `UnfinishedWaterPlantID`；计划被清除时记录同时清除。
- 恢复前检查 `ValidateWaterStepForResume()`：目标植物存在、启用、仍待浇水、未被别人预约。不通过则取消该计划，原因写入日志 `[WaterResume] pre-resume validity check failed. Reason=…`，`[AutonomyPlan] cleared. Reason=water_step_invalid:<原因>`。常见原因 `target_no_longer_needs_water`（玩家先浇了）。
- 恢复时 `BindWaterTarget` 仍选原来那一株（`why=resume_unfinished`），按当前湿润度重新计算动画循环数。
- 说明：第一轮对"目标已满足"选择**取消计划**，没有改成"转去另一株"。要不要加"更急的植物 → 切换"规则（需要阈值，例如低 30 点）属于下一轮的研究决定。

## 4. 给 LLM 的规划上下文

`BuildDecisionContextJson` 增加 `plants` 对象（最多 6 株，最干优先）：`context_version=plants_v1`、一条 `note`、以及每株的 `id / moisture / tier(full|ok|low|dry|critical) / needs_water / urgency / being_watered`。**系统提示词没有改**。每次请求会打一行 `[PlantContext] version=plants_v1 …`；LLM 选了浇水会打 `[WaterDecision] …`（带精力、无聊、责任、情绪、服务状态和各植物湿润度）。

## 5. 快照（SchemaVersion 3）

植物条目新增 `moisture / tier / urgency / enter_moisture / clear_moisture / reserved_by`；`needs_water` 直接读植物的滞回状态（不再用阈值现算），`reserved` 读 `IsReserved()`。精简的 Planning 上下文里植物按"优先级降序、湿润度升序"，并带 `moisture / tier / urgency`。

## 6. 控制台命令（Game/PIE）

| 命令 | 作用 |
|---|---|
| `Cafe.Plant.List` | 列出每株湿润度、档位、待浇水、预约者 |
| `Cafe.Plant.Set <ID\|all> <湿润度>` | 设置湿润度（调试） |
| `Cafe.Plant.Clear <ID>` | 显式解除待浇水 |
| `Cafe.Plant.DecayScale <倍数>`（cvar） | 衰减倍速，0 = 冻结 |
| `Cafe.Plant.NpcWater <ID>` | 让 NPC 立刻去浇指定植物（植物需先低于 60） |
| `Cafe.Snapshot` | 已有，植物部分显示新字段 |

## 7. PIE 验证步骤（编译通过后）

1. 启动 PIE，开 `Cafe.Plant.List`：各株湿润度应在 69–100 之间且互不相同；`Cafe.Plant.DecayScale 20` 加快衰减。
2. 等/设置一株到 59：日志出现 `needs-water ENTERED`、`[PlantReminder] … below 60`；`Cafe.Snapshot` 里该株 `needs_water=yes`，全局标签 `flower_needs_water` 为真；NPC **不会被打断**。
3. 等 NPC 决策或 `ForceAutonomyDecisionNow`：`[PlantContext]` 出现在请求前；若 LLM 选浇水看 `[WaterDecision]`。
4. 浇水过程：`[Watering] bound plant=… `→ 走向**同一株**→ `animation loops=…` → 湿润度按约 5/s 上升（`Cafe.Plant.List` 反复看）→ 到 ≥95 日志 `needs-water CLEARED`、`released … completed=1 reserved` 释放。
5. 打断：浇水动画中（湿润度约 70）对 NPC 说话/打断 → `released … completed=0 unfinishedRecorded=1`，植物仍 `needs=1`、`reserved=-`；互动结束后应看到 `[WaterResume] record=…`→`[AutonomyPlan] resumed`→ 同一株继续浇。
6. 失效：重复第 5 步，但在对话期间用 `Cafe.Plant.Set <该株> 100`（或 `Cafe.Plant.Clear`）→ 恢复时应出现 `[WaterResume] pre-resume validity check failed. Reason=target_no_longer_needs_water`，计划被取消。
7. 回归：迎宾、点单、上菜、收拾、自主行为各走一遍；确认没有 `[Watering] … watchdog_orphaned`（出现说明有路径漏释放，把日志给我）。

## 8. 已知限制 / 待确认

- 动画循环是整圈：到 95 时若正处于某圈中间，动画会播完，湿润度最多到 100（不会在 95 当场停）。
- `water_flower` 的动画若没配置，走等待回退（时长 = 需要的秒数，上限 40 s），湿润度同样按秒增加。
- 初始值哈希错开会覆盖关卡里每株手摆的 WaterNeed；不想覆盖就把该株的 `bRandomizeInitialMoisture` 关掉。
- `NeedsWaterThreshold`（旧字段）保留但不再参与判断。
- 植物 `Tick` 间隔 0.25 s；头顶进度条已在第 16 节加入（读 `GetMoisture()`）。
- 修改了 `NPCAutonomyComponent.h` 与 `NPCActionQueueComponent.h`，会触发较大范围重编译；内存不足的问题见 README 的构建说明（页面文件、`MaxParallelActions`）。

## 9. 等点单期间的挂起计划：玩家放行与沉默处理（2026-11-12 追加，未编译）

背景（基线事实 F7）：计划被接客打断后挂起，服务状态进入 `WaitingForOrder`，原有规则让自主评估直接返回，NPC 既不恢复也不做别的；玩家说"你先去忙吧"也没有任何通道能让计划恢复。

| 情况 | 行为 | 日志 |
|---|---|---|
| 玩家放行 | 对话 LLM 的 `embodied_response` 新增布尔字段 `player_released_npc`（"你先去忙吧 / 不用管我 / 決まったら呼びます"之类才为 true）。收到后解除限制，回复说完（或 5 秒后）恢复挂起的计划 | `[EmbodiedResponse] parsed … Released=true`、`[PlanRelease] source=player_released …`、`[AutonomyPlan] resumed` |
| 玩家沉默 ≥ 40 秒，且有挂起计划 | 她主动说"ご注文はごゆっくりどうぞ。少しだけ作業に戻りますので、お決まりになりましたらお声がけください。"，然后恢复计划 | `[PlanRelease] source=self_after_silence silence=…` |
| 玩家沉默 ≥ 60 秒，且没有挂起计划 | 轻声询问一次（每次等点单只说一次）："ご注文はお決まりでしょうか。…" | `[OrderWait] nudge spoken …` |
| 玩家再次开口 | 之前的放行作废；如果她正在做事，按原有的打断流程处理，计划重新挂起，需要再次放行或再次沉默 40 秒 | — |

- 提示词改动（`LLMClient.cpp`）：输出格式里加了 `player_released_npc`；规则里加了三条——何时为 true；为 true 且有挂起计划时，回复必须说"回去做那件事"（用 `active_plan.goal` / `interrupted_step`），并请玩家决定后叫她，不许说"去休息"；没有挂起计划时不许编造任务。
- 对话上下文 `active_plan` 在挂起时新增 `interrupted_step` 和 `waiting_for_player_release`。
- 沉默从这三者中最晚的一个算起：进入等点单的时刻、玩家上次说话、NPC 上次回复播完（没开 VOICEVOX 时第三项不会更新）。
- 可调参数（自主组件，分类 "NPC Autonomy|Order Wait"）：`bEnablePlayerReleaseResume`、`bEnableOrderWaitSilencePrompt`（两者都关 = 复现基线 F7）、`OrderWaitSelfReleaseSilenceSeconds`(40)、`OrderWaitNudgeSilenceSeconds`(60)、`ReleaseResumeDelaySeconds`(5)，以及两句台词。
- 两句主动台词是固定文本（和迎宾台词同一做法），不是 LLM 生成。研究事件：`suspended_plan_released`（含 source、沉默秒数）、`order_wait_nudge`。
- 与已有的 `NPCProactiveMenuHelpComponent`（蓝图按"犹豫分数"触发的菜单帮助）相互独立，两者可能在同一次等点单里各说一次。

验证步骤：
1. 让她开始浇水（`Cafe.Plant.Set Plant_01 40`），在她走向植物或浇水时进店，触发接客，直到入座等点单。
2. 说"你先去忙吧"：应看到 `Released=true` → `[PlanRelease] source=player_released` → `[WaterResume] record=Plant_01` → `[AutonomyPlan] resumed` → 回到同一株；她的回复应提到回去浇花、让你决定后叫她。
3. 重来一次，入座后什么都不说：约 40 秒后她说告退台词并回去浇水（`source=self_after_silence`）。
4. 她浇水途中点单：应被打断去接单，上菜后计划再恢复（或因植物已满足而取消）。
5. 没有挂起计划时入座并沉默 60 秒：只出现一次 `[OrderWait] nudge spoken`。

## 10. 挂念（Concerns）机制 第一步

目的：把"她为什么此刻要去做某件事"从固定阈值/规则，变成"心里挂着的事各有强度"。本步只改**何时请求规划**和**给 LLM 看什么**，不改恢复/挂起规则（第二步）。

- 植物越过 60/40/20 的提醒线 **不再直接触发规划**（`bPlantReminderTriggersPlanning=false`；设为 true 即恢复旧行为，可作对照基线）。
- 植物挂念强度 = (60 − 湿度) × `PlantConcernScorePerMoisturePoint`(2.0)，上限 100；被打断未完成的那株额外加 `UnfinishedConcernBonus`(30)，每 `UnfinishedConcernHalfLifeSeconds`(180 s) 减半（Zeigarnik 式：未完成的事更挂心，但会淡忘）。
- 触发：最强植物挂念 + `ConcernBoredomWeight`(0.5) × 无聊值 ≥ `ConcernTriggerPressure`(50)，且距上次挂念触发 ≥ `ConcernTriggerCooldownSeconds`(60 s)，日志触发名 `concern_pressure:<植物>:plantX+boredomY`。
- LLM 上下文新增 `concerns`（`concerns_v1`，前 5 项，含 score/detail/unfinished）；日志 `[ConcernContext]`。动机类挂念（boredom/duty/tired）只展示，公式未改。
- 控制台：`Cafe.Concerns` 打印当前挂念与压力。

验证：
1. `Cafe.Plant.Set Plant_01 55`：应**不**触发规划（`[PlantReminder] … TriggersPlanning=0`）；`Cafe.Concerns` 看到 plant 10 左右。
2. `Cafe.Plant.Set Plant_01 30`（强度 60）：应很快出现 `concern_pressure:Plant_01:…`，随后 `[ConcernContext]` 与 `[WaterDecision]`。
3. 打断浇水后 `Cafe.Concerns` 应显示 `[unfinished]` 且分数高于同湿度新植物，随时间下降。
4. 把 `bPlantReminderTriggersPlanning` 勾上对比旧行为。

## 11. 修订：自己落座时跟随 / 放行改为分数比较

- 玩家未被引导、自己坐下（状态为 WaitingForReceptionResponse/SelectingSeat）时，女仆走到该桌（`MoveToTableByID`），日志 `[CafeServiceSync] player seated unguided; maid walks to table`。
- 点单等待中挂起计划的自行告退，不再硬等 40 秒：`最强植物挂念 + 沉默秒数 × OrderWaitSilencePressurePerSecond(1.0) ≥ OrderWaitReleasePressure(110)`，且沉默 ≥ `OrderWaitMinSilenceSeconds`(8 s)。植物越干、被打断的越久，她越早开口。`OrderWaitSelfReleaseSilenceSeconds` 改为 90 s 的上限。日志 `[OrderWait] self release. Silence=… PlantConcern=… Pressure=…/110 Cap=0|1`。

## 12. 修订：点单与自主行动的冲突（2026-11-12 日志）

- 点单丢失：她在点单等待中已回去做自主活动时接到 `place_order`，`OnOrderStarted` 广播 → 服务状态切换 → 自主活动被打断 → `StopActionQueue()` 清掉了刚写入的订单状态（返回桌为空，`Move target is empty`）。修正：`RunOrderFulfillmentToCurrentTableInternal` 先广播、后写订单状态。
- 玩家说话后、回复生成期间不再开始新的计划步骤（`InteractiveRequestPendingUntilWorldTime`，回复到达即清除，15 s 超时）。
- 已入座时到达的过期"带位"回复，改说点单提示（日志 `[LLMClient] Stale guide reply replaced`）。
- 坐姿闪站：坐姿动画每个循环（约 7.7 s）作为独立蒙太奇播放，旧蒙太奇淡出完才开始下一个，中间露出站立基础姿势。新增 `EnqueueSeamlessAnimation`（提前 0.35 s 交接，交叉淡入），用于坐下、坐姿待机、坐姿对话表演。

## 13. 挂念竞争（第二步）：挂起/丢弃/恢复按分数决定

状态（2026-11-13）：代码已写、静态检查通过，**未编译、未运行**。开关 `bEnableConcernCompetition`（Full 条件开，Baseline 条件关；Baseline 仍是固定规则）。

**计划挂念分** = (承诺基础分 + 任务步骤加分) × 衰减 + 水分项，上限 100
- 承诺基础分：medium 45，low 20；剩余步骤含 Task 类活动（且 `bResumeTaskPlansAfterService`）加 15。
- 衰减：挂起后每 `PlanConcernHalfLifeSeconds`(300 s) 减半（只作用于承诺部分）。
- 水分项：当前步骤是浇水时，该植物自身的挂念分 × `PlanWaterConcernWeight`(0.5)；该植物挂念分含"被打断未完成"加成（见第 10 节）。

**① 被打断时：保留还是丢弃**
- 仍有硬规则：游戏结束 / 自主关闭 / 世界状态变化 → 丢弃；玩家请她坐下聊天 → 保留；正在浇水且植物仍缺水 → 保留。
- 其余原来的两条固定规则（承诺程度为 medium；剩余含任务步骤）改为：计划分 ≥ `PlanKeepThreshold`(30) 则保留。新建计划刚被打断时衰减为 1，结果与旧规则一致（medium 45 保留，low+任务 35 保留，low 无任务 20 丢弃）；**区别体现在恢复时**。

**② 恢复时：和别的挂念竞争**（挂起计划获准恢复时执行，日志 `[PlanCompetition] resume check`，事件 `plan_competition`）
- 计划分 < 30：放弃，原因 `faded`（拖太久，自然淡忘）。
- 竞争者 = 其他植物的挂念、疲劳（体力低于 50）；无聊/责任感不算（它们是"想找事做"的驱动，不是对时间的占用）。竞争者分数 > 计划分 + `ConcernCompetitionMargin`(10) 才取代，原因 `outcompeted_by_<id>`（边际用来避免来回切换）。
- 放弃后记 `plan_competition` 为上下文变化，由 LLM 带着挂念列表重新规划；否则照常走恢复前检查并恢复。

**示例**（中等承诺 + 任务步骤，浇 Plant_01，打断时湿度 30）

| 挂起后 | 计划分 | 对手：另一株湿度 25（分 70） | 对手：疲劳 体力 20（分 60） |
| --- | --- | --- | --- |
| 0 s | 100 | 恢复 | 恢复 |
| 180 s | 77 | 恢复（70 < 87） | 恢复 |
| 300 s | 65 | 恢复（70 < 75） | 恢复 |
| 600 s | 46 | 被取代（70 > 56） | 被取代（60 > 56） |

注：浇水计划的水分项（该植物仍很干时约 30）不衰减，所以只要植物还缺水，计划分不会低于 30，不会 `faded`；`faded` 只会发生在没有水分项的计划（如收桌、检查物资）。数值是初始设定，不是调参结果。

**③ 恢复前检查的失败原因**：除浇水外的步骤原先只返回"不可用"，现在 `plan_step_no_longer_available:<步骤>:<原因>`（disabled / energy_below_minimum / world_state_missing / no_scene_target / 无座位 / cooldown_or_other / start_activity_failed）。仍然只有浇水有"目标是否还值得做"的语义检查，收桌、检查物资只检查"能不能开始"。

**验证步骤**（待编译后）
1. `Cafe.Condition full`，`Cafe.Plant.Set Plant_01 30`，等她去浇水，来接待打断。日志应有 `[PlanCompetition] suspend check … -> keep`。
2. 放行恢复时应有 `resume check … -> resume`。
3. 打断后、让她恢复前，`Cafe.Plant.Set Plant_02 10`（比 Plant_01 干得多）：应出现 `outcompeted_by_Plant_02`，随后 LLM 重新规划。
4. 对非浇水的挂起计划（需要先让 LLM 生成一个）把 `PlanConcernHalfLifeSeconds` 临时调小（如 20）并等待：应出现 `faded`。
5. `Cafe.Condition baseline` 重复 1：不应出现 `[PlanCompetition]`。

## 14. 2026-11-13 04:10 运行验证结果（A1/B1/C1/D1）与随后的修订

| 试验 | 结果 | 证据（日志） |
| --- | --- | --- |
| A1 full：打断→入座→沉默→点单 | 通过 | `suspend check … keep`；`self release Silence=17s Pressure=112/110`；`resume check Plan=100 … -> resume`；回到同一株；`Order return table: Table_01`；点单到上菜 14.8 s，一次送达 |
| B1 full：玩家说"你先去忙吧" | **放行路径未通过** | 提示词上下文里有 `waiting_for_player_release:true`，模型仍返回 `player_released_npc=false`，回复"すぐ近くにいますので"；她 21 秒后因沉默自行告退（`self_after_silence`） |
| C1 full：另一株更干 | 通过 | `resume check Plan=75 … Rival=Plant_02:100 … -> outcompeted_by_Plant_02`；`cleared Reason=competition:…`；LLM 重新规划，`[ConcernContext] Plant_02:100 …`，绑定 `Plant_02`（最干） |
| D1 baseline | 通过 | 触发为 `plant_moisture:Plant_01:below_40`（旧行为）；无 `[PlanCompetition]`；过期带位回复未被替换 |

附带观察：
- 过期带位回复在 B1、C1 都被替换（`[LLMClient] Stale guide reply replaced`）。
- 坐姿闪站、点单丢失：本轮未再出现，但没有专门对照，仍以"未复现"记录，不写成"已验证修复"。
- `suspend check` 日志每次打断重复 5 次：已修改，已挂起的计划不再重复判断。

修订（待编译验证）：
1. **玩家放行的本地兜底**：当存在等待放行的挂起计划，且玩家说的话含明确的放行用语（"你先去忙吧""不用管我""お仕事に戻って""go back to work" 等），即使模型没置 `player_released_npc`，也本地判定为放行，并把回复换成固定台词（避免"我就在附近"与实际走开矛盾）。日志 `[LLMClient] Release inferred locally`。仅 `bEnablePlayerReleaseResume` 开启时生效（Baseline 关闭）。这沿用了项目里"本地校验 LLM 提议"的既有做法（如坐下、结束聊天）。
2. 统计脚本把"计划被取代后换植物"记为 `abandoned`，不再算成"换了一株"。

## 15. 2026-11-13 04:23 复测 B2（玩家放行）

- 通过：`Controller received: 你先去做你自己的事情把` → `Released=true` → `[PlanRelease] source=player_released silence=6s` → `[PlanCompetition] resume check Plan=100 … -> resume` → `[WaterResume] record=Plant_01` → 回到同一株继续浇（`why=resume_unfinished`）。
- 这次是**模型自己**置了 `player_released_npc=true`（没有 `Release inferred locally` 日志），本地兜底没有被用到；放行的完整路径首次有运行证据。
- 讨论后的决定（2026-11-13）：模型回复"近くにいますので、何かあればお知らせください"在当时语境下并无不妥（让客人有事知会她），**不强行替换回复**，上一版"放行生效即换固定台词"已撤销。原则：尽量少用固定台词和规则，把判断留给 LLM，本地只做记录与校验。
- 本地放行推断改为**默认关闭**（`bInferReleaseLocally=false`）。模型漏掉明确放行用语时只记录事件 `release_phrase_missed_by_model`（可作为模型可靠性的测量数据），不再覆盖模型的判断。

## 16. 植物头顶湿润度进度条（2026-11-13 追加，未编译）

- 显示的是**湿润度**（`GetMoisture01()`）：满条 = 100，空条 = 0。浇水时湿润度按秒上升，条会随之被"灌满"，所以它同时也是浇水进度。
- 实现：`AWaterablePlantActor` 新增 `MoistureBarComponent`（`UWidgetComponent`，屏幕空间，永远朝向镜头，大小固定），内容是纯 C++ 的 `UPlantMoistureBarWidget`（`PlantMoistureBarWidget.h/.cpp`，`RebuildWidget` 里用 Slate 的 `SProgressBar` 搭出来）。**不需要新建任何蓝图或资源**，关卡里已摆的植物（含蓝图子类）会自动长出这个组件。
- 颜色：绿（≥60）→ 黄（<60，进入"需要浇水"）→ 橙（<40）→ 红（<20）；湿润度刚上升（有人在浇）的 1.2 s 内显示蓝色。
- 位置：目标植物（`ObservedPlantActor`，未设则本 Actor）包围盒顶部 + `MoistureBarHeightOffset`（默认 80 cm），BeginPlay 时计算一次。
- 可调参数（植物实例的 Details 面板，`Waterable Plant|Moisture Bar`）：`bShowMoistureBar`、`MoistureBarHeightOffset`、`MoistureBarDrawSize`（默认 200×26 像素）、`MoistureBarMaxDistance`（默认 1500 cm，超过则隐藏，0 = 不限）、`bMoistureBarOnlyWhenNotFull`（只在未满时显示）。
- 更新频率跟随植物 `Tick`（0.25 s）；`Cafe.Plant.DecayScale 0` 冻结衰减时条仍会因浇水和镜头距离更新。

验证：
1. 编译（新增了两个文件，VS 里若看不到，右键 `.uproject` → Generate Visual Studio project files 后再编）。
2. 进 PIE：每株植物头顶应出现一条条；不同植物长度不同（初始湿润度 69–100，错开）。
3. `Cafe.Plant.Set Plant_01 30` → 条缩短并变橙；`Cafe.Plant.NpcWater Plant_01` → 浇水时条变蓝并逐渐变长，到 95+ 后恢复绿色。
4. `Cafe.Plant.DecayScale 20` 观察条随衰减缓慢变短、颜色逐级变化。
5. 走远到 15 m 外条应消失。

构建依赖：进度条用到 Slate 的 `SProgressBar`/`SBorder`，`LLMNPCPrototype.Build.cs` 的 `PublicDependencyModuleNames` 需要有 `SlateCore`（2026-11-13 首次编译因缺它出现 LNK2019，已补上）。

已知限制：屏幕空间的条不会被墙遮挡（15 m 内隔墙也能看到）；条没有显示 60 的"需要浇水线"刻度；条宽是屏幕像素，远处和近处一样大。

## 17. 玩家手动浇水（2026-11-13 追加，未编译）

操作：准星（相机射线，`PlayerInteractionComponent` 的 300 cm 射线）对准花盆 → 出现提示 **"F 水やり"**；按 F 开始持续浇水，提示变成 **"F 水やりをやめる"**；再按一次 F，或者走出范围，就停止。

- 实现方式沿用现有交互通路：`AWaterablePlantActor` 实现了 `IInteractableInterface`（与座位 `F 座る` 同一套），所以玩家蓝图里原有的 "F → `TryInteract`" 和"提示框绑定 `OnInteractionFocusChanged`"都不用改。提示文字是 `PlayerWaterPrompt` / `PlayerStopWaterPrompt`（`FText`，可在植物实例上改）。
- 为了让准星射线命中花盆，植物 Actor 新增了 `PlantInteractionBox`：只阻挡 Visibility 通道、包住目标植物的包围盒（BeginPlay 时按 `ObservedPlantActor` 或自身计算），不影响移动、导航和碰撞。判定盒在包围盒四周各外扩 `InteractionBoxPadding`（默认 45 cm，改后需重开 PIE），最小半边长 40 cm。
- 浇水速度 `PlayerWateringMoisturePerSecond`（默认 5/s，与 NPC 相同）。浇水期间植物 Tick 间隔临时改成 0.05 s，进度条平滑上升并显示蓝色。
- 停止条件（日志 `[PlayerWatering] stopped … Reason=`）：`player_toggle`（再按 F）、`left_area`（离花盆边缘 > `PlayerWateringMaxDistance`，默认 400 cm）、`full`（湿润度到 100 自动停止）、`player_seated`（坐下）、`player_gone`、`disabled`。
- 开始条件：玩家没坐下、在 400 cm 内、湿润度 < 100；已满的花盆不显示提示。
- 不占用"预约"：玩家浇水不会阻止 NPC 选这株；如果 NPC 的浇水计划绑定的是同一株，玩家把它浇满后，NPC 恢复前检查会得到 `target_no_longer_needs_water` 并取消计划（这正好是一个可观察的行为）。NPC 正在浇水时玩家也可以同时浇，两者的量叠加。
- 研究日志新增事件 `player_watering_started` / `player_watering_stopped`（含 `plant_id`、`moisture`、`reason`、`added`）。

验证：
1. 编译（改了 `WaterablePlantActor.h`，会触发较大范围重编译）。
2. PIE：走近花盆，准星对准 → 出现"F 水やり"；移开准星 → 提示消失。
3. 按 F：日志 `[PlayerWatering] started`，进度条变蓝并变长；再按 F：`stopped … Reason=player_toggle`，`Added` 约等于 5 × 持续秒数。
4. 浇水中走远（> 4 m）：`Reason=left_area`。
5. `Cafe.Plant.Set Plant_01 90` 后浇水：约 2 s 到 100，`Reason=full` 自动停止，提示消失。
6. NPC 联动：让 NPC 进入浇水计划（或挂起后），你把同一株浇满 → 恢复时应出现 `pre-resume validity check failed. Reason=target_no_longer_needs_water`。

已知限制：玩家没有浇水动作/水壶/音效（只有进度条变蓝）；提示框的显示依赖玩家蓝图里原有的提示 UI（和座位提示一样），若提示不出现，先看座位提示是否正常；射线只有 300 cm，第三人称相机离角色较远时需要靠近些。

## 18. 等点单时的自发台词改由 LLM 措辞（2026-11-13 追加，未编译）

背景：她等点单时有两句自发的话——"我先去忙一下"（沉默后自行放行）和"决定好了吗"（没有挂起计划时的轻声询问）——原来是写死的日语。按"尽量少用固定台词，把判断留给 LLM"的原则，改成只传**说话意图**，由模型自己措辞。

- 新接口 `ULLMClient::RequestAutonomyUtterance(IntentJson, FallbackLine)`：单次非流式请求，返回 `{"reply": "..."}`，用 `SpeakDirectText` 说出。上下文只含意图描述（英文）、服务状态、玩家沉默秒数、被打断的任务目标（有的话），不含任何成品台词。
- 它**不占用**"交互请求挂起"标志，不会阻塞玩家；但请求发出后若玩家说话（新的交互请求开始）或有交互请求在进行，回复会被丢弃（`dropped_stale`），避免她盖过玩家的对话。
- 旧的两句固定台词 `OrderWaitSelfReleaseLine` / `OrderWaitNudgeLine` 只作兜底：无 API key、请求失败或回复无法解析时才说。总开关 `bGenerateOrderWaitLinesWithLLM`（默认 true，关掉就回到固定台词）。
- 日志：`[AutonomyUtterance] request sent / generated / dropped / no usable reply`；研究事件 `autonomy_utterance`（`source` = `llm` / `fallback` / `dropped_stale`，`text`）。
- 放行动作本身不等台词：`ReleaseSuspendedPlan` 仍按原来的 5 s 延迟恢复计划，台词生成通常 1–3 s 内到达。

验证：进入等点单状态后保持沉默（有挂起浇水计划）→ 约 15–20 s 出现 `[OrderWait] self release` 与 `[AutonomyUtterance] generated: …`，每次措辞应略有不同；断网或清空 key 时应出现 `no usable reply; using the fallback line` 并说旧句；在台词生成的 1–3 s 内开口说话应出现 `dropped`。

