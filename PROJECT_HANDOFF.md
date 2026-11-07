# LLMNPCPrototype Handoff

Updated: 2026-11-07 (JST)

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

Tentative topic:

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
- `NPCAutonomyComponent`: motives, memory triggers, plans, social seating and expression coordination
- `NPCActionQueueComponent`: execution only; reports movement/service execution facts to the service manager
- `NPCMemoryComponent`: episodic memory persistence and retrieval
- `NPCEmotionComponent`: short-term valence, arousal, mood and real-time decay
- `LLMClient`: prompts, OpenAI requests, JSON parsing and local action repair
- `NPCProactiveMenuHelpComponent`: context-sensitive menu help

The service flow is:

`Idle -> MaidApproachingPlayer -> ReceptionGreeting -> WaitingForReceptionResponse -> SelectingSeat -> GuidingToSeat -> WaitingForPlayerSeated -> WaitingForOrder -> OrderReceived -> GoingToCounter -> DeliveringOrder -> Served`

`CafeServiceManagerActor` is now the only service-state source read by the LLM, autonomy system and proactive-help system. The action queue retains only private execution data and publishes events to the manager.

## 6. Latest Work: Seated Conversation

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

## 7. Latest Work: Unified Service State

The previous implementation had two conflicting state sources:

- Interactive LLM prompts read static snapshots in the action queue.
- Autonomy read `CafeServiceManagerActor::CurrentState`.

This caused the player to be physically seated while autonomy still believed reception was active, so `sit_opposite_player` was rejected.

The current uncommitted implementation:

- Adds `FCafeServiceSnapshot` to `CafeServiceManagerActor`.
- Removes the action queue's public static service-state mirror.
- Makes the action queue publish guidance, table-area and arrival facts to the manager.
- Makes LLM dialogue, autonomy and proactive menu help read the manager snapshot.
- Adds `[CafeServiceState]` and `[CafeServiceSync]` logs.
- Reconciles direct LLM table guidance with the formal service state.

## 8. Verification State

- UnrealHeaderTool: succeeded
- Full Unreal Editor C++ build: succeeded
- Final incremental build after the latest fixes: succeeded
- No automated tests exist.
- The unified-state and persistent seated-conversation flow has **not yet received final in-game verification** after the latest refactor.

Required next test:

1. Enter the cafe and complete reception/table guidance.
2. Sit in the fixed player seat.
3. Say: `你坐我对面陪我聊一会儿。`
4. Continue two or three normal dialogue turns.
5. Say: `你可以起来了。`
6. Repeat with `那你坐下吧` to verify decision-only action repair.
7. While she is seated, request watering or another incompatible task and confirm that she stands before executing it.
8. Inspect `Saved/Logs/LLMNPCPrototype.log`, especially `[CafeServiceState]`, `[CafeServiceSync]`, `[NPCSeat]`, `[NPCSocial]` and `[AutonomyPlan]`.

## 9. Git and Build State

Latest committed revision at handoff:

`35bfe47 Add original maid character and seated conversation flow`

The working tree is intentionally dirty. Do not reset or discard it. Modified files:

- `Source/LLMNPCPrototype/Private/CafeServiceManagerActor.cpp`
- `Source/LLMNPCPrototype/Private/LLMClient.cpp`
- `Source/LLMNPCPrototype/Private/NPCActionQueueComponent.cpp`
- `Source/LLMNPCPrototype/Private/NPCAutonomyComponent.cpp`
- `Source/LLMNPCPrototype/Private/NPCProactiveMenuHelpComponent.cpp`
- `Source/LLMNPCPrototype/Public/CafeServiceManagerActor.h`
- `Source/LLMNPCPrototype/Public/NPCActionQueueComponent.h`
- `Source/LLMNPCPrototype/Public/NPCAutonomyComponent.h`

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
- Copyright/licensing of every third-party environment and animation asset must be checked before public distribution.
- Do not return to VR implementation until the desktop research mechanism and evaluation design are stable.

Immediate priority: run the seated-conversation test above, inspect the new unified-state logs, fix only observed failures, then commit and push the verified checkpoint.

