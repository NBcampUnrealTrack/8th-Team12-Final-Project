"""Revvy desktop control surface for Unreal Engine and local AI clients."""

from __future__ import annotations

import contextlib
import functools
import json
import math
import os
import pathlib
import queue
import signal
import subprocess
import sys
import threading
import time
import tkinter as tk
from tkinter import filedialog, messagebox, scrolledtext, ttk
from collections.abc import Iterator
from typing import Any, Callable, Sequence

from PIL import Image, ImageOps, ImageTk, UnidentifiedImageError

try:
    from tkinterdnd2 import DND_FILES, TkinterDnD
except ImportError:  # Source fallback; packaged builds require tkinterdnd2.
    DND_FILES = None
    TkinterDnD = None

import revvy_app_core as core
import revvy_diff as core_diff
import revvy_files as core_files


# A packaged app lives below its host project. Bind project-local state before
# importing revvy, whose CLI/runtime paths are initialized at module import.
_BOOTSTRAP_PROJECT_DIR = core.detect_host_project_dir()
if _BOOTSTRAP_PROJECT_DIR:
    core.bind_project_state(_BOOTSTRAP_PROJECT_DIR)

import revvy
from revvy_processes import (
    attach_process_tree,
    release_process_tree,
    terminate_attached_process_tree,
)

try:
    import revvy_updater as updater
except ImportError:  # Source checkouts without the packaged updater still run.
    updater = None


SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
# Semantic palettes matched to Revvy's navy face screen, mint eyes and
# periwinkle ear lights. The light theme keeps the workspace bright while the
# header, navigation rail and composer form one dark "workbench" shell.
THEMES = {
    "light": {
        # surface ladder: #ffffff -> #fafafa -> #f5f5f5 -> #f0f0f0 -> #eaeaea -> #dedede
        "window": "#ffffff",
        "surface": "#ffffff",
        "surface_alt": "#f5f5f5",
        "deep": "#f0f0f0",
        "raised": "#eaeaea",
        "raised_hover": "#dedede",
        "text": "#0a0a0a",
        # Orca ships --muted-foreground #737373, which measures 4.35:1 on
        # #f5f5f5. #646464 is the lightest value clearing 4.5 on every light
        # surface in this file, including raised (#eaeaea, 4.92).
        "muted": "#646464",
        # Deliberately sub-AA: placeholder, disabled label, scrollbar arrow,
        # idle dot. Anything meant to be read uses "muted".
        "subtle": "#8f8f8f",
        "border": "#e5e5e5",
        "border_strong": "#b4b4b4",
        "border_focus": "#0a7659",
        "accent": "#5ee6b5",
        "accent_active": "#7af0c7",
        "accent_text": "#0a7659",
        "accent_soft": "#eefaf5",
        "on_accent": "#071a16",
        "violet": "#6d28d9",
        "online": "#15803d",
        "online_soft": "#eef6f1",
        "offline_soft": "#f5f5f5",
        "danger": "#c9000f",
        "danger_soft": "#fbeaea",
        "warning": "#9a5b00",
        "warning_soft": "#fbf3e9",
        "warning_text": "#9a5b00",
        "code": "#ffffff",
        "code_text": "#0a0a0a",
        "code_accent": "#0a7659",
        "code_select": "#dedede",
        "selection": "#dedede",
        "select_soft": "#eaeaea",
        "user_surface": "#f5f5f5",
        "header": "#fafafa",
        "header_text": "#0a0a0a",
        "header_muted": "#646464",
        "rail": "#f5f5f5",
        "rail_hover": "#eaeaea",
        "rail_selected": "#dedede",
        "composer": "#ffffff",
        "progress_track": "#e5e5e5",
        "chip": "#f5f5f5",
    },
    "dark": {
        # surface ladder: #0a0a0a -> #171717 -> #262626 -> #2a2a2a -> #353535 -> #404040
        "window": "#0a0a0a",
        "surface": "#171717",
        "surface_alt": "#2a2a2a",
        "deep": "#262626",
        "raised": "#353535",
        "raised_hover": "#404040",
        "text": "#fafafa",
        "muted": "#a1a1a1",
        "subtle": "#787878",
        # One literal standing in for Orca's rgb(255 255 255 / .07), which
        # resolves to three hexes. #333333 stays lighter than every dark
        # surface it touches; anything below #393939 inverts on surface_alt.
        "border": "#333333",
        "border_strong": "#5c5c5c",
        "border_focus": "#5ee6b5",
        "accent": "#5ee6b5",
        "accent_active": "#7af0c7",
        "accent_text": "#73ebc0",
        "accent_soft": "#1e2c27",
        "on_accent": "#071a16",
        "violet": "#a684ff",
        "online": "#86efac",
        "online_soft": "#222d26",
        "offline_soft": "#262626",
        "danger": "#ff6568",
        "danger_soft": "#2e1f1f",
        "warning": "#fbbf24",
        "warning_soft": "#2e2818",
        "warning_text": "#fbbf24",
        "code": "#1e1e1e",
        "code_text": "#fafafa",
        "code_accent": "#8ef0da",
        "code_select": "#3f3f3f",
        "selection": "#3f3f3f",
        "select_soft": "#343434",
        "user_surface": "#262626",
        "header": "#171717",
        "header_text": "#fafafa",
        "header_muted": "#a1a1a1",
        "rail": "#2a2a2a",
        "rail_hover": "#353535",
        "rail_selected": "#3f3f3f",
        "composer": "#171717",
        "progress_track": "#353535",
        "chip": "#262626",
    },
}
COLORS = dict(THEMES["light"])

# Distinguish "bind this callback to the worker/current project" from an
# explicit ``session=None`` callback that is intentionally window-global.
_POST_CURRENT_SESSION = object()

NAV_ITEMS = (
    ("◉", "대화"),
    ("◇", "도구"),
    ("›_", "실행"),
    ("≡", "로그"),
)

IMAGE_SUFFIXES = {".png", ".jpg", ".jpeg", ".gif", ".webp"}
MAX_IMAGE_ATTACHMENTS = 4
MAX_IMAGE_BYTES = 20 * 1024 * 1024
# How many step rows the viewport shows at once. Steps beyond it are reachable
# by scrolling rather than dropped - a long tool run used to lose everything but
# its last three lines with no way to get them back.
ACTIVITY_VISIBLE_ROWS = 3
# The row reserves space for an optional second line rather than growing when
# one appears: a card that resizes mid-stream makes the transcript above it jump
# on every event, which is what test_activity_panel_height_stays_fixed guards.
# Sized so the viewport is exactly ACTIVITY_VISIBLE_ROWS tall. Four pixels short
# and the scrollbar appears on a list that fits, which is how this was found.
ACTIVITY_PANEL_HEIGHT = 184
ACTIVITY_ROW_HEIGHT = 37
PROGRESS_THICKNESS = 10
PROGRESS_CANVAS_HEIGHT = 14
PROJECT_RAIL_WIDTH = 210
RIGHT_PANEL_WIDTH = 304
FILE_TREE_INDENT = 14
ACTIVITY_TITLE_LIMIT = 88
ACTIVITY_DETAIL_LIMIT = 110
CHAT_LIVENESS_TICK_MS = 1_000
CHAT_SILENT_NOTICE_SECONDS = 30
THEME_FADE_STEPS = 6
THEME_FADE_INTERVAL_MS = 16
EFFORT_LABELS = {
    "": "Auto",
    "minimal": "Minimal",
    "low": "Low",
    "medium": "Medium",
    "high": "High",
    "xhigh": "XHigh",
    "max": "Max",
    "ultra": "Ultra",
}
EFFORT_VALUES = {label: value for value, label in EFFORT_LABELS.items()}
CLAUDE_REASONING_EFFORTS = ("", "low", "medium", "high", "xhigh", "max")
PERMISSION_LABELS = {
    core.PERMISSION_SAFE: "Safe · 읽기 전용",
    core.PERMISSION_FULL: "Full access · 전체 권한",
}
PERMISSION_VALUES = {label: value for value, label in PERMISSION_LABELS.items()}

ENGINE_ACCENTS = {
    core.ENGINE_UNREAL: {"label": "Unreal", "eyebrow": "UNREAL COPILOT", "accent": ""},
    core.ENGINE_UNITY: {"label": "Unity", "eyebrow": "UNITY COPILOT", "accent": "#5B9BD5"},
    core.ENGINE_GODOT: {"label": "Godot", "eyebrow": "GODOT COPILOT", "accent": "#478CBF"},
}
ENGINE_LABELS = {
    None: "자동",
    core.ENGINE_UNREAL: "Unreal",
    core.ENGINE_UNITY: "Unity",
    core.ENGINE_GODOT: "Godot",
}
ENGINE_SELECT_VALUES = {label: value for value, label in ENGINE_LABELS.items()}


def _shade_color(hex_color: str, factor: float) -> str:
    """Lighten (factor > 0) or darken (factor < 0) a ``#rrggbb`` color."""
    value = hex_color.lstrip("#")
    red, green, blue = (int(value[index : index + 2], 16) for index in (0, 2, 4))
    if factor >= 0:
        red = round(red + (255 - red) * factor)
        green = round(green + (255 - green) * factor)
        blue = round(blue + (255 - blue) * factor)
    else:
        red = round(red * (1 + factor))
        green = round(green * (1 + factor))
        blue = round(blue * (1 + factor))
    return f"#{red:02x}{green:02x}{blue:02x}"


def _engine_brand(engine: str) -> dict[str, str]:
    return ENGINE_ACCENTS.get(engine, ENGINE_ACCENTS[core.ENGINE_UNREAL])


def _activity_detail(full: str, compact: str, limit: int = ACTIVITY_DETAIL_LIMIT) -> str:
    """The part of a step report the compacted title had to drop.

    Long tool reports carry their outcome at the end - what was built, how many
    frames ran, what failed - and truncating to a single line threw exactly that
    away. Returns "" when nothing was lost.
    """
    text = " ".join(str(full).split())
    head = compact.rstrip("…").rstrip()
    if not text or not head or not text.startswith(head) or len(text) <= len(head):
        return ""
    tail = text[len(head) :].strip()
    if not tail:
        return ""
    return tail if len(tail) <= limit else tail[: limit - 1].rstrip() + "…"


def _compact_activity_title(value: str, limit: int = ACTIVITY_TITLE_LIMIT) -> str:
    clean = " ".join(str(value).split())
    if len(clean) <= limit:
        return clean
    if limit <= 1:
        return "…"[:limit]
    return clean[: limit - 1].rstrip() + "…"


def _mix_hex(base: str, other: str, amount: float) -> str:
    """Blend two #rrggbb colours. Tk has no alpha, so tints are computed."""
    ratio = min(1.0, max(0.0, amount))
    try:
        left = tuple(int(base[index : index + 2], 16) for index in (1, 3, 5))
        right = tuple(int(other[index : index + 2], 16) for index in (1, 3, 5))
    except (ValueError, IndexError):
        return base
    blended = tuple(
        int(round(left[channel] + (right[channel] - left[channel]) * ratio))
        for channel in range(3)
    )
    return "#%02x%02x%02x" % blended


class PillProgress(tk.Canvas):
    """Rounded progress bar carrying the ttk.Progressbar API this app uses.

    ttk cannot round a bar's caps, and the block it slides in indeterminate mode
    reads like something is broken rather than like work in flight. Drawing it
    on a canvas is the only way to get the pill; keeping ttk's
    ``mode``/``value``/``maximum``/``style`` contract means the render code and
    the tests that guard it never have to know the difference.

    Two behaviours are deliberate. The determinate fill eases toward its target
    instead of snapping, because a bar that jumps cannot be read as filling. And
    ``value`` reports the target, not the animated position - callers ask what
    the bar means, not where the tween happens to be this frame.
    """

    STYLE_TONES = {
        "Activity.Horizontal.TProgressbar": "accent",
        "ActivityDone.Horizontal.TProgressbar": "online",
        "ActivityIssue.Horizontal.TProgressbar": "danger",
    }
    OWN_OPTIONS = ("mode", "value", "maximum", "style")

    def __init__(self, master: tk.Misc, **kwargs: object) -> None:
        # Own options are pulled out before the canvas sees them; Tk rejects
        # -mode and -style outright.
        self._mode = kwargs.pop("mode", "determinate")
        self._value = kwargs.pop("value", 0.0)
        self._maximum = kwargs.pop("maximum", 100.0)
        self._style = kwargs.pop("style", "Activity.Horizontal.TProgressbar")
        background = kwargs.pop("background", COLORS["surface_alt"])
        super().__init__(
            master,
            height=PROGRESS_CANVAS_HEIGHT,
            background=background,
            highlightthickness=0,
            borderwidth=0,
            **kwargs,  # type: ignore[arg-type]
        )
        self._drawn = 0.0
        self._phase = 0.0
        self._interval = 18
        self._job: str | None = None
        self.bind("<Configure>", lambda _event: self._redraw())
        self.bind("<Destroy>", lambda _event: self._cancel())

    # -- ttk.Progressbar surface -------------------------------------------
    def configure(self, cnf: object = None, **kwargs: object) -> object:  # type: ignore[override]
        taken = [name for name in self.OWN_OPTIONS if name in kwargs]
        for name in taken:
            setattr(self, f"_{name}", kwargs.pop(name))
        if "value" in taken and self._fraction() <= 0.0:
            # A reset to empty jumps rather than draining backwards: watching a
            # bar unfill reads as progress being taken away.
            self._drawn = 0.0
        result = super().configure(cnf, **kwargs) if (cnf or kwargs) else None
        if taken:
            if str(self._mode) == "determinate" and self._drawn != self._fraction():
                self._cancel()
                self._tick()
            else:
                self._redraw()
        return result

    config = configure

    def cget(self, key: str) -> object:  # type: ignore[override]
        if key in self.OWN_OPTIONS:
            return getattr(self, f"_{key}")
        return super().cget(key)

    def start(self, interval: int | None = None) -> None:
        self._interval = int(interval or 18)
        if self._job is None:
            self._tick()

    def stop(self) -> None:
        self._cancel()
        if str(self._mode) == "determinate":
            self._drawn = self._fraction()
        self._redraw()

    # -- drawing ------------------------------------------------------------
    def _cancel(self) -> None:
        if self._job is not None:
            try:
                self.after_cancel(self._job)
            except tk.TclError:
                pass
            self._job = None

    def _tone(self) -> str:
        return COLORS[self.STYLE_TONES.get(str(self._style), "accent")]

    def _fraction(self) -> float:
        try:
            maximum = float(self._maximum) or 1.0
            return min(1.0, max(0.0, float(self._value) / maximum))
        except (TypeError, ValueError):
            return 0.0

    def _tick(self) -> None:
        if not self.winfo_exists():
            self._job = None
            return
        if str(self._mode) == "indeterminate":
            self._phase = (self._phase + 0.022) % 1.0
        else:
            target = self._fraction()
            gap = target - self._drawn
            if abs(gap) < 0.004:
                self._drawn = target
                self._job = None
                self._redraw()
                return
            self._drawn += gap * 0.22
        self._redraw()
        self._job = self.after(self._interval, self._tick)

    def _pill(self, x0: float, x1: float, colour: str, thickness: float, dy: float = 0.0) -> None:
        if x1 - x0 < 0.5:
            return
        centre = self.winfo_height() / 2 + dy
        self.create_line(
            x0,
            centre,
            x1,
            centre,
            fill=colour,
            width=max(1.0, thickness),
            capstyle=tk.ROUND,
        )

    def _redraw(self) -> None:
        if not self.winfo_exists():
            return
        self.delete("all")
        width = self.winfo_width()
        if width <= 1:
            return
        thickness = PROGRESS_THICKNESS
        inset = thickness / 2 + 1
        left, right = inset, width - inset
        span = right - left
        if span <= 0:
            return
        tone = self._tone()
        self._pill(left, right, COLORS["progress_track"], thickness)
        if str(self._mode) == "indeterminate":
            # A comet that eases at the turns instead of bouncing off the walls:
            # constant-speed travel is what makes ttk's block look mechanical.
            length = span * 0.3
            travel = span - length
            eased = (1 - math.cos(self._phase * 2 * math.pi)) / 2
            head = left + travel * eased
            start, stop = head, head + length
        else:
            start = left
            stop = left + span * self._drawn
            if self._drawn <= 0.0:
                return
        self._pill(start, stop, tone, thickness)
        # A thin lighter line inset into the fill: the highlight is what makes a
        # flat rectangle read as a rounded, soft object.
        gloss_inset = thickness * 0.34
        self._pill(
            start + gloss_inset,
            stop - gloss_inset,
            _mix_hex(tone, "#ffffff", 0.42),
            thickness * 0.3,
            dy=-thickness * 0.24,
        )


def _format_duration(seconds: float) -> str:
    total = max(0, int(seconds))
    minutes, remainder = divmod(total, 60)
    hours, minutes = divmod(minutes, 60)
    if hours:
        return f"{hours}시간 {minutes}분"
    if minutes:
        return f"{minutes}분 {remainder}초"
    return f"{remainder}초"


def _startup_attachment_args(arguments: list[str]) -> list[str]:
    paths: list[str] = []
    index = 0
    while index < len(arguments):
        argument = arguments[index]
        if argument == "--attach":
            index += 1
            if index >= len(arguments):
                raise ValueError("--attach requires an image path")
            paths.append(arguments[index])
        elif argument.startswith("--attach="):
            paths.append(argument.partition("=")[2])
        index += 1
    return paths


def _default_project(settings: core.AppSettings) -> str:
    # The app is deployed inside <Project>/Plugins/Revvy, so when it runs from
    # within a project that project wins over whatever was saved last time.
    host_project = core.detect_host_project_dir()
    if host_project:
        return host_project
    if settings.project_dir and pathlib.Path(settings.project_dir).is_dir():
        return settings.project_dir
    try:
        config = revvy.load_config()
        for key in ("active_project", "installed_project"):
            project = revvy._project_from_config(config, key)
            if project and pathlib.Path(project.get("path", "")).is_dir():
                return str(pathlib.Path(project["path"]).resolve())
    except (OSError, ValueError, TypeError):
        pass
    return str(pathlib.Path.cwd())


def _adopt_packaged_host_project(settings: core.AppSettings) -> bool:
    """Use the project containing this app and drop another project's override."""
    host_project = core.detect_host_project_dir()
    if not host_project:
        return False
    changed = not core.same_project_path(settings.project_dir, host_project)
    settings.project_dir = host_project
    if changed:
        settings.engine_override = None
    return changed


def _status_snapshot() -> dict[str, Any]:
    config = revvy.load_config()
    health = revvy.get_health(config)
    if health:
        result = dict(health)
        result["proxy_running"] = True
        result["blender_bridge_connected"] = False
        upstream_health = result.get("upstreams")
        upstream_servers = (
            upstream_health.get("servers", {})
            if isinstance(upstream_health, dict)
            else {}
        )
        blender_upstream = (
            upstream_servers.get(core.BLENDER_MCP_SERVER_NAME)
            if isinstance(upstream_servers, dict)
            else None
        )
        # A status refresh must stay observational. Dormant upstreams are
        # activated only by an explicit Blender launch or Blender chat request.
        if isinstance(blender_upstream, dict) and blender_upstream.get("ready") is True:
            try:
                response = revvy.rpc_request(
                    "tools/call",
                    {
                        "name": "blender__blender_bridge_status",
                        "arguments": {},
                    },
                    timeout=4.0,
                )
                rpc_result = response.get("result", {}) if isinstance(response, dict) else {}
                structured = (
                    rpc_result.get("structuredContent", {})
                    if isinstance(rpc_result, dict)
                    else {}
                )
                if isinstance(structured, dict) and structured.get("success") is True:
                    result["blender_bridge_connected"] = True
                    bridge_result = structured.get("result")
                    if isinstance(bridge_result, dict):
                        result["blender_bridge"] = bridge_result
                elif isinstance(structured, dict):
                    error = structured.get("error")
                    if isinstance(error, dict):
                        result["blender_bridge_error"] = str(
                            error.get("message") or error.get("code") or ""
                        )
            except (ConnectionError, OSError, TimeoutError, ValueError) as exc:
                result["blender_bridge_error"] = str(exc)
        return result
    return {
        "service": "revvy-proxy",
        "proxy_running": False,
        "proxy_url": f"http://127.0.0.1:{config['proxy_port']}/mcp",
        "ue_running": revvy._unreal_running(config),
        "ue_url": f"http://127.0.0.1:{config['ue_port']}/mcp",
        "manifest_tools": 0,
        "active_project": None,
    }


def _proxy_endpoint_label(proxy_url: object = "") -> str:
    value = str(proxy_url or "").strip()
    if value:
        return f"MCP  {value}"
    try:
        config = revvy.load_config()
        port = int(config["proxy_port"])
        if not 1 <= port <= 65535:
            raise ValueError("invalid proxy port")
    except (KeyError, OSError, TypeError, ValueError):
        return "MCP  상태 확인 중"
    return f"MCP  http://127.0.0.1:{port}/mcp"


class DiffWindow(tk.Toplevel):
    """Reads a change the way a reviewer does: old on the left, new marked.

    Monaco gives Orca the diff, the intra-line highlighting and the synchronised
    scroll for free. None of that exists here, so the diff is computed with
    difflib and painted with Text tags - which turns out to be enough, because
    what a reviewer needs is which lines moved and which characters inside them.
    """

    def __init__(self, app: "RevvyApp", title: str, diffs: list[Any]) -> None:
        super().__init__(app.root)
        self.title(f"변경 내역 · {title}")
        self.geometry("980x680")
        self.configure(background=COLORS["surface"])
        self.transient(app.root)
        header = tk.Frame(self, background=COLORS["surface_alt"])
        header.pack(fill=tk.X)
        total_added = sum(item.added for item in diffs)
        total_removed = sum(item.removed for item in diffs)
        tk.Label(
            header,
            text=f"{len(diffs)}개 파일   +{total_added}  -{total_removed}",
            background=COLORS["surface_alt"],
            foreground=COLORS["muted"],
            padx=12,
            pady=7,
            font=("Segoe UI Semibold", 9),
        ).pack(side=tk.LEFT)
        shell = tk.Frame(self, background=COLORS["surface"])
        shell.pack(fill=tk.BOTH, expand=True)
        self.view = tk.Text(
            shell,
            wrap=tk.NONE,
            background=COLORS["code"],
            foreground=COLORS["code_text"],
            insertbackground=COLORS["code_text"],
            borderwidth=0,
            highlightthickness=0,
            padx=10,
            pady=8,
            font=("Consolas", 9),
        )
        scroll = ttk.Scrollbar(shell, orient=tk.VERTICAL, command=self.view.yview)
        self.view.configure(yscrollcommand=scroll.set)
        scroll.pack(side=tk.RIGHT, fill=tk.Y)
        self.view.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        self._configure_tags()
        self._render(diffs)
        self.view.configure(state=tk.DISABLED)
        self.bind("<Escape>", lambda _event: self.destroy())

    def _configure_tags(self) -> None:
        add_bg = _mix_hex(COLORS["code"], COLORS["online"], 0.22)
        del_bg = _mix_hex(COLORS["code"], COLORS["danger"], 0.22)
        self.view.tag_configure("add", background=add_bg)
        self.view.tag_configure("del", background=del_bg)
        self.view.tag_configure("meta", foreground=COLORS["accent_text"])
        self.view.tag_configure("gutter", foreground=COLORS["subtle"])
        # The changed characters inside a modified line. Without this a one-word
        # edit and a rewritten line look identical.
        self.view.tag_configure(
            "add_span", background=_mix_hex(COLORS["code"], COLORS["online"], 0.5)
        )
        self.view.tag_configure(
            "del_span", background=_mix_hex(COLORS["code"], COLORS["danger"], 0.5)
        )

    def _render(self, diffs: list[Any]) -> None:
        marks = {core_diff.KIND_ADD: "+", core_diff.KIND_DEL: "-"}
        for index, item in enumerate(diffs):
            if index:
                self.view.insert(tk.END, "\n")
            for line in item.lines:
                if line.kind == core_diff.KIND_META:
                    self.view.insert(tk.END, f"{line.text}\n", "meta")
                    continue
                number = line.new_number or line.old_number or ""
                self.view.insert(tk.END, f"{str(number):>5} ", "gutter")
                start = self.view.index("end-1c")
                self.view.insert(
                    tk.END,
                    f"{marks.get(line.kind, ' ')} {line.text}\n",
                    line.kind if line.kind in marks else "",
                )
                for span_start, span_end in line.spans:
                    # +2 for the marker and the space written above.
                    self.view.tag_add(
                        f"{line.kind}_span",
                        f"{start}+{span_start + 2}c",
                        f"{start}+{span_end + 2}c",
                    )


class FileEditorWindow(tk.Toplevel):
    """Open one project file, editable, refusing to clobber an agent's write.

    The signature captured at load is the whole point. Revvy's agents edit the
    same files the user has open, and a save that does not check whether the
    bytes moved will silently throw one of them away.
    """

    def __init__(self, app: "RevvyApp", node: Any, document: Any) -> None:
        super().__init__(app.root)
        self.app = app
        self.node = node
        self.document = document
        self.title(f"{node.name} · {node.relative}")
        self.geometry("900x680")
        self.configure(background=COLORS["surface"])
        self.transient(app.root)

        bar = tk.Frame(self, background=COLORS["surface_alt"])
        bar.pack(fill=tk.X)
        self.state_var = tk.StringVar(
            value="읽기 전용 · 일부만 불러왔어요" if document.truncated else "저장됨"
        )
        tk.Label(
            bar,
            textvariable=self.state_var,
            background=COLORS["surface_alt"],
            foreground=COLORS["muted"],
            padx=12,
            pady=6,
            font=("Segoe UI", 8),
        ).pack(side=tk.LEFT)
        ttk.Button(bar, text="변경 내역", command=self.show_diff, style="Chip.TButton").pack(
            side=tk.RIGHT, padx=(0, 8), pady=4
        )
        self.save_button = ttk.Button(bar, text="저장  Ctrl+S", command=self.save)
        self.save_button.pack(side=tk.RIGHT, padx=(0, 6), pady=4)

        shell = tk.Frame(self, background=COLORS["surface"])
        shell.pack(fill=tk.BOTH, expand=True)
        self.editor = tk.Text(
            shell,
            wrap=tk.NONE,
            background=COLORS["code"],
            foreground=COLORS["code_text"],
            insertbackground=COLORS["accent"],
            selectbackground=COLORS["code_select"],
            borderwidth=0,
            highlightthickness=0,
            padx=10,
            pady=8,
            undo=True,
            font=("Consolas", 10),
        )
        scroll = ttk.Scrollbar(shell, orient=tk.VERTICAL, command=self.editor.yview)
        self.editor.configure(yscrollcommand=scroll.set)
        scroll.pack(side=tk.RIGHT, fill=tk.Y)
        self.editor.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        self.editor.insert("1.0", document.text)
        self.editor.edit_reset()
        self.editor.edit_modified(False)
        if document.truncated:
            self.editor.configure(state=tk.DISABLED)
            self.save_button.configure(state=tk.DISABLED)
        self.editor.bind("<<Modified>>", self._mark_dirty)
        self.bind("<Control-s>", lambda _event: (self.save(), "break")[1])
        self.bind("<Escape>", lambda _event: self.destroy())

    def _mark_dirty(self, _event: tk.Event | None = None) -> None:
        if self.editor.edit_modified():
            self.state_var.set("저장 안 됨")

    def current_text(self) -> str:
        return self.editor.get("1.0", "end-1c")

    def save(self) -> None:
        if self.document.truncated:
            return
        text = self.current_text()
        try:
            self.document = core_files.write_document(self.document, text)
        except core_files.ExternalEditError as exc:
            if not messagebox.askyesno(
                "디스크에서 바뀐 파일",
                f"{exc}\n\n그래도 내 내용으로 덮어쓸까요?",
                parent=self,
            ):
                self.state_var.set("저장 취소됨 · 디스크가 더 최신")
                return
            try:
                self.document = core_files.write_document(self.document, text, force=True)
            except OSError as error:
                self.state_var.set(str(error))
                return
        except OSError as exc:
            self.state_var.set(str(exc))
            return
        self.editor.edit_modified(False)
        self.state_var.set("저장됨")
        self.app.refresh_files()

    def show_diff(self) -> None:
        """This file versus its last commit, including unsaved edits."""
        root = str(self.app._file_tree().root)
        code, original = core_files._run_git(
            pathlib.Path(root), "show", f"HEAD:{self.node.relative}"
        )
        result = core_diff.diff_text(
            original if code == 0 else "", self.current_text(), self.node.relative
        )
        if not result.changed:
            self.state_var.set("마지막 커밋과 같아요")
            return
        DiffWindow(self.app, f"{self.node.relative} · {result.summary}", [result])


class ProjectSession:
    """Everything that belongs to one open project.

    The app used to hold this block directly, one copy, which is why opening a
    second project meant abandoning the first: its history, engine, connection
    state and running turn had nowhere to live. Each open project now owns a
    ProjectSession and the app focuses one at a time.

    Only state lives here, never widgets. There is still a single set of chat
    widgets and they are repainted from whichever session has focus - so a turn
    running in an unfocused project keeps running, it simply is not on screen.
    """

    # Forwarded onto RevvyApp so the existing call sites keep reading self.x.
    # Listing them here is what makes "this is per-project" reviewable in one
    # place rather than inferred from four thousand lines of attribute access.
    FIELDS = (
        "settings",
        "workspace",
        "engine",
        "provider_key",
        "reasoning_by_provider",
        "reasoning_effort",
        "permission_mode",
        "active_process",
        "_cancel_process",
        "cancel_requested",
        "action_running",
        "ue_connected",
        "ue_status_known",
        "blender_connected",
        "blender_configured",
        "chat_turn_started_at",
        "chat_last_event_at",
        "chat_active_provider",
        "_history_project_dir",
        "history",
        "activity_entries",
        "activity_index",
        "_last_turn_notice",
        "pending_reply",
        "turn_edits",
        "tools",
        "direct_output_text",
        "log_text",
        "chat_draft",
        "direct_args_text",
        "attachments",
        "active_attachment_paths",
        "_attachment_photos",
    )

    def __init__(
        self,
        workspace: core.Workspace | None = None,
        settings: core.AppSettings | None = None,
    ) -> None:
        # Both optional so a session can exist before a project is resolved -
        # see _session_of, which needs somewhere to put state on an app object
        # that never ran __init__.
        settings = settings if settings is not None else core.AppSettings()
        self.workspace = workspace
        self.settings = settings
        self.engine = core.ENGINE_UNREAL
        self.provider_key = settings.provider
        self.reasoning_by_provider: dict[str, str] = {}
        self.reasoning_effort = ""
        self.permission_mode = settings.permission_mode
        self.active_process: subprocess.Popen[str] | None = None
        self._cancel_process: subprocess.Popen[str] | None = None
        self.cancel_requested = threading.Event()
        self.action_running = False
        self.ue_connected = False
        self.ue_status_known = False
        self.blender_connected = False
        self.blender_configured = False
        self.chat_turn_started_at = 0.0
        self.chat_last_event_at = 0.0
        self.chat_active_provider = ""
        self._history_project_dir = settings.project_dir
        self.history: list[tuple[str, str]] = []
        self.activity_entries: list[dict[str, str]] = []
        self.activity_index: dict[str, dict[str, str]] = {}
        self._last_turn_notice: tuple[str, bool] | None = None
        # Reply text streamed so far this turn. Only meaningful while a turn is
        # in flight: history gets the finished answer, and this is what lets an
        # unfocused project's partial reply survive until it is looked at.
        self.pending_reply = ""
        # FileDiff previews built from this turn's edit tool calls, newest last.
        self.turn_edits: list[Any] = []
        self.tools: list[dict[str, Any]] = []
        self.direct_output_text = ""
        self.log_text = ""
        self.chat_draft = ""
        self.direct_args_text = "{}"
        self.attachments: list[pathlib.Path] = []
        self.active_attachment_paths: list[pathlib.Path] = []
        self._attachment_photos: list[ImageTk.PhotoImage] = []

    @property
    def key(self) -> str:
        if self.workspace is not None:
            return self.workspace.key
        return os.path.normcase(os.path.abspath(self.settings.project_dir or ""))

    @property
    def name(self) -> str:
        if self.workspace is not None:
            return self.workspace.name
        return pathlib.Path(self.settings.project_dir or "").name

    @property
    def busy(self) -> bool:
        # Cancellation hands the provider to a separate stopper thread.  The
        # chat worker can finish before that thread has completed its bounded
        # process-tree cleanup, so the cancellation owner remains busy too.
        return (
            self.action_running
            or self.active_process is not None
            or self._cancel_process is not None
        )

    def status_dot(self) -> str:
        """running / connected / idle - what the project rail shows."""
        if self.busy:
            return "running"
        if self.ue_connected or self.blender_connected:
            return "connected"
        return "idle"


def _session_of(app: "RevvyApp") -> ProjectSession:
    """The focused session, creating a detached one if there is none yet.

    Tests build app objects with ``object.__new__`` and assign attributes
    directly, and __init__ itself assigns forwarded fields before a project has
    been resolved. Both need somewhere for that state to land.
    """
    session = app.__dict__.get("_session")
    if session is None:
        session = ProjectSession()
        app.__dict__["_session"] = session
    return session


def _forward_session_fields() -> None:
    """Give RevvyApp a property per session field, reading the focused one.

    A rename-every-call-site migration across this file would be thousands of
    edits with no behavioural payoff. Forwarding keeps `self.action_running`
    meaning "the focused project's turn" while the value actually lives on that
    project's session.
    """

    def make(name: str) -> property:
        def getter(self: "RevvyApp") -> Any:
            return getattr(_session_of(self), name)

        def setter(self: "RevvyApp", value: Any) -> None:
            setattr(_session_of(self), name, value)

        return property(getter, setter)

    for field in ProjectSession.FIELDS:
        setattr(RevvyApp, field, make(field))


class RevvyApp:
    def __init__(self, root: tk.Tk, *, smoke_test: bool = False):
        self.root = root
        self.smoke_test = smoke_test
        host_project = core.detect_host_project_dir()
        if host_project:
            if not core.same_project_path(_BOOTSTRAP_PROJECT_DIR, host_project):
                core.bind_project_state(host_project)
                revvy.configure_runtime_paths()
            settings = core.load_project_settings(
                host_project,
                env=os.environ,
                migrate_legacy=not smoke_test,
            )
        else:
            settings = core.load_settings()
        host_project_changed = _adopt_packaged_host_project(settings)
        settings.project_dir = _default_project(settings)
        # Every attribute in ProjectSession.FIELDS is a property forwarding onto
        # self._session, so the focused session has to exist before the first of
        # them is assigned.
        # The host project may keep a deployment override it was launched with;
        # every later project gets its own <project>/.revvy.
        self._session = ProjectSession(
            core.Workspace.open(settings.project_dir, inherit_overrides=True),
            settings,
        )
        self.sessions[self._session.key] = self._session
        # What the widgets are showing. Normally the same object as _session,
        # but a queued callback temporarily retargets _session at the project
        # that owns it while the screen keeps showing this one.
        self._visible = self._session
        self.engine = core.resolve_engine(
            self.settings.project_dir,
            self.settings.engine_override,
            self._proxy_config_engine(
                self.settings.project_dir,
                self.workspace.config_path() if self.workspace is not None else None,
            ),
        )
        if not smoke_test:
            if host_project_changed:
                try:
                    core.save_settings(
                        self.settings,
                        self.workspace.settings_path()
                        if self.workspace is not None
                        else None,
                    )
                except OSError:
                    pass
            core.write_proxy_project_context(
                self.settings.project_dir,
                self.engine,
                path=self.workspace.config_path() if self.workspace is not None else None,
            )
            try:
                core.repair_blender_upstream_command(
                    self.settings.project_dir,
                    env=self.workspace.env if self.workspace is not None else None,
                )
            except OSError:
                pass
        self.codex_models, self.codex_default = core.discover_codex_models()
        self.kimi_models, self.kimi_default = core.discover_kimi_models()
        self.models_by_provider = {
            core.PROVIDER_CODEX: self.codex_models,
            core.PROVIDER_CLAUDE: list(core.CLAUDE_MODELS),
            core.PROVIDER_KIMI: self.kimi_models,
        }
        self.provider_default_models = {
            core.PROVIDER_CODEX: self.codex_default,
            core.PROVIDER_CLAUDE: "default",
            core.PROVIDER_KIMI: self.kimi_default,
        }
        self.provider_key = self.settings.provider
        if not self.settings.model or self.settings.model not in self.models_by_provider[self.provider_key]:
            self.settings.model = self.provider_default_models[self.provider_key]
        if self.settings.effort_settings_version >= 1:
            codex_effort = core.initial_reasoning_effort(
                self.settings, core.PROVIDER_CODEX
            )
            claude_effort = core.initial_reasoning_effort(
                self.settings, core.PROVIDER_CLAUDE
            )
            kimi_effort = core.initial_reasoning_effort(
                self.settings, core.PROVIDER_KIMI
            )
        else:
            codex_effort = core.initial_reasoning_effort(
                self.settings,
                core.PROVIDER_CODEX,
                core.configured_codex_reasoning_effort(),
            )
            claude_effort = core.initial_reasoning_effort(
                self.settings,
                core.PROVIDER_CLAUDE,
                core.configured_claude_reasoning_effort(),
            )
            kimi_effort = core.initial_reasoning_effort(
                self.settings,
                core.PROVIDER_KIMI,
            )
        self.reasoning_by_provider = {
            core.PROVIDER_CODEX: codex_effort,
            core.PROVIDER_CLAUDE: claude_effort,
            core.PROVIDER_KIMI: kimi_effort,
        }
        self.reasoning_effort = self.reasoning_by_provider[self.provider_key]
        self.permission_mode = self.settings.permission_mode

        # Shell-wide. Unlike the ProjectSession block these are properties of the
        # window and the machine, and are shared by every open project.
        self.ui_queue: queue.Queue[
            tuple[Callable[..., None], tuple[Any, ...], ProjectSession | None]
        ] = queue.Queue()
        # Bare _post() calls made inside _run_worker inherit the project that
        # launched that worker even if focus changes while it is running.
        self._worker_context = threading.local()
        self.process_lock = threading.RLock()
        # revvy.py is still a module-global CLI runtime. Every temporary project
        # binding goes through this separate lock so proxy cold starts never
        # block the UI's cancel/close process lock and two projects cannot split
        # the module paths between them.
        self.runtime_lock = threading.RLock()
        self.status_running = False
        self.closed = False
        self._closing_requested = False
        self._toolchain_offer_active = False
        self._toolchain_prompt_shown = False
        self._toolchain_probe_running = False
        self._install_worker_running = False
        self._install_worker_session: ProjectSession | None = None
        self.history = (
            core.load_chat_history(
                self._history_project_dir,
                self.workspace.chat_history_path()
                if self.workspace is not None
                else None,
            )
            if self.settings.remember_history and not smoke_test
            else []
        )

        self.advanced_visible = False
        self._theme_fade_running = False
        COLORS.clear()
        COLORS.update(self._themed_colors())
        self._configure_root()
        self._load_brand_assets()
        self._configure_styles()
        self._build_ui()
        self._restore_chat_history()
        self._set_icon()
        self.root.protocol("WM_DELETE_WINDOW", self.close)
        self.root.after(40, self._drain_queue)
        if not smoke_test:
            self.root.after(100, self.refresh_status)
            self.root.after(200, self.refresh_login)
            self.root.after(350, self.load_tools)
            self.root.after(900, self._maybe_offer_build_toolchain)
            self.root.after(CHAT_LIVENESS_TICK_MS, self._chat_liveness_tick)
            # Late enough that a slow or unreachable feed cannot delay the window
            # appearing. An update check is never worth a blank screen.
            self.root.after(2500, self._check_for_updates)

    def _configure_root(self) -> None:
        self.root.title(f"Revvy {core.APP_VERSION}")
        self.root.geometry(self.settings.geometry)
        self.root.minsize(1040, 680)
        self.root.configure(background=COLORS["window"])

    def _resource_candidates(self, filename: str) -> list[pathlib.Path]:
        candidates: list[pathlib.Path] = []
        if getattr(sys, "frozen", False) and hasattr(sys, "_MEIPASS"):
            candidates.append(pathlib.Path(sys._MEIPASS) / "Resources" / filename)
        try:
            candidates.append(SCRIPT_DIR.parents[1] / "Resources" / filename)
        except IndexError:
            pass
        return candidates

    def _themed_mascot(self, path: pathlib.Path) -> tk.PhotoImage:
        """레비, readable on whichever surface the theme puts behind him.

        The artwork is drawn for a dark background: the body is #f0f0f0 with a
        near-black face. On the light theme's white chrome the body vanished and
        all that was left was a dark blob. Rather than sit him on a badge - a
        heavy dark square on white - the near-white body is darkened to the
        theme's text colour, which keeps the silhouette and the mint eyes.
        """
        image = Image.open(path).convert("RGBA")
        if self.settings.theme != "light":
            return image
        body = tuple(int(COLORS["text"][index : index + 2], 16) for index in (1, 3, 5))
        pixels = image.load()
        width, height = image.size
        for y in range(height):
            for x in range(width):
                red, green, blue, alpha = pixels[x, y]
                if alpha == 0:
                    continue
                # The body is the only near-white, near-neutral region; the face,
                # the eyes and the ear lights are all saturated or dark.
                if min(red, green, blue) > 200 and max(red, green, blue) - min(
                    red, green, blue
                ) < 20:
                    pixels[x, y] = (*body, alpha)
        return image

    def _load_brand_assets(self) -> None:
        self._mascot_source: ImageTk.PhotoImage | None = None
        self._mascot_header: ImageTk.PhotoImage | None = None
        self._mascot_empty: ImageTk.PhotoImage | None = None
        self._mascot_inline: ImageTk.PhotoImage | None = None
        for candidate in self._resource_candidates("Revvy_256.png"):
            if not candidate.is_file():
                continue
            try:
                art = self._themed_mascot(candidate)
            except (OSError, ValueError):
                continue
            # Resampled by PIL rather than PhotoImage.subsample: subsample only
            # drops rows and columns, so 256 -> 16 threw away 15 of every 16
            # pixels and left the ears ragged. LANCZOS averages them.
            try:
                self._mascot_source = ImageTk.PhotoImage(art)
                self._mascot_header = self._scaled_mascot(art, 51)
                self._mascot_empty = self._scaled_mascot(art, 128)
                # 16, not 26: at 26px the inline icon stood twice as tall as the
                # Semibold 9 name it captions and out-shouted it.
                self._mascot_inline = self._scaled_mascot(art, 16)
                return
            except tk.TclError:
                continue

    @staticmethod
    def _scaled_mascot(art: Image.Image, size: int) -> ImageTk.PhotoImage:
        return ImageTk.PhotoImage(art.resize((size, size), Image.LANCZOS))

    def _configure_styles(self) -> None:
        style = ttk.Style(self.root)
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass
        style.configure("TFrame", background=COLORS["window"])
        style.configure("Header.TFrame", background=COLORS["header"])
        style.configure("Rail.TFrame", background=COLORS["rail"])
        style.configure("Context.TFrame", background=COLORS["surface_alt"])
        style.configure("Surface.TFrame", background=COLORS["surface"])
        style.configure("Window.TFrame", background=COLORS["window"])
        style.configure(
            "TLabel", background=COLORS["window"], foreground=COLORS["text"], font=("Segoe UI", 10)
        )
        style.configure(
            "Header.TLabel",
            background=COLORS["header"],
            foreground=COLORS["header_text"],
            font=("Segoe UI", 10),
        )
        style.configure(
            "Brand.TLabel",
            background=COLORS["header"],
            foreground=COLORS["header_text"],
            font=("Segoe UI Semibold", 17),
        )
        style.configure(
            "Eyebrow.TLabel", background=COLORS["surface_alt"], foreground=COLORS["subtle"], font=("Segoe UI Semibold", 8)
        )
        style.configure("Context.TLabel", background=COLORS["surface_alt"], foreground=COLORS["text"], font=("Segoe UI", 9))
        style.configure("ContextMuted.TLabel", background=COLORS["surface_alt"], foreground=COLORS["muted"], font=("Segoe UI", 9))
        style.configure("Muted.TLabel", background=COLORS["window"], foreground=COLORS["muted"], font=("Segoe UI", 9))
        style.configure("Surface.TLabel", background=COLORS["surface"], foreground=COLORS["text"], font=("Segoe UI", 10))
        style.configure("SurfaceMuted.TLabel", background=COLORS["surface"], foreground=COLORS["muted"], font=("Segoe UI", 9))
        style.configure(
            "Online.TLabel",
            background=COLORS["online_soft"],
            foreground=COLORS["online"],
            font=("Segoe UI Semibold", 8),
            padding=(8, 4),
        )
        style.configure(
            "Offline.TLabel",
            background=COLORS["offline_soft"],
            foreground=COLORS["header_muted"],
            font=("Segoe UI", 8),
            padding=(8, 4),
        )
        style.configure(
            "Warning.TLabel",
            background=COLORS["warning_soft"],
            foreground=COLORS["warning"],
            font=("Segoe UI Semibold", 8),
            padding=(8, 4),
        )
        style.configure(
            "Accent.TButton",
            background=COLORS["accent"],
            foreground=COLORS["on_accent"],
            borderwidth=0,
            padding=(14, 8),
            font=("Segoe UI Semibold", 10),
        )
        style.map(
            "Accent.TButton",
            background=[("active", COLORS["accent_active"]), ("disabled", COLORS["border"])],
            foreground=[("disabled", COLORS["subtle"])],
        )
        style.configure(
            "TButton",
            background=COLORS["raised"],
            foreground=COLORS["text"],
            borderwidth=0,
            padding=(10, 7),
            font=("Segoe UI", 9),
        )
        style.map(
            "TButton",
            background=[("active", COLORS["border"]), ("pressed", COLORS["surface_alt"]), ("disabled", COLORS["surface_alt"])],
            foreground=[("disabled", COLORS["subtle"])],
        )
        style.configure("Toolbutton.TButton", padding=(8, 6), font=("Segoe UI Semibold", 9))
        style.configure(
            "Chip.TButton",
            background=COLORS["chip"],
            foreground=COLORS["text"],
            borderwidth=1,
            bordercolor=COLORS["border"],
            lightcolor=COLORS["border"],
            darkcolor=COLORS["border"],
            padding=(10, 6),
            font=("Segoe UI Semibold", 9),
        )
        style.map(
            "Chip.TButton",
            background=[("active", COLORS["select_soft"]), ("pressed", COLORS["raised"])],
            foreground=[("active", COLORS["accent_text"])],
        )
        style.configure(
            "Danger.TButton",
            background=COLORS["surface_alt"],
            foreground=COLORS["danger"],
            borderwidth=1,
            padding=(11, 7),
            font=("Segoe UI Semibold", 9),
        )
        style.map("Danger.TButton", background=[("active", COLORS["danger_soft"])])
        style.configure(
            "Unreal.TButton",
            background=COLORS["accent"],
            foreground=COLORS["on_accent"],
            borderwidth=0,
            padding=(10, 7),
            font=("Segoe UI Semibold", 9),
        )
        style.map(
            "Unreal.TButton",
            background=[("active", COLORS["accent_active"]), ("disabled", COLORS["border"])],
            foreground=[
                ("active", COLORS["on_accent"]),
                ("disabled", COLORS["subtle"]),
            ],
        )
        style.configure(
            "Hidden.TNotebook",
            background=COLORS["window"],
            bordercolor=COLORS["window"],
            lightcolor=COLORS["window"],
            darkcolor=COLORS["window"],
            borderwidth=0,
            tabmargins=0,
        )
        style.layout("Hidden.TNotebook.Tab", [])
        # ttk never themed its scrollbars here, so the context panel and the
        # activity list were drawing clam defaults - a near-white thumb at 12:1
        # against a dark surface, louder than anything it sits beside.
        style.configure(
            "Vertical.TScrollbar",
            background=COLORS["border"],
            troughcolor=COLORS["surface"],
            bordercolor=COLORS["surface"],
            arrowcolor=COLORS["subtle"],
            borderwidth=0,
            relief=tk.FLAT,
            arrowsize=10,
            width=10,
        )
        style.map(
            "Vertical.TScrollbar",
            background=[("active", COLORS["subtle"]), ("disabled", COLORS["surface"])],
        )
        # The activity bar is drawn by PillProgress, which resolves those style
        # names to palette tones itself. No ttk progressbar is left to configure.
        style.configure(
            "Treeview",
            background=COLORS["surface_alt"],
            foreground=COLORS["text"],
            fieldbackground=COLORS["surface_alt"],
            rowheight=32,
            borderwidth=0,
            font=("Segoe UI", 9),
        )
        style.map("Treeview", background=[("selected", COLORS["select_soft"])], foreground=[("selected", COLORS["text"])])
        style.configure(
            "Treeview.Heading",
            background=COLORS["deep"],
            foreground=COLORS["muted"],
            borderwidth=0,
            padding=(8, 8),
            font=("Segoe UI Semibold", 9),
        )
        style.map("Treeview.Heading", background=[("active", COLORS["surface_alt"])])
        style.configure(
            "TCombobox",
            fieldbackground=COLORS["deep"],
            background=COLORS["raised"],
            foreground=COLORS["text"],
            arrowcolor=COLORS["accent_text"],
            bordercolor=COLORS["border"],
            lightcolor=COLORS["border"],
            darkcolor=COLORS["border"],
            padding=5,
        )
        style.map(
            "TCombobox",
            fieldbackground=[("readonly", COLORS["deep"])],
            foreground=[("readonly", COLORS["text"])],
            selectbackground=[("readonly", COLORS["deep"])],
            selectforeground=[("readonly", COLORS["text"])],
        )
        style.configure(
            "TEntry",
            fieldbackground=COLORS["deep"],
            foreground=COLORS["text"],
            insertcolor=COLORS["text"],
            bordercolor=COLORS["border"],
            lightcolor=COLORS["border"],
            darkcolor=COLORS["border"],
            padding=6,
        )
        style.configure("TSeparator", background=COLORS["border"])
        self.root.option_add("*TCombobox*Listbox.background", COLORS["surface_alt"])
        self.root.option_add("*TCombobox*Listbox.foreground", COLORS["text"])
        self.root.option_add("*TCombobox*Listbox.selectBackground", COLORS["select_soft"])
        self.root.option_add("*TCombobox*Listbox.selectForeground", COLORS["text"])

    def _build_ui(self) -> None:
        self.root.grid_rowconfigure(2, weight=1)
        self.root.grid_columnconfigure(0, weight=1)

        accent_line = tk.Frame(self.root, background=COLORS["accent"], height=4)
        accent_line.grid(row=0, column=0, sticky="ew")

        header = ttk.Frame(self.root, style="Header.TFrame", height=72)
        header.grid(row=1, column=0, sticky="new")
        header.grid_propagate(False)
        header.grid_columnconfigure(1, weight=1)

        brand = ttk.Frame(header, style="Header.TFrame")
        brand.grid(row=0, column=0, sticky="w", padx=(18, 8), pady=9)
        if self._mascot_header is not None:
            tk.Label(
                brand,
                image=self._mascot_header,
                background=COLORS["header"],
                borderwidth=0,
            ).pack(side=tk.LEFT)
        brand_text = ttk.Frame(brand, style="Header.TFrame")
        brand_text.pack(side=tk.LEFT, padx=(9, 0))
        ttk.Label(brand_text, text="Revvy", style="Brand.TLabel").pack(anchor=tk.W)
        ttk.Label(
            brand_text,
            text=_engine_brand(self.engine)["eyebrow"],
            style="Header.TLabel",
            foreground=COLORS["accent"],
            font=("Segoe UI Semibold", 8),
        ).pack(anchor=tk.W)

        project_head = ttk.Frame(header, style="Header.TFrame")
        project_head.grid(row=0, column=1, sticky="w", padx=28)
        self.header_project_var = tk.StringVar(value=pathlib.Path(self.settings.project_dir).name or "Unreal Project")
        ttk.Label(
            project_head,
            text="CURRENT PROJECT",
            style="Header.TLabel",
            foreground=COLORS["header_muted"],
            font=("Segoe UI Semibold", 8),
        ).pack(anchor=tk.W)
        ttk.Label(
            project_head,
            textvariable=self.header_project_var,
            style="Header.TLabel",
            font=("Segoe UI Semibold", 13),
        ).pack(anchor=tk.W)

        # The folder name alone is ambiguous once you keep several playgrounds
        # around, so the full path sits under it with a one-click copy.
        path_row = ttk.Frame(project_head, style="Header.TFrame")
        path_row.pack(anchor=tk.W, pady=(1, 0))
        self.header_path_var = tk.StringVar(value=self.settings.project_dir)
        ttk.Label(
            path_row,
            textvariable=self.header_path_var,
            style="Header.TLabel",
            foreground=COLORS["header_muted"],
            font=("Segoe UI", 8),
        ).pack(side=tk.LEFT)
        self.header_copy_button = tk.Button(
            path_row,
            text="⧉",
            command=self.copy_project_path,
            relief=tk.FLAT,
            borderwidth=0,
            highlightthickness=0,
            cursor="hand2",
            background=COLORS["header"],
            foreground=COLORS["header_muted"],
            activebackground=COLORS["header"],
            activeforeground=COLORS["accent"],
            font=("Segoe UI", 9),
            padx=6,
            pady=0,
        )
        self.header_copy_button.pack(side=tk.LEFT)

        status_row = ttk.Frame(header, style="Header.TFrame")
        status_row.grid(row=0, column=2, sticky="e", padx=(8, 18), pady=16)
        self.proxy_status = ttk.Label(status_row, text="● 프록시 확인 중", style="Warning.TLabel")
        self.proxy_status.pack(side=tk.LEFT, padx=(0, 6))
        editor_chip_text = (
            "● Unreal 확인 중"
            if self.engine == core.ENGINE_UNREAL
            else f"● {_engine_brand(self.engine)['label']} 브리지 준비 중"
        )
        self.unreal_status = ttk.Label(status_row, text=editor_chip_text, style="Warning.TLabel")
        self.unreal_status.pack(side=tk.LEFT, padx=(0, 6))
        self.blender_status = ttk.Label(
            status_row, text="● Blender 확인 중", style="Warning.TLabel"
        )
        self.blender_status.pack(side=tk.LEFT, padx=(0, 6))
        self.provider_status = ttk.Label(status_row, text="● 로그인 확인 중", style="Warning.TLabel")
        self.provider_status.pack(side=tk.LEFT)

        body = ttk.Frame(self.root, style="Window.TFrame")
        body.grid(row=2, column=0, sticky="nsew")
        body.grid_rowconfigure(0, weight=1)
        body.grid_columnconfigure(0, minsize=84)
        # Column 1 is the project list. It only takes space once a second project
        # is open - with one project there is nothing to choose between, and a
        # list of one is just a wider window for no reason.
        body.grid_columnconfigure(1, minsize=0)
        body.grid_columnconfigure(2, weight=1)
        body.grid_columnconfigure(3, minsize=304)

        rail = ttk.Frame(body, style="Rail.TFrame", width=84)
        rail.grid(row=0, column=0, sticky="nsew")
        rail.pack_propagate(False)
        self.nav_items: list[tuple[tk.Frame, tk.Button]] = []
        for index, (label, caption) in enumerate(NAV_ITEMS):
            nav_item = tk.Frame(rail, background=COLORS["rail"], height=64)
            nav_item.pack(fill=tk.X)
            nav_item.pack_propagate(False)
            indicator = tk.Frame(nav_item, background=COLORS["rail"], width=4)
            indicator.pack(side=tk.LEFT, fill=tk.Y)
            button = tk.Button(
                nav_item,
                text=f"{label}\n{caption}",
                command=lambda tab_index=index: self._select_tab(tab_index),
                background=COLORS["rail"],
                foreground=COLORS["header_muted"],
                activebackground=COLORS["rail_hover"],
                activeforeground=COLORS["accent"],
                relief=tk.FLAT,
                borderwidth=0,
                highlightthickness=0,
                cursor="hand2",
                font=("Segoe UI Semibold", 9),
            )
            button.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
            self.nav_items.append((indicator, button))

        is_dark = self.settings.theme == "dark"
        self.theme_button = tk.Button(
            rail,
            text="◐\n라이트" if is_dark else "◑\n다크",
            command=self._toggle_theme,
            background=COLORS["rail"],
            foreground=COLORS["header_muted"],
            activebackground=COLORS["rail_hover"],
            activeforeground=COLORS["accent"],
            relief=tk.FLAT,
            borderwidth=0,
            highlightthickness=0,
            cursor="hand2",
            font=("Segoe UI Semibold", 9),
        )
        self.theme_button.pack(side=tk.BOTTOM, fill=tk.X, ipady=8, pady=(0, 8))

        self._build_project_rail(body)

        content = ttk.Frame(body, style="Window.TFrame", padding=(20, 17, 18, 11))
        content.grid(row=0, column=2, sticky="nsew")
        content.grid_rowconfigure(0, weight=1)
        content.grid_columnconfigure(0, weight=1)

        # The context panel is the densest surface in the app and it grows with
        # every feature. Fixed-height, anything added past the 1040x680 minimum
        # silently fell off the bottom, so it scrolls. The inner frame keeps the
        # name `context`, which leaves every widget packed into it untouched.
        # Two panes share the right column: the session controls this app has
        # always had, and a file explorer. Tabs rather than stacking because at
        # 304px only one of them can be usable at a time.
        right_shell = ttk.Frame(body, style="Context.TFrame", width=RIGHT_PANEL_WIDTH)
        right_shell.grid(row=0, column=3, sticky="nsew")
        right_shell.grid_propagate(False)
        right_shell.grid_rowconfigure(1, weight=1)
        right_shell.grid_columnconfigure(0, weight=1)

        self.right_tab_var = tk.StringVar(value="session")
        tab_row = ttk.Frame(right_shell, style="Context.TFrame", padding=(12, 10, 12, 0))
        tab_row.grid(row=0, column=0, sticky="ew")
        for column, (key, label) in enumerate((("session", "세션"), ("files", "파일"))):
            tab_row.grid_columnconfigure(column, weight=1)
            ttk.Radiobutton(
                tab_row,
                text=label,
                value=key,
                variable=self.right_tab_var,
                command=self._select_right_tab,
                style="Chip.TButton",
            ).grid(row=0, column=column, sticky="ew", padx=(0, 4) if column == 0 else 0)

        context_shell = ttk.Frame(right_shell, style="Context.TFrame")
        context_shell.grid(row=1, column=0, sticky="nsew")
        context_shell.grid_rowconfigure(0, weight=1)
        context_shell.grid_columnconfigure(0, weight=1)
        self._context_shell = context_shell
        self._right_shell = right_shell

        self.context_canvas = tk.Canvas(
            context_shell,
            background=COLORS["surface_alt"],
            highlightthickness=0,
            borderwidth=0,
        )
        self.context_canvas.grid(row=0, column=0, sticky="nsew")
        self.context_scroll = ttk.Scrollbar(
            context_shell, orient=tk.VERTICAL, command=self.context_canvas.yview
        )
        self.context_canvas.configure(yscrollcommand=self._sync_context_scrollbar)

        context = ttk.Frame(self.context_canvas, style="Context.TFrame", padding=(18, 17))
        self._context_window = self.context_canvas.create_window(
            (0, 0), window=context, anchor="nw"
        )
        context.bind(
            "<Configure>",
            lambda _event: self.context_canvas.configure(
                scrollregion=self.context_canvas.bbox("all")
            ),
        )
        self.context_canvas.bind(
            "<Configure>",
            lambda event: self.context_canvas.itemconfigure(
                self._context_window, width=event.width
            ),
        )
        self._bind_mouse_wheel(self.context_canvas)
        self._build_file_panel(right_shell)
        ttk.Label(
            context,
            text="세션 설정",
            style="Context.TLabel",
            font=("Segoe UI Semibold", 11),
        ).pack(anchor=tk.W, pady=(0, 10))

        ttk.Label(context, text="AI 엔진", style="ContextMuted.TLabel").pack(anchor=tk.W)
        self.provider_var = tk.StringVar(value=core.PROVIDER_LABELS[self.provider_key])
        self.provider_combo = ttk.Combobox(
            context,
            state="readonly",
            textvariable=self.provider_var,
            values=[core.PROVIDER_LABELS[key] for key in core.PROVIDERS],
        )
        self.provider_combo.pack(fill=tk.X, pady=(5, 14))
        self.provider_combo.bind("<<ComboboxSelected>>", self._provider_changed)

        model_settings = ttk.Frame(context, style="Context.TFrame")
        model_settings.pack(fill=tk.X, pady=(0, 14))
        model_group = ttk.Frame(model_settings, style="Context.TFrame")
        model_group.pack(side=tk.LEFT, fill=tk.X, expand=True)
        ttk.Label(model_group, text="모델", style="ContextMuted.TLabel").pack(anchor=tk.W)
        self.model_var = tk.StringVar(value=self.settings.model)
        self.model_combo = ttk.Combobox(
            model_group,
            state="readonly",
            textvariable=self.model_var,
            values=self.models_by_provider[self.provider_key],
        )
        self.model_combo.pack(fill=tk.X, pady=(5, 0))
        self.model_combo.bind("<<ComboboxSelected>>", self._model_changed)

        effort_group = ttk.Frame(model_settings, style="Context.TFrame")
        effort_group.pack(side=tk.LEFT, padx=(8, 0))
        ttk.Label(effort_group, text="추론", style="ContextMuted.TLabel").pack(anchor=tk.W)
        self.effort_var = tk.StringVar()
        self.effort_combo = ttk.Combobox(
            effort_group,
            state="readonly",
            width=9,
            textvariable=self.effort_var,
        )
        self.effort_combo.pack(pady=(5, 0))
        self.effort_combo.bind("<<ComboboxSelected>>", self._effort_changed)
        self._update_effort_options()

        ttk.Label(context, text="권한", style="ContextMuted.TLabel").pack(anchor=tk.W)
        self.permission_var = tk.StringVar(value=PERMISSION_LABELS[self.permission_mode])
        self.permission_combo = ttk.Combobox(
            context,
            state="readonly",
            textvariable=self.permission_var,
            values=[PERMISSION_LABELS[mode] for mode in core.PERMISSION_MODES],
        )
        self.permission_combo.pack(fill=tk.X, pady=(5, 14))
        self.permission_combo.bind("<<ComboboxSelected>>", self._permission_changed)

        ttk.Separator(context).pack(fill=tk.X, pady=14)
        ttk.Label(
            context,
            text="프로젝트 설정",
            style="Context.TLabel",
            font=("Segoe UI Semibold", 11),
        ).pack(anchor=tk.W, pady=(0, 10))
        project_row = ttk.Frame(context, style="Context.TFrame")
        project_row.pack(fill=tk.X, pady=(0, 8))
        self.project_var = tk.StringVar(value=self.settings.project_dir)
        self.project_entry = ttk.Entry(project_row, textvariable=self.project_var)
        self.project_entry.pack(side=tk.LEFT, fill=tk.X, expand=True)
        self.project_browse_button = ttk.Button(
            project_row,
            text="...",
            width=3,
            command=self.browse_project,
        )
        self.project_browse_button.pack(side=tk.LEFT, padx=(5, 0))

        self.project_hint = ttk.Label(context, text="", style="ContextMuted.TLabel", wraplength=248)
        self.project_hint.pack(anchor=tk.W, fill=tk.X)

        ttk.Label(context, text="엔진", style="ContextMuted.TLabel").pack(anchor=tk.W, pady=(9, 0))
        self.engine_var = tk.StringVar(value=ENGINE_LABELS[self.settings.engine_override])
        self.engine_combo = ttk.Combobox(
            context,
            state="readonly",
            textvariable=self.engine_var,
            values=list(ENGINE_LABELS.values()),
        )
        self.engine_combo.pack(fill=tk.X, pady=(5, 0))
        self.engine_combo.bind("<<ComboboxSelected>>", self._engine_changed)

        unreal_buttons = ttk.Frame(context, style="Context.TFrame")
        unreal_buttons.pack(fill=tk.X, pady=(9, 0))
        launch_text = "▶ 에디터" if self.engine != core.ENGINE_UNREAL else "▶ 실행"
        secondary_text = (
            "▶ 게임 실행"
            if self.engine == core.ENGINE_GODOT
            else "⚙ 빌드 + 실행"
        )
        self.unreal_launch_button = ttk.Button(
            unreal_buttons,
            text=launch_text,
            command=lambda: self.start_engine(build=False),
            style="Toolbutton.TButton",
        )
        self.unreal_launch_button.pack(side=tk.LEFT, fill=tk.X, expand=True)
        self.unreal_build_button = ttk.Button(
            unreal_buttons,
            text=secondary_text,
            command=lambda: self.start_engine(build=True),
            style="Unreal.TButton",
        )
        self.unreal_build_button.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(6, 0))
        self.unreal_state_var = tk.StringVar(value="오프라인")
        ttk.Label(
            context,
            textvariable=self.unreal_state_var,
            style="ContextMuted.TLabel",
        ).pack(anchor=tk.W, pady=(8, 0))

        ttk.Separator(context).pack(fill=tk.X, pady=14)
        ttk.Label(
            context,
            text="MCP 연결",
            style="Context.TLabel",
            font=("Segoe UI Semibold", 11),
        ).pack(anchor=tk.W, pady=(0, 8))
        mcp_row = ttk.Frame(context, style="Context.TFrame")
        mcp_row.pack(fill=tk.X)
        self.mcp_state_var = tk.StringVar(value="MCP 연결 확인 중")
        ttk.Label(
            mcp_row,
            textvariable=self.mcp_state_var,
            style="ContextMuted.TLabel",
        ).pack(side=tk.LEFT)
        self.mcp_add_button = ttk.Button(
            mcp_row,
            text="＋ MCP 추가",
            command=self.open_mcp_config,
            style="Toolbutton.TButton",
        )
        self.mcp_add_button.pack(side=tk.RIGHT)

        ttk.Separator(context).pack(fill=tk.X, pady=14)
        self.advanced_toggle_button = ttk.Button(
            context,
            text="▸  고급 설정 열기",
            command=self._toggle_advanced,
            style="Toolbutton.TButton",
        )
        self.advanced_toggle_button.pack(fill=tk.X)
        advanced = ttk.Frame(context, style="Context.TFrame")
        self.advanced_frame = advanced

        self.unreal_maintenance_button = ttk.Button(
            advanced,
            text="↻ 캐시 정리 + 프로젝트 파일 재생성",
            command=self.clean_and_regenerate_project,
            style="Toolbutton.TButton",
        )
        self.unreal_maintenance_button.pack(fill=tk.X, pady=(4, 0))

        ttk.Label(advanced, text="BRIDGE", style="Eyebrow.TLabel").pack(anchor=tk.W, pady=(12, 8))
        status_buttons = ttk.Frame(advanced, style="Context.TFrame")
        status_buttons.pack(fill=tk.X)
        self.start_button = ttk.Button(status_buttons, text="시작", command=self.start_proxy, style="Toolbutton.TButton")
        self.start_button.pack(side=tk.LEFT, fill=tk.X, expand=True)
        self.restart_button = ttk.Button(status_buttons, text="재시작", command=self.restart_proxy, style="Toolbutton.TButton")
        self.restart_button.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=6)
        ttk.Button(status_buttons, text="갱신", command=self.refresh_status, style="Toolbutton.TButton").pack(
            side=tk.LEFT, fill=tk.X, expand=True
        )

        ttk.Label(advanced, text="BLENDER", style="Eyebrow.TLabel").pack(
            anchor=tk.W, pady=(12, 8)
        )
        blender_buttons = ttk.Frame(advanced, style="Context.TFrame")
        blender_buttons.pack(fill=tk.X)
        self.blender_setup_button = ttk.Button(
            blender_buttons,
            text="연결 준비",
            command=self.setup_blender_bridge,
            style="Toolbutton.TButton",
        )
        self.blender_setup_button.pack(side=tk.LEFT, fill=tk.X, expand=True)
        self.blender_launch_button = ttk.Button(
            blender_buttons,
            text="실행",
            command=self.start_blender,
            style="Toolbutton.TButton",
        )
        self.blender_launch_button.pack(
            side=tk.LEFT, fill=tk.X, expand=True, padx=(6, 0)
        )
        self.blender_state_var = tk.StringVar(value="브리지 확인 중")
        ttk.Label(
            advanced,
            textvariable=self.blender_state_var,
            style="ContextMuted.TLabel",
            wraplength=248,
        ).pack(anchor=tk.W, fill=tk.X, pady=(7, 0))

        ttk.Separator(advanced).pack(fill=tk.X, pady=12)
        ttk.Button(advanced, text="상태 폴더 열기", command=self.open_state_folder).pack(fill=tk.X)
        self.mcp_endpoint_var = tk.StringVar(value=_proxy_endpoint_label())
        ttk.Label(
            advanced,
            textvariable=self.mcp_endpoint_var,
            style="ContextMuted.TLabel",
            font=("Cascadia Mono", 8),
            wraplength=248,
        ).pack(anchor=tk.W, fill=tk.X, pady=(10, 0))
        if self.advanced_visible:
            self.advanced_frame.pack(fill=tk.X, pady=(8, 0))
            self.advanced_toggle_button.configure(text="▾  고급 설정 닫기")

        self.notebook = ttk.Notebook(content, style="Hidden.TNotebook")
        self.notebook.grid(row=0, column=0, sticky="nsew")
        self.chat_tab = ttk.Frame(self.notebook, style="Window.TFrame")
        self.tools_tab = ttk.Frame(self.notebook, style="Window.TFrame")
        self.direct_tab = ttk.Frame(self.notebook, style="Window.TFrame")
        self.logs_tab = ttk.Frame(self.notebook, style="Window.TFrame")
        self.notebook.add(self.chat_tab, text="Chat")
        self.notebook.add(self.tools_tab, text="Tools")
        self.notebook.add(self.direct_tab, text="Direct call")
        self.notebook.add(self.logs_tab, text="Logs")
        self.notebook.bind("<<NotebookTabChanged>>", self._tab_changed)

        self._build_chat_tab()
        self._build_tools_tab()
        self._build_direct_tab()
        self._build_logs_tab()

        footer_frame = ttk.Frame(content, style="Window.TFrame")
        footer_frame.grid(row=1, column=0, sticky="ew", pady=(8, 0))
        footer_frame.grid_columnconfigure(1, weight=1)
        tk.Label(
            footer_frame,
            text="●",
            background=COLORS["window"],
            foreground=COLORS["accent"],
            font=("Segoe UI", 8),
        ).grid(row=0, column=0, sticky="w")
        self.footer_var = tk.StringVar(value="준비 완료! 레비가 기다리고 있어요")
        self.footer_label = ttk.Label(
            footer_frame,
            textvariable=self.footer_var,
            style="Muted.TLabel",
            anchor=tk.W,
            width=1,
        )
        self.footer_label.grid(row=0, column=1, sticky="ew", padx=(5, 10))
        self.footer_version_label = ttk.Label(
            footer_frame,
            text=f"REVVY DESKTOP  {core.APP_VERSION}",
            style="Muted.TLabel",
            foreground=COLORS["subtle"],
            font=("Segoe UI Semibold", 8),
        )
        # An update offer, not an interruption. It sits in the footer and waits;
        # Orca's toast says "세션은 중단되지 않습니다" for the same reason.
        self.update_banner = tk.Frame(footer_frame, background=COLORS["select_soft"])
        self.update_var = tk.StringVar(value="")
        tk.Label(
            self.update_banner,
            textvariable=self.update_var,
            background=COLORS["select_soft"],
            foreground=COLORS["accent_text"],
            padx=9,
            pady=2,
            font=("Segoe UI Semibold", 8),
        ).pack(side=tk.LEFT)
        self.update_button = ttk.Button(
            self.update_banner,
            text="설치하고 다시 시작",
            command=self.apply_update,
            style="Chip.TButton",
        )
        self.update_button.pack(side=tk.LEFT, padx=(6, 0))
        self.update_banner.grid(row=0, column=3, sticky="e", padx=(10, 0))
        self.update_banner.grid_remove()
        self.footer_version_label.grid(row=0, column=2, sticky="e")
        self._update_chat_context()
        self._update_project_header()
        self._select_tab(0)
        self._set_controls_busy(False)

    def _build_chat_tab(self) -> None:
        self.chat_tab.grid_rowconfigure(1, weight=1)
        self.chat_tab.grid_rowconfigure(2, minsize=0)
        self.chat_tab.grid_columnconfigure(0, weight=1)

        chat_toolbar = ttk.Frame(self.chat_tab, style="Window.TFrame")
        chat_toolbar.grid(row=0, column=0, sticky="ew", pady=(0, 11))
        title_group = ttk.Frame(chat_toolbar, style="Window.TFrame")
        title_group.pack(side=tk.LEFT)
        ttk.Label(
            title_group,
            text="CHAT WORKSPACE",
            style="Muted.TLabel",
            foreground=COLORS["accent_text"],
            font=("Segoe UI Semibold", 8),
        ).pack(anchor=tk.W)
        ttk.Label(
            title_group,
            text="레비와 대화",
            foreground=COLORS["text"],
            font=("Segoe UI Semibold", 17),
        ).pack(anchor=tk.W, pady=(1, 0))
        self.chat_context_var = tk.StringVar()
        self.chat_provider_context_var = tk.StringVar()
        self.chat_model_context_var = tk.StringVar()
        self.chat_effort_context_var = tk.StringVar()
        self.chat_permission_context_var = tk.StringVar()
        context_chips = tk.Frame(title_group, background=COLORS["window"])
        context_chips.pack(anchor=tk.W, pady=(5, 0))
        for variable in (
            self.chat_provider_context_var,
            self.chat_model_context_var,
            self.chat_effort_context_var,
        ):
            tk.Label(
                context_chips,
                textvariable=variable,
                background=COLORS["chip"],
                foreground=COLORS["muted"],
                padx=7,
                pady=2,
                font=("Segoe UI Semibold", 8),
            ).pack(side=tk.LEFT, padx=(0, 5))
        self.chat_permission_badge = tk.Label(
            context_chips,
            textvariable=self.chat_permission_context_var,
            background=COLORS["select_soft"],
            foreground=COLORS["accent_text"],
            padx=7,
            pady=2,
            font=("Segoe UI Semibold", 8),
        )
        self.chat_permission_badge.pack(side=tk.LEFT)
        ttk.Button(chat_toolbar, text="＋ 새 대화", command=self.new_chat).pack(side=tk.RIGHT, pady=3)

        self.chat_stage = tk.Frame(
            self.chat_tab,
            background=COLORS["surface"],
            highlightbackground=COLORS["border"],
            highlightthickness=1,
            borderwidth=0,
        )
        self.chat_stage.grid(row=1, column=0, sticky="nsew")
        self.chat_output = scrolledtext.ScrolledText(
            self.chat_stage,
            wrap=tk.WORD,
            state=tk.DISABLED,
            background=COLORS["surface"],
            foreground=COLORS["text"],
            selectbackground=COLORS["selection"],
            selectforeground=COLORS["text"],
            relief=tk.FLAT,
            borderwidth=0,
            highlightthickness=0,
            padx=24,
            pady=18,
            font=("Segoe UI", 10),
        )
        self.chat_output.pack(fill=tk.BOTH, expand=True)
        self._theme_scrolledtext_bar(self.chat_output)
        # One rail for both roles. A tagged background always spans
        # [padx, width - padx] no matter what lmargin/rmargin say - those inset
        # the text *inside* the band, they cannot move it. So the old lmargin 84
        # on the user block bought nothing but 60px of bare tint down its left
        # side, and the four different left edges never lined up with anything.
        CHAT_RAIL = 38
        self.chat_output.tag_configure(
            "user_label",
            foreground=COLORS["violet"],
            font=("Segoe UI Semibold", 9),
            spacing1=16,
            lmargin1=CHAT_RAIL,
            lmargin2=CHAT_RAIL,
            # Right-justified text lands on width - padx, which is exactly the
            # band's right edge, so the name and its block finally share one.
            rmargin=0,
            justify=tk.RIGHT,
        )
        self.chat_output.tag_configure(
            "assistant_label",
            foreground=COLORS["accent_text"],
            font=("Segoe UI Semibold", 9),
            spacing1=16,
            lmargin1=CHAT_RAIL,
            lmargin2=CHAT_RAIL,
        )
        self.chat_output.tag_configure(
            "user_body",
            background=COLORS["user_surface"],
            foreground=COLORS["text"],
            lmargin1=CHAT_RAIL,
            lmargin2=CHAT_RAIL,
            rmargin=CHAT_RAIL,
            # Painted, so these read as the band's top and bottom padding.
            spacing1=8,
            spacing3=10,
            justify=tk.LEFT,
        )
        self.chat_output.tag_configure(
            "assistant_body",
            foreground=COLORS["text"],
            lmargin1=CHAT_RAIL,
            lmargin2=CHAT_RAIL,
            rmargin=CHAT_RAIL,
            spacing1=8,
            spacing3=10,
        )
        self.chat_output.tag_configure(
            "error",
            foreground=COLORS["danger"],
            lmargin1=CHAT_RAIL,
            lmargin2=CHAT_RAIL,
            rmargin=CHAT_RAIL,
            spacing1=16,
            spacing3=10,
        )
        # A cancel is not a failure. The activity card already draws that
        # distinction - warning for "잠깐 멈춤", danger for "앗, 실패" - and the
        # transcript used to contradict it by painting both in danger.
        self.chat_output.tag_configure(
            "notice",
            foreground=COLORS["warning"],
            lmargin1=CHAT_RAIL,
            lmargin2=CHAT_RAIL,
            rmargin=CHAT_RAIL,
            spacing1=16,
            spacing3=10,
        )

        self.empty_state = tk.Frame(self.chat_stage, background=COLORS["surface"])
        empty_inner = tk.Frame(self.empty_state, background=COLORS["surface"])
        # Centred, not at 0.46. Attaching an image shrinks the stage without
        # hiding the empty state, and off-centre it pushed the top chip to a
        # negative y at the minimum window size.
        empty_inner.place(relx=0.5, rely=0.5, anchor=tk.CENTER)
        tk.Label(
            empty_inner,
            text="✦  PROJECT CONTEXT READY",
            background=COLORS["select_soft"],
            foreground=COLORS["accent_text"],
            padx=12,
            pady=5,
            font=("Segoe UI Semibold", 8),
        ).pack(pady=(0, 10))
        self._empty_mascot: tk.Label | None = None
        if self._mascot_empty is not None:
            self._empty_mascot = tk.Label(
                empty_inner,
                image=self._mascot_empty,
                background=COLORS["surface"],
                borderwidth=0,
            )
            self._empty_mascot.pack(pady=(0, 10))
        self._empty_inner = empty_inner
        # The mascot is the first thing to go when the stage gets short.
        # Attaching an image shrinks the stage without hiding the empty state,
        # and this block used to overflow off the top rather than shed anything.
        self.empty_state.bind("<Configure>", self._fit_empty_state)
        tk.Label(
            empty_inner,
            text="무엇을 함께 만들어볼까요?",
            background=COLORS["surface"],
            foreground=COLORS["text"],
            font=("Segoe UI Semibold", 15),
        ).pack()
        tk.Label(
            empty_inner,
            text="현재 프로젝트를 확인하고 정확한 Revvy 도구로 작업할게요.",
            background=COLORS["surface"],
            foreground=COLORS["muted"],
            font=("Segoe UI", 9),
        ).pack(pady=(5, 15))
        quick_row = ttk.Frame(empty_inner, style="Surface.TFrame")
        quick_row.pack()
        for label, prompt in (
            ("선택 에셋 분석", "현재 선택한 에셋을 조회하고 문제나 개선점을 점검해줘"),
            ("Blueprint 도움", "현재 Blueprint 구조를 확인하고 다음 구현 단계를 제안해줘"),
            ("Niagara 개선", "현재 Niagara 이펙트를 분석하고 품질 개선점을 제안해줘"),
        ):
            ttk.Button(
                quick_row,
                text=label,
                command=lambda value=prompt: self._use_quick_prompt(value),
                style="Chip.TButton",
            ).pack(side=tk.LEFT, padx=4)
        self._show_empty_chat()

        self.chat_activity = tk.Frame(
            self.chat_tab,
            background=COLORS["surface_alt"],
            highlightbackground=COLORS["border"],
            highlightthickness=1,
            borderwidth=0,
            height=ACTIVITY_PANEL_HEIGHT,
        )
        self.chat_activity.grid(row=2, column=0, sticky="ew", pady=(8, 0))
        self.chat_activity.grid_propagate(False)
        self.chat_activity.grid_columnconfigure(1, weight=1)
        self.chat_activity.grid_rowconfigure(2, weight=1)
        self.chat_activity_marker = tk.Label(
            self.chat_activity,
            text="" if self._mascot_inline is not None else "✦",
            image=self._mascot_inline if self._mascot_inline is not None else "",
            background=COLORS["surface_alt"],
            foreground=COLORS["accent"],
            font=("Segoe UI Semibold", 11),
        )
        self.chat_activity_marker.grid(row=0, column=0, padx=(11, 7), pady=7)
        self.chat_activity_var = tk.StringVar(value="레비 작업 노트")
        self.chat_activity_label = tk.Label(
            self.chat_activity,
            textvariable=self.chat_activity_var,
            background=COLORS["surface_alt"],
            foreground=COLORS["muted"],
            anchor=tk.W,
            font=("Segoe UI Semibold", 9),
        )
        self.chat_activity_label.grid(row=0, column=1, sticky="ew", pady=7)
        self.chat_activity_progress = PillProgress(
            self.chat_activity,
            mode="indeterminate",
            style="Activity.Horizontal.TProgressbar",
        )
        self.chat_activity_percent_var = tk.StringVar(value="READY")
        self.chat_activity_percent = tk.Label(
            self.chat_activity,
            textvariable=self.chat_activity_percent_var,
            background=COLORS["select_soft"],
            foreground=COLORS["accent_text"],
            padx=7,
            pady=2,
            font=("Segoe UI Semibold", 8),
        )
        # Appears the moment the agent's first edit streams in, and opens every
        # change it has made this turn. Waiting for the turn to end to show what
        # changed is waiting past the moment it was worth pointing at.
        self.edit_summary_var = tk.StringVar(value="")
        self.edit_summary_button = ttk.Button(
            self.chat_activity,
            textvariable=self.edit_summary_var,
            command=self.show_turn_edits,
            style="Chip.TButton",
        )
        self.edit_summary_button.grid(row=0, column=2, sticky="e", padx=(0, 4), pady=7)
        self.edit_summary_button.grid_remove()
        self.chat_activity_percent.grid(row=0, column=3, padx=(8, 11), pady=7)
        self.chat_activity_progress.grid(
            row=1,
            column=0,
            columnspan=4,
            sticky="ew",
            padx=11,
            pady=(0, 6),
        )
        # The step list scrolls inside a viewport of a fixed number of rows. The
        # card itself must not grow - see ACTIVITY_PANEL_HEIGHT - so the shell
        # holds the height and the inner frame is free to be as tall as the run.
        steps_shell = tk.Frame(
            self.chat_activity,
            background=COLORS["surface_alt"],
            height=ACTIVITY_VISIBLE_ROWS * ACTIVITY_ROW_HEIGHT,
        )
        steps_shell.grid(
            row=2,
            column=0,
            columnspan=4,
            sticky="nsew",
            padx=11,
            pady=(0, 7),
        )
        steps_shell.grid_propagate(False)
        steps_shell.grid_rowconfigure(0, weight=1)
        steps_shell.grid_columnconfigure(0, weight=1)
        self.activity_canvas = tk.Canvas(
            steps_shell,
            background=COLORS["surface_alt"],
            highlightthickness=0,
            borderwidth=0,
        )
        self.activity_canvas.grid(row=0, column=0, sticky="nsew")
        self.activity_scroll = ttk.Scrollbar(
            steps_shell, orient=tk.VERTICAL, command=self.activity_canvas.yview
        )
        self.activity_canvas.configure(yscrollcommand=self._sync_activity_scrollbar)
        # Keeping the inner frame's name means every row that grids into it, and
        # every index that reads it back, stays exactly as it was.
        self.chat_activity_steps = tk.Frame(
            self.activity_canvas, background=COLORS["surface_alt"]
        )
        self._activity_window = self.activity_canvas.create_window(
            (0, 0), window=self.chat_activity_steps, anchor="nw"
        )
        self.chat_activity_steps.bind(
            "<Configure>",
            lambda _event: self.activity_canvas.configure(
                scrollregion=self.activity_canvas.bbox("all")
            ),
        )
        self.activity_canvas.bind(
            "<Configure>",
            lambda event: self.activity_canvas.itemconfigure(
                self._activity_window, width=event.width
            ),
        )
        self._bind_mouse_wheel(self.activity_canvas)
        self.chat_activity_steps.grid_columnconfigure(1, weight=1)
        self.chat_activity_rows: list[dict[str, tk.Label]] = []
        self._ensure_activity_rows(ACTIVITY_VISIBLE_ROWS)
        self._hide_chat_activity()
        self._build_watch_strip()

        self.chat_composer = tk.Frame(
            self.chat_tab,
            background=COLORS["composer"],
            highlightbackground=COLORS["border"],
            highlightcolor=COLORS["border_focus"],
            highlightthickness=1,
            borderwidth=0,
        )
        self.chat_composer.grid(row=4, column=0, sticky="ew", pady=(10, 0))
        self.chat_composer.grid_columnconfigure(0, weight=1)

        self.attachment_strip = tk.Frame(self.chat_composer, background=COLORS["composer"])
        self.attachment_strip.grid(row=0, column=0, columnspan=2, sticky="ew", padx=9, pady=(8, 2))
        self.attachment_chips = tk.Frame(self.attachment_strip, background=COLORS["composer"])
        self.attachment_chips.pack(side=tk.LEFT, fill=tk.X)
        self.attachment_strip.grid_remove()

        self.chat_input = tk.Text(
            self.chat_composer,
            height=3,
            wrap=tk.WORD,
            background=COLORS["surface"],
            foreground=COLORS["text"],
            insertbackground=COLORS["text"],
            selectbackground=COLORS["selection"],
            selectforeground=COLORS["text"],
            relief=tk.FLAT,
            borderwidth=0,
            highlightthickness=0,
            padx=13,
            pady=11,
            undo=True,
            font=("Segoe UI", 10),
        )
        self.chat_input.grid(row=1, column=0, rowspan=2, sticky="ew")
        self.chat_input.bind("<Return>", self._send_shortcut)
        self.chat_input.bind("<Shift-Return>", self._newline_shortcut)
        self.chat_input.bind("<KP_Enter>", self._send_shortcut)
        self.chat_input.bind("<Shift-KP_Enter>", self._newline_shortcut)
        self.chat_input.bind("<FocusIn>", self._update_chat_input_hint, add="+")
        self.chat_input.bind("<FocusOut>", self._update_chat_input_hint, add="+")
        self.chat_input.bind("<Escape>", self._cancel_shortcut)
        self.chat_input.bind("<KeyRelease>", self._update_chat_input_hint, add="+")
        self.chat_input_hint = tk.Label(
            self.chat_input,
            text="Revvy에게 만들고 싶은 것을 이야기해 주세요…",
            background=COLORS["surface"],
            foreground=COLORS["subtle"],
            cursor="xterm",
            font=("Segoe UI", 10),
        )
        self.chat_input_hint.place(x=13, y=11)
        self.chat_input_hint.bind("<Button-1>", self._focus_chat_input_from_hint)
        self.send_button = ttk.Button(
            self.chat_composer,
            text="보내기  ↑",
            command=self.send_chat,
            style="Accent.TButton",
        )
        self.send_button.grid(
            row=1,
            column=1,
            rowspan=2,
            sticky="nsew",
            padx=(8, 10),
            pady=(9, 5),
        )
        self.cancel_button = ttk.Button(
            self.chat_composer,
            text="중지  ■",
            command=self.cancel_chat,
            state=tk.DISABLED,
            style="Danger.TButton",
        )
        self.cancel_button.grid(
            row=1,
            column=1,
            rowspan=2,
            sticky="nsew",
            padx=(8, 10),
            pady=(9, 5),
        )
        self.cancel_button.grid_remove()

        attachment_actions = tk.Frame(self.chat_composer, background=COLORS["composer"])
        attachment_actions.grid(
            row=3,
            column=0,
            columnspan=2,
            sticky="ew",
            padx=9,
            pady=(0, 8),
        )
        self.attach_button = ttk.Button(
            attachment_actions,
            text="＋ 이미지",
            command=self.pick_images,
            style="Toolbutton.TButton",
        )
        self.attach_button.pack(side=tk.LEFT)
        self.code_button = tk.Button(
            attachment_actions,
            text="</>",
            command=self.wrap_input_code_block,
            relief=tk.FLAT,
            borderwidth=0,
            highlightthickness=0,
            cursor="hand2",
            background=COLORS["composer"],
            foreground=COLORS["header_muted"],
            activebackground=COLORS["composer"],
            activeforeground=COLORS["accent"],
            font=("Segoe UI Semibold", 9),
            padx=8,
        )
        self.code_button.pack(side=tk.LEFT, padx=(6, 0))
        # A label that looks like a hint but answers a question when clicked
        # beats a static line nobody can act on.
        self.shortcut_hint = tk.Button(
            attachment_actions,
            text="단축키 안내  ›",
            command=self.show_shortcut_guide,
            relief=tk.FLAT,
            borderwidth=0,
            highlightthickness=0,
            cursor="hand2",
            background=COLORS["composer"],
            foreground=COLORS["header_muted"],
            activebackground=COLORS["composer"],
            activeforeground=COLORS["accent"],
            font=("Segoe UI", 8),
        )
        self.shortcut_hint.pack(side=tk.RIGHT, padx=(10, 2))
        self._register_image_drop(self.chat_stage)
        self._register_image_drop(self.chat_composer)
        self._register_image_drop(self.chat_input)

    def _build_tools_tab(self) -> None:
        self.tools_tab.grid_rowconfigure(1, weight=1)
        self.tools_tab.grid_columnconfigure(0, weight=1)
        toolbar = ttk.Frame(self.tools_tab, style="Window.TFrame")
        toolbar.grid(row=0, column=0, sticky="ew", pady=(0, 11))
        title_group = ttk.Frame(toolbar, style="Window.TFrame")
        title_group.pack(side=tk.LEFT)
        ttk.Label(
            title_group,
            text="에디터 도구",
            foreground=COLORS["text"],
            font=("Segoe UI Semibold", 16),
        ).pack(anchor=tk.W)
        ttk.Label(title_group, text="Revvy MCP에서 사용할 수 있는 도구", style="Muted.TLabel").pack(anchor=tk.W, pady=(2, 0))
        ttk.Button(toolbar, text="↻ 새로고침", command=self.load_tools).pack(side=tk.RIGHT, pady=3)
        self.tool_count_var = tk.StringVar(value="0 tools")
        ttk.Label(toolbar, textvariable=self.tool_count_var, style="Muted.TLabel").pack(side=tk.RIGHT, padx=14)

        pane = ttk.Panedwindow(self.tools_tab, orient=tk.HORIZONTAL)
        pane.grid(row=1, column=0, sticky="nsew")
        tree_frame = ttk.Frame(pane, style="Surface.TFrame", padding=(10, 10))
        detail_frame = ttk.Frame(pane, style="Surface.TFrame", padding=(12, 10))
        pane.add(tree_frame, weight=3)
        pane.add(detail_frame, weight=2)
        filter_row = ttk.Frame(tree_frame, style="Surface.TFrame")
        filter_row.pack(fill=tk.X, pady=(0, 9))
        ttk.Label(filter_row, text="검색", style="SurfaceMuted.TLabel").pack(side=tk.LEFT, padx=(0, 8))
        self.tool_filter_var = tk.StringVar()
        tool_filter = ttk.Entry(filter_row, textvariable=self.tool_filter_var)
        tool_filter.pack(side=tk.LEFT, fill=tk.X, expand=True)
        tool_filter.bind("<KeyRelease>", self._filter_tools)
        self.tool_tree = ttk.Treeview(tree_frame, columns=("name", "description"), show="headings", selectmode="browse")
        self.tool_tree.heading("name", text="도구")
        self.tool_tree.heading("description", text="설명")
        self.tool_tree.column("name", width=210, minwidth=150, stretch=False)
        self.tool_tree.column("description", width=430, minwidth=220, stretch=True)
        tool_scroll = ttk.Scrollbar(tree_frame, orient=tk.VERTICAL, command=self.tool_tree.yview)
        self.tool_tree.configure(yscrollcommand=tool_scroll.set)
        self.tool_tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        tool_scroll.pack(side=tk.RIGHT, fill=tk.Y)
        self.tool_tree.bind("<<TreeviewSelect>>", self._tool_selected)
        self.tool_tree.bind("<Double-1>", self._tool_to_direct)
        self.tool_tree.bind("<Return>", self._tool_to_direct)
        detail_head = ttk.Frame(detail_frame, style="Surface.TFrame")
        detail_head.pack(fill=tk.X, pady=(0, 8))
        ttk.Label(detail_head, text="도구 스키마", style="Surface.TLabel", font=("Segoe UI Semibold", 10)).pack(side=tk.LEFT)
        ttk.Button(detail_head, text="실행 화면으로", command=self._tool_to_direct).pack(side=tk.RIGHT)
        self.tool_details = scrolledtext.ScrolledText(
            detail_frame,
            wrap=tk.WORD,
            state=tk.DISABLED,
            background=COLORS["code"],
            foreground=COLORS["code_accent"],
            selectbackground=COLORS["code_select"],
            selectforeground=COLORS["code_text"],
            relief=tk.FLAT,
            borderwidth=0,
            highlightthickness=0,
            padx=12,
            pady=10,
            font=("Consolas", 9),
        )
        self.tool_details.pack(fill=tk.BOTH, expand=True)
        self._theme_scrolledtext_bar(self.tool_details)

    def _build_direct_tab(self) -> None:
        self.direct_tab.grid_rowconfigure(1, weight=1)
        self.direct_tab.grid_columnconfigure(0, weight=1)
        header = ttk.Frame(self.direct_tab, style="Window.TFrame")
        header.grid(row=0, column=0, sticky="ew", pady=(0, 11))
        title_group = ttk.Frame(header, style="Window.TFrame")
        title_group.pack(side=tk.LEFT)
        ttk.Label(
            title_group,
            text="직접 실행",
            foreground=COLORS["text"],
            font=("Segoe UI Semibold", 16),
        ).pack(anchor=tk.W)
        ttk.Label(title_group, text="정확한 도구와 JSON 인자로 한 번 실행", style="Muted.TLabel").pack(anchor=tk.W, pady=(2, 0))
        runner = ttk.Frame(header, style="Window.TFrame")
        runner.pack(side=tk.RIGHT, fill=tk.X, expand=True, padx=(32, 0), pady=4)
        self.direct_tool_var = tk.StringVar()
        self.direct_tool_combo = ttk.Combobox(runner, textvariable=self.direct_tool_var, state="readonly")
        self.direct_tool_combo.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(0, 8))
        self.direct_run_button = ttk.Button(runner, text="실행  ▶", command=self.run_direct_call, style="Accent.TButton")
        self.direct_run_button.pack(side=tk.LEFT)

        body = ttk.Panedwindow(self.direct_tab, orient=tk.HORIZONTAL)
        body.grid(row=1, column=0, sticky="nsew")
        arguments_frame = ttk.Frame(body, style="Surface.TFrame", padding=(12, 10))
        output_frame = ttk.Frame(body, style="Surface.TFrame", padding=(12, 10))
        body.add(arguments_frame, weight=1)
        body.add(output_frame, weight=1)
        ttk.Label(arguments_frame, text="인자  JSON", style="Surface.TLabel", font=("Segoe UI Semibold", 10)).pack(anchor=tk.W)
        self.direct_args = scrolledtext.ScrolledText(
            arguments_frame,
            wrap=tk.NONE,
            background=COLORS["code"],
            foreground=COLORS["code_accent"],
            insertbackground=COLORS["code_text"],
            selectbackground=COLORS["code_select"],
            selectforeground=COLORS["code_text"],
            relief=tk.FLAT,
            borderwidth=0,
            highlightthickness=0,
            padx=12,
            pady=10,
            undo=True,
            font=("Consolas", 10),
        )
        self.direct_args.pack(fill=tk.BOTH, expand=True, pady=(9, 0))
        self._theme_scrolledtext_bar(self.direct_args)
        self.direct_args.insert("1.0", "{}")
        ttk.Label(output_frame, text="결과", style="Surface.TLabel", font=("Segoe UI Semibold", 10)).pack(anchor=tk.W)
        self.direct_output = scrolledtext.ScrolledText(
            output_frame,
            wrap=tk.WORD,
            state=tk.DISABLED,
            background=COLORS["code"],
            foreground=COLORS["code_text"],
            selectbackground=COLORS["code_select"],
            selectforeground=COLORS["code_text"],
            relief=tk.FLAT,
            borderwidth=0,
            highlightthickness=0,
            padx=12,
            pady=10,
            font=("Consolas", 9),
        )
        self.direct_output.pack(fill=tk.BOTH, expand=True, pady=(9, 0))
        self._theme_scrolledtext_bar(self.direct_output)

    def _build_logs_tab(self) -> None:
        self.logs_tab.grid_rowconfigure(1, weight=1)
        self.logs_tab.grid_columnconfigure(0, weight=1)
        toolbar = ttk.Frame(self.logs_tab, style="Window.TFrame")
        toolbar.grid(row=0, column=0, sticky="ew", pady=(0, 11))
        title_group = ttk.Frame(toolbar, style="Window.TFrame")
        title_group.pack(side=tk.LEFT)
        ttk.Label(
            title_group,
            text="활동 로그",
            foreground=COLORS["text"],
            font=("Segoe UI Semibold", 16),
        ).pack(anchor=tk.W)
        ttk.Label(title_group, text="프록시와 로컬 AI 실행 기록", style="Muted.TLabel").pack(anchor=tk.W, pady=(2, 0))
        ttk.Button(
            toolbar,
            text="비우기",
            command=lambda: self._apply_log_snapshot(""),
        ).pack(side=tk.RIGHT, pady=3)
        ttk.Button(toolbar, text="↻ 새로고침", command=self.refresh_logs).pack(side=tk.RIGHT, padx=7, pady=3)
        self.log_output = scrolledtext.ScrolledText(
            self.logs_tab,
            wrap=tk.NONE,
            state=tk.DISABLED,
            background=COLORS["code"],
            foreground=COLORS["code_accent"],
            insertbackground=COLORS["code_text"],
            selectbackground=COLORS["code_select"],
            selectforeground=COLORS["code_text"],
            relief=tk.FLAT,
            borderwidth=0,
            highlightbackground=COLORS["border"],
            highlightthickness=1,
            padx=13,
            pady=12,
            font=("Consolas", 9),
        )
        self.log_output.grid(row=1, column=0, sticky="nsew")
        self._theme_scrolledtext_bar(self.log_output)

    def _set_icon(self) -> None:
        for candidate in self._resource_candidates("Icon128.png"):
            if not candidate.is_file():
                continue
            try:
                self._icon_image = tk.PhotoImage(file=str(candidate))
                self.root.iconphoto(True, self._icon_image)
                return
            except tk.TclError:
                continue

    def _select_tab(self, index: int) -> None:
        if hasattr(self, "notebook"):
            self.notebook.select(index)
        for item_index, (indicator, button) in enumerate(getattr(self, "nav_items", [])):
            selected = item_index == index
            background = COLORS["rail_selected"] if selected else COLORS["rail"]
            indicator.configure(background=COLORS["accent"] if selected else COLORS["rail"])
            button.configure(
                background=background,
                foreground=COLORS["accent"] if selected else COLORS["header_muted"],
                activebackground=COLORS["rail_hover"],
            )

    def _toggle_advanced(self) -> None:
        self.advanced_visible = not self.advanced_visible
        if self.advanced_visible:
            self.advanced_frame.pack(fill=tk.X, pady=(8, 0))
            self.advanced_toggle_button.configure(text="▾  고급 설정 닫기")
        else:
            self.advanced_frame.pack_forget()
            self.advanced_toggle_button.configure(text="▸  고급 설정 열기")

    @staticmethod
    def _proxy_config_engine(
        project_dir: str | os.PathLike[str] | None,
        config_path: str | os.PathLike[str] | None = None,
    ) -> str | None:
        """Return proxy engine only when its project matches the selected one."""
        try:
            target = (
                pathlib.Path(config_path).expanduser().resolve()
                if config_path is not None
                else pathlib.Path(project_dir or "").expanduser().resolve()
                / ".revvy"
                / "revvy-proxy.json"
            )
            config = json.loads(target.read_text(encoding="utf-8-sig"))
            if not isinstance(config, dict):
                return None
            value = str(config.get("engine", "")).strip().lower()
        except (json.JSONDecodeError, OSError, ValueError, TypeError):
            return None
        if value not in core.ENGINES:
            return None
        for key in ("active_project", "installed_project"):
            project = revvy._project_from_config(config, key)
            if isinstance(project, dict) and core.same_project_path(
                project.get("path"), project_dir
            ):
                return value
        return None

    def _themed_colors(self) -> dict[str, str]:
        """Theme palette with the active engine's accent overlay applied."""
        palette = dict(THEMES.get(self.settings.theme, THEMES["light"]))
        accent = _engine_brand(self.engine)["accent"]
        if accent:
            palette["accent"] = accent
            palette["accent_active"] = _shade_color(accent, 0.18)
            palette["accent_text"] = _shade_color(
                accent, -0.32 if self.settings.theme != "dark" else 0.42
            )
            palette["blue"] = accent
            palette["border_focus"] = accent
        return palette

    def _adopt_engine(self, engine: str) -> None:
        """Switch the active engine and rebuild the UI with its accent overlay."""
        if self.closed or engine not in core.ENGINES or engine == self.engine:
            return
        if self.action_running or self.active_process is not None or self._theme_fade_running:
            self.root.after(2000, self._adopt_engine, engine)
            return
        self.engine = engine
        self._apply_theme()
        self._set_footer(f"엔진 · {_engine_brand(engine)['label']}")

    def _engine_changed(self, _event: tk.Event | None = None) -> None:
        selected = ENGINE_SELECT_VALUES.get(self.engine_var.get())
        if selected == self.settings.engine_override:
            return
        if self.action_running or self.active_process is not None or self._theme_fade_running:
            self._set_footer("진행 중인 작업이 끝나면 엔진을 바꿀 수 있어요")
            self.engine_var.set(ENGINE_LABELS[self.settings.engine_override])
            return
        self.settings.engine_override = selected
        self._save_settings()
        engine = core.resolve_engine(
            self.settings.project_dir,
            selected,
            self._proxy_config_engine(
                self.settings.project_dir,
                self.workspace.config_path() if self.workspace is not None else None,
            ),
        )
        if not core.write_proxy_project_context(
            self.settings.project_dir,
            engine,
            path=self.workspace.config_path() if self.workspace is not None else None,
        ):
            self._append_log("Could not record project context in revvy-proxy.json")
        self._adopt_engine(engine)
        self._set_footer(f"엔진 · {ENGINE_LABELS[selected]}")

    def _toggle_theme(self) -> None:
        if self.action_running or self.active_process is not None:
            self._set_footer("진행 중인 작업이 끝나면 테마를 바꿀 수 있어요")
            return
        if getattr(self, "_theme_fade_running", False):
            return
        self.settings.theme = "dark" if self.settings.theme != "dark" else "light"
        self._save_settings()
        self._animate_theme_swap()

    def _animate_theme_swap(self) -> None:
        """Fade the window out, rebuild while invisible, fade back in.

        The rebuild destroys and recreates every widget, which is visibly
        janky when it happens on screen; hiding it behind a short alpha fade
        makes the switch read as one smooth transition. If the platform does
        not support the alpha attribute the swap simply happens instantly.
        """
        self._theme_fade_running = True
        steps = THEME_FADE_STEPS

        def finish() -> None:
            self._theme_fade_running = False
            self._set_footer(
                "다크 모드로 바꿨어요 🌙"
                if self.settings.theme == "dark"
                else "라이트 모드로 바꿨어요 ☀"
            )

        def fade_in(step: int) -> None:
            if self.closed:
                self._theme_fade_running = False
                return
            try:
                self.root.attributes("-alpha", min(1.0, step / steps))
            except tk.TclError:
                step = steps
            if step < steps:
                self.root.after(THEME_FADE_INTERVAL_MS, fade_in, step + 1)
                return
            finish()

        def fade_out(step: int) -> None:
            if self.closed:
                self._theme_fade_running = False
                return
            try:
                self.root.attributes("-alpha", max(0.0, step / steps))
            except tk.TclError:
                step = 0
            if step > 0:
                self.root.after(THEME_FADE_INTERVAL_MS, fade_out, step - 1)
                return
            try:
                self._apply_theme()
                self.root.update_idletasks()
            except Exception as exc:
                try:
                    self.root.attributes("-alpha", 1.0)
                except tk.TclError:
                    pass
                self._theme_fade_running = False
                self._set_footer(f"테마 전환에 실패했어요: {exc}", error=True)
                return
            self.root.after(THEME_FADE_INTERVAL_MS, fade_in, 1)

        fade_out(steps)

    def _apply_theme(self) -> None:
        """Swap the palette and rebuild the UI in place.

        Widget attributes are recreated by _build_ui, the chat re-renders from
        self.history, and pending image attachments are re-rendered into the
        new composer. The status poll loop repopulates the new labels on its
        next tick.
        """
        COLORS.clear()
        COLORS.update(self._themed_colors())
        self._capture_visible_session_controls()
        selected = 0
        if hasattr(self, "notebook"):
            try:
                selected = self.notebook.index(self.notebook.select())
            except tk.TclError:
                selected = 0
        self.active_attachment_paths = []
        self._attachment_photos = []
        for child in self.root.winfo_children():
            child.destroy()
        self.root.configure(background=COLORS["window"])
        self._configure_styles()
        self._build_ui()
        self._restore_chat_history()
        self._repaint_session_controls()
        self._select_tab(selected)
        # The status poll loop keeps itself alive across the rebuild, but the
        # login check and tool list only run once at startup — retrigger them
        # so the fresh header/tabs do not stay stuck on their placeholders.
        if not self.smoke_test:
            self.refresh_login()
            if not self.tools:
                self.root.after(150, self.load_tools)

    def _show_empty_chat(self) -> None:
        if hasattr(self, "empty_state"):
            self.empty_state.place(relx=0, rely=0, relwidth=1, relheight=1)
            self.empty_state.lift()

    def _hide_empty_chat(self) -> None:
        if hasattr(self, "empty_state"):
            self.empty_state.place_forget()

    def _use_quick_prompt(self, prompt: str) -> None:
        self.chat_input.delete("1.0", tk.END)
        self.chat_input.insert("1.0", prompt)
        self.chat_input.focus_set()
        self._update_chat_input_hint()

    def _focus_chat_input_from_hint(self, _event: tk.Event | None = None) -> str:
        self.chat_input.focus_set()
        self._update_chat_input_hint()
        return "break"

    def _update_chat_input_hint(self, _event: tk.Event | None = None) -> None:
        if not hasattr(self, "chat_input_hint") or not hasattr(self, "chat_input"):
            return
        try:
            draft = self.chat_input.get("1.0", "end-1c")
            self.chat_draft = draft
            is_empty = not draft.strip()
            has_focus = self.root.focus_get() is self.chat_input
            if is_empty and not has_focus:
                self.chat_input_hint.place(x=13, y=11)
                self.chat_input_hint.lift()
            else:
                self.chat_input_hint.place_forget()
        except tk.TclError:
            return

    def _register_image_drop(self, widget: tk.Widget) -> None:
        if DND_FILES is None or not hasattr(widget, "drop_target_register"):
            return
        widget.drop_target_register(DND_FILES)  # type: ignore[attr-defined]
        widget.dnd_bind("<<DropEnter>>", self._image_drag_enter)  # type: ignore[attr-defined]
        widget.dnd_bind("<<DropLeave>>", self._image_drag_leave)  # type: ignore[attr-defined]
        widget.dnd_bind("<<Drop>>", self._image_drop)  # type: ignore[attr-defined]

    def _image_drag_enter(self, event: Any) -> str | None:
        self.chat_composer.configure(
            highlightbackground=COLORS["accent"],
            highlightcolor=COLORS["accent"],
        )
        return getattr(event, "action", None)

    def _image_drag_leave(self, event: Any) -> str | None:
        self.chat_composer.configure(
            highlightbackground=COLORS["border"],
            highlightcolor=COLORS["border_focus"],
        )
        return getattr(event, "action", None)

    def _image_drop(self, event: Any) -> str | None:
        self._image_drag_leave(event)
        try:
            paths = self.root.tk.splitlist(str(event.data))
        except (AttributeError, tk.TclError):
            self._set_footer("드롭한 파일 경로를 읽을 수 없습니다", error=True)
            return getattr(event, "action", None)
        self._add_image_paths(paths)
        return getattr(event, "action", None)

    def pick_images(self) -> None:
        if self.action_running:
            return
        selected = filedialog.askopenfilenames(
            initialdir=self.project_var.get() or str(pathlib.Path.home()),
            filetypes=(
                ("Images", "*.png *.jpg *.jpeg *.gif *.webp"),
                ("All files", "*.*"),
            ),
        )
        if selected:
            self._add_image_paths(selected)

    @staticmethod
    def _validate_image_path(value: str | os.PathLike[str]) -> pathlib.Path:
        path = pathlib.Path(value).expanduser()
        if not path.is_file():
            raise ValueError(f"이미지 파일을 찾을 수 없습니다: {path}")
        path = path.resolve()
        if path.suffix.casefold() not in IMAGE_SUFFIXES:
            raise ValueError(f"지원하지 않는 이미지 형식입니다: {path.name}")
        if path.stat().st_size > MAX_IMAGE_BYTES:
            raise ValueError(f"이미지는 20 MB 이하여야 합니다: {path.name}")
        try:
            with Image.open(path) as image:
                if image.width * image.height > 100_000_000:
                    raise ValueError(f"이미지 해상도가 너무 큽니다: {path.name}")
                image.verify()
        except (OSError, UnidentifiedImageError, Image.DecompressionBombError) as exc:
            raise ValueError(f"올바른 이미지가 아닙니다: {path.name}") from exc
        return path

    def _add_image_paths(self, values: Any) -> None:
        if self.action_running:
            return
        existing = {
            os.path.normcase(os.path.normpath(str(path)))
            for path in self.attachments
        }
        added = 0
        errors: list[str] = []
        for value in values:
            try:
                path = self._validate_image_path(str(value))
            except ValueError as exc:
                errors.append(str(exc))
                continue
            key = os.path.normcase(os.path.normpath(str(path)))
            if key in existing:
                continue
            if len(self.attachments) >= MAX_IMAGE_ATTACHMENTS:
                errors.append(f"이미지는 최대 {MAX_IMAGE_ATTACHMENTS}장까지 첨부할 수 있습니다")
                break
            self.attachments.append(path)
            existing.add(key)
            added += 1
        self._render_attachments()
        if errors:
            self._set_footer(errors[0], error=True)
        elif added:
            self._set_footer(f"이미지 {len(self.attachments)}개 첨부됨")

    @staticmethod
    def _short_file_name(name: str, limit: int = 18) -> str:
        if len(name) <= limit:
            return name
        suffix = pathlib.Path(name).suffix
        stem_limit = max(4, limit - len(suffix) - 1)
        return f"{pathlib.Path(name).stem[:stem_limit]}…{suffix}"

    @staticmethod
    def _file_size_label(size: int) -> str:
        if size >= 1024 * 1024:
            return f"{size / (1024 * 1024):.1f} MB"
        return f"{max(1, round(size / 1024))} KB"

    def _attachment_thumbnail(self, path: pathlib.Path) -> ImageTk.PhotoImage:
        with Image.open(path) as source:
            source.seek(0)
            image = ImageOps.exif_transpose(source).convert("RGBA")
            image.thumbnail((40, 40), Image.Resampling.LANCZOS)
        canvas = Image.new("RGBA", (42, 42), COLORS["surface_alt"])
        canvas.alpha_composite(image, ((42 - image.width) // 2, (42 - image.height) // 2))
        return ImageTk.PhotoImage(canvas, master=self.root)

    def _render_attachments(self) -> None:
        renderable_attachments: list[tuple[pathlib.Path, int]] = []
        for path in self.attachments:
            try:
                renderable_attachments.append((path, path.stat().st_size))
            except OSError:
                continue
        self.attachments = [path for path, _size in renderable_attachments]
        # State updates are also used by owner-bound worker callbacks and during
        # early/theme-rebuild lifecycle points where the composer has not been
        # constructed yet. Keep the owning session accurate without assuming
        # its shared widgets exist (or belong to the project currently on screen).
        if (
            not self._is_visible()
            or not hasattr(self, "attachment_chips")
            or not hasattr(self, "attachment_strip")
        ):
            return
        for child in self.attachment_chips.winfo_children():
            child.destroy()
        self._attachment_photos.clear()
        if not renderable_attachments:
            self.attachment_strip.grid_remove()
            return
        for path, file_size in renderable_attachments:
            chip = tk.Frame(
                self.attachment_chips,
                width=142,
                height=52,
                background=COLORS["raised"],
                highlightbackground=COLORS["border"],
                highlightthickness=1,
            )
            chip.pack(side=tk.LEFT, padx=(0, 6))
            chip.pack_propagate(False)
            try:
                photo = self._attachment_thumbnail(path)
            except (OSError, UnidentifiedImageError, Image.DecompressionBombError):
                photo = None
            if photo is not None:
                self._attachment_photos.append(photo)
                tk.Label(
                    chip,
                    image=photo,
                    background=COLORS["raised"],
                    borderwidth=0,
                ).pack(side=tk.LEFT, padx=(5, 6), pady=4)
            text_group = tk.Frame(chip, background=COLORS["raised"])
            text_group.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, pady=6)
            tk.Label(
                text_group,
                text=self._short_file_name(path.name),
                background=COLORS["raised"],
                foreground=COLORS["text"],
                anchor=tk.W,
                font=("Segoe UI Semibold", 8),
            ).pack(anchor=tk.W)
            tk.Label(
                text_group,
                text=self._file_size_label(file_size),
                background=COLORS["raised"],
                foreground=COLORS["muted"],
                anchor=tk.W,
                font=("Segoe UI", 8),
            ).pack(anchor=tk.W, pady=(2, 0))
            tk.Button(
                chip,
                text="×",
                command=lambda image_path=path: self._remove_attachment(image_path),
                background=COLORS["raised"],
                foreground=COLORS["muted"],
                activebackground=COLORS["border"],
                activeforeground=COLORS["text"],
                relief=tk.FLAT,
                borderwidth=0,
                highlightthickness=0,
                cursor="hand2",
                font=("Segoe UI", 10),
            ).pack(side=tk.RIGHT, anchor=tk.N, padx=(2, 4), pady=2)
        self.attachment_strip.grid()

    def _remove_attachment(self, path: pathlib.Path) -> None:
        self.attachments = [item for item in self.attachments if item != path]
        self._render_attachments()

    def _clear_pending_attachments(self) -> None:
        self.attachments.clear()
        self._render_attachments()

    def _remember_turn_notice(self, text: str, *, notice: bool) -> None:
        """Hold the last turn's cancel or failure so a theme switch keeps it.

        Notices are widget-only - only user and assistant messages reach
        self.history, and _clean_chat_messages would drop a third kind on save
        while build_chat_prompt would leak it into the model prompt. Keeping one
        in memory is enough to stop a theme toggle from erasing the reason the
        last question has no answer.
        """
        self._last_turn_notice = (text, notice)

    def _restore_chat_history(self) -> None:
        if not hasattr(self, "chat_output"):
            return
        self._replace_text(self.chat_output, "")
        streaming = getattr(self._session, "pending_reply", "")
        pending = getattr(self, "_last_turn_notice", None)
        if not self.history and not streaming and not pending:
            # A project with a turn already in flight has no finished messages
            # yet but is not empty; showing the welcome screen would hide that
            # it is working.
            self._show_empty_chat()
            return
        # Scrolling to the tail once at the end, not once per message. Replaying
        # a full transcript is what a theme switch does, and the per-message
        # see() was ~95% of its cost - spent with the window faded to alpha 0.
        self._suppress_autoscroll = True
        try:
            for role, content in self.history:
                self._append_chat("You" if role == "user" else "Revvy", content)
            # A reply that was still streaming while this project sat in the
            # background. Without it, refocusing a working project shows the
            # question and nothing under it until the turn ends.
            if streaming:
                self._append_chat("Revvy", streaming)
            if pending:
                text, is_notice = pending
                self._append_chat(
                    "Revvy", text, notice=is_notice, error=not is_notice, label=False
                )
        finally:
            self._suppress_autoscroll = False
        self.chat_output.see(tk.END)

    def _persist_chat_history(self) -> None:
        if self.smoke_test:
            return
        try:
            if self.settings.remember_history:
                # Trim in memory too. The cap was only ever applied on the way to
                # disk, so a long session grew unbounded in RAM and made every
                # theme rebuild replay the whole thing.
                self.history = core._clean_chat_messages(self.history)
                core.save_chat_history(
                    self._history_project_dir,
                    self.history,
                    self.workspace.chat_history_path()
                    if self.workspace is not None
                    else None,
                )
            else:
                core.clear_chat_history(
                    self._history_project_dir,
                    self.workspace.chat_history_path()
                    if self.workspace is not None
                    else None,
                )
        except OSError as exc:
            self._append_log(f"Could not save chat history: {exc}")

    @staticmethod
    def _same_project(left: str, right: str) -> bool:
        return os.path.normcase(os.path.abspath(left)) == os.path.normcase(os.path.abspath(right))

    def _open_session(self, project_dir: str) -> tuple[ProjectSession, bool]:
        """Return the session for a project, opening one if it is not open yet.

        Returns (session, created). A project that is already open keeps the
        session it has - including a turn still running in it - rather than
        being reloaded from disk on top of itself.
        """
        target = str(pathlib.Path(project_dir).expanduser().resolve())
        key = os.path.normcase(target)
        existing = self.sessions.get(key)
        if existing is not None:
            return existing, False
        workspace = core.Workspace.open(target)
        settings = core.load_project_settings(
            target,
            env=workspace.env,
            migrate_legacy=not self.smoke_test,
        )
        # Window presentation is a property of the shell, not of the project.
        settings.theme = self.settings.theme
        settings.geometry = self.settings.geometry
        settings.project_dir = target
        session = ProjectSession(workspace, settings)
        session.engine = core.resolve_engine(
            target,
            settings.engine_override,
            self._proxy_config_engine(target, session.workspace.config_path()),
        )
        if not self.smoke_test:
            try:
                core.repair_blender_upstream_command(
                    target,
                    env=session.workspace.env,
                )
            except OSError:
                pass
        models = self.models_by_provider[session.provider_key]
        if not settings.model or settings.model not in models:
            settings.model = self.provider_default_models[session.provider_key]
        session.reasoning_by_provider = {
            provider: core.initial_reasoning_effort(settings, provider)
            for provider in (
                core.PROVIDER_CODEX,
                core.PROVIDER_CLAUDE,
                core.PROVIDER_KIMI,
            )
        }
        session.reasoning_effort = session.reasoning_by_provider[session.provider_key]
        session._history_project_dir = target
        session.history = (
            core.load_chat_history(target, workspace.chat_history_path())
            if settings.remember_history and not self.smoke_test
            else []
        )
        self.sessions[key] = session
        self._provision_project(session)
        return session, True

    # What each engine calls the thing being installed, and what the user has to
    # do afterwards. Both belong in the confirmation: an install the user cannot
    # act on is an install they will think failed.
    BRIDGE_LABELS = {
        core.ENGINE_UNREAL: ("플러그인", "에디터를 다시 시작하면 적용돼요"),
        core.ENGINE_GODOT: (
            "애드온",
            "프로젝트 설정 ▸ 플러그인에서 Revvy Editor Bridge를 켜주세요",
        ),
        core.ENGINE_UNITY: ("패키지", "에디터가 도메인을 다시 로드하면 붙어요"),
    }

    def _provision_project(self, session: ProjectSession) -> None:
        """Offer to install the editor bridge into a project that lacks one.

        The app used to be deployed inside <Project>/Plugins/Revvy, so a project
        it could see was a project it was already part of. Installed on its own
        that stops being true, and a project without the bridge has no MCP server
        for Revvy to reach.

        Asks first. Writing into someone's project directory uninvited is not
        something an app gets to do quietly, so the prompt names the exact path
        and both versions before anything is copied.
        """
        if self.smoke_test:
            return
        engine = session.engine
        if engine not in self.BRIDGE_LABELS:
            return
        try:
            plan = core.describe_bridge_install(session.settings.project_dir, engine)
        except OSError:
            return
        if plan["action"] in ("unavailable", "current"):
            return
        noun, follow_up = self.BRIDGE_LABELS[engine]
        if plan["action"] == "newer":
            # The usual cause is that this project is where the bridge is being
            # developed. Say so and leave it alone.
            self._append_log(
                f"BRIDGE  {session.name}: 프로젝트 쪽 {noun}이(가) 더 최신 "
                f"({plan['existing']} > {plan['shipped']})"
            )
            return
        engine_label = _engine_brand(engine)["label"]
        if plan["action"] == "update":
            question = (
                f"{session.name} 의 Revvy {noun}을(를) "
                f"{plan['existing']} 에서 {plan['shipped']} 로 갱신할까요?"
            )
        else:
            question = (
                f"{session.name} 은(는) {engine_label} 프로젝트예요.\n"
                f"Revvy {noun} {plan['shipped']} 을(를) 설치할까요?"
            )
        if not messagebox.askyesno(
            f"Revvy {noun} 설치",
            f"{question}\n\n설치 위치\n{plan['target']}\n\n{follow_up}",
            parent=self.root,
        ):
            self._set_footer(f"{noun} 설치를 건너뛰었어요")
            return
        try:
            status, detail = core.provision_engine_bridge(
                session.settings.project_dir, engine
            )
        except OSError as exc:
            self._set_footer(f"{noun} 설치에 실패했어요: {exc}", error=True)
            return
        if status in ("installed", "updated"):
            self._append_log(f"BRIDGE  {detail}")
            self._set_footer(f"{detail} · {follow_up}")
        else:
            self._append_log(f"BRIDGE  {detail}")

    @property
    def sessions(self) -> dict[str, ProjectSession]:
        """Open projects, keyed by normalised path.

        Lazily created for the same reason as the focused session: tests build
        app objects that never ran __init__, and they still have to be able to
        reach the registry.
        """
        return self.__dict__.setdefault("sessions", {})

    def _build_watch_strip(self) -> None:
        """A line per project that is working somewhere other than on screen.

        Orca shows parallel sessions by splitting the window into panes. A second
        full transcript here would need a second set of chat widgets and a second
        composer; what is actually worth seeing while working elsewhere is
        narrower - that the other project is alive and what step it is on. So
        this is one row each, and clicking a row goes there.
        """
        self.watch_strip = tk.Frame(
            self.chat_tab,
            background=COLORS["surface_alt"],
            highlightbackground=COLORS["border"],
            highlightthickness=1,
            borderwidth=0,
        )
        self.watch_strip.grid(row=3, column=0, sticky="ew", pady=(8, 0))
        self.watch_strip.grid_columnconfigure(0, weight=1)
        self.watch_rows = tk.Frame(self.watch_strip, background=COLORS["surface_alt"])
        self.watch_rows.grid(row=0, column=0, sticky="ew", padx=10, pady=7)
        self.watch_rows.grid_columnconfigure(1, weight=1)
        self.watch_strip.grid_remove()

    def _refresh_watch_strip(self) -> None:
        """Show the background projects that are busy, hide the strip otherwise."""
        if not hasattr(self, "watch_strip"):
            return
        visible = self.__dict__.get("_visible", self._session)
        working = [
            session
            for session in sorted(
                self.sessions.values(), key=lambda item: item.name.lower()
            )
            if session is not visible and session.busy
        ]
        for child in self.watch_rows.winfo_children():
            child.destroy()
        if not working:
            if self.watch_strip.grid_info():
                self.watch_strip.grid_remove()
            return
        for row, session in enumerate(working):
            marker = tk.Label(
                self.watch_rows,
                text="✦",
                background=COLORS["surface_alt"],
                foreground=COLORS["accent"],
                font=("Segoe UI", 9),
                cursor="hand2",
            )
            marker.grid(row=row, column=0, padx=(0, 7))
            step = ""
            for entry in reversed(session.activity_entries):
                if entry["state"] == "running" and entry["key"] != "turn":
                    step = _compact_activity_title(entry["title"], 46)
                    break
            elapsed = ""
            started = float(session.chat_turn_started_at or 0.0)
            if started:
                elapsed = f" · {_format_duration(time.monotonic() - started)}"
            label = tk.Label(
                self.watch_rows,
                text=f"{session.name} 에서 작업 중{elapsed}"
                + (f"  ·  {step}" if step else ""),
                background=COLORS["surface_alt"],
                foreground=COLORS["muted"],
                anchor=tk.W,
                font=("Segoe UI", 8),
                cursor="hand2",
            )
            label.grid(row=row, column=1, sticky="ew")
            go = tk.Label(
                self.watch_rows,
                text="보기 ›",
                background=COLORS["surface_alt"],
                foreground=COLORS["accent_text"],
                font=("Segoe UI Semibold", 8),
                cursor="hand2",
            )
            go.grid(row=row, column=2, padx=(8, 0))
            for widget in (marker, label, go):
                widget.bind(
                    "<Button-1>",
                    lambda _event, key=session.key: self.focus_project(key),
                )
        if not self.watch_strip.grid_info():
            self.watch_strip.grid()

    def _record_edit(self, preview: Any) -> None:
        """Remember a file the agent just edited, and say so on the card.

        Recorded per session, so a turn running in a background project
        accumulates its own list rather than the focused project's.
        """
        self.turn_edits.append(preview)
        if self._is_visible():
            self._update_edit_summary()

    def _update_edit_summary(self) -> None:
        if not hasattr(self, "edit_summary_button"):
            return
        edits = self._session.turn_edits
        if not edits:
            self.edit_summary_button.grid_remove()
            return
        files = len({item.path for item in edits})
        added = sum(item.added for item in edits)
        removed = sum(item.removed for item in edits)
        self.edit_summary_var.set(f"수정한 파일 {files}개  +{added} -{removed}")
        self.edit_summary_button.grid()

    def show_turn_edits(self) -> None:
        """Everything the agent changed this turn, in one window."""
        edits = self._session.turn_edits
        if not edits:
            self._set_footer("이번 턴에 바뀐 파일이 없어요")
            return
        DiffWindow(self, f"{self._session.name} · 이번 턴", list(edits))

    def _build_file_panel(self, parent: tk.Misc) -> None:
        """The project's files, with git decoration and an editor.

        A Treeview rather than a hand-drawn canvas: it already does expansion,
        keyboard traversal and selection, and children are inserted only when a
        node opens, so an Unreal project's Intermediate/ is never walked.
        """
        self.file_panel = ttk.Frame(parent, style="Context.TFrame", padding=(12, 10))
        self.file_panel.grid(row=1, column=0, sticky="nsew")
        self.file_panel.grid_rowconfigure(2, weight=1)
        self.file_panel.grid_columnconfigure(0, weight=1)
        self.file_panel.grid_remove()

        header = ttk.Frame(self.file_panel, style="Context.TFrame")
        header.grid(row=0, column=0, sticky="ew")
        header.grid_columnconfigure(0, weight=1)
        self.file_root_var = tk.StringVar(value="")
        ttk.Label(
            header,
            textvariable=self.file_root_var,
            style="Context.TLabel",
            font=("Segoe UI Semibold", 9),
        ).grid(row=0, column=0, sticky="w")
        ttk.Button(
            header, text="↻", width=3, command=self.refresh_files, style="Chip.TButton"
        ).grid(row=0, column=1, sticky="e")

        self.file_changed_var = tk.StringVar(value="")
        ttk.Label(
            self.file_panel,
            textvariable=self.file_changed_var,
            style="ContextMuted.TLabel",
            wraplength=RIGHT_PANEL_WIDTH - 40,
        ).grid(row=1, column=0, sticky="ew", pady=(4, 6))

        tree_shell = ttk.Frame(self.file_panel, style="Context.TFrame")
        tree_shell.grid(row=2, column=0, sticky="nsew")
        tree_shell.grid_rowconfigure(0, weight=1)
        tree_shell.grid_columnconfigure(0, weight=1)
        self.file_tree_view = ttk.Treeview(
            tree_shell, columns=("status",), show="tree headings", selectmode="browse"
        )
        self.file_tree_view.heading("#0", text="이름", anchor=tk.W)
        self.file_tree_view.heading("status", text="", anchor=tk.E)
        self.file_tree_view.column("#0", stretch=True, minwidth=120)
        self.file_tree_view.column("status", width=26, stretch=False, anchor=tk.E)
        self.file_tree_view.grid(row=0, column=0, sticky="nsew")
        file_scroll = ttk.Scrollbar(
            tree_shell, orient=tk.VERTICAL, command=self.file_tree_view.yview
        )
        self.file_tree_view.configure(yscrollcommand=file_scroll.set)
        file_scroll.grid(row=0, column=1, sticky="ns")
        self.file_tree_view.bind("<<TreeviewOpen>>", self._expand_file_node)
        self.file_tree_view.bind("<Double-1>", self._open_selected_file)
        self.file_tree_view.bind("<Return>", self._open_selected_file)
        # One tag per status letter; colours are re-applied on theme change.
        for state, letter in core_files.STATUS_LETTERS.items():
            self.file_tree_view.tag_configure(f"status_{letter}", foreground=COLORS["accent_text"])
        self.file_tree_view.tag_configure("noisy", foreground=COLORS["subtle"])

        actions = ttk.Frame(self.file_panel, style="Context.TFrame")
        actions.grid(row=3, column=0, sticky="ew", pady=(8, 0))
        actions.grid_columnconfigure(0, weight=1)
        actions.grid_columnconfigure(1, weight=1)
        ttk.Button(
            actions, text="열기", command=self._open_selected_file, style="Chip.TButton"
        ).grid(row=0, column=0, sticky="ew", padx=(0, 4))
        ttk.Button(
            actions,
            text="변경 내역",
            command=self._show_selected_diff,
            style="Chip.TButton",
        ).grid(row=0, column=1, sticky="ew")

        self._file_nodes: dict[str, core_files.FileNode] = {}
        self._file_trees: dict[str, core_files.FileTree] = {}

    def _select_right_tab(self) -> None:
        wanted = self.right_tab_var.get()
        if wanted == "files":
            self._context_shell.grid_remove()
            self.file_panel.grid()
            self.refresh_files()
        else:
            self.file_panel.grid_remove()
            self._context_shell.grid()

    def _file_tree(self) -> core_files.FileTree:
        """The tree for the focused project, built once and reused."""
        key = self._session.key
        tree = self._file_trees.get(key)
        root = self.settings.project_dir
        if tree is None or str(tree.root) != str(pathlib.Path(root).resolve()):
            tree = core_files.FileTree(root)
            self._file_trees[key] = tree
        return tree

    def refresh_files(self) -> None:
        """Re-read the root listing and git status for the focused project."""
        if not hasattr(self, "file_tree_view") or self.right_tab_var.get() != "files":
            return
        tree = self._file_tree()
        self.file_root_var.set(self._session.name or str(tree.root))
        self.file_tree_view.delete(*self.file_tree_view.get_children())
        self._file_nodes.clear()
        tracked = tree.refresh_status()
        if not tracked:
            self.file_changed_var.set("Git 저장소가 아니라 변경 표시는 없어요")
        else:
            changed = tree.changed_paths()
            suffix = " (일부만 표시)" if tree.status_truncated else ""
            self.file_changed_var.set(
                f"변경된 파일 {len(changed)}개{suffix}" if changed else "변경된 파일 없어요"
            )
        self._insert_children("", tree, None)

    def _insert_children(
        self, parent: str, tree: core_files.FileTree, directory: pathlib.Path | None
    ) -> None:
        for node in tree.children(directory):
            letter = tree.status_of(node)
            tags: list[str] = []
            if letter:
                tags.append(f"status_{letter}")
            if node.noisy:
                tags.append("noisy")
            item = self.file_tree_view.insert(
                parent,
                tk.END,
                text=f"  {node.name}",
                values=(letter,),
                tags=tuple(tags),
            )
            self._file_nodes[item] = node
            if node.is_dir:
                # A placeholder child is what gives the row its expander without
                # reading the directory. It is replaced on first open.
                self.file_tree_view.insert(item, tk.END, text="", tags=("placeholder",))

    def _expand_file_node(self, _event: tk.Event | None = None) -> None:
        item = self.file_tree_view.focus()
        node = self._file_nodes.get(item)
        if node is None or not node.is_dir:
            return
        children = self.file_tree_view.get_children(item)
        if len(children) != 1 or self._file_nodes.get(children[0]) is not None:
            return
        self.file_tree_view.delete(children[0])
        self._insert_children(item, self._file_tree(), node.path)

    def _selected_file_node(self) -> core_files.FileNode | None:
        return self._file_nodes.get(self.file_tree_view.focus())

    def _open_selected_file(self, _event: tk.Event | None = None) -> str:
        node = self._selected_file_node()
        if node is None or node.is_dir:
            return "break"
        try:
            document = core_files.read_document(node.path)
        except OSError as exc:
            self._set_footer(str(exc), error=True)
            return "break"
        if document.binary:
            self._set_footer(f"{node.name} 은(는) 바이너리라 열 수 없어요")
            return "break"
        FileEditorWindow(self, node, document)
        return "break"

    def _show_selected_diff(self, _event: tk.Event | None = None) -> str:
        node = self._selected_file_node()
        if node is None or node.is_dir:
            return "break"
        result = core_diff.diff_against_head(str(self._file_tree().root), node.relative)
        if not result.changed:
            self._set_footer(f"{node.name} 은(는) 마지막 커밋과 같아요")
            return "break"
        DiffWindow(self, f"{node.relative} · {result.summary}", [result])
        return "break"

    def _check_for_updates(self) -> None:
        """Look for a newer build, off the UI thread.

        Silent about everything except success. A machine with no feed
        configured, no network, or a typo in the URL is a machine that should
        still just be a working Revvy - an update check is not the point of the
        app and must never interrupt it.
        """
        if self.closed or updater is None:
            return
        instance = updater.RevvyUpdater()
        if not instance.configured:
            return

        def worker() -> None:
            try:
                staged = instance.pending()
                if staged is None:
                    found = instance.check()
                    if found is None:
                        return
                    instance.download(found)
                    staged = found
            except Exception:
                return
            self._post(self._announce_update, staged, session=None)

        threading.Thread(target=worker, name="Revvy-update", daemon=True).start()

    def _announce_update(self, info: Any) -> None:
        """Offer a staged update without touching the work in progress.

        Nothing is applied here. The installer runs when the user asks, and the
        app closes first - replacing the executables under a running turn is how
        an update gets blamed for losing someone's work.
        """
        if self.closed or not hasattr(self, "footer_var"):
            return
        self._pending_update = info
        if hasattr(self, "update_banner"):
            self.update_var.set(f"업데이트 준비됨 · {info.version}")
            self.update_banner.grid()
        self._set_footer(f"레비 {info.version} 업데이트가 준비됐어요")

    def apply_update(self) -> None:
        info = getattr(self, "_pending_update", None)
        if info is None or updater is None:
            return
        # Work in progress outranks a missing file: whether an update *could*
        # run is irrelevant if it must not run yet, and reporting the wrong one
        # of the two sends the user looking in the wrong place.
        busy = [session.name for session in self.sessions.values() if session.busy]
        if busy:
            self._set_footer(
                f"{', '.join(busy)}에서 작업 중이에요. 끝난 뒤 업데이트하세요", error=True
            )
            return
        installer = updater.RevvyUpdater().installer_path(info)
        if not installer.is_file():
            self._set_footer("업데이트 파일을 찾지 못했어요", error=True)
            return
        try:
            subprocess.Popen(
                [str(installer), "/SILENT", "/NORESTART"],
                close_fds=True,
                creationflags=getattr(subprocess, "DETACHED_PROCESS", 0),
            )
        except OSError as exc:
            self._set_footer(f"업데이트를 시작하지 못했어요: {exc}", error=True)
            return
        self.close()

    def _build_project_rail(self, parent: tk.Misc) -> None:
        """The list of open projects, between the icon rail and the workspace.

        Stays collapsed while only one project is open. A picker with one entry
        is not a choice, and the transcript is worth more than the 210px.
        """
        self.project_rail = tk.Frame(
            parent,
            background=COLORS["surface_alt"],
            highlightbackground=COLORS["border"],
            highlightthickness=1,
            borderwidth=0,
            width=PROJECT_RAIL_WIDTH,
        )
        self.project_rail.grid(row=0, column=1, sticky="nsew")
        self.project_rail.grid_propagate(False)
        self.project_rail.grid_rowconfigure(1, weight=1)
        self.project_rail.grid_columnconfigure(0, weight=1)

        head = tk.Frame(self.project_rail, background=COLORS["surface_alt"])
        head.grid(row=0, column=0, sticky="ew", padx=12, pady=(12, 6))
        head.grid_columnconfigure(0, weight=1)
        tk.Label(
            head,
            text="프로젝트",
            background=COLORS["surface_alt"],
            foreground=COLORS["muted"],
            anchor=tk.W,
            font=("Segoe UI Semibold", 9),
        ).grid(row=0, column=0, sticky="w")
        self.project_add_button = ttk.Button(
            head, text="+", width=3, command=self.add_project, style="Chip.TButton"
        )
        self.project_add_button.grid(row=0, column=1, sticky="e")

        self.project_list = tk.Frame(
            self.project_rail, background=COLORS["surface_alt"]
        )
        self.project_list.grid(row=1, column=0, sticky="nsew", padx=8)
        self.project_list.grid_columnconfigure(0, weight=1)
        self._project_rows: dict[str, dict[str, tk.Widget]] = {}
        self._refresh_project_rail()

    def _refresh_project_rail(self) -> None:
        """Rebuild the rows and show or hide the whole rail.

        Cheap to rebuild - there are as many rows as open projects - and it keeps
        row order, focus and status in one place rather than three.
        """
        if not hasattr(self, "project_list"):
            return
        for child in self.project_list.winfo_children():
            child.destroy()
        self._project_rows = {}
        sessions = sorted(self.sessions.values(), key=lambda item: item.name.lower())
        dots = {
            "running": (COLORS["accent"], "●"),
            "connected": (COLORS["online"], "●"),
            "idle": (COLORS["subtle"], "○"),
        }
        visible = self.__dict__.get("_visible", self._session)
        for row, session in enumerate(sessions):
            focused = session is visible
            background = COLORS["select_soft"] if focused else COLORS["surface_alt"]
            item = tk.Frame(self.project_list, background=background, cursor="hand2")
            item.grid(row=row, column=0, sticky="ew", pady=1)
            item.grid_columnconfigure(1, weight=1)
            colour, glyph = dots[session.status_dot()]
            marker = tk.Label(
                item,
                text=glyph,
                background=background,
                foreground=colour,
                font=("Segoe UI", 9),
            )
            marker.grid(row=0, column=0, rowspan=2, padx=(8, 6))
            name = tk.Label(
                item,
                text=_compact_activity_title(session.name, 22),
                background=background,
                foreground=COLORS["text"] if focused else COLORS["muted"],
                anchor=tk.W,
                font=("Segoe UI Semibold", 9),
            )
            name.grid(row=0, column=1, sticky="ew", pady=(6, 0))
            engine = tk.Label(
                item,
                text=_engine_brand(session.engine)["label"],
                background=background,
                foreground=COLORS["subtle"],
                anchor=tk.W,
                font=("Segoe UI", 8),
            )
            engine.grid(row=1, column=1, sticky="ew", pady=(0, 6))
            for widget in (item, marker, name, engine):
                widget.bind(
                    "<Button-1>",
                    lambda _event, key=session.key: self.focus_project(key),
                )
            self._project_rows[session.key] = {
                "item": item,
                "marker": marker,
                "name": name,
                "engine": engine,
            }
        wanted = len(sessions) > 1
        mapped = bool(self.project_rail.grid_info())
        if wanted and not mapped:
            self.project_rail.grid()
            self.project_rail.master.grid_columnconfigure(
                1, minsize=PROJECT_RAIL_WIDTH
            )
        elif not wanted and mapped:
            self.project_rail.grid_remove()
            self.project_rail.master.grid_columnconfigure(1, minsize=0)

    def add_project(self) -> None:
        """Open another project alongside the ones already open."""
        selected = filedialog.askdirectory(
            title="함께 열 프로젝트 폴더를 고르세요",
            initialdir=self.settings.project_dir or str(pathlib.Path.home()),
        )
        if not selected:
            return
        target = str(pathlib.Path(selected).expanduser().resolve())
        if os.path.normcase(target) in self.sessions:
            self.focus_project(os.path.normcase(target))
            self._set_footer("이미 열려 있는 프로젝트예요")
            return
        self._switch_history_project(target, rebind_state=True)
        self._set_footer(f"{pathlib.Path(target).name}을(를) 함께 열었어요")

    def focus_project(self, key: str) -> None:
        session = self.sessions.get(key)
        if session is None or session is self._session:
            return
        self._switch_history_project(str(session.settings.project_dir), rebind_state=True)

    def _capture_visible_session_controls(self) -> None:
        """Save editable shared widgets into the project currently on screen."""
        visible = self.__dict__.get("_visible", self._session)
        with self._acting_on(visible):
            if hasattr(self, "chat_input"):
                self.chat_draft = self.chat_input.get("1.0", "end-1c")
            if hasattr(self, "direct_args"):
                self.direct_args_text = self.direct_args.get("1.0", "end-1c")

    def _reset_project_status_widgets(self) -> None:
        """Remove the previous project's status while the new poll catches up."""
        engine_label = _engine_brand(self.engine)["label"]
        if hasattr(self, "provider_status"):
            self.provider_status.configure(
                text="● 로그인 확인 중", style="Warning.TLabel"
            )
        if hasattr(self, "proxy_status"):
            self.proxy_status.configure(
                text="● 프록시 확인 중", style="Warning.TLabel"
            )
        if hasattr(self, "unreal_status"):
            self.unreal_status.configure(
                text=f"● {engine_label} 확인 중", style="Warning.TLabel"
            )
        if hasattr(self, "blender_status"):
            self.blender_status.configure(
                text="● Blender 확인 중", style="Warning.TLabel"
            )
        if hasattr(self, "unreal_state_var"):
            self.unreal_state_var.set(
                "MCP 연결됨"
                if self.ue_connected
                else "연결 확인 중"
                if not self.ue_status_known
                else "오프라인"
            )
        if hasattr(self, "blender_state_var"):
            self.blender_state_var.set(
                "MCP 연결됨"
                if self.blender_connected
                else "MCP 준비됨 · Blender 실행 대기"
                if self.blender_configured
                else "연결 확인 중"
            )
        if hasattr(self, "mcp_state_var"):
            self.mcp_state_var.set("MCP 연결 확인 중")
        if hasattr(self, "mcp_endpoint_var"):
            self.mcp_endpoint_var.set("프로젝트 프록시 확인 중")
        if hasattr(self, "project_hint"):
            self.project_hint.configure(text=f"프로젝트  {self._session.name}")

    def _repaint_session_controls(self) -> None:
        """Point the shared session controls at whichever project has focus."""
        models = self.models_by_provider[self.provider_key]
        if hasattr(self, "provider_var"):
            self.provider_var.set(core.PROVIDER_LABELS[self.provider_key])
        if hasattr(self, "model_combo"):
            self.model_combo.configure(values=models)
        if hasattr(self, "model_var"):
            self.model_var.set(self.settings.model)
        if hasattr(self, "permission_var"):
            self.permission_var.set(PERMISSION_LABELS[self.permission_mode])
        if hasattr(self, "effort_combo"):
            self._update_effort_options()
        if hasattr(self, "project_var"):
            self.project_var.set(self.settings.project_dir)
        if hasattr(self, "engine_var"):
            self.engine_var.set(ENGINE_LABELS[self.settings.engine_override])
        if hasattr(self, "chat_input"):
            self.chat_input.delete("1.0", tk.END)
            self.chat_input.insert("1.0", self.chat_draft)
            self._update_chat_input_hint()
        if hasattr(self, "direct_args"):
            self.direct_args.delete("1.0", tk.END)
            self.direct_args.insert("1.0", self.direct_args_text or "{}")
        if hasattr(self, "tool_tree"):
            self._paint_tools()
        if hasattr(self, "direct_output"):
            self._replace_text(self.direct_output, self.direct_output_text)
        if hasattr(self, "log_output"):
            self._replace_text(self.log_output, self.log_text)
        self._render_attachments()
        self._update_project_header()
        self._reset_project_status_widgets()
        if hasattr(self, "file_tree_view"):
            self.file_tree_view.delete(*self.file_tree_view.get_children())
            self._file_nodes.clear()
            if self.right_tab_var.get() == "files":
                self.refresh_files()
        self._update_chat_context()

    def _rebuild_for_focused_engine(self, previous_engine: str) -> None:
        """Apply a focused project's engine palette without recapturing widgets.

        ``_switch_history_project`` has already captured the project being left
        before it changes ``_session``. Calling ``_apply_theme`` here would run
        that capture a second time against the newly focused session while the
        widgets still contain the previous project's values. Rebuild the shared
        widget tree directly, then repaint from the new session instead.
        """

        if previous_engine == self.engine:
            self._repaint_session_controls()
            return
        COLORS.clear()
        COLORS.update(self._themed_colors())
        selected = 0
        if hasattr(self, "notebook"):
            try:
                selected = self.notebook.index(self.notebook.select())
            except tk.TclError:
                selected = 0
        for child in self.root.winfo_children():
            child.destroy()
        self.root.configure(background=COLORS["window"])
        self._configure_styles()
        self._build_ui()
        self._restore_chat_history()
        self._repaint_session_controls()
        self._select_tab(selected)

    def _switch_history_project(
        self, project_dir: str, *, rebind_state: bool = False
    ) -> None:
        target = str(pathlib.Path(project_dir).expanduser().resolve())
        if self._same_project(self._history_project_dir, target):
            return
        self._capture_visible_session_controls()
        self._persist_chat_history()
        if rebind_state:
            previous_engine = self.engine
            session, created = self._open_session(target)
            self._session = session
            self._visible = session
            # Focus is logical/UI state only. Runtime workers bind their owning
            # workspace under _runtime_for; changing revvy's module globals here
            # would race a cold proxy start for another open project.
            self._rebuild_for_focused_engine(previous_engine)
            if previous_engine == self.engine:
                self._restore_chat_history()
            self._sync_composer_to_visible()
            self._update_edit_summary()
            if created:
                self._reset_chat_activities()
            elif session.activity_entries:
                # Returning to a project that was already open. Its activity card
                # may describe a turn that is still running; rebuilding it from
                # scratch would erase what that project is currently doing.
                self._render_chat_activities()
            else:
                self._hide_chat_activity()
            self._refresh_project_rail()
            self._refresh_watch_strip()
            if (
                hasattr(self, "root")
                and not getattr(self, "closed", False)
                and not getattr(self, "smoke_test", False)
            ):
                self.root.after(0, self.refresh_status)
                self.root.after(0, self.refresh_login)
            return
        self._history_project_dir = target
        self.history = (
            core.load_chat_history(
                target,
                self.workspace.chat_history_path()
                if self.workspace is not None
                else None,
            )
            if self.settings.remember_history and not self.smoke_test
            else []
        )
        self._restore_chat_history()
        self._reset_chat_activities()

    def _update_chat_context(self) -> None:
        if hasattr(self, "chat_context_var"):
            effort = EFFORT_LABELS.get(self.reasoning_effort, self.reasoning_effort or "Auto")
            permission = PERMISSION_LABELS.get(self.permission_mode, self.permission_mode)
            if (
                self.provider_key == core.PROVIDER_KIMI
                and self.permission_mode == core.PERMISSION_SAFE
            ):
                permission = "Safe · Kimi 전송 차단"
            self.chat_context_var.set(
                f"{core.PROVIDER_LABELS[self.provider_key]}  ·  "
                f"{self.model_var.get().strip() or 'default'}  ·  {effort}  ·  {permission}"
            )
            chip_values = (
                ("chat_provider_context_var", core.PROVIDER_LABELS[self.provider_key]),
                ("chat_model_context_var", self.model_var.get().strip() or "default"),
                ("chat_effort_context_var", effort),
                ("chat_permission_context_var", permission),
            )
            for attribute, value in chip_values:
                variable = getattr(self, attribute, None)
                if variable is not None:
                    variable.set(value)
            badge = getattr(self, "chat_permission_badge", None)
            if badge is not None:
                full_access = self.permission_mode == core.PERMISSION_FULL
                badge.configure(
                    background=COLORS["accent_soft"],
                    foreground=(
                        COLORS["warning_text"] if full_access else COLORS["accent_text"]
                    ),
                )

    def _update_project_header(self) -> None:
        if not hasattr(self, "header_project_var"):
            return
        raw = self.project_var.get().strip()
        project = pathlib.Path(raw)
        self.header_project_var.set(project.name or str(project) or "Unreal Project")
        if hasattr(self, "header_path_var"):
            self.header_path_var.set(self._elide_path(raw))

    @staticmethod
    def _elide_path(value: str, limit: int = 58) -> str:
        """Keep the drive and the tail; the middle is the part nobody reads."""
        text = value.strip()
        if len(text) <= limit:
            return text
        head, tail = text[:12], text[-(limit - 15) :]
        return f"{head}…{tail}"

    def wrap_input_code_block(self) -> None:
        """Fence the selection, or open an empty fence at the caret.

        Pasted engine logs and snippets lose their shape without a fence, and
        typing three backticks twice is the kind of chore a button should absorb.
        """
        widget = self.chat_input
        try:
            selected = widget.get("sel.first", "sel.last")
        except tk.TclError:
            selected = ""
        if selected:
            widget.delete("sel.first", "sel.last")
            widget.insert(tk.INSERT, f"```\n{selected.strip()}\n```\n")
        else:
            widget.insert(tk.INSERT, "```\n\n```\n")
            widget.mark_set(tk.INSERT, f"{tk.INSERT} -2 lines lineend")
        widget.focus_set()
        self._update_chat_input_hint()

    def _cancel_shortcut(self, _event: tk.Event | None = None) -> str | None:
        """Esc stops a running turn, and does nothing when none is running."""
        if not self.action_running:
            return None
        self.cancel_chat()
        return "break"

    def show_shortcut_guide(self) -> None:
        messagebox.showinfo(
            "단축키 안내",
            "Enter\t\t보내기\n"
            "Shift+Enter\t줄바꿈\n"
            "Esc\t\t진행 중인 요청 중지\n"
            "드래그 & 드롭\t이미지 파일 첨부",
            parent=self.root,
        )

    def _update_mcp_summary(self, upstreams: object) -> None:
        """Report external MCP servers by what they are doing, not just a count.

        A server that is configured but failed to start is not the same as no
        server at all, and that difference is what the user needs to act on.
        """
        if not hasattr(self, "mcp_state_var"):
            return
        servers = upstreams if isinstance(upstreams, dict) else {}
        if not servers:
            self.mcp_state_var.set("연결된 MCP 없음")
            return
        ready = sum(
            1
            for entry in servers.values()
            if isinstance(entry, dict) and (entry.get("ready") or entry.get("running"))
        )
        total = len(servers)
        if ready == total:
            self.mcp_state_var.set(f"MCP {total}개 연결됨")
        elif ready:
            self.mcp_state_var.set(f"MCP {ready}/{total}개 연결됨")
        else:
            self.mcp_state_var.set(f"MCP {total}개 대기 중")

    def open_mcp_config(self) -> None:
        """Open the upstream config, seeding an empty one when it is absent."""
        try:
            path = core.mcp_upstreams_config_path()
            if not path.exists():
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(
                    json.dumps({"servers": {}}, ensure_ascii=False, indent=2) + "\n",
                    encoding="utf-8",
                )
            if os.name == "nt":
                os.startfile(path)  # type: ignore[attr-defined]
            elif sys.platform == "darwin":
                subprocess.Popen(["open", str(path)])
            else:
                subprocess.Popen(["xdg-open", str(path)])
        except OSError as exc:
            self._set_footer(f"MCP 설정을 열지 못했어요: {exc}", error=True)
            return
        self._set_footer("MCP 설정을 열었어요. 저장 후 브리지를 다시 시작하세요")

    def _fit_empty_state(self, _event: tk.Event | None = None) -> None:
        """Drop the mascot rather than let the empty state clip off the top."""
        mascot = getattr(self, "_empty_mascot", None)
        if mascot is None:
            return
        room = self.empty_state.winfo_height()
        if room <= 1:
            return
        wanted = room >= 320
        mapped = bool(mascot.winfo_manager())
        if wanted and not mapped:
            mascot.pack(pady=(0, 10), after=self._empty_inner.winfo_children()[0])
        elif not wanted and mapped:
            mascot.pack_forget()

    @staticmethod
    def _theme_scrolledtext_bar(widget: scrolledtext.ScrolledText) -> None:
        """Repaint ScrolledText's classic Tk scrollbar in the palette.

        scrolledtext builds a plain tk.Scrollbar, not a ttk one, so it renders
        as a raised Win32 bar with arrow buttons welded to the widget's edge -
        untouched by any ttk style and by far the loudest thing next to the
        transcript on a dark surface.
        """
        try:
            widget.vbar.configure(
                background=COLORS["border"],
                troughcolor=COLORS["surface"],
                activebackground=COLORS["subtle"],
                relief=tk.FLAT,
                borderwidth=0,
                elementborderwidth=0,
                highlightthickness=0,
                width=10,
            )
        except tk.TclError:
            pass

    @staticmethod
    def _toggle_scrollbar(
        scrollbar: ttk.Scrollbar, first: str, last: str, **grid: object
    ) -> None:
        """Show a scrollbar only when there is something to scroll to.

        A bar that is always visible on a panel that usually fits reads as
        clutter; one that is never visible hides that content exists below.
        """
        scrollbar.set(first, last)
        needed = not (float(first) <= 0.0 and float(last) >= 1.0)
        mapped = bool(scrollbar.grid_info())
        if needed and not mapped:
            scrollbar.grid(**grid)  # type: ignore[arg-type]
        elif not needed and mapped:
            scrollbar.grid_remove()

    def _sync_context_scrollbar(self, first: str, last: str) -> None:
        self._toggle_scrollbar(
            self.context_scroll, first, last, row=0, column=1, sticky="ns"
        )

    def _sync_activity_scrollbar(self, first: str, last: str) -> None:
        self._toggle_scrollbar(
            self.activity_scroll, first, last, row=0, column=1, sticky="ns", padx=(4, 0)
        )

    def _ensure_activity_rows(self, count: int) -> None:
        """Grow the step-row pool to `count`, reusing what already exists.

        Rows are pooled rather than rebuilt per render because a tool run emits
        dozens of events and destroying labels on each one makes the card flicker.
        """
        while len(self.chat_activity_rows) < count:
            row = len(self.chat_activity_rows)
            self.chat_activity_steps.grid_rowconfigure(row, minsize=ACTIVITY_ROW_HEIGHT)
            icon_label = tk.Label(
                self.chat_activity_steps,
                text="",
                background=COLORS["surface_alt"],
                foreground=COLORS["subtle"],
                font=("Segoe UI Semibold", 9),
                width=2,
            )
            icon_label.grid(row=row, column=0, sticky="w")
            # Title and detail stack inside one cell so a step that gains a
            # second line does not push the row grid around.
            text_cell = tk.Frame(self.chat_activity_steps, background=COLORS["surface_alt"])
            text_cell.grid(row=row, column=1, sticky="ew", padx=(4, 8))
            text_cell.grid_columnconfigure(0, weight=1)
            title_label = tk.Label(
                text_cell,
                text="",
                background=COLORS["surface_alt"],
                foreground=COLORS["muted"],
                anchor=tk.W,
                font=("Segoe UI", 9),
                width=1,
            )
            title_label.pack(fill=tk.X, anchor=tk.W)
            detail_label = tk.Label(
                text_cell,
                text="",
                background=COLORS["surface_alt"],
                foreground=COLORS["subtle"],
                anchor=tk.W,
                justify=tk.LEFT,
                font=("Segoe UI", 8),
                width=1,
            )
            state_label = tk.Label(
                self.chat_activity_steps,
                text="",
                background=COLORS["surface_alt"],
                foreground=COLORS["subtle"],
                anchor=tk.E,
                font=("Segoe UI Semibold", 8),
                width=7,
            )
            state_label.grid(row=row, column=2, sticky="e")
            self.chat_activity_rows.append(
                {
                    "icon": icon_label,
                    "cell": text_cell,
                    "title": title_label,
                    "detail": detail_label,
                    "state": state_label,
                }
            )
        # Surplus rows are unmapped rather than left blank: an empty row still
        # occupies the scroll region, so leaving them would summon a scrollbar
        # for content that is not there.
        for index, widgets in enumerate(self.chat_activity_rows):
            wanted = index < count
            for key in ("icon", "cell", "state"):
                widget = widgets[key]
                mapped = bool(widget.grid_info())
                if wanted and not mapped:
                    widget.grid()
                elif not wanted and mapped:
                    widget.grid_remove()

    def _activity_at_tail(self) -> bool:
        try:
            return float(self.activity_canvas.yview()[1]) >= 0.999
        except (tk.TclError, IndexError, ValueError):
            return True

    def _follow_activity_tail(self) -> None:
        """Keep the newest step in view unless the reader scrolled away.

        Yanking the view back while someone is reading an earlier step is worse
        than letting the newest one sit off-screen, so this only fires when the
        view was already at the bottom before the update.
        """
        try:
            self.activity_canvas.update_idletasks()
            self.activity_canvas.yview_moveto(1.0)
        except tk.TclError:
            pass

    def _bind_mouse_wheel(self, canvas: tk.Canvas) -> None:
        """Scroll the canvas while the pointer is over it, and only then."""

        def _on_wheel(event: tk.Event) -> str:
            first, last = canvas.yview()
            if first <= 0.0 and last >= 1.0:
                return "break"
            canvas.yview_scroll(-1 if event.delta > 0 else 1, "units")
            return "break"

        canvas.bind("<Enter>", lambda _e: canvas.bind_all("<MouseWheel>", _on_wheel))
        canvas.bind("<Leave>", lambda _e: canvas.unbind_all("<MouseWheel>"))

    def copy_project_path(self) -> None:
        path = self.project_var.get().strip()
        if not path:
            return
        try:
            self.root.clipboard_clear()
            self.root.clipboard_append(path)
        except tk.TclError:
            self._set_footer("경로를 복사하지 못했어요")
            return
        self._set_footer("프로젝트 경로를 복사했어요")

    def _filter_tools(self, _event: tk.Event | None = None) -> None:
        self._populate_tools(self.tool_filter_var.get())

    def _populate_tools(self, query: str = "") -> None:
        normalized = query.strip().lower()
        self.tool_tree.delete(*self.tool_tree.get_children())
        visible = 0
        for index, tool in enumerate(self.tools):
            name = str(tool.get("name", ""))
            description = " ".join(str(tool.get("description", "")).split())
            if normalized and normalized not in name.lower() and normalized not in description.lower():
                continue
            self.tool_tree.insert("", tk.END, iid=str(index), values=(name, description))
            visible += 1
        total = len(self.tools)
        self.tool_count_var.set(f"{visible} / {total} tools" if normalized else f"{total} tools")

    @contextlib.contextmanager
    def _runtime_for(
        self,
        owner: ProjectSession | None = None,
    ) -> Iterator[Any]:
        """Temporarily bind revvy.py to one workspace without leaking globally.

        The CLI predates the desktop app's multi-project sessions and stores its
        paths in module globals. Keep the compatibility bridge serialized and
        restore both the module and the small set of environment keys exactly.
        Callers must keep slow work outside this context unless that work itself
        reads revvy's runtime globals.
        """

        session = owner or getattr(self, "_session", None)
        workspace = getattr(session, "workspace", None)
        if workspace is None:
            raise ConnectionError("Revvy project workspace is unavailable")
        lock = getattr(self, "runtime_lock", None)
        if lock is None:
            lock = threading.RLock()
            self.runtime_lock = lock
        environment_names = (
            "REVVY_STATE_DIR",
            "REVVY_PROJECT_DIR",
            "REVVY_CONFIG",
            "REVVY_MCP_UPSTREAMS_CONFIG",
            "REVVY_PROXY_LOG",
        )
        module_names = (
            "STATE_DIR",
            "CONFIG_PATH",
            "UPSTREAM_CONFIG_PATH",
            "MANIFEST_PATH",
            "PROCESS_PATH",
            "LOG_PATH",
            "_RUNTIME_PROJECT_DIR",
            "_ALLOW_LEGACY_CONFIG_FALLBACK",
        )
        missing = object()
        with lock:
            previous_environment = {
                name: os.environ.get(name, missing) for name in environment_names
            }
            previous_module = {name: getattr(revvy, name) for name in module_names}
            try:
                for name in environment_names:
                    value = workspace.env.get(name)
                    if value is None:
                        os.environ.pop(name, None)
                    else:
                        os.environ[name] = str(value)
                # Re-read the installed workspace environment instead of passing
                # state_root explicitly: explicit host deployment overrides for
                # config/upstreams/log are part of Workspace.env and must survive.
                revvy.configure_runtime_paths()
                if not core.same_project_path(
                    revvy.CONFIG_PATH,
                    workspace.config_path(),
                ):
                    raise ConnectionError(
                        "Revvy project runtime changed; focus the project and try again"
                    )
                yield revvy
            finally:
                for name, value in previous_module.items():
                    setattr(revvy, name, value)
                for name, value in previous_environment.items():
                    if value is missing:
                        os.environ.pop(name, None)
                    else:
                        os.environ[name] = str(value)

    def _post(
        self,
        callback: Callable[..., None],
        *args: Any,
        session: ProjectSession | None | object = _POST_CURRENT_SESSION,
    ) -> None:
        """Queue a UI callback, optionally bound to the project that owns it.

        A worker's callbacks used to land on whichever project happened to be
        focused when they ran. With two projects working at once that means one
        project's finished turn clears the other project's running flag.
        """
        if not self.closed:
            owner: ProjectSession | None
            if session is _POST_CURRENT_SESSION:
                worker_context = getattr(self, "_worker_context", None)
                owner = getattr(worker_context, "session", self._session)
            else:
                owner = session  # type: ignore[assignment]
            self.ui_queue.put((callback, args, owner))

    @contextlib.contextmanager
    def _acting_on(self, session: ProjectSession | None) -> Iterator[None]:
        """Make ``session`` the one that forwarded attributes resolve to.

        Widgets keep showing whatever is focused - see ``_is_visible`` - so a
        background project's turn updates its own state without repainting the
        screen out from under the project the user is reading.
        """
        if session is None:
            yield
            return
        previous = self._session
        self._session = session
        try:
            yield
        finally:
            self._session = previous

    def _is_visible(self, session: ProjectSession | None = None) -> bool:
        """Whether this session is the one the widgets are currently showing."""
        target = session or self._session
        return target is self.__dict__.get("_visible", self._session)

    def _drain_queue(self) -> None:
        try:
            while True:
                try:
                    callback, args, owner = self.ui_queue.get_nowait()
                except queue.Empty:
                    break
                try:
                    with self._acting_on(owner):
                        callback(*args)
                except Exception as exc:
                    callback_name = getattr(callback, "__name__", callback.__class__.__name__)
                    try:
                        with self._acting_on(owner):
                            self._append_log(
                                f"UI callback failed ({callback_name}): {exc}"
                            )
                    except Exception:
                        pass
        finally:
            if not self.closed:
                self.root.after(40, self._drain_queue)

    def _run_worker(
        self,
        label: str,
        task: Callable[[], Any],
        success: Callable[[Any], bool | None] | None = None,
        *,
        exclusive: bool = True,
    ) -> None:
        if exclusive and self.action_running:
            self._set_footer("다른 작업이 실행 중입니다", error=True)
            return
        owner = self._session
        if exclusive:
            self.action_running = True
            self._set_controls_busy(True)
        self._set_footer(label)

        def worker() -> None:
            worker_context = getattr(self, "_worker_context", None)
            if worker_context is None:
                worker_context = threading.local()
                self._worker_context = worker_context
            previous_owner = getattr(
                worker_context, "session", _POST_CURRENT_SESSION
            )
            worker_context.session = owner
            try:
                try:
                    result = task()
                except Exception as exc:  # UI boundary: show every actionable error.
                    self._post(
                        self._worker_failed,
                        label,
                        str(exc),
                        exclusive,
                        session=owner,
                    )
                    return
                self._post(
                    self._worker_succeeded,
                    label,
                    result,
                    success,
                    exclusive,
                    session=owner,
                )
            finally:
                if previous_owner is _POST_CURRENT_SESSION:
                    try:
                        del worker_context.session
                    except AttributeError:
                        pass
                else:
                    worker_context.session = previous_owner

        threading.Thread(target=worker, name=f"Revvy-{label}", daemon=True).start()

    def _worker_failed(self, label: str, error: str, exclusive: bool) -> None:
        if exclusive:
            self.action_running = False
            self._sync_composer_to_visible()
        self._append_log(f"ERROR {label}: {error}")
        self._set_footer(error, error=True)

    def _worker_succeeded(
        self,
        label: str,
        result: Any,
        success: Callable[[Any], None] | None,
        exclusive: bool,
    ) -> None:
        if exclusive:
            self.action_running = False
            self._sync_composer_to_visible()
        footer_handled = success(result) is True if success else False
        if not footer_handled:
            self._set_footer(f"{label} 완료")

    def _sync_composer_to_visible(self) -> None:
        """Put the shared composer in the state of the project on screen.

        Called whenever a turn starts or ends anywhere, so the buttons describe
        the project the user is looking at rather than the last one to report.
        """
        if not hasattr(self, "send_button"):
            return
        visible = self.__dict__.get("_visible", self._session)
        with self._acting_on(visible):
            busy = visible.busy
            self._set_controls_busy(busy, session=visible)
            self.cancel_button.configure(state=tk.NORMAL if busy else tk.DISABLED)
            self.send_button.configure(state=tk.DISABLED if busy else tk.NORMAL)
            if busy:
                self.send_button.grid_remove()
                self.cancel_button.grid()
            else:
                self.cancel_button.grid_remove()
                self.send_button.grid()
        self._refresh_project_rail()
        self._refresh_watch_strip()

    def _set_controls_busy(
        self, busy: bool, *, session: ProjectSession | None = None
    ) -> None:
        if session is not None and not self._is_visible(session):
            return
        state = tk.DISABLED if busy else tk.NORMAL
        combo_state = "disabled" if busy else "readonly"
        self.provider_combo.configure(state=combo_state)
        self.model_combo.configure(state=combo_state)
        self.effort_combo.configure(state=combo_state)
        self.permission_combo.configure(state=combo_state)
        self.project_entry.configure(state=state)
        self.project_browse_button.configure(state=state)
        self.start_button.configure(state=state)
        self.restart_button.configure(state=state)
        self.direct_run_button.configure(state=state)
        self.attach_button.configure(state=state)
        unreal_state = (
            tk.DISABLED
            if busy or self.ue_connected or not self.ue_status_known
            else tk.NORMAL
        )
        self.unreal_launch_button.configure(state=unreal_state)
        secondary_state = (
            tk.DISABLED
            if busy
            or not self.ue_status_known
            or self.engine == core.ENGINE_UNITY
            or (self.engine == core.ENGINE_UNREAL and self.ue_connected)
            else tk.NORMAL
        )
        self.unreal_build_button.configure(state=secondary_state)
        self.unreal_maintenance_button.configure(
            state=unreal_state if self.engine == core.ENGINE_UNREAL else tk.DISABLED
        )
        blender_setup_state = tk.DISABLED if busy or self.blender_connected else tk.NORMAL
        blender_launch_state = (
            tk.DISABLED
            if busy or self.blender_connected or not self.blender_configured
            else tk.NORMAL
        )
        self.blender_setup_button.configure(state=blender_setup_state)
        self.blender_launch_button.configure(state=blender_launch_state)
        if self.active_process is None:
            self.send_button.configure(state=state)

    def _set_footer(self, value: str, *, error: bool = False) -> None:
        if not self._is_visible():
            return
        self.footer_var.set(value)
        if error:
            self.root.bell()

    def _render_chat_activities(self) -> None:
        if not self._is_visible():
            # Entries were already recorded on the owning session. Drawing them
            # would overwrite the card of whichever project is on screen.
            return
        if not self.chat_activity.winfo_ismapped():
            self.chat_activity.grid()
        states = {
            "running": ("✦", "하는 중", COLORS["accent"]),
            "completed": ("✓", "완료!", COLORS["online"]),
            "failed": ("✕", "앗, 실패", COLORS["danger"]),
            "canceled": ("◼", "잠깐 멈춤", COLORS["warning"]),
        }
        at_tail = self._activity_at_tail()
        visible = self.activity_entries
        self._ensure_activity_rows(max(len(visible), ACTIVITY_VISIBLE_ROWS))
        tool_entries = [
            item for item in self.activity_entries if item["key"] != "turn"
        ]
        completed = sum(item["state"] == "completed" for item in tool_entries)
        failed = sum(item["state"] == "failed" for item in tool_entries)
        canceled = sum(item["state"] == "canceled" for item in tool_entries)
        total = len(tool_entries)
        chat_running = bool(
            getattr(self, "action_running", False)
            and getattr(self, "chat_active_provider", "")
        )
        if chat_running:
            provider_name = {
                core.PROVIDER_CODEX: "Codex",
                core.PROVIDER_CLAUDE: "Claude",
                core.PROVIDER_KIMI: "Kimi",
            }.get(getattr(self, "chat_active_provider", ""), "AI")
            processed = completed + failed + canceled
            summary = f"레비 열심히 작업 중 ({provider_name}) · 도구 {processed}단계 처리"
            if failed:
                summary += f" · {failed} 실패"
            now = time.monotonic()
            started_at = float(getattr(self, "chat_turn_started_at", 0.0) or 0.0)
            last_event_at = float(getattr(self, "chat_last_event_at", 0.0) or 0.0)
            elapsed = now - started_at if started_at else 0.0
            silent_for = now - last_event_at if last_event_at else elapsed
            if silent_for >= CHAT_SILENT_NOTICE_SECONDS:
                summary += f" · 응답 대기 {_format_duration(silent_for)}"
            elif elapsed:
                summary += f" · 경과 {_format_duration(elapsed)}"
        else:
            if canceled:
                summary = f"잠깐 멈췄어요 · 도구 {completed}단계 완료"
            elif failed:
                summary = f"여기까지 해냈어요 · 도구 {completed}단계 완료"
            else:
                summary = f"완료! 도구 {completed}단계 처리 ✨"
            if failed:
                summary += f" · {failed} 실패"
        self.chat_activity_var.set(summary)

        for row, widgets in enumerate(self.chat_activity_rows):
            if row >= len(visible):
                widgets["icon"].configure(text="")
                widgets["title"].configure(text="")
                widgets["state"].configure(text="")
                widgets["detail"].pack_forget()
                continue
            item = visible[row]
            icon, state_label, color = states.get(item["state"], states["running"])
            widgets["icon"].configure(text=icon, foreground=color)
            compact = _compact_activity_title(item["title"])
            widgets["title"].configure(
                text=compact,
                foreground=COLORS["text"] if item["state"] == "running" else COLORS["muted"],
            )
            widgets["state"].configure(text=state_label, foreground=color)
            detail = _activity_detail(item["title"], compact)
            if detail:
                widgets["detail"].configure(text=detail)
                widgets["detail"].pack(fill=tk.X, anchor=tk.W)

            else:
                widgets["detail"].pack_forget()
        if at_tail:
            self.root.after_idle(self._follow_activity_tail)
        has_running = chat_running or any(
            item["state"] == "running" for item in self.activity_entries
        )
        has_failed = any(item["state"] == "failed" for item in self.activity_entries)
        has_canceled = any(item["state"] == "canceled" for item in self.activity_entries)
        overall_state = (
            "running"
            if has_running
            else "failed"
            if has_failed
            else "canceled"
            if has_canceled
            else "completed"
        )
        self.chat_activity_marker.configure(foreground=states[overall_state][2])
        self.chat_activity_progress.stop()
        if has_running:
            self.chat_activity_percent_var.set(
                f"{completed + failed + canceled}/{total}" if total else "LIVE"
            )
            self.chat_activity_progress.configure(
                mode="indeterminate",
                value=0,
                style="Activity.Horizontal.TProgressbar",
            )
            self.chat_activity_progress.start(18)
        else:
            terminal_progress = (
                100
                if self.activity_entries and not (has_failed or has_canceled)
                else int((completed / total) * 100)
                if total
                else 0
            )
            self.chat_activity_percent_var.set(f"{terminal_progress}%")
            self.chat_activity_progress.configure(
                mode="determinate",
                maximum=100,
                value=terminal_progress,
                style=(
                    "ActivityIssue.Horizontal.TProgressbar"
                    if has_failed or has_canceled
                    else "ActivityDone.Horizontal.TProgressbar"
                ),
            )

    def _set_chat_activity(
        self,
        value: str,
        state: str = "running",
        activity_id: str = "",
    ) -> None:
        title = core.humanize_activity(" ".join(str(value).split()))
        key = activity_id or f"{state}:{title}"
        entry = self.activity_index.get(key)
        if entry is None and not activity_id and state != "running":
            entry = next(
                (
                    item
                    for item in reversed(self.activity_entries)
                    if item["state"] == "running" and item["title"] == title
                ),
                None,
            )
        if entry is None:
            entry = {"key": key, "title": title, "state": state}
            self.activity_entries.append(entry)
            self.activity_index[key] = entry
        else:
            entry["title"] = title
            entry["state"] = state
        self._render_chat_activities()
        state_label = {
            "running": "진행 중",
            "completed": "완료",
            "failed": "실패",
            "canceled": "중지",
        }.get(state, "진행 중")
        if (
            bool(
                getattr(self, "action_running", False)
                and getattr(self, "chat_active_provider", "")
            )
            and state in {"completed", "failed", "canceled"}
            and key != "turn"
        ):
            self._set_footer(
                f"단계 {state_label} · {title} · 전체 작업 계속 중"
            )
        else:
            self._set_footer(f"{state_label} · {title}")

    def _mark_chat_event(self) -> None:
        self.chat_last_event_at = time.monotonic()

    def _chat_liveness_tick(self) -> None:
        if self.closed:
            return
        if (
            self.action_running
            and self.chat_active_provider
            and self.activity_entries
        ):
            self._render_chat_activities()
        if any(
            session.busy
            for session in self.sessions.values()
            if session is not self.__dict__.get("_visible", self._session)
        ):
            # Elapsed times and current steps for the background projects. Only
            # while one is actually working, so an idle app rebuilds nothing.
            self._refresh_watch_strip()
        self.root.after(CHAT_LIVENESS_TICK_MS, self._chat_liveness_tick)

    def _finish_running_activities(self, state: str) -> None:
        for item in self.activity_entries:
            if item["state"] == "running":
                item["state"] = state
        if self.activity_entries:
            self._render_chat_activities()

    def _reset_chat_activities(self) -> None:
        self.activity_entries.clear()
        self.activity_index.clear()
        self._hide_chat_activity()

    def _hide_chat_activity(self) -> None:
        if not self._is_visible():
            return
        self.chat_activity_progress.stop()
        self.chat_activity_progress.configure(
            mode="determinate",
            maximum=100,
            value=0,
            style="Activity.Horizontal.TProgressbar",
        )
        self.chat_activity_var.set("대기 중 · 레비에게 말을 걸어주세요!")
        self.chat_activity_percent_var.set("READY")
        self.chat_activity_marker.configure(foreground=COLORS["subtle"])
        self._ensure_activity_rows(ACTIVITY_VISIBLE_ROWS)
        if hasattr(self, "edit_summary_button"):
            self.edit_summary_button.grid_remove()
        for row, widgets in enumerate(self.chat_activity_rows):
            widgets["icon"].configure(
                text="·" if row == 0 else "",
                foreground=COLORS["subtle"],
            )
            widgets["title"].configure(
                text="다음 요청을 기다리고 있어요" if row == 0 else "",
                foreground=COLORS["muted"],
            )
            widgets["state"].configure(
                text="준비" if row == 0 else "",
                foreground=COLORS["subtle"],
            )
            widgets["detail"].pack_forget()
        try:
            self.activity_canvas.yview_moveto(0.0)
        except tk.TclError:
            pass
        self.chat_activity.grid_remove()

    def _append_log(self, value: str) -> None:
        timestamp = time.strftime("%H:%M:%S")
        line = f"[{timestamp}] {value}\n"
        self.log_text = (self.log_text + line)[-200_000:]
        if not self._is_visible():
            return
        self.log_output.configure(state=tk.NORMAL)
        self.log_output.insert(tk.END, line)
        self.log_output.see(tk.END)
        self.log_output.configure(state=tk.DISABLED)

    def _apply_log_snapshot(self, value: str) -> bool:
        self.log_text = str(value)[-200_000:]
        if self._is_visible():
            self._replace_text(self.log_output, self.log_text)
        return True

    def _apply_direct_output(self, value: str) -> bool:
        self.direct_output_text = str(value)
        if self._is_visible():
            self._replace_text(self.direct_output, self.direct_output_text)
        return True

    @staticmethod
    def _replace_text(widget: tk.Text, value: str) -> None:
        widget.configure(state=tk.NORMAL)
        widget.delete("1.0", tk.END)
        widget.insert("1.0", value)
        widget.configure(state=tk.DISABLED)

    def _close_open_message(self) -> None:
        """End the previous message with a newline carrying that message's tag.

        Tk paints a tagged newline as a block running to the right text edge, but
        paints an untagged one not at all. The terminator used to be supplied by
        the *next* message's label tag, so the final line of a message with a
        background stopped at its last character and left a ragged notch of bare
        surface beside it. Tagging the terminator is what makes the block flush.

        Streamed replies arrive fragment by fragment and cannot terminate
        themselves, so this runs lazily before whatever comes next.
        """
        end = self.chat_output.index("end-1c")
        if end.endswith(".0"):
            return
        owned = [
            name
            for name in self.chat_output.tag_names(f"{end} -1c")
            if name in ("user_body", "assistant_body", "error", "notice")
        ]
        self.chat_output.insert(tk.END, "\n", tuple(owned))

    def _append_chat(
        self,
        role: str,
        text: str,
        *,
        error: bool = False,
        notice: bool = False,
        label: bool = True,
        close_previous: bool = True,
    ) -> None:
        if not self._is_visible():
            # A background project's reply. It is already accumulating in that
            # project's own history and pending_reply, and replays when the user
            # focuses it - painting it here would drop it into the wrong
            # project's transcript.
            if role != "You" and not error and not notice:
                self._session.pending_reply += text
            return
        self._hide_empty_chat()
        self.chat_output.configure(state=tk.NORMAL)
        # Sampled before the inserts: whether the reader was already at the
        # bottom is the question, and after inserting they never are.
        pinned = not getattr(self, "_suppress_autoscroll", False) and (
            self.chat_output.yview()[1] >= 0.999
        )
        if close_previous:
            # Unconditional, not label-only. A cancel or error notice carries no
            # label, and without this it used to weld onto the last word of the
            # reply it was reporting on - "부분 답변요청을 멈췄어요".
            self._close_open_message()
        if label:
            is_user = role == "You"
            tag = "user_label" if is_user else "assistant_label"
            display_role = "나" if is_user else "레비"
            if not is_user and self._mascot_inline is not None:
                # image_create takes no tags, so an untagged image at column 0
                # made Tk resolve the whole label line from defaults - the
                # configured margins and spacing were silently discarded and the
                # turn gap changed depending on whether the PNG loaded.
                # "end" sits past the widget's own trailing newline, so the mark
                # has to be "end-1c" - that is where image_create actually lands.
                mark = self.chat_output.index("end-1c")
                self.chat_output.image_create(tk.END, image=self._mascot_inline)
                self.chat_output.tag_add(tag, mark, f"{mark}+1c")
                self.chat_output.insert(tk.END, "  ", tag)
            self.chat_output.insert(tk.END, f"{display_role}\n", tag)
        if error:
            body_tag = "error"
        elif notice:
            body_tag = "notice"
        else:
            body_tag = "user_body" if role == "You" else "assistant_body"
        self.chat_output.insert(tk.END, text, body_tag)
        if body_tag == "user_body":
            # User messages are never streamed, so this one can close itself and
            # be flush the moment it appears rather than when the reply starts.
            self._close_open_message()
        if pinned:
            # Following the tail unconditionally yanked the view away from
            # whatever the reader was looking at on every streamed fragment.
            self.chat_output.see(tk.END)
        self.chat_output.configure(state=tk.DISABLED)

    def _append_chat_fragment(self, text: str) -> None:
        # Never closes the previous message: a fragment IS the previous message
        # continuing, and terminating each one would shred a reply into one line
        # per token.
        self._append_chat("Revvy", text, label=False, close_previous=False)

    def _provider_changed(self, _event: tk.Event | None = None) -> None:
        previous_provider = self.provider_key
        self.reasoning_by_provider[previous_provider] = self.reasoning_effort
        label = self.provider_var.get()
        self.provider_key = next(
            (key for key, value in core.PROVIDER_LABELS.items() if value == label),
            core.PROVIDER_CODEX,
        )
        self.reasoning_effort = self.reasoning_by_provider[self.provider_key]
        models = self.models_by_provider[self.provider_key]
        self.model_combo.configure(values=models)
        current = self.model_var.get().strip()
        if current not in models:
            self.model_var.set(self.provider_default_models[self.provider_key])
        self._update_effort_options()
        self._save_settings()
        self._update_chat_context()
        self.refresh_login()
        if (
            self.provider_key == core.PROVIDER_KIMI
            and self.permission_mode == core.PERMISSION_SAFE
        ):
            self._set_footer(
                "Kimi 비대화형 모드는 읽기 전용을 보장하지 않아 Safe에서 전송이 차단됩니다. "
                "사용하려면 Full access를 선택하세요.",
                error=True,
            )

    def _model_changed(self, _event: tk.Event | None = None) -> None:
        self._update_effort_options()
        self._save_settings()
        self._update_chat_context()

    def _update_effort_options(self) -> None:
        if self.provider_key == core.PROVIDER_CODEX:
            efforts, default = core.discover_codex_reasoning_levels(self.model_var.get().strip())
            efforts = ["", *efforts]
        elif self.provider_key == core.PROVIDER_KIMI:
            efforts, default = [""], ""
        else:
            efforts, default = list(CLAUDE_REASONING_EFFORTS), ""
        if not efforts:
            efforts = [""]
        current = self.reasoning_effort
        if current not in efforts:
            current = default if default in efforts else efforts[0]
        self.reasoning_effort = current
        self.reasoning_by_provider[self.provider_key] = current
        labels = [EFFORT_LABELS.get(value, value.title()) for value in efforts]
        self.effort_combo.configure(values=labels)
        self.effort_var.set(EFFORT_LABELS.get(current, current.title()))

    def _effort_changed(self, _event: tk.Event | None = None) -> None:
        self.reasoning_effort = EFFORT_VALUES.get(self.effort_var.get(), "")
        self.reasoning_by_provider[self.provider_key] = self.reasoning_effort
        self._save_settings()
        self._update_chat_context()

    def _permission_changed(self, _event: tk.Event | None = None) -> None:
        selected = PERMISSION_VALUES.get(self.permission_var.get(), core.PERMISSION_SAFE)
        if selected == self.permission_mode:
            return
        if self.action_running or self.active_process is not None:
            self.permission_var.set(PERMISSION_LABELS[self.permission_mode])
            return
        if selected == core.PERMISSION_FULL:
            confirmed = messagebox.askyesno(
                "Full access 활성화",
                "Full access는 프로젝트와 사용자 설정(.codex 포함) 쓰기, 셸 명령, "
                "네트워크 및 로컬 MCP 설정을 승인 없이 사용할 수 있습니다.\n\n"
                "이 프로젝트에서 활성화할까요?",
                parent=self.root,
            )
            if not confirmed:
                self.permission_var.set(PERMISSION_LABELS[self.permission_mode])
                return
        self.permission_mode = selected
        self.settings.codex_thread_id = ""
        self.settings.codex_session_key = ""
        self.settings.claude_session_id = ""
        self.settings.claude_session_key = ""
        self.settings.kimi_session_id = ""
        self.settings.kimi_session_key = ""
        self._save_settings()
        self._update_chat_context()
        self._set_footer(f"권한 · {PERMISSION_LABELS[self.permission_mode]}")

    def _save_settings(self) -> None:
        visible = self._is_visible()
        self.settings.provider = self.provider_key
        if visible:
            self.settings.model = self.model_var.get().strip()
        self.settings.permission_mode = self.permission_mode
        self.settings.reasoning_effort = self.reasoning_effort
        self.settings.codex_reasoning_effort = self.reasoning_by_provider[core.PROVIDER_CODEX]
        self.settings.claude_reasoning_effort = self.reasoning_by_provider[core.PROVIDER_CLAUDE]
        self.settings.kimi_reasoning_effort = self.reasoning_by_provider[core.PROVIDER_KIMI]
        self.settings.effort_settings_version = 1
        if self.workspace is not None:
            self.settings.project_dir = str(self.workspace.project_dir)
        elif visible:
            self.settings.project_dir = self.project_var.get().strip()
        if visible:
            self._update_project_header()
        try:
            if visible:
                self.settings.geometry = self.root.geometry()
            core.save_settings(
                self.settings,
                self.workspace.settings_path()
                if self.workspace is not None
                else None,
            )
        except OSError as exc:
            self._append_log(f"Could not save settings: {exc}")

    def browse_project(self) -> None:
        if self.action_running:
            self._set_footer("작업 중에는 프로젝트를 변경할 수 없습니다")
            return
        selected = filedialog.askdirectory(initialdir=self.project_var.get() or str(pathlib.Path.home()))
        if selected:
            project_changed = not core.same_project_path(self.project_var.get(), selected)
            selected_key = os.path.normcase(
                str(pathlib.Path(selected).expanduser().resolve())
            )
            session_was_open = selected_key in self.sessions
            if project_changed:
                self._save_settings()
                # Browsing opens another session alongside the current one. Its
                # proxy may still serve a background turn, so focus changes must
                # never stop it as a side effect.
            self._switch_history_project(selected, rebind_state=project_changed)
            self.project_var.set(selected)
            if project_changed and not session_was_open:
                self.engine_var.set(ENGINE_LABELS[self.settings.engine_override])
                self.tools = []
                self._apply_tools([])
            self._save_settings()
            self._update_project_header()
            engine = core.resolve_engine(
                selected,
                self.settings.engine_override,
                self._proxy_config_engine(
                    selected,
                    self.workspace.config_path() if self.workspace is not None else None,
                ),
            )
            core.write_proxy_project_context(
                selected,
                engine,
                path=self.workspace.config_path() if self.workspace is not None else None,
            )
            engine_changed = engine != self.engine
            self._adopt_engine(engine)
            self.refresh_status()
            if (
                project_changed
                and not engine_changed
                and not self.smoke_test
                and not self.tools
            ):
                self.root.after(150, self.load_tools)

    def refresh_status(self) -> None:
        if self.status_running or self.closed:
            return
        self.status_running = True
        owner = self._session

        def worker() -> None:
            try:
                with self._runtime_for(owner):
                    status = _status_snapshot()
                self._post(self._apply_status, status, session=owner)
            except Exception as exc:
                self._post(self._status_failed, str(exc), session=owner)

        threading.Thread(target=worker, name="Revvy-status", daemon=True).start()

    def _apply_status(self, status: dict[str, Any]) -> None:
        self.status_running = False
        if not self._is_visible():
            if not self.closed:
                self.root.after(0, self.refresh_status)
            return
        proxy_running = bool(status.get("proxy_running"))
        ue_running = bool(status.get("ue_running"))
        self.mcp_endpoint_var.set(_proxy_endpoint_label(status.get("proxy_url")))
        self.proxy_status.configure(
            text=f"● 프록시 {'준비됨' if proxy_running else '중지'}",
            style="Online.TLabel" if proxy_running else "Offline.TLabel",
        )
        if self.engine == core.ENGINE_UNREAL:
            self.unreal_status.configure(
                text=f"● Unreal {'연결됨' if ue_running else '오프라인'}",
                style="Online.TLabel" if ue_running else "Offline.TLabel",
            )
        else:
            engine_label = _engine_brand(self.engine)["label"]
            self.unreal_status.configure(
                text=f"● {engine_label} {'연결됨' if ue_running else '브리지 준비 중'}",
                style="Online.TLabel" if ue_running else "Offline.TLabel",
            )
        upstream_health = status.get("upstreams")
        upstreams = (
            upstream_health.get("servers", {})
            if isinstance(upstream_health, dict)
            else {}
        )
        self._update_mcp_summary(upstreams)
        blender = (
            upstreams.get(core.BLENDER_MCP_SERVER_NAME, {})
            if isinstance(upstreams, dict)
            else {}
        )
        blender_state = str(blender.get("state") or "").casefold()
        blender_tools = int(blender.get("tools") or blender.get("tool_count") or 0)
        blender_connected = bool(status.get("blender_bridge_connected"))
        blender_configured = bool(
            blender.get("configured", bool(blender))
        ) and blender_state not in {"disabled", "invalid", "unconfigured"}
        self.blender_connected = blender_connected
        self.blender_configured = blender_configured
        self.blender_status.configure(
            text=f"● Blender {'연결됨' if blender_connected else '대기'}",
            style="Online.TLabel" if blender_connected else "Offline.TLabel",
        )
        if blender_connected:
            self.blender_state_var.set(f"MCP 연결됨 · 도구 {blender_tools}개")
        elif blender_configured:
            error = str(
                status.get("blender_bridge_error")
                or blender.get("last_error")
                or ""
            ).strip()
            self.blender_state_var.set(
                error[:56] if error else "MCP 준비됨 · Blender 실행 대기"
            )
        else:
            self.blender_state_var.set("연결 준비 후 Blender를 실행하세요")
        self.ue_connected = ue_running
        self.ue_status_known = True
        if self.engine == core.ENGINE_UNREAL:
            self.unreal_state_var.set("MCP 연결됨" if ue_running else "오프라인")
        else:
            self.unreal_state_var.set(
                "MCP 연결됨" if ue_running else "브리지 준비 중"
            )
        self._set_controls_busy(self.action_running)
        project = status.get("active_project")
        if isinstance(project, dict) and project.get("name"):
            if core.same_project_path(project.get("path"), self.project_var.get()):
                self.project_hint.configure(text=f"연결 프로젝트  {project['name']}")
            else:
                self.project_hint.configure(text=f"다른 프로젝트 연결됨  {project['name']}")
        else:
            self.project_hint.configure(text=f"캐시된 도구  {status.get('manifest_tools', 0)}개")
        if not self.closed:
            self.root.after(2500, self.refresh_status)
        if self.settings.engine_override is None:
            reported = str(status.get("engine", "")).strip().lower()
            if (
                reported in core.ENGINES
                and isinstance(project, dict)
                and core.same_project_path(project.get("path"), self.project_var.get())
            ):
                self._adopt_engine(reported)

    def _status_failed(self, error: str) -> None:
        self.status_running = False
        if not self._is_visible():
            if not self.closed:
                self.root.after(0, self.refresh_status)
            return
        self.ue_status_known = False
        self.proxy_status.configure(text="● 프록시 오류", style="Warning.TLabel")
        if self.engine == core.ENGINE_UNREAL:
            self.unreal_status.configure(
                text="● Unreal 확인 실패",
                style="Warning.TLabel",
            )
        else:
            self.unreal_status.configure(
                text=f"● {_engine_brand(self.engine)['label']} 브리지 확인 실패",
                style="Warning.TLabel",
            )
        self.blender_status.configure(text="● Blender 확인 실패", style="Warning.TLabel")
        self.blender_state_var.set("상태 확인 실패")
        self.unreal_state_var.set("상태 확인 실패")
        self._set_controls_busy(self.action_running)
        self._append_log(f"Status error: {error}")
        if not self.closed:
            self.root.after(2500, self.refresh_status)

    def refresh_login(self) -> None:
        provider = self.provider_key
        self.provider_status.configure(text="● 로그인 확인 중", style="Warning.TLabel")

        def task() -> tuple[str, bool, str]:
            ok, detail = core.run_login_status(provider)
            return provider, ok, detail

        self._run_worker("Login status", task, self._apply_login_status, exclusive=False)

    def _apply_login_status(self, result: tuple[str, bool, str]) -> None:
        provider, ok, detail = result
        if provider != self.provider_key:
            return
        self._append_log(f"{core.PROVIDER_LABELS[provider]}: {detail or 'no status output'}")
        if not self._is_visible():
            return
        provider_name = {
            core.PROVIDER_CODEX: "ChatGPT",
            core.PROVIDER_CLAUDE: "Claude",
            core.PROVIDER_KIMI: "Kimi",
        }[provider]
        status = (
            ("인증 설정됨" if ok else "로그인 필요")
            if provider == core.PROVIDER_KIMI
            else ("로그인됨" if ok else "로그인 필요")
        )
        self.provider_status.configure(
            text=f"● {provider_name} {status}",
            style="Online.TLabel" if ok else "Offline.TLabel",
        )

    @staticmethod
    def _same_project_path(left: str, right: pathlib.Path) -> bool:
        try:
            return os.path.normcase(os.path.normpath(os.path.abspath(left))) == os.path.normcase(
                os.path.normpath(os.path.abspath(right))
            )
        except (OSError, TypeError, ValueError):
            return False

    def _wait_for_project_connection(
        self,
        project_dir: str | os.PathLike[str],
        *,
        timeout: float = 300.0,
        owner: ProjectSession | None = None,
    ) -> bool:
        expected_project = pathlib.Path(project_dir).expanduser().resolve()
        deadline = time.monotonic() + timeout
        runtime_owner = owner or getattr(self, "_session", None)
        has_workspace = getattr(runtime_owner, "workspace", None) is not None
        while time.monotonic() < deadline and not self.closed:
            try:
                binding = (
                    self._runtime_for(runtime_owner)
                    if has_workspace
                    else contextlib.nullcontext()
                )
                with binding:
                    config = revvy.load_config()
                    running = revvy._unreal_running(config, timeout=1.0)
                    active = revvy._project_from_config(config, "active_project")
                if running:
                    if isinstance(active, dict) and self._same_project_path(
                        str(active.get("path", "")), expected_project
                    ):
                        return True
            except (OSError, ValueError, TypeError):
                pass
            time.sleep(2.0)
        return False

    def _wait_for_unreal_connection(
        self,
        context: core.UnrealProjectContext,
        *,
        timeout: float = 300.0,
        owner: ProjectSession | None = None,
    ) -> bool:
        return self._wait_for_project_connection(
            context.project_file.parent,
            timeout=timeout,
            owner=owner,
        )

    def _build_environment_problem(self, selection: str) -> str | None:
        """Why the engine would refuse to build this project, or None when ready.

        Uses the engine's own Windows_SDK.json rules (min MSVC, banned ranges,
        min Windows SDK), so a machine with only a banned MSVC is caught here
        instead of failing the UBT build later.
        """
        try:
            project_file = core.resolve_uproject(selection)
            if not core.unreal_project_has_cpp_source(project_file.parent):
                return None
            association = core.read_engine_association(project_file)
            engine_root = core.find_unreal_engine_root(association)
            environment = core.check_unreal_build_environment(engine_root)
        except Exception:
            # Engine/project resolution failures are reported by the launch
            # worker; this gate only decides whether to offer the install.
            return None
        if environment.get("ok"):
            return None
        requirements = environment.get("requirements", {})
        toolchain = environment.get("toolchain", {})
        reasons: list[str] = []
        if not toolchain.get("ok"):
            rejected = toolchain.get("rejected") or []
            banned = [
                str(item.get("version"))
                for item in rejected
                if item.get("reason") == "banned_compiler_bug"
            ]
            if banned:
                reasons.append(
                    f"설치된 MSVC {', '.join(banned)}는 UE {association}이 금지한 "
                    "버전입니다 (알려진 컴파일러 버그)"
                )
            elif any(item.get("reason") == "below_minimum" for item in rejected):
                reasons.append(
                    f"엔진이 요구하는 MSVC {requirements.get('min_msvc')} 이상이 "
                    "설치되어 있지 않습니다"
                )
            else:
                reasons.append("C++ 빌드 도구(MSVC)가 설치되어 있지 않습니다")
        windows_sdk = environment.get("windows_sdk", {})
        if not windows_sdk.get("ok"):
            reasons.append(
                f"Windows SDK {requirements.get('min_windows_sdk')} 이상이 "
                "설치되어 있지 않습니다"
            )
        if not reasons:
            return "C++ 빌드 환경이 엔진 요구사항을 충족하지 않습니다"
        return "\n".join(reasons)

    def _maybe_offer_build_toolchain(self) -> None:
        """First-run helper: offer the VS Build Tools install to non-programmers."""
        if self.closed or self.action_running:
            return
        owner = self._session
        project_dir = owner.settings.project_dir

        def probe() -> None:
            reason = self._build_environment_problem(project_dir)
            if reason:
                self._post(
                    self._offer_build_toolchain_install,
                    None,
                    True,
                    reason,
                    project_dir,
                    session=owner,
                )

        threading.Thread(
            target=probe, name="RevvyToolchainProbe", daemon=True
        ).start()

    def _offer_build_toolchain_install(
        self,
        on_success: Callable[[], None] | None = None,
        startup: bool = False,
        reason: str | None = None,
        selection: str | None = None,
    ) -> None:
        if (
            self.closed
            or self.action_running
            or self._install_worker_running
            or self._toolchain_offer_active
            or not self._is_visible()
        ):
            return
        if startup and self._toolchain_prompt_shown:
            return
        self._toolchain_offer_active = True
        self._toolchain_prompt_shown = True
        try:
            cause = reason or "Visual Studio Build Tools가 설치되어 있지 않습니다."
            confirmed = messagebox.askyesno(
                "빌드 도구 설치",
                (
                    "이 프로젝트는 C++ 빌드가 필요합니다.\n"
                    f"{cause}\n\n"
                    "지금 필요한 빌드 도구를 자동으로 설치할까요?\n"
                    "· 관리자 권한 확인 창이 뜨면 '예'를 눌러 주세요\n"
                    "· 다운로드와 설치에 10~30분 정도 걸립니다\n\n"
                    "'아니요'를 선택해도 '빌드 + 실행'을 누르면 다시 안내해 드립니다."
                ),
                parent=self.root,
            )
            if not confirmed:
                self._set_footer(
                    "빌드 도구 설치를 건너뛰었습니다. '빌드 + 실행' 시 다시 확인합니다"
                )
                return

            installer_owner = self._session
            self._install_worker_running = True
            self._install_worker_session = installer_owner

            def task() -> dict[str, Any]:
                engine_root = None
                if selection:
                    try:
                        engine_root = core.find_unreal_engine_root(
                            core.read_engine_association(selection)
                        )
                    except Exception:
                        engine_root = None
                try:
                    ok, message = core.install_visual_cpp_build_tools(
                        progress=lambda line: self._post(
                            self._append_log, f"VS   {line}"
                        ),
                        engine_root=engine_root,
                    )
                finally:
                    if self._install_worker_session is installer_owner:
                        self._install_worker_running = False
                        self._install_worker_session = None
                if not ok:
                    raise RuntimeError(message)
                return {"message": message}

            def done(result: dict[str, Any]) -> bool:
                self._append_log(f"VS   {result['message']}")
                self._set_footer(result["message"])
                if on_success and not (self.closed or self._closing_requested):
                    on_success()
                return True

            self._run_worker("빌드 도구 설치", task, done)
        finally:
            self._toolchain_offer_active = False

    def start_engine(self, *, build: bool) -> None:
        """Dispatch the shared project buttons to the selected editor."""
        if self.engine == core.ENGINE_UNREAL:
            self.start_unreal(build=build)
            return
        if self.engine == core.ENGINE_GODOT:
            self.start_godot(run_project=build)
            return
        self._set_footer(
            "Unity 에디터 실행은 아직 지원되지 않습니다. Unity Hub에서 프로젝트를 열어 주세요.",
            error=True,
        )

    def start_godot(self, *, run_project: bool = False) -> None:
        if self.action_running or self.closed or self._closing_requested:
            return
        if self.ue_connected and not run_project:
            self._set_footer("Godot이 이미 연결되어 있습니다")
            return

        selection = self.project_var.get().strip()
        try:
            project_file = core.resolve_godot_project(selection)
            executable = core.resolve_godot_executable(
                project_file,
                configured=self.settings.godot_executable,
            )
        except core.GodotConfigurationError as exc:
            if "실행 파일" not in str(exc):
                self._set_footer(str(exc), error=True)
                return
            if messagebox.askyesno(
                "Godot 다운로드",
                (
                    "Godot 실행 파일을 찾을 수 없습니다.\n\n"
                    "최신 Godot 4 안정판을 자동으로 다운로드할까요? (약 60MB)\n"
                    "아니요를 누르면 직접 파일을 선택할 수 있습니다."
                ),
                parent=self.root,
            ):
                self._start_godot_download(project_file, run_project)
                return
            chosen = filedialog.askopenfilename(
                title="Godot 4.x 실행 파일 선택",
                initialdir=str(project_file.parent),
                filetypes=[("Godot executable", "Godot*.exe"), ("Executable", "*.exe")],
            )
            if not chosen:
                self._set_footer(str(exc), error=True)
                return
            try:
                executable = core.resolve_godot_executable(
                    project_file,
                    configured=chosen,
                    search_roots=(),
                )
            except core.GodotConfigurationError as chosen_exc:
                self._set_footer(str(chosen_exc), error=True)
                return

        if self.settings.godot_executable != str(executable):
            self.settings.godot_executable = str(executable)
            self._save_settings()
        self._start_godot_worker(
            project_file=project_file,
            executable=executable,
            run_project=run_project,
        )

    def _start_godot_worker(
        self,
        *,
        project_file: pathlib.Path,
        executable: pathlib.Path,
        run_project: bool,
    ) -> None:
        label = "Godot 게임 실행" if run_project else "Godot 에디터 실행"

        owner = self._session
        workspace = owner.workspace
        if workspace is None:
            self._set_footer("Revvy project workspace is unavailable", error=True)
            return

        def task() -> dict[str, Any]:
            with self._runtime_for(owner):
                revvy.register_project(str(project_file), include_claude=True)
                runtime_config = revvy.ensure_runtime_config()
                proxy_code = core.start_proxy_service(revvy)
            if proxy_code != revvy.EXIT_OK:
                raise RuntimeError(f"Revvy 프록시 시작 실패 (종료 코드 {proxy_code})")
            process = core.launch_godot_editor(
                project_file,
                executable=executable,
                run_project=run_project,
                env=workspace.child_env(
                    {"REVVY_EDITOR_PORT": str(runtime_config["ue_port"])}
                ),
            )
            mode = "게임" if run_project else "Editor"
            self._post(self._append_log, f"Godot {mode} 시작됨 (PID {process.pid})")
            self._post(
                self._set_footer,
                "Godot 게임 시작 중" if run_project else "Godot 시작 중 · Revvy 연결 대기",
            )
            if run_project:
                return {
                    "connected": owner.ue_connected,
                    "project": project_file.parent.name,
                    "run_project": True,
                }
            connected = self._wait_for_project_connection(
                project_file.parent,
                timeout=90.0,
                owner=owner,
            )
            return {
                "connected": connected,
                "project": project_file.parent.name,
                "run_project": False,
            }

        self._run_worker(label, task, self._godot_started)

    def _start_godot_download(
        self,
        project_file: pathlib.Path,
        run_project: bool,
    ) -> None:
        def task() -> dict[str, Any]:
            executable = core.download_godot_editor(
                progress=lambda line: self._post(
                    self._append_log, f"GODOT  {line}"
                )
            )
            return {"executable": executable}

        def done(result: dict[str, Any]) -> bool:
            executable = result["executable"]
            self.settings.godot_executable = str(executable)
            self._save_settings()
            self._append_log(f"Godot 준비 완료 · {executable}")
            if not (self.closed or self._closing_requested):
                self._start_godot_worker(
                    project_file=project_file,
                    executable=executable,
                    run_project=run_project,
                )
            return True

        self._run_worker("Godot 다운로드", task, done)

    def _godot_started(self, result: dict[str, Any]) -> bool:
        if result.get("run_project"):
            if not self._is_visible():
                return True
            self.refresh_status()
            self._set_footer(f"{result.get('project', 'Godot')} · 게임 실행됨")
            return True
        connected = bool(result.get("connected"))
        self.ue_connected = connected
        if not self._is_visible():
            return True
        self.unreal_state_var.set("MCP 연결됨" if connected else "연결 대기 중")
        self._set_controls_busy(False)
        self.refresh_status()
        if connected:
            self._set_footer(f"{result.get('project', 'Godot')} · Revvy 자동 연결됨")
            self.load_tools()
        else:
            self._set_footer(
                "Godot은 실행됐지만 Revvy 연결을 계속 확인 중입니다",
                error=True,
            )
        return True

    def _project_needs_build(self, selection: str) -> bool:
        """C++ project without its editor DLL — plain launch hits 'missing modules'."""
        try:
            project_file = core.resolve_uproject(selection)
            if not core.unreal_project_has_cpp_source(project_file.parent):
                return False
            target = core.find_editor_target(project_file)
            return not core.unreal_editor_module_built(project_file, target)
        except core.UnrealConfigurationError:
            # Resolution failures are reported by the launch worker itself.
            return False

    def _offer_engine_install(
        self,
        project_file: str,
        association: str,
        build: bool,
    ) -> None:
        """Guide a non-developer when the project's engine is not installed."""
        needed = association.strip() or "?"
        try:
            installed = [
                version for version, _root in core.list_installed_unreal_engines()
            ]
        except Exception:
            installed = []
        version_like = all(part.isdigit() for part in needed.split('.'))
        if version_like and installed and needed not in installed:
            latest = installed[0]
            choice = messagebox.askyesnocancel(
                "Unreal Engine 버전 불일치",
                (
                    f"이 프로젝트는 Unreal Engine {needed}용인데, 이 PC에는 "
                    f"{', '.join(installed[:3])}만 설치되어 있습니다.\n\n"
                    f"예: {latest}(으)로 변환해서 열기\n"
                    f"아니요: {needed} 설치 안내 보기\n"
                    "취소: 아무 작업도 하지 않기"
                ),
                parent=self.root,
            )
            if choice is None:
                self._set_footer("엔진 확인을 취소했습니다")
                return
            if choice:
                try:
                    core.set_engine_association(project_file, latest)
                except core.UnrealConfigurationError as exc:
                    self._set_footer(str(exc), error=True)
                    return
                self._append_log(
                    f"엔진 버전 {needed} → {latest} 변환 후 다시 시작합니다"
                )
                self.start_unreal(build=build)
                return
        launcher = core.epic_games_launcher_executable()
        if launcher is not None:
            message = (
                f"Unreal Engine {needed}이(가) 설치되어 있지 않습니다.\n\n"
                "Epic Games Launcher를 열까요?\n"
                "(라이브러리 → 엔진 버전 추가에서 설치)"
            )
            target = str(launcher)
        else:
            message = (
                f"Unreal Engine {needed}이(가) 설치되어 있지 않습니다.\n\n"
                "Epic Games 다운로드 페이지를 열까요?\n"
                "(Epic Games Launcher 설치 후 엔진을 설치할 수 있습니다)"
            )
            target = core.UNREAL_DOWNLOAD_URL
        if messagebox.askyesno("Unreal Engine 설치 필요", message, parent=self.root):
            try:
                os.startfile(target)  # type: ignore[attr-defined]
            except OSError as exc:
                self._set_footer(f"열기 실패: {exc}", error=True)
                return
        self._set_footer(f"Unreal Engine {needed} 설치 후 다시 '실행'을 눌러 주세요")

    def start_unreal(self, *, build: bool) -> None:
        if self.action_running or self._toolchain_probe_running:
            return
        if self.closed or self._closing_requested:
            return
        if self.ue_connected:
            self._set_footer("Unreal이 이미 연결되어 있습니다")
            return
        selection = self.project_var.get().strip()
        if not build and self._project_needs_build(selection):
            self._append_log(
                "C++ 모듈이 아직 빌드되지 않았습니다 · 빌드 + 실행으로 전환합니다"
            )
            build = True
        if build:
            # The vswhere probe spawns a subprocess; keep it off the Tk thread.
            self._toolchain_probe_running = True
            owner = self._session

            def probe() -> None:
                reason = None
                try:
                    reason = self._build_environment_problem(selection)
                finally:
                    # Clear here too: _post drops callbacks once the app is
                    # closed, which would otherwise leave the flag stuck.
                    self._toolchain_probe_running = False
                    self._post(
                        self._toolchain_gate_done,
                        reason,
                        selection,
                        session=owner,
                    )

            threading.Thread(
                target=probe, name="RevvyToolchainGate", daemon=True
            ).start()
            return
        self._start_unreal_worker(build=False, selection=selection)

    def _toolchain_gate_done(self, reason: str | None, selection: str) -> None:
        self._toolchain_probe_running = False
        if self.closed or self._closing_requested or self.action_running:
            return
        owner = self._session
        if reason:
            if not self._is_visible():
                self._append_log(
                    "Unreal build environment is not ready; focus this project to fix it"
                )
                return
            self._offer_build_toolchain_install(
                on_success=lambda: self._start_unreal_worker(
                    build=True,
                    selection=selection,
                    owner=owner,
                ),
                reason=reason,
                selection=selection,
            )
            return
        self._start_unreal_worker(build=True, selection=selection, owner=owner)

    def _start_unreal_worker(
        self,
        *,
        build: bool,
        selection: str,
        owner: ProjectSession | None = None,
    ) -> None:
        label = "Unreal 빌드 + 실행" if build else "Unreal 실행"
        owner = owner or self._session

        def task() -> dict[str, Any]:
            project_file = core.resolve_uproject(selection)
            association = core.read_engine_association(project_file)
            try:
                core.find_unreal_engine_root(association)
            except core.UnrealConfigurationError:
                return {
                    "connected": False,
                    "built": False,
                    "project": project_file.stem,
                    "engine_missing": True,
                    "project_file": str(project_file),
                    "association": association,
                    "build_requested": build,
                }
            context = core.inspect_unreal_project(selection)
            if build:
                core.ensure_unreal_build_ready()
            self._post(
                self._append_log,
                f"Unreal {context.engine_association} / {context.editor_target} / {context.project_file}",
            )
            with self._runtime_for(owner):
                revvy.register_project(str(context.project_file), include_claude=True)
                proxy_code = core.start_proxy_service(revvy)
            if proxy_code != revvy.EXIT_OK:
                raise RuntimeError(f"Revvy 프록시 시작 실패 (종료 코드 {proxy_code})")
            if build:
                command = core.build_unreal_editor_command(
                    context.engine_root,
                    context.project_file,
                    context.editor_target,
                )
                self._post(self._append_log, f"UBT  {subprocess.list2cmdline(command)}")
                build_output: list[str] = []

                def report_build_output(line: str) -> None:
                    build_output.append(line)
                    if len(build_output) > 250:
                        del build_output[:-250]
                    self._post(self._append_log, f"UBT  {line}")

                try:
                    core.run_unreal_build(
                        command,
                        output_callback=report_build_output,
                    )
                except subprocess.CalledProcessError as exc:
                    detail = core.describe_unreal_build_failure(build_output)
                    if detail:
                        raise RuntimeError(detail) from exc
                    raise RuntimeError(f"Unreal 빌드 실패 (종료 코드 {exc.returncode})") from exc
            if self.closed:
                return {"connected": False, "built": build, "project": context.project_file.stem}
            workspace = owner.workspace
            if workspace is None:
                raise ConnectionError("Revvy project workspace is unavailable")
            process = core.launch_unreal_editor(
                context.engine_root,
                context.project_file,
                env=workspace.child_env(),
            )
            self._post(self._append_log, f"Unreal Editor 시작됨 (PID {process.pid})")
            self._post(self._set_footer, "Unreal 시작 중 · Revvy 연결 대기")
            connected = self._wait_for_unreal_connection(context, owner=owner)
            return {
                "connected": connected,
                "built": build,
                "project": context.project_file.stem,
            }

        self._run_worker(label, task, self._unreal_started)

    def clean_and_regenerate_project(self) -> None:
        if self.action_running:
            return
        selection = self.project_var.get().strip()
        try:
            project_file = core.resolve_uproject(selection)
        except core.UnrealConfigurationError as exc:
            self._set_footer(str(exc), error=True)
            return
        project_root = project_file.parent
        existing = [
            name
            for name in core.UNREAL_MAINTENANCE_CACHE_DIRS
            if (project_root / name).exists()
        ]
        targets = ", ".join(existing) if existing else "삭제할 캐시 없음"
        confirmed = messagebox.askyesno(
            "Unreal 프로젝트 정리",
            (
                f"프로젝트: {project_file.name}\n\n"
                f"정리 대상: {targets}\n"
                "보존 대상: Saved, Content, Config, Source, Plugins\n\n"
                "Unreal Editor와 Rider/Visual Studio를 닫아야 합니다. "
                "정리 후 프로젝트 파일을 다시 생성할까요?"
            ),
            parent=self.root,
        )
        if not confirmed:
            self._set_footer("프로젝트 정리를 취소했습니다")
            return

        def report(message: str) -> None:
            self._post(self._append_log, f"PROJECT  {message}")
            self._post(self._set_chat_activity, message, "running")

        def task() -> list[pathlib.Path]:
            try:
                return core.clean_and_regenerate_unreal_project(
                    project_file,
                    progress_callback=report,
                )
            except Exception as exc:
                self._post(self._set_chat_activity, str(exc), "failed")
                raise

        self._set_chat_activity("프로젝트 캐시와 실행 프로세스 확인", "running")
        self._run_worker(
            "캐시 정리 + 프로젝트 파일 재생성",
            task,
            self._project_regenerated,
        )

    def _project_regenerated(self, removed: list[pathlib.Path]) -> bool:
        detail = f"캐시 {len(removed)}개 정리 · 프로젝트 파일 재생성"
        self._set_chat_activity(detail, "completed")
        self._append_log(f"PROJECT  {detail} 완료")
        return True

    def _unreal_started(self, result: dict[str, Any]) -> bool:
        if result.get("engine_missing"):
            self.ue_connected = False
            self._offer_engine_install(
                result["project_file"],
                result["association"],
                bool(result.get("build_requested")),
            )
            return True
        connected = bool(result.get("connected"))
        self.ue_connected = connected
        if not self._is_visible():
            return True
        self.unreal_state_var.set("MCP 연결됨" if connected else "연결 대기 중")
        self._set_controls_busy(False)
        self.refresh_status()
        if connected:
            self._set_footer(f"{result.get('project', 'Unreal')} · Revvy 자동 연결됨")
            self.load_tools()
        else:
            self._set_footer("Unreal은 실행됐지만 Revvy 연결을 계속 확인 중입니다", error=True)
        return True

    def start_proxy(self) -> None:
        owner = self._session

        def task() -> int:
            with self._runtime_for(owner):
                return core.start_proxy_service(revvy)

        self._run_worker(
            "프록시 시작",
            task,
            lambda code: self._proxy_action_done(code, "start"),
        )

    def restart_proxy(self) -> None:
        owner = self._session

        def task() -> int:
            with self._runtime_for(owner):
                stopped = revvy.stop_proxy(quiet=True)
                if stopped != revvy.EXIT_OK:
                    return stopped
                return core.start_proxy_service(revvy)

        self._run_worker("프록시 재시작", task, lambda code: self._proxy_action_done(code, "restart"))

    def _proxy_action_done(self, code: int, action: str) -> bool:
        if not self._is_visible():
            return True
        action_label = {"start": "시작", "restart": "재시작"}.get(action, action)
        if code != revvy.EXIT_OK:
            self._set_footer(f"프록시 {action_label} 실패 (종료 코드 {code})", error=True)
            footer_handled = True
        else:
            footer_handled = False
        self.refresh_status()
        self.refresh_logs()
        return footer_handled

    def setup_blender_bridge(self) -> None:
        project = self.project_var.get().strip()
        owner = self._session
        workspace = owner.workspace
        if workspace is None:
            self._set_footer("Revvy project workspace is unavailable", error=True)
            return
        self._run_worker(
            "Blender 브리지 연결 준비",
            lambda: core.install_blender_bridge(project, env=workspace.env),
            self._blender_setup_done,
        )

    def _blender_setup_done(self, result: dict[str, Any]) -> bool:
        self.blender_configured = True
        self._append_log(
            "Blender bridge installed: "
            f"{result.get('addon_dir')} / exchange={result.get('exchange_root')}"
        )
        if not self._is_visible():
            return True
        self.blender_state_var.set(
            f"Blender {result.get('blender_version', '')} · 실행 대기"
        )
        self._set_footer("Blender 브리지 준비 완료 · Blender를 실행하세요")
        self.refresh_status()
        self.load_tools()
        return True

    def start_blender(self) -> None:
        if self.action_running:
            return
        if self.blender_connected:
            self._set_footer("Blender 브리지가 이미 연결되어 있습니다")
            return
        if not self.blender_configured:
            self._set_footer("먼저 Blender 연결 준비를 완료하세요")
            return
        owner = self._session

        def task() -> int:
            proxy_target = self._prepare_proxy_target(owner)
            workspace = owner.workspace
            if workspace is None:
                raise ConnectionError("Revvy project workspace is unavailable")
            process = core.launch_blender(env=workspace.child_env())
            try:
                response = core.rpc_request_at_proxy(
                    proxy_target,
                    "tools/call",
                    {
                        "name": "manage_upstreams",
                        "arguments": {
                            "action": "activate",
                            "namespace": core.BLENDER_MCP_SERVER_NAME,
                        },
                    },
                    timeout=20,
                )
                result = response.get("result", {}) if isinstance(response, dict) else {}
                if isinstance(result, dict) and result.get("isError"):
                    self._post(
                        self._append_log,
                        f"Blender MCP activation failed: {core.tool_result_text(response)}",
                    )
            except (ConnectionError, OSError, TimeoutError, ValueError) as exc:
                self._post(self._append_log, f"Blender MCP activation failed: {exc}")
            return int(process.pid)

        self._run_worker("Blender 실행", task, self._blender_started)

    def _blender_started(self, pid: int) -> bool:
        self._append_log(f"Blender started (PID {pid})")
        if not self._is_visible():
            return True
        self.blender_state_var.set("Blender 시작 중 · MCP 연결 대기")
        self._set_footer("Blender 시작됨 · 브리지 연결을 확인하는 중입니다")
        self.refresh_status()
        self.load_tools()
        return True

    def _prepare_proxy_target(
        self,
        owner: ProjectSession | None = None,
    ) -> core.ProxyTarget:
        """Start and capture one session's proxy from a background worker."""
        session = owner or self._session
        workspace = session.workspace
        if workspace is None:
            raise ConnectionError(
                "Revvy project runtime changed; focus the project and try again"
            )
        # revvy.py still owns process management as module globals. The dedicated
        # binding context serializes start/capture without occupying process_lock,
        # which keeps cancel and close responsive during an eight-second cold start.
        with self._runtime_for(session):
            config = revvy.ensure_runtime_config()
            if core.start_proxy_service(revvy) != revvy.EXIT_OK:
                raise ConnectionError(
                    "Revvy proxy is unavailable. See "
                    f"{workspace.env['REVVY_PROXY_LOG']}"
                )
            return core.proxy_target(
                f"http://127.0.0.1:{config['proxy_port']}/mcp",
                workspace.config_path(),
                revvy.full_access_token(),
            )

    def load_tools(self) -> None:
        owner = self._session
        self._run_worker(
            "도구 불러오기",
            lambda: core.tool_list_from_response(
                core.rpc_request_at_proxy(
                    self._prepare_proxy_target(owner),
                    "tools/list",
                    None,
                    timeout=15,
                )
            ),
            self._apply_tools,
        )

    def _apply_tools(self, tools: list[dict[str, Any]]) -> None:
        self.tools = sorted(tools, key=lambda item: str(item.get("name", "")))
        if not self._is_visible():
            return
        self._paint_tools()

    def _paint_tools(self) -> None:
        names = [str(tool.get("name", "")) for tool in self.tools]
        self._populate_tools(self.tool_filter_var.get())
        self.direct_tool_combo.configure(values=names)
        if names and self.direct_tool_var.get() not in names:
            self.direct_tool_var.set(names[0])
        elif not names:
            self.direct_tool_var.set("")
        if hasattr(self, "tool_details"):
            self._replace_text(self.tool_details, "")

    def _tool_selected(self, _event: tk.Event | None = None) -> None:
        selected = self.tool_tree.selection()
        if not selected:
            return
        tool = self.tools[int(selected[0])]
        self._replace_text(self.tool_details, json.dumps(tool, indent=2, ensure_ascii=False))

    def _tool_to_direct(self, _event: tk.Event | None = None) -> None:
        selected = self.tool_tree.selection()
        if not selected:
            return
        tool = self.tools[int(selected[0])]
        self.direct_tool_var.set(str(tool.get("name", "")))
        self.notebook.select(self.direct_tab)

    def run_direct_call(self) -> None:
        name = self.direct_tool_var.get().strip()
        if not name:
            self._set_footer("실행할 도구를 선택하세요", error=True)
            return
        self.direct_args_text = self.direct_args.get("1.0", "end-1c")
        try:
            arguments = core.parse_tool_arguments(self.direct_args_text)
        except (ValueError, json.JSONDecodeError) as exc:
            self._set_footer(str(exc), error=True)
            return
        owner = self._session

        def task() -> str:
            response = core.rpc_request_at_proxy(
                self._prepare_proxy_target(owner),
                "tools/call",
                {"name": name, "arguments": arguments},
                timeout=125,
            )
            return core.tool_result_text(response)

        self._run_worker(
            f"{name} 실행",
            task,
            self._apply_direct_output,
        )

    def _send_shortcut(self, _event: tk.Event) -> str:
        self.send_chat()
        return "break"

    def _newline_shortcut(self, _event: tk.Event) -> str:
        self.chat_input.insert(tk.INSERT, "\n")
        return "break"

    def send_chat(self) -> None:
        if self._session.busy:
            return
        message = self.chat_input.get("1.0", tk.END).strip()
        pending_attachments = list(self.attachments)
        if not message and not pending_attachments:
            return
        if not message:
            message = "첨부한 이미지를 분석해줘."
        project = pathlib.Path(self.project_var.get().strip()).expanduser()
        if not project.is_dir():
            self._set_footer(f"프로젝트 폴더를 찾을 수 없습니다: {project}", error=True)
            return
        project = project.resolve()
        self._switch_history_project(str(project))
        model = self.model_var.get().strip() or "default"
        provider = self.provider_key
        if (
            provider == core.PROVIDER_KIMI
            and self.permission_mode == core.PERMISSION_SAFE
        ):
            self._set_footer(
                "Kimi 비대화형 모드는 내장 파일/셸 도구를 자동 승인하므로 Safe에서 실행할 수 없습니다. "
                "권한을 Full access로 변경한 뒤 다시 보내세요.",
                error=True,
            )
            return
        owner = self._session
        workspace = owner.workspace
        if workspace is None:
            self._set_footer("Revvy project workspace is unavailable", error=True)
            return
        # Everything that depends on the proxy endpoint is built in the worker
        # after _prepare_proxy_target has captured this project's exact identity.
        # That prevents a project switch during cold start from splitting the
        # provider URL and the upstream-activation target across two projects.
        permission_mode = self.permission_mode
        reasoning_effort = self.reasoning_effort
        executable = core.resolve_executable(
            {
                core.PROVIDER_CODEX: "codex",
                core.PROVIDER_CLAUDE: "claude",
                core.PROVIDER_KIMI: "kimi",
            }[provider]
        )
        session_key = core.session_key(
            provider,
            model,
            str(project),
            permission_mode=permission_mode,
        )
        if provider == core.PROVIDER_CODEX:
            session_id = (
                self.settings.codex_thread_id
                if self.settings.codex_session_key == session_key
                else ""
            )
        elif provider == core.PROVIDER_CLAUDE:
            session_id = (
                self.settings.claude_session_id
                if self.settings.claude_session_key == session_key
                else ""
            )
        else:
            session_id = (
                self.settings.kimi_session_id
                if self.settings.kimi_session_key == session_key
                else ""
            )
        try:
            staged_attachments = core.stage_image_attachments(
                pending_attachments,
                env=workspace.env,
            )
        except OSError as exc:
            self._set_footer(f"이미지 첨부 준비 실패: {exc}", error=True)
            return
        prompt = (
            core.build_turn_prompt(message, staged_attachments, provider=provider)
            if session_id
            else core.build_chat_prompt(
                message,
                self.history,
                image_paths=staged_attachments,
                provider=provider,
            )
        )

        def build_invocation(
            target: core.ProxyTarget,
        ) -> tuple[list[str], dict[str, str]]:
            environment = core.provider_subprocess_environment(
                permission_mode,
                target.token,
                environ=workspace.env,
            )
            if provider == core.PROVIDER_KIMI:
                environment = core.kimi_subprocess_environment(environment)
                core.ensure_kimi_mcp_config(
                    project,
                    target.mcp_url,
                    permission_mode=permission_mode,
                )
            if provider == core.PROVIDER_CODEX:
                command = (
                    core.build_codex_resume_command(
                        executable,
                        model,
                        session_id,
                        target.mcp_url,
                        image_paths=staged_attachments,
                        reasoning_effort=reasoning_effort,
                        permission_mode=permission_mode,
                    )
                    if session_id
                    else core.build_codex_command(
                        executable,
                        model,
                        str(project),
                        target.mcp_url,
                        image_paths=staged_attachments,
                        reasoning_effort=reasoning_effort,
                        permission_mode=permission_mode,
                    )
                )
            elif provider == core.PROVIDER_CLAUDE:
                command = core.build_claude_command(
                    executable,
                    model,
                    str(project),
                    target.mcp_url,
                    session_id=session_id,
                    image_paths=staged_attachments,
                    reasoning_effort=reasoning_effort,
                    permission_mode=permission_mode,
                )
            else:
                command = core.build_kimi_command(
                    executable,
                    model,
                    str(project),
                    session_id=session_id,
                    image_paths=staged_attachments,
                    permission_mode=permission_mode,
                )
                command.append(core.limit_kimi_prompt(prompt))
            return command, environment

        self.active_attachment_paths = staged_attachments

        self.chat_input.delete("1.0", tk.END)
        self.chat_draft = ""
        self._update_chat_input_hint()
        display_message = message
        if pending_attachments:
            names = "  ·  ".join(path.name for path in pending_attachments)
            display_message += f"\n\n이미지  {names}"
        self._last_turn_notice = None
        self.pending_reply = ""
        self.turn_edits = []
        self._append_chat("You", display_message)
        self._append_chat("Revvy", "")
        self.history.append(("user", message))
        self._persist_chat_history()
        self.cancel_requested.clear()
        now = time.monotonic()
        self.chat_turn_started_at = now
        self.chat_last_event_at = now
        self.chat_active_provider = provider
        self.action_running = True
        self._sync_composer_to_visible()
        self._reset_chat_activities()
        self._set_chat_activity(
            core.revvy_thinking_status(),
            "running",
            "turn",
        )
        session_note = f" resume={session_id}" if session_id else " new-session"
        self._append_log(
            f"Starting {provider} model={model} effort={reasoning_effort or 'default'} "
            f"permission={permission_mode} cwd={project} MCP=project-bound"
            f"{session_note} images={len(staged_attachments)}"
        )

        threading.Thread(
            target=self._chat_worker,
            args=(
                provider,
                build_invocation,
                prompt,
                str(project),
                session_key,
                None,
                owner,
                core.upstream_namespaces_for_prompt(message, self.history),
                None,
            ),
            name="Revvy-chat",
            daemon=True,
        ).start()

    def _chat_worker(
        self,
        provider: str,
        command: list[str]
        | Callable[[core.ProxyTarget], tuple[list[str], dict[str, str]]],
        prompt: str,
        project: str,
        session_key: str,
        provider_environment: dict[str, str] | None = None,
        owner: ProjectSession | None = None,
        upstream_namespaces: Sequence[str] = (),
        proxy_target: core.ProxyTarget | None = None,
    ) -> None:
        # Every callback this worker queues is stamped with the project that
        # started the turn. Without it a reply finishing in one project would
        # land on whichever project the user had switched to meanwhile.
        # ``self`` fallback keeps the isolated worker harness usable; real app
        # calls always supply/capture a ProjectSession.
        session = owner or getattr(self, "_session", None) or self
        post = functools.partial(self._post, session=session)
        answer_parts: list[str] = []
        snapshot = ""
        error = ""
        stderr_lines: list[str] = []
        activity_labels: dict[str, str] = {}
        session_id = ""
        known_kimi_sessions: set[str] = set()
        return_code = -1
        terminal_seen = False
        process: subprocess.Popen[str] | None = None
        try:
            if self.closed:
                return
            if proxy_target is None:
                proxy_target = self._prepare_proxy_target(session)
            if session.cancel_requested.is_set():
                raise InterruptedError("요청이 실행 전에 중지되었습니다.")
            if callable(command):
                command, provider_environment = command(proxy_target)
            if provider == core.PROVIDER_KIMI and "--session" in command:
                session_index = command.index("--session") + 1
                if session_index < len(command):
                    session_id = str(command[session_index]).strip()
            if provider == core.PROVIDER_KIMI:
                known_kimi_sessions = set(
                    core.kimi_session_ids_for_project(
                        project, provider_environment
                    )
                )
            for namespace in dict.fromkeys(upstream_namespaces):
                if session.cancel_requested.is_set():
                    raise InterruptedError("요청이 실행 전에 중지되었습니다.")
                if proxy_target is None:
                    raise ConnectionError(
                        "Revvy proxy target was not captured for upstream activation"
                    )
                activation = core.rpc_request_at_proxy(
                    proxy_target,
                    "tools/call",
                    {
                        "name": "manage_upstreams",
                        "arguments": {
                            "action": "activate",
                            "namespace": namespace,
                        },
                    },
                    timeout=20,
                )
                if session.cancel_requested.is_set():
                    raise InterruptedError("요청이 실행 전에 중지되었습니다.")
                activation_result = (
                    activation.get("result") if isinstance(activation, dict) else None
                )
                activation_ok = (
                    isinstance(activation, dict)
                    and "error" not in activation
                    and isinstance(activation_result, dict)
                    and activation_result.get("isError") is False
                )
                if not activation_ok:
                    try:
                        detail = core.tool_result_text(activation)
                    except ValueError as exc:
                        detail = str(exc)
                    raise ConnectionError(
                        f"External MCP activation failed ({namespace}): {detail}"
                    )
                else:
                    post(
                        self._append_log,
                        f"External MCP activated before provider start: {namespace}",
                    )
            if self.closed:
                return
            if session.cancel_requested.is_set():
                raise InterruptedError("요청이 실행 전에 중지되었습니다.")
            process = subprocess.Popen(
                command,
                cwd=project,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                errors="replace",
                bufsize=1,
                env=provider_environment,
                creationflags=core.process_creation_flags(),
                start_new_session=os.name != "nt",
            )
            if not attach_process_tree(process):
                post(
                    self._append_log,
                    "Provider tree ownership could not be attached; cancellation will use PID fallback",
                )
            stop_after_spawn = False
            with self.process_lock:
                session.active_process = process
                canceled_after_spawn = (
                    self.closed or session.cancel_requested.is_set()
                )
                if (
                    canceled_after_spawn
                    and getattr(session, "_cancel_process", None) is not process
                ):
                    session._cancel_process = process
                    stop_after_spawn = True
            if canceled_after_spawn:
                if stop_after_spawn:
                    self._stop_provider_process(process, session)
                raise InterruptedError("요청이 실행 전에 중지되었습니다.")
            post(self._mark_chat_event)
            post(self._mark_attachments_sent)
            if provider == core.PROVIDER_KIMI and not session_id:
                for attempt in range(11):
                    session_id = core.latest_kimi_session_id(
                        project,
                        known_kimi_sessions,
                        provider_environment,
                    )
                    if session_id:
                        post(
                            self._remember_provider_session,
                            provider,
                            session_id,
                            session_key,
                        )
                        break
                    if attempt < 10:
                        time.sleep(0.05)
            assert process.stdin is not None
            stop_before_io = False
            with self.process_lock:
                canceled_before_io = (
                    self.closed or session.cancel_requested.is_set()
                )
                if canceled_before_io:
                    if getattr(session, "_cancel_process", None) is not process:
                        session._cancel_process = process
                        stop_before_io = True
                else:
                    # cancel_chat sets its event under the same lock. Therefore
                    # either this write wins before cancellation is accepted, or
                    # cancellation wins and no provider stdin is touched.
                    if provider != core.PROVIDER_KIMI:
                        process.stdin.write(prompt)
                    process.stdin.close()
            if canceled_before_io:
                if stop_before_io:
                    self._stop_provider_process(process, session)
                raise InterruptedError("요청이 실행 전에 중지되었습니다.")
            assert process.stdout is not None

            def read_stderr() -> None:
                assert process.stderr is not None
                for stderr_line in process.stderr:
                    clean = core.sanitize_provider_log(stderr_line.rstrip())
                    if clean:
                        stderr_lines.append(clean)
                        if len(stderr_lines) > 80:
                            del stderr_lines[:-80]
                        post(self._mark_chat_event)
                        post(self._append_log, f"stderr: {clean}")

            stderr_thread = threading.Thread(
                target=read_stderr,
                name="Revvy-provider-stderr",
                daemon=True,
            )
            stderr_thread.start()
            for line in process.stdout:
                post(self._mark_chat_event)
                update = core.parse_stream_event(provider, line)
                if update.thread_id:
                    session_id = update.thread_id
                    post(
                        self._remember_provider_session,
                        provider,
                        session_id,
                        session_key,
                    )
                if update.log:
                    post(self._append_log, update.log)
                if update.tool_name:
                    preview = core_diff.diff_from_tool_call(
                        update.tool_name, update.tool_input
                    )
                    if preview is not None:
                        post(self._record_edit, preview)
                if update.activity:
                    activity = update.activity
                    if update.activity_id:
                        if update.activity_state == "running":
                            activity_labels[update.activity_id] = activity
                        elif update.activity_id in activity_labels and activity in {
                            "도구 실행",
                            "작업 실행",
                        }:
                            activity = activity_labels[update.activity_id]
                        if update.activity_state in {"completed", "failed"}:
                            activity_labels.pop(update.activity_id, None)
                    post(
                        self._set_chat_activity,
                        activity,
                        update.activity_state or "running",
                        update.activity_id or ("turn" if update.done or activity == "요청 분석" else ""),
                    )
                if update.done:
                    terminal_seen = True
                if update.error:
                    error = update.error
                    post(self._append_log, f"Provider error: {update.error}")
                if update.text:
                    if update.text_kind == "snapshot":
                        snapshot = update.text
                    elif provider == core.PROVIDER_CLAUDE and update.text_kind == "delta":
                        continue
                    elif update.text_kind == "message":
                        separator = "\n\n" if answer_parts else ""
                        fragment = separator + update.text
                        answer_parts.append(fragment)
                        post(self._append_chat_fragment, fragment)
                    elif update.text_kind == "final" and answer_parts:
                        pass
                    else:
                        answer_parts.append(update.text)
                        post(self._append_chat_fragment, update.text)
            return_code = process.wait()
            stderr_thread.join(timeout=2)
            if not answer_parts and snapshot:
                answer_parts.append(snapshot)
                post(self._append_chat_fragment, snapshot)
            if provider == core.PROVIDER_KIMI and not session_id:
                session_id = core.latest_kimi_session_id(
                    project,
                    known_kimi_sessions,
                    provider_environment,
                )
                if session_id:
                    post(
                        self._remember_provider_session,
                        provider,
                        session_id,
                        session_key,
                    )
            if return_code != 0 and not error and not session.cancel_requested.is_set():
                detail = stderr_lines[-1] if stderr_lines else ""
                error = f"{core.PROVIDER_LABELS[provider]} exited with code {return_code}."
                if detail:
                    error += f" {detail}"
            elif return_code == 0 and not terminal_seen:
                post(self._append_log, "Provider stream ended without a terminal event")
                error = "응답이 완료 신호 없이 끝났어요. 로그를 확인해 주세요."
        except Exception as exc:
            error = (
                f"{core.PROVIDER_LABELS.get(provider, provider)} 처리 중 오류: {exc}"
            )
            post(self._append_log, error)
            process_alive = False
            if process is not None:
                try:
                    process_alive = process.poll() is None
                except Exception:
                    process_alive = True
            stop_process = False
            if process is not None and process_alive:
                with self.process_lock:
                    # cancel_chat may already have handed this exact process to a
                    # stopper thread. Do not race a second signal/taskkill against
                    # it from the worker's exception path.
                    if getattr(session, "_cancel_process", None) is not process:
                        session._cancel_process = process
                        stop_process = True
            if stop_process and process is not None:
                self._stop_provider_process(process, session)
        finally:
            release_tree = False
            with self.process_lock:
                if session.active_process is process:
                    session.active_process = None
                if (
                    process is not None
                    and getattr(session, "_cancel_process", None) is not process
                ):
                    release_tree = True
            if release_tree and process is not None:
                release_process_tree(process)
        post(
            self._chat_finished,
            "".join(answer_parts),
            error,
            session.cancel_requested.is_set(),
            return_code,
            provider,
            session_id,
            session_key,
        )

    def _mark_attachments_sent(self) -> None:
        self._clear_pending_attachments()

    def _remember_provider_session(
        self,
        provider: str,
        session_id: str,
        session_key: str,
    ) -> None:
        if not session_id or not session_key:
            return
        if provider == core.PROVIDER_CODEX:
            current = (
                self.settings.codex_thread_id,
                self.settings.codex_session_key,
            )
            updated = (session_id, session_key)
            self.settings.codex_thread_id, self.settings.codex_session_key = updated
        elif provider == core.PROVIDER_CLAUDE:
            current = (
                self.settings.claude_session_id,
                self.settings.claude_session_key,
            )
            updated = (session_id, session_key)
            self.settings.claude_session_id, self.settings.claude_session_key = updated
        else:
            current = (
                self.settings.kimi_session_id,
                self.settings.kimi_session_key,
            )
            updated = (session_id, session_key)
            self.settings.kimi_session_id, self.settings.kimi_session_key = updated
        if current != updated:
            self._save_settings()

    def _cleanup_active_attachments(self) -> None:
        staged = self.active_attachment_paths
        self.active_attachment_paths = []
        if not staged:
            return
        try:
            core.cleanup_staged_attachments(staged)
        except OSError as exc:
            self._append_log(f"Attachment cleanup failed: {exc}")

    def _chat_finished(
        self,
        answer: str,
        error: str,
        canceled: bool,
        return_code: int,
        provider: str,
        session_id: str,
        session_key: str,
    ) -> None:
        self._cleanup_active_attachments()
        self.action_running = False
        # The composer belongs to the project on screen. A turn finishing in a
        # background project must not re-enable Send for a project that is still
        # working - the guard in send_chat would then be the only thing left
        # between the user and a second concurrent turn on one editor.
        self._sync_composer_to_visible()
        # The finished answer goes to history; the streaming buffer has served
        # its purpose and would otherwise be replayed a second time underneath it.
        self.pending_reply = ""
        # A whitespace-only reply is truthy but _clean_chat_messages drops it on
        # persist, so it used to leave two consecutive user turns in the restored
        # transcript and desync role alternation for build_chat_prompt.
        if answer.strip():
            self.history.append(("assistant", answer))
        self._persist_chat_history()
        if session_id:
            self._remember_provider_session(provider, session_id, session_key)
        if canceled:
            self._finish_running_activities("canceled")
            # notice, not error: stopping on purpose is not a failure, and the
            # activity card right below already says "잠깐 멈춤" in warning.
            self._append_chat(
                "Revvy",
                "요청을 멈췄어요. 언제든 다시 불러주세요!",
                notice=True,
                label=False,
            )
            self._remember_turn_notice("요청을 멈췄어요. 언제든 다시 불러주세요!", notice=True)
            self._set_chat_activity("요청을 안전하게 멈췄어요", "canceled", "turn")
            self._set_footer("요청을 멈췄어요")
        elif error:
            self._finish_running_activities("failed")
            self._append_chat("Revvy", str(error), error=True, label=False)
            self._remember_turn_notice(str(error), notice=False)
            self._set_chat_activity(error, "failed", "turn")
            self._set_footer(error, error=True)
        else:
            self._finish_running_activities("completed")
            self._last_turn_notice = None
            completed_steps = sum(
                item["state"] == "completed"
                for item in self.activity_entries
                if item["key"] != "turn"
            )
            self._set_chat_activity(
                core.revvy_turn_status("completed", count=completed_steps),
                "completed",
                "turn",
            )
            failed_steps = sum(item["state"] == "failed" for item in self.activity_entries)
            if failed_steps:
                self._set_footer(
                    f"레비 응답 완료 · {completed_steps}단계 완료 · {failed_steps}단계 실패"
                )
            else:
                self._set_footer(core.revvy_turn_status("completed", count=completed_steps))
        self.chat_active_provider = ""
        self.chat_turn_started_at = 0.0
        self.chat_last_event_at = 0.0
        if getattr(self, "_closing_requested", False):
            self.root.after(0, self._wait_for_close)
        else:
            self.refresh_status()

    def new_chat(self) -> None:
        if self._session.busy:
            return
        if self.provider_key == core.PROVIDER_CODEX:
            self.settings.codex_thread_id = ""
            self.settings.codex_session_key = ""
        elif self.provider_key == core.PROVIDER_CLAUDE:
            self.settings.claude_session_id = ""
            self.settings.claude_session_key = ""
        else:
            self.settings.kimi_session_id = ""
            self.settings.kimi_session_key = ""
        self.history.clear()
        self._last_turn_notice = None
        if not self.smoke_test:
            try:
                core.clear_chat_history(
                    self._history_project_dir,
                    self.workspace.chat_history_path()
                    if self.workspace is not None
                    else None,
                )
            except OSError as exc:
                self._append_log(f"Could not clear chat history: {exc}")
        self._clear_pending_attachments()
        self._replace_text(self.chat_output, "")
        self._show_empty_chat()
        self._reset_chat_activities()
        self._save_settings()
        self._set_footer("새 대화를 시작했습니다")

    def cancel_chat(self, session: ProjectSession | None = None) -> None:
        owner = session or self._session
        process: subprocess.Popen[str] | None
        with self.process_lock:
            was_requested = owner.cancel_requested.is_set()
            owner.cancel_requested.set()
            process = owner.active_process
            if process is None or owner._cancel_process is process:
                process = None
            else:
                owner._cancel_process = process
        if not was_requested and owner.action_running:
            with self._acting_on(owner):
                self._set_chat_activity("요청 안전하게 중지 중", "running", "cancel")
        if process is None:
            return
        threading.Thread(
            target=self._stop_provider_process,
            args=(process, owner),
            name="Revvy-cancel",
            daemon=True,
        ).start()

    def _stop_provider_process(
        self,
        process: subprocess.Popen[str],
        session: ProjectSession | None = None,
    ) -> None:
        owner = session or getattr(self, "_session", None) or self
        issues: list[str] = []

        def has_exited() -> bool:
            try:
                return process.poll() is not None
            except Exception:
                return False

        def wait_for_exit(timeout: float) -> bool:
            deadline = time.monotonic() + max(0.0, timeout)
            while time.monotonic() < deadline:
                if has_exited():
                    return True
                time.sleep(0.05)
            # The final poll is the readback that gates ownership release.
            return has_exited()

        exited = has_exited()
        tree_verified = False
        # Always terminate the durable job/group even if the root already
        # exited: that is precisely when a PID-only tree walk loses orphaned
        # descendants launched by a short-lived bootloader.
        if os.name == "nt" and not exited:
            try:
                process.send_signal(signal.CTRL_BREAK_EVENT)
            except (OSError, ValueError):
                pass
        try:
            tree_verified = bool(
                terminate_attached_process_tree(process, timeout=8.0)
            )
        except Exception as exc:
            issues.append(f"process-tree cleanup failed: {exc}")
            tree_verified = False
        if not tree_verified and not issues:
            issues.append("process-tree cleanup could not be verified")

        if not exited:
            exited = wait_for_exit(1.0)
            if not exited:
                kill_error = ""
                try:
                    # Last-resort parent cleanup. A failed tree verification is
                    # still reported below so this is never presented as proof
                    # that descendants were removed.
                    process.kill()
                except (OSError, ValueError) as exc:
                    kill_error = str(exc)
                exited = wait_for_exit(1.0)
                if not exited and kill_error:
                    issues.append(f"provider kill failed: {kill_error}")

        if not exited:
            issues.append("provider process is still running after the stop timeout")

        if issues:
            self._post(
                self._append_log,
                "Provider stop warning: " + "; ".join(dict.fromkeys(issues)),
                session=owner,
            )

        # Release the cancellation owner only after the bounded final readback.
        # On an unkillable process the warning above preserves the failure
        # evidence while this bounded release prevents a permanently frozen UI.
        released = False
        with self.process_lock:
            if getattr(owner, "_cancel_process", None) is process:
                owner._cancel_process = None
                released = True
        sync_composer = getattr(self, "_sync_composer_to_visible", None)
        if released and callable(sync_composer):
            self._post(sync_composer, session=owner)

    def refresh_logs(self) -> None:
        workspace = self.workspace
        if workspace is None:
            return
        log_path = pathlib.Path(workspace.env["REVVY_PROXY_LOG"])
        self._run_worker(
            "로그 새로고침",
            lambda: core.tail_text(log_path),
            self._apply_log_snapshot,
            exclusive=False,
        )

    def open_state_folder(self) -> None:
        workspace = self.workspace
        folder = workspace.state_root if workspace is not None else core.state_dir()
        folder.mkdir(parents=True, exist_ok=True)
        try:
            if os.name == "nt":
                os.startfile(folder)  # type: ignore[attr-defined]
            elif sys.platform == "darwin":
                subprocess.Popen(["open", str(folder)])
            else:
                subprocess.Popen(["xdg-open", str(folder)])
        except OSError as exc:
            self._set_footer(str(exc), error=True)

    def _tab_changed(self, _event: tk.Event | None = None) -> None:
        selected = self.notebook.index(self.notebook.select())
        self._select_tab(selected)
        if self.notebook.select() == str(self.logs_tab):
            self.refresh_logs()

    def close(self) -> None:
        if self.closed or self._closing_requested:
            return
        with self.process_lock:
            busy_sessions = [session for session in self.sessions.values() if session.busy]
            installer_owner = (
                getattr(self, "_install_worker_session", None) or self._session
                if self._install_worker_running
                else None
            )
            # The elevated installer is independent and may outlive the shell.
            # That exception belongs only to the session that launched it; every
            # other session still has to finish provider/process cleanup.
            detached_installer = (
                installer_owner
                if installer_owner is not None
                and installer_owner.active_process is None
                and installer_owner._cancel_process is None
                else None
            )
            blocking_sessions = [
                session for session in busy_sessions if session is not detached_installer
            ]
        if detached_installer is not None and not blocking_sessions:
            # The elevated installer is an independent process and the worker
            # thread is a daemon, so quitting the app does not abort the install.
            if not messagebox.askyesno(
                "Revvy 종료",
                "Visual Studio Build Tools 설치가 진행 중입니다.\n"
                "설치는 백그라운드에서 계속되며, 완료 후 '빌드 + 실행'을 "
                "다시 눌러 주세요.\n\n지금 종료할까요?",
                parent=self.root,
            ):
                return
            self._finalize_close()
            return
        if blocking_sessions:
            if not messagebox.askyesno(
                "Revvy 종료",
                "Kimi/AI 작업이 아직 진행 중입니다.\n"
                "세션을 저장하고 작업을 안전하게 중지한 뒤 종료할까요?",
                parent=self.root,
            ):
                return
            self._closing_requested = True
            self._persist_all_sessions()
            for session in blocking_sessions:
                self.cancel_chat(session)
            self.root.after(100, self._wait_for_close)
            return
        self._finalize_close()

    def _wait_for_close(self) -> None:
        if self.closed:
            return
        with self.process_lock:
            installer_owner = (
                getattr(self, "_install_worker_session", None) or self._session
                if self._install_worker_running
                else None
            )
            detached_installer = (
                installer_owner
                if installer_owner is not None
                and installer_owner.active_process is None
                and installer_owner._cancel_process is None
                else None
            )
            busy = any(
                session.busy and session is not detached_installer
                for session in self.sessions.values()
            )
        if busy:
            self.root.after(100, self._wait_for_close)
            return
        self._finalize_close()

    def _persist_all_sessions(self) -> None:
        """Persist each open project's own settings/history during shutdown."""

        previous = self._session
        try:
            # Pull shared widget values only into the project actually on screen.
            # Background sessions already own their model/project values; reading
            # B's widgets while acting on A would corrupt A at shutdown.
            visible = self.__dict__.get("_visible", previous)
            with self._acting_on(visible):
                self._save_settings()
            for session in list(self.sessions.values()):
                with self._acting_on(session):
                    self._persist_chat_history()
                    if session is not visible:
                        if session.workspace is not None:
                            session.settings.project_dir = str(
                                session.workspace.project_dir
                            )
                            core.save_settings(
                                session.settings,
                                session.workspace.settings_path(),
                            )
        finally:
            self._session = previous

    def _finalize_close(self) -> None:
        if self.closed:
            return
        self._persist_all_sessions()
        self.closed = True
        for session in list(self.sessions.values()):
            with self._acting_on(session):
                self._cleanup_active_attachments()
        self.root.destroy()


_forward_session_fields()


def main() -> int:
    smoke_test = "--smoke-test" in sys.argv
    root = TkinterDnD.Tk() if TkinterDnD is not None else tk.Tk()
    app = RevvyApp(root, smoke_test=smoke_test)
    startup_attachments = _startup_attachment_args(sys.argv[1:])
    if startup_attachments:
        app._add_image_paths(startup_attachments)
    if smoke_test:
        root.update_idletasks()
        root.update()
        app.close()
        return 0
    root.mainloop()
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        try:
            root = tk.Tk()
            root.withdraw()
            messagebox.showerror("Revvy", str(exc))
            root.destroy()
        except Exception:
            pass
        raise
