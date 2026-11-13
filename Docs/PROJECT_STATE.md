# Current Project State

Updated: 2026-11-13 JST

## Project

- Unreal Engine 5.7 desktop research prototype in `F:\ue5\LLMNPCPrototype`.
- Main map: `/Game/RestaurantDemo/Maps/Map_RestaurantDemo`.
- Original VRoid cafe maid: Sumiha Shinomori / 篠森 澄羽.
- Research focus: continuity and embodied consistency when a player interrupts an LLM NPC's ongoing plan.
- VR is outside the current scope.

## Implemented Experience

- Reception, table guidance, player seating, ordering, food delivery, departure confirmation, and cleanup.
- Autonomous short plans, task interruption/suspension/resumption, daily actions, props, and navigation.
- Episodic memory, recall/appraisal, short-term emotion, mood decay, expressions, blinking, and startle response.
- Proactive and seated conversation, including NPC seating and upper-body performance.
- Baseline/full experiment switches, structured research logs, trial commands, and analysis tooling.
- Continuous plant moisture, target binding/reservation, NPC/player watering, and moisture display.

Implemented does not mean every path has passed the latest in-game regression test.

## Runtime Architecture

- `ACafeServiceManagerActor`: authoritative service workflow and `FCafeServiceSnapshot`.
- `UCafeWorldSnapshotSubsystem`: demand-driven read model. It rereads actors/components on capture; it is not a
  writable duplicate state table and does not tick.
- `UNPCAutonomyComponent`: motives, plans, concerns, memory triggers, interruption and embodied coordination.
- `UNPCActionQueueComponent`: movement, animation, seating, prop, and queued execution.
- `UPlayerSeatComponent`: authoritative physical player seating.
- `UNPCMemoryComponent` / `UNPCEmotionComponent`: memory and affect state.
- `ULLMClient`: prompts, requests, structured response parsing, and local validation/repair.

## Canonical State Model

`FCafeWorldSnapshot` schema version is 4. Service workflow, player posture, player position, and activity are
separate facts:

- Service phase: `reception`, `guiding`, `waiting_for_order`, `order_in_progress`, `served`,
  `meal_confirmation`, or `post_meal`.
- Player presence: actor existence is separate from participation in the current service scene.
- Player pose: `absent`, `standing`, `walking`, or `seated`.
- Player zone and table-area membership describe position only; they never prove that the player is seated.
- `bSeatedForService` is retained only as a legacy workflow-readiness field. Physical posture comes from
  `UPlayerSeatComponent` / `FCafePlayerSnapshot::bSeated`.

Interactive dialogue, autonomous planning, and proactive/social dialogue now obtain these shared facts from the
world snapshot. One interactive request reuses the same captured snapshot for its top-level service/player facts
and embodied behavior context.

## LLM Boundary

Local code owns physical truth, feasibility, service rules, action execution, and persistence. The LLM handles
semantic interpretation, dialogue strategy, short-plan proposals, memory appraisal, and compatible expression
intentions. Local validation may reject or repair an LLM proposal.

Task-level behavior and expressive behavior are separate layers: an expression, gaze, or compatible gesture can
accompany a task without silently replacing that task.

## Current Verification

- `LLMNPCPrototypeEditor Win64 Development`: passed on 2026-11-13 after the canonical-state integration.
- Latest canonical-state change has not yet completed its PIE regression matrix.
- Use `Cafe.Snapshot log` during PIE to inspect the readable state.
- VOICEVOX connection errors are unrelated when its local server is intentionally offline.

## Known Limits

- Animation variety is still limited; humming audio is not implemented.
- Some expression/gesture assets are substitutes.
- Interactive prompts remain large and should be compressed only after behavior is stable.
- Prototype thresholds still require controlled evaluation.
- Asset licenses must be checked before public distribution.
- The working tree is intentionally dirty; inspect it before every task and never reset unrelated work.

Detailed historical design and experiment notes remain in the root-level Markdown files. Read them only when a
task specifically needs that detail.
