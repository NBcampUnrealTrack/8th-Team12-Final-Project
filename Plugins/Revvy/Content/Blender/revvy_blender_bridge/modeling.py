from __future__ import annotations

"""Typed, non-destructive Blender modeling handlers for the Revvy bridge.

This module intentionally exposes no Python/script/console escape hatch.  Every
mutation operates on an exact object name, creates a new mesh object, and is
validated by :mod:`revvy_blender_bridge.common` before reaching these handlers.
"""

import hashlib
import math
import os
import secrets
import struct
from typing import Any, Iterable

from .common import (
    BridgeConfig,
    BridgeError,
    resolve_exchange_path_any,
)

try:
    import bmesh
    import bpy
    from mathutils import Euler, Matrix, Vector
except ModuleNotFoundError:  # Protocol/schema tests run outside Blender.
    bmesh = bpy = None
    Euler = Matrix = Vector = None


MODEL_IMPORT_EXTENSIONS = (".fbx", ".obj", ".glb", ".gltf")
MODEL_EXPORT_EXTENSIONS = (".fbx", ".obj", ".glb")
_ID_COLLECTIONS = (
    "objects",
    "meshes",
    "materials",
    "images",
    "textures",
    "collections",
    "curves",
    "cameras",
    "lights",
    "actions",
    "armatures",
)


def _require_blender() -> None:
    if bpy is None or bmesh is None:
        raise BridgeError(
            "blender_unavailable",
            "This command must run on Blender's main thread.",
        )


def _exact_object(name: str, object_type: str | None = None):
    obj = bpy.data.objects.get(name)
    if obj is None or (object_type is not None and obj.type != object_type):
        available = sorted(
            item.name
            for item in bpy.data.objects
            if object_type is None or item.type == object_type
        )
        label = object_type.lower() if object_type else "Blender"
        raise BridgeError(
            "object_not_found",
            f'Exact {label} object "{name}" was not found.',
            {"available": available[:200]},
        )
    return obj


def _finite_values(values: Iterable[float]) -> bool:
    return all(math.isfinite(float(value)) for value in values)


def _sync_view_layer() -> None:
    """Flush pending object/mesh transforms before fingerprints and reports."""
    view_layer = getattr(getattr(bpy, "context", None), "view_layer", None)
    update = getattr(view_layer, "update", None)
    if callable(update):
        update()


def _vector_payload(value: Any) -> list[float]:
    return [float(component) for component in value]


def _matrix_payload(matrix: Any) -> list[list[float]]:
    return [[float(value) for value in row] for row in matrix]


def _mesh_fingerprint(obj: Any) -> str:
    _sync_view_layer()
    mesh = obj.data
    digest = hashlib.sha256()
    digest.update(b"revvy-blender-mesh-v1\0")
    for row in obj.matrix_world:
        digest.update(struct.pack("<4d", *(float(value) for value in row)))
    for vertex in mesh.vertices:
        digest.update(struct.pack("<3d", *(float(value) for value in vertex.co)))
    for edge in mesh.edges:
        digest.update(struct.pack("<2I", *sorted(int(value) for value in edge.vertices)))
    for polygon in mesh.polygons:
        indices = tuple(int(value) for value in polygon.vertices)
        digest.update(struct.pack("<I", len(indices)))
        if indices:
            digest.update(struct.pack(f"<{len(indices)}I", *indices))
        digest.update(struct.pack("<I?", int(polygon.material_index), bool(polygon.use_smooth)))
    for layer in mesh.uv_layers:
        digest.update(layer.name.encode("utf-8", errors="surrogatepass"))
        digest.update(b"\0")
        for loop in layer.data:
            digest.update(struct.pack("<2d", float(loop.uv.x), float(loop.uv.y)))
    return digest.hexdigest()


def _bounds_payload(obj: Any) -> dict[str, list[float]]:
    local_points = [Vector(corner) for corner in obj.bound_box]
    world_points = [obj.matrix_world @ point for point in local_points]

    def bounds(points: list[Any]) -> tuple[list[float], list[float]]:
        if not points:
            return [0.0, 0.0, 0.0], [0.0, 0.0, 0.0]
        minimum = [min(float(point[index]) for point in points) for index in range(3)]
        maximum = [max(float(point[index]) for point in points) for index in range(3)]
        return minimum, maximum

    local_min, local_max = bounds(local_points)
    world_min, world_max = bounds(world_points)
    return {
        "local_min": local_min,
        "local_max": local_max,
        "world_min": world_min,
        "world_max": world_max,
    }


def _uv_layer_payload(mesh: Any, layer: Any) -> dict[str, Any]:
    values = [
        (float(loop.uv.x), float(loop.uv.y))
        for loop in layer.data
    ]
    finite_values = [
        component
        for pair in values
        for component in pair
        if math.isfinite(component)
    ]
    non_finite = sum(
        1 for pair in values if not _finite_values(pair)
    )
    if values and len(finite_values) == len(values) * 2:
        minimum = [
            min(pair[index] for pair in values)
            for index in range(2)
        ]
        maximum = [
            max(pair[index] for pair in values)
            for index in range(2)
        ]
    else:
        minimum = maximum = [0.0, 0.0]
    out_of_range = sum(
        1
        for u, v in values
        if math.isfinite(u)
        and math.isfinite(v)
        and (u < -1e-6 or u > 1.000001 or v < -1e-6 or v > 1.000001)
    )
    degenerate_faces = 0
    for polygon in mesh.polygons:
        coordinates = [
            values[loop_index]
            for loop_index in polygon.loop_indices
            if 0 <= loop_index < len(values)
        ]
        if len(coordinates) < 3 or not all(_finite_values(item) for item in coordinates):
            degenerate_faces += 1
            continue
        twice_area = 0.0
        for index, current in enumerate(coordinates):
            following = coordinates[(index + 1) % len(coordinates)]
            twice_area += current[0] * following[1] - following[0] * current[1]
        if abs(twice_area) <= 1e-12:
            degenerate_faces += 1
    return {
        "name": layer.name,
        "active": mesh.uv_layers.active == layer,
        "active_render": bool(getattr(layer, "active_render", False)),
        "loop_count": len(values),
        "bounds_min": minimum,
        "bounds_max": maximum,
        "out_of_zero_one_range": out_of_range,
        "non_finite": non_finite,
        "degenerate_faces": degenerate_faces,
    }


def _uv_fingerprint(layer: Any) -> str:
    digest = hashlib.sha256()
    digest.update(b"revvy-blender-uv-v1\0")
    digest.update(layer.name.encode("utf-8", errors="surrogatepass"))
    digest.update(b"\0")
    for loop in layer.data:
        digest.update(struct.pack("<2d", float(loop.uv.x), float(loop.uv.y)))
    return digest.hexdigest()


def _mesh_report(obj: Any, requirements: dict[str, Any] | None = None) -> dict[str, Any]:
    _sync_view_layer()
    mesh = obj.data
    requirements = dict(requirements or {})
    vertices = list(mesh.vertices)
    edges = list(mesh.edges)
    polygons = list(mesh.polygons)
    triangle_count = sum(max(0, len(polygon.vertices) - 2) for polygon in polygons)

    referenced_vertices = {
        int(index)
        for edge in edges
        for index in edge.vertices
    }
    polygon_edge_keys = {
        tuple(sorted((int(first), int(second))))
        for polygon in polygons
        for first, second in polygon.edge_keys
    }
    loose_vertices = sum(
        1 for vertex in vertices if int(vertex.index) not in referenced_vertices
    )
    loose_edges = sum(
        1
        for edge in edges
        if tuple(sorted(int(value) for value in edge.vertices)) not in polygon_edge_keys
    )
    zero_length_edges = sum(
        1
        for edge in edges
        if (
            mesh.vertices[edge.vertices[0]].co
            - mesh.vertices[edge.vertices[1]].co
        ).length_squared
        <= 1e-18
    )
    non_finite_vertices = sum(
        1 for vertex in vertices if not _finite_values(vertex.co)
    )
    degenerate_faces = sum(
        1
        for polygon in polygons
        if not math.isfinite(float(polygon.area)) or float(polygon.area) <= 1e-12
    )
    invalid_face_normals = sum(
        1
        for polygon in polygons
        if not _finite_values(polygon.normal)
        or float(polygon.normal.length_squared) <= 1e-18
    )

    edit_mesh = bmesh.new()
    try:
        edit_mesh.from_mesh(mesh)
        boundary_edges = sum(1 for edge in edit_mesh.edges if edge.is_boundary)
        wire_edges = sum(1 for edge in edit_mesh.edges if edge.is_wire)
        non_manifold_edges = sum(
            1
            for edge in edit_mesh.edges
            if not edge.is_manifold and not edge.is_boundary and not edge.is_wire
        )
    finally:
        edit_mesh.free()

    uv_layers = [_uv_layer_payload(mesh, layer) for layer in mesh.uv_layers]
    materials = [
        {
            "slot": index,
            "name": slot.material.name if slot.material is not None else None,
        }
        for index, slot in enumerate(obj.material_slots)
    ]
    modifiers = [
        {
            "name": modifier.name,
            "type": modifier.type,
            "show_viewport": bool(modifier.show_viewport),
            "show_render": bool(modifier.show_render),
        }
        for modifier in obj.modifiers
    ]
    scale = _vector_payload(obj.scale)
    blockers: list[dict[str, str]] = []
    warnings: list[dict[str, str]] = []

    def issue(target: list[dict[str, str]], code: str, message: str) -> None:
        target.append({"code": code, "message": message})

    if not vertices or not polygons:
        issue(blockers, "empty_mesh", "Mesh must contain vertices and polygons.")
    if non_finite_vertices:
        issue(blockers, "non_finite_vertices", "Mesh contains NaN or infinite vertices.")
    if degenerate_faces:
        issue(blockers, "degenerate_faces", "Mesh contains zero-area faces.")
    if invalid_face_normals:
        issue(blockers, "invalid_face_normals", "Mesh contains invalid face normals.")
    if loose_vertices or loose_edges or zero_length_edges:
        issue(warnings, "loose_geometry", "Mesh contains loose or zero-length geometry.")
    if boundary_edges or wire_edges or non_manifold_edges:
        issue(
            warnings,
            "non_manifold_geometry",
            "Mesh contains boundary, wire, or non-manifold edges.",
        )
    if not uv_layers:
        issue(warnings, "missing_uv", "Mesh does not contain a UV map.")
    if not any(item["name"] for item in materials):
        issue(warnings, "missing_material", "Mesh has no assigned material.")
    if any(not math.isfinite(value) or abs(value) <= 1e-9 for value in scale):
        issue(blockers, "invalid_scale", "Object scale contains zero, NaN, or infinity.")
    elif any(value < 0 for value in scale):
        issue(warnings, "negative_scale", "Object has a negative scale component.")

    if requirements.get("require_uv") and not uv_layers:
        issue(blockers, "required_uv_missing", "A UV map is required.")
    if requirements.get("require_material") and not any(
        item["name"] for item in materials
    ):
        issue(blockers, "required_material_missing", "A material is required.")
    if requirements.get("require_manifold") and (
        boundary_edges or wire_edges or non_manifold_edges
    ):
        issue(blockers, "manifold_required", "A closed manifold mesh is required.")
    if requirements.get("require_applied_scale") and any(
        not math.isclose(value, 1.0, abs_tol=1e-6) for value in scale
    ):
        issue(blockers, "applied_scale_required", "Object scale must be applied.")
    maximum_triangles = requirements.get("max_triangles")
    if maximum_triangles is not None and triangle_count > int(maximum_triangles):
        issue(
            blockers,
            "triangle_budget_exceeded",
            f"Mesh has {triangle_count} triangles; maximum is {maximum_triangles}.",
        )

    score = max(0, 100 - 25 * len(blockers) - 6 * len(warnings))
    return {
        "object": {
            "name": obj.name,
            "type": obj.type,
            "data_name": mesh.name,
            "location": _vector_payload(obj.location),
            "rotation_degrees": [
                math.degrees(float(value)) for value in obj.rotation_euler
            ],
            "scale": scale,
            "dimensions": _vector_payload(obj.dimensions),
            "matrix_world": _matrix_payload(obj.matrix_world),
            "parent": obj.parent.name if obj.parent is not None else None,
            "collections": sorted(collection.name for collection in obj.users_collection),
        },
        "fingerprint": _mesh_fingerprint(obj),
        "geometry": {
            "vertices": len(vertices),
            "edges": len(edges),
            "polygons": len(polygons),
            "triangles": triangle_count,
            "ngons": sum(1 for polygon in polygons if len(polygon.vertices) > 4),
            "loose_vertices": loose_vertices,
            "loose_edges": loose_edges,
            "zero_length_edges": zero_length_edges,
            "boundary_edges": boundary_edges,
            "wire_edges": wire_edges,
            "non_manifold_edges": non_manifold_edges,
            "degenerate_faces": degenerate_faces,
            "invalid_face_normals": invalid_face_normals,
            "non_finite_vertices": non_finite_vertices,
        },
        "bounds": _bounds_payload(obj),
        "materials": materials,
        "uv_layers": uv_layers,
        "modifiers": modifiers,
        "quality": {
            "passed": not blockers,
            "score": score,
            "blockers": blockers,
            "warnings": warnings,
            "requirements": requirements,
        },
    }


def _scene_inspect(_config: BridgeConfig, args: dict[str, Any]) -> dict[str, Any]:
    _require_blender()
    scene = bpy.context.scene
    include_hidden = bool(args.get("include_hidden", False))
    maximum = int(args.get("max_objects", 500))
    objects = []
    for obj in sorted(scene.objects, key=lambda item: item.name):
        hidden = bool(obj.hide_viewport)
        try:
            hidden = hidden or bool(obj.hide_get())
        except RuntimeError:
            pass
        if hidden and not include_hidden:
            continue
        payload = {
            "id": f"OBJECT:{obj.name}",
            "name": obj.name,
            "type": obj.type,
            "data_name": getattr(getattr(obj, "data", None), "name", None),
            "location": _vector_payload(obj.location),
            "rotation_degrees": [
                math.degrees(float(value)) for value in obj.rotation_euler
            ],
            "scale": _vector_payload(obj.scale),
            "dimensions": _vector_payload(obj.dimensions),
            "parent": obj.parent.name if obj.parent is not None else None,
            "collections": sorted(collection.name for collection in obj.users_collection),
            "hidden": hidden,
            "selected": bool(obj.select_get()),
        }
        if obj.type == "MESH":
            payload["fingerprint"] = _mesh_fingerprint(obj)
        objects.append(payload)
        if len(objects) >= maximum:
            break
    return {
        "scene": scene.name,
        "active_object": (
            bpy.context.view_layer.objects.active.name
            if bpy.context.view_layer.objects.active is not None
            else None
        ),
        "object_count": len(scene.objects),
        "returned_object_count": len(objects),
        "truncated": len(objects) < sum(
            1
            for obj in scene.objects
            if include_hidden or not bool(obj.hide_viewport)
        ),
        "collections": sorted(collection.name for collection in bpy.data.collections),
        "materials": sorted(material.name for material in bpy.data.materials),
        "objects": objects,
    }


def _mesh_inspect(_config: BridgeConfig, args: dict[str, Any]) -> dict[str, Any]:
    _require_blender()
    obj = _exact_object(args["object_name"], "MESH")
    return _mesh_report(obj, args.get("requirements"))


def _uv_inspect(_config: BridgeConfig, args: dict[str, Any]) -> dict[str, Any]:
    _require_blender()
    obj = _exact_object(args["object_name"], "MESH")
    report = _mesh_report(obj)
    layer_name = args.get("uv_layer_name")
    layers = report["uv_layers"]
    if layer_name:
        layers = [layer for layer in layers if layer["name"] == layer_name]
        if not layers:
            raise BridgeError(
                "uv_layer_not_found",
                f'Exact UV layer "{layer_name}" was not found.',
                {"available": [layer["name"] for layer in report["uv_layers"]]},
            )
    return {
        "object_name": obj.name,
        "fingerprint": report["fingerprint"],
        "geometry": report["geometry"],
        "uv_layers": layers,
        "quality": {
            "passed": bool(layers)
            and all(
                layer["non_finite"] == 0 and layer["degenerate_faces"] == 0
                for layer in layers
            ),
            "issues": [
                {
                    "code": "missing_uv",
                    "message": "Mesh does not contain the requested UV layer.",
                }
            ]
            if not layers
            else [
                {
                    "code": "uv_diagnostics",
                    "message": (
                        f'UV layer "{layer["name"]}" contains non-finite or '
                        "degenerate coordinates."
                    ),
                }
                for layer in layers
                if layer["non_finite"] or layer["degenerate_faces"]
            ],
        },
    }


def _snapshot_ids() -> dict[str, set[int]]:
    return {
        name: {item.as_pointer() for item in getattr(bpy.data, name, ())}
        for name in _ID_COLLECTIONS
    }


def _rollback_ids(snapshot: dict[str, set[int]]) -> None:
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


def _selection_state() -> tuple[list[str], str | None, str]:
    selected = [
        str(item.name)
        for item in bpy.context.scene.objects
        if item.select_get()
    ]
    active = bpy.context.view_layer.objects.active
    mode = str(getattr(active, "mode", "OBJECT")) if active is not None else "OBJECT"
    return selected, str(active.name) if active is not None else None, mode


def _object_mode() -> None:
    active = bpy.context.view_layer.objects.active
    if active is not None and getattr(active, "mode", "OBJECT") != "OBJECT":
        try:
            bpy.ops.object.mode_set(mode="OBJECT")
        except RuntimeError:
            pass


def _activate_object(obj: Any) -> None:
    _object_mode()
    for item in bpy.context.scene.objects:
        try:
            item.select_set(False)
        except RuntimeError:
            pass
    obj.hide_set(False)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj


def _restore_selection(state: tuple[list[str], str | None, str]) -> None:
    selected, active, mode = state
    _object_mode()
    for item in bpy.context.scene.objects:
        try:
            item.select_set(False)
        except RuntimeError:
            pass
    for name in selected:
        item = bpy.data.objects.get(name)
        if item is not None:
            try:
                item.select_set(True)
            except RuntimeError:
                pass
    active_object = bpy.data.objects.get(active) if active is not None else None
    if active_object is not None:
        bpy.context.view_layer.objects.active = active_object
        target_mode = "EDIT" if mode.startswith("EDIT") else mode
        if target_mode in {"EDIT", "POSE", "SCULPT", "VERTEX_PAINT", "WEIGHT_PAINT"}:
            try:
                bpy.ops.object.mode_set(mode=target_mode)
            except (RuntimeError, TypeError):
                pass


def _supported_operator_options(operator: Any, requested: dict[str, Any]) -> dict[str, Any]:
    properties = {
        item.identifier
        for item in operator.get_rna_type().properties
        if item.identifier
    }
    return {key: value for key, value in requested.items() if key in properties}


def _import_operator(source: Any) -> tuple[Any, dict[str, Any]]:
    suffix = source.suffix.lower()
    if suffix == ".fbx":
        operator = getattr(getattr(bpy.ops, "import_scene", None), "fbx", None)
        options = {"filepath": str(source), "use_anim": False}
    elif suffix == ".obj":
        operator = getattr(getattr(bpy.ops, "wm", None), "obj_import", None)
        if operator is None:
            operator = getattr(getattr(bpy.ops, "import_scene", None), "obj", None)
        options = {"filepath": str(source)}
    else:
        operator = getattr(getattr(bpy.ops, "import_scene", None), "gltf", None)
        options = {"filepath": str(source)}
    if operator is None:
        raise BridgeError(
            "model_import_unavailable",
            f"Blender does not provide an importer for {suffix}.",
        )
    return operator, _supported_operator_options(operator, options)


def _model_import(config: BridgeConfig, args: dict[str, Any]) -> dict[str, Any]:
    _require_blender()
    source = resolve_exchange_path_any(
        config,
        args["path"],
        suffixes=MODEL_IMPORT_EXTENSIONS,
        must_exist=True,
    )
    prefix = args["name_prefix"]
    snapshot = _snapshot_ids()
    selection = _selection_state()
    try:
        operator, options = _import_operator(source)
        result = operator(**options)
        if "FINISHED" not in result:
            raise BridgeError(
                "model_import_failed",
                f"Blender importer returned {sorted(result)}.",
            )
        imported = [
            item
            for item in bpy.data.objects
            if item.as_pointer() not in snapshot["objects"]
        ]
        meshes = [item for item in imported if item.type == "MESH"]
        if not meshes:
            raise BridgeError(
                "model_import_failed",
                "The imported file did not create a mesh object.",
            )

        planned: list[tuple[Any, str]] = []
        planned_names: set[str] = set()
        for obj in imported:
            candidate = f"{prefix}_{obj.name}"[:128]
            if candidate in planned_names:
                raise BridgeError(
                    "name_collision",
                    f'Import prefix produces duplicate object name "{candidate}".',
                )
            existing = bpy.data.objects.get(candidate)
            if existing is not None and existing not in imported:
                raise BridgeError(
                    "name_collision",
                    f'Output object already exists: "{candidate}".',
                )
            planned.append((obj, candidate))
            planned_names.add(candidate)
        temporary_prefix = f"__RevvyImport_{secrets.token_hex(8)}"
        for index, (obj, _candidate) in enumerate(planned):
            obj.name = f"{temporary_prefix}_{index}"
        for obj, candidate in planned:
            obj.name = candidate
            if obj.type == "MESH" and obj.data is not None:
                obj.data.name = f"{candidate}_Mesh"[:128]
        return {
            "source_path": str(source),
            "format": source.suffix.lower().lstrip("."),
            "name_prefix": prefix,
            "objects": [
                {
                    "name": obj.name,
                    "type": obj.type,
                    "data_name": getattr(getattr(obj, "data", None), "name", None),
                }
                for obj in sorted(imported, key=lambda item: item.name)
            ],
            "mesh_reports": [
                _mesh_report(obj)
                for obj in sorted(meshes, key=lambda item: item.name)
            ],
        }
    except Exception:
        _rollback_ids(snapshot)
        raise
    finally:
        _restore_selection(selection)


_UNSET = object()


def _duplicate_mesh(source: Any, output_name: str):
    if bpy.data.objects.get(output_name) is not None:
        raise BridgeError(
            "name_collision",
            f'Output object already exists: "{output_name}".',
        )
    data_name = f"{output_name}_Mesh"[:128]
    if bpy.data.meshes.get(data_name) is not None:
        raise BridgeError(
            "name_collision",
            f'Output mesh data already exists: "{data_name}".',
        )
    duplicate = None
    duplicate_mesh = None
    try:
        duplicate = source.copy()
        duplicate_mesh = source.data.copy()
        duplicate.data = duplicate_mesh
        duplicate.name = output_name
        duplicate_mesh.name = data_name
        if duplicate.name != output_name or duplicate_mesh.name != data_name:
            raise BridgeError(
                "name_collision",
                "Blender could not reserve the exact output object and mesh names.",
                {
                    "requested_object_name": output_name,
                    "actual_object_name": duplicate.name,
                    "requested_mesh_name": data_name,
                    "actual_mesh_name": duplicate_mesh.name,
                },
            )
        collections = list(source.users_collection)
        target_collection = (
            collections[0] if collections else bpy.context.scene.collection
        )
        target_collection.objects.link(duplicate)
        return duplicate
    except Exception:
        if duplicate is not None:
            _remove_duplicate(duplicate, duplicate_mesh)
        elif duplicate_mesh is not None:
            _remove_unused_mesh(duplicate_mesh)
        raise


def _remove_unused_mesh(mesh: Any) -> None:
    """Best-effort rollback helper that must never mask the primary failure."""
    try:
        if mesh is not None and getattr(mesh, "users", 0) == 0:
            bpy.data.meshes.remove(mesh)
    except Exception:
        # Rollback is already handling another exception. Blender add-ons and
        # test doubles can surface different Exception subclasses here.
        pass


def _remove_duplicate(obj: Any, mesh: Any = _UNSET) -> None:
    """Remove a staged duplicate without allowing cleanup errors to escape."""
    if mesh is _UNSET:
        try:
            mesh = getattr(obj, "data", None)
        except Exception:
            mesh = None
    try:
        bpy.data.objects.remove(obj, do_unlink=True)
    except Exception:
        pass
    _remove_unused_mesh(mesh)


def _vertex_transform_matrix(operation: dict[str, Any]) -> Any:
    translation = Vector(operation.get("translation", (0.0, 0.0, 0.0)))
    rotation = Euler(
        tuple(
            math.radians(float(value))
            for value in operation.get("rotation_degrees", (0.0, 0.0, 0.0))
        ),
        "XYZ",
    ).to_matrix().to_4x4()
    scale = operation.get("scale", (1.0, 1.0, 1.0))
    scale_matrix = Matrix.Diagonal(
        (float(scale[0]), float(scale[1]), float(scale[2]), 1.0)
    )
    return Matrix.Translation(translation) @ rotation @ scale_matrix


def _apply_mesh_operation(obj: Any, operation: dict[str, Any]) -> dict[str, Any]:
    operation_type = operation["type"]
    if operation_type == "transform_object":
        if "location" in operation:
            obj.location = operation["location"]
        if "rotation_degrees" in operation:
            obj.rotation_euler = [
                math.radians(float(value))
                for value in operation["rotation_degrees"]
            ]
        if "scale" in operation:
            obj.scale = operation["scale"]
        return {"type": operation_type}

    edit_mesh = bmesh.new()
    try:
        edit_mesh.from_mesh(obj.data)
        if operation_type == "transform_vertices":
            bmesh.ops.transform(
                edit_mesh,
                matrix=_vertex_transform_matrix(operation),
                verts=list(edit_mesh.verts),
            )
        elif operation_type == "merge_by_distance":
            bmesh.ops.remove_doubles(
                edit_mesh,
                verts=list(edit_mesh.verts),
                dist=float(operation.get("distance", 0.0001)),
            )
        elif operation_type == "recalculate_normals":
            bmesh.ops.recalc_face_normals(edit_mesh, faces=list(edit_mesh.faces))
        elif operation_type == "triangulate":
            bmesh.ops.triangulate(edit_mesh, faces=list(edit_mesh.faces))
        elif operation_type == "delete_loose":
            loose_edges = [edge for edge in edit_mesh.edges if not edge.link_faces]
            if loose_edges:
                bmesh.ops.delete(edit_mesh, geom=loose_edges, context="EDGES")
            loose_vertices = [
                vertex
                for vertex in edit_mesh.verts
                if not vertex.link_edges and not vertex.link_faces
            ]
            if loose_vertices:
                bmesh.ops.delete(edit_mesh, geom=loose_vertices, context="VERTS")
        elif operation_type == "shade_smooth":
            smooth = bool(operation.get("value", True))
            for face in edit_mesh.faces:
                face.smooth = smooth
        else:  # Defense in depth; common.validate_command rejects this first.
            raise BridgeError(
                "invalid_arguments",
                f"Unsupported mesh edit operation: {operation_type}",
            )
        edit_mesh.normal_update()
        edit_mesh.to_mesh(obj.data)
        obj.data.update()
    finally:
        edit_mesh.free()
    return {"type": operation_type}


def _mesh_edit(_config: BridgeConfig, args: dict[str, Any]) -> dict[str, Any]:
    _require_blender()
    source = _exact_object(args["source_object_name"], "MESH")
    source_fingerprint = _mesh_fingerprint(source)
    output = _duplicate_mesh(source, args["output_object_name"])
    try:
        applied = [
            _apply_mesh_operation(output, operation)
            for operation in args["operations"]
        ]
        if _mesh_fingerprint(source) != source_fingerprint:
            raise BridgeError(
                "source_changed",
                "The source mesh changed during a non-destructive edit.",
            )
        return {
            "source_object_name": source.name,
            "source_fingerprint": source_fingerprint,
            "output_object_name": output.name,
            "operations": applied,
            "output": _mesh_report(output),
            "source_preserved": True,
        }
    except Exception:
        _remove_duplicate(output)
        raise


def _configure_modifier(modifier: Any, spec: dict[str, Any]) -> None:
    modifier_type = spec["type"]
    if modifier_type == "bevel":
        modifier.width = float(spec.get("width", 0.01))
        modifier.segments = int(spec.get("segments", 2))
        if hasattr(modifier, "limit_method"):
            modifier.limit_method = "ANGLE"
        if hasattr(modifier, "angle_limit"):
            modifier.angle_limit = math.radians(float(spec.get("angle_degrees", 30.0)))
    elif modifier_type == "subdivision":
        modifier.levels = int(spec.get("levels", 1))
        modifier.render_levels = int(spec.get("render_levels", modifier.levels))
        if hasattr(modifier, "subdivision_type"):
            modifier.subdivision_type = "CATMULL_CLARK"
    elif modifier_type == "solidify":
        modifier.thickness = float(spec.get("thickness", 0.01))
        modifier.offset = float(spec.get("offset", 0.0))
    elif modifier_type == "decimate":
        modifier.decimate_type = "COLLAPSE"
        modifier.ratio = float(spec.get("ratio", 0.5))
    elif modifier_type == "mirror":
        axes = spec.get("axes", [True, False, False])
        modifier.use_axis = tuple(bool(value) for value in axes)
        modifier.use_clip = bool(spec.get("use_clip", True))
    elif modifier_type == "triangulate":
        if hasattr(modifier, "keep_custom_normals"):
            modifier.keep_custom_normals = True


_MODIFIER_TYPES = {
    "bevel": "BEVEL",
    "subdivision": "SUBSURF",
    "solidify": "SOLIDIFY",
    "decimate": "DECIMATE",
    "mirror": "MIRROR",
    "triangulate": "TRIANGULATE",
}


def _modifier_apply(_config: BridgeConfig, args: dict[str, Any]) -> dict[str, Any]:
    _require_blender()
    source = _exact_object(args["source_object_name"], "MESH")
    source_fingerprint = _mesh_fingerprint(source)
    output = _duplicate_mesh(source, args["output_object_name"])
    selection = _selection_state()
    try:
        _activate_object(output)
        spec = args["modifier"]
        modifier_type = _MODIFIER_TYPES[spec["type"]]
        modifier = output.modifiers.new(
            name=f"Revvy_{spec['type'].title()}",
            type=modifier_type,
        )
        _configure_modifier(modifier, spec)
        result = bpy.ops.object.modifier_apply(modifier=modifier.name)
        if "FINISHED" not in result:
            raise BridgeError(
                "modifier_apply_failed",
                f"Blender modifier apply returned {sorted(result)}.",
            )
        if _mesh_fingerprint(source) != source_fingerprint:
            raise BridgeError(
                "source_changed",
                "The source mesh changed while applying a modifier copy.",
            )
        return {
            "source_object_name": source.name,
            "source_fingerprint": source_fingerprint,
            "output_object_name": output.name,
            "modifier": spec,
            "output": _mesh_report(output),
            "source_preserved": True,
        }
    except Exception:
        _remove_duplicate(output)
        raise
    finally:
        _restore_selection(selection)


def _remove_unused_material(material: Any) -> None:
    """Best-effort rollback helper that must never mask the primary failure."""
    try:
        if material is not None and getattr(material, "users", 0) == 0:
            bpy.data.materials.remove(material)
    except Exception:
        # Preserve the configuration/assignment error that initiated rollback.
        pass


def _create_material(spec: dict[str, Any]):
    material = None
    try:
        material = bpy.data.materials.new(spec["material_name"])
        material.diffuse_color = tuple(
            spec.get("base_color", (0.8, 0.8, 0.8, 1.0))
        )
        material.metallic = float(spec.get("metallic", 0.0))
        material.roughness = float(spec.get("roughness", 0.5))
        material.use_nodes = True
        tree = material.node_tree
        principled = next(
            (
                node
                for node in tree.nodes
                if node.type == "BSDF_PRINCIPLED"
            ),
            None,
        )
        if principled is not None:
            values = {
                "Base Color": tuple(
                    spec.get("base_color", (0.8, 0.8, 0.8, 1.0))
                ),
                "Metallic": float(spec.get("metallic", 0.0)),
                "Roughness": float(spec.get("roughness", 0.5)),
                "Alpha": float(
                    spec.get("base_color", (0.8, 0.8, 0.8, 1.0))[3]
                ),
            }
            for name, value in values.items():
                socket = principled.inputs.get(name)
                if socket is not None:
                    socket.default_value = value
        return material
    except Exception:
        _remove_unused_material(material)
        raise


def _material_assign(_config: BridgeConfig, args: dict[str, Any]) -> dict[str, Any]:
    _require_blender()
    source = _exact_object(args["source_object_name"], "MESH")
    source_fingerprint = _mesh_fingerprint(source)
    output = _duplicate_mesh(source, args["output_object_name"])
    material = None
    created_material = False
    try:
        material = bpy.data.materials.get(args["material_name"])
        if args.get("create_material", False):
            if material is not None:
                raise BridgeError(
                    "material_already_exists",
                    f'Creation requested but material already exists: "{material.name}".',
                )
            material = _create_material(args)
            created_material = True
        elif material is None:
            raise BridgeError(
                "material_not_found",
                f'Exact material "{args["material_name"]}" was not found.',
                {"available": sorted(item.name for item in bpy.data.materials)[:200]},
            )

        replace_slot = args.get("replace_slot")
        if replace_slot is not None:
            slot_index = int(replace_slot)
            if slot_index >= len(output.data.materials):
                raise BridgeError(
                    "material_slot_not_found",
                    f"Material slot {slot_index} does not exist.",
                    {"slot_count": len(output.data.materials)},
                )
            output.data.materials[slot_index] = material
        else:
            output.data.materials.append(material)
            slot_index = len(output.data.materials) - 1
            for polygon in output.data.polygons:
                polygon.material_index = slot_index
        output.data.update()
        if _mesh_fingerprint(source) != source_fingerprint:
            raise BridgeError(
                "source_changed",
                "The source mesh changed during material assignment.",
            )
        return {
            "source_object_name": source.name,
            "source_fingerprint": source_fingerprint,
            "output_object_name": output.name,
            "material_name": material.name,
            "material_created": created_material,
            "slot": slot_index,
            "output": _mesh_report(output),
            "source_preserved": True,
        }
    except Exception:
        _remove_duplicate(output)
        if created_material:
            _remove_unused_material(material)
        raise


def _ensure_uv_layer(obj: Any, args: dict[str, Any]):
    requested = args.get("uv_layer_name")
    if requested:
        layer = obj.data.uv_layers.get(requested)
        if layer is None and args.get("create_layer", False):
            layer = obj.data.uv_layers.new(name=requested)
        if layer is None:
            raise BridgeError(
                "uv_layer_not_found",
                f'Exact UV layer "{requested}" was not found.',
                {"available": [item.name for item in obj.data.uv_layers]},
            )
    else:
        layer = obj.data.uv_layers.active
        if layer is None:
            layer = obj.data.uv_layers.new(name="UVMap")
    obj.data.uv_layers.active = layer
    return layer


def _uv_edit(_config: BridgeConfig, args: dict[str, Any]) -> dict[str, Any]:
    _require_blender()
    source = _exact_object(args["source_object_name"], "MESH")
    source_fingerprint = _mesh_fingerprint(source)
    output = _duplicate_mesh(source, args["output_object_name"])
    selection = _selection_state()
    try:
        layer = _ensure_uv_layer(output, args)
        layer_name = str(layer.name)
        before_uv_fingerprint = _uv_fingerprint(layer)
        _activate_object(output)
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        operation = args["operation"]
        margin = float(args.get("island_margin", 0.02))
        if operation == "smart_project":
            operator = bpy.ops.uv.smart_project
            options = _supported_operator_options(
                operator,
                {
                    "angle_limit": math.radians(
                        float(args.get("angle_limit_degrees", 66.0))
                    ),
                    "island_margin": margin,
                },
            )
        elif operation == "cube_project":
            operator = bpy.ops.uv.cube_project
            options = _supported_operator_options(
                operator,
                {"cube_size": float(args.get("cube_size", 1.0))},
            )
        elif operation == "unwrap":
            operator = bpy.ops.uv.unwrap
            options = _supported_operator_options(
                operator,
                {"method": "ANGLE_BASED", "margin": margin},
            )
        else:
            if not output.data.uv_layers:
                raise BridgeError(
                    "uv_layer_not_found",
                    "pack_islands requires an existing UV layer.",
                )
            operator = bpy.ops.uv.pack_islands
            options = _supported_operator_options(
                operator,
                {"margin": margin, "rotate": True, "scale": True},
            )
        result = operator(**options)
        if "FINISHED" not in result:
            raise BridgeError(
                "uv_edit_failed",
                f"Blender UV operator returned {sorted(result)}.",
            )
        bpy.ops.object.mode_set(mode="OBJECT")
        output.data.update()
        layer = output.data.uv_layers.get(layer_name)
        if layer is None:
            raise BridgeError(
                "uv_layer_not_found",
                f'UV operation removed the requested layer "{layer_name}".',
            )
        after_uv_fingerprint = _uv_fingerprint(layer)
        uv_changed = before_uv_fingerprint != after_uv_fingerprint
        layer_report = _uv_layer_payload(output.data, layer)
        if operation != "pack_islands" and not uv_changed:
            raise BridgeError(
                "uv_edit_no_change",
                (
                    f"{operation} did not change the requested UV layer. "
                    "The mesh may require seams or a different typed projection."
                ),
            )
        if layer_report["non_finite"] or layer_report["degenerate_faces"]:
            raise BridgeError(
                "uv_validation_failed",
                "The UV operation produced non-finite or degenerate UV faces.",
                {"uv_layer": layer_report},
            )
        if _mesh_fingerprint(source) != source_fingerprint:
            raise BridgeError(
                "source_changed",
                "The source mesh changed during UV editing.",
            )
        return {
            "source_object_name": source.name,
            "source_fingerprint": source_fingerprint,
            "output_object_name": output.name,
            "operation": operation,
            "uv_layer_name": layer_name,
            "uv_changed": uv_changed,
            "uv_layer": layer_report,
            "output": _mesh_report(output),
            "source_preserved": True,
        }
    except Exception:
        _object_mode()
        _remove_duplicate(output)
        raise
    finally:
        _restore_selection(selection)


def _export_operator(destination: Any, temporary: Any, args: dict[str, Any]):
    suffix = destination.suffix.lower()
    apply_modifiers = bool(args.get("apply_modifiers", True))
    if suffix == ".fbx":
        operator = getattr(getattr(bpy.ops, "export_scene", None), "fbx", None)
        options = {
            "filepath": str(temporary),
            "use_selection": True,
            "object_types": {"MESH"},
            "use_mesh_modifiers": apply_modifiers,
            "bake_anim": False,
            "add_leaf_bones": False,
        }
    elif suffix == ".obj":
        operator = getattr(getattr(bpy.ops, "wm", None), "obj_export", None)
        if operator is None:
            operator = getattr(getattr(bpy.ops, "export_scene", None), "obj", None)
        options = {
            "filepath": str(temporary),
            "export_selected_objects": True,
            "use_selection": True,
            "apply_modifiers": apply_modifiers,
            "export_materials": False,
            "use_materials": False,
        }
    else:
        operator = getattr(getattr(bpy.ops, "export_scene", None), "gltf", None)
        options = {
            "filepath": str(temporary),
            "export_format": "GLB",
            "use_selection": True,
            "export_apply": apply_modifiers,
            "export_animations": False,
        }
    if operator is None:
        raise BridgeError(
            "model_export_unavailable",
            f"Blender does not provide an exporter for {suffix}.",
        )
    supported = _supported_operator_options(operator, options)
    if "filepath" not in supported:
        raise BridgeError(
            "model_export_incompatible",
            f"The Blender {suffix} exporter does not expose filepath.",
        )
    selection_keys = (
        {"use_selection"}
        if suffix in {".fbx", ".glb"}
        else {"export_selected_objects", "use_selection"}
    )
    if not any(key in supported for key in selection_keys):
        raise BridgeError(
            "model_export_incompatible",
            f"The Blender {suffix} exporter cannot guarantee selected-only export.",
        )
    if suffix == ".fbx" and "bake_anim" not in supported:
        raise BridgeError(
            "model_export_incompatible",
            "The Blender FBX exporter cannot guarantee animation-free model export.",
        )
    if suffix == ".glb" and supported.get("export_format") != "GLB":
        raise BridgeError(
            "model_export_incompatible",
            "The Blender glTF exporter cannot guarantee a single-file GLB export.",
        )
    return operator, supported


def _commit_model_export(
    temporary: Any,
    destination: Any,
    *,
    overwrite: bool,
) -> None:
    """Commit a complete staged export without racing ``overwrite=False``."""
    if overwrite:
        try:
            os.replace(temporary, destination)
        except OSError as exc:
            raise BridgeError(
                "model_export_failed",
                f"Could not commit the completed model export: {destination}",
            ) from exc
        return

    try:
        # The staging file is created beside the destination, so a hard link is
        # an atomic create-if-absent commit on supported filesystems. Unlike an
        # exists()+replace sequence, it cannot clobber a file created by a
        # competing process between the check and commit.
        os.link(temporary, destination)
    except FileExistsError as exc:
        raise BridgeError(
            "file_already_exists",
            f"Output appeared during export and was not replaced: {destination}",
        ) from exc
    except OSError as exc:
        raise BridgeError(
            "model_export_failed",
            (
                "Could not atomically commit the completed model export "
                f"without replacement: {destination}"
            ),
        ) from exc
    try:
        pathlib_temporary = os.fspath(temporary)
        os.unlink(pathlib_temporary)
    except OSError:
        # The complete destination is already committed. A hidden randomized
        # staging file is safe to leave for later cleanup rather than turning a
        # successful, unambiguous export into an apparent failed mutation.
        pass


def _model_export(config: BridgeConfig, args: dict[str, Any]) -> dict[str, Any]:
    _require_blender()
    destination = resolve_exchange_path_any(
        config,
        args["path"],
        suffixes=MODEL_EXPORT_EXTENSIONS,
        must_exist=False,
    )
    if destination.exists() and not args.get("overwrite", False):
        raise BridgeError(
            "file_already_exists",
            f"Output already exists: {destination}",
        )
    objects = [
        _exact_object(name, "MESH")
        for name in args["object_names"]
    ]
    selection = _selection_state()
    temporary = destination.with_name(
        f".{destination.stem}.{secrets.token_hex(8)}.tmp{destination.suffix.lower()}"
    )
    try:
        _object_mode()
        for item in bpy.context.scene.objects:
            item.select_set(False)
        for obj in objects:
            obj.hide_set(False)
            obj.select_set(True)
        bpy.context.view_layer.objects.active = objects[0]
        operator, options = _export_operator(destination, temporary, args)
        result = operator(**options)
        if "FINISHED" not in result:
            raise BridgeError(
                "model_export_failed",
                f"Blender exporter returned {sorted(result)}.",
            )
    except Exception:
        temporary.unlink(missing_ok=True)
        raise
    finally:
        _restore_selection(selection)
    if not temporary.is_file() or temporary.stat().st_size <= 0:
        temporary.unlink(missing_ok=True)
        raise BridgeError(
            "model_export_failed",
            "Blender exporter did not create a non-empty file.",
        )
    try:
        _commit_model_export(
            temporary,
            destination,
            overwrite=bool(args.get("overwrite", False)),
        )
    except Exception:
        temporary.unlink(missing_ok=True)
        raise
    return {
        "path": str(destination),
        "format": destination.suffix.lower().lstrip("."),
        "bytes": destination.stat().st_size,
        "object_names": [obj.name for obj in objects],
        "fingerprints": {
            obj.name: _mesh_fingerprint(obj)
            for obj in objects
        },
        "apply_modifiers": bool(args.get("apply_modifiers", True)),
    }


MODELING_COMMAND_HANDLERS = {
    "scene.inspect": _scene_inspect,
    "model.import": _model_import,
    "mesh.inspect": _mesh_inspect,
    "mesh.edit": _mesh_edit,
    "modifier.apply": _modifier_apply,
    "material.assign": _material_assign,
    "uv.inspect": _uv_inspect,
    "uv.edit": _uv_edit,
    "model.export": _model_export,
}
