#!/usr/bin/env python3
"""Standalone command-line client and process manager for Revvy."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import re
import secrets
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
from typing import Any

from revvy_secure_store import (
    SecureStoreError,
    ensure_bridge_hmac_key,
    retire_legacy_ue_backend_token,
    scrub_legacy_editor_api_keys,
)
from revvy_processes import terminate_process_tree


FULL_ACCESS_TOKEN_ENV = "REVVY_FULL_ACCESS_TOKEN"
MIN_FULL_ACCESS_TOKEN_LENGTH = 32
DEFAULT_PROXY_PORT = 8089
DEFAULT_EDITOR_PORT = 8088
PROJECT_EDITOR_PORT_BASE = 12000
PROJECT_PORT_PAIR_COUNT = 15000
PROJECT_PORT_PROBE_ATTEMPTS = 256
# A supplied token shorter than this is refused by the proxy too, so honouring it
# here would only produce a mismatch that looks like a permission bug.
_SUPPLIED_FULL_ACCESS_TOKEN = os.environ.get(FULL_ACCESS_TOKEN_ENV) or ""
_PROCESS_FULL_ACCESS_TOKEN = (
    _SUPPLIED_FULL_ACCESS_TOKEN
    if len(_SUPPLIED_FULL_ACCESS_TOKEN) >= MIN_FULL_ACCESS_TOKEN_LENGTH
    else secrets.token_urlsafe(32)
)
os.environ[FULL_ACCESS_TOKEN_ENV] = _PROCESS_FULL_ACCESS_TOKEN


if sys.stdout is not None and hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
if sys.stderr is not None and hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")


EXIT_OK = 0
EXIT_USAGE = 2
EXIT_PROXY_UNAVAILABLE = 3
EXIT_UNREAL_UNAVAILABLE = 4

SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
PLUGIN_DIR = SCRIPT_DIR.parent.parent
PROXY_SCRIPT = SCRIPT_DIR / "revvy-proxy.py"
PROJECT_STATE_DIRNAME = ".revvy"


def _same_runtime_path(left: object, right: object) -> bool:
    if not left or not right:
        return False
    try:
        return os.path.normcase(os.path.abspath(os.fspath(left))) == os.path.normcase(
            os.path.abspath(os.fspath(right))
        )
    except (OSError, TypeError, ValueError):
        return False


def _is_supported_project_root(candidate: pathlib.Path) -> bool:
    """Whether ``candidate`` is a project Revvy can bind to independently."""
    try:
        return (
            any(candidate.glob("*.uproject"))
            or (candidate / "project.godot").is_file()
            or (
                (candidate / "Assets").is_dir()
                and (candidate / "ProjectSettings").is_dir()
            )
        )
    except OSError:
        return False


def _project_engine(project_dir: pathlib.Path) -> str:
    """Return the bridge identity implied by one supported project root."""
    try:
        if any(project_dir.glob("*.uproject")):
            return "unreal"
        if (project_dir / "project.godot").is_file():
            return "godot"
        if (project_dir / "Assets").is_dir() and (
            project_dir / "ProjectSettings"
        ).is_dir():
            return "unity"
    except OSError:
        pass
    return ""


def _nearest_project_root(anchor: pathlib.Path) -> pathlib.Path | None:
    try:
        location = anchor.expanduser().resolve()
    except OSError:
        return None
    start = location if location.is_dir() else location.parent
    for candidate in (start, *start.parents):
        if _is_supported_project_root(candidate):
            return candidate
    return None


def _resolve_runtime_state() -> tuple[pathlib.Path, pathlib.Path | None, bool]:
    """Return the state root, owning project, and legacy-fallback policy.

    A runtime named explicitly or discovered inside a project is isolated even
    when its directory is not literally named ``.revvy``. Only the historical
    machine-wide default may consult the plugin-level legacy configuration.
    """
    configured = os.environ.get("REVVY_STATE_DIR")
    if configured:
        state_root = pathlib.Path(configured).expanduser().resolve()
        configured_project = str(os.environ.get("REVVY_PROJECT_DIR") or "").strip()
        if configured_project:
            project = pathlib.Path(configured_project).expanduser().resolve()
            # REVVY_STATE_DIR is allowed to live outside the project (managed
            # deployments and test hosts use this deliberately).  The explicit
            # project variable still owns that runtime and is required for
            # project-scoped ports and bridge identity checks.
            return state_root, project, False
        previous_state = globals().get("STATE_DIR")
        if previous_state is not None and _same_runtime_path(state_root, previous_state):
            return (
                state_root,
                globals().get("_RUNTIME_PROJECT_DIR"),
                bool(globals().get("_ALLOW_LEGACY_CONFIG_FALLBACK", False)),
            )
        return state_root, None, False
    configured_project = str(os.environ.get("REVVY_PROJECT_DIR") or "").strip()
    if configured_project:
        project = pathlib.Path(configured_project).expanduser().resolve()
        return project / PROJECT_STATE_DIRNAME, project, False
    anchors = [
        pathlib.Path(sys.executable) if getattr(sys, "frozen", False) else SCRIPT_DIR,
        pathlib.Path.cwd(),
    ]
    for anchor in anchors:
        project = _nearest_project_root(anchor)
        if project is not None:
            return project / PROJECT_STATE_DIRNAME, project, False
    base = os.environ.get("APPDATA") or os.environ.get("XDG_STATE_HOME")
    state_root: pathlib.Path
    if base:
        state_root = pathlib.Path(base) / "Revvy"
    else:
        state_root = pathlib.Path.home() / ".local" / "state" / "Revvy"
    # An explicit config path is authoritative even when the state root itself
    # uses the historical default location.
    allow_legacy = not bool(str(os.environ.get("REVVY_CONFIG") or "").strip())
    return state_root, None, allow_legacy


def _state_dir() -> pathlib.Path:
    return _resolve_runtime_state()[0]


LEGACY_CONFIG_PATH = PLUGIN_DIR / "revvy-proxy.json"


def configure_runtime_paths(
    state_root: str | os.PathLike[str] | None = None,
    *,
    project_dir: str | os.PathLike[str] | None = None,
) -> pathlib.Path:
    """Rebind all module runtime paths after an in-app project switch."""
    global STATE_DIR, CONFIG_PATH, MANIFEST_PATH, PROCESS_PATH, LOG_PATH
    global UPSTREAM_CONFIG_PATH, _RUNTIME_PROJECT_DIR
    global _ALLOW_LEGACY_CONFIG_FALLBACK
    if state_root is None:
        STATE_DIR, runtime_project, allow_legacy = _resolve_runtime_state()
    else:
        STATE_DIR = pathlib.Path(state_root).expanduser().resolve()
        runtime_project = (
            pathlib.Path(project_dir).expanduser().resolve()
            if project_dir is not None
            else None
        )
        allow_legacy = False
    _RUNTIME_PROJECT_DIR = runtime_project
    _ALLOW_LEGACY_CONFIG_FALLBACK = allow_legacy
    os.environ["REVVY_STATE_DIR"] = str(STATE_DIR)
    if runtime_project is not None:
        os.environ["REVVY_PROJECT_DIR"] = str(runtime_project)
        try:
            STATE_DIR.mkdir(parents=True, exist_ok=True)
            ignore_path = STATE_DIR / ".gitignore"
            if not ignore_path.exists():
                try:
                    with ignore_path.open("x", encoding="utf-8", newline="\n") as handle:
                        handle.write("*\n")
                except FileExistsError:
                    pass
        except OSError:
            pass
    defaults = {
        "REVVY_CONFIG": STATE_DIR / "revvy-proxy.json",
        "REVVY_MCP_UPSTREAMS_CONFIG": STATE_DIR / "mcp-upstreams.json",
        "REVVY_PROXY_LOG": STATE_DIR / "proxy.log",
    }
    for name, path in defaults.items():
        if state_root is not None:
            os.environ[name] = str(path)
        else:
            os.environ.setdefault(name, str(path))
    CONFIG_PATH = pathlib.Path(
        os.environ.get("REVVY_CONFIG", str(STATE_DIR / "revvy-proxy.json"))
    ).expanduser()
    UPSTREAM_CONFIG_PATH = pathlib.Path(
        os.environ.get(
            "REVVY_MCP_UPSTREAMS_CONFIG", str(STATE_DIR / "mcp-upstreams.json")
        )
    ).expanduser()
    MANIFEST_PATH = STATE_DIR / "tools-manifest.json"
    PROCESS_PATH = STATE_DIR / "proxy-process.json"
    LOG_PATH = pathlib.Path(
        os.environ.get("REVVY_PROXY_LOG", str(STATE_DIR / "proxy.log"))
    ).expanduser()
    return STATE_DIR


STATE_DIR: pathlib.Path
CONFIG_PATH: pathlib.Path
UPSTREAM_CONFIG_PATH: pathlib.Path
MANIFEST_PATH: pathlib.Path
PROCESS_PATH: pathlib.Path
LOG_PATH: pathlib.Path
_RUNTIME_STATE_WAS_EXPLICIT = bool(os.environ.get("REVVY_STATE_DIR"))
_RUNTIME_PROJECT_DIR: pathlib.Path | None
_ALLOW_LEGACY_CONFIG_FALLBACK: bool
configure_runtime_paths()


def proxy_process_creation_flags() -> int:
    """Keep the persistent proxy off the user's desktop on Windows."""
    if os.name != "nt":
        return 0
    return subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP


def _read_json(path: pathlib.Path) -> Any:
    for encoding in ("utf-8-sig", "utf-16", "utf-8"):
        try:
            with path.open("r", encoding=encoding) as handle:
                return json.load(handle)
        except (UnicodeDecodeError, UnicodeError):
            continue
    raise ValueError(f"Could not decode JSON file: {path}")


def _write_json(path: pathlib.Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    # A fixed ``.tmp`` name lets two Revvy processes open the same staging
    # file and makes the second replace fail after the first one moves it.  A
    # unique sibling preserves atomic replacement without that collision.
    temporary = path.with_name(
        f".{path.name}.{os.getpid()}.{secrets.token_hex(8)}.tmp"
    )
    try:
        with temporary.open("x", encoding="utf-8", newline="\n") as handle:
            json.dump(value, handle, indent=2, ensure_ascii=False)
            handle.write("\n")
        # Windows can transiently deny a replace while another process has the
        # destination between open/read/close.  Keep the unique staging file
        # and retry that atomic operation for a short bounded interval.
        deadline = time.monotonic() + 1.0
        while True:
            try:
                os.replace(temporary, path)
                break
            except PermissionError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.01)
    finally:
        try:
            temporary.unlink(missing_ok=True)
        except OSError:
            pass


def load_config() -> dict[str, Any]:
    config: dict[str, Any] = {
        "config_version": 2,
        "proxy_port": DEFAULT_PROXY_PORT,
        "ue_port": DEFAULT_EDITOR_PORT,
        "bearer_token": "",
        "bearer_scope": "",
        "allowed_origins": [],
    }
    source: pathlib.Path | None = CONFIG_PATH if CONFIG_PATH.exists() else None
    if source is None and _ALLOW_LEGACY_CONFIG_FALLBACK and LEGACY_CONFIG_PATH.exists():
        source = LEGACY_CONFIG_PATH
    if source is not None:
        loaded = _read_json(source)
        if not isinstance(loaded, dict):
            raise ValueError(f"Expected a JSON object in {source}")
        config.update(loaded)
    for key in ("proxy_port", "ue_port"):
        raw_value = config[key]
        if isinstance(raw_value, bool):
            raise ValueError(f"{key} must be an integer TCP port")
        if not isinstance(raw_value, int):
            raise ValueError(f"{key} must be an integer TCP port")
        value = raw_value
        if not 1 <= value <= 65535:
            raise ValueError(f"{key} must be between 1 and 65535")
        config[key] = value
    config["bearer_token"] = str(config.get("bearer_token", ""))
    config["bearer_scope"] = str(config.get("bearer_scope", "")).strip().lower()
    allowed_origins = config.get("allowed_origins", [])
    config["allowed_origins"] = (
        [origin for origin in allowed_origins if isinstance(origin, str) and origin]
        if isinstance(allowed_origins, list)
        else []
    )
    return config


def _project_port_pair(project_dir: pathlib.Path) -> tuple[int, int]:
    """Return a stable editor/proxy port pair for one project path."""
    canonical = os.path.normcase(str(project_dir.resolve())).replace("\\", "/")
    digest = hashlib.sha256(canonical.encode("utf-8")).digest()
    slot = int.from_bytes(digest[:4], "big") % PROJECT_PORT_PAIR_COUNT
    editor_port = PROJECT_EDITOR_PORT_BASE + slot * 2
    return editor_port, editor_port + 1


def _loopback_port_pair_available(editor_port: int, proxy_port: int) -> bool:
    probes: list[socket.socket] = []
    try:
        for port in (editor_port, proxy_port):
            probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            probes.append(probe)
            if os.name == "nt" and hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
                probe.setsockopt(
                    socket.SOL_SOCKET,
                    socket.SO_EXCLUSIVEADDRUSE,
                    1,
                )
            probe.bind(("127.0.0.1", port))
        return True
    except OSError:
        return False
    finally:
        for probe in probes:
            probe.close()


def _loopback_port_available(port: int) -> bool:
    """Return whether one loopback port can be exclusively claimed now."""
    probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        if os.name == "nt" and hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
            probe.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        probe.bind(("127.0.0.1", int(port)))
        return True
    except (OSError, TypeError, ValueError):
        return False
    finally:
        probe.close()


def _available_project_port_pair(project_dir: pathlib.Path) -> tuple[int, int]:
    preferred_editor, _preferred_proxy = _project_port_pair(project_dir)
    preferred_slot = (preferred_editor - PROJECT_EDITOR_PORT_BASE) // 2
    for offset in range(PROJECT_PORT_PROBE_ATTEMPTS):
        slot = (preferred_slot + offset) % PROJECT_PORT_PAIR_COUNT
        editor_port = PROJECT_EDITOR_PORT_BASE + slot * 2
        proxy_port = editor_port + 1
        if _loopback_port_pair_available(editor_port, proxy_port):
            return editor_port, proxy_port
    raise RuntimeError("No free project-local Revvy port pair is available")


def _ensure_project_port_scope(config: dict[str, Any]) -> bool:
    """Give every supported project a stable editor/proxy port pair."""
    if _RUNTIME_PROJECT_DIR is None:
        return False
    if not _is_supported_project_root(_RUNTIME_PROJECT_DIR):
        return False
    try:
        proxy_port = int(config.get("proxy_port", DEFAULT_PROXY_PORT))
        editor_port = int(config.get("ue_port", DEFAULT_EDITOR_PORT))
    except (TypeError, ValueError):
        proxy_port = 0
        editor_port = 0
    valid_ports = 1 <= editor_port <= 65535 and 1 <= proxy_port <= 65535
    legacy_defaults = (
        editor_port == DEFAULT_EDITOR_PORT
        and proxy_port == DEFAULT_PROXY_PORT
        and config.get("port_scope") != "project"
    )
    # An editor can legitimately be launched before Revvy has bootstrapped the
    # project.  In that first session it is already bound to the historical
    # port, so migrating the JSON underneath it would split the published route
    # from the real listener.  Keep the live, identity-verified route for this
    # session; once the editor stops, the next bootstrap migrates normally.
    published_engine = str(config.get("engine") or "").strip().lower()
    published_project = str(config.get("project_path") or "").strip()
    if not published_project:
        active_project = config.get("active_project")
        if isinstance(active_project, dict):
            published_project = str(active_project.get("path") or "").strip()
    if (
        legacy_defaults
        and published_engine in {"unreal", "unity", "godot"}
        and published_project
    ):
        probe_config = dict(config)
        probe_config.setdefault("project_path", str(_RUNTIME_PROJECT_DIR))
        probe_config.setdefault("engine", _project_engine(_RUNTIME_PROJECT_DIR))
        if _unreal_running(probe_config, timeout=1.0):
            return False

    occupied_by_foreign_listener = False
    if (
        valid_ports
        and config.get("port_scope") == "project"
        and not _loopback_port_pair_available(editor_port, proxy_port)
    ):
        try:
            raw_health = _request_proxy_health(config, timeout=0.25)
        except (OSError, ValueError):
            raw_health = None
        own_editor_is_live = _unreal_running(config, timeout=0.5)
        own_proxy_is_live = bool(
            raw_health is not None
            and _same_runtime_path(raw_health.get("config_path"), CONFIG_PATH)
        )
        # Treat the two ports independently.  A healthy proxy on this runtime's
        # proxy port does not prove that the adjacent editor port belongs to
        # this project; an unrelated listener there would otherwise make the
        # next editor bind fail forever.
        editor_port_is_foreign = bool(
            not own_editor_is_live and not _loopback_port_available(editor_port)
        )
        proxy_port_is_foreign = bool(
            not own_proxy_is_live and not _loopback_port_available(proxy_port)
        )
        occupied_by_foreign_listener = bool(
            not own_editor_is_live
            and (editor_port_is_foreign or proxy_port_is_foreign)
        )
    changed = False
    if not valid_ports or legacy_defaults or occupied_by_foreign_listener:
        editor_port, proxy_port = _available_project_port_pair(
            _RUNTIME_PROJECT_DIR
        )
        config["ue_port"] = editor_port
        config["proxy_port"] = proxy_port
        changed = True
    if config.get("port_scope") != "project":
        config["port_scope"] = "project"
        changed = True
    return changed


def ensure_runtime_config() -> dict[str, Any]:
    config = load_config()
    changed = config.pop("full_access_token", None) is not None
    changed = _ensure_project_port_scope(config) or changed
    legacy_backend_token = (
        str(config.get("bearer_token") or "").strip()
        if config.get("bearer_scope") != "read_only"
        else ""
    )
    migration_error: SecureStoreError | None = None
    if legacy_backend_token and len(legacy_backend_token) >= 32:
        try:
            ensure_bridge_hmac_key()
        except SecureStoreError as exc:
            migration_error = exc
        try:
            scrub_legacy_editor_api_keys(config, require_project=True)
        except SecureStoreError as exc:
            migration_error = migration_error or exc
        config["bearer_token"] = ""
        config["bearer_scope"] = "read_only"
        changed = True
    elif legacy_backend_token:
        scrub_legacy_editor_api_keys(config, require_project=False)
        config["bearer_token"] = ""
        config["bearer_scope"] = "read_only"
        changed = True
    if not CONFIG_PATH.exists() or changed:
        _write_json(CONFIG_PATH, config)
    if migration_error is not None:
        raise migration_error
    # All editor bridges authenticate the proxy with an owner-only v2 key that
    # is independent of the retired bearer previously sent over HTTP.
    # Provision it before a packaged or source proxy can be launched.
    ensure_bridge_hmac_key()
    retire_legacy_ue_backend_token()
    return config


def full_access_token() -> str:
    return _PROCESS_FULL_ACCESS_TOKEN


def full_access_fingerprint() -> str:
    token = full_access_token()
    return hashlib.sha256(token.encode("utf-8")).hexdigest() if token else ""


def _proxy_url(config: dict[str, Any], path: str = "/mcp") -> str:
    return f"http://127.0.0.1:{config['proxy_port']}{path}"


_SAFE_FALLBACK_ANNOUNCED = False


def _proxy_session(
    config: dict[str, Any],
    health: dict[str, Any] | None,
) -> tuple[str, dict[str, str]]:
    """Pick the MCP endpoint this shell is actually allowed to use.

    The Full access bearer is minted per process, so a proxy started by the
    desktop app or by another terminal will reject ours. Falling back to the
    read-only endpoint keeps queries working instead of failing every command
    with an opaque 401, and mutating calls then return an explanation rather
    than an HTTP error.
    """

    global _SAFE_FALLBACK_ANNOUNCED
    token = full_access_token()
    owns_proxy = bool(
        token
        and health
        and health.get("full_access_token_fingerprint") == full_access_fingerprint()
    )
    if owns_proxy:
        return _proxy_url(config, "/mcp"), {"Authorization": f"Bearer {token}"}

    if not _SAFE_FALLBACK_ANNOUNCED:
        _SAFE_FALLBACK_ANNOUNCED = True
        print(
            "Revvy: this shell has Safe (read-only) access because the running proxy "
            "was started by another process.\n"
            f"  Set {FULL_ACCESS_TOKEN_ENV} in your environment and run 'revvy restart' "
            "for Full access.",
            file=sys.stderr,
        )
    return _proxy_url(config, "/mcp/safe"), {}


def _request_json(
    url: str,
    *,
    body: dict[str, Any] | None = None,
    timeout: float = 5.0,
    extra_headers: dict[str, str] | None = None,
) -> dict[str, Any]:
    data = None if body is None else json.dumps(body).encode("utf-8")
    headers = {"Accept": "application/json", "Connection": "close"}
    if data is not None:
        headers["Content-Type"] = "application/json"
        headers["MCP-Protocol-Version"] = "2025-06-18"
    if extra_headers:
        headers.update(extra_headers)
    request = urllib.request.Request(url, data=data, headers=headers)
    with urllib.request.urlopen(request, timeout=timeout) as response:
        payload = response.read()
    decoded = json.loads(payload.decode("utf-8"))
    if not isinstance(decoded, dict):
        raise ValueError("Expected a JSON object response")
    return decoded


def _request_proxy_health(
    config: dict[str, Any], timeout: float = 1.5
) -> dict[str, Any] | None:
    # Identity must be established before any credential is disclosed. Public
    # health contains the service/config/capability/fingerprint fields needed
    # for that decision; private upstream diagnostics are not needed here.
    try:
        health = _request_json(
            _proxy_url(config, "/health"), timeout=timeout
        )
    except (OSError, ValueError, json.JSONDecodeError, urllib.error.URLError):
        return None
    if health.get("service") != "revvy-proxy":
        return None
    return health


def _proxy_supports_current_runtime(health: Any) -> bool:
    """Whether health belongs to the API contract this CLI can safely use."""
    if not isinstance(health, dict):
        return False
    try:
        api_version = int(health.get("proxy_api_version", 0))
    except (TypeError, ValueError):
        return False
    capabilities = health.get("capabilities")
    return (
        api_version >= 5
        and isinstance(capabilities, list)
        and {
            "mcp_upstreams",
            "on_demand_mcp_upstreams",
            "blender_bridge",
            "safe_read_only_endpoint",
            "full_access_auth",
        }.issubset({str(value) for value in capabilities})
    )


def _proxy_uses_current_full_access_token(health: Any) -> bool:
    """Whether this process may authenticate to the proxy's Full endpoint."""
    return bool(
        isinstance(health, dict)
        and full_access_fingerprint()
        and health.get("full_access_token_fingerprint")
        == full_access_fingerprint()
    )


def get_health(config: dict[str, Any], timeout: float = 1.5) -> dict[str, Any] | None:
    """Return health only for the proxy bound to this runtime state directory."""
    health = _request_proxy_health(config, timeout)
    if health is None:
        return None
    reported_config = health.get("config_path")
    if reported_config:
        return health if _same_runtime_path(reported_config, CONFIG_PATH) else None
    # Older proxies did not always expose config_path. They remain compatible
    # with legacy/global state, but cannot be safely adopted by project-local
    # sessions because another project may own the fixed port.
    if not _ALLOW_LEGACY_CONFIG_FALLBACK:
        return None
    return health


def _project_from_config(config: dict[str, Any], key: str) -> dict[str, str] | None:
    value = config.get(key)
    if isinstance(value, dict):
        name = str(value.get("name", "")).strip()
        path = str(value.get("path", "")).strip()
        if name or path:
            return {"name": name, "path": path}
    if key == "active_project":
        name = str(config.get("project_name", "")).strip()
        path = str(config.get("project_path", "")).strip()
        if name or path:
            return {"name": name, "path": path}
    return None


def _config_for_proxy_port(config: dict[str, Any], port: int) -> dict[str, Any]:
    routed = dict(config)
    routed["proxy_port"] = port
    return routed


def _managed_process_records() -> list[dict[str, Any]]:
    if not PROCESS_PATH.exists():
        return []
    try:
        state = _read_json(PROCESS_PATH)
    except (OSError, ValueError, json.JSONDecodeError):
        return []
    if not isinstance(state, dict):
        return []
    records = [state]
    stale = state.get("stale_process")
    if isinstance(stale, dict):
        records.append(stale)
    return records


def _managed_proxy_candidates(
    config: dict[str, Any], *, include_configured_port: bool
) -> list[tuple[dict[str, Any], dict[str, Any]]]:
    endpoints: list[tuple[int, int | None]] = []
    if include_configured_port:
        endpoints.append((int(config["proxy_port"]), None))
    for record in _managed_process_records():
        try:
            port = int(record.get("proxy_port", 0))
            pid = int(record.get("pid", 0))
        except (TypeError, ValueError):
            continue
        if 1 <= port <= 65535 and pid > 0:
            endpoints.append((port, pid))

    candidates: list[tuple[dict[str, Any], dict[str, Any]]] = []
    seen_pids: set[int] = set()
    for port, expected_pid in endpoints:
        routed = _config_for_proxy_port(config, port)
        health = get_health(routed)
        pid = health.get("pid") if health else None
        if not isinstance(pid, int) or pid <= 0:
            continue
        if expected_pid is not None and pid != expected_pid:
            continue
        # A configured port alone is not ownership proof: a loopback peer can
        # imitate the public health payload and nominate an unrelated PID. Only
        # the matching Full-access owner may stop an unrecorded endpoint. A
        # persisted manager record remains usable across app restarts, where the
        # intentionally process-local token has changed.
        if (
            expected_pid is None
            and health.get("full_access_token_fingerprint")
            != full_access_fingerprint()
        ):
            continue
        if pid not in seen_pids:
            candidates.append((routed, health))
            seen_pids.add(pid)
    return candidates


def _stop_managed_proxies(
    config: dict[str, Any], *, include_configured_port: bool, quiet: bool
) -> int:
    candidates = _managed_proxy_candidates(
        config, include_configured_port=include_configured_port
    )
    if not candidates:
        PROCESS_PATH.unlink(missing_ok=True)
        if not quiet:
            print("Revvy proxy is not running.")
        return EXIT_OK

    failed = False
    for routed, health in candidates:
        pid = int(health["pid"])
        tree_stopped = terminate_process_tree(pid)

        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline and get_health(routed, timeout=0.2):
            time.sleep(0.1)
        if get_health(routed, timeout=0.2):
            detail = "process tree stop failed; " if not tree_stopped else ""
            print(
                f"Revvy proxy PID {pid} did not stop ({detail}health still responds).",
                file=sys.stderr,
            )
            failed = True

    if failed:
        return EXIT_PROXY_UNAVAILABLE
    PROCESS_PATH.unlink(missing_ok=True)
    if not quiet:
        print("Revvy proxy stopped.")
    return EXIT_OK


def _configured_project_path(config: dict[str, Any]) -> pathlib.Path | None:
    if _RUNTIME_PROJECT_DIR is not None:
        return _RUNTIME_PROJECT_DIR
    configured_project = str(os.environ.get("REVVY_PROJECT_DIR") or "").strip()
    if configured_project:
        try:
            return pathlib.Path(configured_project).expanduser().resolve()
        except (OSError, ValueError):
            return None
    for key in ("installed_project", "active_project"):
        value = config.get(key)
        if isinstance(value, dict) and value.get("path"):
            try:
                candidate = pathlib.Path(str(value["path"])).expanduser()
                if candidate.is_absolute():
                    return candidate.resolve()
            except (OSError, ValueError):
                pass
    value = str(config.get("project_path") or "").strip()
    if value:
        try:
            candidate = pathlib.Path(value).expanduser()
            if candidate.is_absolute():
                return candidate.resolve()
        except (OSError, ValueError):
            pass
    return None


def _bridge_identity_matches(config: dict[str, Any], result: Any) -> bool:
    """Require the listener to identify the engine and exact owning project."""
    if not isinstance(result, dict):
        return False
    expected_project = _configured_project_path(config)
    if expected_project is None:
        return False
    expected_engine = str(config.get("engine") or "").strip().lower()
    if expected_engine not in {"unreal", "unity", "godot"}:
        expected_engine = _project_engine(expected_project)
    if not expected_engine or str(result.get("engine") or "").strip().lower() != expected_engine:
        return False
    reported_project = str(result.get("project_path") or "").strip()
    if not reported_project:
        return False
    try:
        candidate = pathlib.Path(reported_project).expanduser()
        if not candidate.is_absolute():
            return False
        project_matches = _same_runtime_path(
            candidate.resolve(), expected_project.resolve()
        )
    except (OSError, ValueError):
        return False
    if not project_matches:
        return False

    configured_instance = str(config.get("bridge_instance_id") or "").strip()
    reported_instance = str(result.get("bridge_instance_id") or "").strip()
    reported_port = result.get("editor_port")
    try:
        config_version = int(config.get("config_version", 0))
    except (TypeError, ValueError):
        config_version = 0
    if configured_instance and reported_instance != configured_instance:
        return False
    if reported_port is not None:
        if isinstance(reported_port, bool) or not isinstance(reported_port, int):
            return False
        if reported_port != config.get("ue_port"):
            return False
    # Config v3 is emitted by every current editor bridge.  Requiring both
    # process instance and bound port blocks a stale/legacy listener from
    # downgrading the identity proof by simply omitting the new fields.
    if config_version >= 3 and (not configured_instance or not reported_instance):
        return False
    if config_version >= 3 and reported_port is None:
        return False
    return True


def _editor_ping_result(
    config: dict[str, Any], timeout: float = 0.5
) -> dict[str, Any] | None:
    payload = {"jsonrpc": "2.0", "id": "revvy-cli-health", "method": "ping"}
    headers = {
        "Content-Type": "application/json",
        "Accept": "application/json",
        "Connection": "close",
        "MCP-Protocol-Version": "2025-06-18",
    }
    # Establish identity with the public Safe credential.  The protected editor
    # capability must never be disclosed to a listener before it proves which
    # project it belongs to.
    if config.get("bearer_scope") == "read_only" and config.get("bearer_token"):
        headers["Authorization"] = f"Bearer {config['bearer_token']}"
    try:
        response = _request_json(
            f"http://127.0.0.1:{config['ue_port']}/mcp",
            body=payload,
            timeout=timeout,
            extra_headers=headers,
        )
        if (
            response.get("jsonrpc") == "2.0"
            and response.get("id") == "revvy-cli-health"
            and isinstance(response.get("result"), dict)
            and "error" not in response
        ):
            return response["result"]
    except (OSError, ValueError, json.JSONDecodeError, urllib.error.URLError):
        pass
    return None


def _unreal_running(config: dict[str, Any], timeout: float = 0.5) -> bool:
    """Compatibility name: report any supported editor only after identity proof."""
    return _bridge_identity_matches(config, _editor_ping_result(config, timeout))


def _proxy_child_environment() -> dict[str, str]:
    """Build one authoritative runtime context for either proxy child path."""
    environment = os.environ.copy()
    environment["PYTHONUTF8"] = "1"
    environment["REVVY_STATE_DIR"] = str(STATE_DIR)
    environment["REVVY_CONFIG"] = str(CONFIG_PATH)
    environment["REVVY_MCP_UPSTREAMS_CONFIG"] = str(UPSTREAM_CONFIG_PATH)
    environment["REVVY_PROXY_LOG"] = str(LOG_PATH)
    environment[FULL_ACCESS_TOKEN_ENV] = full_access_token()
    return environment


def start_proxy(quiet: bool = False) -> int:
    config = ensure_runtime_config()
    health = get_health(config)
    if _proxy_supports_current_runtime(health):
        if not quiet:
            print(f"Revvy proxy is already running (PID {health.get('pid', '?')}).")
        return EXIT_OK
    if health:
        stopped = _stop_managed_proxies(
            config, include_configured_port=True, quiet=True
        )
        if stopped != EXIT_OK:
            return stopped
        if get_health(config):
            if not quiet:
                print(
                    "An incompatible Revvy proxy still owns this project's port.",
                    file=sys.stderr,
                )
            return EXIT_PROXY_UNAVAILABLE

    foreign_health = _request_proxy_health(config)
    if foreign_health:
        foreign_config = str(foreign_health.get("config_path") or "unknown state")
        if not quiet:
            print(
                "Revvy proxy port "
                f"{config['proxy_port']} is already owned by another project ({foreign_config}). "
                "Stop that project's Revvy runtime before starting this one.",
                file=sys.stderr,
            )
        return EXIT_PROXY_UNAVAILABLE

    stopped = _stop_managed_proxies(
        config, include_configured_port=False, quiet=True
    )
    if stopped != EXIT_OK:
        print(
            "Could not stop the proxy managed on the previous port; refusing to create an orphan.",
            file=sys.stderr,
        )
        return stopped

    if not PROXY_SCRIPT.exists():
        print(f"Proxy script not found: {PROXY_SCRIPT}", file=sys.stderr)
        return EXIT_USAGE

    STATE_DIR.mkdir(parents=True, exist_ok=True)
    LOG_PATH.parent.mkdir(parents=True, exist_ok=True)
    environment = _proxy_child_environment()
    command = [sys.executable, str(PROXY_SCRIPT)]
    popen_kwargs: dict[str, Any] = {
        "cwd": str(SCRIPT_DIR),
        "env": environment,
        "stdin": subprocess.DEVNULL,
    }
    log_handle = LOG_PATH.open("ab")
    popen_kwargs["stdout"] = log_handle
    popen_kwargs["stderr"] = subprocess.STDOUT
    if os.name == "nt":
        popen_kwargs["creationflags"] = proxy_process_creation_flags()
    else:
        popen_kwargs["start_new_session"] = True

    try:
        process = subprocess.Popen(command, **popen_kwargs)
    finally:
        log_handle.close()

    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        if process.poll() is not None:
            break
        health = get_health(config, timeout=1.0)
        if _proxy_supports_current_runtime(health):
            if not _proxy_uses_current_full_access_token(health):
                # Another compatible proxy for this exact runtime won the bind.
                # Keep it alive and use its Safe endpoint; never record or later
                # tree-kill a process this invocation did not create.
                if process.poll() is None:
                    terminate_process_tree(process.pid, timeout=5.0)
                if not quiet:
                    print(
                        f"Revvy proxy is already running (PID {health.get('pid', '?')})."
                    )
                return EXIT_OK
            _write_json(
                PROCESS_PATH,
                {
                    "pid": health.get("pid", process.pid),
                    "proxy_port": config["proxy_port"],
                    "script": str(PROXY_SCRIPT),
                    "started_at": time.time(),
                },
            )
            if not quiet:
                print(f"Revvy proxy started on {_proxy_url(config)} (PID {health.get('pid', process.pid)}).")
            return EXIT_OK
        if health:
            # A listener won the bind race but does not implement this runtime's
            # complete API/capability/token contract. Never publish it as the
            # process we just started or let the CLI send an authenticated RPC.
            if process.poll() is None:
                terminate_process_tree(process.pid, timeout=5.0)
            if not quiet:
                print(
                    "An incompatible Revvy proxy appeared during startup.",
                    file=sys.stderr,
                )
            return EXIT_PROXY_UNAVAILABLE
        time.sleep(0.1)

    print(f"Revvy proxy failed to start. See {LOG_PATH}", file=sys.stderr)
    if process.poll() is None:
        terminate_process_tree(process.pid, timeout=5.0)
    return EXIT_PROXY_UNAVAILABLE


def stop_proxy(quiet: bool = False) -> int:
    config = load_config()
    return _stop_managed_proxies(
        config, include_configured_port=True, quiet=quiet
    )


def ensure_proxy() -> tuple[dict[str, Any], dict[str, Any] | None, int]:
    config = ensure_runtime_config()
    health = get_health(config)
    if _proxy_supports_current_runtime(health):
        return config, health, EXIT_OK
    exit_code = start_proxy(quiet=True)
    final_health = get_health(config)
    if exit_code == EXIT_OK and not _proxy_supports_current_runtime(final_health):
        return config, final_health, EXIT_PROXY_UNAVAILABLE
    return config, final_health, exit_code


def rpc_request(method: str, params: dict[str, Any] | None, timeout: float) -> dict[str, Any]:
    config, health, exit_code = ensure_proxy()
    if exit_code != EXIT_OK or not health:
        raise ConnectionError(f"Revvy proxy is unavailable. See {LOG_PATH}")
    request_id = int(time.time() * 1000)
    payload: dict[str, Any] = {"jsonrpc": "2.0", "id": request_id, "method": method}
    if params is not None:
        payload["params"] = params
    url, headers = _proxy_session(config, health)
    return _request_json(
        url,
        body=payload,
        timeout=timeout,
        extra_headers=headers,
    )


def command_status(args: argparse.Namespace) -> int:
    config = load_config()
    health = get_health(config)
    if health is None:
        ue_running = _unreal_running(config)
        configured_active_project = _project_from_config(config, "active_project")
        status = {
            "service": "revvy-proxy",
            "proxy_running": False,
            "proxy_url": _proxy_url(config),
            "ue_running": ue_running,
            "ue_url": f"http://127.0.0.1:{config['ue_port']}/mcp",
            "config_path": str(CONFIG_PATH),
            "manifest_path": str(MANIFEST_PATH),
            "active_project": configured_active_project if ue_running else None,
            "last_active_project": configured_active_project,
            "installed_project": _project_from_config(config, "installed_project"),
        }
    else:
        status = dict(health)
        status["proxy_running"] = True
        if "active_project" not in status:
            configured_active_project = _project_from_config(status, "active_project")
            if configured_active_project is None:
                configured_active_project = _project_from_config(config, "active_project")
            status["active_project"] = (
                configured_active_project if status.get("ue_running") else None
            )
            status["last_active_project"] = configured_active_project
        if "installed_project" not in status:
            status["installed_project"] = _project_from_config(config, "installed_project")
    if args.json:
        print(json.dumps(status, indent=2, ensure_ascii=False))
    else:
        print(f"Proxy: {'running' if status['proxy_running'] else 'stopped'} ({status['proxy_url']})")
        print(f"Unreal: {'connected' if status.get('ue_running') else 'offline'} ({status['ue_url']})")
        print(f"Tools: {status.get('manifest_tools', 0)} cached")
        active_project = status.get("active_project")
        installed_project = status.get("installed_project")
        if isinstance(active_project, dict):
            print(
                f"Active project: {active_project.get('name') or '(unnamed)'} "
                f"({active_project.get('path') or 'path unavailable'})"
            )
        else:
            print("Active project: none")
        if isinstance(installed_project, dict):
            print(
                f"Installed project: {installed_project.get('name') or '(unnamed)'} "
                f"({installed_project.get('path') or 'path unavailable'})"
            )
        print(f"Config: {status['config_path']}")
    if not status["proxy_running"]:
        return EXIT_PROXY_UNAVAILABLE
    return EXIT_OK if status.get("ue_running") else EXIT_UNREAL_UNAVAILABLE


def command_start(_args: argparse.Namespace) -> int:
    return start_proxy()


def command_stop(_args: argparse.Namespace) -> int:
    return stop_proxy()


def command_restart(_args: argparse.Namespace) -> int:
    stopped = stop_proxy(quiet=True)
    return stopped if stopped != EXIT_OK else start_proxy()


def command_serve(_args: argparse.Namespace) -> int:
    ensure_runtime_config()
    environment = _proxy_child_environment()
    return subprocess.call([sys.executable, str(PROXY_SCRIPT)], env=environment)


def command_tools(args: argparse.Namespace) -> int:
    try:
        response = rpc_request("tools/list", {}, args.timeout)
    except (ConnectionError, OSError, ValueError, urllib.error.URLError) as exc:
        print(str(exc), file=sys.stderr)
        return EXIT_PROXY_UNAVAILABLE
    tools = response.get("result", {}).get("tools", [])
    if args.filter:
        needle = args.filter.casefold()
        tools = [
            tool
            for tool in tools
            if needle in str(tool.get("name", "")).casefold()
            or needle in str(tool.get("description", "")).casefold()
        ]
    if args.json:
        print(json.dumps(tools, indent=2, ensure_ascii=False))
    else:
        for tool in tools:
            description = " ".join(str(tool.get("description", "")).split())
            print(f"{tool.get('name', '')}\t{description}")
    return EXIT_OK


def _parse_argument(value: str) -> tuple[str, Any]:
    if "=" not in value:
        raise argparse.ArgumentTypeError("--arg must use KEY=VALUE")
    key, raw_value = value.split("=", 1)
    if not key:
        raise argparse.ArgumentTypeError("--arg key cannot be empty")
    try:
        parsed: Any = json.loads(raw_value)
    except json.JSONDecodeError:
        parsed = raw_value
    return key, parsed


def command_call(args: argparse.Namespace) -> int:
    arguments: dict[str, Any] = {}
    raw_arguments = getattr(args, "args", None)
    args_file = getattr(args, "args_file", None)
    if args_file:
        try:
            raw_arguments = pathlib.Path(args_file).read_text(encoding="utf-8-sig")
        except OSError as exc:
            print(f"Could not read --args-file: {exc}", file=sys.stderr)
            return EXIT_USAGE
    elif getattr(args, "args_stdin", False):
        raw_arguments = sys.stdin.read()

    if raw_arguments:
        try:
            loaded = json.loads(raw_arguments)
        except json.JSONDecodeError as exc:
            print(f"Invalid --args JSON: {exc}", file=sys.stderr)
            return EXIT_USAGE
        if not isinstance(loaded, dict):
            print("--args must be a JSON object.", file=sys.stderr)
            return EXIT_USAGE
        arguments.update(loaded)
    for key, value in args.arg:
        arguments[key] = value

    try:
        response = rpc_request(
            "tools/call",
            {"name": args.tool, "arguments": arguments},
            args.timeout,
        )
    except (ConnectionError, OSError, ValueError, urllib.error.URLError) as exc:
        print(str(exc), file=sys.stderr)
        return EXIT_PROXY_UNAVAILABLE

    result = response.get("result", {})
    has_error = "error" in response or bool(result.get("isError"))
    if args.json:
        print(json.dumps(response, indent=2, ensure_ascii=False))
    else:
        if "error" in response:
            print(response["error"].get("message", str(response["error"])), file=sys.stderr)
            return EXIT_UNREAL_UNAVAILABLE
        for item in result.get("content", []):
            if item.get("type") == "text":
                print(item.get("text", ""))
    return EXIT_UNREAL_UNAVAILABLE if has_error else EXIT_OK


def _find_project_dir(value: str) -> pathlib.Path:
    path = pathlib.Path(value).expanduser().resolve()
    if path.is_file():
        if path.suffix.casefold() == ".uproject" or path.name == "project.godot":
            return path.parent
        raise ValueError(f"Not a supported Unreal, Unity, or Godot project file: {path}")
    for candidate in (path, *path.parents):
        projects = list(candidate.glob("*.uproject"))
        if len(projects) == 1:
            return candidate
        if len(projects) > 1:
            raise ValueError(f"Multiple .uproject files found in {candidate}; pass one explicitly")
        if (candidate / "project.godot").is_file():
            return candidate
        if (candidate / "Assets").is_dir() and (
            candidate / "ProjectSettings"
        ).is_dir():
            return candidate
    raise ValueError(f"No Unreal, Unity, or Godot project found from {path}")


def _bind_cli_project_runtime(project_dir: pathlib.Path) -> None:
    """Select local CLI state unless the caller supplied an explicit override."""
    if _RUNTIME_STATE_WAS_EXPLICIT:
        return
    environment_state = str(os.environ.get("REVVY_STATE_DIR") or "").strip()
    # Test hosts and embedders may inject module paths directly. Do not trample
    # that deliberate runtime when it differs from the process environment.
    if environment_state and not _same_runtime_path(environment_state, STATE_DIR):
        return
    configure_runtime_paths(
        project_dir / PROJECT_STATE_DIRNAME,
        project_dir=project_dir,
    )


def register_project(project: str, *, include_claude: bool = True) -> pathlib.Path:
    """Merge Revvy into a project's local MCP settings without starting a process."""

    project_dir = _find_project_dir(project)
    _bind_cli_project_runtime(project_dir)
    config = ensure_runtime_config()
    project_files = list(project_dir.glob("*.uproject"))
    engine = (
        "unreal"
        if project_files
        else "godot"
        if (project_dir / "project.godot").is_file()
        else "unity"
    )
    config["engine"] = engine
    config["installed_project"] = {
        "name": project_files[0].stem if len(project_files) == 1 else project_dir.name,
        "path": str(project_dir),
    }
    config["updated_at"] = time.time()
    _write_json(CONFIG_PATH, config)
    mcp_path = project_dir / ".mcp.json"
    mcp_config: dict[str, Any] = {}
    if mcp_path.exists():
        loaded = _read_json(mcp_path)
        if not isinstance(loaded, dict):
            raise ValueError(f"Expected a JSON object in {mcp_path}")
        mcp_config = loaded
    servers = mcp_config.setdefault("mcpServers", {})
    if not isinstance(servers, dict):
        raise ValueError(f"mcpServers must be an object in {mcp_path}")
    # Point at the same endpoint the editor writes (MCPServer.cpp) so the two
    # writers cannot fight over this entry. A user who wired Full access adds an
    # Authorization header; that is a deliberate choice, so it is preserved.
    existing_server = servers.get("Revvy")
    revvy_server = dict(existing_server) if isinstance(existing_server, dict) else {}
    revvy_server["type"] = "http"
    existing_url = str(revvy_server.get("url") or "").strip()
    managed_safe_url = bool(
        re.fullmatch(
            r"http://127\.0\.0\.1:\d+/mcp/safe/?",
            existing_url,
            flags=re.IGNORECASE,
        )
    )
    if not existing_url or (
        "headers" not in revvy_server and managed_safe_url
    ):
        revvy_server["url"] = _proxy_url(config, "/mcp/safe")
    servers["Revvy"] = revvy_server
    _write_json(mcp_path, mcp_config)

    if include_claude:
        _set_claude_server_enabled(project_dir, "Revvy", enabled=True)
    return mcp_path


FULL_ACCESS_SERVER_NAME = "Revvy-Full"


def _set_claude_server_enabled(
    project_dir: pathlib.Path, server_name: str, *, enabled: bool
) -> None:
    settings_path = project_dir / ".claude" / "settings.local.json"
    if not enabled and not settings_path.exists():
        return
    settings: dict[str, Any] = {}
    if settings_path.exists():
        loaded = _read_json(settings_path)
        if not isinstance(loaded, dict):
            raise ValueError(f"Expected a JSON object in {settings_path}")
        settings = loaded
    names = settings.setdefault("enabledMcpjsonServers", [])
    if not isinstance(names, list):
        raise ValueError(f"enabledMcpjsonServers must be an array in {settings_path}")
    if enabled:
        if server_name not in names:
            names.append(server_name)
    else:
        settings["enabledMcpjsonServers"] = [name for name in names if name != server_name]
    _write_json(settings_path, settings)


def grant_full_access(
    project: str,
    *,
    server_name: str = FULL_ACCESS_SERVER_NAME,
    include_claude: bool = True,
) -> tuple[pathlib.Path, list[str]]:
    """Add a Full access MCP entry alongside the read-only one.

    The entry references the environment variable rather than the secret, so the
    file stays safe to commit. The read-only `Revvy` entry is left in place: a
    client picks a permission level by choosing which server it connects to.
    """

    if len(_SUPPLIED_FULL_ACCESS_TOKEN) < MIN_FULL_ACCESS_TOKEN_LENGTH:
        raise ValueError(
            f"{FULL_ACCESS_TOKEN_ENV} is not set in this shell, or is shorter than "
            f"{MIN_FULL_ACCESS_TOKEN_LENGTH} characters. Set it for your user account, open a "
            "new terminal, then run this command again. Without it the client would send an "
            "unresolved placeholder and every request would fail with HTTP 401."
        )

    project_dir = _find_project_dir(project)
    _bind_cli_project_runtime(project_dir)
    config = ensure_runtime_config()
    warnings: list[str] = []
    health = get_health(config)
    if not health:
        warnings.append("The proxy is not running; start it with 'revvy start'.")
    elif health.get("full_access_token_fingerprint") != full_access_fingerprint():
        warnings.append(
            "The running proxy was started with a different token. Run 'revvy restart' "
            "from this shell so it adopts yours, or the Full entry will still get 401."
        )

    mcp_path = project_dir / ".mcp.json"
    mcp_config: dict[str, Any] = {}
    if mcp_path.exists():
        loaded = _read_json(mcp_path)
        if not isinstance(loaded, dict):
            raise ValueError(f"Expected a JSON object in {mcp_path}")
        mcp_config = loaded
    servers = mcp_config.setdefault("mcpServers", {})
    if not isinstance(servers, dict):
        raise ValueError(f"mcpServers must be an object in {mcp_path}")
    servers[server_name] = {
        "type": "http",
        "url": _proxy_url(config, "/mcp"),
        "headers": {"Authorization": f"Bearer ${{{FULL_ACCESS_TOKEN_ENV}}}"},
    }
    _write_json(mcp_path, mcp_config)

    if include_claude:
        _set_claude_server_enabled(project_dir, server_name, enabled=True)
    return mcp_path, warnings


def revoke_full_access(
    project: str,
    *,
    server_name: str = FULL_ACCESS_SERVER_NAME,
    include_claude: bool = True,
) -> tuple[pathlib.Path, bool]:
    """Remove the Full access entry, leaving the read-only server in place."""

    project_dir = _find_project_dir(project)
    mcp_path = project_dir / ".mcp.json"
    removed = False
    if mcp_path.exists():
        mcp_config = _read_json(mcp_path)
        if not isinstance(mcp_config, dict):
            raise ValueError(f"Expected a JSON object in {mcp_path}")
        servers = mcp_config.get("mcpServers")
        if isinstance(servers, dict) and server_name in servers:
            del servers[server_name]
            removed = True
            _write_json(mcp_path, mcp_config)
    if include_claude:
        _set_claude_server_enabled(project_dir, server_name, enabled=False)
    return mcp_path, removed


def command_install(args: argparse.Namespace) -> int:
    try:
        mcp_path = register_project(args.project, include_claude=not args.no_claude)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(str(exc), file=sys.stderr)
        return EXIT_USAGE

    print(f"Registered Revvy in {mcp_path}")
    return start_proxy()


def command_grant_full(args: argparse.Namespace) -> int:
    try:
        mcp_path, warnings = grant_full_access(
            args.project,
            server_name=args.server_name,
            include_claude=not args.no_claude,
        )
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(str(exc), file=sys.stderr)
        return EXIT_USAGE

    print(f"Added the Full access server '{args.server_name}' to {mcp_path}")
    print("The read-only 'Revvy' entry is untouched; a client picks its permission")
    print("level by choosing which of the two servers it connects to.")
    print("Reconnect the MCP client to pick up the change.")
    for warning in warnings:
        print(f"warning: {warning}", file=sys.stderr)
    return EXIT_OK


def command_revoke_full(args: argparse.Namespace) -> int:
    try:
        mcp_path, removed = revoke_full_access(
            args.project,
            server_name=args.server_name,
            include_claude=not args.no_claude,
        )
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(str(exc), file=sys.stderr)
        return EXIT_USAGE

    if removed:
        print(f"Removed the Full access server '{args.server_name}' from {mcp_path}")
    else:
        print(f"No '{args.server_name}' server was present in {mcp_path}")
    return EXIT_OK


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="revvy", description="Control Revvy outside Unreal Editor.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    subparsers.add_parser("start", help="Start the persistent MCP proxy.").set_defaults(func=command_start)
    subparsers.add_parser("stop", help="Stop the persistent MCP proxy.").set_defaults(func=command_stop)
    subparsers.add_parser("restart", help="Restart the persistent MCP proxy.").set_defaults(func=command_restart)
    subparsers.add_parser("serve", help="Run the proxy in the foreground.").set_defaults(func=command_serve)

    status_parser = subparsers.add_parser("status", help="Show proxy and Unreal connection state.")
    status_parser.add_argument("--json", action="store_true", help="Print machine-readable JSON.")
    status_parser.set_defaults(func=command_status)

    tools_parser = subparsers.add_parser("tools", help="List cached Revvy tools.")
    tools_parser.add_argument("filter", nargs="?", help="Filter by name or description.")
    tools_parser.add_argument("--json", action="store_true", help="Print machine-readable JSON.")
    tools_parser.add_argument("--timeout", type=float, default=10.0)
    tools_parser.set_defaults(func=command_tools)

    call_parser = subparsers.add_parser("call", help="Call a Revvy tool through MCP.")
    call_parser.add_argument("tool", help="Exact tool name from `revvy tools`.")
    argument_source = call_parser.add_mutually_exclusive_group()
    argument_source.add_argument("--args", help="Tool arguments as one JSON object.")
    argument_source.add_argument("--args-file", help="Read the argument object from a UTF-8 JSON file.")
    argument_source.add_argument(
        "--args-stdin", action="store_true", help="Read the argument object from standard input."
    )
    call_parser.add_argument(
        "--arg",
        action="append",
        type=_parse_argument,
        default=[],
        metavar="KEY=VALUE",
        help="Add an argument; JSON scalars, arrays, and objects are accepted.",
    )
    call_parser.add_argument("--json", action="store_true", help="Print the full MCP response.")
    call_parser.add_argument("--timeout", type=float, default=120.0)
    call_parser.set_defaults(func=command_call)

    install_parser = subparsers.add_parser("install", help="Merge Revvy into a project's MCP config.")
    install_parser.add_argument("--project", default=".", help="Project directory or .uproject path.")
    install_parser.add_argument("--no-claude", action="store_true", help="Do not update Claude settings.")
    install_parser.set_defaults(func=command_install)

    grant_parser = subparsers.add_parser(
        "grant-full",
        help="Add a Full access MCP server entry beside the read-only one.",
    )
    grant_parser.add_argument("--project", default=".", help="Project directory or .uproject path.")
    grant_parser.add_argument(
        "--server-name",
        default=FULL_ACCESS_SERVER_NAME,
        help=f"Name for the Full access entry (default {FULL_ACCESS_SERVER_NAME}).",
    )
    grant_parser.add_argument("--no-claude", action="store_true", help="Do not update Claude settings.")
    grant_parser.set_defaults(func=command_grant_full)

    revoke_parser = subparsers.add_parser(
        "revoke-full", help="Remove the Full access MCP server entry."
    )
    revoke_parser.add_argument("--project", default=".", help="Project directory or .uproject path.")
    revoke_parser.add_argument(
        "--server-name",
        default=FULL_ACCESS_SERVER_NAME,
        help=f"Name of the Full access entry (default {FULL_ACCESS_SERVER_NAME}).",
    )
    revoke_parser.add_argument("--no-claude", action="store_true", help="Do not update Claude settings.")
    revoke_parser.set_defaults(func=command_revoke_full)
    return parser


def main(argv: list[str] | None = None) -> int:
    try:
        args = build_parser().parse_args(argv)
        return int(args.func(args))
    except KeyboardInterrupt:
        return 130
    except (OSError, ValueError) as exc:
        print(str(exc), file=sys.stderr)
        return EXIT_USAGE


if __name__ == "__main__":
    raise SystemExit(main())
