---
name: blender-3d-modeling
description: Inspect, modify, validate, and export Blender 3D models through granular typed MCP tools. Use automatically for Blender mesh or topology edits, object transforms, modifiers, materials, UV changes, model import/export, 3D modeling, mesh editing, 블렌더, 3D 모델링, 메시 수정, 토폴로지, 머티리얼, or UV requests; stop safely when the installed Blender MCP lacks the required modeling capability.
---

# Blender 3D Modeling

Use granular typed Blender MCP operations, preserve the source, and prove the result through before-and-after inspection.

## Critical Rules

1. Activate the typed Blender namespace with `manage_upstreams(action="activate", namespace="blender")`, refresh `tools/list`, then treat that refreshed list as the authority. Do not infer modeling support from Blender being installed or from animation tools being available.
2. Never use arbitrary Python, code execution, console commands, UI macros, or a generic script tool as a substitute for a missing typed modeling capability.
3. Never overwrite the input `.blend`, imported model, source object, or export file. Create a backup and require a new output object name or output path.
4. Inspect before every mutation and re-inspect after it.
5. Perform one logical mutation per call. Keep an exact record of changed objects.
6. If a mutating call times out or loses its acknowledgement, poll its stable `job_id`; never retry the mutation blindly.
7. Use `blender-animation-roundtrip` for armature, action, keyframe, timing, or animation FBX changes. Load both skills only when a task truly changes mesh data and animation.

## Typed Tool Capability Contract

The installed Blender MCP may expose only the baseline animation tools. A 3D modeling request must not proceed until every tool required by that request is present.

### Read-Only Capabilities

| Tool | Contract |
|---|---|
| `blender__blender_bridge_status` | Confirm Blender, add-on, bridge, exchange root, and connection readiness |
| `blender__blender_scene_inspect` | Return exact object IDs/names, types, transforms, visibility, parent links, collections, and active scene |
| `blender__blender_mesh_inspect` | Return mesh counts, bounds, normals/manifold diagnostics, material slots, UV maps, modifiers, and source fingerprint |
| `blender__blender_uv_inspect` | Return UV layer names, active layer, bounds, non-finite/degenerate/out-of-range diagnostics, and mesh fingerprint |
| `blender__blender_job_status` | Poll a mutating job by `job_id` without starting another mutation |

### Mutating Capabilities

Every mutating tool must return a stable `job_id` when asynchronous, identify the new or changed outputs, and provide structured warnings or validation errors.

| Tool | Required safety contract |
|---|---|
| `blender__blender_model_import` | Import `path` under a new `name_prefix`; never replace matching objects |
| `blender__blender_mesh_edit` | Accept an inspected source object, typed `operations`, and a required new `output_object_name`; preserve the source object |
| `blender__blender_modifier_apply` | Apply one typed modifier configuration to a copied object with a new `output_object_name` |
| `blender__blender_material_assign` | Assign verified material slots to a copied object with a new `output_object_name` |
| `blender__blender_uv_edit` | Apply one typed unwrap, projection, or packing operation to a copied object with a new `output_object_name` |
| `blender__blender_model_export` | Export explicit inspected objects to a new `path` with `overwrite:false` |
| `blender__blender_save_copy` | Save the working scene to a new `.blend` path with `overwrite:false` before risky changes and at sign-off |

If a required tool is absent, report the missing tool names and leave the scene unchanged. The baseline seven-tool animation bridge is not a modeling bridge.

Use only values advertised by the live schemas:

- model import: `.fbx`, `.obj`, `.glb`, `.gltf`, always with `name_prefix`;
- mesh edit operations: `transform_object`, `transform_vertices`, `merge_by_distance`, `recalculate_normals`, `triangulate`, `delete_loose`, `shade_smooth`;
- modifiers: `bevel`, `subdivision`, `solidify`, `decimate`, `mirror`, `triangulate`;
- UV operations: `smart_project`, `cube_project`, `unwrap`, `pack_islands`;
- model export: `.fbx`, `.obj`, `.glb`, with an explicit `object_names` list.

The mesh edit tool operates on the whole copied object or whole copied mesh. It
does not expose arbitrary vertex-selection expressions, BMesh scripts, Geometry
Nodes programs, or generic Blender operators. If the requested modeling
operation is not in the schema, report that exact capability gap.

## Workflow

### 1. Preflight

1. Confirm the user supplied or implied an exact source file and target object.
2. Call `manage_upstreams(action="activate", namespace="blender")` and refresh the tool list. If the client caches tools for the whole session, reconnect once as instructed by the activation result.
3. Call `blender__blender_bridge_status`.
4. Check the refreshed tool list for every required typed capability.
5. Reject an overwrite destination and select a new output path.

Do not launch a mutation merely to test whether a tool exists.

### 2. Establish a Baseline

Call scene, mesh, and UV inspection as applicable. Record:

- stable source object identifier and source fingerprint;
- vertex, edge, polygon, triangle, object, and material-slot counts;
- transforms, dimensions, bounds, parents, and collection membership;
- modifiers and their order;
- UV layers and diagnostics;
- non-manifold edges, degenerate geometry, invalid normals, loose geometry, and NaN or infinite values.

Stop if the requested object is ambiguous.

### 3. Preserve the Source

Call `blender__blender_save_copy` with a new `.blend` path and `overwrite:false`. For object-level edits, require a new `output_object_name` so the original object remains available for comparison and rollback.

### 4. Apply Typed Mutations

Use only the smallest matching tool:

- whole-mesh transforms or the listed cleanup/topology operations: `blender__blender_mesh_edit`;
- bevel, subdivision, mirror, solidify, decimate, or triangulate: `blender__blender_modifier_apply`;
- material-slot change: `blender__blender_material_assign`;
- smart project, cube project, unwrap, or pack: `blender__blender_uv_edit`.

Reference only identifiers returned by inspection. Do not guess object names, mesh datablocks, material slots, UV layers, or modifier names.

### 5. Resolve Asynchronous Results

When a mutation returns a running job or acknowledgement-loss result:

1. retain its `job_id`;
2. poll `blender__blender_job_status(job_id=...)`;
3. wait for a terminal success or failure;
4. do not submit the mutation again.

Treat an unknown terminal state as potentially mutated and re-inspect before any further action.

### 6. Verify and Export

Re-run the same inspections used for the baseline. Require:

- the original object and source fingerprint remain unchanged;
- only intended output objects were added or changed;
- expected topology, modifier, material, transform, and UV deltas match the request;
- no new error-severity geometry diagnostics;
- all numeric transforms, bounds, vertices, and UV values are finite;
- export selection contains only explicitly approved objects.

Export with `blender__blender_model_export` to a new path and `overwrite:false`. Confirm the terminal job result and output path, then save a final `.blend` copy.

## QA Sign-Off

Do not claim completion unless the final evidence includes:

- backup `.blend` path;
- source and output object identifiers;
- before-and-after counts and fingerprints;
- geometry, normal, manifold, material, modifier, transform, and UV diagnostics;
- terminal job results for every mutation;
- verified export path and format;
- confirmation that the source object and input file were not overwritten.

## COMMON_MISTAKES

- Treating animation import/adjust/export tools as general mesh-editing support.
- Falling back to arbitrary Blender Python when a typed capability is missing.
- Editing the original object in place instead of creating a named output object.
- Repeating a timed-out mutation rather than polling its `job_id`.
- Applying modifiers or UV operations to a guessed object or layer name.
- Comparing only visual appearance instead of structured topology and fingerprint evidence.
- Exporting every visible object instead of an explicit inspected selection.
