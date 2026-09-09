---
name: revvy-external-control
description: Control Revvy from its Windows desktop app, a terminal, shell script, IDE, or local program through the persistent MCP proxy. Use for GUI chat with local Claude/Codex logins, Revvy CLI setup, connection diagnostics, cached tool or skill discovery while Unreal Editor is offline, scripted tool calls, editor restart recovery, and MCP client configuration.
---

# Revvy External Control

Use the standalone proxy as the stable frontend and Unreal Editor as the replaceable backend:

```text
desktop app / terminal / IDE / local program -> project proxy port -> project editor bridge port
```

The proxy survives editor restarts. Unreal mutations still require an open editor because they use editor-only APIs.

## Desktop App

Launch `Plugins\Revvy\RevvyApp.vbs` when the user wants a windowed app instead of a command prompt. Prefer `Binaries\RevvyApp\Revvy.exe` when present; build it with `BuildRevvyApp.ps1` when needed.

In the app:

1. Select the verified Unreal project folder.
2. Check proxy and editor status before making a request.
3. Select `Codex (ChatGPT login)` or `Claude Code (local login)` and an advertised model.
4. Discover a tool before any direct call; do not invent its name or arguments.
5. Verify editor state after every mutation.

The app stores preferences, transcripts, and local CLI session IDs under `<project>/.revvy`. It must reuse the existing CLI login and must not request or store an OpenAI API key. Machine credentials remain in the user security store. A Codex thread may resume after app restart; clear the session when the project, provider, or model changes.

## First Setup

From the Unreal project directory, run:

```powershell
Plugins\Revvy\revvy.cmd install --project .
Plugins\Revvy\revvy.cmd status
```

`install` merges the `Revvy` entry into `.mcp.json` and the Claude allow-list. It preserves existing servers and permissions.

## Standard Workflow

1. Query connection and active-project state.
2. List the exact tool name and schema.
3. Query current Unreal state before mutation; never guess an actor name, asset path, or property value.
4. Call one logical operation.
5. Re-query Unreal and verify the result.

```powershell
Plugins\Revvy\revvy.cmd status --json
Plugins\Revvy\revvy.cmd tools asset --json
Plugins\Revvy\revvy.cmd call manage_asset --arg action=search --arg search_term=Player --json
```

Use a UTF-8 JSON file when arguments contain arrays or nested objects. This avoids Windows shell quoting changes:

```powershell
Plugins\Revvy\revvy.cmd call TOOL_NAME --args-file .\revvy-args.json --json
```

Do not invent `TOOL_NAME` or argument keys. Read them from `tools --json` first.

## Offline Behavior

These operations remain available while Unreal Editor is closed or restarting:

- `status`
- `tools`
- `manage_skills` actions `list`, `suggest`, and `load`
- MCP `initialize` and `tools/list`

Editor-backed calls return `isError: true` and a nonzero CLI exit code while offline. Never automatically replay a mutation after reconnect because the original call may have partially completed. Wait for `status` to report `ue_running: true`, inspect current state, then decide whether a retry is still needed.

## Restart Recovery

```powershell
Plugins\Revvy\revvy.cmd status --json
# Restart Unreal Editor. Do not restart the proxy.
Plugins\Revvy\revvy.cmd status --json
```

The proxy reloads the backend port and bearer token for every request. A new editor process reconnects without creating a new external AI session.

## Process Commands

```powershell
Plugins\Revvy\revvy.cmd start
Plugins\Revvy\revvy.cmd stop
Plugins\Revvy\revvy.cmd restart
Plugins\Revvy\revvy.cmd serve
```

Use `serve` only for foreground diagnostics. Normal editor shutdown must not stop the standalone proxy.

## Programmatic MCP

Read `proxy_port` from `<project>/.revvy/revvy-proxy.json`, then send JSON-RPC 2.0 with `Content-Type: application/json` to one of two endpoints. `8089` is only the legacy/default example, not a cross-project constant:

- `http://127.0.0.1:<proxy_port>/mcp/safe` — read-only tools, no credential. This is the endpoint written into `.mcp.json`.
- `http://127.0.0.1:<proxy_port>/mcp` — every tool, and requires `Authorization: Bearer $REVVY_FULL_ACCESS_TOKEN`. Without the header it answers HTTP 401 and the MCP handshake never completes.

See "External Client Permissions" in the plugin README for how to obtain the bearer.

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/call",
  "params": {
    "name": "revvy_status",
    "arguments": {}
  }
}
```

Read `/health` for machine-readable proxy, editor, manifest, and active-project state. The server accepts only loopback connections and rejects non-local browser origins.

## Safety

- Confirm the exact target and current value before every mutation.
- Ask for explicit confirmation before asset deletion, overwrite, project-setting changes, reference-check bypasses, or changes affecting 100 or more actors or instances.
- Do not queue editor mutations while offline.
- Treat exit code `0` as success, `2` as usage/config error, `3` as proxy unavailable, and `4` as editor unavailable or tool failure.
- Verify through a read-only tool after every change; a successful transport response is not proof that Unreal state changed.
