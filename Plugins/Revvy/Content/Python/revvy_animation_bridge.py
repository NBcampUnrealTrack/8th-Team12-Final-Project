"""Safe Unreal animation FBX exchange helpers for Revvy.

The module is intentionally importable outside Unreal. Public operations import
``unreal`` lazily and return JSON-serializable dictionaries, which makes them
usable from ``execute_python_code`` and straightforward to test offline.

Typical Unreal usage::

    import revvy_animation_bridge as bridge
    result = bridge.export_animation_job("/Game/Characters/Animations/AN_Walk")
    # Edit result["paths"]["source_fbx"] in Blender and export to
    # result["paths"]["blender_output_fbx"].
    imported = bridge.import_animation_job(result["job_id"])

Source assets are never reimported or overwritten. Every import targets a new,
isolated content folder and fails if its intended asset already exists.
"""

from __future__ import annotations

import datetime as _datetime
import hashlib
import json
import math
import os
import pathlib
import re
import threading
import uuid
from typing import Any, Optional


SCHEMA_VERSION = 1
TIMING_CONTRACT_ID = "revvy.unreal_anim_sequence_fbx_timing.v1"
TIMING_SOURCE_EXPORTER = "unreal.AnimSequenceExporterFBX"
TIMING_FRAME_COUNT_CONVENTION = "sample_intervals"
TIMING_TERMINAL_POLICY = "trim_one_verified_duplicate"
EXCHANGE_SUBDIR = ("Revvy", "BlenderExchange")
DEFAULT_CONTENT_ROOT = "/Game/Revvy/BlenderExchange"
_JOB_ID_RE = re.compile(r"^[A-Za-z0-9_]{1,64}$")
_ASSET_TOKEN_RE = re.compile(r"^[A-Za-z0-9_]+$")
_ASSET_NAME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9_]{0,127}$")
_ROOT_MOTION_PROPERTIES = (
    "enable_root_motion",
    "root_motion_root_lock",
    "force_root_lock",
    "use_normalized_root_motion_scale",
)
_DEFERRED_IMPORTS: dict[str, Any] = {}
_DEFERRED_IMPORTS_LOCK = threading.Lock()


class AnimationBridgeError(RuntimeError):
    """An expected bridge failure with a stable machine-readable code."""

    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code


def _utc_now() -> str:
    return (
        _datetime.datetime.now(_datetime.timezone.utc)
        .isoformat(timespec="seconds")
        .replace("+00:00", "Z")
    )


def _get_unreal(unreal_module=None):
    if unreal_module is not None:
        return unreal_module
    try:
        import unreal  # type: ignore
    except ImportError as exc:
        raise AnimationBridgeError(
            "unreal_unavailable",
            "This operation must run in Unreal Editor with PythonScriptPlugin enabled.",
        ) from exc
    return unreal


def _success(operation: str, **values: Any) -> dict[str, Any]:
    return {"success": True, "operation": operation, **values}


def _failure(operation: str, error: Exception, **values: Any) -> dict[str, Any]:
    code = getattr(error, "code", "unexpected_error")
    return {
        "success": False,
        "operation": operation,
        **values,
        "error": {"code": str(code), "message": str(error)},
    }


def _validate_job_id(job_id: Any) -> str:
    value = str(job_id or "").strip()
    if not _JOB_ID_RE.fullmatch(value):
        raise AnimationBridgeError(
            "invalid_job_id",
            "job_id must contain 1-64 ASCII letters, digits, or underscores.",
        )
    return value


def _new_job_id() -> str:
    stamp = _datetime.datetime.now(_datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    return f"{stamp}_{uuid.uuid4().hex[:10]}"


def _split_asset_path(path: str) -> tuple[str, Optional[str]]:
    tail = path.rsplit("/", 1)[-1]
    if "." not in tail:
        return path, None
    package_tail, object_name = tail.split(".", 1)
    package_path = path[: -len(tail)] + package_tail
    return package_path, object_name


def _validate_asset_path(path: Any, field: str = "asset_path") -> str:
    value = str(path or "").strip()
    if (
        not value.startswith("/")
        or value.endswith("/")
        or "\\" in value
        or ":" in value
        or "'" in value
        or '"' in value
    ):
        raise AnimationBridgeError(
            "invalid_asset_path",
            f"{field} must be an exact Unreal object or package path.",
        )
    segments = value[1:].split("/")
    if len(segments) < 2 or any(not segment for segment in segments):
        raise AnimationBridgeError(
            "invalid_asset_path",
            f"{field} must include a mount point, folder, and asset name.",
        )
    tail = segments[-1]
    if tail.count(".") > 1:
        raise AnimationBridgeError("invalid_asset_path", f"{field} has an invalid object suffix.")
    if "." in tail:
        package_name, object_name = tail.split(".", 1)
        if package_name != object_name:
            raise AnimationBridgeError(
                "invalid_asset_path",
                f"{field} object name must exactly match its package name.",
            )
        segments[-1] = package_name
    if any(not _ASSET_TOKEN_RE.fullmatch(segment) for segment in segments):
        raise AnimationBridgeError(
            "invalid_asset_path",
            f"{field} contains characters Unreal asset paths do not support.",
        )
    return value


def _validate_destination_path(path: Any) -> str:
    value = str(path or "").strip().rstrip("/")
    if not value.startswith("/Game/") or "." in value or "\\" in value or ":" in value:
        raise AnimationBridgeError(
            "invalid_destination",
            "destination_path must be a writable /Game/... content folder.",
        )
    segments = value[1:].split("/")
    if any(not _ASSET_TOKEN_RE.fullmatch(segment) for segment in segments):
        raise AnimationBridgeError(
            "invalid_destination",
            "destination_path contains unsupported characters.",
        )
    return value


def _validate_destination_name(name: Any) -> str:
    value = str(name or "").strip()
    if not _ASSET_NAME_RE.fullmatch(value):
        raise AnimationBridgeError(
            "invalid_destination_name",
            "destination_name must start with a letter and contain only letters, digits, or underscores.",
        )
    return value


def _canonical_package_path(asset_path: str) -> str:
    return _split_asset_path(asset_path)[0]


def _object_path(obj: Any) -> str:
    getter = getattr(obj, "get_path_name", None)
    if callable(getter):
        value = str(getter())
        if value:
            return value
    value = _get_field(obj, "path_name", "asset_path")
    return str(value or "")


def _class_name(obj: Any) -> str:
    get_class = getattr(obj, "get_class", None)
    if callable(get_class):
        cls = get_class()
        get_name = getattr(cls, "get_name", None)
        if callable(get_name):
            return str(get_name())
    return type(obj).__name__


def _get_field(obj: Any, *names: str) -> Any:
    for name in names:
        try:
            return getattr(obj, name)
        except (AttributeError, TypeError):
            pass
        get_editor_property = getattr(obj, "get_editor_property", None)
        if callable(get_editor_property):
            try:
                return get_editor_property(name)
            except Exception:
                pass
    return None


def _set_editor_property(obj: Any, name: str, value: Any, *, required: bool = True) -> bool:
    setter = getattr(obj, "set_editor_property", None)
    if callable(setter):
        try:
            setter(name, value)
            return True
        except Exception:
            pass
    try:
        setattr(obj, name, value)
        return True
    except Exception as exc:
        if required:
            raise AnimationBridgeError(
                "unreal_api_mismatch",
                f"Unreal object {type(obj).__name__} does not expose '{name}'.",
            ) from exc
    return False


def _is_expected_type(ue: Any, obj: Any, class_name: str) -> bool:
    expected = getattr(ue, class_name, None)
    if expected is not None:
        try:
            return isinstance(obj, expected)
        except TypeError:
            pass
    return _class_name(obj) == class_name


def _asset_library(ue: Any) -> Any:
    library = getattr(ue, "EditorAssetLibrary", None)
    if library is None:
        raise AnimationBridgeError(
            "unreal_api_missing",
            "EditorAssetLibrary is unavailable. Enable EditorScriptingUtilities.",
        )
    return library


def _ensure_editor_not_playing(ue: Any) -> None:
    subsystem_type = getattr(ue, "LevelEditorSubsystem", None)
    get_subsystem = getattr(ue, "get_editor_subsystem", None)
    if subsystem_type is None or not callable(get_subsystem):
        return
    try:
        subsystem = get_subsystem(subsystem_type)
        is_playing = getattr(subsystem, "is_in_play_in_editor", None)
        if callable(is_playing) and bool(is_playing()):
            raise AnimationBridgeError(
                "editor_in_play_mode",
                "Stop Play In Editor or simulation before exporting or importing animation assets.",
            )
    except AnimationBridgeError:
        raise
    except Exception:
        return


def _load_exact_asset(ue: Any, supplied_path: Any, class_name: str) -> tuple[Any, str]:
    requested = _validate_asset_path(supplied_path)
    requested_package, requested_object = _split_asset_path(requested)
    library = _asset_library(ue)
    loaders = [
        loader
        for loader in (
            getattr(ue, "load_asset", None),
            getattr(library, "load_asset", None),
        )
        if callable(loader)
    ]
    if not loaders:
        raise AnimationBridgeError("unreal_api_missing", "No Unreal asset loader is available.")
    asset = None
    for lookup_path in dict.fromkeys((requested, requested_package)):
        for loader in loaders:
            try:
                asset = loader(lookup_path)
            except Exception:
                asset = None
            if asset is not None:
                break
        if asset is not None:
            break
    if asset is None:
        raise AnimationBridgeError("asset_not_found", f"Asset does not exist: {requested}")
    if not _is_expected_type(ue, asset, class_name):
        raise AnimationBridgeError(
            "wrong_asset_type",
            f"Expected {class_name}, got {_class_name(asset)} at {requested}.",
        )

    canonical = _validate_asset_path(_object_path(asset), "loaded_asset_path")
    canonical_package, _ = _split_asset_path(canonical)
    if requested_object is not None:
        matches = canonical == requested
    else:
        matches = canonical_package == requested_package
    if not matches:
        raise AnimationBridgeError(
            "asset_path_mismatch",
            f"Requested {requested}, but Unreal resolved {canonical}.",
        )
    return asset, canonical


def _asset_exists(ue: Any, supplied_path: str) -> bool:
    requested = _validate_asset_path(supplied_path)
    package_path, _ = _split_asset_path(requested)
    library = _asset_library(ue)
    exists = getattr(library, "does_asset_exist", None)
    if callable(exists):
        try:
            if bool(exists(package_path)):
                return True
        except Exception:
            pass
    for loader in (
        getattr(ue, "load_asset", None),
        getattr(library, "load_asset", None),
    ):
        if not callable(loader):
            continue
        for lookup_path in dict.fromkeys((requested, package_path)):
            try:
                if loader(lookup_path) is not None:
                    return True
            except Exception:
                continue
    return False


def _full_path(ue: Any, value: Any) -> pathlib.Path:
    raw = str(value)
    paths = getattr(ue, "Paths", None)
    converter = getattr(paths, "convert_relative_path_to_full", None) if paths else None
    if callable(converter):
        raw = str(converter(raw))
    return pathlib.Path(raw).expanduser().resolve()


def _exchange_root(ue: Any) -> pathlib.Path:
    paths = getattr(ue, "Paths", None)
    saved_dir = getattr(paths, "project_saved_dir", None) if paths else None
    if not callable(saved_dir):
        raise AnimationBridgeError("unreal_api_missing", "unreal.Paths.project_saved_dir is unavailable.")
    return _full_path(ue, saved_dir()).joinpath(*EXCHANGE_SUBDIR)


def _is_within(path: pathlib.Path, parent: pathlib.Path) -> bool:
    child_norm = os.path.normcase(str(path.resolve()))
    parent_norm = os.path.normcase(str(parent.resolve()))
    try:
        return os.path.commonpath((child_norm, parent_norm)) == parent_norm
    except ValueError:
        return False


def _atomic_write_json(path: pathlib.Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{uuid.uuid4().hex}.tmp")
    try:
        with temporary.open("x", encoding="utf-8", newline="\n") as handle:
            json.dump(value, handle, ensure_ascii=True, indent=2, sort_keys=True)
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass


def _read_json(path: pathlib.Path) -> dict[str, Any]:
    try:
        with path.open("r", encoding="utf-8") as handle:
            value = json.load(handle)
    except FileNotFoundError as exc:
        raise AnimationBridgeError("job_not_found", f"Job manifest not found: {path}") from exc
    except (OSError, json.JSONDecodeError) as exc:
        raise AnimationBridgeError("invalid_manifest", f"Cannot read job manifest: {path}") from exc
    if not isinstance(value, dict) or value.get("schema_version") != SCHEMA_VERSION:
        raise AnimationBridgeError("invalid_manifest", "Unsupported or malformed job manifest.")
    return value


def _job_paths(ue: Any, job_id: str) -> tuple[pathlib.Path, pathlib.Path]:
    root = _exchange_root(ue)
    directory = (root / _validate_job_id(job_id)).resolve()
    if not _is_within(directory, root):
        raise AnimationBridgeError("invalid_job_id", "Job path escapes the exchange root.")
    return directory, directory / "manifest.json"


def _read_job(ue: Any, job_id: Any) -> tuple[dict[str, Any], pathlib.Path, pathlib.Path]:
    value = _validate_job_id(job_id)
    directory, manifest_path = _job_paths(ue, value)
    manifest = _read_json(manifest_path)
    if manifest.get("job_id") != value:
        raise AnimationBridgeError("invalid_manifest", "Manifest job_id does not match its directory.")
    recorded_dir = pathlib.Path(str(manifest.get("paths", {}).get("job_dir", ""))).resolve()
    if recorded_dir != directory or not _is_within(recorded_dir, _exchange_root(ue)):
        raise AnimationBridgeError("invalid_manifest", "Manifest job directory is not trusted.")
    return manifest, directory, manifest_path


def _safe_number(value: Any, *, integer: bool = False) -> Optional[float | int]:
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    if not math.isfinite(number) or number < 0:
        return None
    return int(number) if integer else number


def _optional_call(owner: Any, name: str, *args: Any) -> Any:
    function = getattr(owner, name, None)
    if not callable(function):
        return None
    try:
        return function(*args)
    except Exception:
        return None


def _skeleton_from_animation(asset: Any) -> Any:
    skeleton = _get_field(asset, "skeleton")
    if skeleton is not None:
        return skeleton
    getter = getattr(asset, "get_skeleton", None)
    if callable(getter):
        try:
            return getter()
        except Exception:
            pass
    return None


def _bone_hierarchy(ue: Any, skeleton_path: str) -> Optional[list[dict[str, Any]]]:
    service = getattr(ue, "SkeletonService", None)
    bones = _optional_call(service, "list_bones", skeleton_path) if service else None
    if bones is None:
        return None
    result = []
    for bone in bones:
        bone_index = _get_field(bone, "bone_index")
        parent_index = _get_field(bone, "parent_bone_index")
        result.append(
            {
                "name": str(_get_field(bone, "bone_name") or ""),
                "parent": str(_get_field(bone, "parent_bone_name") or ""),
                "index": int(bone_index if bone_index is not None else 0),
                "parent_index": int(parent_index if parent_index is not None else -1),
            }
        )
    return result


def _inspect_loaded_animation(ue: Any, asset: Any, canonical_path: str) -> dict[str, Any]:
    skeleton = _skeleton_from_animation(asset)
    if skeleton is None or not _is_expected_type(ue, skeleton, "Skeleton"):
        raise AnimationBridgeError(
            "missing_skeleton",
            f"Animation has no valid existing Skeleton: {canonical_path}",
        )
    skeleton_path = _validate_asset_path(_object_path(skeleton), "skeleton_path")

    service = getattr(ue, "AnimSequenceService", None)
    animation_library = getattr(ue, "AnimationLibrary", None)

    duration = _optional_call(service, "get_animation_length", canonical_path) if service else None
    if duration is None:
        duration = _optional_call(asset, "get_play_length")
    duration = _safe_number(duration)

    frame_rate = (
        _optional_call(service, "get_animation_frame_rate", canonical_path) if service else None
    )
    frame_rate = _safe_number(frame_rate)

    frame_count = (
        _optional_call(animation_library, "get_num_frames", asset)
        if animation_library
        else None
    )
    if frame_count is None and service:
        frame_count = _optional_call(service, "get_animation_frame_count", canonical_path)
    frame_count = _safe_number(frame_count, integer=True)

    track_names = (
        _optional_call(animation_library, "get_animation_track_names", asset)
        if animation_library
        else None
    )
    if track_names is not None:
        track_names = [str(name) for name in track_names]

    root_motion = (
        _optional_call(animation_library, "is_root_motion_enabled", asset)
        if animation_library
        else None
    )
    if root_motion is None:
        root_motion = _get_field(asset, "enable_root_motion")
    if root_motion is not None:
        root_motion = bool(root_motion)
    root_motion_settings: dict[str, Any] = {}
    for property_name in _ROOT_MOTION_PROPERTIES:
        value = (
            root_motion
            if property_name == "enable_root_motion"
            else _get_field(asset, property_name)
        )
        if value is None:
            continue
        if isinstance(value, (bool, int, float, str)):
            root_motion_settings[property_name] = value
        else:
            root_motion_settings[property_name] = str(value)

    package_guid = None
    package = _optional_call(asset, "get_outermost")
    if package is None:
        package = _optional_call(asset, "get_package")
    if package is not None:
        guid = _optional_call(package, "get_guid")
        if guid is not None:
            package_guid = str(guid)

    unavailable = []
    for field, value in (
        ("duration_seconds", duration),
        ("frame_rate", frame_rate),
        ("frame_count", frame_count),
        ("track_names", track_names),
        ("root_motion_enabled", root_motion),
    ):
        if value is None:
            unavailable.append(field)

    package_path, _ = _split_asset_path(canonical_path)
    asset_name = package_path.rsplit("/", 1)[-1]
    hierarchy = _bone_hierarchy(ue, skeleton_path)
    if hierarchy is None:
        unavailable.append("bone_hierarchy")
    return {
        "asset_path": canonical_path,
        "package_path": package_path,
        "asset_name": asset_name,
        "class_name": _class_name(asset),
        "skeleton_path": skeleton_path,
        "duration_seconds": duration,
        "frame_rate": frame_rate,
        "frame_count": frame_count,
        "track_names": track_names,
        "track_count": len(track_names) if track_names is not None else None,
        "bone_hierarchy": hierarchy,
        "bone_count": len(hierarchy) if hierarchy is not None else None,
        "root_motion_enabled": root_motion,
        "root_motion_settings": root_motion_settings,
        "package_guid": package_guid,
        "unavailable_fields": unavailable,
    }


def _build_timing_contract(metadata: dict[str, Any]) -> dict[str, Any]:
    frame_count = _safe_number(metadata.get("frame_count"), integer=True)
    duration = _safe_number(metadata.get("duration_seconds"))
    frame_rate = _safe_number(metadata.get("frame_rate"))
    if frame_count is None or duration is None or frame_rate is None:
        raise AnimationBridgeError(
            "timing_metadata_unavailable",
            "Exact frame count, duration, and frame rate are required for Blender roundtrip.",
        )
    if int(frame_count) < 0 or float(duration) < 0.0 or float(frame_rate) <= 0.0:
        raise AnimationBridgeError(
            "invalid_timing_metadata",
            "Animation timing metadata contains unsupported negative or zero values.",
        )
    duration_frame_span = float(duration) * float(frame_rate)
    if not math.isclose(
        duration_frame_span,
        float(frame_count),
        rel_tol=0.0,
        abs_tol=0.001,
    ):
        raise AnimationBridgeError(
            "unsupported_frame_count_convention",
            "Unreal animation timing does not use the expected sample-interval convention.",
        )
    return {
        "contract": TIMING_CONTRACT_ID,
        "source_exporter": TIMING_SOURCE_EXPORTER,
        "frame_count_convention": TIMING_FRAME_COUNT_CONVENTION,
        "terminal_boundary_policy": TIMING_TERMINAL_POLICY,
        "expected_frame_count": int(frame_count),
        "expected_duration_seconds": float(duration),
        "expected_frame_rate": float(frame_rate),
    }


def _source_fingerprint(metadata: dict[str, Any]) -> str:
    hierarchy = [
        {
            "name": str(item.get("name") or ""),
            "parent": str(item.get("parent") or ""),
        }
        for item in (metadata.get("bone_hierarchy") or [])
        if isinstance(item, dict)
    ]
    payload = {
        "asset_path": metadata.get("asset_path"),
        "skeleton_path": metadata.get("skeleton_path"),
        "duration_seconds": _safe_number(metadata.get("duration_seconds")),
        "frame_rate": _safe_number(metadata.get("frame_rate")),
        "frame_count": _safe_number(metadata.get("frame_count"), integer=True),
        "track_names": sorted(
            str(value).casefold() for value in (metadata.get("track_names") or [])
        ),
        "bone_hierarchy": hierarchy,
        "root_motion_settings": metadata.get("root_motion_settings") or {
            "enable_root_motion": metadata.get("root_motion_enabled")
        },
        "package_guid": metadata.get("package_guid"),
    }
    encoded = json.dumps(
        payload,
        ensure_ascii=True,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def inspect_animation(asset_path: str, unreal_module=None) -> dict[str, Any]:
    """Inspect one exact AnimSequence without searching or mutating assets."""

    operation = "inspect_animation"
    try:
        ue = _get_unreal(unreal_module)
        asset, canonical = _load_exact_asset(ue, asset_path, "AnimSequence")
        return _success(operation, animation=_inspect_loaded_animation(ue, asset, canonical))
    except Exception as exc:
        return _failure(operation, exc, asset_path=str(asset_path or ""))


def _default_import_target(source_name: str, job_id: str) -> tuple[str, str]:
    destination_path = _validate_destination_path(f"{DEFAULT_CONTENT_ROOT}/{job_id}")
    base = re.sub(r"[^A-Za-z0-9_]", "_", source_name).strip("_") or "Animation"
    base = base[:80]
    name = _validate_destination_name(f"AN_Revvy_{base}_{job_id[-10:]}")
    return destination_path, name


def _task_errors(task: Any) -> list[str]:
    errors = _get_field(task, "errors")
    return [str(item) for item in (errors or [])]


def _update_manifest(manifest: dict[str, Any], path: pathlib.Path) -> None:
    manifest["updated_at"] = _utc_now()
    _atomic_write_json(path, manifest)


def export_animation_job(
    anim_sequence_path: str,
    job_id: Optional[str] = None,
    unreal_module=None,
) -> dict[str, Any]:
    """Export an AnimSequence FBX into a new Saved/Revvy exchange job."""

    operation = "export_animation_job"
    manifest = None
    manifest_path = None
    resolved_job_id = str(job_id or "")
    try:
        ue = _get_unreal(unreal_module)
        _ensure_editor_not_playing(ue)
        asset, canonical = _load_exact_asset(ue, anim_sequence_path, "AnimSequence")
        source = _inspect_loaded_animation(ue, asset, canonical)
        timing_contract = _build_timing_contract(source)
        resolved_job_id = _validate_job_id(job_id) if job_id is not None else _new_job_id()
        job_dir, manifest_path = _job_paths(ue, resolved_job_id)
        if job_dir.exists():
            raise AnimationBridgeError(
                "job_already_exists",
                f"Exchange job already exists and will not be overwritten: {resolved_job_id}",
            )
        job_dir.mkdir(parents=True, exist_ok=False)

        source_fbx = job_dir / "source.fbx"
        blender_output_fbx = job_dir / "edited.fbx"
        destination_path, destination_name = _default_import_target(
            source["asset_name"], resolved_job_id
        )
        manifest = {
            "schema_version": SCHEMA_VERSION,
            "job_id": resolved_job_id,
            "created_at": _utc_now(),
            "updated_at": _utc_now(),
            "status": "export_pending",
            "source": source,
            "source_fingerprint": _source_fingerprint(source),
            "timing_contract": timing_contract,
            "paths": {
                "job_dir": str(job_dir),
                "manifest": str(manifest_path),
                "source_fbx": str(source_fbx),
                "blender_output_fbx": str(blender_output_fbx),
            },
            "export": {
                "status": "pending",
                "api": [
                    "unreal.AssetExportTask",
                    "unreal.AnimSequenceExporterFBX",
                    "unreal.AnimSeqExportOption",
                    "unreal.Exporter.run_asset_export_task",
                ],
            },
            "import": {
                "status": "pending",
                "default_destination_path": destination_path,
                "default_destination_name": destination_name,
                "replace_existing": False,
            },
            "validation": None,
        }
        _atomic_write_json(manifest_path, manifest)

        if source_fbx.exists():
            raise AnimationBridgeError(
                "export_target_exists",
                f"FBX export target already exists: {source_fbx}",
            )

        try:
            task = ue.AssetExportTask()
            exporter = ue.AnimSequenceExporterFBX()
            options = ue.AnimSeqExportOption()
        except AttributeError as exc:
            raise AnimationBridgeError(
                "unreal_api_missing",
                "Required Unreal animation FBX export classes are unavailable.",
            ) from exc

        for property_name, value in (
            ("export_transforms", True),
            ("export_morph_targets", True),
            ("export_attribute_curves", True),
            ("export_material_curves", True),
            ("skip_curves_with_zero_value", False),
            ("record_in_world_space", False),
            ("evaluate_all_skeletal_mesh_components", False),
            ("set_retarget_source_asset", False),
        ):
            _set_editor_property(options, property_name, value, required=False)

        for property_name, value in (
            ("object", asset),
            ("exporter", exporter),
            ("options", options),
            ("filename", str(source_fbx)),
            ("selected", False),
            ("replace_identical", False),
            ("prompt", False),
            ("automated", True),
            ("use_file_archive", False),
            ("write_empty_files", False),
        ):
            _set_editor_property(task, property_name, value)

        exporter_api = getattr(ue, "Exporter", None)
        run_export = getattr(exporter_api, "run_asset_export_task", None)
        if not callable(run_export):
            raise AnimationBridgeError(
                "unreal_api_missing",
                "unreal.Exporter.run_asset_export_task is unavailable.",
            )
        exported = bool(run_export(task))
        errors = _task_errors(task)
        if not exported or not source_fbx.is_file() or source_fbx.stat().st_size <= 0:
            detail = "; ".join(errors) if errors else "Unreal exporter returned no usable FBX."
            raise AnimationBridgeError("fbx_export_failed", detail)

        manifest["status"] = "exported"
        manifest["export"] = {
            **manifest["export"],
            "status": "completed",
            "completed_at": _utc_now(),
            "fbx_size_bytes": source_fbx.stat().st_size,
            "errors": errors,
        }
        _update_manifest(manifest, manifest_path)
        return _success(
            operation,
            job_id=resolved_job_id,
            status=manifest["status"],
            manifest_path=str(manifest_path),
            paths=dict(manifest["paths"]),
            source=source,
            timing_contract=dict(timing_contract),
            default_import={
                "destination_path": destination_path,
                "destination_name": destination_name,
            },
            limitations=[
                "AnimSequenceExporterFBX requires a Skeleton with a compatible preview SkeletalMesh.",
                "FBX preserves sampled animation data but not every Unreal-only animation setting.",
            ],
        )
    except Exception as exc:
        if manifest is not None and manifest_path is not None:
            manifest["status"] = "export_failed"
            manifest["export"] = {
                **manifest.get("export", {}),
                "status": "failed",
                "failed_at": _utc_now(),
                "error": {"code": getattr(exc, "code", "unexpected_error"), "message": str(exc)},
            }
            try:
                _update_manifest(manifest, manifest_path)
            except Exception:
                pass
        values = {"job_id": resolved_job_id} if resolved_job_id else {}
        if manifest_path is not None:
            values["manifest_path"] = str(manifest_path)
        return _failure(operation, exc, **values)


def _metadata_issue(
    code: str,
    severity: str,
    message: str,
    source: Any = None,
    imported: Any = None,
) -> dict[str, Any]:
    result = {"code": code, "severity": severity, "message": message}
    if source is not None:
        result["source"] = source
    if imported is not None:
        result["imported"] = imported
    return result


def compare_animation_metadata(
    source: dict[str, Any],
    imported: dict[str, Any],
) -> dict[str, Any]:
    """Pure comparison used by import validation and offline tests."""

    issues = []
    if source.get("skeleton_path") != imported.get("skeleton_path"):
        issues.append(
            _metadata_issue(
                "skeleton_mismatch",
                "error",
                "Imported animation uses a different Skeleton.",
                source.get("skeleton_path"),
                imported.get("skeleton_path"),
            )
        )

    source_bones = source.get("bone_hierarchy")
    imported_bones = imported.get("bone_hierarchy")
    if source_bones is not None and imported_bones is not None:
        source_signature = [(item.get("name"), item.get("parent")) for item in source_bones]
        imported_signature = [(item.get("name"), item.get("parent")) for item in imported_bones]
        if source_signature != imported_signature:
            issues.append(
                _metadata_issue(
                    "bone_hierarchy_mismatch",
                    "error",
                    "Skeleton bone names or parent hierarchy changed.",
                    len(source_signature),
                    len(imported_signature),
                )
            )
    elif source_bones is None or imported_bones is None:
        issues.append(
            _metadata_issue(
                "bone_hierarchy_unavailable",
                "warning",
                "Bone hierarchy comparison is unavailable in this Unreal build.",
            )
        )

    source_tracks = source.get("track_names")
    imported_tracks = imported.get("track_names")
    if source_tracks is not None and imported_tracks is not None:
        source_set = set(source_tracks)
        imported_set = set(imported_tracks)
        missing = sorted(source_set - imported_set)
        added = sorted(imported_set - source_set)
        if missing or added:
            issues.append(
                _metadata_issue(
                    "track_set_mismatch",
                    "error",
                    "Animation bone track names changed.",
                    {"missing": missing},
                    {"added": added},
                )
            )
    else:
        issues.append(
            _metadata_issue(
                "track_names_unavailable",
                "warning",
                "Animation track-name comparison is unavailable in this Unreal build.",
            )
        )

    source_rate = _safe_number(source.get("frame_rate"))
    imported_rate = _safe_number(imported.get("frame_rate"))
    if source_rate is not None and imported_rate is not None:
        rate_tolerance = max(0.0001, abs(float(source_rate)) * 1e-6)
        if not math.isclose(
            float(source_rate),
            float(imported_rate),
            rel_tol=0.0,
            abs_tol=rate_tolerance,
        ):
            issues.append(
                _metadata_issue(
                    "frame_rate_mismatch",
                    "error",
                    "Animation sample frame rate changed.",
                    source_rate,
                    imported_rate,
                )
            )
    else:
        issues.append(
            _metadata_issue(
                "frame_rate_unavailable",
                "warning",
                "Frame-rate comparison is unavailable in this Unreal build.",
            )
        )

    source_frames = _safe_number(source.get("frame_count"), integer=True)
    imported_frames = _safe_number(imported.get("frame_count"), integer=True)
    if source_frames is not None and imported_frames is not None:
        if int(source_frames) != int(imported_frames):
            issues.append(
                _metadata_issue(
                    "frame_count_mismatch",
                    "error",
                    "Animation frame count changed.",
                    source_frames,
                    imported_frames,
                )
            )

    source_duration = _safe_number(source.get("duration_seconds"))
    imported_duration = _safe_number(imported.get("duration_seconds"))
    if source_duration is not None and imported_duration is not None:
        duration_delta = abs(float(source_duration) - float(imported_duration))
        if source_rate is not None:
            duration_changed = duration_delta * float(source_rate) > 0.001
        else:
            duration_changed = duration_delta > 1e-6
        if duration_changed:
            issues.append(
                _metadata_issue(
                    "duration_mismatch",
                    "error",
                    "Animation duration changed beyond floating-point sample tolerance.",
                    source_duration,
                    imported_duration,
                )
            )
    else:
        issues.append(
            _metadata_issue(
                "duration_unavailable",
                "warning",
                "Duration comparison is unavailable in this Unreal build.",
            )
        )

    source_root_settings = source.get("root_motion_settings")
    imported_root_settings = imported.get("root_motion_settings")
    if isinstance(source_root_settings, dict) and isinstance(imported_root_settings, dict):
        if source_root_settings != imported_root_settings:
            issues.append(
                _metadata_issue(
                    "root_motion_setting_mismatch",
                    "error",
                    "One or more Unreal root-motion settings changed.",
                    source_root_settings,
                    imported_root_settings,
                )
            )
    else:
        source_root_motion = source.get("root_motion_enabled")
        imported_root_motion = imported.get("root_motion_enabled")
        if source_root_motion is not None and imported_root_motion is not None:
            if bool(source_root_motion) != bool(imported_root_motion):
                issues.append(
                    _metadata_issue(
                        "root_motion_setting_mismatch",
                        "error",
                        "Unreal root-motion enablement changed.",
                        bool(source_root_motion),
                        bool(imported_root_motion),
                    )
                )
        else:
            issues.append(
                _metadata_issue(
                    "root_motion_setting_unavailable",
                    "warning",
                    "Root-motion setting comparison is unavailable in this Unreal build.",
                )
            )

    errors = [item for item in issues if item["severity"] == "error"]
    warnings = [item for item in issues if item["severity"] == "warning"]
    return {
        "passed": not errors,
        "error_count": len(errors),
        "warning_count": len(warnings),
        "issues": issues,
    }


def _copy_root_motion_settings(ue: Any, source: Any, target: Any) -> list[str]:
    copied: list[str] = []
    enabled = _get_field(source, "enable_root_motion")
    animation_library = getattr(ue, "AnimationLibrary", None)
    setter = (
        getattr(animation_library, "set_root_motion_enabled", None)
        if animation_library
        else None
    )
    if enabled is not None and callable(setter):
        try:
            setter(target, bool(enabled))
            copied.append("enable_root_motion")
        except Exception:
            pass
    if enabled is not None and "enable_root_motion" not in copied:
        if _set_editor_property(
            target,
            "enable_root_motion",
            bool(enabled),
            required=False,
        ):
            copied.append("enable_root_motion")
    for property_name in _ROOT_MOTION_PROPERTIES[1:]:
        value = _get_field(source, property_name)
        if value is None:
            continue
        if _set_editor_property(target, property_name, value, required=False):
            copied.append(property_name)
    return copied


def _save_loaded_asset(ue: Any, asset: Any) -> bool:
    saver = getattr(_asset_library(ue), "save_loaded_asset", None)
    if not callable(saver):
        return False
    try:
        return bool(saver(asset, False))
    except TypeError:
        try:
            return bool(saver(asset))
        except Exception:
            return False
    except Exception:
        return False


def _imported_paths(task: Any) -> list[str]:
    values = _get_field(task, "imported_object_paths") or []
    paths = [str(value) for value in values if str(value)]
    get_objects = getattr(task, "get_objects", None)
    if callable(get_objects):
        try:
            paths.extend(_object_path(obj) for obj in get_objects() if obj is not None)
        except Exception:
            pass
    return list(dict.fromkeys(path for path in paths if path))


def _record_import_failure(
    manifest: dict[str, Any],
    manifest_path: pathlib.Path,
    error: Exception,
    attempt: dict[str, Any],
) -> None:
    failure = {
        **attempt,
        "status": "failed",
        "failed_at": _utc_now(),
        "error": {"code": getattr(error, "code", "unexpected_error"), "message": str(error)},
    }
    import_state = dict(manifest.get("import", {}))
    attempts = [
        dict(item) for item in import_state.get("attempts", []) if isinstance(item, dict)
    ]
    attempt_id = attempt.get("attempt_id")
    replaced = False
    if attempt_id:
        for index, item in enumerate(attempts):
            if item.get("attempt_id") == attempt_id:
                attempts[index] = failure
                replaced = True
                break
    if attempt and not replaced:
        attempts.append(failure)
    manifest["status"] = "import_failed"
    manifest["import"] = {
        **import_state,
        **failure,
        "attempts": attempts,
    }
    _update_manifest(manifest, manifest_path)


def import_animation_job(
    job_id: str,
    edited_fbx_path: Optional[str] = None,
    skeleton_path: Optional[str] = None,
    destination_path: Optional[str] = None,
    destination_name: Optional[str] = None,
    unreal_module=None,
) -> dict[str, Any]:
    """Queue an FBX import outside the active MCP tool stack when Unreal supports it."""

    operation = "import_animation_job"
    try:
        ue = _get_unreal(unreal_module)
        _ensure_editor_not_playing(ue)
        registrar = getattr(ue, "register_slate_post_tick_callback", None)
        unregister = getattr(ue, "unregister_slate_post_tick_callback", None)
        if not callable(registrar) or not callable(unregister):
            return _import_animation_job_now(
                job_id,
                edited_fbx_path,
                skeleton_path,
                destination_path,
                destination_name,
                unreal_module=ue,
            )

        validated_job_id = _validate_job_id(job_id)
        manifest, _job_dir, manifest_path = _read_job(ue, validated_job_id)
        with _DEFERRED_IMPORTS_LOCK:
            if validated_job_id in _DEFERRED_IMPORTS:
                raise AnimationBridgeError(
                    "import_already_queued",
                    f"An Unreal import is already queued for job {validated_job_id}.",
                )
            _DEFERRED_IMPORTS[validated_job_id] = {"handle": None}

        holder: dict[str, Any] = {}

        def run_deferred(_delta_seconds: float) -> None:
            handle = holder.get("handle")
            if handle is not None:
                try:
                    unregister(handle)
                except Exception:
                    pass
            try:
                _import_animation_job_now(
                    validated_job_id,
                    edited_fbx_path,
                    skeleton_path,
                    destination_path,
                    destination_name,
                    unreal_module=ue,
                )
            finally:
                with _DEFERRED_IMPORTS_LOCK:
                    _DEFERRED_IMPORTS.pop(validated_job_id, None)

        try:
            handle = registrar(run_deferred)
            holder["handle"] = handle
            with _DEFERRED_IMPORTS_LOCK:
                _DEFERRED_IMPORTS[validated_job_id]["handle"] = handle
        except Exception:
            with _DEFERRED_IMPORTS_LOCK:
                _DEFERRED_IMPORTS.pop(validated_job_id, None)
            raise

        manifest["status"] = "import_queued"
        manifest["import"] = {
            **manifest.get("import", {}),
            "status": "queued",
            "queued_at": _utc_now(),
        }
        _update_manifest(manifest, manifest_path)
        return _success(
            operation,
            job_id=validated_job_id,
            status="queued",
            manifest_path=str(manifest_path),
            poll_with="get_job",
            deferred_outside_mcp_tool=True,
        )
    except Exception as exc:
        return _failure(operation, exc, job_id=str(job_id or ""))


def _import_animation_job_now(
    job_id: str,
    edited_fbx_path: Optional[str] = None,
    skeleton_path: Optional[str] = None,
    destination_path: Optional[str] = None,
    destination_name: Optional[str] = None,
    unreal_module=None,
) -> dict[str, Any]:
    """Perform a queued import after the active MCP tool execution has returned."""

    operation = "import_animation_job"
    manifest = None
    manifest_path = None
    attempt: dict[str, Any] = {}
    try:
        ue = _get_unreal(unreal_module)
        _ensure_editor_not_playing(ue)
        manifest, job_dir, manifest_path = _read_job(ue, job_id)
        if manifest.get("export", {}).get("status") != "completed":
            raise AnimationBridgeError("job_not_exported", "The job has no completed source export.")

        source_path = manifest.get("source", {}).get("asset_path")
        source_asset, canonical_source = _load_exact_asset(ue, source_path, "AnimSequence")
        current_source = _inspect_loaded_animation(ue, source_asset, canonical_source)
        recorded_skeleton = str(manifest.get("source", {}).get("skeleton_path") or "")
        if current_source["skeleton_path"] != recorded_skeleton:
            raise AnimationBridgeError(
                "source_skeleton_changed",
                "The source animation Skeleton changed after export; create a new job.",
            )
        recorded_fingerprint = str(
            manifest.get("source_fingerprint")
            or _source_fingerprint(manifest.get("source", {}))
        )
        if _source_fingerprint(current_source) != recorded_fingerprint:
            raise AnimationBridgeError(
                "source_animation_changed",
                "The source animation changed after export; create a new roundtrip job.",
            )
        recorded_timing = manifest.get("timing_contract")
        expected_timing = _build_timing_contract(current_source)
        if not isinstance(recorded_timing, dict) or recorded_timing != expected_timing:
            raise AnimationBridgeError(
                "invalid_manifest",
                "The Blender roundtrip timing contract is missing or does not match the source.",
            )

        expected_fbx = pathlib.Path(
            str(manifest.get("paths", {}).get("blender_output_fbx", ""))
        ).resolve()
        if not _is_within(expected_fbx, job_dir):
            raise AnimationBridgeError("invalid_manifest", "Blender output path escapes the job.")
        fbx_path = pathlib.Path(edited_fbx_path).expanduser().resolve() if edited_fbx_path else expected_fbx
        if not _is_within(fbx_path, job_dir):
            raise AnimationBridgeError(
                "fbx_outside_job",
                "edited_fbx_path must be inside this exchange job directory.",
            )
        if fbx_path.suffix.lower() != ".fbx" or not fbx_path.is_file():
            raise AnimationBridgeError("edited_fbx_not_found", f"Edited FBX not found: {fbx_path}")

        requested_skeleton = skeleton_path or recorded_skeleton
        skeleton, canonical_skeleton = _load_exact_asset(ue, requested_skeleton, "Skeleton")
        if canonical_skeleton != recorded_skeleton:
            raise AnimationBridgeError(
                "skeleton_mismatch",
                "Roundtrip import must use the exact Skeleton recorded by the export job.",
            )

        import_state = manifest.get("import", {})
        destination_path = _validate_destination_path(
            destination_path or import_state.get("default_destination_path")
        )
        destination_name = _validate_destination_name(
            destination_name or import_state.get("default_destination_name")
        )
        target_package = f"{destination_path}/{destination_name}"
        target_object = f"{target_package}.{destination_name}"
        if _canonical_package_path(canonical_source) == target_package:
            raise AnimationBridgeError(
                "source_overwrite_blocked",
                "Import target resolves to the source animation and is blocked.",
            )
        if _asset_exists(ue, target_object):
            raise AnimationBridgeError(
                "destination_exists",
                f"Import target already exists and will not be overwritten: {target_package}",
            )

        attempt = {
            "attempt_id": uuid.uuid4().hex,
            "started_at": _utc_now(),
            "edited_fbx_path": str(fbx_path),
            "skeleton_path": canonical_skeleton,
            "destination_path": destination_path,
            "destination_name": destination_name,
            "target_asset_path": target_object,
            "replace_existing": False,
        }
        import_state = dict(manifest.get("import", {}))
        attempts = [
            dict(item)
            for item in import_state.get("attempts", [])
            if isinstance(item, dict)
        ]
        attempts.append({**attempt, "status": "importing"})
        manifest["status"] = "importing"
        manifest["import"] = {
            **import_state,
            **attempt,
            "status": "importing",
            "attempts": attempts,
        }
        _update_manifest(manifest, manifest_path)

        try:
            task = ue.AssetImportTask()
            options = ue.FbxImportUI()
        except AttributeError as exc:
            raise AnimationBridgeError(
                "unreal_api_missing",
                "Required Unreal FBX animation import classes are unavailable.",
            ) from exc

        animation_type = getattr(getattr(ue, "FBXImportType", None), "FBXIT_ANIMATION", None)
        if animation_type is None:
            raise AnimationBridgeError(
                "unreal_api_missing",
                "unreal.FBXImportType.FBXIT_ANIMATION is unavailable.",
            )
        for property_name, value in (
            ("automated_import_should_detect_type", False),
            ("mesh_type_to_import", animation_type),
            ("import_animations", True),
            ("import_mesh", False),
            ("import_as_skeletal", False),
            ("import_materials", False),
            ("import_textures", False),
            ("skeleton", skeleton),
            ("override_animation_name", destination_name),
        ):
            _set_editor_property(options, property_name, value)

        animation_data = _get_field(options, "anim_sequence_import_data")
        if animation_data is None:
            raise AnimationBridgeError(
                "unreal_api_missing",
                "FbxImportUI did not provide AnimSequenceImportData.",
            )
        length_enum = getattr(
            getattr(ue, "FBXAnimationLengthImportType", None),
            "FBXALIT_EXPORTED_TIME",
            None,
        )
        if length_enum is not None:
            _set_editor_property(animation_data, "animation_length", length_enum)
        rate = _safe_number(recorded_timing.get("expected_frame_rate"))
        custom_rate = int(round(float(rate))) if rate and abs(float(rate) - round(float(rate))) < 0.001 else 0
        for property_name, value in (
            ("use_default_sample_rate", False),
            ("custom_sample_rate", custom_rate),
            ("snap_to_closest_frame_boundary", True),
            ("import_meshes_in_bone_hierarchy", False),
            ("import_custom_attribute", True),
            ("delete_existing_custom_attribute_curves", False),
            ("delete_existing_non_curve_custom_attributes", False),
            ("import_bone_tracks", True),
            ("add_curve_metadata_to_skeleton", False),
            ("remove_redundant_keys", False),
            ("delete_existing_morph_target_curves", False),
            ("do_not_import_curve_with_zero", False),
            ("preserve_local_transform", True),
            ("convert_scene", True),
            ("convert_scene_unit", True),
        ):
            _set_editor_property(animation_data, property_name, value, required=False)

        for property_name, value in (
            ("filename", str(fbx_path)),
            ("destination_path", destination_path),
            ("destination_name", destination_name),
            ("replace_existing", False),
            ("replace_existing_settings", False),
            ("automated", True),
            ("save", True),
            ("options", options),
        ):
            _set_editor_property(task, property_name, value)
        _set_editor_property(task, "async_", False, required=False)

        helpers = getattr(ue, "AssetToolsHelpers", None)
        get_asset_tools = getattr(helpers, "get_asset_tools", None) if helpers else None
        if not callable(get_asset_tools):
            raise AnimationBridgeError("unreal_api_missing", "AssetToolsHelpers is unavailable.")
        asset_tools = get_asset_tools()
        importer = getattr(asset_tools, "import_asset_tasks", None)
        if not callable(importer):
            raise AnimationBridgeError("unreal_api_missing", "AssetTools.import_asset_tasks is unavailable.")
        importer([task])

        imported_paths = _imported_paths(task)
        task_errors = _task_errors(task)
        attempt.update(
            {
                "imported_object_paths": imported_paths,
                "task_errors": task_errors,
            }
        )
        attempts[-1] = {**attempt, "status": "imported_from_fbx"}
        manifest["import"] = {
            **manifest.get("import", {}),
            **attempt,
            "attempts": attempts,
        }
        _update_manifest(manifest, manifest_path)
        imported_animations = []
        for path in imported_paths:
            try:
                candidate, canonical = _load_exact_asset(ue, path, "AnimSequence")
            except AnimationBridgeError:
                continue
            imported_animations.append((candidate, canonical))
        if len(imported_animations) != 1:
            detail = "; ".join(task_errors)
            if detail:
                detail = f" Importer errors: {detail}"
            raise AnimationBridgeError(
                "unexpected_import_result",
                (
                    f"Expected exactly one AnimSequence, got {len(imported_animations)}: "
                    f"{imported_paths}.{detail}"
                ),
            )

        imported_asset, imported_path = imported_animations[0]
        if _canonical_package_path(imported_path) != target_package:
            raise AnimationBridgeError(
                "unexpected_import_path",
                f"Unreal created {imported_path}, expected {target_object}.",
            )

        copied_root_motion_settings = _copy_root_motion_settings(
            ue,
            source_asset,
            imported_asset,
        )
        if copied_root_motion_settings and not _save_loaded_asset(ue, imported_asset):
            raise AnimationBridgeError(
                "post_import_save_failed",
                "The imported animation was created, but root-motion settings could not be saved.",
            )

        imported_metadata = _inspect_loaded_animation(ue, imported_asset, imported_path)
        validation = compare_animation_metadata(manifest["source"], imported_metadata)
        manifest["validation"] = validation
        manifest["status"] = "imported_valid" if validation["passed"] else "imported_with_errors"
        manifest["import"] = {
            **manifest.get("import", {}),
            **attempt,
            "status": "completed",
            "completed_at": _utc_now(),
            "imported_object_paths": imported_paths,
            "imported_animation_path": imported_path,
            "root_motion_settings_copied": copied_root_motion_settings,
            "task_errors": task_errors,
            "attempts": [
                *attempts[:-1],
                {
                    **attempt,
                    "status": "completed",
                    "completed_at": _utc_now(),
                },
            ],
        }
        _update_manifest(manifest, manifest_path)
        result = {
            "success": bool(validation["passed"]),
            "operation": operation,
            "job_id": manifest["job_id"],
            "status": manifest["status"],
            "manifest_path": str(manifest_path),
            "source": manifest["source"],
            "imported": imported_metadata,
            "validation": validation,
            "imported_animation_path": imported_path,
            "source_preserved": True,
            "replace_existing": False,
        }
        if not validation["passed"]:
            result["error"] = {
                "code": "validation_failed",
                "message": "The new AnimSequence was imported, but roundtrip validation failed.",
            }
        return result
    except Exception as exc:
        if manifest is not None and manifest_path is not None:
            try:
                _record_import_failure(manifest, manifest_path, exc, attempt)
            except Exception:
                pass
        values = {"job_id": str(job_id or "")}
        if manifest_path is not None:
            values["manifest_path"] = str(manifest_path)
        return _failure(operation, exc, **values)


def validate_roundtrip(
    source_asset_path: str,
    imported_asset_path: str,
    unreal_module=None,
) -> dict[str, Any]:
    """Compare two exact AnimSequence assets without modifying either one."""

    operation = "validate_roundtrip"
    try:
        ue = _get_unreal(unreal_module)
        source_asset, source_path = _load_exact_asset(ue, source_asset_path, "AnimSequence")
        imported_asset, imported_path = _load_exact_asset(
            ue, imported_asset_path, "AnimSequence"
        )
        source = _inspect_loaded_animation(ue, source_asset, source_path)
        imported = _inspect_loaded_animation(ue, imported_asset, imported_path)
        validation = compare_animation_metadata(source, imported)
        result = {
            "success": bool(validation["passed"]),
            "operation": operation,
            "source": source,
            "imported": imported,
            "validation": validation,
        }
        if not validation["passed"]:
            result["error"] = {
                "code": "validation_failed",
                "message": "The AnimSequence metadata comparison found blocking differences.",
            }
        return result
    except Exception as exc:
        return _failure(
            operation,
            exc,
            source_asset_path=str(source_asset_path or ""),
            imported_asset_path=str(imported_asset_path or ""),
        )


def get_job(job_id: str, unreal_module=None) -> dict[str, Any]:
    """Read one exchange job after validating its location and manifest."""

    operation = "get_job"
    try:
        ue = _get_unreal(unreal_module)
        manifest, _, manifest_path = _read_job(ue, job_id)
        return _success(
            operation,
            job_id=manifest["job_id"],
            manifest_path=str(manifest_path),
            job=manifest,
        )
    except Exception as exc:
        return _failure(operation, exc, job_id=str(job_id or ""))
