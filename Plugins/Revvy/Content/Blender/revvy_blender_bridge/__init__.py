from __future__ import annotations

bl_info = {
    "name": "Revvy Blender Bridge",
    "author": "Revvy",
    "version": (1, 1, 0),
    "blender": (5, 0, 0),
    "location": "View3D > Sidebar > Revvy",
    "description": "Secure typed modeling and animation bridge for Revvy and Unreal Engine",
    "category": "3D View",
}

import json
import math
import os
import queue
import secrets
import socket
import threading
import time
import traceback
from dataclasses import dataclass, field
from typing import Any, Callable

from .common import (
    BRIDGE_HOST,
    COMMANDS,
    MAX_MESSAGE_BYTES,
    MUTATING_COMMANDS,
    READ_ONLY_COMMANDS,
    BridgeConfig,
    BridgeError,
    ensure_config,
    load_config,
    resolve_exchange_path,
    validate_command,
    validate_request,
)
from .modeling import MODELING_COMMAND_HANDLERS

try:
    import bpy
except ModuleNotFoundError:  # Allows protocol/config tests outside Blender.
    bpy = None


ADDON_VERSION = ".".join(str(item) for item in bl_info["version"])
REQUEST_TIMEOUT_SECONDS = 180.0
CONNECTION_READ_TIMEOUT_SECONDS = 1.5
OVERLOAD_READ_TIMEOUT_SECONDS = 0.1
CONNECTION_WORKER_COUNT = 4
CONNECTION_QUEUE_CAPACITY = 8
DEFERRED_COMMANDS = frozenset(MUTATING_COMMANDS)


def _exact_object(name: str, object_type: str):
    obj = bpy.data.objects.get(name)
    if obj is None or obj.type != object_type:
        available = sorted(
            item.name for item in bpy.data.objects if item.type == object_type
        )
        raise BridgeError(
            "object_not_found",
            f'Exact {object_type.lower()} object "{name}" was not found.',
            {"available": available[:200]},
        )
    return obj


def _exact_action(name: str):
    action = bpy.data.actions.get(name)
    if action is None:
        raise BridgeError(
            "action_not_found",
            f'Exact action "{name}" was not found.',
            {"available": sorted(item.name for item in bpy.data.actions)[:200]},
        )
    return action


def _armature_action(armature, action_name: str | None):
    if action_name:
        return _exact_action(action_name)
    animation_data = armature.animation_data
    action = animation_data.action if animation_data else None
    if action is None:
        raise BridgeError(
            "action_not_found",
            f'Armature "{armature.name}" does not have an active action.',
        )
    return action


def _action_slot(action, armature=None, slot_identifier: str | None = None):
    slots = list(getattr(action, "slots", ()))
    if not slots:
        if slot_identifier:
            raise BridgeError(
                "action_slot_not_found",
                f'Legacy action "{action.name}" has no slots.',
            )
        return None
    if slot_identifier:
        for slot in slots:
            if str(getattr(slot, "identifier", "")) == slot_identifier:
                return slot
        raise BridgeError(
            "action_slot_not_found",
            f'Action slot "{slot_identifier}" was not found in "{action.name}".',
            {
                "available": [
                    str(getattr(slot, "identifier", "")) for slot in slots
                ]
            },
        )
    if (
        armature is not None
        and armature.animation_data
        and armature.animation_data.action == action
    ):
        active_slot = getattr(armature.animation_data, "action_slot", None)
        if active_slot in slots:
            return active_slot
    if len(slots) == 1:
        return slots[0]
    raise BridgeError(
        "action_slot_required",
        f'Action "{action.name}" has multiple slots; query and pass action_slot exactly.',
        {
            "available": [
                str(getattr(slot, "identifier", "")) for slot in slots
            ]
        },
    )


def _action_fcurves(
    action,
    armature=None,
    slot_identifier: str | None = None,
) -> list[Any]:
    legacy_fcurves = getattr(action, "fcurves", None)
    if legacy_fcurves is not None:
        if slot_identifier:
            raise BridgeError(
                "action_slot_not_found",
                f'Legacy action "{action.name}" has no slots.',
            )
        return list(legacy_fcurves)

    selected_slot = _action_slot(action, armature, slot_identifier)
    result: list[Any] = []
    for layer in getattr(action, "layers", ()):
        for strip in getattr(layer, "strips", ()):
            for channelbag in getattr(strip, "channelbags", ()):
                if channelbag.slot != selected_slot:
                    continue
                result.extend(channelbag.fcurves)
    return result


def _fps_payload(scene) -> dict[str, float]:
    fps = float(scene.render.fps)
    fps_base = float(scene.render.fps_base)
    return {
        "fps": fps,
        "fps_base": fps_base,
        "effective_fps": fps / fps_base if fps_base else fps,
    }


_TIMING_FRAME_TOLERANCE = 0.001
_TIMING_VALUE_ABS_TOLERANCE = 1e-7


def _frame_matches(actual: float, expected: float) -> bool:
    return math.isclose(
        float(actual),
        float(expected),
        rel_tol=0.0,
        abs_tol=_TIMING_FRAME_TOLERANCE,
    )


def _verify_timing_contract(
    action,
    armature,
    scene,
    contract: dict[str, Any] | None,
    *,
    slot_identifier: str | None = None,
    trim_verified_duplicate: bool,
    normalize_scene: bool,
) -> dict[str, Any] | None:
    if not contract:
        return None
    fcurves = _action_fcurves(action, armature, slot_identifier)
    curves = [
        (curve, sorted(curve.keyframe_points, key=lambda point: float(point.co[0])))
        for curve in fcurves
    ]
    empty_curves = [
        {
            "data_path": curve.data_path,
            "array_index": int(curve.array_index),
        }
        for curve, points in curves
        if not points
    ]
    if not curves or empty_curves:
        raise BridgeError(
            "timing_contract_mismatch",
            "Exact timing verification requires keyed F-curves.",
            {"empty_curves": empty_curves[:50]},
        )

    all_frames = [
        float(point.co[0])
        for _curve, points in curves
        for point in points
    ]
    imported_start = min(all_frames)
    imported_end = max(all_frames)
    normalized_start = float(round(imported_start))
    if not _frame_matches(imported_start, normalized_start):
        raise BridgeError(
            "timing_contract_mismatch",
            "The FBX action does not start on an integral sample frame.",
            {"actual_start": imported_start},
        )

    expected_count = int(contract["expected_frame_count"])
    expected_end = normalized_start + expected_count
    expected_rate = float(contract["expected_frame_rate"])
    actual_rate = float(_fps_payload(scene)["effective_fps"])
    rate_tolerance = max(0.0001, expected_rate * 1e-6)
    if not math.isclose(
        actual_rate,
        expected_rate,
        rel_tol=0.0,
        abs_tol=rate_tolerance,
    ):
        raise BridgeError(
            "timing_contract_mismatch",
            "Blender scene FPS does not match the Unreal source frame rate.",
            {
                "expected_frame_rate": expected_rate,
                "actual_frame_rate": actual_rate,
            },
        )

    duplicate_frame = expected_end + 1.0
    duplicate_detected = _frame_matches(imported_end, duplicate_frame)
    duplicate_trimmed = False
    verified_curve_count = 0
    if duplicate_detected:
        if not trim_verified_duplicate:
            raise BridgeError(
                "timing_contract_mismatch",
                "An unnormalized Unreal exporter terminal frame is still present.",
                {
                    "expected_end": expected_end,
                    "actual_end": imported_end,
                },
            )
        mismatches = []
        for curve, points in curves:
            reason = None
            if len(points) < 2:
                reason = "fewer_than_two_keys"
            elif not _frame_matches(float(points[-1].co[0]), duplicate_frame):
                reason = "missing_duplicate_terminal_key"
            elif not _frame_matches(float(points[-2].co[0]), expected_end):
                reason = "missing_preceding_boundary_key"
            elif not math.isclose(
                float(points[-1].co[1]),
                float(points[-2].co[1]),
                rel_tol=1e-9,
                abs_tol=_TIMING_VALUE_ABS_TOLERANCE,
            ):
                reason = "terminal_value_changed"
            if reason:
                mismatches.append(
                    {
                        "data_path": curve.data_path,
                        "array_index": int(curve.array_index),
                        "reason": reason,
                    }
                )
        if mismatches:
            raise BridgeError(
                "unverified_terminal_duplicate",
                "The extra terminal frame is not an identical Unreal exporter duplicate on every F-curve.",
                {
                    "expected_duplicate_frame": duplicate_frame,
                    "mismatches": mismatches[:100],
                },
            )
        for curve, points in curves:
            curve.keyframe_points.remove(points[-1], fast=True)
            curve.update()
            verified_curve_count += 1
        duplicate_trimmed = True
    elif not _frame_matches(imported_end, expected_end):
        raise BridgeError(
            "timing_contract_mismatch",
            "The FBX action span does not match the Unreal source frame count.",
            {
                "expected_frame_range": [normalized_start, expected_end],
                "actual_frame_range": [imported_start, imported_end],
            },
        )

    normalized_frames = [
        float(point.co[0])
        for curve in _action_fcurves(action, armature, slot_identifier)
        for point in curve.keyframe_points
    ]
    normalized_end = max(normalized_frames)
    if (
        not _frame_matches(min(normalized_frames), normalized_start)
        or not _frame_matches(normalized_end, expected_end)
    ):
        raise BridgeError(
            "timing_contract_mismatch",
            "The normalized action does not match the exact Unreal timing boundary.",
            {
                "expected_frame_range": [normalized_start, expected_end],
                "actual_frame_range": [min(normalized_frames), normalized_end],
            },
        )

    if normalize_scene:
        scene.frame_start = int(round(normalized_start))
        scene.frame_end = int(round(expected_end))
    return {
        "contract": contract["contract"],
        "expected_frame_count": expected_count,
        "expected_duration_seconds": float(contract["expected_duration_seconds"]),
        "expected_frame_rate": expected_rate,
        "imported_frame_range": [imported_start, imported_end],
        "normalized_frame_range": [normalized_start, expected_end],
        "terminal_duplicate_detected": duplicate_detected,
        "terminal_duplicate_trimmed": duplicate_trimmed,
        "verified_duplicate_curve_count": verified_curve_count,
        "scene_frame_range_normalized": bool(normalize_scene),
    }


def _status(_config: BridgeConfig, _args: dict[str, Any]) -> dict[str, Any]:
    scene = bpy.context.scene
    return {
        "bridge": {
            "name": bl_info["name"],
            "version": ADDON_VERSION,
            "host": BRIDGE_HOST,
            "port": _config.port,
            "exchange_root": str(_config.exchange_root),
            "arbitrary_code_execution": False,
            "supported_commands": sorted(COMMANDS),
        },
        "blender": {
            "version": bpy.app.version_string,
            "background": bool(bpy.app.background),
            "file": bpy.data.filepath or None,
            "scene": scene.name,
            "frame_start": scene.frame_start,
            "frame_end": scene.frame_end,
            **_fps_payload(scene),
        },
        "armatures": sorted(
            item.name for item in bpy.data.objects if item.type == "ARMATURE"
        ),
        "mesh_objects": sorted(
            item.name for item in bpy.data.objects if item.type == "MESH"
        ),
        "materials": sorted(item.name for item in bpy.data.materials),
        "actions": sorted(item.name for item in bpy.data.actions),
    }


_OBJECT_TRANSFORM_PATHS = frozenset(
    {"location", "rotation_euler", "rotation_quaternion", "rotation_axis_angle", "scale"}
)


def _set_scene_frame(scene, frame: float) -> None:
    whole = math.floor(float(frame))
    scene.frame_set(whole, subframe=float(frame) - whole)


def _object_transform_frames(action, armature) -> list[float]:
    keyed_frames = {
        float(point.co[0])
        for curve in _action_fcurves(action, armature)
        if curve.data_path in _OBJECT_TRANSFORM_PATHS
        for point in curve.keyframe_points
    }
    start, end = (float(value) for value in action.frame_range)
    first = math.floor(start)
    last = math.ceil(end)
    if last - first > 100_000:
        raise BridgeError(
            "animation_range_too_large",
            "Root restoration is limited to 100,000 sampled frames.",
        )
    sampled_frames = {float(frame) for frame in range(first, last + 1)}
    sampled_frames.update(keyed_frames)
    return sorted(sampled_frames or {start})


def _neutralize_object_transform_curves(action, armature, baseline) -> int:
    location, rotation, scale = baseline.decompose()
    rotation_mode = armature.rotation_mode
    euler_mode = rotation_mode if rotation_mode not in {"QUATERNION", "AXIS_ANGLE"} else "XYZ"
    euler = rotation.to_euler(euler_mode)
    axis_angle = (rotation.angle, rotation.axis.x, rotation.axis.y, rotation.axis.z)
    values = {
        "location": tuple(location),
        "rotation_euler": tuple(euler),
        "rotation_quaternion": tuple(rotation),
        "rotation_axis_angle": axis_angle,
        "scale": tuple(scale),
    }
    changed = 0
    for curve in _action_fcurves(action, armature):
        if curve.data_path not in values:
            continue
        channel_values = values[curve.data_path]
        if not 0 <= int(curve.array_index) < len(channel_values):
            continue
        target = float(channel_values[int(curve.array_index)])
        for point in curve.keyframe_points:
            point.co[1] = target
            point.handle_left[1] = target
            point.handle_right[1] = target
            changed += 1
        curve.update()
    return changed


def _current_bone_hierarchy(armature) -> list[dict[str, str | None]]:
    return [
        {
            "name": bone.name,
            "parent": bone.parent.name if bone.parent else None,
        }
        for bone in armature.data.bones
    ]


def _verify_bone_hierarchy(
    armature,
    expected: list[dict[str, str | None]] | None,
) -> None:
    if not expected:
        return
    actual = _current_bone_hierarchy(armature)
    actual_by_name = {item["name"]: item["parent"] for item in actual}
    expected_by_name = {item["name"]: item["parent"] for item in expected}
    if actual_by_name == expected_by_name:
        return
    raise BridgeError(
        "bone_hierarchy_mismatch",
        "Blender changed the Unreal Skeleton bone signature; editing is blocked.",
        {
            "missing": sorted(set(expected_by_name) - set(actual_by_name)),
            "unexpected": sorted(set(actual_by_name) - set(expected_by_name)),
            "parent_mismatches": [
                {
                    "name": name,
                    "expected": expected_by_name[name],
                    "actual": actual_by_name[name],
                }
                for name in sorted(set(expected_by_name) & set(actual_by_name))
                if expected_by_name[name] != actual_by_name[name]
            ][:200],
        },
    )


def _restore_unreal_root(
    armature,
    action,
    root_name: str,
    expected: list[dict[str, str | None]] | None,
) -> dict[str, Any]:
    expected_by_name = (
        {item["name"]: item["parent"] for item in expected} if expected else {}
    )
    current_by_name = {
        bone.name: bone.parent.name if bone.parent else None
        for bone in armature.data.bones
    }
    restored = root_name not in current_by_name
    if restored:
        if armature.name != root_name:
            raise BridgeError(
                "root_identity_mismatch",
                "The missing Unreal root was not represented by the imported armature object.",
                {
                    "expected_root": root_name,
                    "armature_object": armature.name,
                },
            )
        if expected:
            missing = set(expected_by_name) - set(current_by_name)
            unexpected = set(current_by_name) - set(expected_by_name)
            if missing != {root_name} or unexpected:
                raise BridgeError(
                    "bone_hierarchy_mismatch",
                    "Blender import changed bones beyond the recoverable top-level root.",
                    {
                        "missing": sorted(missing),
                        "unexpected": sorted(unexpected),
                    },
                )
            parent_mismatches = []
            for name, actual_parent in current_by_name.items():
                expected_parent = expected_by_name[name]
                recoverable = expected_parent == root_name and actual_parent is None
                if expected_parent != actual_parent and not recoverable:
                    parent_mismatches.append(
                        {
                            "name": name,
                            "expected": expected_parent,
                            "actual": actual_parent,
                        }
                    )
            if parent_mismatches:
                raise BridgeError(
                    "bone_hierarchy_mismatch",
                    "Blender import changed parent relationships.",
                    {"parent_mismatches": parent_mismatches[:200]},
                )

        scene = bpy.context.scene
        frame_before = float(scene.frame_current) + float(scene.frame_subframe)
        frames = _object_transform_frames(action, armature)
        samples = []
        for frame in frames:
            _set_scene_frame(scene, frame)
            samples.append(armature.matrix_basis.copy())
        baseline = samples[0]

        selected_before, active_before = _selection_state()
        mode_before = getattr(bpy.context.object, "mode", "OBJECT")
        try:
            if mode_before != "OBJECT":
                bpy.ops.object.mode_set(mode="OBJECT")
            for item in bpy.context.selected_objects:
                item.select_set(False)
            armature.select_set(True)
            bpy.context.view_layer.objects.active = armature
            bpy.ops.object.mode_set(mode="EDIT")
            root = armature.data.edit_bones.new(root_name)
            root.head = (0.0, 0.0, 0.0)
            lengths = [float(bone.length) for bone in armature.data.edit_bones if bone != root]
            root.tail = (0.0, max(1.0, max(lengths, default=10.0) * 0.1), 0.0)
            for bone in armature.data.edit_bones:
                if bone == root:
                    continue
                should_parent = (
                    expected_by_name.get(bone.name) == root_name
                    if expected
                    else bone.parent is None
                )
                if should_parent:
                    bone.parent = root
            bpy.ops.object.mode_set(mode="POSE")

            _neutralize_object_transform_curves(action, armature, baseline)
            armature.matrix_basis = baseline
            pose_root = armature.pose.bones[root_name]
            pose_root.rotation_mode = "QUATERNION"
            inverse_baseline = baseline.inverted_safe()
            for frame, sampled in zip(frames, samples):
                delta = inverse_baseline @ sampled
                location, rotation, scale = delta.decompose()
                _set_scene_frame(scene, frame)
                pose_root.location = location
                pose_root.rotation_quaternion = rotation
                pose_root.scale = scale
                pose_root.keyframe_insert(
                    data_path="location",
                    frame=frame,
                    group=root_name,
                )
                pose_root.keyframe_insert(
                    data_path="rotation_quaternion",
                    frame=frame,
                    group=root_name,
                )
                pose_root.keyframe_insert(
                    data_path="scale",
                    frame=frame,
                    group=root_name,
                )
            root_path = pose_root.path_from_id()
            for curve in _action_fcurves(action, armature):
                if curve.data_path.startswith(root_path):
                    for point in curve.keyframe_points:
                        point.interpolation = "LINEAR"
                    curve.update()
        finally:
            if getattr(bpy.context.object, "mode", "OBJECT") != "OBJECT":
                bpy.ops.object.mode_set(mode="OBJECT")
            _set_scene_frame(scene, frame_before)
            _restore_selection(selected_before, active_before)

    existing_armature = bpy.data.objects.get("Armature")
    if existing_armature is not None and existing_armature != armature:
        raise BridgeError(
            "armature_name_collision",
            'An existing object named "Armature" blocks Unreal-safe FBX export.',
        )
    armature.name = "Armature"
    _verify_bone_hierarchy(armature, expected)
    return {
        "root_bone": root_name,
        "restored_from_armature_object": restored,
        "armature_object": armature.name,
        "sampled_root_frames": len(frames) if restored else 0,
        "hierarchy_verified": bool(expected),
    }


_FBX_IMPORT_ID_COLLECTIONS = (
    "objects",
    "actions",
    "armatures",
    "meshes",
    "materials",
    "images",
    "textures",
    "cameras",
    "lights",
    "curves",
    "collections",
)


def _snapshot_import_ids() -> dict[str, set[int]]:
    return {
        name: {item.as_pointer() for item in getattr(bpy.data, name, ())}
        for name in _FBX_IMPORT_ID_COLLECTIONS
    }


def _rollback_import_ids(snapshot: dict[str, set[int]]) -> None:
    removal_order = (
        "objects",
        "collections",
        "actions",
        "meshes",
        "armatures",
        "materials",
        "textures",
        "images",
        "curves",
        "cameras",
        "lights",
    )
    for name in removal_order:
        collection = getattr(bpy.data, name, None)
        if collection is None:
            continue
        created = [
            item
            for item in list(collection)
            if item.as_pointer() not in snapshot.get(name, set())
        ]
        for item in created:
            if name != "objects" and getattr(item, "users", 0) > 0:
                continue
            try:
                if name == "objects":
                    collection.remove(item, do_unlink=True)
                else:
                    collection.remove(item)
            except (ReferenceError, RuntimeError, TypeError):
                pass


def _import_animation_fbx(
    config: BridgeConfig,
    args: dict[str, Any],
) -> dict[str, Any]:
    source = resolve_exchange_path(config, args["path"], suffix=".fbx", must_exist=True)
    operator = getattr(getattr(bpy.ops, "import_scene", None), "fbx", None)
    if operator is None:
        raise BridgeError(
            "fbx_import_unavailable",
            "Blender's FBX import operator is not available.",
        )
    if args.get("root_bone_name") and bpy.data.objects.get("Armature") is not None:
        raise BridgeError(
            "armature_name_collision",
            'An existing object named "Armature" blocks an isolated Unreal roundtrip import.',
        )

    snapshot = _snapshot_import_ids()
    scene = bpy.context.scene
    scene_timing_before = (
        scene.frame_start,
        scene.frame_end,
        scene.render.fps,
        scene.render.fps_base,
    )
    try:
        result = operator(
            filepath=str(source),
            use_anim=True,
            anim_offset=0.0,
            ignore_leaf_bones=False,
            automatic_bone_orientation=False,
        )
        if "FINISHED" not in result:
            raise BridgeError(
                "fbx_import_failed",
                f"FBX import returned {sorted(result)}.",
            )

        imported_objects = [
            item
            for item in bpy.data.objects
            if item.as_pointer() not in snapshot["objects"]
        ]
        imported_actions = [
            item
            for item in bpy.data.actions
            if item.as_pointer() not in snapshot["actions"]
        ]
        armatures = [item for item in imported_objects if item.type == "ARMATURE"]
        if not armatures:
            raise BridgeError(
                "fbx_has_no_armature",
                "The imported FBX did not create an armature.",
                {"imported_objects": [item.name for item in imported_objects]},
            )

        root_handling = None
        timing = None
        root_name = args.get("root_bone_name")
        expected_hierarchy = args.get("expected_bone_hierarchy")
        if root_name:
            if len(armatures) != 1:
                raise BridgeError(
                    "ambiguous_armature",
                    "Unreal roundtrip import requires exactly one armature.",
                    {"armatures": [item.name for item in armatures]},
                )
            action = _armature_action(armatures[0], None)
            root_handling = _restore_unreal_root(
                armatures[0],
                action,
                root_name,
                expected_hierarchy,
            )
        timing_contract = args.get("timing_contract")
        if timing_contract:
            if len(armatures) != 1 or len(imported_actions) != 1:
                raise BridgeError(
                    "ambiguous_animation",
                    "Exact Unreal timing requires exactly one armature and one imported action.",
                    {
                        "armatures": [item.name for item in armatures],
                        "actions": [item.name for item in imported_actions],
                    },
                )
            action = _armature_action(armatures[0], imported_actions[0].name)
            timing = _verify_timing_contract(
                action,
                armatures[0],
                scene,
                timing_contract,
                slot_identifier=None,
                trim_verified_duplicate=True,
                normalize_scene=True,
            )

        prefix = args.get("name_prefix")
        if prefix:
            for item in imported_objects:
                if item.type != "ARMATURE":
                    item.name = f"{prefix}_{item.name}"
            for item in imported_actions:
                item.name = f"{prefix}_{item.name}"

        return {
            "source": str(source),
            "armatures": [item.name for item in armatures],
            "actions": [item.name for item in imported_actions],
            "action_slots": {
                item.name: [
                    str(getattr(slot, "identifier", ""))
                    for slot in getattr(item, "slots", ())
                ]
                for item in imported_actions
            },
            "objects": [
                {"name": item.name, "type": item.type} for item in imported_objects
            ],
            "non_destructive_import": True,
            "leaf_bones_preserved": True,
            "root_handling": root_handling,
            "bone_hierarchy": _current_bone_hierarchy(armatures[0]),
            "timing": timing,
        }
    except Exception:
        (
            scene.frame_start,
            scene.frame_end,
            scene.render.fps,
            scene.render.fps_base,
        ) = scene_timing_before
        _rollback_import_ids(snapshot)
        raise


def _inspect_animation(
    _config: BridgeConfig,
    args: dict[str, Any],
) -> dict[str, Any]:
    armature = _exact_object(args["armature_name"], "ARMATURE")
    action = _armature_action(armature, args.get("action_name"))
    selected_slot = _action_slot(action, armature, args.get("action_slot"))
    fcurves = _action_fcurves(action, armature, args.get("action_slot"))
    bones = []
    for bone in armature.data.bones:
        bones.append(
            {
                "name": bone.name,
                "parent": bone.parent.name if bone.parent else None,
                "deform": bool(bone.use_deform),
            }
        )

    curve_payload = []
    for curve in fcurves:
        frames = [float(point.co[0]) for point in curve.keyframe_points]
        curve_payload.append(
            {
                "data_path": curve.data_path,
                "array_index": int(curve.array_index),
                "key_count": len(curve.keyframe_points),
                "frame_start": min(frames) if frames else None,
                "frame_end": max(frames) if frames else None,
            }
        )
    scene = bpy.context.scene
    timing = _verify_timing_contract(
        action,
        armature,
        scene,
        args.get("timing_contract"),
        slot_identifier=args.get("action_slot"),
        trim_verified_duplicate=False,
        normalize_scene=False,
    )
    return {
        "armature": armature.name,
        "action": action.name,
        "action_slot": (
            str(getattr(selected_slot, "identifier", ""))
            if selected_slot is not None
            else None
        ),
        "frame_range": [float(item) for item in action.frame_range],
        **_fps_payload(scene),
        "bones": bones,
        "fcurves": curve_payload,
        "timing": timing,
    }


def _point_in_range(point, operation: dict[str, Any]) -> bool:
    frame = float(point.co[0])
    start = operation.get("frame_start")
    end = operation.get("frame_end")
    return (start is None or frame >= float(start)) and (
        end is None or frame <= float(end)
    )


def _transform_value(value: float, operand: float, mode: str) -> float:
    if mode == "add":
        return value + operand
    if mode == "multiply":
        return value * operand
    return operand


def _apply_transform_operation(
    armature,
    fcurves: list[Any],
    operation: dict[str, Any],
) -> dict[str, Any]:
    channel_properties = {
        "translate": "location",
        "rotate_euler": "rotation_euler",
        "rotate_quaternion": "rotation_quaternion",
        "scale": "scale",
    }
    property_name = channel_properties[operation["channel"]]
    if operation["target"] == "bone":
        bone = armature.pose.bones.get(operation["bone_name"])
        if bone is None:
            raise BridgeError(
                "bone_not_found",
                f'Exact bone "{operation["bone_name"]}" was not found.',
                {"available": sorted(item.name for item in armature.pose.bones)[:300]},
            )
        data_path = bone.path_from_id(property_name)
        target_name = bone.name
    else:
        data_path = armature.path_from_id(property_name)
        target_name = armature.name

    values = [float(item) for item in operation["values"]]
    if (
        operation["channel"] == "rotate_euler"
        and operation["mode"] != "multiply"
        and operation.get("units", "radians") == "degrees"
    ):
        values = [math.radians(item) for item in values]

    changed = 0
    matched_curves = 0
    for curve in fcurves:
        if curve.data_path != data_path or curve.array_index >= len(values):
            continue
        matched_curves += 1
        operand = values[curve.array_index]
        for point in curve.keyframe_points:
            if not _point_in_range(point, operation):
                continue
            transform: Callable[[float], float] = lambda item: _transform_value(
                float(item), operand, operation["mode"]
            )
            point.co[1] = transform(point.co[1])
            point.handle_left[1] = transform(point.handle_left[1])
            point.handle_right[1] = transform(point.handle_right[1])
            changed += 1
        curve.update()
    if matched_curves == 0:
        raise BridgeError(
            "fcurve_not_found",
            f'No "{property_name}" F-curves exist for exact target "{target_name}".',
            {"data_path": data_path},
        )
    return {
        "type": "transform",
        "target": target_name,
        "data_path": data_path,
        "matched_fcurves": matched_curves,
        "changed_keys": changed,
    }


def _apply_time_operation(
    fcurves: list[Any],
    operation: dict[str, Any],
) -> dict[str, Any]:
    scale = float(operation.get("scale", 1.0))
    offset = float(operation.get("offset", 0.0))
    pivot = float(operation.get("pivot", 0.0))

    def transform(frame: float) -> float:
        return pivot + (frame - pivot) * scale + offset

    changed = 0
    for curve in fcurves:
        for point in curve.keyframe_points:
            if not _point_in_range(point, operation):
                continue
            point.co[0] = transform(float(point.co[0]))
            point.handle_left[0] = transform(float(point.handle_left[0]))
            point.handle_right[0] = transform(float(point.handle_right[0]))
            changed += 1
        curve.update()
    if changed == 0:
        raise BridgeError("keyframe_not_found", "No keyframes matched the time operation.")
    return {"type": "time", "changed_keys": changed}


def _apply_interpolation_operation(
    fcurves: list[Any],
    operation: dict[str, Any],
) -> dict[str, Any]:
    changed = 0
    for curve in fcurves:
        for point in curve.keyframe_points:
            if _point_in_range(point, operation):
                point.interpolation = operation["value"]
                changed += 1
        curve.update()
    if changed == 0:
        raise BridgeError(
            "keyframe_not_found",
            "No keyframes matched the interpolation operation.",
        )
    return {
        "type": "interpolation",
        "value": operation["value"],
        "changed_keys": changed,
    }


def _unique_action_name(base: str) -> str:
    if bpy.data.actions.get(base) is None:
        return base
    index = 1
    while bpy.data.actions.get(f"{base}.{index:03d}") is not None:
        index += 1
    return f"{base}.{index:03d}"


def _adjust_animation(
    _config: BridgeConfig,
    args: dict[str, Any],
) -> dict[str, Any]:
    armature = _exact_object(args["armature_name"], "ARMATURE")
    source_action = _exact_action(args["action_name"])
    action = source_action
    created_copy = False
    animation_data = armature.animation_data_create()
    action_before = animation_data.action
    action_slot_before = getattr(animation_data, "action_slot", None)

    if args.get("create_copy", True):
        requested_name = args.get("output_action_name")
        if requested_name and bpy.data.actions.get(requested_name) is not None:
            raise BridgeError(
                "action_already_exists",
                f'Action "{requested_name}" already exists; choose a new exact name.',
            )
        action = source_action.copy()
        action.name = requested_name or _unique_action_name(f"{source_action.name}_Revvy")
        created_copy = True

    try:
        animation_data.action = action
        selected_slot = _action_slot(action, armature, args.get("action_slot"))
        if (
            selected_slot is not None
            and hasattr(animation_data, "action_slot")
            and animation_data.action == action
        ):
            animation_data.action_slot = selected_slot
        fcurves = _action_fcurves(action, armature, args.get("action_slot"))
        if not fcurves:
            raise BridgeError(
                "fcurve_not_found",
                f'Action "{action.name}" has no editable F-curves for "{armature.name}".',
            )
        results = []
        for operation in args["operations"]:
            if operation["type"] == "transform":
                results.append(_apply_transform_operation(armature, fcurves, operation))
            elif operation["type"] == "time":
                results.append(_apply_time_operation(fcurves, operation))
            else:
                results.append(_apply_interpolation_operation(fcurves, operation))
    except Exception:
        if created_copy:
            animation_data.action = action_before
            if (
                action_slot_before is not None
                and hasattr(animation_data, "action_slot")
                and animation_data.action is not None
            ):
                try:
                    animation_data.action_slot = action_slot_before
                except (RuntimeError, TypeError):
                    pass
            if action.users == 0:
                bpy.data.actions.remove(action)
        raise

    return {
        "armature": armature.name,
        "source_action": source_action.name,
        "action": action.name,
        "action_slot": (
            str(getattr(selected_slot, "identifier", ""))
            if selected_slot is not None
            else None
        ),
        "created_copy": created_copy,
        "operations": results,
        "frame_range": [float(item) for item in action.frame_range],
    }


def _selection_state() -> tuple[list[Any], Any]:
    selected = [item for item in bpy.context.scene.objects if item.select_get()]
    return selected, bpy.context.view_layer.objects.active


def _restore_selection(selected: list[Any], active: Any) -> None:
    for item in bpy.context.scene.objects:
        try:
            item.select_set(False)
        except RuntimeError:
            pass
    for item in selected:
        if item.name in bpy.context.scene.objects:
            try:
                item.select_set(True)
            except RuntimeError:
                pass
    if active is not None and active.name in bpy.context.scene.objects:
        bpy.context.view_layer.objects.active = active


def _supported_operator_options(operator, requested: dict[str, Any]) -> dict[str, Any]:
    properties = {
        item.identifier for item in operator.get_rna_type().properties if item.identifier
    }
    return {key: value for key, value in requested.items() if key in properties}


def _export_animation_fbx(
    config: BridgeConfig,
    args: dict[str, Any],
) -> dict[str, Any]:
    destination = resolve_exchange_path(
        config, args["path"], suffix=".fbx", must_exist=False
    )
    if destination.exists() and not args.get("overwrite", False):
        raise BridgeError(
            "file_already_exists",
            f"Output already exists: {destination}",
        )
    armature = _exact_object(args["armature_name"], "ARMATURE")
    action = _armature_action(armature, args.get("action_name"))
    selected_slot = _action_slot(action, armature, args.get("action_slot"))
    selected_fcurves = _action_fcurves(
        action,
        armature,
        args.get("action_slot"),
    )
    scene = bpy.context.scene
    timing = _verify_timing_contract(
        action,
        armature,
        scene,
        args.get("timing_contract"),
        slot_identifier=args.get("action_slot"),
        trim_verified_duplicate=False,
        normalize_scene=False,
    )
    keyed_frames = [
        float(point.co[0])
        for curve in selected_fcurves
        for point in curve.keyframe_points
    ]
    if not keyed_frames:
        raise BridgeError(
            "fcurve_not_found",
            f'Action "{action.name}" has no keyed curves in the selected slot.',
        )
    if timing:
        export_frame_start = int(round(timing["normalized_frame_range"][0]))
        export_frame_end = int(round(timing["normalized_frame_range"][1]))
    else:
        export_frame_start = math.floor(min(keyed_frames))
        export_frame_end = math.ceil(max(keyed_frames))
    meshes = [_exact_object(name, "MESH") for name in args.get("mesh_names", [])]
    missing_from_scene = [
        item.name for item in [armature, *meshes] if scene.objects.get(item.name) is None
    ]
    if missing_from_scene:
        raise BridgeError(
            "object_not_in_scene",
            "All export objects must belong to the current scene.",
            {"scene": scene.name, "objects": missing_from_scene},
        )
    operator = getattr(getattr(bpy.ops, "export_scene", None), "fbx", None)
    if operator is None:
        raise BridgeError(
            "fbx_export_unavailable",
            "Blender's FBX export operator is not available.",
        )
    temporary = destination.with_name(
        f".{destination.stem}.{secrets.token_hex(8)}.tmp.fbx"
    )

    selected_before, active_before = _selection_state()
    animation_data = armature.animation_data_create()
    action_before = animation_data.action
    action_slot_before = getattr(animation_data, "action_slot", None)
    scene_frame_start_before = scene.frame_start
    scene_frame_end_before = scene.frame_end
    requested_options = {
        "filepath": str(temporary),
        "check_existing": False,
        "use_selection": True,
        "object_types": {"ARMATURE", "MESH"},
        "global_scale": 1.0,
        "apply_unit_scale": True,
        "bake_space_transform": False,
        "axis_forward": "-Y",
        "axis_up": "Z",
        "use_mesh_modifiers": True,
        "add_leaf_bones": False,
        "use_armature_deform_only": False,
        "bake_anim": True,
        "bake_anim_use_all_bones": True,
        "bake_anim_use_nla_strips": False,
        "bake_anim_use_all_actions": False,
        "bake_anim_force_startend_keying": True,
        "bake_anim_step": 1.0,
        "bake_anim_simplify_factor": 0.0,
        "path_mode": "AUTO",
    }
    options = _supported_operator_options(operator, requested_options)
    required = {
        "filepath",
        "use_selection",
        "object_types",
        "global_scale",
        "apply_unit_scale",
        "bake_space_transform",
        "axis_forward",
        "axis_up",
        "use_mesh_modifiers",
        "add_leaf_bones",
        "use_armature_deform_only",
        "bake_anim",
        "bake_anim_use_all_bones",
        "bake_anim_use_nla_strips",
        "bake_anim_use_all_actions",
        "bake_anim_force_startend_keying",
        "bake_anim_step",
        "bake_anim_simplify_factor",
    }
    missing = sorted(required - set(options))
    if missing:
        raise BridgeError(
            "fbx_export_incompatible",
            "This Blender FBX exporter lacks required Unreal-safe options.",
            {"missing_options": missing},
        )

    try:
        for item in bpy.context.scene.objects:
            item.select_set(False)
        for item in [armature, *meshes]:
            item.select_set(True)
        bpy.context.view_layer.objects.active = armature
        animation_data.action = action
        if selected_slot is not None and hasattr(animation_data, "action_slot"):
            animation_data.action_slot = selected_slot
        scene.frame_start = export_frame_start
        scene.frame_end = export_frame_end
        result = operator(**options)
        if "FINISHED" not in result:
            raise BridgeError("fbx_export_failed", f"FBX export returned {sorted(result)}.")
    except Exception:
        temporary.unlink(missing_ok=True)
        raise
    finally:
        animation_data.action = action_before
        if (
            action_slot_before is not None
            and hasattr(animation_data, "action_slot")
            and animation_data.action is not None
        ):
            try:
                animation_data.action_slot = action_slot_before
            except (RuntimeError, TypeError):
                pass
        scene.frame_start = scene_frame_start_before
        scene.frame_end = scene_frame_end_before
        _restore_selection(selected_before, active_before)

    if not temporary.is_file() or temporary.stat().st_size <= 0:
        temporary.unlink(missing_ok=True)
        raise BridgeError("fbx_export_failed", "FBX exporter did not create the file.")
    if destination.exists() and not args.get("overwrite", False):
        temporary.unlink(missing_ok=True)
        raise BridgeError(
            "file_already_exists",
            f"Output appeared during export and was not replaced: {destination}",
        )
    try:
        os.replace(temporary, destination)
    except OSError as exc:
        temporary.unlink(missing_ok=True)
        raise BridgeError(
            "fbx_export_failed",
            f"Could not commit the completed FBX: {destination}",
        ) from exc
    return {
        "path": str(destination),
        "bytes": destination.stat().st_size,
        "armature": armature.name,
        "action": action.name,
        "action_slot": (
            str(getattr(selected_slot, "identifier", ""))
            if selected_slot is not None
            else None
        ),
        "meshes": [item.name for item in meshes],
        "timing": timing,
        "options": {
            "bake_space_transform": False,
            "add_leaf_bones": False,
            "bake_all_bones": True,
            "nla_strips": False,
            "all_actions": False,
            "simplify": 0.0,
            "frame_start": export_frame_start,
            "frame_end": export_frame_end,
        },
    }


def _save_blend_copy(
    config: BridgeConfig,
    args: dict[str, Any],
) -> dict[str, Any]:
    destination = resolve_exchange_path(
        config, args["path"], suffix=".blend", must_exist=False
    )
    if destination.exists() and not args.get("overwrite", False):
        raise BridgeError(
            "file_already_exists",
            f"Output already exists: {destination}",
        )
    temporary = destination.with_name(
        f".{destination.stem}.{secrets.token_hex(8)}.tmp.blend"
    )
    try:
        result = bpy.ops.wm.save_as_mainfile(
            filepath=str(temporary),
            copy=True,
            check_existing=False,
        )
    except Exception:
        temporary.unlink(missing_ok=True)
        raise
    if "FINISHED" not in result or not temporary.is_file() or temporary.stat().st_size <= 0:
        temporary.unlink(missing_ok=True)
        raise BridgeError("blend_save_failed", "Blender did not save the requested copy.")
    if destination.exists() and not args.get("overwrite", False):
        temporary.unlink(missing_ok=True)
        raise BridgeError(
            "file_already_exists",
            f"Output appeared during save and was not replaced: {destination}",
        )
    try:
        os.replace(temporary, destination)
    except OSError as exc:
        temporary.unlink(missing_ok=True)
        raise BridgeError(
            "blend_save_failed",
            f"Could not commit the completed Blender copy: {destination}",
        ) from exc
    return {"path": str(destination), "bytes": destination.stat().st_size, "copy": True}


_COMMAND_HANDLERS = {
    "status": _status,
    "animation.import_fbx": _import_animation_fbx,
    "animation.inspect": _inspect_animation,
    "animation.adjust": _adjust_animation,
    "animation.export_fbx": _export_animation_fbx,
    "file.save_copy": _save_blend_copy,
    **MODELING_COMMAND_HANDLERS,
}


def execute_command(
    config: BridgeConfig,
    command: str,
    arguments: dict[str, Any],
) -> dict[str, Any]:
    args = validate_command(command, arguments)
    return _COMMAND_HANDLERS[command](config, args)


@dataclass
class _PendingRequest:
    config: BridgeConfig
    command: str
    arguments: dict[str, Any]
    event: threading.Event = field(default_factory=threading.Event)
    result: dict[str, Any] | None = None
    error: dict[str, Any] | None = None
    state: str = "queued"
    state_lock: threading.Lock = field(default_factory=threading.Lock)
    created_at: float = field(default_factory=time.time)
    finished_at: float | None = None

    def begin(self) -> bool:
        with self.state_lock:
            if self.state != "queued":
                return False
            self.state = "running"
            return True

    def cancel_queued(self) -> bool:
        with self.state_lock:
            if self.state != "queued":
                return False
            self.state = "cancelled"
            self.finished_at = time.time()
            self.event.set()
            return True

    def finish(self) -> None:
        with self.state_lock:
            self.state = "finished"
            self.finished_at = time.time()
            self.event.set()

    def snapshot(self) -> dict[str, Any]:
        with self.state_lock:
            payload: dict[str, Any] = {
                "job_id": "",
                "command": self.command,
                "state": self.state,
                "created_at": self.created_at,
                "finished_at": self.finished_at,
            }
            if self.result is not None:
                payload["result"] = self.result
            if self.error is not None:
                payload["error"] = self.error
            return payload


class BridgeRuntime:
    def __init__(self) -> None:
        self.config: BridgeConfig | None = None
        self.pending: queue.Queue[_PendingRequest] = queue.Queue(maxsize=128)
        self.connection_queue: queue.Queue[socket.socket] = queue.Queue(
            maxsize=CONNECTION_QUEUE_CAPACITY
        )
        self.stop_event = threading.Event()
        self.ready_event = threading.Event()
        self.thread: threading.Thread | None = None
        self.connection_workers: list[threading.Thread] = []
        self.connection_lock = threading.Lock()
        self.listener_socket: socket.socket | None = None
        self.active_connections: set[socket.socket] = set()
        self.last_error: str | None = None
        self.jobs: dict[str, _PendingRequest] = {}
        self.jobs_lock = threading.Lock()

    @property
    def running(self) -> bool:
        return bool(self.thread and self.thread.is_alive() and not self.last_error)

    def start(self) -> None:
        self.stop()
        if (self.thread and self.thread.is_alive()) or any(
            worker.is_alive() for worker in self.connection_workers
        ):
            raise BridgeError(
                "bridge_stop_failed",
                "The previous Blender bridge listener did not stop.",
            )
        self.config = ensure_config()
        self.connection_queue = queue.Queue(maxsize=CONNECTION_QUEUE_CAPACITY)
        self.connection_workers = []
        self.stop_event = threading.Event()
        self.ready_event.clear()
        self.last_error = None
        stop_event = self.stop_event
        config = self.config
        self.thread = threading.Thread(
            target=self._serve,
            args=(config, stop_event),
            name="RevvyBlenderBridge",
            daemon=True,
        )
        self.thread.start()
        self.ready_event.wait(timeout=2.0)

    def stop(self) -> None:
        self.stop_event.set()
        while True:
            try:
                pending = self.pending.get_nowait()
            except queue.Empty:
                break
            pending.cancel_queued()
        with self.connection_lock:
            listener = self.listener_socket
            active_connections = list(self.active_connections)
        if listener is not None:
            self._close_connection(listener)
        self._drain_connection_queue(self.connection_queue)
        for connection in active_connections:
            self._close_connection(connection)
        if self.thread and self.thread.is_alive():
            self.thread.join(timeout=2.0)
        if self.thread and self.thread.is_alive():
            self.last_error = "Previous bridge listener did not stop within 2 seconds."
            return
        deadline = time.monotonic() + 2.0
        for worker in self.connection_workers:
            remaining = max(0.0, deadline - time.monotonic())
            if worker.is_alive():
                worker.join(timeout=remaining)
        if any(worker.is_alive() for worker in self.connection_workers):
            self.last_error = "Previous bridge connection workers did not stop within 2 seconds."
            return
        self.thread = None
        self.connection_workers = []

    def _serve(self, config: BridgeConfig, stop_event: threading.Event) -> None:
        connection_queue = self.connection_queue
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
                if hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
                    server.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
                server.bind((BRIDGE_HOST, config.port))
                server.listen(8)
                server.settimeout(0.25)
                with self.connection_lock:
                    self.listener_socket = server
                workers = [
                    threading.Thread(
                        target=self._connection_worker,
                        args=(connection_queue, stop_event),
                        name=f"RevvyBlenderBridgeClient-{index + 1}",
                        daemon=True,
                    )
                    for index in range(CONNECTION_WORKER_COUNT)
                ]
                self.connection_workers = workers
                for worker in workers:
                    worker.start()
                self.ready_event.set()
                while not stop_event.is_set():
                    try:
                        connection, _address = server.accept()
                    except socket.timeout:
                        continue
                    except OSError:
                        if stop_event.is_set():
                            break
                        raise
                    connection.settimeout(CONNECTION_READ_TIMEOUT_SECONDS)
                    try:
                        connection_queue.put_nowait(connection)
                    except queue.Full:
                        self._reject_overloaded_connection(connection)
        except OSError as exc:
            if not stop_event.is_set():
                self.last_error = f"{type(exc).__name__}: {exc}"
            self.ready_event.set()
        finally:
            stop_event.set()
            with self.connection_lock:
                self.listener_socket = None
                active_connections = list(self.active_connections)
            self._drain_connection_queue(connection_queue)
            for connection in active_connections:
                self._close_connection(connection)
            deadline = time.monotonic() + 2.0
            for worker in self.connection_workers:
                remaining = max(0.0, deadline - time.monotonic())
                if worker.is_alive():
                    worker.join(timeout=remaining)
            if any(worker.is_alive() for worker in self.connection_workers):
                self.last_error = "Bridge connection workers did not stop within 2 seconds."

    def _connection_worker(
        self,
        connection_queue: queue.Queue[socket.socket],
        stop_event: threading.Event,
    ) -> None:
        while True:
            try:
                connection = connection_queue.get(timeout=0.25)
            except queue.Empty:
                if stop_event.is_set():
                    return
                continue
            try:
                with self.connection_lock:
                    if stop_event.is_set():
                        should_handle = False
                    else:
                        self.active_connections.add(connection)
                        should_handle = True
                if should_handle:
                    with connection:
                        connection.settimeout(CONNECTION_READ_TIMEOUT_SECONDS)
                        self._handle_connection(connection, stop_event)
                else:
                    self._close_connection(connection)
            finally:
                with self.connection_lock:
                    self.active_connections.discard(connection)
                connection_queue.task_done()

    @staticmethod
    def _close_connection(connection: socket.socket) -> None:
        try:
            connection.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        try:
            connection.close()
        except OSError:
            pass

    def _drain_connection_queue(
        self,
        connection_queue: queue.Queue[socket.socket],
    ) -> None:
        while True:
            try:
                connection = connection_queue.get_nowait()
            except queue.Empty:
                return
            self._close_connection(connection)
            connection_queue.task_done()

    def _reject_overloaded_connection(self, connection: socket.socket) -> None:
        request_id: Any = None
        try:
            connection.settimeout(OVERLOAD_READ_TIMEOUT_SECONDS)
            payload = self._read_message(connection)
            if isinstance(payload, dict):
                candidate = payload.get("id")
                if not isinstance(candidate, bool) and isinstance(candidate, (str, int)):
                    request_id = candidate
        except (BridgeError, OSError):
            pass
        self._try_send_message(
            connection,
            {
                "id": request_id,
                "ok": False,
                "error": {
                    "code": "bridge_busy",
                    "message": "Bridge connection capacity is full.",
                },
            },
        )
        try:
            connection.shutdown(socket.SHUT_WR)
        except OSError:
            pass
        try:
            connection.close()
        except OSError:
            pass

    @staticmethod
    def _read_message(connection: socket.socket) -> Any:
        data = bytearray()
        while b"\n" not in data:
            try:
                chunk = connection.recv(min(65536, MAX_MESSAGE_BYTES + 1 - len(data)))
            except socket.timeout as exc:
                raise BridgeError(
                    "request_timeout",
                    "Bridge request was not received before the read timeout.",
                ) from exc
            if not chunk:
                raise BridgeError("invalid_request", "Connection closed before newline.")
            data.extend(chunk)
            if len(data) > MAX_MESSAGE_BYTES:
                raise BridgeError("message_too_large", "Bridge request is too large.")
        line, trailing = bytes(data).split(b"\n", 1)
        if trailing.strip():
            raise BridgeError("invalid_request", "Only one request is allowed per connection.")
        try:
            return json.loads(line.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise BridgeError("invalid_request", "Request must be valid UTF-8 JSON.") from exc

    @staticmethod
    def _send_message(connection: socket.socket, payload: dict[str, Any]) -> None:
        encoded = (
            json.dumps(payload, ensure_ascii=True, separators=(",", ":")).encode("utf-8")
            + b"\n"
        )
        connection.sendall(encoded)

    def _try_send_message(
        self,
        connection: socket.socket,
        payload: dict[str, Any],
    ) -> None:
        try:
            self._send_message(connection, payload)
        except OSError:
            pass

    def _handle_connection(
        self,
        connection: socket.socket,
        stop_event: threading.Event | None = None,
    ) -> None:
        active_stop_event = stop_event or self.stop_event
        request_id: Any = None
        try:
            payload = self._read_message(connection)
            active_config = load_config()
            if not isinstance(payload, dict):
                raise BridgeError("invalid_request", "Request must be a JSON object.")
            request_id = payload.get("id")
            supplied_token = payload.get("token")
            access_scope = ""
            if isinstance(supplied_token, str):
                if secrets.compare_digest(supplied_token, active_config.token):
                    access_scope = "full"
                elif active_config.read_only_token and secrets.compare_digest(
                    supplied_token, active_config.read_only_token
                ):
                    access_scope = "read_only"
            if not access_scope:
                raise BridgeError("unauthorized", "Bridge authentication failed.")
            request_id, _token, command, args, deferred = validate_request(payload)
            if access_scope == "read_only" and command not in READ_ONLY_COMMANDS:
                raise BridgeError(
                    "full_access_required",
                    "This Blender bridge command requires Full access.",
                )
            if command == "job.status":
                result = self.job_status(args["job_id"])
                self._try_send_message(
                    connection,
                    {"id": request_id, "ok": True, "result": result},
                )
                return
            if deferred and command not in DEFERRED_COMMANDS:
                raise BridgeError(
                    "invalid_request",
                    f"{command} does not support deferred execution.",
                )
            pending = _PendingRequest(active_config, command, args)
            job_id = str(request_id)
            if active_stop_event.is_set():
                raise BridgeError("bridge_stopping", "The Blender bridge is stopping.")
            if deferred:
                self._register_job(job_id, pending)
            try:
                self.pending.put(pending, timeout=1.0)
            except queue.Full as exc:
                if deferred:
                    with self.jobs_lock:
                        self.jobs.pop(job_id, None)
                raise BridgeError("bridge_busy", "Bridge request queue is full.") from exc
            if active_stop_event.is_set() and pending.cancel_queued():
                raise BridgeError("bridge_stopping", "The Blender bridge is stopping.")
            if deferred:
                self._try_send_message(
                    connection,
                    {
                        "id": request_id,
                        "ok": True,
                        "result": {
                            "queued": True,
                            "job_id": job_id,
                            "command": command,
                            "state": "queued",
                        },
                    },
                )
                return
            deadline = time.monotonic() + REQUEST_TIMEOUT_SECONDS
            while not pending.event.wait(timeout=0.25):
                if active_stop_event.is_set():
                    pending.cancel_queued()
                    raise BridgeError("bridge_stopping", "The Blender bridge is stopping.")
                if time.monotonic() >= deadline:
                    if pending.cancel_queued():
                        raise BridgeError(
                            "bridge_timeout",
                            "Blender did not start the request before the timeout; it was cancelled.",
                        )
                    pending.event.wait()
                    break
            if pending.error:
                self._try_send_message(
                    connection,
                    {"id": request_id, "ok": False, "error": pending.error},
                )
            else:
                self._try_send_message(
                    connection,
                    {"id": request_id, "ok": True, "result": pending.result},
                )
        except BridgeError as exc:
            self._try_send_message(
                connection,
                {"id": request_id, "ok": False, "error": exc.as_dict()},
            )
        except OSError:
            pass
        except Exception:
            traceback.print_exc()
            self._try_send_message(
                connection,
                {
                    "id": request_id,
                    "ok": False,
                    "error": {
                        "code": "internal_error",
                        "message": "The Blender bridge encountered an internal error.",
                    },
                },
            )

    def _register_job(self, job_id: str, pending: _PendingRequest) -> None:
        with self.jobs_lock:
            if job_id in self.jobs:
                raise BridgeError("duplicate_job_id", f"Bridge job already exists: {job_id}")
            completed = sorted(
                (
                    (item.finished_at or item.created_at, key)
                    for key, item in self.jobs.items()
                    if item.state in {"finished", "cancelled"}
                )
            )
            while len(self.jobs) >= 128 and completed:
                _stamp, oldest = completed.pop(0)
                self.jobs.pop(oldest, None)
            if len(self.jobs) >= 128:
                raise BridgeError("bridge_busy", "Too many Blender jobs are still active.")
            self.jobs[job_id] = pending

    def job_status(self, job_id: str) -> dict[str, Any]:
        with self.jobs_lock:
            pending = self.jobs.get(job_id)
        if pending is None:
            raise BridgeError("job_not_found", f"Blender bridge job was not found: {job_id}")
        result = pending.snapshot()
        result["job_id"] = job_id
        return result

    def drain(self) -> None:
        for _ in range(8):
            try:
                pending = self.pending.get_nowait()
            except queue.Empty:
                return
            if not pending.begin():
                continue
            try:
                pending.result = execute_command(
                    pending.config,
                    pending.command,
                    pending.arguments,
                )
            except BridgeError as exc:
                pending.error = exc.as_dict()
            except Exception:
                traceback.print_exc()
                pending.error = {
                    "code": "internal_error",
                    "message": "The Blender command failed internally.",
                }
            finally:
                pending.finish()


_RUNTIME = BridgeRuntime()


def _drain_timer() -> float | None:
    if _RUNTIME.thread is None:
        return None
    _RUNTIME.drain()
    return 0.05


if bpy is not None:

    class REVVY_OT_bridge_restart(bpy.types.Operator):
        bl_idname = "revvy.bridge_restart"
        bl_label = "Restart Bridge"
        bl_description = "Reload the secure config and restart the local Revvy bridge"

        def execute(self, _context):
            _RUNTIME.start()
            if _RUNTIME.running:
                self.report({"INFO"}, f"Listening on {BRIDGE_HOST}:{_RUNTIME.config.port}")
                return {"FINISHED"}
            self.report({"ERROR"}, _RUNTIME.last_error or "Bridge did not start")
            return {"CANCELLED"}


    class REVVY_OT_bridge_stop(bpy.types.Operator):
        bl_idname = "revvy.bridge_stop"
        bl_label = "Stop Bridge"
        bl_description = "Stop the local Revvy Blender bridge"

        def execute(self, _context):
            _RUNTIME.stop()
            return {"FINISHED"}


    class REVVY_PT_bridge(bpy.types.Panel):
        bl_label = "Revvy Blender Bridge"
        bl_idname = "REVVY_PT_bridge"
        bl_space_type = "VIEW_3D"
        bl_region_type = "UI"
        bl_category = "Revvy"

        def draw(self, _context):
            layout = self.layout
            if _RUNTIME.running:
                layout.label(
                    text=f"Connected: {BRIDGE_HOST}:{_RUNTIME.config.port}",
                    icon="CHECKMARK",
                )
                layout.label(text=str(_RUNTIME.config.exchange_root), icon="FILE_FOLDER")
                row = layout.row(align=True)
                row.operator("revvy.bridge_restart", icon="FILE_REFRESH")
                row.operator("revvy.bridge_stop", icon="PAUSE")
            else:
                layout.label(text=_RUNTIME.last_error or "Stopped", icon="ERROR")
                layout.operator("revvy.bridge_restart", icon="PLAY")


    _CLASSES = (
        REVVY_OT_bridge_restart,
        REVVY_OT_bridge_stop,
        REVVY_PT_bridge,
    )
else:
    _CLASSES = ()


def register() -> None:
    if bpy is None:
        raise RuntimeError("Revvy Blender Bridge must be registered inside Blender.")
    for cls in _CLASSES:
        bpy.utils.register_class(cls)
    _RUNTIME.start()
    if not bpy.app.timers.is_registered(_drain_timer):
        bpy.app.timers.register(_drain_timer, first_interval=0.05, persistent=True)


def unregister() -> None:
    if bpy is None:
        return
    _RUNTIME.stop()
    if bpy.app.timers.is_registered(_drain_timer):
        bpy.app.timers.unregister(_drain_timer)
    for cls in reversed(_CLASSES):
        bpy.utils.unregister_class(cls)
