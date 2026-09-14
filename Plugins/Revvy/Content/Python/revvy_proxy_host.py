"""PyInstaller entry point for the windowless Revvy proxy companion."""

from __future__ import annotations

# These imports mirror revvy-proxy.py. The proxy script is bundled as data and
# executed with runpy, so PyInstaller cannot discover its dependencies itself.
import contextlib
import datetime
import http.server
import json
import os
import pathlib
import runpy
import signal
import socket
import sys
import threading
import time
import traceback
import urllib.error
import urllib.request

import PIL.Image
import PIL.ImageChops
import PIL.ImageStat

import revvy_mcp_upstreams  # PyInstaller analysis for the data-loaded proxy script.


def _packaged_project_state_dir() -> pathlib.Path | None:
    anchor = pathlib.Path(sys.executable if getattr(sys, "frozen", False) else __file__)
    try:
        location = anchor.resolve()
    except OSError:
        return None
    for candidate in (location.parent, *location.parent.parents):
        try:
            if (
                any(candidate.glob("*.uproject"))
                or (candidate / "project.godot").is_file()
                or (
                    (candidate / "Assets").is_dir()
                    and (candidate / "ProjectSettings").is_dir()
                )
            ):
                return candidate / ".revvy"
        except OSError:
            continue
    return None


def proxy_log_path(env: dict[str, str] | None = None) -> pathlib.Path:
    """Resolve the durable log used by the windowless packaged companion."""
    values = os.environ if env is None else env
    configured_log = values.get("REVVY_PROXY_LOG")
    if configured_log:
        return pathlib.Path(configured_log).expanduser()
    configured_state = values.get("REVVY_STATE_DIR")
    if configured_state:
        return pathlib.Path(configured_state).expanduser() / "proxy.log"
    configured_proxy = values.get("REVVY_CONFIG")
    if configured_proxy:
        return pathlib.Path(configured_proxy).expanduser().parent / "proxy.log"
    if env is None:
        project_state = _packaged_project_state_dir()
        if project_state is not None:
            return project_state / "proxy.log"
    base = values.get("APPDATA") or values.get("XDG_STATE_HOME")
    if base:
        return pathlib.Path(base).expanduser() / "Revvy" / "proxy.log"
    return pathlib.Path.home() / ".local" / "state" / "Revvy" / "proxy.log"


@contextlib.contextmanager
def windowless_log_streams():
    """Give a PyInstaller windowed process durable stdout/stderr streams."""
    if sys.stdout is not None and sys.stderr is not None:
        yield
        return

    log_path = proxy_log_path()
    log_path.parent.mkdir(parents=True, exist_ok=True)
    original_stdout = sys.stdout
    original_stderr = sys.stderr
    with log_path.open(
        "a",
        encoding="utf-8",
        errors="replace",
        buffering=1,
    ) as log_handle:
        if sys.stdout is None:
            sys.stdout = log_handle
        if sys.stderr is None:
            sys.stderr = log_handle
        try:
            yield
        finally:
            sys.stdout = original_stdout
            sys.stderr = original_stderr


def proxy_script_path() -> pathlib.Path:
    if getattr(sys, "frozen", False) and hasattr(sys, "_MEIPASS"):
        return pathlib.Path(sys._MEIPASS) / "Content" / "Python" / "revvy-proxy.py"
    return pathlib.Path(__file__).resolve().with_name("revvy-proxy.py")


def main() -> int:
    with windowless_log_streams():
        try:
            script = proxy_script_path()
            if not script.is_file():
                raise FileNotFoundError(f"Revvy proxy script not found: {script}")
            if str(script.parent) not in sys.path:
                sys.path.insert(0, str(script.parent))
            runpy.run_path(str(script), run_name="__main__")
        except Exception:
            traceback.print_exc()
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
