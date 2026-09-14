---
name: lore-vcs
description: Version control with Lore (EpicGames/lore) — Epic's open-source VCS for
  code + binary assets. Use for checkpoints before destructive edits, commit/push/sync,
  status/diff/history, and recovery points required by the automation contract.
  NOT for git repos (use shell git), NOT for asset-level undo (editor transactions handle that).
compatibility: Requires Revvy MCP server; lore CLI optional (degrade gracefully)
revvy_classes:
  - LoreService
keywords:
  - lore
  - vcs
  - version control
  - checkpoint
  - commit
  - source control
related_skills:
  - asset-management
  - ue-project-context
---

# Lore VCS — recovery points and commits for AI workflows

Lore = Epic's open-source VCS (June 2026, MIT, Rust). Centralized server-of-record,
content-addressed fragments, handles binary `.uasset` natively. UEFN uses it built-in;
a first-party UE plugin is on Epic's 2027 roadmap — until then Revvy's `LoreService`
is the integration.

## Preflight — ALWAYS first

```python
import unreal, json
info = json.loads(unreal.LoreService.get_workspace_info())
# {installed, version, is_workspace, workspace_root}
```

- `installed=false` → Lore not on this machine. Say so once, don't retry. Install:
  `irm https://raw.githubusercontent.com/EpicGames/lore/main/scripts/install.ps1 | iex`
- `is_workspace=false` → project not under Lore. Creating a workspace needs a server
  URL (`lore repository create lore://host:41337/name`) — that's a user decision, ask.
- Both true → all other calls work.

## Core mental model (differs from git!)

| Git concept | Lore equivalent | Gotcha |
|---|---|---|
| `git status` | `status --scan` | plain `status` does NOT scan the filesystem — editor-saved files invisible without `--scan` (LoreService always passes it) |
| `git add` | `stage` / `stage --scan .` | directory paths only stage already-dirty files unless `--scan` |
| `git commit -m` | `commit "<msg>"` | message is required positional; needs `identity` in `.lore/config.toml` — error "No commit identity configured" → tell user to set it |
| `git pull` | `sync` | there is NO pull command |
| `git push` | `push` | on conflict: sync → resolve → commit merge → push again |
| `git log` | `history --oneline` | |
| binary merge | none | conflicts = pick a side (`resolve mine|theirs`); locks are advisory-only in 0.x |

## Checkpoint pattern (automation contract "recovery point")

Before bulk/destructive changes (mass delete, landscape rebuild, batch retarget):

```python
result = json.loads(unreal.LoreService.checkpoint("pre: rebuild landscape material stack"))
# success + skipped=true → nothing to commit, fine. success=false → STOP, surface stderr,
# do NOT proceed with the destructive operation without user authorization.
```

After completing a multi-step task the user asked to persist:

```python
unreal.LoreService.stage_all()
print(unreal.LoreService.commit("feat: greybox blockout for arena map"))
# Push only when the user asks — push publishes to the server-of-record.
```

## Service API (all return JSON strings: {success, exit_code, stdout, stderr, error_code?})

`unreal.LoreService.` — discover live before use:
`discover_python_class("unreal.LoreService")`

- `get_workspace_info()` — preflight
- `status()` / `diff(paths)` / `history(length)` — read-only
- `stage(paths)` / `stage_all()` / `mark_dirty(paths)` — mark_dirty is the cheap
  save-hook variant (no content read)
- `commit(message)` / `checkpoint(message)` — local, no network
- `push()` / `sync()` — network; push publishes → confirm with user first

Error codes: `LORE_NOT_INSTALLED`, `LORE_NO_WORKSPACE`, `LORE_BAD_ARGS` — all are
normal states to report, not exceptions to retry.
