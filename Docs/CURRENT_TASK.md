# Current Task

Updated: 2026-11-14 JST

## Status

Canonical state-source integration: build passed and two PIE runs (2026-11-14) satisfy the canonical-state
acceptance criteria. The P plan-panel flicker fix is built and confirmed by the user. The 7b move fix is built
but its exact scenario (invitation after she approaches the seat) was not reproduced. A new failure was found:
spoken replies promise an action that local code then rejects (see "Failure: reply/execution mismatch").
That failure belongs to the coordination work of D-009 rather than a separate patch.

## Completed in the Latest Change

- Added canonical service phases independent of player posture and location.
- Added player service presence, physical pose, zone, table-area membership, and activity to the world snapshot.
- Made interactive dialogue, autonomous decisions, and proactive/social dialogue read the same snapshot model.
- Reused one captured snapshot inside an interactive request to avoid duplicate and potentially inconsistent
  observations.
- Removed the prompt rule that treated table-area presence as proof of sitting.
- Made `sit_opposite_player` require actual physical player seating.
- Preserved legacy fields and Blueprint-facing interfaces.
- Full C++/UHT build passed.

## PIE Checkpoint Definitions

Run `Cafe.Snapshot log` at these checkpoints and keep the relevant log lines:

1. Player enters reception: phase `reception`, present true, pose standing/walking, zone entrance.
2. Guidance starts: phase `guiding`, activity `following_npc`; pose must remain physical.
3. Player presses F to sit: phase `waiting_for_order`, pose `seated`, seated true.
4. Player leaves the seat: pose changes to standing/walking immediately; table-area or service-readiness flags
   must not make dialogue claim the player is still seated.
5. Order is delivered: phase `served` regardless of whether the player later stands.
6. Meal confirmation and completion: phases `meal_confirmation` then `post_meal`.
7. Ask the NPC to sit opposite while the player is standing: request must not start. Repeat while physically
   seated: it may proceed if an appropriate seat is available.

Also check ordinary dialogue, one autonomous decision, and one proactive/social utterance to confirm each context
reports the same phase/pose/zone combination.

## PIE Run 1 Results (2026-11-14)

`Cafe.Snapshot log` was not executed in this run. Evidence comes from the `[DialogueContext]` line written for
each player utterance (same phase/pose/zone fields). Times are log timestamps (UTC, 2026.11.13).

| Checkpoint | Time | Observed | Result |
| --- | --- | --- | --- |
| 1 Reception | 17:30:23 | phase `reception`, present, pose `standing`, zone `entrance` | Pass |
| 2 Guidance | - | no player utterance during guiding, no context captured | Not covered |
| 3 Seated | 17:30:45, 17:31:09 | phase `waiting_for_order`, pose `seated`, seated true | Pass |
| 4 Left seat | 17:34:50 | seated false, pose `standing`, in_table_area true, zone `Table_01` | Pass (table area did not imply seated) |
| 5 Served | 17:32:26, 17:33:29 | phase `served` | Pass |
| 6 Confirmation / post-meal | 17:34:50, 17:36:44 | `meal_confirmation` then `post_meal`; no regression to `reception` after standing | Pass |
| 7a Sit-opposite while standing | - | not attempted | Not covered |
| 7b Sit-opposite while seated | 17:32:26 | accepted verbally, NPC never sat | **Fail (not caused by snapshot change)** |
| Cross-context consistency | - | only dialogue context logs full state; autonomy logs decision only; social opportunity did not trigger | Not verified |

### Failure 7b: seat move failed because she was already at the seat

- Setup: order served, player seated at `Chair_01_B`. Autonomy plan `sit_with_player` (goal お客様とお話ししたい。)
  moved the NPC to the `Chair_01_A` stand point (X=-186, Y=140) and asked
  少しゆっくりお話ししたいのですが、向かいの席に座ってもいいですか？
- Input (17:32:26): player typed `请坐，谢谢`.
- Visible result: NPC replied お言葉に甘えて、こちらに座らせていただきますね。 and then walked to the counter
  without sitting. The player later asked why she left.
- Log excerpt (17:32:34, same frame): `ExecuteMoveAction Target=Chair_01_A` (same stand point) /
  `MovementMode changed: Idle` / `NPC queued move failed: Furniture Chair_01_A` /
  `Reserved seat not found for NPC`; 17:32:37 `[AutonomyPlan] cleared. Reason=plan_step_failed`.
- Cause (from code, consistent with the log): only one seat command was issued (an earlier "double command"
  guess was wrong). The seat sequence first moves to the seat stand point, where she was already standing. The
  navigation query returned no usable path, and furniture moves require a path, so
  `StartMovingToLocation` failed synchronously ("Scene target path failed", shown on screen only). The move
  failure released the seat reservation, so the following enter-seat step failed. Any move to the point she is
  already standing on had the same failure.
- Fix (2026-11-14, `NPCMoveToTableComponent.cpp`, built; exact scenario not yet reproduced in PIE):
  - If no path is found but she is within `AcceptanceRadius` (2D) of the target, treat it as arrival: set a
    one-point path so the next tick completes through the normal `FinishMoveToTable` success path. Logged as
    `[NPCMove] already at target`.
  - `FailMoveToTable` now writes `[NPCMove] move failed. Reason=... Target=... Message=...` to the log.
- Retest: let her invite herself (or ask while seated), answer 请坐, and confirm she sits at `Chair_01_A` and the
  seated conversation opens. Also confirm normal guide, delivery, and counter moves are unchanged.

### Fix: P plan panel flicker (2026-11-14, built; user confirmed fixed in PIE run 2)

- Symptom (reported by the user): the P plan panel flickers and jumps.
- Cause (from code): `UCafePlanPanelWidget::RefreshContent` ran every 0.2 s and cleared and recreated every
  `STextBlock`. New rows with `AutoWrapText` lay out unwrapped on their first frame and wrap on the next, so long
  lines and everything below them jumped five times a second.
- Fix (`CafePlanPanelWidget.h/.cpp` only): rows are created once and updated in place (text, color, font,
  padding only when changed; unused rows collapsed), and wrapping uses a fixed width
  (`PanelWidth - 2 * padding`) instead of `AutoWrapText`. Data source, refresh rate, hotkey and placement are
  unchanged.
- Retest: press P, watch for a few seconds while her plan and concerns change; text should update without
  flicker or vertical jumping. Press P again to close and reopen once.

### Minor observations (no acceptance impact)

- 17:36:12 standing up from `sofa01` after `post_meal` logged "meal completion confirmation started", but
  `ScheduleMealCompletionConfirmation` returned early because departure was already confirmed. Misleading log
  text only.
- 17:31:42-17:31:53 right after delivery, fixed lines お会計は800円です。 and ご来店ありがとうございました。 played
  while the player was still seated. Pre-existing scripted flow, not part of this change.
- VOICEVOX was offline; its connection errors are unrelated.

## PIE Run 2 Results (2026-11-14, log opened 02:48 JST)

Both fixes (7b move, P panel) were built for this run. `Cafe.Snapshot log` was again not executed; evidence is the
`[DialogueContext]` line per utterance plus the social-opening context. Times are log timestamps (UTC, 2026.11.13).

| Check | Time | Observed | Result |
| --- | --- | --- | --- |
| 2 Guidance | 17:50:03 | phase `guiding` (raw `WaitingForPlayerSeated`), pose `standing`, seated false, in_table_area true | Pass |
| 7a Sit-opposite while standing | 17:50:12 | local enforcement skipped (player not seated), LLM actions `[]`, she did not sit; reply お席に着きますね。 | Execution pass, **reply fail** |
| Sit-opposite while seated (direct request) | 17:50:47 | `sit_with_player` started, entered `Chair_01_A`, seated opening triggered | Pass |
| Sit-opposite while seated | 17:52:00 | `explicit sit request failed ... CanRun=false` (post-service bow, movement paused); reply お話ししましょう。 | **Fail (reply/execution mismatch)** |
| Sit-opposite while seated | 17:52:49 | during `organize_counter` plan: `Started=false`, no reason logged; reply もちろんです。お話ししましょう。 | **Fail (reply/execution mismatch)**; start-failure cause not yet investigated |
| Sit-opposite while seated | 17:53:16 | started and sat | Pass |
| Cross-context consistency | 17:50:47 / 17:51:03 | dialogue context and seated social-opening context both report `waiting_for_order`, seated, pose `seated`, zone `Table_01` | Pass (dialogue vs social) |
| 7b invitation path | - | an invitation was queued (17:51:45) but the player spoke first; no `[NPCMove] already at target` line | Not reproduced |
| P plan panel | 17:49:23 | user confirmed: no flicker or jumping | Pass |

Canonical-state acceptance: no context reported `seated` after the player stood; table-area membership never
changed pose; the phase did not regress to `reception` after standing; reception, order, delivery, cleanup and
seated-conversation flows ran.

### Failure: reply/execution mismatch

- Pattern: the strategy reply is spoken in the same frame the action is attempted. When local code rejects or
  fails to start the action, the reply has already promised it and no result reaches the dialogue. This violates
  D-003 ("A rejected proposal must not be described as if it executed").
- 7a case: the snapshot said the player was not seated, yet the LLM returned decision `sit_opposite_player`
  with no action and a reply that promised to sit (the LLM ignored state).
- 17:52:00 and 17:52:49 cases: the rejection came from transient local state (service bow; activity start
  failure) that the LLM could not know in advance.
- Plan: handle in the D-009 coordination work (validate or execute first, then let the reply reflect the real
  result), not as a separate patch. Open item: find why `RequestSitOppositePlayer` returned `Started=false` at
  17:52:49 (no reason logged).

## Next Action

1. Commit a stable snapshot to Git before D-009 work (last commit 2026-11-07).
2. Start D-009 implementation (`Docs/DECISIONS.md`), including the reply/execution mismatch above.
3. Opportunistic retest: when she approaches a seat and invites herself, answer 请坐 and check for
   `[NPCMove] already at target` and a successful sit (7b invitation path).

## Acceptance Criteria

- No context reports `seated` after `UPlayerSeatComponent::bIsSeated` becomes false.
- Table-area membership never changes physical pose.
- Service phase does not regress to reception merely because the player stands up.
- Existing reception, order, delivery, cleanup, and seated-conversation flows still work.
- Any failure is recorded with the exact input, visible result, and matching log excerpt before code changes resume.

## Do Not Change Yet

- Memory, emotion, boredom, energy, concern, or recall formulas.
- Plan-generation frequency or action library.
- Blueprints, animations, character assets, or seat offsets unless a failing checkpoint proves they are involved.
