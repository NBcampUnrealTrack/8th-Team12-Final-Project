from __future__ import annotations

import argparse
import json
import pathlib
import socket
import sys
import uuid
from typing import Any


BLENDER_CONTENT_DIR = pathlib.Path(__file__).resolve().parents[1] / "Blender"
if str(BLENDER_CONTENT_DIR) not in sys.path:
    sys.path.insert(0, str(BLENDER_CONTENT_DIR))

from revvy_blender_bridge.common import (  # noqa: E402
    BRIDGE_HOST,
    MAX_MESSAGE_BYTES,
    TIMING_CONTRACT_ID,
    TIMING_FRAME_COUNT_CONVENTION,
    TIMING_SOURCE_EXPORTER,
    TIMING_TERMINAL_POLICY,
    BridgeError,
    ensure_config,
    load_config,
    validate_command,
)


SERVER_NAME = "revvy-blender"
SERVER_VERSION = "1.1.0"
DEFAULT_PROTOCOL_VERSION = "2025-06-18"
SUPPORTED_PROTOCOL_VERSIONS = {
    "2024-11-05",
    "2025-03-26",
    "2025-06-18",
    "2025-11-25",
}
BRIDGE_TIMEOUT_SECONDS = 185.0


def _object_schema(
    properties: dict[str, Any],
    required: list[str] | None = None,
) -> dict[str, Any]:
    schema: dict[str, Any] = {
        "type": "object",
        "properties": properties,
        "additionalProperties": False,
    }
    if required:
        schema["required"] = required
    return schema


_PATH_PROPERTY = {
    "type": "string",
    "minLength": 1,
    "description": (
        "Absolute or exchange-root-relative path. The bridge rejects paths outside "
        "the configured exchange_root."
    ),
}
_NAME_PROPERTY = {
    "type": "string",
    "minLength": 1,
    "maxLength": 128,
    "description": "Exact, case-sensitive Blender data-block name.",
}
_FRAME_PROPERTY = {"type": "number", "minimum": -1000000, "maximum": 1000000}
_TIMING_CONTRACT = _object_schema(
    {
        "contract": {"const": TIMING_CONTRACT_ID},
        "source_exporter": {"const": TIMING_SOURCE_EXPORTER},
        "frame_count_convention": {"const": TIMING_FRAME_COUNT_CONVENTION},
        "terminal_boundary_policy": {"const": TIMING_TERMINAL_POLICY},
        "expected_frame_count": {
            "type": "integer",
            "minimum": 0,
            "maximum": 1000000,
        },
        "expected_duration_seconds": {
            "type": "number",
            "minimum": 0,
            "maximum": 1000000,
        },
        "expected_frame_rate": {
            "type": "number",
            "exclusiveMinimum": 0,
            "maximum": 1000000,
        },
    },
    [
        "contract",
        "source_exporter",
        "frame_count_convention",
        "terminal_boundary_policy",
        "expected_frame_count",
        "expected_duration_seconds",
        "expected_frame_rate",
    ],
)

_TRANSFORM_OPERATION = _object_schema(
    {
        "type": {"const": "transform"},
        "target": {"type": "string", "enum": ["object", "bone"]},
        "bone_name": {
            **_NAME_PROPERTY,
            "description": "Required exact pose-bone name when target is bone.",
        },
        "channel": {
            "type": "string",
            "enum": [
                "translate",
                "rotate_euler",
                "rotate_quaternion",
                "scale",
            ],
        },
        "mode": {"type": "string", "enum": ["add", "multiply", "set"]},
        "values": {
            "type": "array",
            "items": {"type": "number"},
            "minItems": 3,
            "maxItems": 4,
            "description": (
                "XYZ for translate/rotate_euler/scale; WXYZ channel values for "
                "rotate_quaternion, matched by F-curve array index."
            ),
        },
        "units": {
            "type": "string",
            "enum": ["degrees", "radians"],
            "default": "radians",
            "description": "Only valid for rotate_euler.",
        },
        "frame_start": _FRAME_PROPERTY,
        "frame_end": _FRAME_PROPERTY,
    },
    ["type", "target", "channel", "mode", "values"],
)

_TIME_OPERATION = _object_schema(
    {
        "type": {"const": "time"},
        "scale": {
            "type": "number",
            "minimum": 0.001,
            "maximum": 100,
            "default": 1,
        },
        "offset": {**_FRAME_PROPERTY, "default": 0},
        "pivot": {**_FRAME_PROPERTY, "default": 0},
        "frame_start": _FRAME_PROPERTY,
        "frame_end": _FRAME_PROPERTY,
    },
    ["type"],
)

_INTERPOLATION_OPERATION = _object_schema(
    {
        "type": {"const": "interpolation"},
        "value": {
            "type": "string",
            "enum": ["CONSTANT", "LINEAR", "BEZIER"],
        },
        "frame_start": _FRAME_PROPERTY,
        "frame_end": _FRAME_PROPERTY,
    },
    ["type", "value"],
)


TOOLS: list[dict[str, Any]] = [
    {
        "name": "blender_bridge_status",
        "title": "Blender Bridge Status",
        "description": (
            "Check the authenticated local Blender bridge and list exact armature/action "
            "names, scene frame range, FPS, and exchange root. Call this before animation "
            "operations; never guess Blender names."
        ),
        "inputSchema": _object_schema({}),
        "annotations": {
            "readOnlyHint": True,
            "destructiveHint": False,
            "idempotentHint": True,
            "openWorldHint": False,
        },
    },
    {
        "name": "blender_job_status",
        "title": "Blender Job Status",
        "description": (
            "Poll a Blender mutation returned as a queued job. Wait until state=finished, "
            "then use result or error. Never retry an unfinished job."
        ),
        "inputSchema": _object_schema(
            {"job_id": _NAME_PROPERTY},
            ["job_id"],
        ),
        "annotations": {
            "readOnlyHint": True,
            "destructiveHint": False,
            "idempotentHint": True,
            "openWorldHint": False,
        },
    },
    {
        "name": "blender_animation_import_fbx",
        "title": "Import Animation FBX Into Blender",
        "description": (
            "Import an FBX from the configured exchange root into new Blender objects and "
            "actions. Existing objects/actions are never deleted or replaced. Returns the "
            "exact imported names."
        ),
        "inputSchema": _object_schema(
            {
                "path": _PATH_PROPERTY,
                "name_prefix": {
                    **_NAME_PROPERTY,
                    "description": (
                        "Optional prefix applied to newly imported actions and non-armature "
                        "objects. Unreal root and armature identities are preserved."
                    ),
                },
                "root_bone_name": {
                    **_NAME_PROPERTY,
                    "description": (
                        "Exact Unreal Skeleton root name from the export manifest. Blender "
                        "5 FBX imports may absorb this node into the armature object; the "
                        "bridge restores it as a keyed bone before editing."
                    ),
                },
                "expected_bone_hierarchy": {
                    "type": "array",
                    "minItems": 1,
                    "maxItems": 1024,
                    "items": _object_schema(
                        {
                            "name": _NAME_PROPERTY,
                            "parent": {
                                "type": ["string", "null"],
                                "minLength": 1,
                                "maxLength": 128,
                            },
                        },
                        ["name", "parent"],
                    ),
                    "description": (
                        "Exact name/parent signature from the Unreal export manifest. "
                        "Import fails before editing when Blender changes the hierarchy."
                    ),
                },
                "timing_contract": {
                    **_TIMING_CONTRACT,
                    "description": (
                        "Exact Unreal source count, duration, rate, and verified terminal "
                        "duplicate policy from the export job manifest."
                    ),
                },
            },
            ["path"],
        ),
        "annotations": {
            "readOnlyHint": False,
            "destructiveHint": False,
            "idempotentHint": False,
            "openWorldHint": False,
        },
    },
    {
        "name": "blender_animation_inspect",
        "title": "Inspect Blender Animation",
        "description": (
            "Inspect an exact armature and action, including bone hierarchy, F-curves, "
            "key counts, frame range, and scene FPS. If action_name is omitted, uses the "
            "armature's active action."
        ),
        "inputSchema": _object_schema(
            {
                "armature_name": _NAME_PROPERTY,
                "action_name": _NAME_PROPERTY,
                "action_slot": {
                    **_NAME_PROPERTY,
                    "description": (
                        "Exact Blender 5 action-slot identifier. Required when the action "
                        "contains multiple slots."
                    ),
                },
                "timing_contract": _TIMING_CONTRACT,
            },
            ["armature_name"],
        ),
        "annotations": {
            "readOnlyHint": True,
            "destructiveHint": False,
            "idempotentHint": True,
            "openWorldHint": False,
        },
    },
    {
        "name": "blender_animation_adjust",
        "title": "Adjust Blender Animation Keys",
        "description": (
            "Apply only typed transform, timing, or interpolation edits to exact animation "
            "F-curves. Defaults to copying the source action and assigning the copy, so "
            "the original remains intact. No Python or arbitrary code execution is exposed."
        ),
        "inputSchema": _object_schema(
            {
                "armature_name": _NAME_PROPERTY,
                "action_name": _NAME_PROPERTY,
                "action_slot": _NAME_PROPERTY,
                "operations": {
                    "type": "array",
                    "items": {
                        "oneOf": [
                            _TRANSFORM_OPERATION,
                            _TIME_OPERATION,
                            _INTERPOLATION_OPERATION,
                        ]
                    },
                    "minItems": 1,
                    "maxItems": 100,
                },
                "create_copy": {
                    "type": "boolean",
                    "const": True,
                    "default": True,
                    "description": (
                        "Must remain true. Revvy edits a transactional copy and never "
                        "modifies the imported source action."
                    ),
                },
                "output_action_name": {
                    **_NAME_PROPERTY,
                    "description": (
                        "Optional unused exact name for the copied action. Requires "
                        "create_copy=true."
                    ),
                },
            },
            ["armature_name", "action_name", "operations"],
        ),
        "annotations": {
            "readOnlyHint": False,
            "destructiveHint": True,
            "idempotentHint": False,
            "openWorldHint": False,
        },
    },
    {
        "name": "blender_animation_export_fbx",
        "title": "Export Unreal-Safe Animation FBX",
        "description": (
            "Export an exact armature, action, and optional exact meshes to an FBX under "
            "exchange_root. Uses selected-only animation baking with no leaf bones, no "
            "NLA strips, no all-actions export, no simplification, and no baked space "
            "transform. Existing files require overwrite=true."
        ),
        "inputSchema": _object_schema(
            {
                "path": _PATH_PROPERTY,
                "armature_name": _NAME_PROPERTY,
                "action_name": _NAME_PROPERTY,
                "action_slot": _NAME_PROPERTY,
                "timing_contract": _TIMING_CONTRACT,
                "mesh_names": {
                    "type": "array",
                    "items": _NAME_PROPERTY,
                    "maxItems": 256,
                    "uniqueItems": True,
                    "default": [],
                },
                "overwrite": {"type": "boolean", "default": False},
            },
            ["path", "armature_name"],
        ),
        "annotations": {
            "readOnlyHint": False,
            "destructiveHint": True,
            "idempotentHint": False,
            "openWorldHint": False,
        },
    },
    {
        "name": "blender_save_copy",
        "title": "Save Blender File Copy",
        "description": (
            "Save a copy of the current .blend file under exchange_root without changing "
            "the working file path. Existing files require overwrite=true."
        ),
        "inputSchema": _object_schema(
            {
                "path": _PATH_PROPERTY,
                "overwrite": {"type": "boolean", "default": False},
            },
            ["path"],
        ),
        "annotations": {
            "readOnlyHint": False,
            "destructiveHint": True,
            "idempotentHint": False,
            "openWorldHint": False,
        },
    },
]

_VECTOR3_SCHEMA = {
    "type": "array",
    "items": {"type": "number", "minimum": -1000000, "maximum": 1000000},
    "minItems": 3,
    "maxItems": 3,
}
_MESH_REQUIREMENTS_SCHEMA = _object_schema(
    {
        "require_uv": {"type": "boolean", "default": False},
        "require_material": {"type": "boolean", "default": False},
        "require_manifold": {"type": "boolean", "default": False},
        "require_applied_scale": {"type": "boolean", "default": False},
        "max_triangles": {
            "type": "integer",
            "minimum": 1,
            "maximum": 100000000,
        },
    }
)
_TRANSFORM_OBJECT_EDIT = _object_schema(
    {
        "type": {"const": "transform_object"},
        "location": _VECTOR3_SCHEMA,
        "rotation_degrees": _VECTOR3_SCHEMA,
        "scale": _VECTOR3_SCHEMA,
    },
    ["type"],
)
_TRANSFORM_OBJECT_EDIT["anyOf"] = [
    {"required": ["location"]},
    {"required": ["rotation_degrees"]},
    {"required": ["scale"]},
]
_TRANSFORM_VERTEX_EDIT = _object_schema(
    {
        "type": {"const": "transform_vertices"},
        "translation": _VECTOR3_SCHEMA,
        "rotation_degrees": _VECTOR3_SCHEMA,
        "scale": _VECTOR3_SCHEMA,
    },
    ["type"],
)
_TRANSFORM_VERTEX_EDIT["anyOf"] = [
    {"required": ["translation"]},
    {"required": ["rotation_degrees"]},
    {"required": ["scale"]},
]
_MERGE_EDIT = _object_schema(
    {
        "type": {"const": "merge_by_distance"},
        "distance": {
            "type": "number",
            "minimum": 0.000000001,
            "maximum": 10,
            "default": 0.0001,
        },
    },
    ["type"],
)
_RECALCULATE_NORMALS_EDIT = _object_schema(
    {"type": {"const": "recalculate_normals"}},
    ["type"],
)
_TRIANGULATE_EDIT = _object_schema(
    {"type": {"const": "triangulate"}},
    ["type"],
)
_DELETE_LOOSE_EDIT = _object_schema(
    {"type": {"const": "delete_loose"}},
    ["type"],
)
_SHADE_SMOOTH_EDIT = _object_schema(
    {
        "type": {"const": "shade_smooth"},
        "value": {"type": "boolean", "default": True},
    },
    ["type"],
)
_BEVEL_MODIFIER = _object_schema(
    {
        "type": {"const": "bevel"},
        "width": {
            "type": "number",
            "exclusiveMinimum": 0,
            "maximum": 1000000,
            "default": 0.01,
        },
        "segments": {
            "type": "integer",
            "minimum": 1,
            "maximum": 64,
            "default": 2,
        },
        "angle_degrees": {
            "type": "number",
            "minimum": 0,
            "maximum": 180,
            "default": 30,
        },
    },
    ["type"],
)
_SUBDIVISION_MODIFIER = _object_schema(
    {
        "type": {"const": "subdivision"},
        "levels": {
            "type": "integer",
            "minimum": 0,
            "maximum": 6,
            "default": 1,
        },
        "render_levels": {
            "type": "integer",
            "minimum": 0,
            "maximum": 8,
            "default": 1,
        },
    },
    ["type"],
)
_SOLIDIFY_MODIFIER = _object_schema(
    {
        "type": {"const": "solidify"},
        "thickness": {
            "type": "number",
            "minimum": -1000000,
            "maximum": 1000000,
            "default": 0.01,
        },
        "offset": {
            "type": "number",
            "minimum": -1,
            "maximum": 1,
            "default": 0,
        },
    },
    ["type"],
)
_DECIMATE_MODIFIER = _object_schema(
    {
        "type": {"const": "decimate"},
        "ratio": {
            "type": "number",
            "minimum": 0.01,
            "maximum": 1,
            "default": 0.5,
        },
    },
    ["type"],
)
_MIRROR_MODIFIER = _object_schema(
    {
        "type": {"const": "mirror"},
        "axes": {
            "type": "array",
            "items": {"type": "boolean"},
            "minItems": 3,
            "maxItems": 3,
            "default": [True, False, False],
        },
        "use_clip": {"type": "boolean", "default": True},
    },
    ["type"],
)
_TRIANGULATE_MODIFIER = _object_schema(
    {"type": {"const": "triangulate"}},
    ["type"],
)
_COPY_PROPERTIES = {
    "source_object_name": {
        **_NAME_PROPERTY,
        "description": "Exact source mesh object name returned by inspection.",
    },
    "output_object_name": {
        **_NAME_PROPERTY,
        "description": "Required unused name for the new mesh object.",
    },
    "create_copy": {
        "type": "boolean",
        "const": True,
        "default": True,
        "description": "Must remain true; the source object is never edited in place.",
    },
}

TOOLS.extend(
    [
        {
            "name": "blender_scene_inspect",
            "title": "Inspect Blender Scene",
            "description": (
                "List exact scene object names, types, transforms, parents, collections, "
                "mesh fingerprints, and materials. Use before any modeling mutation."
            ),
            "inputSchema": _object_schema(
                {
                    "include_hidden": {"type": "boolean", "default": False},
                    "max_objects": {
                        "type": "integer",
                        "minimum": 1,
                        "maximum": 2000,
                        "default": 500,
                    },
                }
            ),
            "annotations": {
                "readOnlyHint": True,
                "destructiveHint": False,
                "idempotentHint": True,
                "openWorldHint": False,
            },
        },
        {
            "name": "blender_model_import",
            "title": "Import Model Into Blender",
            "description": (
                "Import FBX, OBJ, GLB, or glTF from exchange_root. Every imported "
                "object receives the required prefix and existing objects are never replaced."
            ),
            "inputSchema": _object_schema(
                {
                    "path": _PATH_PROPERTY,
                    "name_prefix": {
                        **_NAME_PROPERTY,
                        "description": "Required prefix for every newly imported object.",
                    },
                },
                ["path", "name_prefix"],
            ),
            "annotations": {
                "readOnlyHint": False,
                "destructiveHint": False,
                "idempotentHint": False,
                "openWorldHint": False,
            },
        },
        {
            "name": "blender_mesh_inspect",
            "title": "Inspect Blender Mesh",
            "description": (
                "Inspect an exact mesh object and return a geometry fingerprint, counts, "
                "bounds, manifold/normal diagnostics, transforms, modifiers, materials, "
                "UV layers, blockers, and warnings."
            ),
            "inputSchema": _object_schema(
                {
                    "object_name": _NAME_PROPERTY,
                    "requirements": _MESH_REQUIREMENTS_SCHEMA,
                },
                ["object_name"],
            ),
            "annotations": {
                "readOnlyHint": True,
                "destructiveHint": False,
                "idempotentHint": True,
                "openWorldHint": False,
            },
        },
        {
            "name": "blender_mesh_edit",
            "title": "Edit Blender Mesh Copy",
            "description": (
                "Apply bounded typed whole-object or whole-mesh operations to a new copy. "
                "The exact source mesh and its fingerprint remain unchanged; arbitrary "
                "Python and element-selection expressions are not supported."
            ),
            "inputSchema": _object_schema(
                {
                    **_COPY_PROPERTIES,
                    "operations": {
                        "type": "array",
                        "minItems": 1,
                        "maxItems": 32,
                        "items": {
                            "oneOf": [
                                _TRANSFORM_OBJECT_EDIT,
                                _TRANSFORM_VERTEX_EDIT,
                                _MERGE_EDIT,
                                _RECALCULATE_NORMALS_EDIT,
                                _TRIANGULATE_EDIT,
                                _DELETE_LOOSE_EDIT,
                                _SHADE_SMOOTH_EDIT,
                            ]
                        },
                    },
                },
                ["source_object_name", "output_object_name", "operations"],
            ),
            "annotations": {
                "readOnlyHint": False,
                "destructiveHint": False,
                "idempotentHint": False,
                "openWorldHint": False,
            },
        },
        {
            "name": "blender_modifier_apply",
            "title": "Apply Typed Modifier To Mesh Copy",
            "description": (
                "Create a mesh copy, add one bounded typed modifier, and apply it. "
                "Supported modifiers are bevel, subdivision, solidify, decimate, mirror, "
                "and triangulate."
            ),
            "inputSchema": _object_schema(
                {
                    **_COPY_PROPERTIES,
                    "modifier": {
                        "oneOf": [
                            _BEVEL_MODIFIER,
                            _SUBDIVISION_MODIFIER,
                            _SOLIDIFY_MODIFIER,
                            _DECIMATE_MODIFIER,
                            _MIRROR_MODIFIER,
                            _TRIANGULATE_MODIFIER,
                        ]
                    },
                },
                ["source_object_name", "output_object_name", "modifier"],
            ),
            "annotations": {
                "readOnlyHint": False,
                "destructiveHint": False,
                "idempotentHint": False,
                "openWorldHint": False,
            },
        },
        {
            "name": "blender_material_assign",
            "title": "Assign Material To Mesh Copy",
            "description": (
                "Assign an exact existing material, or create one bounded Principled "
                "material, on a new mesh copy. Existing materials and the source mesh "
                "are never modified."
            ),
            "inputSchema": _object_schema(
                {
                    **_COPY_PROPERTIES,
                    "material_name": _NAME_PROPERTY,
                    "create_material": {"type": "boolean", "default": False},
                    "base_color": {
                        "type": "array",
                        "items": {"type": "number", "minimum": 0, "maximum": 1},
                        "minItems": 4,
                        "maxItems": 4,
                        "description": "RGBA used only when create_material=true.",
                    },
                    "metallic": {
                        "type": "number",
                        "minimum": 0,
                        "maximum": 1,
                        "description": "Used only when create_material=true.",
                    },
                    "roughness": {
                        "type": "number",
                        "minimum": 0,
                        "maximum": 1,
                        "description": "Used only when create_material=true.",
                    },
                    "replace_slot": {
                        "type": "integer",
                        "minimum": 0,
                        "maximum": 1024,
                        "description": (
                            "Optional existing material slot to replace on the copy. "
                            "When omitted, a new slot is assigned to all faces."
                        ),
                    },
                },
                ["source_object_name", "output_object_name", "material_name"],
            ),
            "annotations": {
                "readOnlyHint": False,
                "destructiveHint": False,
                "idempotentHint": False,
                "openWorldHint": False,
            },
        },
        {
            "name": "blender_uv_inspect",
            "title": "Inspect Blender UVs",
            "description": (
                "Inspect exact UV layer names, bounds, finite values, out-of-range loops, "
                "degenerate UV faces, and the source mesh fingerprint."
            ),
            "inputSchema": _object_schema(
                {
                    "object_name": _NAME_PROPERTY,
                    "uv_layer_name": _NAME_PROPERTY,
                },
                ["object_name"],
            ),
            "annotations": {
                "readOnlyHint": True,
                "destructiveHint": False,
                "idempotentHint": True,
                "openWorldHint": False,
            },
        },
        {
            "name": "blender_uv_edit",
            "title": "Edit UVs On Mesh Copy",
            "description": (
                "Run one typed smart-project, cube-project, unwrap, or pack-islands "
                "operation on a new mesh copy and return structured UV diagnostics."
            ),
            "inputSchema": _object_schema(
                {
                    **_COPY_PROPERTIES,
                    "operation": {
                        "type": "string",
                        "enum": [
                            "smart_project",
                            "cube_project",
                            "unwrap",
                            "pack_islands",
                        ],
                    },
                    "uv_layer_name": _NAME_PROPERTY,
                    "create_layer": {"type": "boolean", "default": False},
                    "angle_limit_degrees": {
                        "type": "number",
                        "minimum": 1,
                        "maximum": 89,
                        "default": 66,
                    },
                    "island_margin": {
                        "type": "number",
                        "minimum": 0,
                        "maximum": 0.5,
                        "default": 0.02,
                    },
                    "cube_size": {
                        "type": "number",
                        "minimum": 0.000001,
                        "maximum": 1000000,
                        "default": 1,
                    },
                },
                ["source_object_name", "output_object_name", "operation"],
            ),
            "annotations": {
                "readOnlyHint": False,
                "destructiveHint": False,
                "idempotentHint": False,
                "openWorldHint": False,
            },
        },
        {
            "name": "blender_model_export",
            "title": "Export Exact Blender Models",
            "description": (
                "Export only explicit exact mesh objects to FBX, OBJ, or GLB under "
                "exchange_root. The completed file is committed atomically; existing "
                "files require overwrite=true."
            ),
            "inputSchema": _object_schema(
                {
                    "path": _PATH_PROPERTY,
                    "object_names": {
                        "type": "array",
                        "items": _NAME_PROPERTY,
                        "minItems": 1,
                        "maxItems": 256,
                        "uniqueItems": True,
                    },
                    "overwrite": {"type": "boolean", "default": False},
                    "apply_modifiers": {"type": "boolean", "default": True},
                },
                ["path", "object_names"],
            ),
            "annotations": {
                "readOnlyHint": False,
                "destructiveHint": True,
                "idempotentHint": False,
                "openWorldHint": False,
            },
        },
    ]
)

_TOOL_COMMANDS = {
    "blender_bridge_status": "status",
    "blender_job_status": "job.status",
    "blender_scene_inspect": "scene.inspect",
    "blender_model_import": "model.import",
    "blender_mesh_inspect": "mesh.inspect",
    "blender_mesh_edit": "mesh.edit",
    "blender_modifier_apply": "modifier.apply",
    "blender_material_assign": "material.assign",
    "blender_uv_inspect": "uv.inspect",
    "blender_uv_edit": "uv.edit",
    "blender_model_export": "model.export",
    "blender_animation_import_fbx": "animation.import_fbx",
    "blender_animation_inspect": "animation.inspect",
    "blender_animation_adjust": "animation.adjust",
    "blender_animation_export_fbx": "animation.export_fbx",
    "blender_save_copy": "file.save_copy",
}

_DEFERRED_COMMANDS = frozenset(
    {
        "model.import",
        "mesh.edit",
        "modifier.apply",
        "material.assign",
        "uv.edit",
        "model.export",
        "animation.import_fbx",
        "animation.adjust",
        "animation.export_fbx",
        "file.save_copy",
    }
)


def _ack_lost_error(request_id: str) -> BridgeError:
    return BridgeError(
        "bridge_ack_lost",
        (
            "Blender may have accepted the mutation, but its acknowledgement "
            "was lost. Poll the returned job_id before any retry."
        ),
        {
            "job_id": request_id,
            "may_have_started": True,
            "poll_with": "blender_job_status",
        },
    )


def call_bridge(
    command: str,
    arguments: dict[str, Any],
    *,
    request_id: str | None = None,
) -> dict[str, Any]:
    validated_arguments = validate_command(command, arguments)
    config = load_config()
    timeout_seconds = 2.0 if command == "status" else BRIDGE_TIMEOUT_SECONDS
    request_id = request_id or uuid.uuid4().hex
    request = {
        "id": request_id,
        "token": config.token,
        "command": command,
        "args": validated_arguments,
        "defer": command in _DEFERRED_COMMANDS,
    }
    encoded = (
        json.dumps(request, ensure_ascii=True, separators=(",", ":")).encode("utf-8")
        + b"\n"
    )
    send_attempted = False
    try:
        with socket.create_connection(
            (BRIDGE_HOST, config.port),
            timeout=timeout_seconds,
        ) as connection:
            connection.settimeout(timeout_seconds)
            send_attempted = True
            connection.sendall(encoded)
            data = bytearray()
            while b"\n" not in data:
                chunk = connection.recv(min(65536, MAX_MESSAGE_BYTES + 1 - len(data)))
                if not chunk:
                    if command in _DEFERRED_COMMANDS:
                        raise _ack_lost_error(request_id)
                    raise BridgeError(
                        "bridge_disconnected",
                        "Blender closed the bridge connection without a response.",
                    )
                data.extend(chunk)
                if len(data) > MAX_MESSAGE_BYTES:
                    if command in _DEFERRED_COMMANDS:
                        raise _ack_lost_error(request_id)
                    raise BridgeError(
                        "message_too_large",
                        "Blender bridge response exceeded the size limit.",
                    )
    except (ConnectionError, TimeoutError, OSError) as exc:
        if send_attempted and command in _DEFERRED_COMMANDS:
            raise _ack_lost_error(request_id) from exc
        raise BridgeError(
            "blender_unavailable",
            (
                f"Could not reach the Revvy Blender Bridge at "
                f"{BRIDGE_HOST}:{config.port}. Open Blender and enable the add-on."
            ),
        ) from exc

    line, trailing = bytes(data).split(b"\n", 1)
    if trailing.strip():
        if command in _DEFERRED_COMMANDS:
            raise _ack_lost_error(request_id)
        raise BridgeError(
            "invalid_bridge_response",
            "Blender returned more than one response message.",
        )
    try:
        response = json.loads(line.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        if command in _DEFERRED_COMMANDS:
            raise _ack_lost_error(request_id) from exc
        raise BridgeError(
            "invalid_bridge_response",
            "Blender returned invalid UTF-8 JSON.",
        ) from exc
    if not isinstance(response, dict) or response.get("id") != request_id:
        if command in _DEFERRED_COMMANDS:
            raise _ack_lost_error(request_id)
        raise BridgeError(
            "invalid_bridge_response",
            "Blender returned a response with an invalid request id.",
        )
    if response.get("ok") is True and isinstance(response.get("result"), dict):
        return response["result"]
    error = response.get("error")
    if isinstance(error, dict):
        raise BridgeError(
            str(error.get("code", "blender_error")),
            str(error.get("message", "Blender command failed.")),
            error.get("details") if isinstance(error.get("details"), dict) else None,
        )
    if command in _DEFERRED_COMMANDS:
        raise _ack_lost_error(request_id)
    raise BridgeError("invalid_bridge_response", "Blender returned an invalid response.")


def _tool_result(payload: dict[str, Any], *, is_error: bool) -> dict[str, Any]:
    return {
        "content": [
            {
                "type": "text",
                "text": json.dumps(payload, ensure_ascii=True, indent=2),
            }
        ],
        "structuredContent": payload,
        "isError": is_error,
    }


def call_tool(
    name: str,
    arguments: Any,
    *,
    request_id: str | None = None,
) -> dict[str, Any]:
    command = _TOOL_COMMANDS.get(name)
    if command is None:
        raise BridgeError("unknown_tool", f"Unknown tool: {name}")
    if arguments is None:
        arguments = {}
    if not isinstance(arguments, dict):
        raise BridgeError("invalid_arguments", "Tool arguments must be an object.")
    try:
        result = call_bridge(command, arguments, request_id=request_id)
        return _tool_result({"success": True, "result": result}, is_error=False)
    except BridgeError as exc:
        return _tool_result(
            {"success": False, "error": exc.as_dict()},
            is_error=True,
        )


class MCPServer:
    def __init__(self) -> None:
        self.initialized = False
        self.protocol_version = DEFAULT_PROTOCOL_VERSION

    @staticmethod
    def _error(request_id: Any, code: int, message: str, data: Any = None) -> dict[str, Any]:
        error: dict[str, Any] = {"code": code, "message": message}
        if data is not None:
            error["data"] = data
        return {"jsonrpc": "2.0", "id": request_id, "error": error}

    @staticmethod
    def _result(request_id: Any, result: dict[str, Any]) -> dict[str, Any]:
        return {"jsonrpc": "2.0", "id": request_id, "result": result}

    def handle(self, message: Any) -> dict[str, Any] | None:
        if not isinstance(message, dict):
            return self._error(None, -32600, "Invalid Request")
        request_id = message.get("id")
        if message.get("jsonrpc") != "2.0" or not isinstance(message.get("method"), str):
            return self._error(request_id, -32600, "Invalid Request")
        method = message["method"]
        params = message.get("params", {})
        is_notification = "id" not in message

        if method == "notifications/initialized":
            self.initialized = True
            return None
        if method == "notifications/cancelled":
            return None
        if is_notification:
            return None
        if not isinstance(params, dict):
            return self._error(request_id, -32602, "Invalid params")

        if method == "initialize":
            requested = params.get("protocolVersion")
            if requested in SUPPORTED_PROTOCOL_VERSIONS:
                self.protocol_version = requested
            else:
                self.protocol_version = DEFAULT_PROTOCOL_VERSION
            return self._result(
                request_id,
                {
                    "protocolVersion": self.protocol_version,
                    "capabilities": {"tools": {"listChanged": False}},
                    "serverInfo": {
                        "name": SERVER_NAME,
                        "title": "Revvy Blender Modeling and Animation Bridge",
                        "version": SERVER_VERSION,
                    },
                    "instructions": (
                        "Call blender_bridge_status and the matching typed inspection tool "
                        "before every mutation. Use only exact returned object, armature, "
                        "action, material, and UV names. All file paths are confined to "
                        "exchange_root. Keep create_copy=true and overwrite=false. Arbitrary "
                        "Python and code execution are not exposed."
                    ),
                },
            )
        if method == "ping":
            return self._result(request_id, {})
        if method == "tools/list":
            cursor = params.get("cursor")
            if cursor not in (None, ""):
                return self._error(request_id, -32602, "Invalid cursor")
            return self._result(request_id, {"tools": TOOLS})
        if method == "tools/call":
            name = params.get("name")
            if not isinstance(name, str):
                return self._error(request_id, -32602, "Tool name is required")
            if name not in _TOOL_COMMANDS:
                return self._error(request_id, -32602, f"Unknown tool: {name}")
            try:
                stable_request_id = (
                    str(request_id)
                    if _TOOL_COMMANDS.get(name) in _DEFERRED_COMMANDS
                    and not isinstance(request_id, bool)
                    and isinstance(request_id, (str, int))
                    else None
                )
                result = call_tool(
                    name,
                    params.get("arguments", {}),
                    request_id=stable_request_id,
                )
            except BridgeError as exc:
                return self._error(request_id, -32602, exc.message, exc.as_dict())
            return self._result(request_id, result)
        return self._error(request_id, -32601, f"Method not found: {method}")


def run_stdio() -> int:
    server = MCPServer()
    for raw_line in sys.stdin.buffer:
        try:
            message = json.loads(raw_line.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            response = server._error(None, -32700, "Parse error")
        else:
            try:
                response = server.handle(message)
            except Exception:
                response = server._error(
                    message.get("id") if isinstance(message, dict) else None,
                    -32603,
                    "Internal error",
                )
        if response is not None:
            output = json.dumps(response, ensure_ascii=True, separators=(",", ":"))
            sys.stdout.write(output + "\n")
            sys.stdout.flush()
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Revvy Blender stdio MCP server")
    parser.add_argument(
        "--ensure-config",
        action="store_true",
        help="Create the secure bridge config and exchange root if missing.",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Call Blender bridge status once instead of running stdio MCP.",
    )
    options = parser.parse_args(argv)
    if options.ensure_config:
        config = ensure_config()
        print(json.dumps(config.public_dict(), ensure_ascii=True, indent=2))
        return 0
    if options.check:
        try:
            result = call_bridge("status", {})
        except BridgeError as exc:
            print(json.dumps({"success": False, "error": exc.as_dict()}, indent=2))
            return 1
        print(json.dumps({"success": True, "result": result}, indent=2))
        return 0
    return run_stdio()


if __name__ == "__main__":
    raise SystemExit(main())
