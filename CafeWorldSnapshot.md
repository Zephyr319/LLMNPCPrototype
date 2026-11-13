# CafeWorldSnapshot 技术说明

核对日期：2026-11-12（JST）。适用项目：`F:\ue5\LLMNPCPrototype`，Unreal Engine 5.7。

本文描述当前工作区实现，不代表全部验收已通过。项目概览见 [README](README.md)，当前提交、构建结果及下一步见 [PROJECT_HANDOFF](PROJECT_HANDOFF.md)。接口与语义集中维护在本文，验收结果以实际日志为准。

## 1. 用途与实现边界

研究重点是玩家介入正在执行的任务时，NPC 在继续、暂停后恢复、切换、取消并重规划之间的协调，以及对话、表情、动作、朝向、手持道具的一致性。

`FCafeWorldSnapshot` 为这些工作提供统一观测输入。已有状态的唯一来源仍是各个 Actor / Component：快照按值返回，修改返回值不会写回场景。Subsystem 不 Tick、不存储快照副本；每次采集重新读取当前 World，内部仅保存上次摘要和观测版本号。

当前功能是观察与投影接口，不是新的决策器。现有动机和回忆公式、LLM 提示词、动作选择及服务调用路径没有改接到快照。按用途筛选的结果也不会自动发送给 LLM。

### 文件职责

| 文件 | 职责 |
| --- | --- |
| [CafeWorldSnapshot.h](Source/LLMNPCPrototype/Public/CafeWorldSnapshot.h) | `BlueprintReadOnly` 值类型、用途枚举、离线转换接口；不包含 `bSpeaking` 字段 |
| [CafeWorldSnapshotSubsystem.h](Source/LLMNPCPrototype/Public/CafeWorldSnapshotSubsystem.h) | 获取 Subsystem、按需采集与蓝图可调用 API |
| [CafeWorldSnapshotSubsystem.cpp](Source/LLMNPCPrototype/Private/CafeWorldSnapshotSubsystem.cpp) | 读取源组件、派生字段、实体扫描、版本更新、日志输出、`Cafe.Snapshot` 控制台命令 |
| [CafeWorldSnapshot.cpp](Source/LLMNPCPrototype/Private/CafeWorldSnapshot.cpp) | 完整 JSON、用途投影、可读摘要和状态摘要计算 |
| [NPCAutonomyComponent.h](Source/LLMNPCPrototype/Public/NPCAutonomyComponent.h)、[NPCActionQueueComponent.h](Source/LLMNPCPrototype/Public/NPCActionQueueComponent.h) | 通过 `friend class UCafeWorldSnapshotSubsystem` 开放私有运行状态读取 |

当前采用 friend 读取方式，没有采用先前方案中的两个 `GetWorldSnapshotState()` 导出函数。friend 在 C++ 层允许访问私有成员，**只读性由采集函数的实现和审查维持，并非编译器强制的不可写权限**。

## 2. 数据来源与字段语义

| 快照部分 | 读取来源 | 含义与使用限制 |
| --- | --- | --- |
| 时间、版本 | UTC、World 时间、Subsystem 元数据 | 见第 4 节 |
| `Service` | `ACafeServiceManagerActor::GetServiceSnapshot()` | 原样保存完整 `FCafeServiceSnapshot` 值；服务阶段不等于物理姿态 |
| `Player` | 服务管理器的玩家引用，回退到本 World 的玩家 Pawn；`UPlayerSeatComponent` | 是否存在、实际落座、座位与桌号；读取 `bSeatStateKnown` 后再解释姿态 |
| `NPC` | 女仆引用、`UNPCAutonomyComponent`、队列 busy 查询 | 活动、目标标签、运行阶段、计划、动机和精力；`Zone` 是 `LastKnownNPCZone`，不是实时几何区域检测 |
| `Execution` | `UNPCActionQueueComponent`、移动组件 | 当前动作、剩余队列、动画、服务手势、移动锁、预留座位、延后动作及实际挂载道具 |
| `Body` | 执行状态、朝向组件、`UNPCFoodCarryComponent` | 基于观测事实的资源解释，不是权威身体资源调度器 |
| `Emotion` | `UNPCEmotionComponent::GetCurrentState()` | 已存的短期情绪、唤醒度、长期心情、表情意图；采集不主动做衰减 |
| `Memory` | 记忆组件及自主行为中已存的评价摘要 | 数量、最近回忆 ID/摘要、最近评价摘要；不是完整记忆库，不执行回忆或评分 |
| `Tables` | `ATableMarkerActor`、关联座位、清理标签及已完成服务桌号 | 占用聚合与清理推断，见下文 |
| `Seats` | `AFurnitureMarkerActor`、玩家/NPC 实际坐姿 | 区分预留与已坐下，见下文 |
| `Plants` | `AWaterablePlantActor` 当前字段 | 即时需水判断、预约 bool、可选性、优先度和标签配置 |
| 世界标签 | 自主行为的 `WorldStateTags` 与植物即时聚合 | 分别保留 reported 和 active，见第 3 节 |

### 玩家、活动与计划

- `Player.bSeated` 直接读座位组件；站起后 `SeatId` / `TableId` 清空。`Service.CurrentTableID` 可以继续保留服务桌号，`Service.bSeatedForService` 也可能因带位完成而仍为 true。
- 玩家未坐下时，`Player.Activity` 会采用已推送的 `PlayerContext.Activity`；空值或 `unknown` 才回退为 `standing`。该上下文可能滞后，核对物理姿态应使用 `bSeatStateKnown` 和 `bSeated`。
- `NPC.bBusy` 来自活动运行、等待身体响应或队列 `IsBusyForAutonomy()`，不表示语音正在播放，也不等于所有潜在异步工作的总状态。
- 计划包含状态、目标、完整步骤、索引、承诺程度和触发原因。结构/JSON 的索引从 0 开始；调试摘要显示的步骤序号为索引加 1。使用 `CurrentPlanStep` 前仍应检查计划状态及索引，不能仅凭非空步骤判定计划正在执行。
- `PendingActionCount` 不包含正在执行的动作。`ReservedSeatId` 表示预留座位，不能单独作为已入座证据。
- 缺失组件时，先检查 `bServiceAvailable`、`bAutonomyAvailable`、`Execution.bAvailable`、`Body.bExecutionStateKnown`、`bEmotionAvailable` 等；默认数值不代表实际观测到了空闲或正常情绪。精简上下文不会保留全部可用性标志，调用方需先检查结构化快照。

> **2026-11-12 更新（SchemaVersion 3）**：植物改为连续湿润度模型（衰减、滞回待浇水、预约者、按秒浇水），详见 [WateringMoisture.md](WateringMoisture.md)。下文第 3 节与第 56–57 行中关于"阈值即时计算""`bReserved` 无人置位""`[WaterablePlantApply]` 日志"的描述已过期：现在 `needs_water` 读植物的滞回状态，新增 `moisture / tier / urgency / reserved_by`，`reserved` 读 `IsReserved()`，选中目标对照日志 `[Watering] bound/released`。

### 玩家方位与已选目标（SchemaVersion 2，2026-11-12 新增，待运行验证）

- `Player.DistanceToNPC`（2D，<0 为未知）、`AngleFromNPCForwardDeg`（0 为正前方，180 为正后方）、`RearFactor = (1 − dot) / 2`，与自主行为的惊吓上下文使用同一公式，但这里是独立重算，不读取其内部状态。精简上下文只输出 `distance` 和 `position`（`rear_factor` ≥ 0.7 为 `behind`，≥ 0.3 为 `side`，否则 `front`，与惊吓上下文阈值一致）。
- `NPC.SelectedTargetId / SelectedTargetReason / SelectedTargetAgeSeconds`：来自移动组件最近一次按标签或 ID 的目标选择，**只表示移动步骤选了什么**。浇花效果步骤会独立重选植物，两者可能不同；对照看日志 `[WaterablePlantApply]`。`ActivityTarget` 仍是活动定义里的场景标签。
- 植物 `bReserved` 目前没有 C++ 代码会置位（蓝图未检查），因此快照里该字段通常为 false，不代表植物没有被某个任务占用。

### 身体资源和道具

`HeldProps` 分别列出动作队列道具和携带的食物/餐具，记录物品及实际挂载 socket。`Locomotion` 为 `seated`、`locked`、`moving`、`idle` 或 `unknown`；`UpperBody` 表示服务手势、动画、暂停动画、free 或 unknown。

当前手部推断只识别 socket 字符串中的 `left` / `right`；无法识别时保守地把两只手都标为持物。因此 `hand_L` 等缩写尚不能精确归属。`free` 只表示这一读取路径没有发现持物占用，不保证动画骨骼或任意蓝图动作允许插入手势。

`bMoving` 包括移动等待、跟随和引导状态，不是速度非零的测量；`bFacingTarget` 表示朝向请求存在，不证明画面中已经面向目标。快照目前不输出发声状态。

### 座位

| 字段 | 当前实现 |
| --- | --- |
| `bOccupied` | Marker 已被持有，且识别到持有者实际坐在该座位 |
| `bReserved` | Marker 已被持有，但未确认持有者已坐下 |
| `OccupantId` | `player`、`npc`、`unknown` 或空串，是角色标签，不是任意 Actor 的唯一 ID |
| `bAvailable` | `bCanSit && !bIsOccupied`，表示未被持有的座位 |
| `bAvailableForNPC` | 允许 NPC 坐且对当前 NPC `IsSeatAvailableFor()` 返回 true；NPC 自己已预约的座位也可能为 true |

其他持有者无法识别时会显示 `unknown`，并归入预留；不能据此断言其实际未坐下。列表包含 `bCanSit || bAllowNPCSit` 的 Furniture。实体 ID 优先用配置 ID，空值回退 Actor 名称；当前只按 ID 排序，场景应使用唯一 ID。

### 桌子与清理

`bMarkerOccupied` 保留桌子自身标志；`bOccupied` 还包括关联座位的占用或预留。`Capacity` 是配置容量，`OccupiedOrReservedSeats` 是扫描得到的数量。

项目没有逐桌 dirty 标志。当前清理推断按以下规则输出，必须同时读取 `bCleanupStateKnown` 和 `CleanupEvidence`：

| 条件 | 已知？ | 需要清理？ | `CleanupEvidence` |
| --- | --- | --- | --- |
| 没有自主行为组件 | 否 | 不确定 | `autonomy_unavailable` |
| 全局清理标签未置位 | 是，按现有全局模型 | false | `tidy_tag_clear` |
| 标签置位，且等于最近完成服务的桌号 | 是，按归属推断 | true | `tidy_tag_raised+completed_service_table` |
| 标签置位，已有完成服务桌号，但不是本桌 | 是，按现有单目标模型 | false | `tidy_tag_raised_for_other_table` |
| 标签置位，无法获得完成服务桌号 | 否 | 不确定 | `tidy_tag_raised_table_unattributed` |

这些结果不是桌面清洁程度的独立测量，不能覆盖任意多桌同时待清理的情形。

## 3. 植物与有效世界标签

需水判断直接计算 `bEnabled && (WaterNeed + KINDA_SMALL_NUMBER >= NeedsWaterThreshold)`，不调用 `NeedsWater()` 的缓存查询，也不主动刷新 Actor。原始数值仍按原值导出；该采集路径不会对直接写入的越界值做 Clamp。

- `bCachedNeedsWater` 保留 Actor 的缓存标志，方便诊断；启用植物的缓存与即时判断不同会产生 `plant_cached_flag_stale:<ID>`。
- `bAvailable` 还要求未预约，以及满足 `bOnlyTargetWhenNeedsWater`。预约不消除缺水事实；目前植物仅有预约 bool，没有预约者身份。
- `bAnyPlantNeedsWater` 汇总所有启用且需水的植物，包括未启用标签同步的植物。
- `ReportedWorldStateTags` 是自主行为组件已记录为 true 的标签。
- 对本次扫描到且启用 `bSyncAutonomyWorldState`、配置了非空 `WorldTag` 的植物，按标签做 OR 聚合，更新返回值中的 `ActiveWorldStateTags`。其余标签采用 reported 值。
- 不一致记录为 `world_tag_stale_true:<tag>` 或 `world_tag_missing_true:<tag>`。采集不调用 `SetWorldStateTag()` / `RefreshAutonomyWorldState()`，不会把派生结果写回或触发重规划。

边界：如果某标签已没有任何同步植物来源，当前实现仍采用 reported 值，不能据此保证删除最后一株植物后该标签自动清除。两株植物中只浇完一株时，另一株仍需水则有效标签应继续为 true。

## 4. 时间、版本和序列化

- `SchemaVersion` 当前为 1；`CapturedAtUtc` 是 ISO 8601 UTC，`WorldTimeSeconds` 是该 World 的游戏时间。
- `Revision` 是每个 Subsystem 的**连续观测版本**。首次有效采集通常为 1，状态摘要与上次不同才加 1；不是逐事件计数，也不是存档版本。
- 摘要来自去掉采集时间、游戏时间、Revision 和情绪更新时间后的 JSON，再计算 MD5。动机、精力、情绪和植物数值在 JSON 中保留两位小数，因此小于舍入精度的变化不一定推进版本。
- 两次采集之间发生又恢复的变化不会记录。诊断内容也参加摘要；实体排序、唯一 ID 和稳定的标签内容会影响摘要稳定性。不能把 Revision 当作无遗漏事件日志。
- 同一 World 的多个 Manager 共用该 Subsystem 的一组摘要元数据；交替采集不同 Manager 也可能推进 Revision。
- 全量 JSON 使用 snake_case 命名。`service.state` 用状态名称表达枚举，情绪源缺失时省略 `emotion` 对象；这是观测视图，非结构的逐字节无损转储。

## 5. 调用接口

Subsystem 仅支持 `Game` / `PIE` World，不在普通编辑器 World 创建。所有采集应在游戏线程调用。未传 Manager 时选择本 World 中找到的第一个服务管理器；多个 Manager 时应显式指定。当前代码不校验传入 Manager 是否同 World，调用方须保证来源一致。

| API | 行为 |
| --- | --- |
| `UCafeWorldSnapshotSubsystem::Get(const UObject*)` | C++ 入口，返回上下文 World 的 Subsystem 或 nullptr；本静态方法不是蓝图 UFUNCTION |
| `CaptureSnapshot(ACafeServiceManagerActor* = nullptr)` | 采集最新状态，返回 `FCafeWorldSnapshot` 值 |
| `CaptureJson(bool bPretty = false, ACafeServiceManagerActor* = nullptr)` | 重新采集并返回完整 JSON 字符串，不保存文件 |
| `CaptureContext(ECafeSnapshotPurpose, const FString& FocusEntityId, int32 MaxEntities, ACafeServiceManagerActor*)` | 重新采集并按用途投影；默认 focus 为空字符串（声明为 `TEXT("")`，UHT 不接受 `FString()` 作为 UFUNCTION 默认值）、上限为 6、Manager 为空 |
| `CaptureDebugLog(ACafeServiceManagerActor* = nullptr)` | 重新采集，向 `LogCafeWorldSnapshot` 输出多行可读摘要并返回字符串 |

C++ 调用示例：

```cpp
#include "CafeWorldSnapshotSubsystem.h"

if (UCafeWorldSnapshotSubsystem* Reader = UCafeWorldSnapshotSubsystem::Get(this))
{
    const FCafeWorldSnapshot Snapshot = Reader->CaptureSnapshot();
    const FString FullJson = FCafeWorldSnapshotJson::ToJson(Snapshot, true);
    const FString Context = FCafeWorldSnapshotJson::BuildContext(
        Snapshot, ECafeSnapshotPurpose::TaskInterruption, TEXT("Plant_01"), 6);
    const FString Summary = FCafeWorldSnapshotJson::DebugSummary(Snapshot);
    // FullJson / Context / Summary 来自同一次采集；是否保存或发送由调用方决定。
}
```

要立即输出日志，可单独调用 `Reader->CaptureDebugLog()`。如果连续调用 `CaptureSnapshot`、`CaptureJson`、`CaptureContext`，会发生三次采集；需要同一时点的数据时使用上述离线转换接口。

蓝图在 PIE/Game 中通过 **Get World Subsystem** 获取 `CafeWorldSnapshotSubsystem`，再调用四个 `Capture...` 节点。运行中查看快照最简单的方法是下面的控制台命令。

### 控制台命令 `Cafe.Snapshot`

在 PIE / Game 运行中按 **`** 打开控制台输入。命令只在 Game / PIE World 有效；编辑器未开始运行时会提示找不到 Subsystem。

| 命令 | 输出 |
| --- | --- |
| `Cafe.Snapshot`（或 `Cafe.Snapshot log`） | 可读摘要，写入 `LogCafeWorldSnapshot`，同时以青色文字显示在游戏画面左上角 20 秒；再次输入会刷新同一条屏幕消息 |
| `Cafe.Snapshot pretty` / `json` | 完整 JSON（缩进 / 单行），仅写日志 |
| `Cafe.Snapshot planning` / `dialogue` / `interrupt` | 对应用途的精简上下文及字符数 `chars=`，仅写日志 |
| `Cafe.Snapshot interrupt <FocusEntityId>` | 同上，并附带一个座位 / 桌子 / 植物的完整信息 |

控制台命令是调试入口，每次输入都会重新采集一次；`MaxEntities` 使用默认值 6。屏幕摘要较长时可能超出画面，此时在输出日志中查看。

## 6. 按用途筛选的上下文

各用途共同提供 `purpose`、`revision`、精简服务和玩家状态。均不附带全量记忆，也不执行新的 LLM 请求。

| 用途 | 主要输出 | 裁剪与限制 |
| --- | --- | --- |
| `Planning` | 当前活动、计划状态/目标/当前步、动机/精力、情绪、身体、有效标签；可用座位、可浇水植物、活跃桌子 | `free_seats` 实际筛选 `bAvailableForNPC`，包括可能由 NPC 自己预留的座位；植物需同时 needs-water 与 available，按优先度再缺水度降序；桌子需占用或需要清理 |
| `PlayerDialogue` | 当前活动和目标、交互状态、计划状态/目标/当前步、情绪、身体/手势、有效标签 | 不包含完整步骤或全场实体列表；不会自动加入当前桌详情，需要时传 focus |
| `TaskInterruption` | 活动/运行阶段、交互及恢复标志、动机、计划目标/当前步/之后剩余步骤、队列、身体/道具、情绪、有效标签、需水植物数量 | 不带完整实体列表；不决定继续、暂停、切换或取消，只提供输入事实 |

`MaxEntities` 默认 6，内部限制为 0～20。它分别限制规划中的各类实体列表和各用途的有效标签数，**不是总实体数或 token 预算**。当前没有截断计数/提示；剩余计划步骤和字符串长度不受其限制，截断后的标签缺失不能解释为该标签为 false。

`FocusEntityId` 不区分大小写，仅匹配配置/回退实体 ID；按座位、桌子、植物顺序找到首个匹配，额外加入一个完整实体对象。focus 不受 `MaxEntities` 限制，不会自动从当前任务推导，也不按 ActorId 匹配。跨类型的重复 ID 应避免使用。

## 7. 调试输出与尚未接入的功能

`CaptureDebugLog()` 的日志类别为 `LogCafeWorldSnapshot`，摘要首行为 `===== CafeWorldSnapshot rev=... =====`，随后包含 Service、Player、NPC、Plan、Motives、Emotion、Exec、Body、Tables、Seats、Plants、Tags、Diag。它不是字面上的 `[CafeWorldSnapshot]` 标签；检索 `CafeWorldSnapshot` 即可。

仍可结合原有 `[CafeServiceState]`、`[CafeServiceSync]`、`[NPCSeat]`、`[NPCSocial]`、`[AutonomyPlan]`、`[NPCFacing]`、`[WaterablePlant]` 日志核对。

当前未实现：JSON 自动落盘、向 `NPCResearchLoggerComponent` 自动写快照、快照变化事件、自动化验收测试，以及现有 LLM 调用点的替换。后续接入应另行实现，不能把本接口存在视为接入完成。

## 8. 验证状态与验收步骤

2026-11-12，`LLMNPCPrototypeEditor Win64 Development` 构建**通过**（在 Visual Studio 中确认）。通过前修复了两处问题：

| 阶段 | 错误 | 修复 |
| --- | --- | --- |
| UHT | `CafeWorldSnapshotSubsystem.h(34): C++ Default parameter not parsed: FocusEntityId 'FString()'` | `CaptureContext` 的默认参数改为 `TEXT("")` |
| C++ 编译 | `CafeWorldSnapshot.cpp` 中 `TArray<const FCafePlantSnapshot*>::Sort` 的比较函数使用了指针参数 | UE 对指针数组排序时会先解引用（`TDereferenceWrapper`），比较函数改为 `const FCafePlantSnapshot&` 参数 |

构建环境注意：新增源文件会使 UBT 重新划分 unity 编译单元并全量编译本模块，同时启动的 cl.exe 较多。本次曾出现 `C3859 未能创建 PCH 的虚拟内存` / `C1076 达到内部堆限制`（系统代码 1455，页面文件太小），属于 Windows 虚拟内存不足而非源码错误；加大页面文件或在 `BuildConfiguration.xml` 中降低 `MaxParallelActions` 可解决。

构建通过只说明代码能编译和链接，游戏内行为尚未验证。此前 2026-11-08 的成功记录对应朝向修复版本。仓库中未发现已实现的快照自动化测试。

在 `/Game/RestaurantDemo/Maps/Map_RestaurantDemo` 中，通过 `Cafe.Snapshot` 控制台命令（或蓝图 / C++ 调用）进行以下验收。每步操作完成后采集一次；“预期”不等于“已验证”。当前所有游戏内项目均为待测。

| 验收项 | 操作与核对字段 |
| --- | --- |
| 玩家落座/离座 | 入座前、入座后、离座后采集；核对 `Player.bSeated`、SeatId/TableId、座位持有状态；服务桌号可保留，另核对 `Player.Activity` 是否与物理姿态冲突 |
| NPC 动作与计划 | 开始、暂停、恢复、完成/停止后采集；核对 ActivityId、bActivityRunning、Plan 状态/当前步、当前动作与 pending 数、实际道具 socket 及 Body |
| 座位预约/释放 | 分别检查 NPC 尚在靠近、已坐下、起身释放；预留 ID 非空不等于 seated；他人持有时不可作为普通空座 |
| 浇花 | 同时放置至少两株需水植物；先浇一株再浇最后一株；核对 WaterNeed、即时 needs-water、reported/active 标签及诊断；预约不会消除水需求 |
| 只读性 | 同一时点连续采集；确认不改变动机、情绪更新时间、回忆次数、强制惊吓标志、计划索引、队列和道具，也不发送 LLM 请求；修改返回副本不影响下一次采集 |
| 精简上下文 | 对同一份快照生成三种用途，检查字段差异、focus、0/6/20 上限，确认未混入完整记忆/无关实体；单独量测长度，不能把 MaxEntities 当 token 保证 |
| 服务回归 | 完成迎宾→带位→落座→点单→上菜→用餐结束确认→清理，并复测坐姿多轮聊天及起身后执行不兼容任务；对照原日志和快照 |

记录模板：测试时间、地图、构建/提交、操作步骤、快照 Revision、相关字段/诊断、对应日志时间段、实际结果。接口尚未接入原流程只能说明改动范围较小，**不能作为“服务流程没有回归”的验收证据**。
