# Accepted Design Decisions

Updated: 2026-11-14 JST

## D-001: Actors and Components Own Gameplay Truth

The world snapshot is a read model captured on demand. Do not create a second writable state table that must be
synchronized manually.

## D-002: Keep Workflow, Posture, Position, and Activity Separate

Service phase describes where the cafe workflow is. Pose describes the player's body. Zone describes location.
Activity describes what the player is doing. One fact must not be inferred from another when an authoritative
source exists.

## D-003: Local Authority, LLM Semantic Judgment

Local code enforces physical feasibility, service/safety rules, occupied resources, and final execution. The LLM
interprets context and proposes dialogue, plans, appraisal, and expression. A rejected proposal must not be
described as if it executed.

## D-004: Planned and Expressive Behavior Are Separate Layers

Task continuity is handled independently from face, gaze, gesture, voice style, and later humming. Compatible
expression may accompany a task; an incompatible expression must be repaired, delayed, or rejected explicitly.

## D-005: Prefer Event-Driven LLM Calls

Continuous local values update without an LLM call. Invoke the LLM for meaningful context change, conversation,
new planning, interruption interpretation, or memory appraisal, not on a fixed short timer.

## D-006: Preserve Existing Blueprint Contracts

Additive C++ changes should retain legacy Blueprint-visible fields and events unless a migration is explicitly
planned and tested.

## D-007: Research Scope

The present contribution is the coordination and evaluation of task continuity and embodied response during
player intervention. Do not dilute the evaluation by adding VR or a large unrelated action library before the
core mechanism is stable.

## D-008: Handoff Discipline

The code plus `PROJECT_STATE.md`, `CURRENT_TASK.md`, and this file are the cross-agent source of truth. Update the
current task after meaningful work; update stable documents only when accepted architecture changes. Preserve the
dirty working tree and never erase another agent's unrelated changes.

## D-009: Suspended-Plan Arbitration - Local Gate, Scores as Evidence, LLM Decides

When one or more plans are suspended, local code only determines feasibility: invalid (goal gone),
blocked (service forbids it now), steps broken (goal valid, steps unusable), or ready. Only an invalid
plan is abandoned locally. Concern scores are evidence and produce a local default suggestion; they no
longer drop a plan by threshold. The LLM chooses resume, keep suspended, abandon, or replan for each
plan within the options the gate allows, may override the scores, and every override is logged. An
out-of-range or failed LLM answer falls back to the local default. The LLM call is skipped only when
there is a single ready plan and the local default is also "resume". The pool has a small fixed capacity.

Status: accepted design, not yet implemented. The current code still resolves a single suspended plan
locally (`ResolvePlanCompetition`). Implementation starts after the canonical-state PIE regression is
complete.
