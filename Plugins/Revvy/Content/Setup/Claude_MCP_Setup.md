# Revvy External MCP Setup

Revvy gives each project a persistent local proxy/editor port pair. Read `proxy_port` and `ue_port` from `<project>/.revvy/revvy-proxy.json`; `8089/8088` are only legacy/default values. The proxy remains available while Unreal Editor restarts.

## Windowed App

Run `Plugins\Revvy\RevvyApp.vbs` to use Revvy without a command window. The desktop app can control the persistent proxy, show Unreal status, chat through the existing Claude Code or Codex/ChatGPT login, inspect tools, and make direct calls. Build `Binaries\RevvyApp\Revvy.exe` with `Plugins\Revvy\BuildRevvyApp.ps1` when a standalone executable is required.

## Requirements

- Python 3 on `PATH`
- Revvy enabled in the Unreal project
- Unreal Editor launched once to export the full tool manifest and provision its backend token

## Install

Run from the project root:

```powershell
Plugins\Revvy\revvy.cmd install --project .
```

This command:

1. creates the project-local proxy config under `<project>/.revvy`;
2. merges `Revvy` into the existing `.mcp.json` without removing other servers;
3. adds `Revvy` to `.claude/settings.local.json` without replacing permissions;
4. starts the standalone proxy.

Do not put bearer tokens in the plugin directory or source control. Unreal generates and synchronizes the backend token automatically.

## Verify

```powershell
Plugins\Revvy\revvy.cmd status --json
Plugins\Revvy\revvy.cmd tools --json
Plugins\Revvy\revvy.cmd call revvy_status --json
```

`status` returns exit code `4` when the proxy is healthy but Unreal Editor is offline. Cached `tools` and proxy-owned `manage_skills` calls remain available in that state.

## Editor Restart

Leave the proxy running and restart only Unreal Editor:

```powershell
Plugins\Revvy\revvy.cmd status --json
# Restart Unreal Editor here.
Plugins\Revvy\revvy.cmd status --json
```

The proxy reloads the editor port and token for each request. Do not automatically replay failed mutations after reconnect; inspect current Unreal state first because a timed-out operation may have partially completed.

## Terminal Calls

```powershell
Plugins\Revvy\revvy.cmd tools asset --json
Plugins\Revvy\revvy.cmd call manage_asset --arg action=search --arg search_term=Player --json
```

For nested arguments, use a UTF-8 JSON file to avoid shell quoting changes:

```powershell
Plugins\Revvy\revvy.cmd call TOOL_NAME --args-file .\revvy-args.json --json
```

## Programmatic MCP

Local programs read `<proxy_port>` from the project runtime config and send JSON-RPC 2.0 requests to one of two endpoints:

| Endpoint | Tools | Authentication |
|---|---|---|
| `http://127.0.0.1:<proxy_port>/mcp/safe` | Read-only tools only | None. Written into `.mcp.json` by the installer and by Unreal Editor |
| `http://127.0.0.1:<proxy_port>/mcp` | Every tool, including mutations | `Authorization: Bearer $REVVY_FULL_ACCESS_TOKEN`, otherwise HTTP 401 |

Health information is available at `http://127.0.0.1:<proxy_port>/health`. Full diagnostics (external MCP server output, config paths) are returned only to a caller that presents the Full bearer.

To give your own client Full access: pin a `REVVY_FULL_ACCESS_TOKEN` of at least 32 characters for your user account, open a new terminal, run `revvy.cmd restart` so the proxy adopts it, then

```powershell
Plugins\Revvy\revvy.cmd grant-full --project .
```

which adds a second `Revvy-Full` server entry carrying `Authorization: Bearer ${REVVY_FULL_ACCESS_TOKEN}`. The read-only `Revvy` entry stays, so both permission levels remain available. `revvy.cmd revoke-full --project .` removes it. See "External Client Permissions" in the plugin README for the full explanation.

The proxy binds only to loopback and rejects non-local browser origins. Do not expose either Revvy port through a firewall, port forward, or public reverse proxy.

## Process Control

```powershell
Plugins\Revvy\revvy.cmd start
Plugins\Revvy\revvy.cmd stop
Plugins\Revvy\revvy.cmd restart
Plugins\Revvy\revvy.cmd serve
```

Use `serve` for foreground diagnostics. Normal Unreal Editor shutdown does not stop the standalone proxy.

## Troubleshooting

| Problem | Action |
|---|---|
| Proxy unavailable, exit `3` | Run `revvy.cmd start`; inspect `<project>/.revvy/proxy.log` |
| HTTP `401` from `/mcp` | You are on the Full endpoint without a bearer. Use `/mcp/safe`, or follow "External Client Permissions" in the README |
| `Safe (read-only) access` notice from `revvy.cmd` | The running proxy belongs to another process. Pin `REVVY_FULL_ACCESS_TOKEN` and run `revvy.cmd restart` from that shell |
| Mutating tool refused as `Safe/read-only` | The client is on `/mcp/safe`. Switch the desktop app to `Full access`, or wire the bearer into your own client |
| Editor offline, exit `4` | Launch the matching Unreal project and wait for `ue_running: true` |
| Wrong active project | Check `project_path` from `/health`; close the other editor instance |
| No cached tools | Launch Unreal once so Revvy exports `tools-manifest.json` |
| Port change shows `restart_required` | Run `revvy.cmd restart` once |
| Python missing | Install Python 3 and confirm `python --version` succeeds |
