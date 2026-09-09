"""Pure helpers for Revvy's production Niagara asset pipeline."""

from __future__ import annotations

import math
import pathlib
import re
import struct
from typing import Any, Iterable

try:
    from PIL import Image, ImageChops, ImageStat
except ImportError:  # Unreal's embedded Python may not ship Pillow.
    Image = ImageChops = ImageStat = None


TEXTURE_USAGES = (
    "sprite",
    "additive",
    "mask",
    "tileable_noise",
    "flowmap",
    "normal",
    "flipbook",
)
VFX_PHASES = ("anticipation", "emission", "body", "impact", "decay")
MESH_PRIMITIVES = ("box", "sphere", "cylinder", "torus", "static_mesh")
MESH_OPERATIONS = ("append", "subtract", "intersect")
_ASSET_NAME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9_]*$")


class VfxSpecError(ValueError):
    pass


def _issue(code: str, severity: str, message: str, penalty: int = 0) -> dict[str, Any]:
    return {
        "code": code,
        "severity": severity,
        "message": message,
        "penalty": penalty,
    }


def _finite_number(value: Any, field: str, minimum: float, maximum: float) -> float:
    try:
        result = float(value)
    except (TypeError, ValueError) as exc:
        raise VfxSpecError(f"{field} must be numeric") from exc
    if not math.isfinite(result) or not minimum <= result <= maximum:
        raise VfxSpecError(f"{field} must be between {minimum} and {maximum}")
    return result


def validate_asset_path(path: Any, prefix: str, roots: tuple[str, ...] = ("/Game/",)) -> str:
    value = str(path or "").strip().rstrip("/")
    if not any(value.startswith(root) for root in roots):
        raise VfxSpecError(f"asset path must start with one of: {', '.join(roots)}")
    name = value.rsplit("/", 1)[-1]
    if (prefix and not name.startswith(prefix)) or not _ASSET_NAME_RE.fullmatch(name):
        expected = f"{prefix}<letters, numbers, underscores>" if prefix else "letters, numbers, and underscores"
        raise VfxSpecError(f"asset name must use {expected}")
    return value


def validate_content_folder(path: Any) -> str:
    value = str(path or "").strip().rstrip("/")
    if not value.startswith("/Game/") or "." in value or ".." in value.split("/"):
        raise VfxSpecError("destination_path must be a /Game/... folder")
    return value


def validate_texture_name(name: Any) -> str:
    value = str(name or "").strip()
    if not value.startswith("T_") or not _ASSET_NAME_RE.fullmatch(value):
        raise VfxSpecError("asset_name must be T_<letters, numbers, underscores>")
    return value


def build_texture_prompt(
    subject: str,
    usage: str = "sprite",
    *,
    style: str = "production game VFX",
    size: int = 1024,
    grid: tuple[int, int] = (1, 1),
    correction: str = "",
) -> str:
    usage = str(usage).strip().lower()
    if usage not in TEXTURE_USAGES:
        raise VfxSpecError(f"usage must be one of: {', '.join(TEXTURE_USAGES)}")
    subject = str(subject).strip()
    if not 3 <= len(subject) <= 1200:
        raise VfxSpecError("subject must contain 3 to 1200 characters")
    if size not in (512, 1024):
        raise VfxSpecError("VFX texture size must be 512 or 1024")
    columns, rows = grid
    if usage == "flipbook":
        if not (2 <= columns <= 8 and 2 <= rows <= 8 and columns * rows <= 36):
            raise VfxSpecError("flipbook grid must contain 4 to 36 cells, with each axis <= 8")
    else:
        columns, rows = 1, 1

    common = (
        f"Create one exact {size}x{size} PNG texture for Unreal Engine Niagara. "
        f"Subject: {subject}. Art direction: {style}. "
        "No text, letters, logo, watermark, frame, drop shadow, perspective scene, "
        "environment, UI, baked lens flare, or baked bloom. Keep the texture centered, "
        "uncropped, and usable as raw VFX source art."
    )
    profiles = {
        "sprite": (
            " Isolated particle sprite on a fully transparent RGBA background. Preserve a clean "
            "alpha silhouette, soft outer falloff, detailed interior value variation, and at least "
            "8 percent empty transparent padding on every edge."
        ),
        "additive": (
            " Additive particle sprite on exact pure black RGB(0,0,0). Use a bright detailed core "
            "that smoothly falls to absolute black at every edge. Do not encode ambient lighting."
        ),
        "mask": (
            " Single-channel linear grayscale mask represented in RGB. Use the full 0-255 range, "
            "neutral flat lighting, crisp intentional shapes, and no color cast or gamma styling."
        ),
        "tileable_noise": (
            " Seamless tileable grayscale noise. Opposite borders must match pixel-for-pixel in "
            "structure and value. Use broad, medium, and fine frequency detail with full range."
        ),
        "flowmap": (
            " Seamless tangent-space 2D flow map. Encode horizontal direction in red and vertical "
            "direction in green around neutral 0.5, keep blue black, and make opposite borders match."
        ),
        "normal": (
            " Tangent-space normal map only: neutral purple-blue base, +X in red, +Y in green, "
            "+Z in blue. No shaded beauty render, albedo, highlights, or ambient occlusion."
        ),
        "flipbook": (
            f" Sprite sheet with an exact {columns} columns by {rows} rows grid and no gutters. "
            "Every cell uses identical framing and scale. Animate a coherent birth, peak, breakup, "
            "and dissipating decay sequence left-to-right, top-to-bottom, on pure black background. "
            "No element may cross a cell boundary."
        ),
    }
    correction_text = f" Correct these prior validation problems: {correction}." if correction else ""
    return common + profiles[usage] + correction_text


def _png_dimensions(path: pathlib.Path) -> tuple[int, int]:
    with path.open("rb") as handle:
        header = handle.read(24)
    if len(header) < 24 or header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        raise VfxSpecError("generated file is not a valid PNG")
    return struct.unpack(">II", header[16:24])


def _mean(values: Iterable[float]) -> float:
    items = list(values)
    return sum(items) / len(items) if items else 0.0


def inspect_texture_file(
    path: str | pathlib.Path,
    usage: str,
    *,
    expected_size: int = 1024,
    grid: tuple[int, int] = (1, 1),
    threshold: int = 72,
) -> dict[str, Any]:
    target = pathlib.Path(path)
    usage = str(usage).strip().lower()
    if usage not in TEXTURE_USAGES:
        raise VfxSpecError(f"usage must be one of: {', '.join(TEXTURE_USAGES)}")
    if not target.is_file():
        return {
            "passed": False,
            "score": 0,
            "issues": [_issue("missing_file", "error", f"File not found: {target}", 100)],
        }

    issues: list[dict[str, Any]] = []
    try:
        width, height = _png_dimensions(target)
    except (OSError, VfxSpecError) as exc:
        return {
            "passed": False,
            "score": 0,
            "issues": [_issue("invalid_png", "error", str(exc), 100)],
        }

    if width != expected_size or height != expected_size:
        issues.append(
            _issue(
                "wrong_dimensions",
                "error",
                f"Expected {expected_size}x{expected_size}, got {width}x{height}",
                35,
            )
        )
    if width & (width - 1) or height & (height - 1):
        issues.append(_issue("not_power_of_two", "error", "Texture dimensions are not power-of-two", 20))
    if usage == "flipbook" and (width % grid[0] or height % grid[1]):
        issues.append(_issue("invalid_grid", "error", "Image dimensions do not divide evenly by the grid", 30))

    if Image is None:
        issues.append(
            _issue(
                "limited_validation",
                "warning",
                "Pillow is unavailable; pixel-level VFX quality checks were skipped",
                8,
            )
        )
    else:
        try:
            with Image.open(target) as opened:
                image = opened.convert("RGBA")
                image.thumbnail((256, 256))
                rgba = image.copy()
        except (OSError, ValueError) as exc:
            return {
                "passed": False,
                "score": 0,
                "issues": [_issue("decode_failed", "error", str(exc), 100)],
            }

        rgb = rgba.convert("RGB")
        gray = rgb.convert("L")
        alpha = rgba.getchannel("A")
        rgb_stat = ImageStat.Stat(rgb)
        gray_stat = ImageStat.Stat(gray)
        alpha_stat = ImageStat.Stat(alpha)
        extrema = gray.getextrema()
        dynamic_range = (extrema[1] - extrema[0]) / 255.0
        if dynamic_range < 0.45 or gray_stat.stddev[0] < 18:
            issues.append(_issue("flat_values", "warning", "Texture lacks useful internal value range", 12))

        border = 4
        bands = (
            rgba.crop((0, 0, rgba.width, border)),
            rgba.crop((0, rgba.height - border, rgba.width, rgba.height)),
            rgba.crop((0, 0, border, rgba.height)),
            rgba.crop((rgba.width - border, 0, rgba.width, rgba.height)),
        )
        border_alpha = _mean(ImageStat.Stat(b.getchannel("A")).mean[0] / 255.0 for b in bands)
        border_luma = _mean(ImageStat.Stat(b.convert("L")).mean[0] / 255.0 for b in bands)

        if usage == "sprite":
            if alpha.getextrema() == (255, 255):
                issues.append(_issue("missing_alpha", "error", "Sprite has no transparent alpha", 35))
            elif border_alpha > 0.06:
                issues.append(_issue("alpha_touches_border", "error", "Sprite alpha reaches the image border", 22))
            opaque_fraction = sum(1 for value in alpha.getdata() if value > 24) / (alpha.width * alpha.height)
            if not 0.04 <= opaque_fraction <= 0.86:
                issues.append(_issue("bad_coverage", "warning", "Sprite subject coverage is too small or cropped", 10))

        if usage in ("additive", "flipbook") and border_luma > 0.035:
            issues.append(_issue("non_black_border", "error", "Additive texture does not fall to pure black", 24))

        if usage == "mask":
            channel_spread = _mean(
                abs(rgb_stat.mean[index] - rgb_stat.mean[0]) / 255.0 for index in (1, 2)
            )
            if channel_spread > 0.03:
                issues.append(_issue("mask_color_cast", "warning", "Mask contains a color cast", 12))
            if extrema[0] > 12 or extrema[1] < 243:
                issues.append(_issue("mask_range", "warning", "Mask does not use the full black-white range", 10))

        if usage in ("tileable_noise", "flowmap"):
            left = rgb.crop((0, 0, 1, rgb.height))
            right = rgb.crop((rgb.width - 1, 0, rgb.width, rgb.height))
            top = rgb.crop((0, 0, rgb.width, 1))
            bottom = rgb.crop((0, rgb.height - 1, rgb.width, rgb.height))
            lr = _mean(ImageStat.Stat(ImageChops.difference(left, right)).mean) / 255.0
            tb = _mean(ImageStat.Stat(ImageChops.difference(top, bottom)).mean) / 255.0
            if max(lr, tb) > 0.08:
                issues.append(_issue("visible_tile_seam", "error", "Opposite texture borders do not match", 25))

        if usage == "normal":
            means = [value / 255.0 for value in rgb_stat.mean]
            if not (0.35 <= means[0] <= 0.65 and 0.35 <= means[1] <= 0.65 and means[2] >= 0.62):
                issues.append(_issue("invalid_normal_bias", "error", "Image is not centered around a tangent-space normal", 28))

        if usage == "flowmap":
            if rgb_stat.stddev[0] < 8 or rgb_stat.stddev[1] < 8:
                issues.append(_issue("flat_flow", "error", "Flow map has insufficient directional variation", 24))

    score = max(0, 100 - sum(int(item["penalty"]) for item in issues))
    blockers = any(item["severity"] == "error" for item in issues)
    return {
        "passed": score >= int(threshold) and not blockers,
        "score": score,
        "threshold": int(threshold),
        "usage": usage,
        "width": width,
        "height": height,
        "issues": issues,
    }


def _vector3(value: Any, field: str, default: tuple[float, float, float]) -> list[float]:
    source = default if value is None else value
    if not isinstance(source, (list, tuple)) or len(source) != 3:
        raise VfxSpecError(f"{field} must be a three-number array")
    return [_finite_number(component, field, -100000.0, 100000.0) for component in source]


def _positive_vector3(value: Any, field: str, default: tuple[float, float, float]) -> list[float]:
    source = default if value is None else value
    if not isinstance(source, (list, tuple)) or len(source) != 3:
        raise VfxSpecError(f"{field} must be a three-number array")
    return [_finite_number(component, field, 0.01, 100000.0) for component in source]


def validate_mesh_recipe(recipe: Any) -> dict[str, Any]:
    if not isinstance(recipe, dict):
        raise VfxSpecError("mesh recipe must be an object")
    output = validate_asset_path(recipe.get("path"), "SM_")
    parts = recipe.get("parts")
    if not isinstance(parts, list) or not 1 <= len(parts) <= 32:
        raise VfxSpecError("mesh recipe requires 1 to 32 parts")

    normalized_parts = []
    for index, raw in enumerate(parts):
        if not isinstance(raw, dict):
            raise VfxSpecError(f"parts[{index}] must be an object")
        primitive = str(raw.get("type", "")).strip().lower()
        operation = str(raw.get("operation", "append")).strip().lower()
        if primitive not in MESH_PRIMITIVES:
            raise VfxSpecError(f"parts[{index}].type must be one of: {', '.join(MESH_PRIMITIVES)}")
        if operation not in MESH_OPERATIONS or (index == 0 and operation != "append"):
            raise VfxSpecError(f"parts[{index}].operation is invalid")
        part = {
            "type": primitive,
            "operation": operation,
            "location": _vector3(raw.get("location"), f"parts[{index}].location", (0, 0, 0)),
            "rotation": _vector3(raw.get("rotation"), f"parts[{index}].rotation", (0, 0, 0)),
            "scale": _vector3(raw.get("scale"), f"parts[{index}].scale", (1, 1, 1)),
        }
        if any(abs(value) < 0.0001 for value in part["scale"]):
            raise VfxSpecError(f"parts[{index}].scale cannot contain zero")
        if primitive == "box":
            part["dimensions"] = _positive_vector3(raw.get("dimensions"), f"parts[{index}].dimensions", (10, 10, 10))
            raw_steps = raw.get("steps", (1, 1, 1))
            if not isinstance(raw_steps, (list, tuple)) or len(raw_steps) != 3:
                raise VfxSpecError(f"parts[{index}].steps must be a three-integer array")
            part["steps"] = [max(1, min(32, int(value))) for value in raw_steps]
        elif primitive == "sphere":
            part["radius"] = _finite_number(raw.get("radius", 50), f"parts[{index}].radius", 0.01, 100000)
            part["steps"] = max(4, min(128, int(raw.get("steps", 16))))
        elif primitive == "cylinder":
            part["radius"] = _finite_number(raw.get("radius", 50), f"parts[{index}].radius", 0.01, 100000)
            part["height"] = _finite_number(raw.get("height", 10), f"parts[{index}].height", 0.01, 100000)
            part["radial_steps"] = max(3, min(256, int(raw.get("radial_steps", 32))))
            part["height_steps"] = max(1, min(64, int(raw.get("height_steps", 1))))
        elif primitive == "torus":
            part["major_radius"] = _finite_number(raw.get("major_radius", 50), f"parts[{index}].major_radius", 0.01, 100000)
            part["minor_radius"] = _finite_number(raw.get("minor_radius", 3), f"parts[{index}].minor_radius", 0.01, part["major_radius"])
            part["major_steps"] = max(8, min(256, int(raw.get("major_steps", 64))))
            part["minor_steps"] = max(3, min(64, int(raw.get("minor_steps", 8))))
            part["revolve_degrees"] = _finite_number(
                raw.get("revolve_degrees", 360.0),
                f"parts[{index}].revolve_degrees",
                1.0,
                360.0,
            )
            part["degree_offset"] = _finite_number(
                raw.get("degree_offset", 0.0),
                f"parts[{index}].degree_offset",
                -360000.0,
                360000.0,
            )
            part["fill_end_caps"] = bool(raw.get("fill_end_caps", True))
        else:
            part["asset_path"] = validate_asset_path(
                raw.get("asset_path"), "", roots=("/Game/", "/Engine/", "/Revvy/")
            )
            part["lod"] = max(0, min(7, int(raw.get("lod", 0))))
        normalized_parts.append(part)

    return {
        "path": output,
        "parts": normalized_parts,
        "replace_existing": bool(recipe.get("replace_existing", False)),
    }


def find_rapid_iteration_parameter(
    parameters: Iterable[Any],
    emitter_name: str,
    module_name: str,
    input_name: str,
) -> str:
    """Resolve a display/internal-name pair to its exact Niagara RI parameter."""

    def normalized(value: Any) -> str:
        return re.sub(r"[^a-z0-9]", "", str(value or "").casefold())

    expected = normalized(
        f"Constants.{emitter_name}.{module_name}.{input_name}"
    )
    for parameter in parameters or []:
        if isinstance(parameter, dict):
            name = parameter.get("parameter_name", "")
        else:
            name = getattr(parameter, "parameter_name", "")
        if normalized(name) == expected:
            return str(name)
    return ""


def assess_niagara_observation(
    observation: dict[str, Any],
    *,
    profile: str = "combat",
    threshold: int = 78,
) -> dict[str, Any]:
    def scalar_number(value: Any) -> float | None:
        match = re.fullmatch(
            r"\s*([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)\s*",
            str(value or ""),
        )
        if not match:
            return None
        number = float(match.group(1))
        return number if math.isfinite(number) else None

    issues: list[dict[str, Any]] = []
    compile_info = observation.get("compile") or {}
    if not compile_info.get("success", False):
        issues.append(_issue("compile_failed", "error", "Niagara compilation failed", 60))
    warning_count = int(compile_info.get("warning_count", 0) or 0)
    if warning_count:
        issues.append(_issue("compile_warnings", "warning", f"Compiler reported {warning_count} warning(s)", min(12, warning_count * 2)))

    emitters = observation.get("emitters") or []
    emitter_count = len(emitters)
    minimum = 3 if profile in ("combat", "cinematic") else 1
    if emitter_count < minimum:
        issues.append(_issue("thin_layering", "warning", f"Profile expects at least {minimum} purposeful emitter layers", 14))
    if emitter_count > 8:
        issues.append(_issue("emitter_overhead", "warning", "More than 8 emitters increases fixed system overhead", 8))

    phases = {str(value).lower() for value in observation.get("phases", [])}
    required_phases = {"emission", "decay"}
    if profile in ("combat", "cinematic"):
        required_phases.update(("impact", "body"))
    missing_phases = sorted(required_phases - phases)
    if missing_phases:
        issues.append(_issue("missing_phases", "warning", f"Missing timing phases: {', '.join(missing_phases)}", 12))

    renderer_types: set[str] = set()
    moving_emitters = 0
    has_live_timing = any("spawn_time" in emitter for emitter in emitters)
    observed_spawn_times: list[float] = []
    expected_lifecycle = str((observation.get("system") or {}).get("lifecycle", "")).casefold()
    for emitter in emitters:
        modules = {str(name).casefold() for name in emitter.get("modules", [])}
        has_motion_module = any(
            token in module
            for module in modules
            for token in ("velocity", "force", "curl", "drag", "solve", "rotation")
        )
        emitter_name = str(emitter.get("name", ""))
        normalized_emitter_name = re.sub(r"[^a-z0-9]", "", emitter_name.casefold())
        active_renderers = [
            renderer
            for renderer in emitter.get("renderers", [])
            if renderer.get("enabled", True)
        ]
        if expected_lifecycle == "one_shot":
            if not bool(emitter.get("lifecycle_resolved", False)):
                issues.append(
                    _issue(
                        "unresolved_lifecycle",
                        "error",
                        f"{emitter_name} lifecycle static switches could not be resolved",
                        32,
                    )
                )
            else:
                life_cycle_mode = str(emitter.get("life_cycle_mode", "")).casefold()
                loop_behavior = str(emitter.get("loop_behavior", "")).casefold()
                inactive_response = str(emitter.get("inactive_response", "")).casefold()
                if life_cycle_mode != "self" or loop_behavior != "once":
                    issues.append(
                        _issue(
                            "non_terminating_one_shot",
                            "error",
                            f"{emitter_name} is {emitter.get('life_cycle_mode')}/{emitter.get('loop_behavior')}; one-shot emitters require Self/Once",
                            36,
                        )
                    )
                if inactive_response and "complete" not in inactive_response:
                    issues.append(
                        _issue(
                            "one_shot_inactive_response",
                            "error",
                            f"{emitter_name} inactive response does not complete the emitter",
                            24,
                        )
                    )
        for module_name, inputs in (emitter.get("module_inputs") or {}).items():
            if not isinstance(inputs, dict):
                continue
            casefolded_inputs = {
                str(input_name).casefold(): (str(input_name), value)
                for input_name, value in inputs.items()
            }
            for input_name, minimum_value in inputs.items():
                match = re.fullmatch(r"(.+?)\s+min", str(input_name), re.IGNORECASE)
                if not match:
                    continue
                range_name = match.group(1)
                maximum_entry = casefolded_inputs.get(f"{range_name} max".casefold())
                if maximum_entry is None:
                    continue
                minimum = scalar_number(minimum_value)
                maximum = scalar_number(maximum_entry[1])
                if minimum is not None and maximum is not None and minimum > maximum:
                    issues.append(
                        _issue(
                            "inverted_numeric_range",
                            "error",
                            f"{emitter_name}/{module_name} {range_name} Min {minimum:g} exceeds Max {maximum:g}",
                            18,
                        )
                    )
        has_ribbon_motion = any(
            "ribbon" in str(renderer.get("type", "")).casefold()
            for renderer in active_renderers
        )
        has_temporal_ring_motion = any(
            renderer.get("temporal_material", False)
            and (
                "ring" in str(renderer.get("material_master", "")).casefold()
                or any(
                    token in normalized_emitter_name
                    for token in ("ring", "shockwave", "shockarc")
                )
            )
            for renderer in active_renderers
        )
        if has_motion_module or has_ribbon_motion or has_temporal_ring_motion:
            moving_emitters += 1

        if has_live_timing:
            if "spawn_time" in emitter:
                try:
                    spawn_time = float(emitter.get("spawn_time", 0.0) or 0.0)
                except (TypeError, ValueError):
                    spawn_time = 0.0
                if math.isfinite(spawn_time):
                    observed_spawn_times.append(round(spawn_time, 3))

            if profile in ("combat", "cinematic") and bool(emitter.get("continuous_spawn", False)):
                issues.append(
                    _issue(
                        "continuous_spawn_in_one_shot",
                        "error",
                        f"{emitter_name} uses continuous spawn in a one-shot {profile} effect",
                        24,
                    )
                )

            try:
                spawn_count = int(float(emitter.get("spawn_count", 0) or 0))
            except (TypeError, ValueError):
                spawn_count = 0
            if spawn_count > 64:
                issues.append(
                    _issue(
                        "particle_clutter",
                        "warning",
                        f"{emitter_name} spawns {spawn_count} particles in one burst",
                        8,
                    )
                )

            has_fade_contract = any(
                token in re.sub(r"[^a-z0-9]", "", module)
                for module in modules
                for token in ("floatfromcurve", "colorfromcurve")
            ) or any(
                renderer.get("temporal_material", False)
                for renderer in active_renderers
            )
            if not has_fade_contract:
                issues.append(
                    _issue(
                        "missing_fade_contract",
                        "error",
                        f"{emitter_name} has no curve or temporal material fade contract",
                        16,
                    )
                )

            if any(token in normalized_emitter_name for token in ("spark", "streak", "trail")):
                for renderer in active_renderers:
                    renderer_type = str(renderer.get("type", "")).casefold()
                    if "sprite" not in renderer_type:
                        continue
                    alignment = re.sub(
                        r"[^a-z0-9]",
                        "",
                        str(renderer.get("alignment", "")).casefold(),
                    )
                    if "velocityaligned" not in alignment:
                        issues.append(
                            _issue(
                                "directional_sprite_misaligned",
                                "error",
                                f"{emitter_name} sprite renderer is not velocity aligned",
                                18,
                            )
                        )

        for renderer in active_renderers:
            renderer_type = str(renderer.get("type", "Unknown"))
            renderer_types.add(renderer_type)
            material = str(renderer.get("material", ""))
            if not material:
                issues.append(_issue("missing_material", "error", f"{emitter.get('name')}/{renderer_type} has no material", 22))
            elif "/Niagara/DefaultAssets/" in material:
                issues.append(_issue("default_material", "warning", f"{emitter.get('name')} still uses a default Niagara material", 10))
            if renderer_type.casefold() == "mesh" and not renderer.get("mesh"):
                issues.append(_issue("missing_mesh", "error", f"{emitter.get('name')} mesh renderer has no particle mesh", 22))

    if has_live_timing and emitter_count >= 3 and len(set(observed_spawn_times)) < 3:
        issues.append(
            _issue(
                "flat_timing",
                "error",
                "At least three distinct emitter spawn times are required for layered timing",
                20,
            )
        )

    if profile in ("combat", "cinematic") and len(renderer_types) < 2:
        issues.append(_issue("renderer_variety", "warning", "Use at least two purposeful renderer types for silhouette depth", 8))
    if emitter_count and moving_emitters < max(1, emitter_count // 2):
        issues.append(_issue("static_motion", "warning", "Too many layers rely on static sprites or scale alone", 12))

    properties = observation.get("system") or {}
    if not properties.get("effect_type"):
        issues.append(_issue("missing_effect_type", "warning", "No Niagara Effect Type is assigned", 8))
    has_gpu = any(str(e.get("sim_target", "")).casefold().startswith("gpu") for e in emitters)
    if has_gpu and not properties.get("fixed_bounds"):
        issues.append(_issue("gpu_without_fixed_bounds", "error", "GPU emitters require verified fixed bounds", 20))

    runtime = observation.get("runtime_simulation")
    if runtime is not None:
        if not runtime.get("success", False):
            issues.append(_issue("simulation_failed", "error", "Runtime Sim Cache capture failed", 40))
        runtime_emitters = runtime.get("emitters") or {}
        for emitter in emitters:
            emitter_name = str(emitter.get("name", ""))
            evidence = runtime_emitters.get(emitter_name) or {}
            particle_count = int(evidence.get("max_particles", 0) or 0)
            if particle_count <= 0:
                issues.append(_issue("no_runtime_particles", "error", f"{emitter_name} produced no particles at the sampled times", 24))
                continue
            active_types = {
                str(renderer.get("type", "")).casefold()
                for renderer in emitter.get("renderers", [])
                if renderer.get("enabled", True)
            }
            if "sprite" in active_types and float(evidence.get("max_sprite_extent", 0) or 0) <= 0.001:
                issues.append(_issue("zero_sprite_size", "error", f"{emitter_name} produced zero-size sprites", 28))
            if "mesh" in active_types and float(evidence.get("max_mesh_scale", 0) or 0) <= 0.001:
                issues.append(_issue("zero_mesh_scale", "error", f"{emitter_name} produced zero-scale meshes", 28))
            if "ribbon" in active_types and float(evidence.get("max_ribbon_width", 0) or 0) <= 0.001:
                issues.append(_issue("zero_ribbon_width", "error", f"{emitter_name} produced zero-width ribbons", 28))
            if float(evidence.get("max_alpha", 1) or 0) <= 0.001:
                issues.append(_issue("zero_particle_alpha", "error", f"{emitter_name} particles have zero alpha", 24))

            samples = sorted(
                list(evidence.get("samples") or []),
                key=lambda sample: float(sample.get("time", 0.0) or 0.0),
            )
            emitted = False
            decayed = False
            for sample in samples:
                count = int(sample.get("particles", 0) or 0)
                if count > 0 and decayed:
                    issues.append(
                        _issue(
                            "particle_reappeared_after_decay",
                            "error",
                            f"{emitter_name} emitted particles again after reaching zero",
                            36,
                        )
                    )
                    break
                if count > 0:
                    emitted = True
                elif emitted:
                    decayed = True

        bounds_values = [
            float(value)
            for value in re.findall(
                r"[-+]?(?:\d+\.?\d*|\.\d+)",
                str(properties.get("fixed_bounds_value", "")),
            )
        ]
        if has_gpu and len(bounds_values) >= 6:
            bounds_extent = [
                max(abs(bounds_values[index]), abs(bounds_values[index + 3]))
                for index in range(3)
            ]
            runtime_extent = [0.0, 0.0, 0.0]
            for evidence in runtime_emitters.values():
                position_abs = list(evidence.get("max_position_abs") or (0.0, 0.0, 0.0))
                for index in range(min(3, len(position_abs))):
                    runtime_extent[index] = max(runtime_extent[index], float(position_abs[index]))
            if any(runtime_extent[index] > bounds_extent[index] for index in range(3)):
                issues.append(
                    _issue(
                        "runtime_outside_fixed_bounds",
                        "error",
                        f"Runtime particle positions {runtime_extent} exceed fixed bounds {bounds_extent}",
                        28,
                    )
                )

    penalties = sum(int(item["penalty"]) for item in issues)
    score = max(0, 100 - penalties)
    blockers = [item for item in issues if item["severity"] == "error"]
    return {
        "passed": score >= int(threshold) and not blockers,
        "score": score,
        "threshold": int(threshold),
        "profile": profile,
        "emitter_count": emitter_count,
        "renderer_types": sorted(renderer_types),
        "issues": issues,
        "blockers": blockers,
    }
