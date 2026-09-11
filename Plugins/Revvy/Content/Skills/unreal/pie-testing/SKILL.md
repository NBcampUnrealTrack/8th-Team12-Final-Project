---
name: pie-testing
display_name: Play-In-Editor Testing
description: Start, stop, and query Play-In-Editor (PIE) sessions for runtime testing
  of Blueprints, gameplay logic, widgets, AI, and any in-game behavior. Use when the
  user asks you to "play", "test", "run", "PIE", "start the game", "stop the game",
  or otherwise needs a live game world to validate changes.
revvy_classes:
  - WidgetService
unreal_classes:
  - UEditorEngine
  - FRequestPlaySessionParams
keywords:
  - pie
  - pie_control
  - play in editor
  - play
  - run
  - test
  - testing
  - simulate
  - gameplay
  - runtime
  - start game
  - stop game
  - end play
---

# Play-In-Editor (PIE) Testing Skill

PIE is the only way to validate runtime behavior — Blueprint logic, AI ticking, animation, input, widget interaction, gameplay events. Without starting PIE, your "fix" is unverified.

## Preferred path: the `pie_control` MCP tool (2026-08)

First-class tool, no Python hop, richer contract:

| Call | Result |
|------|--------|
| `pie_control action=start` | Requests a session in the active viewport. `map=/Game/Maps/X` overrides the level (validated), `simulate=true` skips player possession. Returns `{requested:true}` — the transition is deferred a frame. |
| `pie_control action=status` | `state: stopped\|playing\|simulating` + `map`, `play_time_seconds`, `num_player_controllers`, `is_paused`. Poll this after start/stop. |
| `pie_control action=stop` | Requests session end; confirm with status. Errors with `PIE_NOT_RUNNING` when nothing plays. |

Pair with `run_automation_tests` (see `automation-testing` skill) when what you actually want is a pass/fail verdict rather than a live world to poke.

## Python fallback (WidgetService)

PIE control also lives on `WidgetService` for historical reasons (it grew out of widget testing) — use it when already inside an `execute_python_code` block.

| Method | Description |
|--------|-------------|
| `unreal.WidgetService.start_pie()` | Start PIE if not already running. Returns `True` on success or if already running. |
| `unreal.WidgetService.stop_pie()` | End the current PIE session. Returns `True` if stopped or already stopped. |
| `unreal.WidgetService.is_pie_running()` | `True` if PIE or Simulate-In-Editor is active. |

## Standard Test Loop

```python
# 1. Make sure you're starting from a clean state
if unreal.WidgetService.is_pie_running():
    unreal.WidgetService.stop_pie()

# 2. Start the session (uses the editor's current PIE settings — default map, viewport)
unreal.WidgetService.start_pie()

# 3. Let the test run / inspect log output / interact via other services
# 4. Stop when done
unreal.WidgetService.stop_pie()
```

## Gotchas

- **PIE start is asynchronous.** `start_pie()` returns immediately after `RequestPlaySession` is queued. The world isn't actually playing until the editor processes the request on its next tick. If you need to act inside the running world, give it a tick or poll `is_pie_running()`.
- **Already-running is treated as success.** `start_pie()` returns `True` if a PIE session already exists — it does NOT restart. Stop first if you need a fresh session.
- **`stop_pie()` cleans up tracked PIE widgets** (those spawned via `WidgetService.spawn_widget_in_pie`). Other widgets/actors are torn down by the engine as part of `RequestEndPlayMap`.
- **Save before starting.** Dirty asset changes are NOT picked up by PIE unless saved/compiled. Always `compile_blueprint(...)` before launching PIE to test Blueprint changes.
- **Don't leave PIE running between tasks.** Subsequent edits (recompiles, asset moves, hot reload) can fail or behave oddly while a PIE world is alive. Call `stop_pie()` before returning control to the user.

## When to use PIE

- Verifying a Blueprint event fires (combine with log inspection — see the `read_logs` tool, backed by `LogReaderService`)
- Validating gameplay logic (damage, scoring, state transitions)
- Testing widgets in their real runtime context (combine with `umg-widgets` skill's `spawn_widget_in_pie`)
- Reproducing user-reported runtime bugs

## When NOT to use PIE

- Pure asset/editor validation (use `compile_blueprint`, `search_assets`, etc.)
- Static introspection (use `get_nodes_in_graph`, `get_node_pins`)
- Anything you can verify without a live world — PIE is slow, save it for genuine runtime checks.
