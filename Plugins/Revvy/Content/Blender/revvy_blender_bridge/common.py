from __future__ import annotations

import json
import math
import os
import pathlib
import re
import secrets
from dataclasses import dataclass
from typing import Any, Mapping

from .security import SecurityError, read_secure_json, security_dir, write_secure_json


CONFIG_VERSION = 1
BRIDGE_HOST = "127.0.0.1"
DEFAULT_PORT = 9876
MAX_MESSAGE_BYTES = 8 * 1024 * 1024
TIMING_CONTRACT_ID = "revvy.unreal_anim_sequence_fbx_timing.v1"
TIMING_SOURCE_EXPORTER = "unreal.AnimSequenceExporterFBX"
TIMING_FRAME_COUNT_CONVENTION = "sample_intervals"
TIMING_TERMINAL_POLICY = "trim_one_verified_duplicate"
COMMANDS = frozenset(
    {
        "status",
        "job.status",
        "scene.inspect",
        "model.import",
        "mesh.inspect",
        "mesh.edit",
        "modifier.apply",
        "material.assign",
        "uv.inspect",
        "uv.edit",
        "model.export",
        "animation.import_fbx",
        "animation.inspect",
        "animation.adjust",
        "animation.export_fbx",
        "file.save_copy",
    }
)
READ_ONLY_COMMANDS = frozenset(
    {
        "status",
        "job.status",
        "scene.inspect",
        "mesh.inspect",
        "uv.inspect",
        "animation.inspect",
    }
)
MUTATING_COMMANDS = COMMANDS - READ_ONLY_COMMANDS

_SAFE_NAME_RE = re.compile(r"^[^\x00-\x1f\\/]{1,128}$")


class BridgeError(Exception):
    def __init__(
        self,
        code: str,
        message: str,
        details: Mapping[str, Any] | None = None,
    ) -> None:
        super().__init__(message)
        self.code = code
        self.message = message
        self.details = dict(details or {})

    def as_dict(self) -> dict[str, Any]:
        payload: dict[str, Any] = {"code": self.code, "message": self.message}
        if self.details:
            payload["details"] = self.details
        return payload


@dataclass(frozen=True)
class BridgeConfig:
    path: pathlib.Path
    token: str
    exchange_root: pathlib.Path
    port: int
    read_only_token: str = ""

    def public_dict(self) -> dict[str, Any]:
        return {
            "version": CONFIG_VERSION,
            "host": BRIDGE_HOST,
            "port": self.port,
            "exchange_root": str(self.exchange_root),
        }


def config_path(env: Mapping[str, str] | None = None) -> pathlib.Path:
    values = os.environ if env is None else env
    configured = str(values.get("REVVY_BLENDER_BRIDGE_CONFIG") or "").strip()
    if configured:
        return pathlib.Path(configured).expanduser().resolve()
    return security_dir(values) / "blender-bridge.json"


def legacy_config_path(env: Mapping[str, str] | None = None) -> pathlib.Path:
    values = os.environ if env is None else env
    state_root = str(values.get("REVVY_STATE_DIR") or "").strip()
    if state_root:
        return (
            pathlib.Path(state_root).expanduser().resolve()
            / "blender-bridge.json"
        )
    appdata = values.get("APPDATA")
    if appdata:
        root = pathlib.Path(appdata)
    elif os.name == "nt":
        root = pathlib.Path.home() / "AppData" / "Roaming"
    else:
        root = pathlib.Path(values.get("XDG_CONFIG_HOME", pathlib.Path.home() / ".config"))
    return root / "Revvy" / "blender-bridge.json"


def default_exchange_root(
    env: Mapping[str, str] | None = None,
    *,
    config_file: pathlib.Path | None = None,
) -> pathlib.Path:
    values = os.environ if env is None else env
    local_appdata = values.get("LOCALAPPDATA")
    if local_appdata:
        return pathlib.Path(local_appdata) / "Revvy" / "BlenderExchange"
    path = config_file or config_path(values)
    return path.parent / "BlenderExchange"


def _write_json_atomic(path: pathlib.Path, payload: Mapping[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    temporary.write_text(
        json.dumps(payload, ensure_ascii=True, indent=2) + "\n",
        encoding="utf-8",
    )
    try:
        os.chmod(temporary, 0o600)
    except OSError:
        pass
    os.replace(temporary, path)


def ensure_config(
    path: pathlib.Path | None = None,
    *,
    env: Mapping[str, str] | None = None,
) -> BridgeConfig:
    target = (path or config_path(env)).expanduser()
    if not target.exists():
        legacy = legacy_config_path(env)
        existing: dict[str, Any] = {}
        if legacy != target and legacy.is_file():
            try:
                loaded = json.loads(legacy.read_text(encoding="utf-8-sig"))
                if isinstance(loaded, dict):
                    existing = loaded
            except (OSError, UnicodeError, json.JSONDecodeError):
                pass
        exchange_root = pathlib.Path(
            existing.get("exchange_root")
            or default_exchange_root(env, config_file=target)
        ).expanduser().resolve()
        exchange_root.mkdir(parents=True, exist_ok=True)
        token = str(existing.get("token") or "").strip()
        if len(token) < 32:
            token = secrets.token_urlsafe(48)
        port = existing.get("port", DEFAULT_PORT)
        if isinstance(port, bool) or not isinstance(port, int) or not 1024 <= port <= 65535:
            port = DEFAULT_PORT
        write_secure_json(
            target,
            {
                "version": CONFIG_VERSION,
                "host": BRIDGE_HOST,
                "port": port,
                "token": token,
                "read_only_token": secrets.token_urlsafe(48),
                "exchange_root": str(exchange_root),
            },
        )
        if legacy != target and legacy.exists():
            _write_json_atomic(
                legacy,
                {
                    "version": CONFIG_VERSION,
                    "host": BRIDGE_HOST,
                    "port": port,
                    "exchange_root": str(exchange_root),
                },
            )
    return load_config(target)


def load_config(path: pathlib.Path | None = None) -> BridgeConfig:
    target = (path or config_path()).expanduser()
    try:
        if not target.is_file():
            raise FileNotFoundError(target)
        payload = read_secure_json(target)
    except FileNotFoundError as exc:
        raise BridgeError(
            "config_missing",
            f"Blender bridge config does not exist: {target}",
        ) from exc
    except (OSError, SecurityError, UnicodeError, json.JSONDecodeError) as exc:
        raise BridgeError(
            "config_invalid",
            f"Could not read Blender bridge config: {target}",
        ) from exc

    if not isinstance(payload, dict):
        raise BridgeError("config_invalid", "Bridge config must be a JSON object.")
    allowed = {
        "version",
        "host",
        "port",
        "token",
        "read_only_token",
        "exchange_root",
    }
    unknown = sorted(set(payload) - allowed)
    if unknown:
        raise BridgeError(
            "config_invalid",
            "Bridge config contains unsupported fields.",
            {"fields": unknown},
        )
    if payload.get("version") != CONFIG_VERSION:
        raise BridgeError(
            "config_invalid",
            f"Bridge config version must be {CONFIG_VERSION}.",
        )
    if payload.get("host") != BRIDGE_HOST:
        raise BridgeError(
            "config_invalid",
            f"Bridge host must be exactly {BRIDGE_HOST}.",
        )
    port = payload.get("port")
    if isinstance(port, bool) or not isinstance(port, int) or not 1024 <= port <= 65535:
        raise BridgeError("config_invalid", "Bridge port must be between 1024 and 65535.")
    token = payload.get("token")
    if not isinstance(token, str) or len(token) < 32:
        raise BridgeError(
            "config_invalid",
            "Bridge token must be a string with at least 32 characters.",
        )
    read_only_token = payload.get("read_only_token", "")
    if read_only_token != "" and (
        not isinstance(read_only_token, str)
        or len(read_only_token) < 32
        or secrets.compare_digest(read_only_token, token)
    ):
        raise BridgeError(
            "config_invalid",
            "Bridge read-only token must be distinct and contain at least 32 characters.",
        )
    exchange_value = payload.get("exchange_root")
    if not isinstance(exchange_value, str) or not exchange_value.strip():
        raise BridgeError("config_invalid", "exchange_root must be a non-empty path.")
    exchange_root = pathlib.Path(exchange_value).expanduser().resolve()
    try:
        exchange_root.mkdir(parents=True, exist_ok=True)
    except OSError as exc:
        raise BridgeError(
            "config_invalid",
            f"Could not create exchange_root: {exchange_root}",
        ) from exc
    if not exchange_root.is_dir():
        raise BridgeError("config_invalid", "exchange_root must be a directory.")
    return BridgeConfig(
        path=target.resolve(),
        token=token,
        exchange_root=exchange_root,
        port=port,
        read_only_token=read_only_token,
    )


def resolve_exchange_path(
    config: BridgeConfig,
    value: Any,
    *,
    suffix: str,
    must_exist: bool,
) -> pathlib.Path:
    return resolve_exchange_path_any(
        config,
        value,
        suffixes=(suffix,),
        must_exist=must_exist,
    )


def resolve_exchange_path_any(
    config: BridgeConfig,
    value: Any,
    *,
    suffixes: tuple[str, ...],
    must_exist: bool,
) -> pathlib.Path:
    if not isinstance(value, str) or not value.strip():
        raise BridgeError("invalid_arguments", "path must be a non-empty string.")
    normalized_suffixes = tuple(
        item.lower() if item.startswith(".") else f".{item.lower()}"
        for item in suffixes
        if isinstance(item, str) and item.strip()
    )
    if not normalized_suffixes:
        raise BridgeError("invalid_arguments", "At least one file extension is required.")
    raw = pathlib.Path(value).expanduser()
    candidate = (raw if raw.is_absolute() else config.exchange_root / raw).resolve()
    try:
        candidate.relative_to(config.exchange_root)
    except ValueError as exc:
        raise BridgeError(
            "path_outside_exchange_root",
            "Path must stay inside the configured exchange_root.",
            {"exchange_root": str(config.exchange_root)},
        ) from exc
    if candidate == config.exchange_root:
        raise BridgeError("invalid_arguments", "Path must identify a file.")
    if candidate.suffix.lower() not in normalized_suffixes:
        allowed = ", ".join(normalized_suffixes)
        raise BridgeError(
            "invalid_arguments",
            f"Path must use one of these extensions: {allowed}.",
        )
    if must_exist and not candidate.is_file():
        raise BridgeError("file_not_found", f"File does not exist: {candidate}")
    if not must_exist:
        candidate.parent.mkdir(parents=True, exist_ok=True)
        resolved_parent = candidate.parent.resolve()
        try:
            resolved_parent.relative_to(config.exchange_root)
        except ValueError as exc:
            raise BridgeError(
                "path_outside_exchange_root",
                "Output directory escapes exchange_root.",
            ) from exc
    return candidate


def _expect_object(value: Any, label: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise BridgeError("invalid_arguments", f"{label} must be an object.")
    return value


def _reject_unknown(value: Mapping[str, Any], allowed: set[str], label: str) -> None:
    unknown = sorted(set(value) - allowed)
    if unknown:
        raise BridgeError(
            "invalid_arguments",
            f"{label} contains unsupported fields.",
            {"fields": unknown},
        )


def _name(value: Any, label: str) -> str:
    if not isinstance(value, str) or not _SAFE_NAME_RE.fullmatch(value):
        raise BridgeError(
            "invalid_arguments",
            f"{label} must be a non-empty exact Blender name without path separators.",
        )
    return value


def _bone_hierarchy(value: Any) -> list[dict[str, str | None]]:
    if not isinstance(value, list) or not 1 <= len(value) <= 1024:
        raise BridgeError(
            "invalid_arguments",
            "expected_bone_hierarchy must contain between 1 and 1024 bones.",
        )
    result: list[dict[str, str | None]] = []
    names: set[str] = set()
    for index, item in enumerate(value):
        bone = _expect_object(item, f"expected_bone_hierarchy[{index}]")
        _reject_unknown(bone, {"name", "parent"}, f"expected_bone_hierarchy[{index}]")
        name = _name(bone.get("name"), f"expected_bone_hierarchy[{index}].name")
        parent_value = bone.get("parent")
        if parent_value in (None, ""):
            parent = None
        else:
            parent = _name(
                parent_value,
                f"expected_bone_hierarchy[{index}].parent",
            )
        if name in names:
            raise BridgeError(
                "invalid_arguments",
                f'expected_bone_hierarchy contains duplicate bone "{name}".',
            )
        names.add(name)
        result.append({"name": name, "parent": parent})
    missing_parents = sorted(
        {
            str(item["parent"])
            for item in result
            if item["parent"] is not None and item["parent"] not in names
        }
    )
    if missing_parents:
        raise BridgeError(
            "invalid_arguments",
            "expected_bone_hierarchy references missing parent bones.",
            {"parents": missing_parents},
        )
    roots = [item["name"] for item in result if item["parent"] is None]
    if len(roots) != 1:
        raise BridgeError(
            "invalid_arguments",
            "expected_bone_hierarchy must contain exactly one root bone.",
            {"roots": roots},
        )
    return result


def _timing_contract(value: Any) -> dict[str, Any]:
    contract = _expect_object(value, "timing_contract")
    fields = {
        "contract",
        "source_exporter",
        "frame_count_convention",
        "terminal_boundary_policy",
        "expected_frame_count",
        "expected_duration_seconds",
        "expected_frame_rate",
    }
    _reject_unknown(contract, fields, "timing_contract")
    expected_literals = {
        "contract": TIMING_CONTRACT_ID,
        "source_exporter": TIMING_SOURCE_EXPORTER,
        "frame_count_convention": TIMING_FRAME_COUNT_CONVENTION,
        "terminal_boundary_policy": TIMING_TERMINAL_POLICY,
    }
    for field, expected in expected_literals.items():
        if contract.get(field) != expected:
            raise BridgeError(
                "invalid_arguments",
                f"timing_contract.{field} is unsupported.",
                {"expected": expected},
            )

    frame_count = contract.get("expected_frame_count")
    if (
        isinstance(frame_count, bool)
        or not isinstance(frame_count, int)
        or not 0 <= frame_count <= 1_000_000
    ):
        raise BridgeError(
            "invalid_arguments",
            "timing_contract.expected_frame_count must be an integer from 0 to 1,000,000.",
        )
    duration = _optional_number(
        contract.get("expected_duration_seconds"),
        "timing_contract.expected_duration_seconds",
    )
    frame_rate = _optional_number(
        contract.get("expected_frame_rate"),
        "timing_contract.expected_frame_rate",
    )
    if duration is None or duration < 0.0:
        raise BridgeError(
            "invalid_arguments",
            "timing_contract.expected_duration_seconds must be non-negative.",
        )
    if frame_rate is None or frame_rate <= 0.0:
        raise BridgeError(
            "invalid_arguments",
            "timing_contract.expected_frame_rate must be positive.",
        )
    expected_span = duration * frame_rate
    if not math.isclose(expected_span, float(frame_count), abs_tol=0.001):
        raise BridgeError(
            "invalid_arguments",
            "timing_contract does not use Unreal's sample-interval convention.",
            {
                "expected_frame_count": frame_count,
                "duration_times_rate": expected_span,
            },
        )
    return contract


def _optional_number(value: Any, label: str) -> float | None:
    if value is None:
        return None
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise BridgeError("invalid_arguments", f"{label} must be a number.")
    number = float(value)
    if not -1_000_000.0 <= number <= 1_000_000.0:
        raise BridgeError("invalid_arguments", f"{label} is outside the supported range.")
    return number


def _frame_range(operation: dict[str, Any]) -> None:
    start = _optional_number(operation.get("frame_start"), "frame_start")
    end = _optional_number(operation.get("frame_end"), "frame_end")
    if start is not None and end is not None and start > end:
        raise BridgeError("invalid_arguments", "frame_start must not exceed frame_end.")


def _validate_operation(value: Any) -> dict[str, Any]:
    operation = _expect_object(value, "operation")
    operation_type = operation.get("type")
    if operation_type == "transform":
        _reject_unknown(
            operation,
            {
                "type",
                "target",
                "bone_name",
                "channel",
                "mode",
                "values",
                "units",
                "frame_start",
                "frame_end",
            },
            "transform operation",
        )
        target = operation.get("target")
        if target not in {"object", "bone"}:
            raise BridgeError(
                "invalid_arguments",
                "transform target must be object or bone.",
            )
        if target == "bone":
            _name(operation.get("bone_name"), "bone_name")
        elif "bone_name" in operation:
            raise BridgeError(
                "invalid_arguments",
                "bone_name is only valid when target is bone.",
            )
        channel = operation.get("channel")
        dimensions = {
            "translate": 3,
            "rotate_euler": 3,
            "rotate_quaternion": 4,
            "scale": 3,
        }
        if channel not in dimensions:
            raise BridgeError("invalid_arguments", "Unsupported transform channel.")
        if operation.get("mode") not in {"add", "multiply", "set"}:
            raise BridgeError("invalid_arguments", "Unsupported transform mode.")
        values = operation.get("values")
        if not isinstance(values, list) or len(values) != dimensions[channel]:
            raise BridgeError(
                "invalid_arguments",
                f"{channel} requires {dimensions[channel]} numeric values.",
            )
        for index, item in enumerate(values):
            _optional_number(item, f"values[{index}]")
        units = operation.get("units", "radians")
        if channel == "rotate_euler":
            if units not in {"degrees", "radians"}:
                raise BridgeError(
                    "invalid_arguments",
                    "rotate_euler units must be degrees or radians.",
                )
        elif "units" in operation:
            raise BridgeError(
                "invalid_arguments",
                "units is only valid for rotate_euler.",
            )
        _frame_range(operation)
        return operation

    if operation_type == "time":
        _reject_unknown(
            operation,
            {"type", "scale", "offset", "pivot", "frame_start", "frame_end"},
            "time operation",
        )
        scale = operation.get("scale", 1.0)
        scale_value = _optional_number(scale, "scale")
        if scale_value is None or not 0.001 <= scale_value <= 100.0:
            raise BridgeError("invalid_arguments", "time scale must be from 0.001 to 100.")
        _optional_number(operation.get("offset", 0.0), "offset")
        _optional_number(operation.get("pivot", 0.0), "pivot")
        _frame_range(operation)
        return operation

    if operation_type == "interpolation":
        _reject_unknown(
            operation,
            {"type", "value", "frame_start", "frame_end"},
            "interpolation operation",
        )
        if operation.get("value") not in {"CONSTANT", "LINEAR", "BEZIER"}:
            raise BridgeError(
                "invalid_arguments",
                "interpolation value must be CONSTANT, LINEAR, or BEZIER.",
            )
        _frame_range(operation)
        return operation

    raise BridgeError(
        "invalid_arguments",
        "operation type must be transform, time, or interpolation.",
    )


def _bounded_number(
    value: Any,
    label: str,
    minimum: float,
    maximum: float,
) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise BridgeError("invalid_arguments", f"{label} must be a number.")
    result = float(value)
    if not math.isfinite(result) or not minimum <= result <= maximum:
        raise BridgeError(
            "invalid_arguments",
            f"{label} must be between {minimum} and {maximum}.",
        )
    return result


def _bounded_integer(
    value: Any,
    label: str,
    minimum: int,
    maximum: int,
) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise BridgeError("invalid_arguments", f"{label} must be an integer.")
    if not minimum <= value <= maximum:
        raise BridgeError(
            "invalid_arguments",
            f"{label} must be between {minimum} and {maximum}.",
        )
    return value


def _boolean(value: Any, label: str) -> bool:
    if not isinstance(value, bool):
        raise BridgeError("invalid_arguments", f"{label} must be boolean.")
    return value


def _vector(
    value: Any,
    label: str,
    length: int,
    minimum: float,
    maximum: float,
) -> list[float]:
    if not isinstance(value, list) or len(value) != length:
        raise BridgeError(
            "invalid_arguments",
            f"{label} must contain exactly {length} numbers.",
        )
    return [
        _bounded_number(item, f"{label}[{index}]", minimum, maximum)
        for index, item in enumerate(value)
    ]


def _require_copy_contract(args: Mapping[str, Any]) -> None:
    source = _name(args.get("source_object_name"), "source_object_name")
    output = _name(args.get("output_object_name"), "output_object_name")
    if source == output:
        raise BridgeError(
            "invalid_arguments",
            "output_object_name must differ from source_object_name.",
        )
    create_copy = args.get("create_copy", True)
    _boolean(create_copy, "create_copy")
    if not create_copy:
        raise BridgeError(
            "invalid_arguments",
            "create_copy=false is blocked; modeling edits must preserve the source.",
        )


def _mesh_requirements(value: Any) -> dict[str, Any]:
    requirements = _expect_object(value, "requirements")
    allowed = {
        "require_uv",
        "require_material",
        "require_manifold",
        "require_applied_scale",
        "max_triangles",
    }
    _reject_unknown(requirements, allowed, "requirements")
    for field in allowed - {"max_triangles"}:
        if field in requirements:
            _boolean(requirements[field], f"requirements.{field}")
    if "max_triangles" in requirements:
        _bounded_integer(
            requirements["max_triangles"],
            "requirements.max_triangles",
            1,
            100_000_000,
        )
    return requirements


def _validate_mesh_edit_operation(value: Any) -> dict[str, Any]:
    operation = _expect_object(value, "mesh operation")
    operation_type = operation.get("type")
    if operation_type in {"transform_object", "transform_vertices"}:
        allowed = {"type"}
        if operation_type == "transform_object":
            allowed.update({"location", "rotation_degrees", "scale"})
        else:
            allowed.update({"translation", "rotation_degrees", "scale"})
        _reject_unknown(operation, allowed, f"{operation_type} operation")
        transform_fields = allowed - {"type"}
        if not any(field in operation for field in transform_fields):
            raise BridgeError(
                "invalid_arguments",
                f"{operation_type} requires at least one transform field.",
            )
        for field in transform_fields:
            if field in operation:
                values = _vector(
                    operation[field],
                    field,
                    3,
                    -1_000_000.0,
                    1_000_000.0,
                )
                if field == "scale" and any(abs(item) <= 1e-9 for item in values):
                    raise BridgeError(
                        "invalid_arguments",
                        "scale values must be non-zero.",
                    )
        return operation
    if operation_type == "merge_by_distance":
        _reject_unknown(operation, {"type", "distance"}, "merge_by_distance operation")
        _bounded_number(
            operation.get("distance", 0.0001),
            "distance",
            0.000000001,
            10.0,
        )
        return operation
    if operation_type in {
        "recalculate_normals",
        "triangulate",
        "delete_loose",
    }:
        _reject_unknown(operation, {"type"}, f"{operation_type} operation")
        return operation
    if operation_type == "shade_smooth":
        _reject_unknown(operation, {"type", "value"}, "shade_smooth operation")
        if "value" in operation:
            _boolean(operation["value"], "value")
        return operation
    raise BridgeError(
        "invalid_arguments",
        (
            "mesh operation type must be transform_object, transform_vertices, "
            "merge_by_distance, recalculate_normals, triangulate, delete_loose, "
            "or shade_smooth."
        ),
    )


def _validate_modifier(value: Any) -> dict[str, Any]:
    modifier = _expect_object(value, "modifier")
    modifier_type = modifier.get("type")
    if modifier_type == "bevel":
        _reject_unknown(
            modifier,
            {"type", "width", "segments", "angle_degrees"},
            "bevel modifier",
        )
        _bounded_number(modifier.get("width", 0.01), "width", 0.000000001, 1_000_000)
        _bounded_integer(modifier.get("segments", 2), "segments", 1, 64)
        _bounded_number(modifier.get("angle_degrees", 30.0), "angle_degrees", 0, 180)
    elif modifier_type == "subdivision":
        _reject_unknown(
            modifier,
            {"type", "levels", "render_levels"},
            "subdivision modifier",
        )
        levels = _bounded_integer(modifier.get("levels", 1), "levels", 0, 6)
        render_levels = _bounded_integer(
            modifier.get("render_levels", levels),
            "render_levels",
            0,
            8,
        )
        if render_levels < levels:
            raise BridgeError(
                "invalid_arguments",
                "render_levels must be greater than or equal to levels.",
            )
    elif modifier_type == "solidify":
        _reject_unknown(
            modifier,
            {"type", "thickness", "offset"},
            "solidify modifier",
        )
        _bounded_number(
            modifier.get("thickness", 0.01),
            "thickness",
            -1_000_000,
            1_000_000,
        )
        _bounded_number(modifier.get("offset", 0.0), "offset", -1.0, 1.0)
    elif modifier_type == "decimate":
        _reject_unknown(modifier, {"type", "ratio"}, "decimate modifier")
        _bounded_number(modifier.get("ratio", 0.5), "ratio", 0.01, 1.0)
    elif modifier_type == "mirror":
        _reject_unknown(
            modifier,
            {"type", "axes", "use_clip"},
            "mirror modifier",
        )
        axes = modifier.get("axes", [True, False, False])
        if not isinstance(axes, list) or len(axes) != 3:
            raise BridgeError(
                "invalid_arguments",
                "axes must contain exactly three booleans.",
            )
        for index, enabled in enumerate(axes):
            _boolean(enabled, f"axes[{index}]")
        if not any(axes):
            raise BridgeError("invalid_arguments", "At least one mirror axis is required.")
        if "use_clip" in modifier:
            _boolean(modifier["use_clip"], "use_clip")
    elif modifier_type == "triangulate":
        _reject_unknown(modifier, {"type"}, "triangulate modifier")
    else:
        raise BridgeError(
            "invalid_arguments",
            (
                "modifier type must be bevel, subdivision, solidify, decimate, "
                "mirror, or triangulate."
            ),
        )
    return modifier


def _validate_modeling_command(command: str, args: dict[str, Any]) -> bool:
    if command == "scene.inspect":
        _reject_unknown(args, {"include_hidden", "max_objects"}, "args")
        if "include_hidden" in args:
            _boolean(args["include_hidden"], "include_hidden")
        if "max_objects" in args:
            _bounded_integer(args["max_objects"], "max_objects", 1, 2000)
    elif command == "model.import":
        _reject_unknown(args, {"path", "name_prefix"}, "args")
        if "path" not in args:
            raise BridgeError("invalid_arguments", "path is required.")
        _name(args.get("name_prefix"), "name_prefix")
    elif command == "mesh.inspect":
        _reject_unknown(args, {"object_name", "requirements"}, "args")
        _name(args.get("object_name"), "object_name")
        if "requirements" in args:
            _mesh_requirements(args["requirements"])
    elif command == "mesh.edit":
        _reject_unknown(
            args,
            {
                "source_object_name",
                "output_object_name",
                "create_copy",
                "operations",
            },
            "args",
        )
        _require_copy_contract(args)
        operations = args.get("operations")
        if not isinstance(operations, list) or not 1 <= len(operations) <= 32:
            raise BridgeError(
                "invalid_arguments",
                "operations must contain between 1 and 32 typed mesh operations.",
            )
        for operation in operations:
            _validate_mesh_edit_operation(operation)
    elif command == "modifier.apply":
        _reject_unknown(
            args,
            {
                "source_object_name",
                "output_object_name",
                "create_copy",
                "modifier",
            },
            "args",
        )
        _require_copy_contract(args)
        _validate_modifier(args.get("modifier"))
    elif command == "material.assign":
        _reject_unknown(
            args,
            {
                "source_object_name",
                "output_object_name",
                "create_copy",
                "material_name",
                "create_material",
                "base_color",
                "metallic",
                "roughness",
                "replace_slot",
            },
            "args",
        )
        _require_copy_contract(args)
        _name(args.get("material_name"), "material_name")
        create_material = args.get("create_material", False)
        _boolean(create_material, "create_material")
        material_properties = {"base_color", "metallic", "roughness"}
        if not create_material and any(field in args for field in material_properties):
            raise BridgeError(
                "invalid_arguments",
                "Material properties are only valid with create_material=true.",
            )
        if "base_color" in args:
            _vector(args["base_color"], "base_color", 4, 0.0, 1.0)
        if "metallic" in args:
            _bounded_number(args["metallic"], "metallic", 0.0, 1.0)
        if "roughness" in args:
            _bounded_number(args["roughness"], "roughness", 0.0, 1.0)
        if "replace_slot" in args:
            _bounded_integer(args["replace_slot"], "replace_slot", 0, 1024)
    elif command == "uv.inspect":
        _reject_unknown(args, {"object_name", "uv_layer_name"}, "args")
        _name(args.get("object_name"), "object_name")
        if "uv_layer_name" in args:
            _name(args["uv_layer_name"], "uv_layer_name")
    elif command == "uv.edit":
        _reject_unknown(
            args,
            {
                "source_object_name",
                "output_object_name",
                "create_copy",
                "operation",
                "uv_layer_name",
                "create_layer",
                "angle_limit_degrees",
                "island_margin",
                "cube_size",
            },
            "args",
        )
        _require_copy_contract(args)
        operation = args.get("operation")
        if operation not in {"smart_project", "cube_project", "unwrap", "pack_islands"}:
            raise BridgeError(
                "invalid_arguments",
                "operation must be smart_project, cube_project, unwrap, or pack_islands.",
            )
        if "uv_layer_name" in args:
            _name(args["uv_layer_name"], "uv_layer_name")
        if "create_layer" in args:
            _boolean(args["create_layer"], "create_layer")
        if operation == "pack_islands" and args.get("create_layer", False):
            raise BridgeError(
                "invalid_arguments",
                "pack_islands cannot create an empty UV layer.",
            )
        if "angle_limit_degrees" in args:
            if operation != "smart_project":
                raise BridgeError(
                    "invalid_arguments",
                    "angle_limit_degrees is only valid for smart_project.",
                )
            _bounded_number(
                args["angle_limit_degrees"],
                "angle_limit_degrees",
                1.0,
                89.0,
            )
        if "island_margin" in args:
            if operation == "cube_project":
                raise BridgeError(
                    "invalid_arguments",
                    "island_margin is not valid for cube_project.",
                )
            _bounded_number(args["island_margin"], "island_margin", 0.0, 0.5)
        if "cube_size" in args:
            if operation != "cube_project":
                raise BridgeError(
                    "invalid_arguments",
                    "cube_size is only valid for cube_project.",
                )
            _bounded_number(args["cube_size"], "cube_size", 0.000001, 1_000_000)
    elif command == "model.export":
        _reject_unknown(
            args,
            {"path", "object_names", "overwrite", "apply_modifiers"},
            "args",
        )
        if "path" not in args:
            raise BridgeError("invalid_arguments", "path is required.")
        object_names = args.get("object_names")
        if not isinstance(object_names, list) or not 1 <= len(object_names) <= 256:
            raise BridgeError(
                "invalid_arguments",
                "object_names must contain between 1 and 256 names.",
            )
        normalized = [_name(item, "object_name") for item in object_names]
        if len(set(normalized)) != len(normalized):
            raise BridgeError(
                "invalid_arguments",
                "object_names must not contain duplicates.",
            )
        if "overwrite" in args:
            _boolean(args["overwrite"], "overwrite")
        if "apply_modifiers" in args:
            _boolean(args["apply_modifiers"], "apply_modifiers")
    else:
        return False
    return True


def validate_command(command: Any, arguments: Any) -> dict[str, Any]:
    if command not in COMMANDS:
        raise BridgeError("unknown_command", f"Unsupported command: {command}")
    args = _expect_object(arguments, "args")

    if _validate_modeling_command(command, args):
        return args

    if command == "status":
        _reject_unknown(args, set(), "args")
    elif command == "job.status":
        _reject_unknown(args, {"job_id"}, "args")
        _name(args.get("job_id"), "job_id")
    elif command == "animation.import_fbx":
        _reject_unknown(
            args,
            {
                "path",
                "name_prefix",
                "root_bone_name",
                "expected_bone_hierarchy",
                "timing_contract",
            },
            "args",
        )
        if "path" not in args:
            raise BridgeError("invalid_arguments", "path is required.")
        if "name_prefix" in args:
            _name(args["name_prefix"], "name_prefix")
        if "root_bone_name" in args:
            _name(args["root_bone_name"], "root_bone_name")
        if "expected_bone_hierarchy" in args:
            hierarchy = _bone_hierarchy(args["expected_bone_hierarchy"])
            if "root_bone_name" not in args:
                raise BridgeError(
                    "invalid_arguments",
                    "root_bone_name is required with expected_bone_hierarchy.",
                )
            expected_root = next(
                item["name"] for item in hierarchy if item["parent"] is None
            )
            if args["root_bone_name"] != expected_root:
                raise BridgeError(
                    "invalid_arguments",
                    "root_bone_name must match the hierarchy root.",
                )
            if "timing_contract" not in args:
                raise BridgeError(
                    "invalid_arguments",
                    "timing_contract is required for an Unreal hierarchy roundtrip.",
                )
        if "timing_contract" in args:
            _timing_contract(args["timing_contract"])
    elif command == "animation.inspect":
        _reject_unknown(
            args,
            {"armature_name", "action_name", "action_slot", "timing_contract"},
            "args",
        )
        _name(args.get("armature_name"), "armature_name")
        if "action_name" in args:
            _name(args["action_name"], "action_name")
        if "action_slot" in args:
            _name(args["action_slot"], "action_slot")
        if "timing_contract" in args:
            _timing_contract(args["timing_contract"])
    elif command == "animation.adjust":
        _reject_unknown(
            args,
            {
                "armature_name",
                "action_name",
                "operations",
                "create_copy",
                "output_action_name",
                "action_slot",
            },
            "args",
        )
        _name(args.get("armature_name"), "armature_name")
        _name(args.get("action_name"), "action_name")
        if "action_slot" in args:
            _name(args["action_slot"], "action_slot")
        operations = args.get("operations")
        if not isinstance(operations, list) or not 1 <= len(operations) <= 100:
            raise BridgeError(
                "invalid_arguments",
                "operations must contain between 1 and 100 typed operations.",
            )
        for operation in operations:
            _validate_operation(operation)
        create_copy = args.get("create_copy", True)
        if not isinstance(create_copy, bool):
            raise BridgeError("invalid_arguments", "create_copy must be boolean.")
        if not create_copy:
            raise BridgeError(
                "invalid_arguments",
                "create_copy=false is blocked; Revvy edits must be transactional.",
            )
        if "output_action_name" in args:
            _name(args["output_action_name"], "output_action_name")
    elif command == "animation.export_fbx":
        _reject_unknown(
            args,
            {
                "path",
                "armature_name",
                "action_name",
                "action_slot",
                "mesh_names",
                "overwrite",
                "timing_contract",
            },
            "args",
        )
        if "path" not in args:
            raise BridgeError("invalid_arguments", "path is required.")
        _name(args.get("armature_name"), "armature_name")
        if "action_name" in args:
            _name(args["action_name"], "action_name")
        if "action_slot" in args:
            _name(args["action_slot"], "action_slot")
        meshes = args.get("mesh_names", [])
        if not isinstance(meshes, list) or len(meshes) > 256:
            raise BridgeError("invalid_arguments", "mesh_names must be a list.")
        normalized = [_name(item, "mesh_name") for item in meshes]
        if len(set(normalized)) != len(normalized):
            raise BridgeError("invalid_arguments", "mesh_names must not contain duplicates.")
        if not isinstance(args.get("overwrite", False), bool):
            raise BridgeError("invalid_arguments", "overwrite must be boolean.")
        if "timing_contract" in args:
            _timing_contract(args["timing_contract"])
    elif command == "file.save_copy":
        _reject_unknown(args, {"path", "overwrite"}, "args")
        if "path" not in args:
            raise BridgeError("invalid_arguments", "path is required.")
        if not isinstance(args.get("overwrite", False), bool):
            raise BridgeError("invalid_arguments", "overwrite must be boolean.")
    return args


def validate_request(payload: Any) -> tuple[Any, str, str, dict[str, Any], bool]:
    request = _expect_object(payload, "request")
    _reject_unknown(request, {"id", "token", "command", "args", "defer"}, "request")
    request_id = request.get("id")
    if isinstance(request_id, bool) or not isinstance(request_id, (str, int)):
        raise BridgeError("invalid_request", "id must be a string or integer.")
    token = request.get("token")
    if not isinstance(token, str):
        raise BridgeError("unauthorized", "A bridge token is required.")
    command = request.get("command")
    if not isinstance(command, str):
        raise BridgeError("invalid_request", "command must be a string.")
    deferred = request.get("defer", False)
    if not isinstance(deferred, bool):
        raise BridgeError("invalid_request", "defer must be boolean.")
    args = validate_command(command, request.get("args", {}))
    return request_id, token, command, args, deferred
