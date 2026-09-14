# Revvy Blender Bridge

This first-party bridge exposes bounded typed 3D modeling and animation
operations. It does not accept Python, expressions, shell commands, generic
operators, or arbitrary code.

Modeling capabilities cover scene/mesh/UV inspection, non-destructive mesh
copies, bounded modifiers, material assignment, UV projection/unwrap/packing,
FBX/OBJ/glTF import, and FBX/OBJ/GLB export. Animation capabilities preserve the
existing Unreal-safe FBX round-trip contract.

## Install

1. Create the authenticated local config and add-on archive:

   ```powershell
   python install_revvy_blender_bridge.py
   ```

2. In Blender 5.0, open Preferences, choose **Install from Disk**, select the
   returned `revvy_blender_bridge.zip`, and enable **Revvy Blender Bridge**.

3. Register the stdio MCP server with its exact Python executable and script
   path:

   ```text
   python Plugins/Revvy/Content/Python/revvy_blender_mcp.py
   ```

Revvy's installer registers this server with `activation: "on_demand"`, so
ordinary health checks and engine-only tool discovery do not start the stdio
process or publish its 16 schemas. A Blender request in the Revvy app activates
it automatically. External clients can call
`manage_upstreams(action="activate", namespace="blender")` and then refresh
`tools/list`; clients that cache tools for a whole session should reconnect once.

For direct installation, pass an exact existing add-ons directory:

```powershell
python install_revvy_blender_bridge.py --addons-dir "C:\exact\scripts\addons"
```

Existing files are not replaced unless `--overwrite` is supplied.

## Security

- TCP binds only to `127.0.0.1`.
- Full-access and read-only requests use separate random tokens stored in the
  local Revvy security directory.
- Model, FBX, and `.blend` paths are restricted to the configured
  `exchange_root`.
- The Blender API is called only on Blender's main thread.
- Mutations are queued with stable job IDs and are polled instead of blindly
  retried after an ambiguous acknowledgement.
- Modeling edits require a new output object; the exact source mesh and
  fingerprint remain available for comparison.
- Animation adjustment copies the source action by default.
- Exact object, material, UV, armature, action, bone, and mesh names are
  required.

Use this health check after Blender has loaded the add-on:

```powershell
python ..\Python\revvy_blender_mcp.py --check
```
