---
name: blender-animation-roundtrip
display_name: Blender Animation Roundtrip
description: Safely export an Unreal AnimSequence to Blender, inspect and refine keyed bone motion, return it as a new validation asset, and verify skeleton, timing, root motion, and pose quality
revvy_classes:
  - AnimSequenceService
  - SkeletonService
unreal_classes:
  - AnimSequence
  - Skeleton
  - SkeletalMesh
keywords:
  - blender
  - blender mcp
  - animation roundtrip
  - animation polish
  - fbx
  - export animation
  - import animation
  - bone keys
  - action
  - root motion
  - foot sliding
  - pose polish
  - unreal animation
---

# Blender Animation Roundtrip

Use this workflow when an existing Unreal `AnimSequence` needs detailed
keyframe cleanup or pose polish in Blender.

Read [research.md](research.md) when changing FBX settings, bridge security, or
the tool contract.

## Non-negotiable rules

1. Query exact Unreal asset and Skeleton paths before export.
2. Query exact Blender armature, action, slot, mesh, and bone names after import.
3. Keep every file under `<Project>/Saved/Revvy/BlenderExchange/<job-id>`.
4. Keep `create_copy=true` for Blender edits.
5. Import to a new Unreal validation asset. Never replace the source unless the
   user explicitly approves after validation.
6. Every Blender mutation is asynchronous. Poll its exact `job_id` to a terminal
   state before using its result or deciding whether a retry is safe.
7. Do not retry an Unreal import after an ambiguous timeout. Read the exact job
   manifest first.
8. Do not substitute Mixamo conversion for a normal Blender roundtrip.

## Required workflow

### 1. Check both editors

Call `revvy_status`, then
`manage_upstreams(action="activate", namespace="blender")`. Refresh the tool
list before checking capabilities; if the client caches tools for the whole
session, reconnect once as instructed by the activation result. Only then call
`blender__blender_bridge_status`.

If the Blender upstream is not configured, ask the user to use
`BLENDER > 연결 준비` in Revvy. If configured but offline, ask them to use
Revvy's `실행` button to start Blender. Do not fabricate Blender state.

Blender mutation tools (`blender_animation_import_fbx`,
`blender_animation_adjust`, `blender_animation_export_fbx`, and
`blender_save_copy`) return only `queued=true` and a `job_id` initially. Poll:

```text
blender__blender_job_status
  job_id=<exact queued job_id>
```

Continue while `state` is `queued` or `running`. At `state=finished`, stop on
`error`; otherwise consume the nested `result`. Treat `cancelled` as failure.
Never blindly resend an unfinished mutation.

### 2. Inspect and export from Unreal

Use `AnimSequenceService.list_anim_sequences` or
`find_animations_for_skeleton` to discover the exact asset. Then call the
Unreal Python helper through `execute_python_code`:

```python
import json
import revvy_animation_bridge as bridge

inspection = bridge.inspect_animation("/Game/Exact/AS_Name")
job = bridge.export_animation_job("/Game/Exact/AS_Name")
print(json.dumps({"inspection": inspection, "job": job}, ensure_ascii=False))
```

Stop if either result has `success=false`. Record `job_id`, the explicit
Skeleton path, `paths.source_fbx` / `paths.blender_output_fbx`, and the exact
`timing_contract`. Do not reconstruct or alter the contract.

Normalize the exported hierarchy without changing names or parents, and derive
the root from its single `parent=null` entry:

```python
expected_bone_hierarchy = [
    {
        "name": str(bone["name"]),
        "parent": (
            None
            if bone.get("parent") in (None, "")
            else str(bone["parent"])
        ),
    }
    for bone in job["source"]["bone_hierarchy"]
]
roots = [bone["name"] for bone in expected_bone_hierarchy if bone["parent"] is None]
if len(roots) != 1:
    raise RuntimeError("Exported Skeleton must contain exactly one root bone.")
root_bone_name = roots[0]
```

Stop on duplicate names, missing parents, or any value that cannot be
represented exactly. Do not infer a root from naming conventions.

### 3. Import and inspect in Blender

Call:

```text
blender__blender_animation_import_fbx
  path=<paths.source_fbx>
  root_bone_name=<derived root_bone_name>
  expected_bone_hierarchy=<normalized expected_bone_hierarchy>
  timing_contract=<job.timing_contract>
```

Poll the returned `job_id`. Parse the terminal status object's nested
`result.armatures`, `result.actions`, `result.action_slots`, and
`result.objects`. Require exactly one armature and one action. Require exactly
one non-empty slot identifier for that action, and record it as `action_slot`.
Derive mesh names only from entries in `result.objects` whose `type` is
`MESH`.

Require `result.root_handling.hierarchy_verified=true`. Compare
`result.bone_hierarchy` to `expected_bone_hierarchy`: counts, unique names, and
every exact `name -> parent` pair must match. Stop before editing on any
difference.

Require `normalized_frame_range[1] - normalized_frame_range[0] ==
timing_contract.expected_frame_count`. If
`result.timing.terminal_duplicate_detected=true`, also require
`terminal_duplicate_trimmed=true` and a positive
`verified_duplicate_curve_count`. The bridge trims one terminal key only after
every F-curve proves it is an identical Unreal exporter duplicate.

Then call with the exact returned values:

```text
blender__blender_animation_inspect
  armature_name=<returned armature>
  action_name=<returned action>
  action_slot=<returned action_slot>
  timing_contract=<job.timing_contract>
```

Confirm action frame range, scene FPS, f-curves, keyed bones, and root/object
channels before editing. The inspected `action_slot` must equal the requested
slot.

### 4. Make a copied action and refine it

Call `blender__blender_animation_adjust` with the inspected
`armature_name`, `action_name`, and `action_slot`, `create_copy=true`, and one
or more typed operations. Poll its `job_id` to `finished`, then use the nested
`result.action` and `result.action_slot`; require `result.created_copy=true`.

Supported operations:

- `transform`: target `object` or `bone`; channel `translate`,
  `rotate_euler`, `rotate_quaternion`, or `scale`; mode `add`, `multiply`, or
  `set`; optional frame range. Euler units must be explicit.
- `time`: scale or offset keys around a pivot, optionally within a frame range.
- `interpolation`: `CONSTANT`, `LINEAR`, or `BEZIER`, optionally within a frame
  range.

Use small, localized deltas. Inspect the copied output action after every
logical edit, always passing its exact `action_slot` and `timing_contract`.
This exact roundtrip does not permit duration or frame-count changes; a time
operation must preserve the contracted span. For contact fixes, compare
pelvis/root, thigh, calf, and foot keys together instead of moving only the
foot. Preserve intended root motion.

### 5. Export the copied Blender action

Call `blender__blender_animation_export_fbx` with:

- `path=<paths.blender_output_fbx>`
- exact `armature_name`
- copied `action_name`
- copied `action_slot`
- exact `timing_contract`
- inspected `mesh_names` only when a mesh is required
- `overwrite=false` on the first attempt

The bridge owns Unreal-safe FBX settings. Do not override axis, leaf-bone,
space-transform, sampling, NLA, all-action, or simplify settings ad hoc.
Poll the export `job_id` to `finished`; only then use `result.path`.

### 6. Import as a validation asset

Call through `execute_python_code`:

```python
import json
import revvy_animation_bridge as bridge

result = bridge.import_animation_job(
    job_id="<exact job id>",
    skeleton_path="<exact exported Skeleton path>",
)
print(json.dumps(result, ensure_ascii=False))
```

Require `status="queued"`. Do not call `import_animation_job` again while it is
queued or importing. Poll through `execute_python_code`:

```python
import json
import revvy_animation_bridge as bridge

state = bridge.get_job("<exact job id>")
print(json.dumps(state, ensure_ascii=False))
```

Read `state["job"]["status"]`. Continue polling for `import_queued` or
`importing`; terminal states are `imported_valid`, `imported_with_errors`, and
`import_failed`. Use only
`state["job"]["import"]["imported_animation_path"]` after a completed import.
Do not choose a Skeleton by name similarity or first-match logic.

### 7. Validate before any promotion

```python
import json
import revvy_animation_bridge as bridge

result = bridge.validate_roundtrip(
    source_asset_path="/Game/Exact/AS_Source",
    imported_asset_path="/Game/Exact/Validation/AS_Imported",
)
print(json.dumps(result, ensure_ascii=False))
```

Require:

- explicit Skeleton compatibility;
- expected bone/track coverage;
- exact FPS and frame count, with duration differing by less than `0.001`
  source frame;
- unchanged root-motion policy unless requested;
- no unexpected scale or axis drift;
- visual checks at contact, anticipation, impact, recovery, and loop seam;
- no foot slide, knee/elbow pop, hand contact loss, or first/last-frame snap.

If validation fails, keep the source untouched, revise the copied Blender
action, export again only after checking file state, and import under another
validation name.

### 8. Explicit retry with a new file and asset

After confirming the previous Blender and Unreal jobs are terminal, export the
revised copied action to a different file inside the same job:

```text
blender__blender_animation_export_fbx
  path=<paths.job_dir>/edited_retry_01.fbx
  armature_name=<exact armature>
  action_name=<exact copied action>
  action_slot=<exact copied action_slot>
  timing_contract=<exact job timing_contract>
  mesh_names=<exact inspected meshes>
  overwrite=false
```

Poll that Blender job to `finished`, then queue a new Unreal destination:

```python
import json
import revvy_animation_bridge as bridge

result = bridge.import_animation_job(
    job_id="20260718T011153Z_a4e4e0a645",
    edited_fbx_path=r"C:\Exact\Project\Saved\Revvy\BlenderExchange\20260718T011153Z_a4e4e0a645\edited_retry_01.fbx",
    skeleton_path="/Game/Exact/Skeleton.SK_Exact",
    destination_path="/Game/Revvy/BlenderExchange/20260718T011153Z_a4e4e0a645/Retry01",
    destination_name="AN_Revvy_Attack_Retry01",
)
print(json.dumps(result, ensure_ascii=False))
```

The alternate FBX must remain inside that exact exchange job. The destination
path and name must be unused; the bridge always imports with
`replace_existing=false` and rejects overwrites. Poll `bridge.get_job` again
instead of resubmitting the import.
