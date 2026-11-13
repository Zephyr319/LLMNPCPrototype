# Claude Code Project Entry

Use the repository as shared memory between Claude and Codex. Do not reconstruct the project from old chat
history and do not reread every root-level report.

Read in this order:

1. `Docs/PROJECT_STATE.md`
2. `Docs/CURRENT_TASK.md`
3. `Docs/DECISIONS.md`
4. Current Git diff and only the source files relevant to the active task

The code and these three documents override older handoff/task files. Preserve all unrelated uncommitted
changes. Work only on `Docs/CURRENT_TASK.md` unless the user explicitly redirects the task.

Before editing, briefly report what is already implemented, what remains unverified, and the files you expect
to touch. After implementation, build the UE editor target, give exact PIE checks, and update
`Docs/CURRENT_TASK.md`. Do not mark runtime behavior as passed until a PIE/log result confirms it.

Build with the UnrealBuildTool command recorded in `AGENTS.md`.
