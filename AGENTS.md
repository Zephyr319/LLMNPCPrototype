# Codex Project Entry

Before working, read only these files in order:

1. `Docs/PROJECT_STATE.md`
2. `Docs/CURRENT_TASK.md`
3. `Docs/DECISIONS.md`
4. The relevant source files and the current Git diff

Treat the code and the three files above as the current source of truth. Root-level files such as
`PROJECT_HANDOFF.md`, `CLAUDE_NEXT_TASK.md`, and research notes are historical or detailed references;
do not let an older statement in them override the current documents.

Rules:

- The working tree may contain valuable uncommitted work. Never reset, discard, or rewrite unrelated changes.
- Continue only the scope in `Docs/CURRENT_TASK.md` unless the user changes the goal.
- Reuse the existing architecture and Blueprint-facing interfaces.
- After a meaningful change, update `Docs/CURRENT_TASK.md`. Update `PROJECT_STATE.md` or `DECISIONS.md`
  only when architecture or an accepted design decision actually changes.
- Record what was built, what was tested in PIE, and what remains unverified. A successful C++ build is not
  evidence that gameplay behavior passed.

UE build command:

```powershell
F:\UE_5.7\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe `
  LLMNPCPrototypeEditor Win64 Development `
  -Project=F:\ue5\LLMNPCPrototype\LLMNPCPrototype.uproject `
  -WaitMutex -NoHotReloadFromIDE
```
