"""Host-side visual quality checks for captured VFX screenshots.

The analyzer compares an effect frame against a matching blank frame. It has no
Unreal dependency and deliberately uses only Pillow so it can run from Revvy's
host process or a build worker.
"""

from __future__ import annotations

import argparse
from collections import deque
import json
import math
from pathlib import Path
import sys
from typing import Any, Sequence

try:
    from PIL import Image, ImageChops, ImageOps
except ImportError:  # pragma: no cover - exercised only on hosts without Pillow.
    Image = None
    ImageChops = None
    ImageOps = None


REPORT_VERSION = 1
DEFAULT_DIFFERENCE_THRESHOLD = 12
DEFAULT_EDGE_MARGIN_FRACTION = 0.02
DEFAULT_COMPONENT_MAX_SIDE = 512
DEFAULT_SYMMETRY_MAX_SIDE = 256


class VisualQualityError(ValueError):
    """Raised when a screenshot pair cannot be analyzed safely."""


def pillow_available() -> bool:
    return Image is not None


def _require_pillow() -> None:
    if not pillow_available():
        raise VisualQualityError(
            "Pillow is required for screenshot visual quality analysis"
        )


def _validate_roi(
    roi: Sequence[int] | None,
    width: int,
    height: int,
) -> tuple[int, int, int, int]:
    if roi is None:
        return (0, 0, width, height)
    if len(roi) != 4:
        raise VisualQualityError("ROI must contain left, top, right, bottom")
    left, top, right, bottom = (int(value) for value in roi)
    if not (0 <= left < right <= width and 0 <= top < bottom <= height):
        raise VisualQualityError(
            f"ROI {(left, top, right, bottom)} is outside image size {width}x{height}"
        )
    return left, top, right, bottom


def _count_mask_pixels(mask: Any) -> int:
    histogram = mask.histogram()
    return int(histogram[255])


def _masked_histogram(image: Any, mask: Any) -> list[int]:
    return [int(value) for value in image.histogram(mask=mask)]


def _percentile(histogram: Sequence[int], percentile: float) -> float:
    total = int(sum(histogram))
    if total <= 0:
        return 0.0
    target = max(1, int(math.ceil(total * float(percentile))))
    cumulative = 0
    for value, count in enumerate(histogram):
        cumulative += int(count)
        if cumulative >= target:
            return float(value)
    return 255.0


def _histogram_mean(histogram: Sequence[int]) -> float:
    total = int(sum(histogram))
    if total <= 0:
        return 0.0
    return sum(value * int(count) for value, count in enumerate(histogram)) / total


def _signal_center_of_mass(signal: Any) -> tuple[float | None, float | None]:
    width, height = signal.size
    x_values = list(signal.resize((width, 1), Image.Resampling.BOX).getdata())
    y_values = list(signal.resize((1, height), Image.Resampling.BOX).getdata())
    x_weight = float(sum(x_values))
    y_weight = float(sum(y_values))
    if x_weight <= 0.0 or y_weight <= 0.0:
        return None, None
    center_x = sum((index + 0.5) * value for index, value in enumerate(x_values))
    center_y = sum((index + 0.5) * value for index, value in enumerate(y_values))
    return center_x / x_weight / width, center_y / y_weight / height


def _strip_metrics(
    mask: Any,
    changed_pixels: int,
    margin: int,
) -> tuple[dict[str, dict[str, float | int | bool]], float, bool]:
    width, height = mask.size
    boxes = {
        "left": (0, 0, margin, height),
        "right": (width - margin, 0, width, height),
        "top": (0, 0, width, margin),
        "bottom": (0, height - margin, width, height),
    }
    side_metrics: dict[str, dict[str, float | int | bool]] = {}
    minimum_touch_pixels = max(3, int(math.ceil(changed_pixels * 0.001)))
    for side, box in boxes.items():
        side_mask = mask.crop(box)
        side_changed = _count_mask_pixels(side_mask)
        strip_pixels = side_mask.width * side_mask.height
        side_metrics[side] = {
            "changed_pixels": side_changed,
            "strip_coverage": side_changed / strip_pixels if strip_pixels else 0.0,
            "changed_share": side_changed / changed_pixels if changed_pixels else 0.0,
            "touches": side_changed >= minimum_touch_pixels,
        }

    edge_band = Image.new("L", (width, height), 0)
    edge_band.paste(255, (0, 0, width, margin))
    edge_band.paste(255, (0, height - margin, width, height))
    edge_band.paste(255, (0, 0, margin, height))
    edge_band.paste(255, (width - margin, 0, width, height))
    edge_changed = _count_mask_pixels(ImageChops.multiply(mask, edge_band))
    edge_fraction = edge_changed / changed_pixels if changed_pixels else 0.0
    clipping_risk = any(bool(value["touches"]) for value in side_metrics.values())
    return side_metrics, edge_fraction, clipping_risk


def _downsample_binary(mask: Any, max_side: int) -> Any:
    width, height = mask.size
    if max(width, height) <= max_side:
        return mask.copy()
    scale = max_side / max(width, height)
    size = (
        max(1, int(round(width * scale))),
        max(1, int(round(height * scale))),
    )
    reduced = mask.resize(size, Image.Resampling.BOX)
    return reduced.point(lambda value: 255 if value >= 16 else 0, mode="L")


def _connected_components(mask: Any, max_side: int) -> dict[str, Any]:
    reduced = _downsample_binary(mask, max_side)
    width, height = reduced.size
    data = reduced.tobytes()
    visited = bytearray(width * height)
    component_sizes: list[int] = []

    for start in range(width * height):
        if not data[start] or visited[start]:
            continue
        visited[start] = 1
        queue: deque[int] = deque([start])
        size = 0
        while queue:
            index = queue.popleft()
            size += 1
            x = index % width
            y = index // width
            for offset_y in (-1, 0, 1):
                neighbor_y = y + offset_y
                if neighbor_y < 0 or neighbor_y >= height:
                    continue
                row_offset = neighbor_y * width
                for offset_x in (-1, 0, 1):
                    if offset_x == 0 and offset_y == 0:
                        continue
                    neighbor_x = x + offset_x
                    if neighbor_x < 0 or neighbor_x >= width:
                        continue
                    neighbor = row_offset + neighbor_x
                    if data[neighbor] and not visited[neighbor]:
                        visited[neighbor] = 1
                        queue.append(neighbor)
        component_sizes.append(size)

    component_sizes.sort(reverse=True)
    changed = int(sum(component_sizes))
    minimum_significant = max(2, int(math.ceil(changed * 0.002)))
    significant = [size for size in component_sizes if size >= minimum_significant]
    dominant_ratio = component_sizes[0] / changed if changed else 0.0
    return {
        "analysis_size": [width, height],
        "count": len(component_sizes),
        "significant_count": len(significant),
        "dominant_ratio": dominant_ratio,
        "largest_pixels": component_sizes[0] if component_sizes else 0,
        "minimum_significant_pixels": minimum_significant,
    }


def _symmetry_similarity(signal: Any, mirrored: Any) -> float:
    signal_sum = float(sum(signal.getdata()))
    if signal_sum <= 0.0:
        return 0.0
    difference_sum = float(sum(ImageChops.difference(signal, mirrored).getdata()))
    return max(0.0, min(1.0, 1.0 - difference_sum / (2.0 * signal_sum)))


def _symmetry_metrics(
    signal: Any,
    changed_bbox: tuple[int, int, int, int] | None,
    max_side: int,
) -> dict[str, Any]:
    if not changed_bbox:
        return {
            "analysis_size": [0, 0],
            "vertical_axis_score": 0.0,
            "horizontal_axis_score": 0.0,
            "vertical_axis_risk": False,
            "horizontal_axis_risk": False,
            "bilateral_risk": False,
        }

    cropped = signal.crop(changed_bbox)
    width, height = cropped.size
    if max(width, height) > max_side:
        scale = max_side / max(width, height)
        cropped = cropped.resize(
            (
                max(1, int(round(width * scale))),
                max(1, int(round(height * scale))),
            ),
            Image.Resampling.BOX,
        )
    vertical_score = _symmetry_similarity(cropped, ImageOps.mirror(cropped))
    horizontal_score = _symmetry_similarity(cropped, ImageOps.flip(cropped))
    vertical_risk = vertical_score >= 0.94
    horizontal_risk = horizontal_score >= 0.94
    return {
        "analysis_size": list(cropped.size),
        "vertical_axis_score": vertical_score,
        "horizontal_axis_score": horizontal_score,
        "vertical_axis_risk": vertical_risk,
        "horizontal_axis_risk": horizontal_risk,
        "bilateral_risk": vertical_risk and horizontal_risk,
    }


def analyze_images(
    effect_image: Any,
    blank_image: Any,
    *,
    roi: Sequence[int] | None = None,
    difference_threshold: int = DEFAULT_DIFFERENCE_THRESHOLD,
    edge_margin_fraction: float = DEFAULT_EDGE_MARGIN_FRACTION,
    component_max_side: int = DEFAULT_COMPONENT_MAX_SIDE,
    symmetry_max_side: int = DEFAULT_SYMMETRY_MAX_SIDE,
) -> dict[str, Any]:
    """Compare an effect screenshot against a matching blank screenshot."""

    _require_pillow()
    if effect_image.size != blank_image.size:
        raise VisualQualityError(
            f"Screenshot sizes differ: effect={effect_image.size}, blank={blank_image.size}"
        )
    if not 0 <= int(difference_threshold) <= 254:
        raise VisualQualityError("difference_threshold must be between 0 and 254")
    if not 0.0 < float(edge_margin_fraction) <= 0.25:
        raise VisualQualityError("edge_margin_fraction must be in (0, 0.25]")
    if int(component_max_side) < 32 or int(symmetry_max_side) < 32:
        raise VisualQualityError("analysis max-side values must be at least 32")

    image_width, image_height = effect_image.size
    roi_box = _validate_roi(roi, image_width, image_height)
    effect = effect_image.convert("RGB").crop(roi_box)
    blank = blank_image.convert("RGB").crop(roi_box)
    width, height = effect.size
    roi_pixels = width * height

    rgb_difference = ImageChops.difference(effect, blank)
    red, green, blue = rgb_difference.split()
    difference_signal = ImageChops.lighter(ImageChops.lighter(red, green), blue)
    mask = difference_signal.point(
        lambda value: 255 if value > int(difference_threshold) else 0,
        mode="L",
    )
    changed_pixels = _count_mask_pixels(mask)
    changed_bbox = mask.getbbox()
    changed_coverage = changed_pixels / roi_pixels if roi_pixels else 0.0
    bbox_area = 0
    if changed_bbox:
        bbox_area = (changed_bbox[2] - changed_bbox[0]) * (
            changed_bbox[3] - changed_bbox[1]
        )
    bbox_coverage = bbox_area / roi_pixels if roi_pixels else 0.0
    bbox_fill_ratio = changed_pixels / bbox_area if bbox_area else 0.0

    signal = Image.composite(
        difference_signal,
        Image.new("L", (width, height), 0),
        mask,
    )
    center_x, center_y = _signal_center_of_mass(signal)

    margin = max(1, int(round(min(width, height) * edge_margin_fraction)))
    side_metrics, edge_fraction, edge_clipping_risk = _strip_metrics(
        mask,
        changed_pixels,
        margin,
    )

    effect_luminance = effect.convert("L")
    blank_luminance = blank.convert("L")
    luminance_histogram = _masked_histogram(effect_luminance, mask)
    delta_luminance_histogram = _masked_histogram(
        ImageChops.difference(effect_luminance, blank_luminance),
        mask,
    )
    white_mask = effect_luminance.point(lambda value: 255 if value >= 250 else 0)
    white_changed = _count_mask_pixels(ImageChops.multiply(white_mask, mask))
    white_clipped_fraction = (
        white_changed / changed_pixels if changed_pixels else 0.0
    )

    components = _connected_components(mask, int(component_max_side))
    symmetry = _symmetry_metrics(
        signal,
        changed_bbox,
        int(symmetry_max_side),
    )

    issues: list[dict[str, Any]] = []
    if changed_pixels == 0:
        issues.append(
            {
                "code": "no_visible_effect",
                "severity": "error",
                "message": "Effect frame does not differ from the blank frame",
            }
        )
    elif changed_coverage < 0.002:
        issues.append(
            {
                "code": "effect_too_small",
                "severity": "error",
                "message": "Changed pixels cover less than 0.2% of the capture ROI",
            }
        )
    if changed_coverage > 0.85:
        issues.append(
            {
                "code": "effect_fills_frame",
                "severity": "warning",
                "message": "Changed pixels cover more than 85% of the capture ROI",
            }
        )
    if edge_clipping_risk:
        touched = [
            side for side, values in side_metrics.items() if values["touches"]
        ]
        issues.append(
            {
                "code": "edge_clipping_risk",
                "severity": "error",
                "message": "Effect signal reaches ROI edge: " + ", ".join(touched),
            }
        )
    if white_clipped_fraction > 0.03:
        issues.append(
            {
                "code": "white_clipping",
                "severity": "warning",
                "message": "More than 3% of changed pixels are clipped near white",
            }
        )
    if (
        components["significant_count"] >= 8
        and components["dominant_ratio"] < 0.35
    ):
        issues.append(
            {
                "code": "fragmented_silhouette",
                "severity": "warning",
                "message": "Effect is split across many similarly small components",
            }
        )
    if symmetry["bilateral_risk"] and changed_coverage >= 0.002:
        issues.append(
            {
                "code": "bilateral_symmetry_risk",
                "severity": "warning",
                "message": "Effect is highly symmetric across both image axes",
            }
        )

    absolute_bbox = None
    if changed_bbox:
        absolute_bbox = [
            changed_bbox[0] + roi_box[0],
            changed_bbox[1] + roi_box[1],
            changed_bbox[2] + roi_box[0],
            changed_bbox[3] + roi_box[1],
        ]
    return {
        "version": REPORT_VERSION,
        "passed": not any(issue["severity"] == "error" for issue in issues),
        "image_size": [image_width, image_height],
        "roi": list(roi_box),
        "config": {
            "difference_threshold": int(difference_threshold),
            "edge_margin_fraction": float(edge_margin_fraction),
            "edge_margin_pixels": margin,
            "component_max_side": int(component_max_side),
            "symmetry_max_side": int(symmetry_max_side),
        },
        "metrics": {
            "changed_pixels": changed_pixels,
            "roi_pixels": roi_pixels,
            "changed_coverage": changed_coverage,
            "changed_bbox": absolute_bbox,
            "changed_bbox_roi": list(changed_bbox) if changed_bbox else None,
            "bbox_coverage": bbox_coverage,
            "bbox_fill_ratio": bbox_fill_ratio,
            "edge": {
                "clipping_risk": edge_clipping_risk,
                "changed_fraction": edge_fraction,
                "sides": side_metrics,
            },
            "white_clipped_pixels": white_changed,
            "white_clipped_fraction": white_clipped_fraction,
            "luminance": {
                "mean": _histogram_mean(luminance_histogram),
                "p10": _percentile(luminance_histogram, 0.10),
                "p50": _percentile(luminance_histogram, 0.50),
                "p90": _percentile(luminance_histogram, 0.90),
                "p99": _percentile(luminance_histogram, 0.99),
                "delta_mean": _histogram_mean(delta_luminance_histogram),
                "delta_p50": _percentile(delta_luminance_histogram, 0.50),
                "delta_p90": _percentile(delta_luminance_histogram, 0.90),
                "delta_p99": _percentile(delta_luminance_histogram, 0.99),
                "center_of_mass_normalized": [center_x, center_y],
                "center_offset_normalized": [
                    center_x - 0.5 if center_x is not None else None,
                    center_y - 0.5 if center_y is not None else None,
                ],
            },
            "connected_components": components,
            "symmetry": symmetry,
        },
        "issues": issues,
    }


def analyze_paths(
    effect_path: str | Path,
    blank_path: str | Path,
    **kwargs: Any,
) -> dict[str, Any]:
    """Load a screenshot pair and return a JSON-serializable quality report."""

    _require_pillow()
    effect_path = Path(effect_path).expanduser().resolve()
    blank_path = Path(blank_path).expanduser().resolve()
    if not effect_path.is_file():
        raise VisualQualityError(f"Effect screenshot does not exist: {effect_path}")
    if not blank_path.is_file():
        raise VisualQualityError(f"Blank screenshot does not exist: {blank_path}")
    with Image.open(effect_path) as effect_image, Image.open(blank_path) as blank_image:
        report = analyze_images(effect_image, blank_image, **kwargs)
    report["effect_path"] = str(effect_path)
    report["blank_path"] = str(blank_path)
    return report


def _parse_roi(value: str) -> tuple[int, int, int, int]:
    try:
        parts = tuple(int(part.strip()) for part in value.split(","))
    except ValueError as exc:
        raise argparse.ArgumentTypeError(
            "ROI must be four comma-separated integers"
        ) from exc
    if len(parts) != 4:
        raise argparse.ArgumentTypeError(
            "ROI must be left,top,right,bottom"
        )
    return parts


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Compare a VFX screenshot with a matching blank capture",
    )
    parser.add_argument("effect", help="Effect screenshot path")
    parser.add_argument("--blank", required=True, help="Matching blank screenshot path")
    parser.add_argument(
        "--roi",
        type=_parse_roi,
        help="Capture ROI as left,top,right,bottom",
    )
    parser.add_argument(
        "--difference-threshold",
        type=int,
        default=DEFAULT_DIFFERENCE_THRESHOLD,
        help="Per-pixel RGB difference threshold (default: 12)",
    )
    parser.add_argument(
        "--edge-margin",
        type=float,
        default=DEFAULT_EDGE_MARGIN_FRACTION,
        help="Fraction of ROI treated as an edge band (default: 0.02)",
    )
    parser.add_argument("--output", help="Optional JSON output path")
    parser.add_argument(
        "--compact",
        action="store_true",
        help="Write compact JSON instead of indented JSON",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Return exit code 1 when the report contains an error",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_argument_parser()
    args = parser.parse_args(argv)
    try:
        report = analyze_paths(
            args.effect,
            args.blank,
            roi=args.roi,
            difference_threshold=args.difference_threshold,
            edge_margin_fraction=args.edge_margin,
        )
    except VisualQualityError as exc:
        print(json.dumps({"success": False, "error": str(exc)}), file=sys.stderr)
        return 2

    indent = None if args.compact else 2
    serialized = json.dumps(report, ensure_ascii=False, indent=indent)
    if args.output:
        output_path = Path(args.output).expanduser().resolve()
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(serialized + "\n", encoding="utf-8")
    print(serialized)
    return 1 if args.strict and not report["passed"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
