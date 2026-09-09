# Blender MCP and FBX Roundtrip Research

## Sources reviewed

- [ahujasid/blender-mcp](https://github.com/ahujasid/blender-mcp), MIT, inspected at
  `6641189231caf3752302ae20591bc87fda85fc4e` (v1.6.0). It is easy to install,
  but advanced work depends on unauthenticated `execute_blender_code` and it
  includes telemetry. Revvy does not expose that arbitrary-code surface.
- [djeada/blender-mcp-server](https://github.com/djeada/blender-mcp-server),
  MIT, inspected at `7eed33edf4aca2ab0ca84a6da27321f89f68b504`
  (v0.1.3). Its stdio-to-loopback bridge, typed tools, allowlist, and tests are
  the architectural baseline. It lacks a typed FBX import and animation-edit
  contract, so Revvy supplies those itself.
- [sandraschi/blender-mcp](https://github.com/sandraschi/blender-mcp) has broad
  animation/action/NLA operations. Its schemas informed the typed edit
  operations, but Revvy does not adopt its general code execution or
  `bake_space_transform=True` Unreal preset.
- [PatrykIti/blender-ai-mcp](https://github.com/PatrykIti/blender-ai-mcp) has
  useful typed armature and FBX tools and reports Blender 5.0 testing. Its
  default all-interface TCP binding is not appropriate for Revvy's local-only
  threat model.

## Official pipeline constraints

- [Blender FBX export API](https://docs.blender.org/api/current/bpy.ops.export_scene.html):
  `bake_space_transform` is experimental and known to break armatures or
  animations. Revvy keeps it disabled, disables leaf bones and NLA/all-action
  expansion, samples every frame, exports all required bones, and disables
  simplification.
- [Blender FBX import API](https://docs.blender.org/api/current/bpy.ops.import_scene.html):
  Revvy imports animation with leaf-bone filtering and explicit axis settings.
- [Unreal FBX animation import](https://dev.epicgames.com/documentation/en-us/unreal-engine/importing-animations-using-fbx-in-unreal-engine):
  animation-only FBX import requires an existing Skeleton, and the UE FBX
  pipeline expects FBX 2020.2-compatible data.
- [Unreal AssetExportTask API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UAssetExportTask)
  and [AssetImportTask API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/UnrealEd/UAssetImportTask)
  are the automation boundary used by the Revvy helper.
- [MCP security best practices](https://modelcontextprotocol.io/docs/tutorials/security/security_best_practices):
  the Revvy bridge uses stdio for MCP, loopback-only TCP for Blender, a local
  authentication token, typed tools, and a project-scoped exchange directory.

## Adopted safety boundary

- Blender listens only on `127.0.0.1`.
- Every bridge request carries the token from
  `%LOCALAPPDATA%/Revvy/Security/blender-bridge.json` (or the
  `REVVY_BLENDER_BRIDGE_CONFIG` override).
- File operations resolve and remain under
  `<Project>/Saved/Revvy/BlenderExchange`.
- No arbitrary Python execution tool is exposed.
- Names are never guessed. Unreal asset, Skeleton, Blender armature, action,
  mesh, and bone names must come from an inspection result.
- Edits create a copied Blender action by default.
- Unreal import creates a new validation asset by default. Source replacement
  is a separate, explicit user-approved step.
