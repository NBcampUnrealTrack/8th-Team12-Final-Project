---
name: epic-mcp-toolsets
display_name: Epic Official MCP (UE 5.8)
description: Epic's built-in experimental MCP server that ships with UE 5.8 — how to
  enable it, what its toolsets cover, how it coexists with Revvy's MCP, and how to
  extend it. Use when the user asks about "Unreal MCP", the engine's own MCP plugin,
  or connecting external AI clients directly to the editor.
unreal_classes:
  - UToolsetDefinition
keywords:
  - epic mcp
  - unreal mcp
  - model context protocol
  - toolset
  - AICallable
  - GenerateClientConfig
  - "5.8"
related_skills: []
---

# Epic Official MCP (UE 5.8) Skill

> Knowledge pack, verified against Epic docs 2026-08. Experimental — APIs and data
> formats may change per release; 5.8.1 fixed tools/call response framing (5.8.0
> clients can see immediate disconnects).

## What ships in 5.8

Plugins **"Unreal MCP"** (`ModelContextProtocol` + Toolset Registry) and **"All
Toolsets"** (the default tool catalog, individually activatable). HTTP + SSE only
(no stdio/WebSocket), default `http://127.0.0.1:8000/mcp`, loopback-bound,
**no authentication layer** — never expose beyond the local machine. Tool calls
execute serially on the game thread.

Setup: enable both plugins → restart → Editor Preferences → General →
Model Context Protocol (auto-start) → console `ModelContextProtocol.GenerateClientConfig
ClaudeCode` (also Cursor/VSCode/Gemini/Codex — Codex emits TOML) writes `.mcp.json`
into the project root. `ModelContextProtocol.RefreshTools` reloads after authoring tools.

Toolsets include Scene / Actor / MaterialInstance / Object / AttributeSet(GAS) /
Editor / AutomationTest / LiveCoding / ConfigSettings / Logs. **Tool Search mode**
returns only 3 metatools (`list_toolsets` / `describe_toolset` / `call_tool`) so
hundreds of tools cost ~zero context.

## Extending it

- **Python (recommended):** class inheriting `unreal.ToolsetDefinition` under a
  plugin's `Content/Python/`, methods decorated `@toolset_registry.tool_call`,
  schemas from type hints + docstrings.
- **C++:** derive `UToolsetDefinition`, `UCLASS(BlueprintType, Hidden)`, static
  methods with `UFUNCTION(meta = (AICallable))`.
- **Dynamic:** implement `IModelContextProtocolTool`, register via
  `IModelContextProtocolModule::GetChecked().AddTool(...)`.

## Epic MCP vs Revvy — when to point at which

| Need | Use |
|---|---|
| Auth, remote/proxy access, in-editor chat, skills, checkpoints, idempotent retries | **Revvy** (port 8088 behind the proxy) |
| Terrain/PCG/Niagara/animation depth, deep_research, vision loop | **Revvy** (60+ services) |
| A stock external client with zero Revvy install, basic scene/actor/material ops | Epic MCP |
| Automation tests from an external client | Either — Revvy `run_automation_tests` returns structured per-test JSON |

Both can run simultaneously (different ports). Keep Epic's server disabled unless
needed — it is unauthenticated by design.
