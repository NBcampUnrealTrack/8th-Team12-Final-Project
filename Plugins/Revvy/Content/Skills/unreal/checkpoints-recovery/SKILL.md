---
name: checkpoints-recovery
display_name: Checkpoints & Recovery
description: Restore the previous on-disk version of assets Revvy tools have overwritten.
  Use when the user says "revert", "rollback", "restore", "undo that save", "이전
  버전으로 되돌려", or after a tool-driven change turned out wrong.
unreal_classes:
  - UPackage
keywords:
  - checkpoint
  - restore
  - rollback
  - revert
  - undo save
  - recover
  - backup
  - previous version
related_skills: []
---

# Checkpoints & Recovery Skill

Editor transactions (Ctrl+Z) cover in-session undo. **Once a tool saves, the previous on-disk version is gone — except Revvy checkpoints it automatically.** Whenever a Revvy tool execution saves over an existing file under `Content/`, the pre-save version is copied to `Saved/Revvy/Checkpoints/<day>/` first, tagged with the tool that caused it.

## Recovery flow

```text
1. manage_checkpoints action=list, package_filter=BP_Player   # newest first
2. manage_checkpoints action=restore, id=<from list>          # file back + auto reload
```

- `restore` **refuses when the package has unsaved in-memory edits** — save or revert them, or pass `force=true` to discard deliberately. This is the destructive direction; list first, restore deliberately.
- A restored package that is loaded gets reloaded in-editor automatically; if the reload fails the file is already restored on disk and an editor restart shows it.

## What is (and is not) captured

| Captured | Not captured |
|---|---|
| Saves during any Revvy tool execution that overwrite an existing `Content/` file | First save of a brand-new asset (no previous version exists) |
| The tool name responsible, in the manifest | Manual Ctrl+S saves (default `bRevvyToolScopeOnly=true`) |
| | Cook/autosave targets outside `Content/` |

## Budget & config

14 days / 1 GB by default, pruned automatically (oldest day first). `Saved/Config/Revvy.ini`:

```ini
[Revvy.Checkpoints]
bEnabled=True
bRevvyToolScopeOnly=True   ; False = checkpoint EVERY editor save
MaxAgeDays=14
MaxTotalMB=1024
```

`action=status` reports mode, storage dir, entry count and total size. `action=prune` trims early with explicit `max_age_days` / `max_total_mb`.
