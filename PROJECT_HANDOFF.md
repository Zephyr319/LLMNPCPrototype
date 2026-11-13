# LLMNPCPrototype Handoff

Updated: 2026-11-12 (JST)

Experiment harness (2026-11-13, implemented, not yet built or run): condition switch (baseline/full), `Cafe.Trial.Begin/End`, `Cafe.Say`, `Cafe.Condition`, log schema 2 with trial/interaction ids, `Tools/analyze_trials.py`. See [EXPERIMENT_HARNESS.md](EXPERIMENT_HARNESS.md). This replaces the over-scoped parts of `CLAUDE_NEXT_TASK.md`. Evidence logs of the 2026-11-12 failures are kept in `Saved/ResearchEvidence/2026-11-12/`.

Watering redesign (2026-11-12, implemented but not yet built or run): continuous plant moisture, hysteresis "needs water" state, bound/reserved target plant, per-second watering during the animation, pre-resume validity check, plant context for the planner. See [WateringMoisture.md](WateringMoisture.md). Second pass (overhead progress bar, player watering with F, watering can) is not started.

Documentation map: [README](README.md) covers the overview/setup; this handoff records the current checkpoint and validation status; [CafeWorldSnapshot technical reference](CafeWorldSnapshot.md) is the detailed source for snapshot APIs, semantics, limitations and acceptance checks. This update changes documentation only.

## 1. Project Summary

- Workspace: `F:\ue5\LLMNPCPrototype`
- Repository: `https://github.com/Zephyr319/LLMNPCPrototype`
- Branch: `main`
- Engine: Unreal Engine 5.7 on Windows
- Main map: `/Game/RestaurantDemo/Maps/Map_RestaurantDemo`
- Current target: desktop prototype. VR is a future extension and is not the present research scope.
- Character: original VRoid maid model, replacing the copyrighted Blue Archive character previously used.
- Persona: Sumiha Shinomori / 篠森 澄羽, a warm, mature and observant 25-year-old senior cafe employee.

This is a master's research prototype for an LLM-based embodied NPC in a cafe. The NPC should continue to behave coherently when the player gives no input, while reacting to the player, world events, memories, emotion and its current plan.

## 2. Current Research Focus

Current focus: when the player intervenes during an NPC task, coordinate **continue**, **pause and resume**, **switch task**, and **cancel and replan**, while keeping dialogue, expression, animation, facing and held props consistent. These are research cases to implement/evaluate, not a claim that every branch is validated.

Broader architectural topic:

> A context-aware LLM NPC architecture that separates task-level planned behavior from emotion-dependent expressive behavior, while coordinating both during interaction.

The central research ideas are:

1. **Planned behavior and expressive behavior are separate but can run together.**
   Cleaning, watering plants or checking supplies are task-level plans. Facial expression, gaze, small gestures, surprise and later humming are expressive layers that can accompany the task without replacing it.
2. **LLM calls are event driven instead of periodic.**
   Local code continuously updates simple numerical state and filters impossible actions. The LLM is called when semantic interpretation, a new plan, conversation or memory appraisal is needed.
3. **Memory, emotion and behavior form a feedback loop.**
   A recalled event is appraised, changes short-term emotion and slower mood, and then changes action preference and outward expression.
4. **Dialogue and body response are generated from the same situation.**
   The LLM can return speech strategy plus face, gaze, hand, voice and prop intentions. Local validation decides what can actually be executed.
5. **Service rules remain authoritative.**
   Player requests cannot override safety, service priority, scene availability or occupied objects.

Do not claim that the entire concept is unprecedented. The contribution should be evaluated as the integration and coordination mechanism, plus controlled comparison and user evaluation.

## 3. Input, Reasoning and Output

### Inputs

- Player text or recognized speech
- Estimated emotion from the player's voice
- Player activity and location, such as entering, seated, walking or leaving the table
- Cafe service state and table/seat state
- NPC location, current activity and recent completed activities
- Boredom, duty/task pressure, energy and social attention
- Short-term emotion, arousal and slower mood
- Retrieved episodic memories
- World tags such as a plant needing water or a dining area needing cleanup
- Active, suspended or completed plan state

### Local processing

- Frequent numerical state updates and time decay
- Action feasibility filtering and priority checks
- Real-time scheduling using computer time where appropriate
- Pathfinding, animation playback, props, seating and action queue execution
- Memory storage and candidate retrieval
- Service-state validation and interruption/resumption rules

### LLM responsibilities

- Generate dialogue and conversational strategy
- Interpret a situation that cannot be reduced to a simple rule
- Generate a short multi-step plan from locally filtered candidates
- Appraise recalled memories and propose emotion changes
- Select compatible embodied response intentions

### Outputs

- Japanese dialogue and subtitle, including optional expression/thought notes in parentheses
- Movement and task actions
- Animation, face, gaze and upper-body gesture intentions
- Temporary props such as a phone or watering can
- New or updated memories, emotion and plan state
- Structured research logs

## 4. Implemented Features

- Reception, dining-intent confirmation and table guidance
- Fixed seated camera; movement input exits the player's seat
- Menu/order flow, food delivery, meal completion confirmation and table cleanup
- Multi-step autonomous plans with interruption and resumption
- Watering, cleaning, supply checking, idle behavior, phone use and sofa/seat behavior
- Plant/table/furniture target markers and navigation
- Props attached during relevant actions
- Episodic memory persistence at `Saved/NPCMemory/sumiha_memories.json`
- Seeded positive and negative demonstration memories
- Memory recall probability, LLM appraisal, emotion update and decay
- Facial expressions and automatic blinking
- Speech-startle probability using focus, suddenness and player direction
- Proactive conversation using context and memory
- Structured embodied-response parsing
- Research logging

Local voice services include Whisper, voice-emotion recognition and VOICEVOX/Style-Bert-VITS2. Repeated VOICEVOX connection errors can be ignored when its local server is intentionally not running.

## 5. Current Architecture

Important components:

- `CafeServiceManagerActor`: authoritative cafe-service state and unified `FCafeServiceSnapshot`
- `CafeWorldSnapshotSubsystem`: demand-driven, value-only read model across the existing Actors/Components; no Tick or cached world-state copy. Builds as of 2026-11-12; in-game validation pending (section 8).
- `PlayerSeatComponent`: authoritative physical player seating, seat ID and table ID; distinct from service-stage flags
- `NPCAutonomyComponent`: motives, memory triggers, plans, social seating and expression coordination
- `NPCActionQueueComponent`: execution only; reports movement/service execution facts to the service manager
- `NPCMemoryComponent`: episodic memory persistence and retrieval
- `NPCEmotionComponent`: short-term valence, arousal, mood and real-time decay
- `LLMClient`: prompts, OpenAI requests, JSON parsing and local action repair
- `NPCProactiveMenuHelpComponent`: context-sensitive menu help

The service flow is:

`Idle -> MaidApproachingPlayer -> ReceptionGreeting -> WaitingForReceptionResponse -> SelectingSeat -> GuidingToSeat -> WaitingForPlayerSeated -> WaitingForOrder -> OrderReceived -> GoingToCounter -> DeliveringOrder -> Served`

`CafeServiceManagerActor` is now the only service-state source read by the LLM, autonomy system and proactive-help system. The action queue retains only private execution data and publishes events to the manager.

The new world snapshot observes these sources without replacing them. Existing LLM prompts, decision calls and research-logger calls have not been switched to the snapshot APIs.

## 6. Implemented: Seated Conversation

A persistent social-seating state machine has been implemented:

- `SocialSeatEntering`
- `SocialSeatSession`
- `SocialSeatExiting`

Expected behavior:

- The player can ask Sumiha to sit opposite and talk.
- She should walk to an available opposite seat, sit, start a topic and remain seated across ordinary dialogue turns.
- Each reply can add a seated upper-body gesture and expression.
- She stands when explicitly asked, when the player leaves, after 120 seconds of inactivity, or when a higher-priority task requires it.
- An incompatible task requested while she is seated is deferred until she stands.
- A previously suspended autonomous plan should resume afterward.

The response parser now repairs a missing action when the LLM returns `decision: sit_opposite_player` but leaves `actions` empty.

## 7. Implemented: Unified Service State and Current Additions

The previous implementation had two conflicting state sources:

- Interactive LLM prompts read static snapshots in the action queue.
- Autonomy read `CafeServiceManagerActor::CurrentState`.

This caused the player to be physically seated while autonomy still believed reception was active, so `sit_opposite_player` was rejected.

The implementation committed in checkpoint `b06611c`:

- Adds `FCafeServiceSnapshot` to `CafeServiceManagerActor`.
- Removes the action queue's public static service-state mirror.
- Makes the action queue publish guidance, table-area and arrival facts to the manager.
- Makes LLM dialogue, autonomy and proactive menu help read the manager snapshot.
- Adds `[CafeServiceState]` and `[CafeServiceSync]` logs.
- Reconciles direct LLM table guidance with the formal service state.

### Uncommitted speech-facing changes

- A separate speech-facing request covers ordinary replies, service speech and proactive conversation.
- Ordinary action-queue cleanup does not cancel that request. VOICEVOX logical pending state controls its lifetime, with a subtitle-duration fallback when audio is unavailable.
- Moving, seated and orientation-sensitive task states retain their protections; standalone startle/shush dialogue animations are allowed to face the player.
- `[NPCFacing]` logs expose requests, suppression reasons and release. UHT and Editor C++ build passed for this version on 2026-11-08; final in-game verification is still pending.
- Seat offsets were not edited. To calibrate the maid only, adjust the relevant Furniture Marker instance's `NPC Sit Location Offset` / `NPC Sit Rotation Offset`, then re-enter the seat. The last inspected session used `Chair_01_A`; confirm the current seat in `[NPCSeat]` logs before editing it.

### Uncommitted CafeWorldSnapshot additions

- Four files implement `FCafeWorldSnapshot`, `UCafeWorldSnapshotSubsystem`, JSON, purpose-specific contexts and readable debug output. `Get(this)` is the C++ entry; `CaptureSnapshot`, `CaptureJson`, `CaptureContext` and `CaptureDebugLog` are the capture APIs.
- The subsystem supports Game/PIE worlds and is intended for game-thread use. Every call rereads the sources and returns a copy; only a digest and observed Revision are retained.
- The current implementation uses `friend class UCafeWorldSnapshotSubsystem` in Autonomy and ActionQueue headers, not the previously proposed runtime-export getters.
- Physical player seating, reserved versus occupied seats, instantaneous plant need, reported versus effective tags, and inferred table cleanup are documented in the [technical reference](CafeWorldSnapshot.md). Body-resource labels are interpretations of execution facts; there is no snapshot `bSpeaking` field.
- Revision compares consecutive observed digests, not every event. Changes that revert between captures are not counted. JSON rounding and timestamp exclusions also affect it.
- Console command `Cafe.Snapshot [log|json|pretty|planning|dialogue|interrupt] [FocusEntityId]` (Game/PIE only). The default mode logs the readable summary and also shows it on screen (top-left, 20 s); the other modes log only. Details are in the [technical reference](CafeWorldSnapshot.md) section 5.
- No automatic JSON file export, research-logger integration, or LLM-call replacement is present.

## 8. Verification State

- **Current workspace build (2026-11-12): PASSED** (`LLMNPCPrototypeEditor Win64 Development`, confirmed in Visual Studio).
- Two snapshot fixes were needed first: the UHT error `CafeWorldSnapshotSubsystem.h(34): C++ Default parameter not parsed: FocusEntityId 'FString()'` (default changed to `TEXT("")`), and a C++ error in `CafeWorldSnapshot.cpp` where the `TArray<const FCafePlantSnapshot*>::Sort` predicate took pointers (UE dereferences pointer elements before calling the predicate; it now takes `const FCafePlantSnapshot&`).
- Build environment note: adding source files makes UBT regroup unity files and rebuild the whole module with many parallel `cl.exe` processes. One run failed with `C3859` / `C1076` (system code 1455, page file too small). That is a Windows virtual-memory limit, not a source error; enlarge the page file or lower `MaxParallelActions` in `BuildConfiguration.xml`.
- Historical UHT and Editor C++ build success on 2026-11-08 applies to the earlier speech-facing changes, not the new snapshot implementation.
- No snapshot automation tests were found under `Source`. The snapshot, speech-facing update, and complete seated-conversation/service regression cases remain pending in-game validation.
- Static documentation checks confirm the APIs and source ownership; unchanged service call sites are not proof of runtime non-regression.

Execute the [snapshot acceptance matrix](CafeWorldSnapshot.md), running `Cafe.Snapshot` before/after player seating, reservations, NPC actions and watering. Readable output is under `LogCafeWorldSnapshot` and starts with `===== CafeWorldSnapshot rev=... =====`; search `CafeWorldSnapshot`, not a bracketed tag.

Retain the seated-conversation regression sequence:

1. Enter the cafe and complete reception/table guidance.
2. Sit in the fixed player seat.
3. Say: `你坐我对面陪我聊一会儿。`
4. Continue two or three normal dialogue turns.
5. Say: `你可以起来了。`
6. Repeat with `那你坐下吧` to verify decision-only action repair.
7. While she is seated, request watering or another incompatible task and confirm that she stands before executing it.
8. Inspect `Saved/Logs/LLMNPCPrototype.log`, especially `[CafeServiceState]`, `[CafeServiceSync]`, `[NPCSeat]`, `[NPCSocial]`, `[AutonomyPlan]`, `[NPCFacing]`, `[WaterablePlant]` and `CafeWorldSnapshot`.

Also exercise reception, ordering, delivery, meal-completion confirmation and cleanup end to end. Record observed results rather than marking these passed based only on the additive design.

## 9. Git and Build State

Latest committed revision at handoff:

`b06611c01e5cf33c16fd2d1b5f6856acbff6c6bd Unify cafe service state and persist seated conversations`

This checkpoint was pushed to `origin/main` earlier in this chat. At the 2026-11-12 local inspection, `main` and the locally recorded `origin/main` still matched it; no remote fetch or new push was performed during documentation work.

The working tree is intentionally dirty. Do not reset or discard it. Existing modified source files:

- `Source/LLMNPCPrototype/Private/LLMClient.cpp`
- `Source/LLMNPCPrototype/Private/NPCActionQueueComponent.cpp`
- `Source/LLMNPCPrototype/Private/NPCAutonomyComponent.cpp`
- `Source/LLMNPCPrototype/Private/NPCFaceTargetComponent.cpp`
- `Source/LLMNPCPrototype/Private/VoicevoxTTSClient.cpp`
- `Source/LLMNPCPrototype/Public/NPCActionQueueComponent.h`
- `Source/LLMNPCPrototype/Public/NPCAutonomyComponent.h`
- `Source/LLMNPCPrototype/Public/NPCFaceTargetComponent.h`
- `Source/LLMNPCPrototype/Public/VoicevoxTTSClient.h`

New snapshot source files, still untracked at inspection:

- `Source/LLMNPCPrototype/Public/CafeWorldSnapshot.h`
- `Source/LLMNPCPrototype/Public/CafeWorldSnapshotSubsystem.h`
- `Source/LLMNPCPrototype/Private/CafeWorldSnapshot.cpp`
- `Source/LLMNPCPrototype/Private/CafeWorldSnapshotSubsystem.cpp`

Documentation updated in this task: `README.md`, `PROJECT_HANDOFF.md`, and new `CafeWorldSnapshot.md`. These changes have not been committed or pushed. Use current `git status` / `git diff` when resuming; this list records the inspection time.

The Codex runtime may falsely report that `Build.bat` is already running because its temporary lock cannot be created. If no real Unreal build is active, use UnrealBuildTool directly:

```powershell
dotnet F:\UE_5.7\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll `
  LLMNPCPrototypeEditor Win64 Development `
  -Project=F:\ue5\LLMNPCPrototype\LLMNPCPrototype.uproject `
  -WaitMutex -NoHotReloadFromIDE
```

## 10. Known Limitations and Next Priorities

- The available animation library is still limited; humming has not been added.
- Some expressions are substitutes rather than purpose-made animation assets.
- The current interactive prompt was around 8,200 input tokens in the last test and should later be compressed.
- Current thresholds and probabilities are prototype values and still need controlled evaluation.
- Snapshot table cleanup is inferred from one global tag and the last completed service table, not a per-table dirt measurement. Hand occupancy is also a conservative socket-based interpretation. Do not treat either as a complete action-feasibility model.
- Purpose-specific contexts are implemented but not integrated into existing LLM requests. `MaxEntities` limits individual lists/tags, not total tokens; exact behavior and other known boundaries are in the technical reference.
- Copyright/licensing of every third-party environment and animation asset must be checked before public distribution.
- Do not return to VR implementation until the desktop research mechanism and evaluation design are stable.

Immediate priority: complete the snapshot acceptance matrix and service/seated-conversation regressions in game. Consider JSON export and research-logger integration as separate follow-up work. Commit/push only when requested, recording any remaining unverified cases.

