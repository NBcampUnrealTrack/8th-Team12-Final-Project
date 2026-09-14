#!/usr/bin/env python3
"""
Revvy MCP Proxy
================
Always-running proxy that lets MCP clients see Revvy tool definitions even
when the selected Unreal, Unity, or Godot editor is not running.

How it works:
  - Listens on the project-scoped proxy port (the first project normally uses 8089)
  - tools/list  -> served from <project>/.revvy/tools-manifest.json (written by the editor)
  - tools/call  -> forwarded over the authenticated local editor bridge
  - initialize  -> answered directly (no editor needed)
  - everything else -> forwarded to the selected editor bridge

Setup:
  1. Run this script once to start the proxy:
       pythonw revvy-proxy.py      (Windows, no console window)
       python  revvy-proxy.py      (any platform, with console)
  2. Read the selected project's .revvy/mcp.json and use its proxy_url.
  3. Prefer the Revvy desktop app or `revvy start`, which owns proxy lifecycle.
"""

import contextlib
import base64
import binascii
import copy
import difflib
import json
import hashlib
import hmac
import http.client
import os
import pathlib
import re
import secrets
import signal
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, HTTPServer, ThreadingHTTPServer
from datetime import datetime, timezone

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

DEFAULT_PROXY_PORT = 8089
DEFAULT_UE_PORT = 8088
PROXY_API_VERSION = 5
PROXY_CAPABILITIES = (
    "mcp_upstreams",
    "on_demand_mcp_upstreams",
    "blender_bridge",
    "safe_read_only_endpoint",
    "full_access_auth",
)
MAX_REQUEST_BYTES = 1024 * 1024
MODERN_PROTOCOL_VERSION = "2026-07-28"
SUPPORTED_PROTOCOL_VERSIONS = ("2025-11-25", "2025-06-18", "2024-11-05")
LEGACY_BRIDGE_PROTOCOL_VERSION = SUPPORTED_PROTOCOL_VERSIONS[0]
MODERN_CACHE_TTL_MS = 1_000
SERVER_INFO = {"name": "Revvy-Proxy", "version": "2.0.0"}
SKILL_CATALOG_SCAN_INTERVAL_SECONDS = 0.5
FULL_ACCESS_TOKEN_ENV = "REVVY_FULL_ACCESS_TOKEN"
MIN_FULL_ACCESS_TOKEN_LENGTH = 32
BRIDGE_SIGNATURE_VERSION = "v2"
BRIDGE_SIGNATURE_MAX_AGE_SECONDS = 30
EDITOR_ACK_LOST_TEXT = (
    "The editor request may have started, but its authenticated response was not "
    "received. Do not retry automatically; inspect the current editor state first."
)
EDITOR_ACK_LOST_BYTES = EDITOR_ACK_LOST_TEXT.encode("utf-8")


def _resolve_full_access_token() -> tuple[str, str]:
    """Adopt a caller-supplied Full bearer only when it can resist guessing.

    Operators are told to pin this variable so their own MCP client can hold
    Full access. A short value there would be the weak link in the whole scheme,
    so it is refused in favour of a random one rather than silently trusted.
    """

    supplied = os.environ.get(FULL_ACCESS_TOKEN_ENV) or ""
    if len(supplied) >= MIN_FULL_ACCESS_TOKEN_LENGTH:
        return supplied, ""
    if supplied:
        return (
            secrets.token_urlsafe(32),
            f"{FULL_ACCESS_TOKEN_ENV} was ignored: it must be at least "
            f"{MIN_FULL_ACCESS_TOKEN_LENGTH} characters. A random token was used instead, "
            "so external clients will fall back to the Safe endpoint.",
        )
    return secrets.token_urlsafe(32), ""


_FULL_ACCESS_TOKEN, _FULL_ACCESS_TOKEN_WARNING = _resolve_full_access_token()
os.environ[FULL_ACCESS_TOKEN_ENV] = _FULL_ACCESS_TOKEN
_FULL_ACCESS_TOKEN_FINGERPRINT = hashlib.sha256(
    _FULL_ACCESS_TOKEN.encode("utf-8")
).hexdigest()


def _nearest_project_root(anchor: pathlib.Path) -> pathlib.Path | None:
    try:
        location = anchor.expanduser().resolve()
    except OSError:
        return None
    start = location if location.is_dir() else location.parent
    for candidate in (start, *start.parents):
        try:
            if any(candidate.glob("*.uproject")):
                return candidate
            if (candidate / "project.godot").is_file():
                return candidate
            if (candidate / "Assets").is_dir() and (
                candidate / "ProjectSettings"
            ).is_dir():
                return candidate
        except OSError:
            continue
    return None


def _resolve_runtime_state() -> tuple[pathlib.Path, bool]:
    """Return the runtime state and whether plugin-level legacy config is valid."""
    configured = os.environ.get("REVVY_STATE_DIR")
    if configured:
        return pathlib.Path(configured).expanduser().resolve(), False
    configured_project = str(os.environ.get("REVVY_PROJECT_DIR") or "").strip()
    if configured_project:
        return (
            pathlib.Path(configured_project).expanduser().resolve() / ".revvy",
            False,
        )
    anchors = [
        pathlib.Path(sys.executable)
        if getattr(sys, "frozen", False)
        else pathlib.Path(__file__),
        pathlib.Path.cwd(),
    ]
    for anchor in anchors:
        project = _nearest_project_root(anchor)
        if project is not None:
            return project / ".revvy", False
    base = os.environ.get("APPDATA") or os.environ.get("XDG_STATE_HOME")
    state_root: pathlib.Path
    if base:
        state_root = pathlib.Path(base) / "Revvy"
    else:
        state_root = pathlib.Path.home() / ".local" / "state" / "Revvy"
    allow_legacy = not bool(str(os.environ.get("REVVY_CONFIG") or "").strip())
    return state_root, allow_legacy


def _state_dir() -> pathlib.Path:
    return _resolve_runtime_state()[0]


STATE_DIR, _ALLOW_LEGACY_CONFIG_FALLBACK = _resolve_runtime_state()
MANIFEST_PATH = STATE_DIR / "tools-manifest.json"
PROCESS_PATH = STATE_DIR / "proxy-process.json"
SKILLS_DIR = pathlib.Path(__file__).resolve().parent.parent / "Skills"
PYTHON_DIR = pathlib.Path(__file__).resolve().parent
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))
from revvy_mcp_upstreams import (
    MutationAcknowledgementLost,
    MutationDenied,
    ToolDefinition,
    UpstreamError,
    UpstreamManager,
)
from revvy_processes import terminate_process_tree
from revvy_secure_store import (
    SecureStoreError,
    ensure_bridge_hmac_key,
    read_bridge_hmac_key,
    retire_legacy_ue_backend_token,
    scrub_legacy_editor_api_keys,
)

_PROXY_CONFIG_PATH = pathlib.Path(
    os.environ.get("REVVY_CONFIG", str(STATE_DIR / "revvy-proxy.json"))
).expanduser()
UPSTREAM_CONFIG_PATH = pathlib.Path(
    os.environ.get("REVVY_MCP_UPSTREAMS_CONFIG", str(STATE_DIR / "mcp-upstreams.json"))
).expanduser()
UPSTREAM_MANAGER = UpstreamManager(UPSTREAM_CONFIG_PATH)
_LEGACY_CONFIG_PATH = pathlib.Path(__file__).parent.parent.parent / "revvy-proxy.json"
_CONFIG_LOCK = threading.RLock()
_GIT_LOCK = threading.Lock()
_VFX_ASSET_LOCKS_GUARD = threading.Lock()
_VFX_ASSET_LOCKS: dict[str, list] = {}
_DISCOVERY_CACHE_LOCK = threading.RLock()
_SKILL_CATALOG_BUILD_LOCK = threading.Lock()
_MANIFEST_CACHE_KEY = None
_MANIFEST_CACHE_VALUE: list = []
_SKILL_FRONTMATTER_CACHE: dict[str, tuple[tuple, dict]] = {}
_SKILL_CATALOG_CACHE_KEY = None
_SKILL_CATALOG_CACHE_VALUE: dict | None = None
_SKILL_CATALOG_CACHE_CONTEXT = None
_SKILL_CATALOG_LAST_SCAN = 0.0


def _runtime_config_source() -> pathlib.Path | None:
    if _PROXY_CONFIG_PATH.exists():
        return _PROXY_CONFIG_PATH
    if _ALLOW_LEGACY_CONFIG_FALLBACK and _LEGACY_CONFIG_PATH.exists():
        return _LEGACY_CONFIG_PATH
    return None


@contextlib.contextmanager
def _vfx_asset_transaction(asset_path: str):
    with _VFX_ASSET_LOCKS_GUARD:
        entry = _VFX_ASSET_LOCKS.get(asset_path)
        if entry is None:
            entry = [threading.Lock(), 0]
            _VFX_ASSET_LOCKS[asset_path] = entry
        entry[1] += 1
        lock = entry[0]
    try:
        with lock:
            yield
    finally:
        with _VFX_ASSET_LOCKS_GUARD:
            entry[1] -= 1
            if entry[1] == 0 and _VFX_ASSET_LOCKS.get(asset_path) is entry:
                _VFX_ASSET_LOCKS.pop(asset_path, None)


def _read_json(path: pathlib.Path) -> dict:
    for encoding in ("utf-8-sig", "utf-16", "utf-8"):
        try:
            with path.open("r", encoding=encoding) as handle:
                value = json.load(handle)
            return value if isinstance(value, dict) else {}
        except (UnicodeDecodeError, UnicodeError):
            continue
    return {}


def _write_public_json(path: pathlib.Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    temporary.write_text(
        json.dumps(payload, ensure_ascii=True, indent=2) + "\n",
        encoding="utf-8",
    )
    os.replace(temporary, path)


def migrate_legacy_backend_token() -> bool:
    """Move a pre-scope UE token out of public config before the proxy starts."""
    with _CONFIG_LOCK:
        source = _runtime_config_source()
        if source is None:
            return False
        try:
            payload = _read_json(source)
        except (OSError, ValueError, json.JSONDecodeError):
            return False
        token = str(payload.get("bearer_token") or "").strip()
        scope = str(payload.get("bearer_scope") or "").strip().lower()
        if not token or scope == "read_only":
            return False
        error: Exception | None = None
        if len(token) >= 32:
            try:
                # Never reuse the former wire bearer as the v2 HMAC key.
                ensure_bridge_hmac_key()
            except (OSError, SecureStoreError) as exc:
                error = exc
            try:
                scrub_legacy_editor_api_keys(payload, require_project=True)
            except SecureStoreError as exc:
                error = error or exc
        else:
            scrub_legacy_editor_api_keys(payload, require_project=False)
        payload["bearer_token"] = ""
        payload["bearer_scope"] = "read_only"
        payload.pop("full_access_token", None)
        _write_public_json(_PROXY_CONFIG_PATH, payload)
        if source != _PROXY_CONFIG_PATH:
            try:
                _write_public_json(source, payload)
            except OSError:
                pass
        if error is not None:
            raise SecureStoreError(
                f"Could not retire the legacy editor bearer token: {error}"
            ) from error
        return True


def load_runtime_config() -> dict:
    """Reload routing and credentials so an editor restart needs no proxy restart."""
    config = {
        "config_version": 2,
        "proxy_port": DEFAULT_PROXY_PORT,
        "ue_port": DEFAULT_UE_PORT,
        "bearer_token": "",
        "bearer_scope": "",
    }
    with _CONFIG_LOCK:
        source = _runtime_config_source()
        if source is not None:
            try:
                config.update(_read_json(source))
            except (OSError, ValueError, json.JSONDecodeError) as exc:
                config["config_error"] = f"Could not read {source}: {exc}"
    for key, default in (("proxy_port", DEFAULT_PROXY_PORT), ("ue_port", DEFAULT_UE_PORT)):
        raw_value = config.get(key, default)
        if isinstance(raw_value, bool):
            config[key] = default
            continue
        if not isinstance(raw_value, int):
            config[key] = default
            continue
        value = raw_value
        config[key] = value if 1 <= value <= 65535 else default
    config["bearer_token"] = str(config.get("bearer_token", ""))
    config["bearer_scope"] = str(config.get("bearer_scope", "")).strip().lower()
    if config["bearer_scope"] != "read_only":
        config["bearer_token"] = ""
    try:
        config["_bridge_hmac_key"] = read_bridge_hmac_key()
    except (OSError, SecureStoreError) as exc:
        config["_bridge_hmac_key"] = ""
        config["security_error"] = str(exc)
    allowed_origins = config.get("allowed_origins", [])
    config["allowed_origins"] = (
        [origin for origin in allowed_origins if isinstance(origin, str) and origin]
        if isinstance(allowed_origins, list)
        else []
    )
    config["config_path"] = str(_runtime_config_source() or _PROXY_CONFIG_PATH)
    return config


_STARTUP_CONFIG = load_runtime_config()
PROXY_PORT = _STARTUP_CONFIG["proxy_port"]

# ---------------------------------------------------------------------------
# Synthetic tools — always present in tools/list regardless of manifest state.
# Handled entirely by the proxy; never forwarded to UE.
# Keep descriptions SHORT — they are paid as tokens on every Claude request.
# ---------------------------------------------------------------------------

SYNTHETIC_TOOLS: list[dict] = [
    {
        "name": "revvy_status",
        "description": (
            "Check Revvy setup status and get next steps. "
            "Call this first if tools are not responding or UE seems unreachable. "
            "For first-time setup, read Plugins/Revvy/Content/Setup/Claude_MCP_Setup.md."
        ),
        "inputSchema": {"type": "object", "properties": {}, "required": []},
        "annotations": {"readOnlyHint": True},
    },
    {
        "name": "manage_skills",
        "description": (
            "List, suggest, or load Revvy skill documentation. "
            "Works while Unreal Editor is restarting or offline."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "action": {"type": "string", "enum": ["list", "suggest", "load"]},
                "skill_name": {"type": "string"},
                "skill_names": {"type": "array", "items": {"type": "string"}},
                "query": {"type": "string"},
                "top_k": {"type": "integer", "minimum": 1, "maximum": 20},
                "detail": {
                    "type": "string",
                    "enum": ["summary", "full"],
                    "description": "For action=list; summary is the compact default.",
                },
            },
            "required": ["action"],
        },
        "annotations": {"readOnlyHint": True},
    },
    {
        "name": "manage_upstreams",
        "description": (
            "List external MCP bridges or activate one typed namespace on demand. "
            "Activate Blender before loading its Blender skill or calling blender__ tools."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "action": {"type": "string", "enum": ["list", "activate"]},
                "namespace": {
                    "type": "string",
                    "pattern": "^[A-Za-z][A-Za-z0-9_-]{0,31}$",
                },
            },
            "required": ["action"],
            "additionalProperties": False,
        },
        "annotations": {
            "readOnlyHint": False,
            "destructiveHint": False,
            "idempotentHint": False,
            "openWorldHint": True,
            "readOnlyActions": ["list"],
        },
    },
    {
        "name": "git_status",
        "description": (
            "Inspect Git status for the configured Unreal project or its bundled "
            "Plugins/Revvy repository, without modifying the repository."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "project_path": {"type": "string", "minLength": 1},
            },
            "required": ["project_path"],
        },
        "annotations": {"readOnlyHint": True},
    },
    {
        "name": "git_asset_history",
        "description": (
            "Inspect commit history for exact Unreal asset paths in a configured project, "
            "without modifying the repository or any stash."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "project_path": {"type": "string", "minLength": 1},
                "paths": {
                    "type": "array",
                    "items": {"type": "string", "minLength": 1},
                    "minItems": 1,
                    "maxItems": 64,
                    "uniqueItems": True,
                },
            },
            "required": ["project_path", "paths"],
        },
        "annotations": {"readOnlyHint": True},
    },
    {
        "name": "git_lfs_object_status",
        "description": (
            "Verify exact orphaned Git LFS objects against missing Unreal asset paths, "
            "sizes, SHA-256 object IDs, and embedded asset-name markers without writing files."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "project_path": {"type": "string", "minLength": 1},
                "items": {
                    "type": "array",
                    "minItems": 1,
                    "maxItems": 64,
                    "items": {
                        "type": "object",
                        "properties": {
                            "path": {"type": "string", "minLength": 1},
                            "oid": {"type": "string", "pattern": "^[0-9a-fA-F]{64}$"},
                            "size": {"type": "integer", "minimum": 1},
                        },
                        "required": ["path", "oid", "size"],
                        "additionalProperties": False,
                    },
                },
            },
            "required": ["project_path", "items"],
        },
        "annotations": {"readOnlyHint": True},
    },
    {
        "name": "git_lfs_marker_search",
        "description": (
            "Search the local Git LFS cache for exact Unreal asset-name markers. "
            "Returns only verified object IDs, sizes, and matched marker names. "
            "The scan is bounded; check scan_truncated before concluding a marker is absent."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "project_path": {"type": "string", "minLength": 1},
                "markers": {
                    "type": "array",
                    "items": {
                        "type": "string",
                        "pattern": "^[A-Za-z0-9_]{1,200}$",
                    },
                    "minItems": 1,
                    "maxItems": 64,
                    "uniqueItems": True,
                },
            },
            "required": ["project_path", "markers"],
        },
        "annotations": {"readOnlyHint": True},
    },
    {
        "name": "git_restore_lfs_objects",
        "description": (
            "Atomically restore verified orphaned Git LFS objects to exact missing Unreal "
            "asset paths. Never overwrites, stages, commits, stashes, checks out, or cleans files."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "project_path": {"type": "string", "minLength": 1},
                "items": {
                    "type": "array",
                    "minItems": 1,
                    "maxItems": 64,
                    "items": {
                        "type": "object",
                        "properties": {
                            "path": {"type": "string", "minLength": 1},
                            "oid": {"type": "string", "pattern": "^[0-9a-fA-F]{64}$"},
                            "size": {"type": "integer", "minimum": 1},
                        },
                        "required": ["path", "oid", "size"],
                        "additionalProperties": False,
                    },
                },
            },
            "required": ["project_path", "items"],
        },
        "annotations": {"readOnlyHint": False},
    },
    {
        "name": "git_sync_ff_only",
        "description": (
            "Safely synchronize a clean configured Unreal project or its bundled Plugins/Revvy "
            "repository: fetch its tracked remote and fast-forward only. Refuses dirty, detached, or "
            "diverged repositories; never stashes, resets, cleans, rebases, or creates a merge commit."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "project_path": {"type": "string", "minLength": 1},
            },
            "required": ["project_path"],
        },
        "annotations": {"readOnlyHint": False},
    },
    {
        "name": "git_publish_paths",
        "description": (
            "Commit only explicitly listed files in a configured Unreal project or its bundled "
            "Plugins/Revvy repository and push the current branch. Existing local commits require "
            "allow_existing_ahead; a branch that is behind its upstream requires "
            "allow_upstream_rebase, which rebuilds the commit on top of upstream and "
            "fast-forwards this branch. Diverged branches are always refused, and unrelated "
            "working-tree changes are left untouched."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "project_path": {"type": "string", "minLength": 1},
                "paths": {
                    "type": "array",
                    "items": {"type": "string", "minLength": 1},
                    "minItems": 1,
                    "maxItems": 128,
                    "uniqueItems": True,
                },
                "message": {"type": "string", "minLength": 1, "maxLength": 200},
                "allow_existing_ahead": {"type": "boolean"},
                "allow_upstream_rebase": {"type": "boolean"},
            },
            "required": ["project_path", "paths", "message", "allow_existing_ahead"],
        },
        "annotations": {"readOnlyHint": False},
    },
    {
        "name": "generate_vfx_texture",
        "description": (
            "Generate a quality-gated Niagara texture with the local Codex/ChatGPT login, "
            "then import and configure it in Unreal. No API key is used."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "subject": {"type": "string", "minLength": 3, "maxLength": 1200},
                "usage": {
                    "type": "string",
                    "enum": [
                        "sprite",
                        "additive",
                        "mask",
                        "tileable_noise",
                        "flowmap",
                        "normal",
                        "flipbook",
                    ],
                },
                "style": {"type": "string", "maxLength": 1200},
                "destination_path": {
                    "type": "string",
                    "maxLength": 512,
                    "description": "/Game/... folder",
                },
                "asset_name": {
                    "type": "string",
                    "maxLength": 128,
                    "description": "T_... asset name",
                },
                "size": {"type": "integer", "enum": [512, 1024], "default": 1024},
                "grid_columns": {
                    "type": "integer",
                    "minimum": 1,
                    "maximum": 8,
                    "default": 1,
                },
                "grid_rows": {
                    "type": "integer",
                    "minimum": 1,
                    "maximum": 8,
                    "default": 1,
                },
                "quality_threshold": {
                    "type": "integer",
                    "minimum": 50,
                    "maximum": 95,
                    "default": 72,
                },
                "attempts": {
                    "type": "integer",
                    "minimum": 1,
                    "maximum": 3,
                    "default": 2,
                },
                "model": {
                    "type": "string",
                    "minLength": 1,
                    "maxLength": 128,
                    "pattern": "^[A-Za-z0-9][A-Za-z0-9._:/-]{0,127}$",
                    "description": (
                        "Optional exact Codex model identifier, for example gpt-5.6-sol."
                    ),
                },
                "replace_existing": {
                    "type": "boolean",
                    "default": False,
                    "description": (
                        "Must remain false. Transactional Unreal asset replacement is "
                        "not available, so true is rejected before generation or import."
                    ),
                },
            },
            "required": ["subject", "usage", "destination_path", "asset_name"],
            "additionalProperties": False,
        },
        "annotations": {"readOnlyHint": False},
    },
]


def handle_revvy_status(req_id) -> dict:
    """Synthetic tool: check UE reachability, token config, and manifest state."""
    config = load_runtime_config()
    upstreams = UPSTREAM_MANAGER.health_snapshot()
    ue_running = _ue_running(config, timeout=2.0)
    configured_active_project = _project_from_config(config, "active_project")
    active_project = configured_active_project if ue_running else None
    installed_project = _project_from_config(config, "installed_project")

    token_set = bool(config.get("_bridge_hmac_key"))
    manifest_found = MANIFEST_PATH.exists()
    manifest_tools = len(load_manifest()) if manifest_found else 0

    if ue_running and token_set and manifest_found:
        text = (
            f"Revvy is ready.\n"
            f"  UE running:     yes\n"
            f"  Token set:      yes\n"
            f"  Manifest:       {manifest_tools} tools loaded\n"
        )
    else:
        steps = []
        n = 1
        if not token_set:
            steps.append(
                f"{n}. Start Revvy once so it can provision the local bridge authentication key at:\n"
                f"     {config['config_path']}"
            )
            n += 1
        if not ue_running:
            steps.append(f"{n}. Launch Unreal Engine with the Revvy plugin enabled.")
            n += 1
        if not manifest_found:
            steps.append(
                f"{n}. Launch UE once with Revvy enabled to generate the tool manifest at:\n"
                f"     {MANIFEST_PATH}"
            )
        text = (
            "Revvy setup incomplete. For guided setup follow: Plugins/Revvy/Content/Setup/Claude_MCP_Setup.md\n\n"
            "Remaining steps:\n"
            + "\n".join(steps)
        )

    if active_project:
        text += (
            f"\n  Active project:  {active_project.get('name') or '(unnamed)'}"
            f" ({active_project.get('path') or 'path unavailable'})\n"
        )
    else:
        text += "\n  Active project:  none\n"
    if installed_project:
        text += (
            f"  Installed project: {installed_project.get('name') or '(unnamed)'}"
            f" ({installed_project.get('path') or 'path unavailable'})\n"
        )
    text += f"  Engine:          {get_active_engine()}\n"
    if upstreams.get("config_error"):
        text += f"  External MCP:   configuration error ({upstreams['config_error']})\n"
    elif upstreams.get("servers"):
        text += "  External MCP:\n"
        for name, snapshot in upstreams["servers"].items():
            state = str(snapshot.get("state") or "configured")
            tool_count = (
                snapshot.get("configured_tools", 0)
                if state == "dormant"
                else snapshot.get("tools", 0)
            )
            detail = f", {tool_count} tools"
            if snapshot.get("last_error"):
                detail += f", error: {snapshot['last_error']}"
            text += f"    {name}: {state}{detail}\n"

    return {
        "jsonrpc": "2.0",
        "id": req_id,
        "result": {"content": [{"type": "text", "text": text}]},
    }


# ---------------------------------------------------------------------------
# Tool-description hints
#
# Appended to the manifest descriptions before serving tools/list.
# Keeps guidance close to the tool without requiring C++ or UE restarts.
#
# TOKEN COST: every hint is injected into every Claude context window.
# Keep hints focused — move deep reference material to skills instead.
# ---------------------------------------------------------------------------

# Maps tool name -> text appended to the existing description.
TOOL_HINTS: dict[str, str] = {

    "execute_python_code": """

IMMEDIATE ACTIONS — go straight to these, no skill or discovery step needed:

  User asks to list / show / describe components in a Widget Blueprint (WBP):
      import unreal
      components = unreal.WidgetService.list_components("/Game/path/WBP_X")
      print(components)
      Returns Array[WidgetInfo]: hierarchy + parent/child relationships, lightweight.
      USE THIS as the default for "what's in this widget" questions.
      DO NOT use get_hierarchy (names only) or get_widget_snapshot (full props, token-heavy) for hierarchy queries.

  User needs widget PROPERTIES (bindings, slot anchors, visibility, is_variable etc.):
      import unreal
      snapshot = unreal.WidgetService.get_widget_snapshot("/Game/path/WBP_X")
      print(snapshot)
      Returns full hierarchy + slot info + ALL properties — use only when property data is needed.
      Snapshot format: flat list of widget dicts. children are NAME STRINGS, not nested objects.
      Build a lookup dict to traverse:
          lookup = {w["name"]: w for w in snapshot}
          def children_of(name): return [lookup[c] for c in lookup[name]["children"] if c in lookup]
      Property access: snapshot[i]["properties"] → list of {name, value} dicts
      is_variable=True means the widget is a BindWidget target in C++.

  User asks to compile a Blueprint:
      import unreal
      unreal.BlueprintService.compile_blueprint("/Game/path/BP_X")

  User asks to list graphs in a Blueprint:
      import unreal
      unreal.BlueprintService.list_graphs("/Game/path/BP_X")

KEY RULES:
  - Assets (search/find/open/save/move/delete) → use the manage_asset TOOL, not Python.
  - Logs → use the read_logs TOOL, not Python file I/O.
  - Subsystems → unreal.get_editor_subsystem(unreal.SubsystemName)
    Verify current editor-subsystem methods; do not rely on deprecated unreal.EditorLevelLibrary.
  - For complex graph authoring or unfamiliar services → load the relevant skill first.""",

    "manage_skills": """

The catalogue is dynamic. Use action='suggest' with the current task, then load only
the smallest matching index. Read available_sections before loading deeper material.
Use revvy-external-control for terminal, MCP, connection, or editor-restart work.
Skill list, suggest, and load remain available while Unreal Editor is offline.""",

}


def apply_hints(tools: list) -> list:
    """Append proxy-side guidance to tool descriptions before serving tools/list."""
    for tool in tools:
        hint = TOOL_HINTS.get(tool.get("name", ""))
        if hint:
            tool = dict(tool)  # shallow copy — don't mutate the cached manifest
            tool["description"] = tool.get("description", "") + hint
        yield tool


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def log(msg: str) -> None:
    ts = datetime.now(timezone.utc).strftime("%H:%M:%S")
    stream = sys.stdout
    if stream is None:
        return
    line = f"[{ts}] {msg}"
    try:
        print(line, flush=True)
    except UnicodeEncodeError:
        encoding = getattr(stream, "encoding", None) or "utf-8"
        print(line.encode(encoding, errors="replace").decode(encoding), flush=True)


_PROTOCOL_VERSION_META_KEY = "io.modelcontextprotocol/protocolVersion"
_CLIENT_INFO_META_KEY = "io.modelcontextprotocol/clientInfo"
_CLIENT_CAPABILITIES_META_KEY = "io.modelcontextprotocol/clientCapabilities"
_LOG_LEVEL_META_KEY = "io.modelcontextprotocol/logLevel"
_SERVER_INFO_META_KEY = "io.modelcontextprotocol/serverInfo"
_MODERN_LOCAL_METHODS = frozenset(
    {
        "server/discover",
        "tools/list",
        "tools/call",
        "resources/list",
        "resources/templates/list",
        "resources/read",
    }
)
_MODERN_NAMED_METHOD_FIELDS = {
    "tools/call": "name",
    "resources/read": "uri",
    "prompts/get": "name",
}
_HTTP_FIELD_TOKEN_RE = re.compile(r"^[!#$%&'*+\-.^_`|~0-9A-Za-z]+$")
_MAX_SAFE_INTEGER = (1 << 53) - 1
_MISSING = object()


def _decode_mcp_header_value(value: str) -> str:
    """Decode the 2026 MCP Base64 sentinel used by Mcp-Name headers."""
    if not value.startswith("=?base64?") or not value.endswith("?="):
        return value
    encoded = value[len("=?base64?") : -2]
    try:
        return base64.b64decode(encoded, validate=True).decode("utf-8")
    except (binascii.Error, UnicodeDecodeError) as exc:
        raise ValueError("invalid MCP Base64 header value") from exc


def _modern_request_validation(rpc: dict, headers: dict) -> tuple[bool, dict | None]:
    """Classify and validate a stateless MCP 2026-07-28 HTTP request.

    Legacy requests deliberately bypass this path. A request that claims the
    modern envelope in either its body or mirrored HTTP headers is kept on the
    modern path even when malformed, so clients receive a useful protocol error
    instead of an unsafe fallback to the session-era bridge.
    """
    params = rpc.get("params")
    params = params if isinstance(params, dict) else {}
    meta = params.get("_meta")
    body_version = (
        meta.get(_PROTOCOL_VERSION_META_KEY) if isinstance(meta, dict) else None
    )
    header_version = str(headers.get("mcp-protocol-version") or "").strip()
    modern_claim = bool(
        body_version is not None
        or header_version == MODERN_PROTOCOL_VERSION
        or (header_version and header_version not in SUPPORTED_PROTOCOL_VERSIONS)
        or "mcp-method" in headers
        or "mcp-name" in headers
    )
    if not modern_claim:
        return False, None

    if not isinstance(meta, dict):
        return True, {
            "code": -32602,
            "message": "Modern MCP requests require params._meta",
            "status": 400,
        }
    if not isinstance(body_version, str) or not body_version:
        return True, {
            "code": -32602,
            "message": f"Missing {_PROTOCOL_VERSION_META_KEY} in params._meta",
            "status": 400,
        }
    if not header_version or header_version != body_version:
        return True, {
            "code": -32020,
            "message": (
                "Header mismatch: MCP-Protocol-Version must match "
                f"params._meta.{_PROTOCOL_VERSION_META_KEY}"
            ),
            "status": 400,
        }
    if body_version != MODERN_PROTOCOL_VERSION:
        return True, {
            "code": -32022,
            "message": f"Unsupported MCP protocol version: {body_version}",
            "status": 400,
            "data": {
                "requested": body_version,
                "supported": [MODERN_PROTOCOL_VERSION],
            },
        }

    method = str(rpc.get("method") or "")
    method_header = headers.get("mcp-method")
    if isinstance(method_header, str):
        method_header = method_header.strip(" \t")
    if method_header != method:
        return True, {
            "code": -32020,
            "message": "Header mismatch: Mcp-Method must match the JSON-RPC method",
            "status": 400,
        }

    named_field = _MODERN_NAMED_METHOD_FIELDS.get(method)
    if named_field:
        expected_name = params.get(named_field)
        encoded_name = headers.get("mcp-name")
        if isinstance(encoded_name, str):
            encoded_name = encoded_name.strip(" \t")
        try:
            actual_name = (
                _decode_mcp_header_value(encoded_name)
                if isinstance(encoded_name, str)
                else None
            )
        except ValueError:
            actual_name = None
        if not isinstance(expected_name, str) or actual_name != expected_name:
            return True, {
                "code": -32020,
                "message": (
                    f"Header mismatch: Mcp-Name must match params.{named_field}"
                ),
                "status": 400,
            }

    client_capabilities = meta.get(_CLIENT_CAPABILITIES_META_KEY)
    if not isinstance(client_capabilities, dict):
        return True, {
            "code": -32602,
            "message": f"Missing or invalid {_CLIENT_CAPABILITIES_META_KEY}",
            "status": 400,
        }
    client_info = meta.get(_CLIENT_INFO_META_KEY)
    if client_info is not None and (
        not isinstance(client_info, dict)
        or not isinstance(client_info.get("name"), str)
        or not isinstance(client_info.get("version"), str)
    ):
        return True, {
            "code": -32602,
            "message": f"Invalid {_CLIENT_INFO_META_KEY}",
            "status": 400,
        }
    return True, None


def _contains_mcp_header_annotation(value) -> bool:
    if isinstance(value, dict):
        return "x-mcp-header" in value or any(
            _contains_mcp_header_annotation(item) for item in value.values()
        )
    if isinstance(value, list):
        return any(_contains_mcp_header_annotation(item) for item in value)
    return False


def _tool_header_bindings(tool: dict) -> list[tuple[str, tuple[str, ...], str]]:
    """Return validated x-mcp-header bindings reachable through properties only."""
    schema = tool.get("inputSchema")
    if not isinstance(schema, dict):
        return []

    bindings: list[tuple[str, tuple[str, ...], str]] = []
    seen_headers: set[str] = set()

    def walk(node: dict, path: tuple[str, ...], *, is_property: bool) -> None:
        if "x-mcp-header" in node:
            header_name = node.get("x-mcp-header")
            value_type = node.get("type")
            if not is_property:
                raise ValueError("x-mcp-header is not on a statically reachable property")
            if (
                not isinstance(header_name, str)
                or not header_name
                or _HTTP_FIELD_TOKEN_RE.fullmatch(header_name) is None
            ):
                raise ValueError("x-mcp-header must be a non-empty HTTP field-name token")
            if value_type not in {"string", "integer", "boolean"}:
                raise ValueError(
                    "x-mcp-header properties must have string, integer, or boolean type"
                )
            folded = header_name.casefold()
            if folded in seen_headers:
                raise ValueError("x-mcp-header names must be case-insensitively unique")
            seen_headers.add(folded)
            bindings.append((header_name, path, value_type))

        properties = node.get("properties")
        if isinstance(properties, dict):
            for property_name, property_schema in properties.items():
                if not isinstance(property_name, str):
                    continue
                if isinstance(property_schema, dict):
                    walk(property_schema, path + (property_name,), is_property=True)
                elif _contains_mcp_header_annotation(property_schema):
                    raise ValueError("x-mcp-header property schema is invalid")

        for keyword, child in node.items():
            if keyword in {"x-mcp-header", "properties"}:
                continue
            if _contains_mcp_header_annotation(child):
                raise ValueError(
                    "x-mcp-header is reachable through a forbidden schema keyword"
                )

    walk(schema, (), is_property=False)
    return bindings


def _argument_at_path(arguments: dict, path: tuple[str, ...]):
    current = arguments
    for part in path:
        if not isinstance(current, dict) or part not in current:
            return _MISSING
        current = current[part]
    return current


def _header_parameter_text(value, value_type: str) -> str:
    if value_type == "string" and isinstance(value, str):
        return value
    if value_type == "boolean" and isinstance(value, bool):
        return "true" if value else "false"
    if (
        value_type == "integer"
        and isinstance(value, int)
        and not isinstance(value, bool)
        and -_MAX_SAFE_INTEGER <= value <= _MAX_SAFE_INTEGER
    ):
        return str(value)
    raise ValueError(f"body value does not match declared {value_type} header type")


def _modern_tool_header_validation(
    arguments: dict, headers: dict, tool: dict
) -> dict | None:
    """Validate SEP-2243 Mcp-Param-* mirrors for one modern tool call."""
    try:
        bindings = _tool_header_bindings(tool)
    except ValueError as exc:
        return {
            "code": -32602,
            "message": f"Tool has an invalid x-mcp-header schema: {exc}",
            "status": 400,
        }

    for header_name, path, value_type in bindings:
        wire_name = f"mcp-param-{header_name}".casefold()
        body_value = _argument_at_path(arguments, path)
        raw_header = headers.get(wire_name)
        if body_value is _MISSING or body_value is None:
            if raw_header is not None:
                return {
                    "code": -32020,
                    "message": f"Header mismatch: Mcp-Param-{header_name} has no body value",
                    "status": 400,
                }
            continue
        try:
            expected = _header_parameter_text(body_value, value_type)
        except ValueError as exc:
            return {"code": -32602, "message": str(exc), "status": 400}
        if not isinstance(raw_header, str):
            return {
                "code": -32020,
                "message": f"Header mismatch: missing Mcp-Param-{header_name}",
                "status": 400,
            }
        wire_value = raw_header.strip(" \t")
        try:
            actual = _decode_mcp_header_value(wire_value)
        except ValueError:
            actual = None
        if actual != expected:
            return {
                "code": -32020,
                "message": f"Header mismatch: Mcp-Param-{header_name} does not match the body",
                "status": 400,
            }
    return None


def _modern_tools(tools: list[dict]) -> list[dict]:
    """Do not advertise malformed x-mcp-header schemas to modern HTTP clients."""
    valid = []
    for tool in tools:
        try:
            _tool_header_bindings(tool)
        except ValueError as exc:
            log(f"Warning: hiding modern tool {tool.get('name')!r}: {exc}")
            continue
        valid.append(tool)
    return valid


def _modernize_jsonrpc_response(data: dict) -> dict:
    """Add fields required on successful 2026-07-28 result envelopes."""
    if not isinstance(data.get("result"), dict):
        return data
    modern = dict(data)
    result = dict(data["result"])
    result.setdefault("resultType", "complete")
    meta = result.get("_meta")
    meta = dict(meta) if isinstance(meta, dict) else {}
    meta.setdefault(_SERVER_INFO_META_KEY, dict(SERVER_INFO))
    result["_meta"] = meta
    modern["result"] = result
    return modern


def _legacy_bridge_request(rpc: dict, headers: dict) -> tuple[bytes, dict]:
    """Translate one stateless 2026 request for the session-era editor bridge."""
    bridge_rpc = copy.deepcopy(rpc)
    params = bridge_rpc.get("params")
    if isinstance(params, dict):
        meta = params.get("_meta")
        if isinstance(meta, dict):
            legacy_meta = dict(meta)
            for key in (
                _PROTOCOL_VERSION_META_KEY,
                _CLIENT_INFO_META_KEY,
                _CLIENT_CAPABILITIES_META_KEY,
                _LOG_LEVEL_META_KEY,
            ):
                legacy_meta.pop(key, None)
            if legacy_meta:
                params["_meta"] = legacy_meta
            else:
                params.pop("_meta", None)
    bridge_headers = dict(headers)
    bridge_headers["mcp-protocol-version"] = LEGACY_BRIDGE_PROTOCOL_VERSION
    return (
        json.dumps(bridge_rpc, ensure_ascii=False).encode("utf-8"),
        bridge_headers,
    )


def _same_project_path(left, right) -> bool:
    if not left or not right:
        return False
    try:
        left_path = pathlib.Path(os.fspath(left)).expanduser()
        right_path = pathlib.Path(os.fspath(right)).expanduser()
        if not left_path.is_absolute() or not right_path.is_absolute():
            return False
        return os.path.normcase(str(left_path.resolve())) == os.path.normcase(
            str(right_path.resolve())
        )
    except (OSError, TypeError, ValueError):
        return False


def _configured_bridge_project(config: dict) -> pathlib.Path | None:
    """Return the runtime-owned project, preferring process/state identity."""
    configured_project = str(os.environ.get("REVVY_PROJECT_DIR") or "").strip()
    if configured_project:
        try:
            candidate = pathlib.Path(configured_project).expanduser()
            if candidate.is_absolute():
                return candidate.resolve()
        except (OSError, ValueError):
            return None
    if STATE_DIR.name.casefold() == ".revvy":
        try:
            return STATE_DIR.parent.resolve()
        except OSError:
            pass
    for key in ("installed_project", "active_project"):
        value = config.get(key)
        if isinstance(value, dict) and value.get("path"):
            try:
                candidate = pathlib.Path(str(value["path"])).expanduser()
                if candidate.is_absolute():
                    return candidate.resolve()
            except (OSError, ValueError):
                pass
    project_path = str(config.get("project_path") or "").strip()
    if project_path:
        try:
            candidate = pathlib.Path(project_path).expanduser()
            if candidate.is_absolute():
                return candidate.resolve()
        except (OSError, ValueError):
            pass
    return None


def _bridge_identity_matches(config: dict, result) -> bool:
    """Validate the exact engine/project before a privileged bridge request."""
    if not isinstance(result, dict):
        return False
    project = _configured_bridge_project(config)
    if project is None:
        return False
    expected_engine = str(config.get("engine") or "").strip().casefold()
    if expected_engine not in {"unreal", "unity", "godot"}:
        expected_engine = detect_engine_for_project(project) or ""
    reported_engine = str(result.get("engine") or "").strip().casefold()
    identity_matches = bool(
        expected_engine
        and reported_engine == expected_engine
        and _same_project_path(result.get("project_path"), project)
    )
    if not identity_matches:
        return False
    configured_instance = str(config.get("bridge_instance_id") or "").strip()
    reported_instance = str(result.get("bridge_instance_id") or "").strip()
    if configured_instance and reported_instance != configured_instance:
        return False
    reported_port = result.get("editor_port")
    if reported_port is not None:
        if isinstance(reported_port, bool) or not isinstance(reported_port, int):
            return False
        if reported_port != config.get("ue_port"):
            return False
    try:
        config_version = int(config.get("config_version", 0))
    except (TypeError, ValueError):
        config_version = 0
    if config_version >= 3 and (not configured_instance or not reported_instance):
        return False
    if config_version >= 3 and reported_port is None:
        return False
    return True


def _probe_editor_bridge_identity(
    config: dict, timeout: float = 0.5
) -> tuple[dict | None, str]:
    payload = json.dumps(
        {"jsonrpc": "2.0", "id": "revvy-health", "method": "ping"}
    ).encode("utf-8")
    headers = {
        "Content-Type": "application/json",
        "Accept": "application/json",
        "Connection": "close",
        "X-Revvy-Proxy": "true",
        "MCP-Protocol-Version": "2025-06-18",
    }
    # Probe with the public Safe capability.  The protected backend bearer is
    # sent only after this listener proves its engine and project identity.
    if config.get("bearer_scope") == "read_only" and config.get("bearer_token"):
        headers["Authorization"] = f"Bearer {config['bearer_token']}"
    try:
        request = urllib.request.Request(
            f"http://127.0.0.1:{config['ue_port']}/mcp",
            data=payload,
            headers=headers,
            method="POST",
        )
        with urllib.request.urlopen(request, timeout=timeout) as response:
            decoded = json.loads(response.read().decode("utf-8"))
        valid_envelope = (
            isinstance(decoded, dict)
            and decoded.get("jsonrpc") == "2.0"
            and decoded.get("id") == "revvy-health"
            and isinstance(decoded.get("result"), dict)
            and "error" not in decoded
        )
        if not valid_envelope:
            return None, "Revvy editor bridge returned an invalid ping response"
        if not _bridge_identity_matches(config, decoded["result"]):
            return (
                None,
                "Revvy editor bridge identity does not match this engine project",
            )
        return decoded["result"], ""
    except (OSError, ValueError, json.JSONDecodeError, urllib.error.URLError) as exc:
        return None, f"Revvy editor bridge is unreachable: {exc}"


def _probe_editor_bridge(
    config: dict, timeout: float = 0.5
) -> tuple[bool, str]:
    identity, error = _probe_editor_bridge_identity(config, timeout)
    return identity is not None, error


def _signed_bridge_headers(
    config: dict,
    identity: dict,
    body_bytes: bytes,
    *,
    access_scope: str,
) -> dict[str, str]:
    """Build a body/instance-bound one-use proxy credential.

    The protected backend capability never crosses the socket.  A listener
    swapped in after the Safe ping sees only a short-lived HMAC that cannot be
    used for a different body, project bridge instance, port, or access scope.
    """

    token = str(config.get("_bridge_hmac_key") or "").strip()
    if len(token) < 32:
        raise ValueError("Protected editor backend capability is unavailable")
    instance = str(identity.get("bridge_instance_id") or "").strip()
    if not instance:
        raise ValueError("Editor bridge did not publish a process instance identity")
    port = config.get("ue_port")
    if isinstance(port, bool) or not isinstance(port, int) or not 1 <= port <= 65535:
        raise ValueError("Editor bridge port is invalid")
    scope = str(access_scope or "").strip().lower()
    if scope not in {"read_only", "full"}:
        raise ValueError("Editor bridge access scope is invalid")
    timestamp = str(int(time.time()))
    nonce = secrets.token_hex(16)
    canonical_prefix = (
        f"revvy-proxy-{BRIDGE_SIGNATURE_VERSION}\n{scope}\n{instance}\n"
        f"{port}\n{timestamp}\n{nonce}\n{len(body_bytes)}\n"
    ).encode("utf-8")
    signature = hmac.new(
        token.encode("utf-8"), canonical_prefix + body_bytes, hashlib.sha1
    ).hexdigest()
    return {
        "X-Revvy-Bridge-Instance": instance,
        "X-Revvy-Access-Scope": scope,
        "X-Revvy-Request-Timestamp": timestamp,
        "X-Revvy-Request-Nonce": nonce,
        "X-Revvy-Request-Signature": f"{BRIDGE_SIGNATURE_VERSION}={signature}",
    }


def _verify_bridge_response(
    config: dict,
    identity: dict,
    request_headers: dict[str, str],
    request_body: bytes,
    response_body: bytes,
    response_headers,
    status: int,
) -> bool:
    """Authenticate a bridge response against the exact signed request."""

    token = str(config.get("_bridge_hmac_key") or "").strip()
    instance = str(identity.get("bridge_instance_id") or "").strip()
    scope = request_headers.get("X-Revvy-Access-Scope", "")
    timestamp = request_headers.get("X-Revvy-Request-Timestamp", "")
    nonce = request_headers.get("X-Revvy-Request-Nonce", "")
    signature = ""
    if response_headers is not None:
        try:
            signature = str(response_headers.get("X-Revvy-Response-Signature") or "")
        except AttributeError:
            signature = ""
    if not signature.startswith(f"{BRIDGE_SIGNATURE_VERSION}="):
        return False
    try:
        port = int(config.get("ue_port"))
    except (TypeError, ValueError):
        return False
    if isinstance(status, bool) or not isinstance(status, int):
        return False
    canonical_prefix = (
        f"revvy-bridge-response-{BRIDGE_SIGNATURE_VERSION}\n{scope}\n{instance}\n"
        f"{port}\n{timestamp}\n{nonce}\n{status}\n{len(request_body)}\n"
    ).encode("utf-8")
    canonical = (
        canonical_prefix
        + request_body
        + f"\n{len(response_body)}\n".encode("utf-8")
        + response_body
    )
    expected = hmac.new(token.encode("utf-8"), canonical, hashlib.sha1).hexdigest()
    return hmac.compare_digest(signature, f"{BRIDGE_SIGNATURE_VERSION}={expected}")


def _ue_running(config: dict, timeout: float = 0.5) -> bool:
    return _probe_editor_bridge(config, timeout)[0]


def _project_from_config(config: dict, key: str) -> dict | None:
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


def _redacted_upstreams(snapshot: dict) -> dict:
    """Liveness only. Third-party stderr and PIDs are not public information."""
    servers = snapshot.get("servers")
    redacted: dict = {}
    if isinstance(servers, dict):
        for namespace, server in servers.items():
            if not isinstance(server, dict):
                continue
            redacted[namespace] = {
                "configured": server.get("configured"),
                "running": server.get("running"),
                "ready": server.get("ready"),
                "tools": server.get("tools"),
                "retired": server.get("retired"),
                "activation": server.get("activation"),
                "activated": server.get("activated"),
                "state": server.get("state"),
                "catalog_visible": server.get("catalog_visible"),
                "configured_tools": server.get("configured_tools"),
            }
    return {"config_found": snapshot.get("config_found"), "servers": redacted}


def build_health(*, include_private: bool = False) -> dict:
    config = load_runtime_config()
    ue_running = _ue_running(config)
    configured_active_project = _project_from_config(config, "active_project")
    upstreams = UPSTREAM_MANAGER.health_snapshot()
    return {
        "service": "revvy-proxy",
        "version": "2.0.0",
        "engine": get_active_engine(),
        "proxy_api_version": PROXY_API_VERSION,
        "capabilities": list(PROXY_CAPABILITIES),
        "full_access_token_fingerprint": _FULL_ACCESS_TOKEN_FINGERPRINT,
        "pid": os.getpid(),
        "proxy_url": f"http://127.0.0.1:{PROXY_PORT}/mcp",
        "ue_url": f"http://127.0.0.1:{config['ue_port']}/mcp",
        "ue_running": ue_running,
        "manifest_tools": len(load_manifest()),
        "manifest_path": str(MANIFEST_PATH),
        "config_path": config["config_path"],
        "active_project": configured_active_project if ue_running else None,
        "last_active_project": configured_active_project,
        "installed_project": _project_from_config(config, "installed_project"),
        "upstreams": upstreams if include_private else _redacted_upstreams(upstreams),
        "restart_required": config["proxy_port"] != PROXY_PORT,
    }


def _proxy_health_at_port(port: int, timeout: float = 1.0) -> dict | None:
    try:
        request = urllib.request.Request(
            f"http://127.0.0.1:{port}/health",
            headers={"Accept": "application/json", "Connection": "close"},
        )
        with urllib.request.urlopen(request, timeout=timeout) as response:
            payload = json.loads(response.read().decode("utf-8"))
        if isinstance(payload, dict) and payload.get("service") == "revvy-proxy":
            return payload
    except (OSError, ValueError, json.JSONDecodeError, urllib.error.URLError):
        pass
    return None


def stop_stale_managed_proxy() -> dict | None:
    """Stop the previously managed proxy when configuration moves to a new port."""
    try:
        process_state = _read_json(PROCESS_PATH)
        stale_pid = int(process_state.get("pid", 0))
        stale_port = int(process_state.get("proxy_port", 0))
    except (OSError, TypeError, ValueError, json.JSONDecodeError):
        return None
    if stale_pid <= 0 or not 1 <= stale_port <= 65535 or stale_port == PROXY_PORT:
        return None

    health = _proxy_health_at_port(stale_port)
    reported_config = health.get("config_path") if health else None
    try:
        same_runtime = bool(reported_config) and os.path.normcase(
            os.path.abspath(os.fspath(reported_config))
        ) == os.path.normcase(os.path.abspath(os.fspath(_PROXY_CONFIG_PATH)))
    except (OSError, TypeError, ValueError):
        same_runtime = False
    if not health or health.get("pid") != stale_pid or not same_runtime:
        return None

    tree_stopped = terminate_process_tree(stale_pid)

    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline and _proxy_health_at_port(stale_port, timeout=0.2):
        time.sleep(0.1)
    if _proxy_health_at_port(stale_port, timeout=0.2):
        detail = "process tree stop failed; " if not tree_stopped else ""
        log(
            f"Stale proxy PID {stale_pid} on port {stale_port} did not stop "
            f"({detail}health still responds)"
        )
        return process_state
    log(f"Stopped stale proxy PID {stale_pid} after port changed to {PROXY_PORT}")
    return None


def _file_cache_key(path: pathlib.Path) -> tuple:
    """Cheap identity that changes when a discovery source is replaced or edited."""
    try:
        stat = path.stat()
    except OSError:
        return (str(path), None)
    return (
        str(path),
        stat.st_mtime_ns,
        stat.st_ctime_ns,
        stat.st_size,
        getattr(stat, "st_ino", 0),
    )


def _clear_discovery_caches() -> None:
    """Drop proxy discovery caches after tests or an explicit runtime refresh."""
    global _MANIFEST_CACHE_KEY, _MANIFEST_CACHE_VALUE
    global _SKILL_CATALOG_CACHE_KEY, _SKILL_CATALOG_CACHE_VALUE
    global _SKILL_CATALOG_CACHE_CONTEXT
    global _SKILL_CATALOG_LAST_SCAN
    with _DISCOVERY_CACHE_LOCK:
        _MANIFEST_CACHE_KEY = None
        _MANIFEST_CACHE_VALUE = []
        _SKILL_FRONTMATTER_CACHE.clear()
        _SKILL_CATALOG_CACHE_KEY = None
        _SKILL_CATALOG_CACHE_VALUE = None
        _SKILL_CATALOG_CACHE_CONTEXT = None
        _SKILL_CATALOG_LAST_SCAN = 0.0


def load_manifest() -> list:
    """Load tool definitions from the manifest file written by UE.
    Handles both UTF-8 and UTF-16 (UE writes UTF-16 by default).

    ``tools/list`` and typo suggestions are hot paths.  The editor publishes a
    new file when descriptors change, so the file identity is a precise and
    much cheaper invalidation key than reparsing JSON for every request.
    """
    global _MANIFEST_CACHE_KEY, _MANIFEST_CACHE_VALUE
    # The editor publishes by replacement. Bound retries so a continuously
    # changing file cannot grow the Python stack or monopolize discovery.
    for _attempt in range(3):
        cache_key = _file_cache_key(MANIFEST_PATH)
        with _DISCOVERY_CACHE_LOCK:
            if cache_key == _MANIFEST_CACHE_KEY:
                return copy.deepcopy(_MANIFEST_CACHE_VALUE)
            if cache_key[1] is None:
                _MANIFEST_CACHE_KEY = cache_key
                _MANIFEST_CACHE_VALUE = []
                return []

        loaded = None
        for encoding in ("utf-8-sig", "utf-16", "utf-8"):
            try:
                with open(MANIFEST_PATH, encoding=encoding) as f:
                    loaded = json.load(f)
                if not isinstance(loaded, list):
                    raise ValueError("tool manifest root must be an array")
                break
            except (UnicodeDecodeError, UnicodeError):
                continue
            except Exception as e:
                # A partial or invalid publication may be replaced immediately;
                # do not make that transient failure sticky in the cache.
                loaded = None
                log(f"Warning: could not read manifest ({encoding}): {e}")
                break
        if loaded is None:
            return []
        # Do not cache a read that raced an atomic editor rewrite.
        if _file_cache_key(MANIFEST_PATH) != cache_key:
            continue
        with _DISCOVERY_CACHE_LOCK:
            if _file_cache_key(MANIFEST_PATH) == cache_key:
                _MANIFEST_CACHE_KEY = cache_key
                _MANIFEST_CACHE_VALUE = loaded
                return copy.deepcopy(_MANIFEST_CACHE_VALUE)
    return []


def merged_tools(*, allow_upstream_start: bool = True) -> list[dict]:
    tools_by_name = {}
    for tool in apply_hints(load_manifest()):
        if not isinstance(tool, dict) or not tool.get("name"):
            continue
        name = str(tool["name"])
        if UPSTREAM_MANAGER.owns_namespace(name):
            continue
        tools_by_name[name] = tool
    for tool in SYNTHETIC_TOOLS:
        tools_by_name[tool["name"]] = tool
    for tool in UPSTREAM_MANAGER.tools(allow_start=allow_upstream_start):
        tools_by_name[tool["name"]] = tool
    # MCP 2026-07-28 explicitly recommends deterministic ordering so clients
    # can cache descriptors and LLM prompt prefixes reliably.
    return [
        tools_by_name[name]
        for name in sorted(tools_by_name, key=lambda value: (value.casefold(), value))
    ]


def _parse_response_paths(value) -> list[tuple[str, ...]]:
    if not isinstance(value, str):
        return []
    paths = []
    for raw_path in value.split(","):
        path = tuple(part.strip() for part in raw_path.strip().split("."))
        if path and all(path) and path not in paths:
            paths.append(path)
    return paths


def _extract_response_controls(arguments: dict) -> tuple[dict, list, list, bool]:
    has_controls = "_fields" in arguments or "_omit" in arguments
    if not has_controls:
        return arguments, [], [], False
    cleaned = dict(arguments)
    fields = _parse_response_paths(cleaned.pop("_fields", None))
    omit = _parse_response_paths(cleaned.pop("_omit", None))
    return cleaned, fields, omit, True


def _keep_json_paths(value, paths: list[tuple[str, ...]]):
    if isinstance(value, list):
        return [_keep_json_paths(item, paths) for item in value]
    if not isinstance(value, dict):
        return value
    kept = {}
    for key, child in value.items():
        matches = [path[1:] for path in paths if path[0] == key]
        if not matches:
            continue
        if () in matches:
            kept[key] = child
        elif isinstance(child, (dict, list)):
            kept[key] = _keep_json_paths(child, matches)
    return kept


def _omit_json_paths(value, paths: list[tuple[str, ...]]):
    if isinstance(value, list):
        return [_omit_json_paths(item, paths) for item in value]
    if not isinstance(value, dict):
        return value
    kept = {}
    for key, child in value.items():
        matches = [path[1:] for path in paths if path[0] == key]
        if () in matches:
            continue
        kept[key] = (
            _omit_json_paths(child, matches)
            if matches and isinstance(child, (dict, list))
            else child
        )
    return kept


def _shape_json_payload(payload, fields: list, omit: list):
    shaped = _keep_json_paths(payload, fields) if fields else payload
    # Omissions are applied last, so _omit wins whenever selectors overlap.
    return _omit_json_paths(shaped, omit) if omit else shaped


def _shape_tool_response(response: dict, fields: list, omit: list) -> dict:
    if not fields and not omit:
        return response
    if not isinstance(response, dict) or "error" in response:
        return response
    result = response.get("result")
    if isinstance(result, dict) and result.get("isError"):
        return response
    if not isinstance(result, (dict, list)):
        return response

    if not isinstance(result, dict) or not (
        "content" in result or "structuredContent" in result
    ):
        shaped = dict(response)
        shaped["result"] = _shape_json_payload(result, fields, omit)
        return shaped

    shaped_result = dict(result)
    changed = False
    structured = result.get("structuredContent")
    if isinstance(structured, (dict, list)):
        shaped_result["structuredContent"] = _shape_json_payload(
            structured, fields, omit
        )
        changed = True

    content = result.get("content")
    if isinstance(content, list):
        shaped_content = []
        for item in content:
            shaped_item = item
            if isinstance(item, dict) and isinstance(item.get("text"), str):
                try:
                    payload = json.loads(item["text"])
                except json.JSONDecodeError:
                    payload = None
                if isinstance(payload, (dict, list)):
                    shaped_item = dict(item)
                    shaped_item["text"] = json.dumps(
                        _shape_json_payload(payload, fields, omit),
                        ensure_ascii=False,
                    )
                    changed = True
            shaped_content.append(shaped_item)
        if changed:
            shaped_result["content"] = shaped_content

    if not changed:
        return response
    shaped = dict(response)
    shaped["result"] = shaped_result
    return shaped


def _tool_name_suggestions(tool_name: str) -> list[str] | None:
    names = []
    for tool in [*load_manifest(), *SYNTHETIC_TOOLS]:
        if not isinstance(tool, dict) or not tool.get("name"):
            continue
        name = str(tool["name"])
        if name not in names:
            names.append(name)
    if tool_name in names:
        return None
    return difflib.get_close_matches(tool_name, names, n=3)


def _add_did_you_mean(response: dict, tool_name: str) -> dict:
    if not isinstance(response, dict):
        return response
    error = response.get("error")
    result = response.get("result")
    if not isinstance(error, dict) and not (
        isinstance(result, dict) and result.get("isError")
    ):
        return response
    suggestions = _tool_name_suggestions(tool_name)
    if suggestions is None:
        return response

    if isinstance(error, dict):
        shaped = dict(response)
        shaped_error = dict(error)
        shaped_error["did_you_mean"] = suggestions
        shaped["error"] = shaped_error
        return shaped

    shaped_result = dict(result)
    changed = False
    structured = result.get("structuredContent")
    if isinstance(structured, dict):
        shaped_structured = dict(structured)
        shaped_structured["did_you_mean"] = suggestions
        shaped_result["structuredContent"] = shaped_structured
        changed = True
    content = result.get("content")
    if isinstance(content, list):
        shaped_content = []
        for item in content:
            shaped_item = item
            if isinstance(item, dict) and isinstance(item.get("text"), str):
                try:
                    payload = json.loads(item["text"])
                except json.JSONDecodeError:
                    payload = None
                if isinstance(payload, dict):
                    payload["did_you_mean"] = suggestions
                    shaped_item = dict(item)
                    shaped_item["text"] = json.dumps(payload, ensure_ascii=False)
                    changed = True
            shaped_content.append(shaped_item)
        if changed:
            shaped_result["content"] = shaped_content
    if not changed:
        shaped_result["did_you_mean"] = suggestions
    shaped = dict(response)
    shaped["result"] = shaped_result
    return shaped


def _tool_annotations(definition: dict | None) -> dict:
    if isinstance(definition, dict) and isinstance(definition.get("annotations"), dict):
        return definition["annotations"]
    return {}


def _tool_is_read_only(tool_name: str, definition: dict | None = None) -> bool:
    if UPSTREAM_MANAGER.owns_namespace(tool_name):
        route = UPSTREAM_MANAGER.routes.get(tool_name)
        return route is not None and not route.mutating
    annotations = _tool_annotations(definition)
    # Matches the editor-side rule (MCPServer.cpp IsSafeReadOnlyTool): a tool that
    # declares itself destructive is not read-only whatever else it claims.
    return (
        annotations.get("readOnlyHint") is True
        and annotations.get("destructiveHint") is not True
    )


def _tool_read_only_actions(tool_name: str, definition: dict | None = None) -> list[str]:
    """Query actions a mixed-verb tool still offers a read-only session."""
    if UPSTREAM_MANAGER.owns_namespace(tool_name):
        return []
    actions = _tool_annotations(definition).get("readOnlyActions")
    if not isinstance(actions, list):
        return []
    return [str(action) for action in actions if isinstance(action, str) and action]


def _tool_visible_to_safe(tool_name: str, definition: dict | None = None) -> bool:
    return bool(
        _tool_is_read_only(tool_name, definition)
        or _tool_read_only_actions(tool_name, definition)
    )


def _narrow_safe_action_enum(tool: dict, allowed: list[str]) -> dict | None:
    """Cut a tool's action enum down to `allowed`; None when nothing survives.

    A description sentence is a hint, an enum is a constraint, and a client that
    validates arguments against inputSchema only ever sees the enum. Narrowing it
    is what stops a safe session spending a call on an action that will be denied.

    Copies every level it descends through: merged_tools() hands out live
    references to SYNTHETIC_TOOLS and to the upstream cache, so writing the enum
    in place would rewrite the /mcp descriptor for the life of the process.
    """
    schema = tool.get("inputSchema")
    if not isinstance(schema, dict):
        return tool
    properties = schema.get("properties")
    if not isinstance(properties, dict):
        return tool
    action = properties.get("action")
    if not isinstance(action, dict) or not isinstance(action.get("enum"), list):
        return tool
    # Descriptor order, not annotation order — the enum is the bridge's own list.
    surviving = [value for value in action["enum"] if value in allowed]
    if not surviving:
        # No argument could satisfy an empty enum, so serving it would advertise
        # an uncallable tool. Defensive: no descriptor in any bridge hits this.
        return None
    if surviving == action["enum"]:
        return tool
    narrowed_action = dict(action)
    narrowed_action["enum"] = surviving
    narrowed_properties = dict(properties)
    narrowed_properties["action"] = narrowed_action
    narrowed_schema = dict(schema)
    narrowed_schema["properties"] = narrowed_properties
    narrowed_tool = dict(tool)
    narrowed_tool["inputSchema"] = narrowed_schema
    return narrowed_tool


def _annotate_safe_tool(tool: dict) -> dict | None:
    """Say which verbs survive, so the model does not spend a call finding out.

    None means no verb survived and the tool is dropped from the safe listing.
    """
    allowed = _tool_read_only_actions(str(tool.get("name") or ""), tool)
    if not allowed or _tool_is_read_only(str(tool.get("name") or ""), tool):
        return tool
    narrowed = _narrow_safe_action_enum(tool, allowed)
    if narrowed is None:
        return None
    narrowed = dict(narrowed)
    narrowed["description"] = (
        f"{tool.get('description', '')} Safe (read-only) session: only these actions "
        f"are available — {', '.join(allowed)}."
    ).strip()
    return narrowed


def _safe_action_denial(
    tool_name: str, definition: dict | None, arguments: dict
) -> str | None:
    """None when a read-only session may run this call, else the reason it may not."""
    if _tool_is_read_only(tool_name, definition):
        return None
    allowed = _tool_read_only_actions(tool_name, definition)
    if not allowed:
        return (
            "This Revvy session is Safe/read-only. "
            "Start a Full access session to run mutating tools."
        )
    action = str(arguments.get("action") or "").strip().lower()
    if action in allowed:
        return None
    return (
        f"Action '{action}' needs a Full access session. "
        f"This Safe session offers: {', '.join(allowed)}."
    )


def _yaml_scalar(value: str) -> str:
    value = value.strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in ("'", '"'):
        return value[1:-1]
    return value


def parse_skill_frontmatter(content: str) -> dict:
    """Parse the small YAML subset used by Revvy skills without dependencies."""
    if not content.startswith("---"):
        return {}
    end = content.find("\n---", 3)
    if end < 0:
        return {}
    result: dict = {}
    current_key = ""
    for line in content[3:end].splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        if line == line.lstrip() and ":" in line:
            key, value = line.split(":", 1)
            current_key = key.strip()
            result[current_key] = _yaml_scalar(value)
            continue
        if stripped.startswith("- ") and current_key:
            existing = result.get(current_key)
            if not isinstance(existing, list):
                existing = []
                result[current_key] = existing
            existing.append(_yaml_scalar(stripped[2:]))
        elif current_key and isinstance(result.get(current_key), str):
            continuation = _yaml_scalar(stripped)
            result[current_key] = " ".join(
                part for part in (result[current_key], continuation) if part
            )
    return result


def _read_skill_frontmatter(path: pathlib.Path) -> dict:
    """Read only one skill's YAML header and cache it until the file changes."""
    cache_key = _file_cache_key(path)
    cache_name = str(path)
    with _DISCOVERY_CACHE_LOCK:
        cached = _SKILL_FRONTMATTER_CACHE.get(cache_name)
        if cached is not None and cached[0] == cache_key:
            return cached[1]

    # Skill indexes can be large, but catalogue discovery only needs the first
    # frontmatter block. Avoid loading the complete workflow into memory here.
    lines: list[str] = []
    consumed = 0
    with open(path, encoding="utf-8-sig") as stream:
        first = stream.readline()
        if first.strip() != "---":
            metadata: dict = {}
        else:
            lines.append(first)
            consumed += len(first.encode("utf-8"))
            for line in stream:
                consumed += len(line.encode("utf-8"))
                if consumed > 64 * 1024:
                    # Malformed metadata should not turn listing into a full-file read.
                    lines = []
                    break
                lines.append(line)
                if line.strip() == "---":
                    break
            metadata = parse_skill_frontmatter("".join(lines)) if lines else {}

    with _DISCOVERY_CACHE_LOCK:
        if _file_cache_key(path) == cache_key:
            _SKILL_FRONTMATTER_CACHE[cache_name] = (cache_key, metadata)
    return metadata


def _is_link_like(path: pathlib.Path) -> bool:
    try:
        if path.is_symlink():
            return True
        is_junction = getattr(path, "is_junction", None)
        return bool(is_junction and is_junction())
    except OSError:
        return True


def _safe_skill_path(path: pathlib.Path, root: pathlib.Path) -> pathlib.Path | None:
    """Resolve an existing path only when no child component crosses a link."""
    try:
        lexical_root = pathlib.Path(os.path.abspath(root))
        lexical_path = pathlib.Path(os.path.abspath(path))
        current = lexical_path
        while current != lexical_root:
            if _is_link_like(current):
                return None
            parent = current.parent
            if parent == current:
                return None
            current = parent
        resolved_root = lexical_root.resolve(strict=True)
        resolved_path = lexical_path.resolve(strict=True)
    except (OSError, RuntimeError):
        return None
    if resolved_path != resolved_root and resolved_root not in resolved_path.parents:
        return None
    return resolved_path


def _skill_section_paths(skill_dir: pathlib.Path) -> list[pathlib.Path]:
    candidates = list(skill_dir.glob("*.md"))
    references_dir = skill_dir / "references"
    if (
        references_dir.is_dir()
        and _safe_skill_path(references_dir, skill_dir) is not None
    ):
        candidates.extend(references_dir.glob("*.md"))
    return sorted(
        (
            path
            for path in candidates
            if path.name.casefold() not in {"skill.md", "log.md"}
            and _safe_skill_path(path, skill_dir) is not None
        ),
        key=lambda path: (
            path.relative_to(skill_dir).as_posix().casefold(),
            path.relative_to(skill_dir).as_posix(),
        ),
    )


def enumerate_skill_sections(
    skill_dir: pathlib.Path,
    candidates: list[pathlib.Path] | None = None,
) -> list[dict]:
    sections: list[dict] = []
    for path in candidates if candidates is not None else _skill_section_paths(skill_dir):
        try:
            safe_skill_dir = _safe_skill_path(skill_dir, skill_dir)
            safe_path = _safe_skill_path(path, skill_dir)
            if safe_skill_dir is None or safe_path is None:
                continue
            relative = safe_path.relative_to(safe_skill_dir).with_suffix("").as_posix()
            metadata = _read_skill_frontmatter(safe_path)
        except (OSError, RuntimeError, UnicodeError, ValueError):
            continue
        section = {"name": relative}
        if metadata.get("description"):
            section["description"] = metadata["description"]
        sections.append(section)
    return sorted(sections, key=lambda item: (item["name"].casefold(), item["name"]))


KNOWN_ENGINES = ("unreal", "unity", "godot", "shared")


def detect_engine_for_project(path: str | pathlib.Path) -> str | None:
    """Best-effort engine detection from project directory markers."""
    if not path:
        return None
    try:
        project_dir = pathlib.Path(path)
        if not project_dir.is_dir():
            return None
        if any(project_dir.glob("*.uproject")):
            return "unreal"
        if (project_dir / "ProjectSettings").is_dir() and (project_dir / "Assets").is_dir():
            return "unity"
        if (project_dir / "project.godot").is_file():
            return "godot"
    except OSError:
        return None
    return None


def get_active_engine() -> str:
    """Active engine: config `engine` field, project path markers, else "unreal"."""
    config = load_runtime_config()
    configured = str(config.get("engine", "")).strip().casefold()
    if configured in KNOWN_ENGINES and configured != "shared":
        return configured
    for key in ("active_project", "last_active_project", "installed_project"):
        project = _project_from_config(config, key) or {}
        detected = detect_engine_for_project(str(project.get("path", "")))
        if detected:
            return detected
    return "unreal"


def _skill_engine_dirs() -> list[tuple[str, pathlib.Path]]:
    """Directories scanned for skills: the active engine first, then shared."""
    active = get_active_engine()
    engines = (active,) if active == "shared" else (active, "shared")
    result = []
    for engine in engines:
        engine_dir = SKILLS_DIR / engine
        if (
            engine_dir.is_dir()
            and _safe_skill_path(engine_dir, SKILLS_DIR) is not None
        ):
            result.append((engine, engine_dir))
    return result


def _skill_catalog_snapshot() -> tuple[tuple, list[tuple]]:
    """Return a deterministic change signature and sources for the active catalogue."""
    sources: list[tuple] = []
    signature: list[tuple] = []
    for engine, engine_dir in _skill_engine_dirs():
        try:
            skill_dirs = sorted(
                engine_dir.iterdir(), key=lambda path: (path.name.casefold(), path.name)
            )
        except OSError:
            continue
        for skill_dir in skill_dirs:
            skill_path = skill_dir / "SKILL.md"
            if (
                not skill_dir.is_dir()
                or not skill_path.is_file()
                or _safe_skill_path(skill_dir, engine_dir) is None
                or _safe_skill_path(skill_path, skill_dir) is None
            ):
                continue
            section_paths = _skill_section_paths(skill_dir)
            sources.append((engine, skill_dir, skill_path, section_paths))
            signature.append(
                (
                    engine,
                    skill_dir.name,
                    _file_cache_key(skill_path),
                    tuple(_file_cache_key(path) for path in section_paths),
                )
            )
    return tuple(signature), sources


def _skill_catalog_context() -> tuple[str, str]:
    """Cheap selectors that can change the active catalogue without file edits."""
    return str(SKILLS_DIR.resolve()), get_active_engine()


def list_skills_payload() -> dict:
    global _SKILL_CATALOG_CACHE_KEY, _SKILL_CATALOG_CACHE_VALUE
    global _SKILL_CATALOG_CACHE_CONTEXT
    global _SKILL_CATALOG_LAST_SCAN
    cache_context = _skill_catalog_context()
    now = time.monotonic()
    with _DISCOVERY_CACHE_LOCK:
        if (
            _SKILL_CATALOG_CACHE_VALUE is not None
            and cache_context == _SKILL_CATALOG_CACHE_CONTEXT
            and now - _SKILL_CATALOG_LAST_SCAN
            < SKILL_CATALOG_SCAN_INTERVAL_SECONDS
        ):
            return copy.deepcopy(_SKILL_CATALOG_CACHE_VALUE)

    # Only one stale caller walks the catalogue; concurrent readers reuse the
    # result after this short critical section instead of causing a scan stampede.
    with _SKILL_CATALOG_BUILD_LOCK:
        cache_context = _skill_catalog_context()
        now = time.monotonic()
        with _DISCOVERY_CACHE_LOCK:
            if (
                _SKILL_CATALOG_CACHE_VALUE is not None
                and cache_context == _SKILL_CATALOG_CACHE_CONTEXT
                and now - _SKILL_CATALOG_LAST_SCAN
                < SKILL_CATALOG_SCAN_INTERVAL_SECONDS
            ):
                return copy.deepcopy(_SKILL_CATALOG_CACHE_VALUE)

        cache_key, sources = _skill_catalog_snapshot()
        with _DISCOVERY_CACHE_LOCK:
            _SKILL_CATALOG_LAST_SCAN = now
            if (
                cache_key == _SKILL_CATALOG_CACHE_KEY
                and cache_context == _SKILL_CATALOG_CACHE_CONTEXT
                and _SKILL_CATALOG_CACHE_VALUE is not None
            ):
                # Callers historically received fresh dictionaries and a few tests
                # intentionally modify them. Preserve that contract without disk IO.
                return copy.deepcopy(_SKILL_CATALOG_CACHE_VALUE)

        skills = []
        for engine, skill_dir, skill_path, section_paths in sources:
            try:
                metadata = dict(_read_skill_frontmatter(skill_path))
            except (OSError, UnicodeError):
                continue
            # Filesystem layout is the authority. Frontmatter may describe a
            # skill, but it must never redirect catalogue or resource paths.
            metadata["name"] = skill_dir.name
            metadata["engine"] = engine
            sections = enumerate_skill_sections(skill_dir, section_paths)
            metadata["sections"] = sections
            metadata["section_count"] = len(sections)
            skills.append(metadata)
        payload = {
            "success": True,
            "skills": skills,
            "usage": (
                "Load an index with skill_name='<skill>'. Load a sub-document with "
                "skill_name='<skill>/<section>', including references/<name> sections."
            ),
            "offline_capable": True,
        }

        # This scan is intentionally outside the discovery lock: cache readers
        # remain fast while publication races are checked.
        current_key, _ = _skill_catalog_snapshot()
        current_context = _skill_catalog_context()
        with _DISCOVERY_CACHE_LOCK:
            if current_key == cache_key and current_context == cache_context:
                _SKILL_CATALOG_CACHE_KEY = cache_key
                _SKILL_CATALOG_CACHE_VALUE = copy.deepcopy(payload)
                _SKILL_CATALOG_CACHE_CONTEXT = cache_context
                _SKILL_CATALOG_LAST_SCAN = time.monotonic()
        return payload


def compact_skills_payload() -> dict:
    """Return only catalogue fields needed to choose the next skill."""
    full = list_skills_payload()
    skills = []
    for skill in full["skills"]:
        skills.append(
            {
                "name": skill.get("name", ""),
                "engine": skill.get("engine", ""),
            }
        )
    return {
        "success": True,
        "skills": skills,
        "usage": full.get(
            "usage",
            "Load the smallest matching skill index, then only needed sections.",
        ),
        "offline_capable": True,
        "detail": "summary",
    }


def _skill_resource_uri(engine: str, skill_name: str, relative: str) -> str:
    segments = (engine, skill_name, *pathlib.PurePosixPath(relative).parts)
    encoded = [urllib.parse.quote(segment, safe="-._~") for segment in segments]
    return f"skill://{encoded[0]}/{'/'.join(encoded[1:])}"


def list_skill_resources_payload() -> dict:
    """Expose skill indexes as standard MCP resources without loading content."""
    resources = []
    for skill in list_skills_payload()["skills"]:
        name = str(skill.get("name") or "")
        engine = str(skill.get("engine") or "")
        if not name or engine not in KNOWN_ENGINES:
            continue
        try:
            _resolved_engine, resolved_name, _section, _skill_dir, index_path = (
                _resolve_skill(f"{engine}/{name}")
            )
            size = index_path.stat().st_size
        except (OSError, ValueError):
            continue
        resource = {
            "uri": _skill_resource_uri(engine, resolved_name, "SKILL.md"),
            "name": resolved_name,
            "title": str(skill.get("display_name") or resolved_name),
            "description": str(skill.get("description") or ""),
            "mimeType": "text/markdown",
            "size": size,
        }
        resources.append(resource)
    resources.sort(key=lambda item: (item["uri"].casefold(), item["uri"]))
    return {"resources": resources}


def list_skill_resource_templates_payload() -> dict:
    return {
        "resourceTemplates": [
            {
                "uriTemplate": "skill://{engine}/{skill}/{path}",
                "name": "revvy_skill_file",
                "title": "Revvy skill file",
                "description": (
                    "Read SKILL.md or a referenced Markdown section for a Revvy skill."
                ),
                "mimeType": "text/markdown",
            }
        ]
    }


def _resolve_skill(spec: str) -> tuple[str, str, str, pathlib.Path, pathlib.Path]:
    normalized = spec.strip().replace("\\", "/").removesuffix(".md")
    parts = pathlib.PurePosixPath(normalized).parts
    if not parts or any(part in ("", ".", "..") for part in parts):
        raise ValueError("Invalid skill name")
    engine = ""
    if len(parts) > 1 and parts[0].casefold() in KNOWN_ENGINES:
        engine = parts[0].casefold()
        parts = parts[1:]
    skill_name = parts[0]
    section = "/".join(parts[1:])
    if engine:
        candidates = [(engine, SKILLS_DIR / engine)]
    else:
        candidates = list(_skill_engine_dirs())
    for candidate_engine, engine_dir in candidates:
        safe_engine_dir = _safe_skill_path(engine_dir, SKILLS_DIR)
        if safe_engine_dir is None or not safe_engine_dir.is_dir():
            continue
        lexical_skill_dir = engine_dir / skill_name
        skill_dir = _safe_skill_path(lexical_skill_dir, engine_dir)
        if skill_dir is None or not skill_dir.is_dir():
            continue
        index_path = _safe_skill_path(lexical_skill_dir / "SKILL.md", lexical_skill_dir)
        if index_path is None or not index_path.is_file():
            continue
        lexical_target = lexical_skill_dir / (
            "SKILL.md" if not section else section + ".md"
        )
        target = _safe_skill_path(lexical_target, lexical_skill_dir)
        if target is not None and target.is_file():
            return candidate_engine, lexical_skill_dir.name, section, skill_dir, target
    raise ValueError(f"Skill not found: {spec}")


def read_skill_resource_payload(uri: str) -> dict:
    """Resolve one canonical skill:// URI without exposing arbitrary files."""
    if not isinstance(uri, str) or not uri:
        raise ValueError("Resource URI is required")
    parsed = urllib.parse.urlsplit(uri)
    if (
        parsed.scheme.casefold() != "skill"
        or parsed.query
        or parsed.fragment
        or parsed.username is not None
        or parsed.password is not None
    ):
        raise ValueError("Invalid skill resource URI")
    try:
        if parsed.port is not None:
            raise ValueError("Invalid skill resource URI")
    except ValueError as exc:
        raise ValueError("Invalid skill resource URI") from exc

    encoded_segments = [parsed.netloc, *parsed.path.lstrip("/").split("/")]
    if len(encoded_segments) < 3 or any(not segment for segment in encoded_segments):
        raise ValueError("Skill resource URI must identify a skill file")
    segments = [urllib.parse.unquote(segment) for segment in encoded_segments]
    if any(
        segment in {"", ".", ".."}
        or "/" in segment
        or "\\" in segment
        or "\x00" in segment
        for segment in segments
    ):
        raise ValueError("Invalid skill resource path")
    engine, skill_name, *file_parts = segments
    engine = engine.casefold()
    if engine not in KNOWN_ENGINES:
        raise ValueError(f"Unknown skill engine: {engine}")
    relative = "/".join(file_parts)
    if relative == "SKILL.md":
        spec = f"{engine}/{skill_name}"
    elif relative.casefold().endswith(".md"):
        spec = f"{engine}/{skill_name}/{relative[:-3]}"
    else:
        raise ValueError("Revvy exposes only Markdown skill resources")
    _resolved_engine, _name, _section, _skill_dir, target = _resolve_skill(spec)
    content = target.read_text(encoding="utf-8-sig")
    canonical_uri = _skill_resource_uri(engine, skill_name, relative)
    return {
        "contents": [
            {
                "uri": canonical_uri,
                "mimeType": "text/markdown",
                "text": content,
            }
        ]
    }


def load_skill_payload(spec: str) -> dict:
    engine, skill_name, section, skill_dir, target = _resolve_skill(spec)
    index_content = (skill_dir / "SKILL.md").read_text(encoding="utf-8-sig")
    content = target.read_text(encoding="utf-8-sig")
    metadata = parse_skill_frontmatter(index_content)
    relative = target.relative_to(SKILLS_DIR).as_posix()
    payload = {
        "success": True,
        "skill_name": spec.strip().removesuffix(".md"),
        "engine": engine,
        "revvy_classes": metadata.get("revvy_classes", []),
        "unreal_classes": metadata.get("unreal_classes", []),
        "available_sections": enumerate_skill_sections(skill_dir),
        "content": content,
        "files_loaded": [relative],
        "token_count": len(content) // 4,
        "offline_capable": True,
    }
    if section:
        payload["loaded_section"] = section
    return payload


def suggest_skills_payload(query: str, top_k: int = 5) -> dict:
    words = {word for word in query.casefold().replace("/", " ").split() if len(word) > 1}
    scored = []
    for skill in list_skills_payload()["skills"]:
        searchable = " ".join(
            str(skill.get(key, ""))
            for key in ("name", "display_name", "description", "keywords")
        ).casefold()
        matched = sorted(word for word in words if word in searchable)
        if matched:
            scored.append((len(matched), skill, matched))
    scored.sort(key=lambda item: (-item[0], str(item[1].get("name", ""))))
    suggestions = []
    for score, skill, matched in scored[: max(1, min(int(top_k), 20))]:
        suggestions.append(
            {
                "name": skill.get("name", ""),
                "description": skill.get("description", ""),
                "engine": skill.get("engine", ""),
                "score": score,
                "matched_keywords": matched,
            }
        )
    return {"success": True, "suggested_skills": suggestions, "offline_capable": True}


def handle_manage_skills(req_id, arguments: dict) -> dict:
    action = str(arguments.get("action", "")).casefold()
    try:
        if action == "list":
            detail = str(arguments.get("detail", "summary")).casefold()
            if detail not in {"summary", "full"}:
                raise ValueError("detail must be summary or full")
            payload = (
                list_skills_payload() if detail == "full" else compact_skills_payload()
            )
            payload["detail"] = detail
        elif action == "suggest":
            payload = suggest_skills_payload(
                str(arguments.get("query", "")), int(arguments.get("top_k", 5))
            )
        elif action == "load":
            names = arguments.get("skill_names")
            if names is None:
                names = [arguments.get("skill_name", "")]
            if not isinstance(names, list) or not names or not all(isinstance(name, str) and name for name in names):
                raise ValueError("load requires skill_name or skill_names")
            loaded = [load_skill_payload(name) for name in names]
            if len(loaded) == 1:
                payload = loaded[0]
            else:
                payload = {
                    "success": True,
                    "skills_loaded": [item["skill_name"] for item in loaded],
                    "revvy_classes": sorted({name for item in loaded for name in item["revvy_classes"]}),
                    "unreal_classes": sorted({name for item in loaded for name in item["unreal_classes"]}),
                    "available_sections": [
                        {"skill": item["skill_name"].split("/", 1)[0], "sections": item["available_sections"]}
                        for item in loaded
                    ],
                    "content": "\n\n---\n\n".join(item["content"] for item in loaded),
                    "files_loaded": [path for item in loaded for path in item["files_loaded"]],
                    "offline_capable": True,
                }
        else:
            raise ValueError("action must be list, suggest, or load")
    except (OSError, UnicodeError, ValueError) as exc:
        payload = {"success": False, "error": str(exc), "offline_capable": True}
    return {
        "jsonrpc": "2.0",
        "id": req_id,
        "result": {
            "content": [
                {"type": "text", "text": json.dumps(payload, ensure_ascii=False)}
            ],
            "isError": not payload.get("success", False),
        },
    }


def handle_manage_upstreams(
    req_id, arguments: dict, *, allow_activation: bool = True
) -> dict:
    """List dormant bridges or activate one namespace without generic dispatch."""
    action = str(arguments.get("action", "")).strip().casefold()
    try:
        if action == "list":
            payload = UPSTREAM_MANAGER.activation_snapshot()
        elif action == "activate":
            if not allow_activation:
                raise ValueError(
                    "External MCP activation requires a Full access session"
                )
            namespace = str(arguments.get("namespace", "")).strip().casefold()
            if not namespace:
                raise ValueError("activate requires namespace")
            tools = UPSTREAM_MANAGER.activate(namespace)
            snapshot = UPSTREAM_MANAGER.activation_snapshot()
            server = snapshot.get("servers", {}).get(namespace, {})
            payload = {
                "success": True,
                "namespace": namespace,
                "state": server.get("state", "active"),
                "tool_count": len(tools),
                "tool_names": sorted(str(tool.get("name", "")) for tool in tools),
                "refresh_tools": True,
                "note": (
                    "Refresh tools/list now. If the client caches tools for the whole "
                    "session, reconnect once before calling the typed tools."
                ),
            }
        else:
            raise ValueError("action must be list or activate")
    except (UpstreamError, ValueError) as exc:
        payload = {"success": False, "error": str(exc), "refresh_tools": False}
    return _synthetic_tool_response(
        req_id, payload, is_error=not payload.get("success", False)
    )


def forward_to_ue(
    body_bytes: bytes,
    headers: dict,
    *,
    read_only: bool = False,
) -> tuple[bool, bytes]:
    """Try to forward a raw request body to UE. Returns (success, response_bytes).

    success=True  -- UE returned a 2xx; response_bytes is valid JSON-RPC to forward as-is.
    success=False -- UE unreachable OR returned an error; response_bytes is a plain-text
                    description of the problem (may be empty if connection failed outright).
                    Caller must wrap this in a proper JSON-RPC error envelope.
    """
    config = load_runtime_config()
    identity, identity_error = _probe_editor_bridge_identity(config, timeout=2.0)
    if identity is None:
        log(f"Editor bridge preflight refused: {identity_error}")
        return False, identity_error.encode("utf-8", errors="replace")
    ue_url = f"http://127.0.0.1:{config['ue_port']}/mcp"
    forward_headers = {
        "Content-Type": "application/json",
        "Accept": "application/json",
        "X-Revvy-Proxy": "true",  # Identifies this request as proxy-forwarded (issue #314)
        # Tell Python and UE not to reuse the connection.  Without this, Python's
        # urllib reuses the keep-alive socket from the previous call; UE's server
        # closes it on its end, so the next request hits a stale socket and raises
        # OSError / RemoteDisconnected, which was reported as "UE is not running".
        "Connection": "close",
    }
    try:
        signed_headers = _signed_bridge_headers(
            config,
            identity,
            body_bytes,
            access_scope="read_only" if read_only else "full",
        )
        forward_headers.update(
            signed_headers
        )
    except ValueError as exc:
        return False, str(exc).encode("utf-8", errors="replace")
    # Pass through MCP protocol version from the incoming request.
    # Do NOT forward mcp-session-id — the proxy answers initialize itself and has no
    # session with UE, so a client session ID forwarded verbatim would be rejected by UE.
    for key in ("mcp-protocol-version",):
        if key in headers:
            forward_headers[key] = headers[key]

    try:
        req = urllib.request.Request(
            ue_url,
            data=body_bytes,
            headers=forward_headers,
            method="POST",
        )
        with urllib.request.urlopen(req, timeout=120) as resp:
            response_body = resp.read()
            status = int(getattr(resp, "status", 200))
            if not _verify_bridge_response(
                config,
                identity,
                signed_headers,
                body_bytes,
                response_body,
                resp.headers,
                status,
            ):
                log("Editor bridge response authentication failed")
                return (
                    (False, b"Editor bridge response authentication failed")
                    if read_only
                    else (False, EDITOR_ACK_LOST_BYTES)
                )
            return True, response_body
    except urllib.error.HTTPError as exc:
        body = exc.read()
        if not _verify_bridge_response(
            config,
            identity,
            signed_headers,
            body_bytes,
            body,
            exc.headers,
            int(exc.code),
        ):
            log(f"Editor bridge HTTP {exc.code} response authentication failed")
            return (
                (False, b"Editor bridge response authentication failed")
                if read_only
                else (False, EDITOR_ACK_LOST_BYTES)
            )
        log(f"UE returned HTTP {exc.code}: {body[:200]}")
        return False, body
    except (
        urllib.error.URLError,
        socket.timeout,
        OSError,
        http.client.HTTPException,
    ) as exc:
        # urllib cannot reliably tell whether the request body was accepted
        # before a transport failure. Full-scope calls are therefore uncertain
        # on *every* post-send transport error, not only a timeout. Retrying a
        # non-idempotent editor tool here could apply the same edit twice.
        log(f"UE request failed without retry: {exc}")
        if not read_only:
            return False, EDITOR_ACK_LOST_BYTES
        return False, b""


def ue_error_response(req_id, tool_name: str, ue_message: str = "") -> dict:
    acknowledgement_lost = ue_message == EDITOR_ACK_LOST_TEXT
    if ue_message:
        text = (
            f"Unreal Engine rejected the request: {ue_message}\n"
            f"Run 'revvy status' and verify the active project and editor connection.\n"
            f"For full setup instructions read: Plugins/Revvy/Content/Setup/Claude_MCP_Setup.md"
        )
    else:
        text = (
            f"Unreal Engine is not running.\n"
            f"Please launch UE with the Revvy plugin enabled, then retry '{tool_name}'.\n"
            f"If this is a first-time setup, follow: Plugins/Revvy/Content/Setup/Claude_MCP_Setup.md"
        )
    response = {
        "jsonrpc": "2.0",
        "id": req_id,
        "result": {
            "content": [{"type": "text", "text": text}],
            "isError": True,
        },
    }
    if acknowledgement_lost:
        response["result"]["structuredContent"] = {
            "success": False,
            "error": {
                "code": "editor_ack_lost",
                "message": EDITOR_ACK_LOST_TEXT,
            },
            "tool_name": tool_name,
            "may_have_started": True,
            "do_not_retry": True,
            "verify_current_state": True,
        }
    return response


def _tool_payload(response: dict) -> tuple[dict | None, str]:
    result = response.get("result") if isinstance(response, dict) else None
    if not isinstance(result, dict):
        return None, "Unreal returned an invalid tool response"
    texts = [
        item.get("text", "")
        for item in result.get("content", [])
        if isinstance(item, dict) and item.get("type") == "text"
    ]
    parsed = None
    for value in texts:
        try:
            candidate = json.loads(value)
        except (TypeError, json.JSONDecodeError):
            continue
        if isinstance(candidate, dict):
            parsed = candidate
    if result.get("isError"):
        message = "\n".join(texts).strip() or "Unreal tool call failed"
        if parsed:
            message = str(parsed.get("error_message") or parsed.get("error") or message)
        return parsed, message
    return parsed or {"text": "\n".join(texts)}, ""


def _call_ue_tool(tool_name: str, arguments: dict) -> tuple[dict | None, str]:
    request = {
        "jsonrpc": "2.0",
        "id": int(time.time() * 1000000),
        "method": "tools/call",
        "params": {"name": tool_name, "arguments": arguments},
    }
    success, raw = forward_to_ue(json.dumps(request).encode("utf-8"), {})
    if not success or not raw:
        return None, raw.decode(errors="replace").strip() if raw else "Unreal is unavailable"
    try:
        response = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        return None, f"Unreal returned invalid JSON: {exc}"
    return _tool_payload(response)


def _synthetic_tool_response(req_id, payload: dict, is_error: bool = False) -> dict:
    return {
        "jsonrpc": "2.0",
        "id": req_id,
        "result": {
            "content": [{"type": "text", "text": json.dumps(payload, ensure_ascii=False)}],
            "structuredContent": payload,
            "isError": bool(is_error),
        },
    }


class _GitToolError(RuntimeError):
    pass


def _redact_git_output(value: str) -> str:
    return re.sub(r"(https?://)[^/@\s]+@", r"\1***@", value)


def _run_git(
    project_root: pathlib.Path,
    arguments: list[str],
    *,
    timeout: float = 120.0,
) -> str:
    env = os.environ.copy()
    env["GIT_TERMINAL_PROMPT"] = "0"
    env["GCM_INTERACTIVE"] = "Never"
    creationflags = int(getattr(subprocess, "CREATE_NO_WINDOW", 0)) if os.name == "nt" else 0
    try:
        completed = subprocess.run(
            ["git", "-C", str(project_root), *arguments],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout,
            check=False,
            env=env,
            creationflags=creationflags,
        )
    except subprocess.TimeoutExpired as exc:
        raise _GitToolError(f"Git command timed out: {' '.join(arguments[:2])}") from exc
    except OSError as exc:
        raise _GitToolError(f"Could not run Git: {exc}") from exc
    if completed.returncode != 0:
        detail = _redact_git_output((completed.stderr or completed.stdout or "").strip())
        raise _GitToolError(
            f"Git {' '.join(arguments[:2])} failed with exit code {completed.returncode}"
            + (f": {detail}" if detail else "")
        )
    return (completed.stdout or "").strip()


def _configured_git_project(
    project_path: object,
    *,
    require_bundled_revvy: bool = False,
) -> pathlib.Path:
    requested = str(project_path or "").strip()
    if not requested:
        raise _GitToolError("project_path is required")
    candidate = pathlib.Path(requested).expanduser()
    try:
        candidate = candidate.resolve(strict=True)
    except OSError as exc:
        raise _GitToolError(f"Project path does not exist: {requested}") from exc
    if candidate.is_file() and candidate.suffix.casefold() == ".uproject":
        candidate = candidate.parent
    if not candidate.is_dir():
        raise _GitToolError(f"Project path is not a directory: {candidate}")

    config = load_runtime_config()
    allowed: set[pathlib.Path] = set()
    for key in ("active_project", "installed_project"):
        configured = _project_from_config(config, key)
        value = str((configured or {}).get("path", "")).strip()
        if not value:
            continue
        try:
            root = pathlib.Path(value).expanduser().resolve(strict=True)
        except OSError:
            continue
        if root.is_file() and root.suffix.casefold() == ".uproject":
            root = root.parent
        allowed.add(root)
    is_registered_project = candidate in allowed
    is_bundled_revvy_repository = False
    for root in allowed:
        revvy_path = root / "Plugins" / "Revvy"
        try:
            resolved_revvy_path = revvy_path.resolve(strict=True)
        except OSError:
            continue
        if candidate != resolved_revvy_path:
            continue
        if resolved_revvy_path != revvy_path.absolute():
            raise _GitToolError("Bundled Revvy repository cannot be a linked or redirected path")
        descriptor = candidate / "Revvy.uplugin"
        if not descriptor.is_file() or descriptor.is_symlink():
            raise _GitToolError("Bundled Revvy repository is missing a regular Revvy.uplugin")
        git_marker = candidate / ".git"
        if not git_marker.is_dir():
            raise _GitToolError("Bundled Revvy repository requires an internal .git directory")
        try:
            resolved_git_marker = git_marker.resolve(strict=True)
        except OSError as exc:
            raise _GitToolError("Bundled Revvy .git directory could not be resolved") from exc
        if resolved_git_marker != git_marker.absolute() or not resolved_git_marker.is_relative_to(candidate):
            raise _GitToolError("Bundled Revvy .git directory cannot be linked outside its repository")
        is_bundled_revvy_repository = True
        break

    if not is_registered_project and not is_bundled_revvy_repository:
        registered = ", ".join(str(path) for path in sorted(allowed, key=str)) or "none"
        raise _GitToolError(
            "Repository path is not registered with Revvy and is not its bundled "
            f"Plugins/Revvy repository: {candidate} (registered: {registered})"
        )
    if require_bundled_revvy and not is_bundled_revvy_repository:
        raise _GitToolError("This operation is restricted to the bundled Plugins/Revvy repository")
    if is_registered_project:
        projects = list(candidate.glob("*.uproject"))
        if len(projects) != 1:
            raise _GitToolError(
                f"Expected exactly one .uproject in {candidate}, found {len(projects)}"
            )
    top_level_text = _run_git(candidate, ["rev-parse", "--show-toplevel"], timeout=15)
    try:
        top_level = pathlib.Path(top_level_text).resolve(strict=True)
    except OSError as exc:
        raise _GitToolError(f"Git returned an invalid repository root: {top_level_text}") from exc
    if top_level != candidate:
        raise _GitToolError(
            f"Requested repository must be an exact Git top-level: requested={candidate}, git={top_level}"
        )
    return candidate


def _validated_publish_paths(project_root: pathlib.Path, raw_paths: object) -> list[str]:
    if not isinstance(raw_paths, list) or not 1 <= len(raw_paths) <= 512:
        raise _GitToolError("paths must contain between 1 and 512 repository-relative files")
    normalized: list[str] = []
    seen: set[str] = set()
    for raw_path in raw_paths:
        if not isinstance(raw_path, str) or not raw_path.strip():
            raise _GitToolError("Every publish path must be a non-empty string")
        if any(character in raw_path for character in ("\x00", "\r", "\n")):
            raise _GitToolError("Publish paths cannot contain control characters")
        windows_path = pathlib.PureWindowsPath(raw_path)
        portable_path = pathlib.PurePosixPath(raw_path.replace("\\", "/"))
        if windows_path.is_absolute() or windows_path.drive or portable_path.is_absolute():
            raise _GitToolError(f"Publish path must be repository-relative: {raw_path}")
        if not portable_path.parts or any(part in {"", ".", ".."} for part in portable_path.parts):
            raise _GitToolError(f"Publish path contains an unsafe segment: {raw_path}")
        if any(part.casefold() == ".git" for part in portable_path.parts):
            raise _GitToolError("Publishing Git metadata is not allowed")
        relative = portable_path.as_posix()
        key = relative.casefold() if os.name == "nt" else relative
        if key in seen:
            raise _GitToolError(f"Duplicate publish path: {relative}")
        seen.add(key)
        filesystem_path = project_root.joinpath(*portable_path.parts)
        try:
            resolved_path = filesystem_path.resolve(strict=False)
        except OSError as exc:
            raise _GitToolError(f"Could not resolve publish path: {relative}") from exc
        if not resolved_path.is_relative_to(project_root):
            raise _GitToolError(f"Publish path escapes the repository: {relative}")
        if filesystem_path.is_symlink():
            raise _GitToolError(f"Publish path cannot be a symbolic link: {relative}")
        if filesystem_path.exists() and not filesystem_path.is_file():
            raise _GitToolError(f"Publish path must identify one file, not a directory: {relative}")
        normalized.append(relative)
    return normalized


def _validated_asset_history_paths(project_root: pathlib.Path, raw_paths: object) -> list[str]:
    if not isinstance(raw_paths, list) or not 1 <= len(raw_paths) <= 64:
        raise _GitToolError("paths must contain between 1 and 64 repository-relative assets")
    normalized: list[str] = []
    seen: set[str] = set()
    for raw_path in raw_paths:
        if not isinstance(raw_path, str) or not raw_path.strip():
            raise _GitToolError("Every asset history path must be a non-empty string")
        if any(character in raw_path for character in ("\x00", "\r", "\n")):
            raise _GitToolError("Asset history paths cannot contain control characters")
        if any(character in raw_path for character in ("*", "?", "[", "]")):
            raise _GitToolError("Asset history paths must be exact and cannot contain wildcards")
        windows_path = pathlib.PureWindowsPath(raw_path)
        portable_path = pathlib.PurePosixPath(raw_path.replace("\\", "/"))
        if windows_path.is_absolute() or windows_path.drive or portable_path.is_absolute():
            raise _GitToolError(f"Asset history path must be repository-relative: {raw_path}")
        if not portable_path.parts or any(part in {"", ".", ".."} for part in portable_path.parts):
            raise _GitToolError(f"Asset history path contains an unsafe segment: {raw_path}")
        if any(part.casefold() == ".git" for part in portable_path.parts):
            raise _GitToolError("Reading Git metadata as an asset path is not allowed")
        if portable_path.suffix.casefold() not in {".uasset", ".umap"}:
            raise _GitToolError(f"Asset history path must end in .uasset or .umap: {raw_path}")
        relative = portable_path.as_posix()
        key = relative.casefold() if os.name == "nt" else relative
        if key in seen:
            raise _GitToolError(f"Duplicate asset history path: {relative}")
        seen.add(key)
        filesystem_path = project_root.joinpath(*portable_path.parts)
        try:
            resolved_path = filesystem_path.resolve(strict=False)
        except OSError as exc:
            raise _GitToolError(f"Could not resolve asset history path: {relative}") from exc
        if not resolved_path.is_relative_to(project_root):
            raise _GitToolError(f"Asset history path escapes the repository: {relative}")
        if filesystem_path.is_symlink():
            raise _GitToolError(f"Asset history path cannot be a symbolic link: {relative}")
        normalized.append(relative)
    return normalized


def _git_asset_history_payload(project_root: pathlib.Path, paths: list[str]) -> dict:
    output = _run_git(
        project_root,
        [
            "log",
            "--all",
            "--date-order",
            "--format=__REVVY_COMMIT__%x09%H%x09%ct%x09%an%x09%s",
            "--name-status",
            "--",
            *paths,
        ],
        timeout=120,
    )
    commits: list[dict] = []
    current: dict | None = None
    seen_paths: set[str] = set()
    for line in output.splitlines():
        if line.startswith("__REVVY_COMMIT__\t"):
            fields = line.split("\t", 4)
            if len(fields) != 5:
                current = None
                continue
            _marker, commit_hash, timestamp_text, author, subject = fields
            try:
                timestamp = int(timestamp_text)
                created_at_utc = datetime.fromtimestamp(
                    timestamp,
                    tz=timezone.utc,
                ).isoformat()
            except (OverflowError, OSError, ValueError):
                timestamp = 0
                created_at_utc = ""
            current = {
                "commit": commit_hash,
                "timestamp": timestamp,
                "created_at_utc": created_at_utc,
                "author": author[:300],
                "subject": subject[:500],
                "changes": [],
            }
            commits.append(current)
        elif current is not None and line.strip():
            change = line[:1000]
            current["changes"].append(change)
            columns = change.split("\t")
            seen_paths.update(path for path in columns[1:] if path)
    return {
        "success": True,
        "project_path": str(project_root),
        "queried_paths": paths,
        "history_count": len(commits),
        "history": commits[:500],
        "history_truncated": len(commits) > 500,
        "never_seen_paths": [path for path in paths if path not in seen_paths],
    }


def _sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while chunk := handle.read(_LFS_SCAN_CHUNK_BYTES):
            digest.update(chunk)
    return digest.hexdigest()


# A marker search reads whatever the LFS cache holds, and an art repository holds
# tens of gigabytes of it. The endpoint that exposes this tool carries no
# credential, so the work one call can cause has to be bounded up front.
_LFS_SCAN_CHUNK_BYTES = 1024 * 1024
_LFS_SCAN_MAX_OBJECTS = 20000
_LFS_SCAN_MAX_TOTAL_BYTES = 8 * 1024 * 1024 * 1024
_LFS_SCAN_TIME_BUDGET_SECONDS = 60.0
_LFS_SCAN_MAX_MATCHES = 512
_LFS_SCAN_LOCK = threading.Lock()


def _scan_lfs_object(
    path: pathlib.Path, patterns: dict[str, tuple[bytes, bytes]]
) -> tuple[int, str, list[str]]:
    """Hash an LFS object and search it for markers in a single streaming pass.

    Reading whole objects with read_bytes() let one caller pin gigabytes of cache
    content in the proxy. Chunks carry an overlapping tail so a marker split
    across a chunk boundary is still found.
    """

    digest = hashlib.sha256()
    longest = max((len(value) for pair in patterns.values() for value in pair), default=1)
    overlap = max(longest - 1, 0)
    matched: set[str] = set()
    remaining = dict(patterns)
    size = 0
    tail = b""

    with path.open("rb") as handle:
        while chunk := handle.read(_LFS_SCAN_CHUNK_BYTES):
            size += len(chunk)
            digest.update(chunk)
            if not remaining:
                continue
            window = tail + chunk
            for marker, (utf8, utf16) in list(remaining.items()):
                if utf8 in window or utf16 in window:
                    matched.add(marker)
                    del remaining[marker]
            tail = window[-overlap:] if overlap else b""

    # Report in the order the caller asked, not the order chunks happened to match.
    return size, digest.hexdigest(), [name for name in patterns if name in matched]


def _validated_lfs_items(project_root: pathlib.Path, raw_items: object) -> list[dict]:
    if not isinstance(raw_items, list) or not 1 <= len(raw_items) <= 64:
        raise _GitToolError("items must contain between 1 and 64 LFS recovery entries")
    raw_paths: list[str] = []
    for item in raw_items:
        if not isinstance(item, dict):
            raise _GitToolError("Every LFS recovery item must be an object")
        raw_paths.append(item.get("path"))
    paths = _validated_asset_history_paths(project_root, raw_paths)

    validated: list[dict] = []
    for path, item in zip(paths, raw_items):
        oid = str(item.get("oid") or "").strip().casefold()
        size = item.get("size")
        if not re.fullmatch(r"[0-9a-f]{64}", oid):
            raise _GitToolError(f"LFS object ID must be a 64-character SHA-256: {path}")
        if isinstance(size, bool) or not isinstance(size, int) or size <= 0:
            raise _GitToolError(f"LFS object size must be a positive integer: {path}")
        validated.append({"path": path, "oid": oid, "size": size})
    return validated


def _git_lfs_objects_root(project_root: pathlib.Path) -> pathlib.Path:
    """Locate the LFS object store the way git-lfs itself resolves it.

    Assuming <project>/.git/lfs/objects reported "no LFS store" for a configured
    lfs.storage, a shared cache, and every repository whose .git is a file
    (worktree, submodule). During a recovery that reads as "the asset is gone",
    which is the one wrong answer this tool must not give.
    """

    git_dir_text = _run_git(
        project_root, ["rev-parse", "--absolute-git-dir"], timeout=30
    ).strip()
    if not git_dir_text:
        raise _GitToolError("Git directory could not be resolved for this project")
    try:
        git_dir = pathlib.Path(git_dir_text).resolve(strict=True)
    except OSError as exc:
        raise _GitToolError("Git directory could not be resolved for this project") from exc

    configured = ""
    try:
        configured = _run_git(
            project_root, ["config", "--get", "lfs.storage"], timeout=30
        ).strip()
    except _GitToolError:
        # `git config --get` exits non-zero when the key is unset, which is the
        # normal case; fall through to the default location.
        configured = ""

    if configured:
        storage = pathlib.Path(configured).expanduser()
        # git-lfs treats a relative lfs.storage as relative to the git directory.
        if not storage.is_absolute():
            storage = git_dir / storage
        objects_root = storage / "objects"
    else:
        objects_root = git_dir / "lfs" / "objects"

    if not objects_root.is_dir() or objects_root.is_symlink():
        raise _GitToolError(
            "Git LFS object store was not found for this repository "
            f"(looked in {objects_root})"
        )
    try:
        resolved_objects = objects_root.resolve(strict=True)
    except OSError as exc:
        raise _GitToolError("Git LFS object store could not be resolved") from exc
    # A configured store may legitimately live outside .git (shared cache); the
    # default one may not, since nothing should have moved it.
    if not configured and not resolved_objects.is_relative_to(git_dir):
        raise _GitToolError("Git LFS object store cannot be linked outside .git")
    return resolved_objects


def _git_lfs_object_status_payload(project_root: pathlib.Path, items: list[dict]) -> dict:
    objects_root = _git_lfs_objects_root(project_root)
    results: list[dict] = []
    for item in items:
        oid = item["oid"]
        source = objects_root / oid[:2] / oid[2:4] / oid
        target = project_root.joinpath(*pathlib.PurePosixPath(item["path"]).parts)
        object_present = source.is_file() and not source.is_symlink()
        expected_marker = pathlib.PurePosixPath(item["path"]).stem
        actual_size = 0
        actual_oid = ""
        marker_present = False
        if object_present:
            # One streaming pass covers the hash and the marker, so a large object
            # is never held in memory whole.
            actual_size, actual_oid, matched = _scan_lfs_object(
                source,
                {
                    expected_marker: (
                        expected_marker.encode("utf-8"),
                        expected_marker.encode("utf-16-le"),
                    )
                },
            )
            marker_present = bool(matched)
        # The marker only counts once the object itself is proven intact.
        marker_verified = (
            marker_present and actual_size == item["size"] and actual_oid == oid
        )
        target_exists = target.exists()
        verified = (
            object_present
            and actual_size == item["size"]
            and actual_oid == oid
            and marker_verified
        )
        results.append(
            {
                "path": item["path"],
                "oid": oid,
                "expected_size": item["size"],
                "actual_size": actual_size,
                "object_present": object_present,
                "sha256_verified": actual_oid == oid,
                "expected_marker": expected_marker,
                "marker_verified": marker_verified,
                "target_exists": target_exists,
                "ready_to_restore": verified and not target_exists,
            }
        )
    return {
        "success": True,
        "project_path": str(project_root),
        "item_count": len(results),
        "ready_count": sum(1 for item in results if item["ready_to_restore"]),
        "items": results,
    }


def _git_lfs_marker_search_payload(project_root: pathlib.Path, raw_markers: object) -> dict:
    if not isinstance(raw_markers, list) or not 1 <= len(raw_markers) <= 64:
        raise _GitToolError("markers must contain between 1 and 64 Unreal asset identifiers")
    markers: list[str] = []
    seen: set[str] = set()
    for marker in raw_markers:
        if not isinstance(marker, str) or not re.fullmatch(r"[A-Za-z0-9_]{1,200}", marker):
            raise _GitToolError(
                "Every marker must be an exact Unreal asset identifier using A-Z, a-z, 0-9, or _"
            )
        if marker in seen:
            raise _GitToolError(f"Duplicate LFS marker: {marker}")
        seen.add(marker)
        markers.append(marker)

    encoded = {
        marker: (marker.encode("utf-8"), marker.encode("utf-16-le"))
        for marker in markers
    }
    objects_root = _git_lfs_objects_root(project_root)

    # One scan at a time. Without this, repeated calls to a credential-free
    # endpoint each take a handler thread and a full pass over the cache.
    if not _LFS_SCAN_LOCK.acquire(blocking=False):
        raise _GitToolError(
            "Another Git LFS marker search is already running; retry once it finishes"
        )
    try:
        deadline = time.monotonic() + _LFS_SCAN_TIME_BUDGET_SECONDS
        scanned = 0
        bytes_scanned = 0
        matches: list[dict] = []
        truncated_reason = ""
        for path in objects_root.glob("*/*/*"):
            if len(matches) >= _LFS_SCAN_MAX_MATCHES:
                truncated_reason = "match limit reached"
                break
            if scanned >= _LFS_SCAN_MAX_OBJECTS:
                truncated_reason = "object limit reached"
                break
            if bytes_scanned >= _LFS_SCAN_MAX_TOTAL_BYTES:
                truncated_reason = "byte budget exhausted"
                break
            if time.monotonic() > deadline:
                truncated_reason = "time budget exhausted"
                break
            if not path.is_file() or path.is_symlink():
                continue
            oid = path.name.casefold()
            if (
                not re.fullmatch(r"[0-9a-f]{64}", oid)
                or path.parent.name.casefold() != oid[2:4]
                or path.parent.parent.name.casefold() != oid[:2]
            ):
                continue
            scanned += 1
            size, actual_oid, matched_markers = _scan_lfs_object(path, encoded)
            bytes_scanned += size
            if not matched_markers:
                continue
            matches.append(
                {
                    "oid": oid,
                    "size": size,
                    "sha256_verified": actual_oid == oid,
                    "markers": matched_markers,
                }
            )
    finally:
        _LFS_SCAN_LOCK.release()

    return {
        "success": True,
        "project_path": str(project_root),
        "queried_markers": markers,
        "objects_scanned": scanned,
        "bytes_scanned": bytes_scanned,
        "matched_object_count": len(matches),
        "matches": matches,
        # A bounded scan that stopped early is not proof the marker is absent.
        "scan_truncated": bool(truncated_reason),
        "scan_truncated_reason": truncated_reason,
        "matches_truncated": truncated_reason == "match limit reached",
    }


def _git_state_fingerprint(project_root: pathlib.Path) -> dict:
    """HEAD, stash list and index, so 'we changed nothing' can be checked, not claimed."""
    return {
        "head": _run_git(project_root, ["rev-parse", "HEAD"], timeout=30).strip(),
        "stash": _run_git(project_root, ["stash", "list"], timeout=30),
        "staged": _run_git(project_root, ["diff", "--cached", "--name-only"], timeout=30),
    }


def _missing_ancestors(
    directory: pathlib.Path, project_root: pathlib.Path
) -> list[pathlib.Path]:
    """Directories that do not exist yet, deepest first, stopping at the project root."""
    missing: list[pathlib.Path] = []
    current = directory
    while current != project_root and not current.exists():
        missing.append(current)
        parent = current.parent
        if parent == current:
            break
        current = parent
    return missing


def _restore_verified_lfs_objects(project_root: pathlib.Path, items: list[dict]) -> dict:
    before = _git_state_fingerprint(project_root)
    status = _git_lfs_object_status_payload(project_root, items)
    not_ready = [item for item in status["items"] if not item["ready_to_restore"]]
    if not_ready:
        paths = ", ".join(item["path"] for item in not_ready[:8])
        raise _GitToolError(
            "Every LFS object must be verified and every target must be missing before restore: "
            + paths
        )

    objects_root = _git_lfs_objects_root(project_root)
    temporary_files: list[pathlib.Path] = []
    restored_targets: list[pathlib.Path] = []
    created_directories: list[pathlib.Path] = []
    try:
        prepared: list[tuple[pathlib.Path, pathlib.Path, dict]] = []
        for item in items:
            oid = item["oid"]
            source = objects_root / oid[:2] / oid[2:4] / oid
            target = project_root.joinpath(*pathlib.PurePosixPath(item["path"]).parts)
            # Remember what we create so a rollback does not leave empty folders behind.
            for directory in _missing_ancestors(target.parent, project_root):
                if directory not in created_directories:
                    created_directories.append(directory)
            target.parent.mkdir(parents=True, exist_ok=True)
            temporary = target.with_name(
                f".{target.name}.revvy-{secrets.token_hex(8)}.tmp"
            )
            shutil.copyfile(source, temporary)
            temporary_files.append(temporary)
            if temporary.stat().st_size != item["size"] or _sha256_file(temporary) != oid:
                raise _GitToolError(f"Temporary LFS copy verification failed: {item['path']}")
            prepared.append((temporary, target, item))

        for temporary, target, item in prepared:
            if target.exists():
                raise _GitToolError(f"Restore target appeared during operation: {item['path']}")
            os.replace(temporary, target)
            temporary_files.remove(temporary)
            restored_targets.append(target)

        failed = []
        for item in items:
            target = project_root.joinpath(*pathlib.PurePosixPath(item["path"]).parts)
            if (
                not target.is_file()
                or target.stat().st_size != item["size"]
                or _sha256_file(target) != item["oid"]
            ):
                failed.append(item["path"])
        if failed:
            raise _GitToolError(
                "Post-restore target verification failed: " + ", ".join(failed[:8])
            )
        # Measured, not asserted: the restore touches the working tree only, and the
        # caller can see that rather than take the tool's word for it.
        after = _git_state_fingerprint(project_root)
        return {
            "success": True,
            "project_path": str(project_root),
            "restored_count": len(items),
            "restored_paths": [item["path"] for item in items],
            "staged": after["staged"] != before["staged"],
            "committed": after["head"] != before["head"],
            "stash_changed": after["stash"] != before["stash"],
        }
    except Exception:
        for temporary in temporary_files:
            try:
                temporary.unlink(missing_ok=True)
            except OSError:
                pass
        for target in restored_targets:
            try:
                target.unlink(missing_ok=True)
            except OSError:
                pass
        for directory in created_directories:
            try:
                directory.rmdir()
            except OSError:
                # Not empty, or never created — either way it is not ours to remove.
                pass
        raise


def _git_stash_payload(project_root: pathlib.Path) -> dict:
    output = _run_git(
        project_root,
        ["stash", "list", "--format=%gd%x09%H%x09%ct%x09%gs"],
        timeout=30,
    )
    rows = [line for line in output.splitlines() if line.strip()]
    stashes: list[dict] = []
    for line in rows[:100]:
        fields = line.split("\t", 3)
        if len(fields) != 4:
            continue
        stash_ref, commit_hash, timestamp_text, message = fields
        parent_line = _run_git(
            project_root,
            ["rev-list", "--parents", "-n", "1", stash_ref],
            timeout=15,
        )
        parent_parts = parent_line.split()
        tracked_output = _run_git(
            project_root,
            ["diff", "--name-status", f"{stash_ref}^1", stash_ref, "--"],
            timeout=30,
        )
        tracked_changes = [
            change[:500]
            for change in tracked_output.splitlines()
            if change.strip()
        ]
        untracked_paths: list[str] = []
        if len(parent_parts) >= 4:
            untracked_output = _run_git(
                project_root,
                ["ls-tree", "-r", "--name-only", parent_parts[3], "--"],
                timeout=30,
            )
            untracked_paths = [
                path[:500]
                for path in untracked_output.splitlines()
                if path.strip()
            ]

        changed_paths: set[str] = set(untracked_paths)
        for change in tracked_changes:
            columns = change.split("\t")
            changed_paths.update(path for path in columns[1:] if path)
        unreal_asset_paths = sorted(
            path
            for path in changed_paths
            if pathlib.PurePosixPath(path).suffix.casefold() in {".uasset", ".umap"}
        )
        try:
            timestamp = int(timestamp_text)
            created_at_utc = datetime.fromtimestamp(
                timestamp,
                tz=timezone.utc,
            ).isoformat()
        except (OverflowError, OSError, ValueError):
            timestamp = 0
            created_at_utc = ""
        stashes.append(
            {
                "ref": stash_ref,
                "commit": commit_hash,
                "timestamp": timestamp,
                "created_at_utc": created_at_utc,
                "message": message[:500],
                "tracked_change_count": len(tracked_changes),
                "tracked_changes": tracked_changes[:250],
                "tracked_changes_truncated": len(tracked_changes) > 250,
                "untracked_file_count": len(untracked_paths),
                "untracked_paths": untracked_paths[:250],
                "untracked_paths_truncated": len(untracked_paths) > 250,
                "unreal_asset_count": len(unreal_asset_paths),
                "unreal_asset_paths": unreal_asset_paths[:1000],
                "unreal_asset_paths_truncated": len(unreal_asset_paths) > 1000,
            }
        )
    return {
        "stash_count": len(rows),
        "stashes": stashes,
        "stashes_truncated": len(rows) > 100,
    }


def _git_status_payload(project_root: pathlib.Path) -> dict:
    output = _run_git(
        project_root,
        ["status", "--porcelain=v2", "--branch", "--untracked-files=normal"],
        timeout=30,
    )
    branch = ""
    upstream = ""
    ahead = 0
    behind = 0
    changes: list[str] = []
    for line in output.splitlines():
        if line.startswith("# branch.head "):
            branch = line.removeprefix("# branch.head ").strip()
        elif line.startswith("# branch.upstream "):
            upstream = line.removeprefix("# branch.upstream ").strip()
        elif line.startswith("# branch.ab "):
            match = re.search(r"\+(\d+)\s+-(\d+)", line)
            if match:
                ahead, behind = int(match.group(1)), int(match.group(2))
        elif line and not line.startswith("#"):
            changes.append(line[:2000])
    payload = {
        "success": True,
        "project_path": str(project_root),
        "branch": branch,
        "upstream": upstream,
        "detached": branch in {"", "(detached)"},
        "dirty": bool(changes),
        "change_count": len(changes),
        "changes": changes[:500],
        "changes_truncated": len(changes) > 500,
        "ahead": ahead,
        "behind": behind,
    }
    payload.update(_git_stash_payload(project_root))
    return payload


def _git_operation_in_progress(project_root: pathlib.Path) -> str:
    git_dir_text = _run_git(project_root, ["rev-parse", "--git-dir"], timeout=15)
    git_dir = pathlib.Path(git_dir_text)
    if not git_dir.is_absolute():
        git_dir = project_root / git_dir
    checks = (
        ("MERGE_HEAD", "merge"),
        ("CHERRY_PICK_HEAD", "cherry-pick"),
        ("REVERT_HEAD", "revert"),
        ("rebase-merge", "rebase"),
        ("rebase-apply", "rebase"),
    )
    for relative, label in checks:
        if (git_dir / relative).exists():
            return label
    return ""


def _publish_paths_on_upstream(
    project_root: pathlib.Path,
    *,
    paths: list[str],
    message: str,
    branch: str,
    remote: str,
    merge_ref: str,
    old_head: str,
    upstream_head: str,
) -> tuple[str, str]:
    """Publish selected files on top of upstream, then fast-forward the source branch."""
    # --no-renames: with rename detection on, --name-only prints only a rename's
    # destination, so a selected path that upstream moved would look untouched and
    # this publish would resurrect it at the old location.
    upstream_changes = {
        line.strip()
        for line in _run_git(
            project_root,
            ["diff", "--name-only", "--no-renames", f"{old_head}..{upstream_head}", "--"],
            timeout=60,
        ).splitlines()
        if line.strip()
    }
    path_keys = {
        path.casefold() if os.name == "nt" else path
        for path in paths
    }
    overlap = sorted(
        path
        for path in upstream_changes
        if (path.casefold() if os.name == "nt" else path) in path_keys
    )
    if overlap:
        raise _GitToolError(
            "Upstream changed a selected publish path; resolve it explicitly first: "
            + ", ".join(overlap[:8])
        )

    temporary_root = pathlib.Path(tempfile.mkdtemp(prefix="revvy-publish-"))
    worktree = temporary_root / f"worktree-{secrets.token_hex(4)}"
    # Deliberately not under temporary_root: the cleanup below removes that tree,
    # which would delete the only copy of the index while it is still needed.
    index_backup = pathlib.Path(
        _run_git(project_root, ["rev-parse", "--absolute-git-dir"], timeout=15).strip()
    ) / f"revvy-index-backup-{secrets.token_hex(4)}"
    worktree_added = False
    pushed_commit = ""
    try:
        _run_git(
            project_root,
            ["worktree", "add", "--detach", "--no-checkout", str(worktree), upstream_head],
            timeout=120,
        )
        worktree_added = True
        _run_git(worktree, ["read-tree", upstream_head], timeout=60)

        attribute_paths = [
            line.strip()
            for line in _run_git(
                project_root,
                ["ls-tree", "-r", "--name-only", upstream_head, "--"],
                timeout=60,
            ).splitlines()
            if line.strip().casefold().endswith(".gitattributes")
        ]
        if attribute_paths:
            _run_git(
                worktree,
                ["checkout-index", "--force", "--", *attribute_paths],
                timeout=60,
            )

        # Upstream's .gitattributes is not the whole story: adding the first asset
        # to a new folder means its LFS rule is still uncommitted, and without it
        # here the binary would be committed literally instead of as a pointer.
        for relative in paths:
            directory = pathlib.PurePosixPath(relative).parent
            while True:
                local_attributes = project_root.joinpath(*directory.parts, ".gitattributes")
                if local_attributes.is_file():
                    destination = worktree.joinpath(*directory.parts, ".gitattributes")
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(local_attributes, destination)
                if not directory.parts:
                    break
                directory = directory.parent

        for relative in paths:
            portable = pathlib.PurePosixPath(relative)
            source = project_root.joinpath(*portable.parts)
            destination = worktree.joinpath(*portable.parts)
            if source.exists():
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, destination)
            elif destination.exists():
                destination.unlink()

        _run_git(worktree, ["add", "--all", "--", *paths], timeout=600)
        staged_paths = [
            line.strip()
            for line in _run_git(
                worktree,
                ["diff", "--cached", "--name-only", "--"],
                timeout=60,
            ).splitlines()
            if line.strip()
        ]
        if not staged_paths:
            raise _GitToolError("Selected paths produced no staged changes")
        unexpected = [
            path for path in staged_paths
            if (path.casefold() if os.name == "nt" else path) not in path_keys
        ]
        if unexpected:
            raise _GitToolError(
                "Staging selected an unexpected path: " + ", ".join(unexpected[:3])
            )

        # A path that Git says should go through the LFS filter must be staged as a
        # pointer. Committing the literal blob instead cannot be undone on the
        # remote without rewriting history, so refuse before the commit exists.
        for staged in staged_paths:
            attribute = _run_git(
                worktree, ["check-attr", "filter", "--", staged], timeout=30
            ).strip()
            if not attribute.endswith(": lfs"):
                continue
            blob = _run_git(worktree, ["show", f":{staged}"], timeout=60)
            if not blob.startswith("version https://git-lfs.github.com/spec/v1"):
                raise _GitToolError(
                    f"{staged} is tracked by Git LFS but was staged as a literal file; "
                    "check that Git LFS is installed and its filters are configured"
                )

        _run_git(
            worktree,
            [
                "-c",
                f"core.hooksPath={os.devnull}",
                "commit",
                "--no-gpg-sign",
                "-m",
                message,
            ],
            timeout=600,
        )
        pushed_commit = _run_git(worktree, ["rev-parse", "HEAD"], timeout=15)
        push_output = _run_git(
            worktree,
            ["push", "--porcelain", remote, f"HEAD:{merge_ref}"],
            timeout=1200,
        )
        _run_git(project_root, ["fetch", "--no-tags", remote], timeout=300)
        verified_upstream = _run_git(project_root, ["rev-parse", "@{upstream}"], timeout=15)
        if verified_upstream != pushed_commit:
            raise _GitToolError(
                f"Push verification failed: local={pushed_commit}, upstream={verified_upstream}. "
                f"Commit {pushed_commit} did reach {remote}, but someone else pushed after it, "
                "so this branch was left untouched. Fetch and merge before publishing again."
            )

        git_index_text = _run_git(project_root, ["rev-parse", "--git-path", "index"], timeout=15)
        git_index = pathlib.Path(git_index_text)
        if not git_index.is_absolute():
            git_index = project_root / git_index
        git_index = git_index.resolve(strict=True)
        shutil.copy2(git_index, index_backup)
        try:
            # The published files are still "modified" here: their new content was
            # committed in the temporary worktree, not in this index. Staging them
            # makes the index agree with what was pushed — the bytes on disk are
            # already identical — so the fast-forward below has nothing to conflict
            # with. The end state has nothing staged, because index then equals HEAD.
            _run_git(project_root, ["add", "--all", "--", *paths], timeout=600)
            # Two trees, so this is an ordinary fast-forward: files upstream changed
            # are checked out, unrelated local edits are carried across, and a real
            # conflict fails loudly. Moving the branch ref alone used to leave every
            # upstream file looking locally reverted, and the next ordinary commit
            # would have pushed that revert back over the team's work.
            _run_git(
                project_root,
                ["read-tree", "-m", "-u", old_head, pushed_commit],
                timeout=120,
            )
            _run_git(
                project_root,
                [
                    "update-ref",
                    "-m",
                    "revvy publish selected paths",
                    f"refs/heads/{branch}",
                    pushed_commit,
                    old_head,
                ],
                timeout=30,
            )
        except BaseException as exc:
            # Any failure here, not just a Git one: the index is half-written and a
            # KeyboardInterrupt or OSError would otherwise leave it that way.
            try:
                os.replace(index_backup, git_index)
            except OSError:
                pass
            if isinstance(exc, _GitToolError):
                raise _GitToolError(
                    f"{exc} — commit {pushed_commit} already reached {remote}; "
                    f"this branch still points at {old_head}. "
                    "Fetch and fast-forward manually to pick it up."
                ) from exc
            raise
        return pushed_commit, push_output
    finally:
        if worktree_added:
            try:
                _run_git(
                    project_root,
                    ["worktree", "remove", "--force", str(worktree)],
                    timeout=120,
                )
            except _GitToolError:
                pass
            try:
                # remove() may have failed; without a prune the admin entry lingers
                # forever once the directory below is gone.
                _run_git(project_root, ["worktree", "prune"], timeout=60)
            except _GitToolError:
                pass
        try:
            index_backup.unlink(missing_ok=True)
        except OSError:
            pass
        shutil.rmtree(temporary_root, ignore_errors=True)


def handle_git_status(req_id, arguments: dict) -> dict:
    try:
        project_root = _configured_git_project(arguments.get("project_path"))
        return _synthetic_tool_response(req_id, _git_status_payload(project_root))
    except _GitToolError as exc:
        return _synthetic_tool_response(
            req_id,
            {"success": False, "error": str(exc)},
            is_error=True,
        )


def handle_git_asset_history(req_id, arguments: dict) -> dict:
    try:
        project_root = _configured_git_project(arguments.get("project_path"))
        paths = _validated_asset_history_paths(project_root, arguments.get("paths"))
        return _synthetic_tool_response(
            req_id,
            _git_asset_history_payload(project_root, paths),
        )
    except _GitToolError as exc:
        return _synthetic_tool_response(
            req_id,
            {"success": False, "error": str(exc)},
            is_error=True,
        )


def handle_git_lfs_object_status(req_id, arguments: dict) -> dict:
    try:
        project_root = _configured_git_project(arguments.get("project_path"))
        items = _validated_lfs_items(project_root, arguments.get("items"))
        return _synthetic_tool_response(
            req_id,
            _git_lfs_object_status_payload(project_root, items),
        )
    except (_GitToolError, OSError) as exc:
        return _synthetic_tool_response(
            req_id,
            {"success": False, "error": str(exc)},
            is_error=True,
        )


def handle_git_lfs_marker_search(req_id, arguments: dict) -> dict:
    try:
        project_root = _configured_git_project(arguments.get("project_path"))
        return _synthetic_tool_response(
            req_id,
            _git_lfs_marker_search_payload(project_root, arguments.get("markers")),
        )
    except (_GitToolError, OSError) as exc:
        return _synthetic_tool_response(
            req_id,
            {"success": False, "error": str(exc)},
            is_error=True,
        )


def handle_git_restore_lfs_objects(req_id, arguments: dict) -> dict:
    try:
        with _GIT_LOCK:
            project_root = _configured_git_project(arguments.get("project_path"))
            items = _validated_lfs_items(project_root, arguments.get("items"))
            return _synthetic_tool_response(
                req_id,
                _restore_verified_lfs_objects(project_root, items),
            )
    except (_GitToolError, OSError) as exc:
        return _synthetic_tool_response(
            req_id,
            {"success": False, "error": str(exc)},
            is_error=True,
        )


def handle_git_sync_ff_only(req_id, arguments: dict) -> dict:
    try:
        with _GIT_LOCK:
            project_root = _configured_git_project(arguments.get("project_path"))
            before = _git_status_payload(project_root)
            operation = _git_operation_in_progress(project_root)
            if operation:
                raise _GitToolError(f"Git {operation} is already in progress")
            if before["detached"]:
                raise _GitToolError("Cannot synchronize a detached HEAD")
            if not before["upstream"]:
                raise _GitToolError(f"Branch {before['branch']} has no upstream")
            if before["dirty"]:
                raise _GitToolError(
                    f"Working tree has {before['change_count']} local change(s); "
                    "commit or stash them explicitly before synchronization"
                )

            branch = str(before["branch"])
            remote = _run_git(
                project_root,
                ["config", "--get", f"branch.{branch}.remote"],
                timeout=15,
            )
            remotes = set(_run_git(project_root, ["remote"], timeout=15).splitlines())
            if not remote or remote == "." or remote not in remotes:
                raise _GitToolError(f"Branch {branch} has no valid remote")

            old_head = _run_git(project_root, ["rev-parse", "HEAD"], timeout=15)
            _run_git(project_root, ["fetch", "--no-tags", remote], timeout=180)
            after_fetch = _git_status_payload(project_root)
            if after_fetch["dirty"]:
                raise _GitToolError("Working tree changed while fetching; fast-forward was canceled")
            counts = _run_git(
                project_root,
                ["rev-list", "--left-right", "--count", "HEAD...@{upstream}"],
                timeout=30,
            ).split()
            if len(counts) != 2:
                raise _GitToolError(f"Could not determine ahead/behind counts: {' '.join(counts)}")
            ahead, behind = int(counts[0]), int(counts[1])
            if ahead and behind:
                raise _GitToolError(
                    f"Branch diverged from upstream (ahead {ahead}, behind {behind}); "
                    "manual merge or rebase is required"
                )
            if behind:
                if ahead:
                    raise _GitToolError("Fast-forward is not possible")
                _run_git(
                    project_root,
                    ["merge", "--ff-only", "--no-edit", "@{upstream}"],
                    timeout=120,
                )
            new_head = _run_git(project_root, ["rev-parse", "HEAD"], timeout=15)
            upstream_head = _run_git(
                project_root,
                ["rev-parse", "@{upstream}"],
                timeout=15,
            )
            if behind and new_head != upstream_head:
                raise _GitToolError("Fast-forward verification failed")
            final = _git_status_payload(project_root)
            final.update(
                {
                    "old_head": old_head,
                    "new_head": new_head,
                    "fetched_remote": remote,
                    "updated": old_head != new_head,
                    "result": "fast-forwarded" if old_head != new_head else "up-to-date",
                }
            )
            return _synthetic_tool_response(req_id, final)
    except (_GitToolError, TypeError, ValueError) as exc:
        return _synthetic_tool_response(
            req_id,
            {"success": False, "error": str(exc)},
            is_error=True,
        )


def handle_git_publish_paths(req_id, arguments: dict) -> dict:
    try:
        with _GIT_LOCK:
            project_root = _configured_git_project(
                arguments.get("project_path"),
                require_bundled_revvy=False,
            )
            paths = _validated_publish_paths(project_root, arguments.get("paths"))
            message = str(arguments.get("message") or "").strip()
            if not message or len(message) > 200 or any(character in message for character in ("\r", "\n")):
                raise _GitToolError("message must be one non-empty line of at most 200 characters")
            allow_existing_ahead = arguments.get("allow_existing_ahead")
            if not isinstance(allow_existing_ahead, bool):
                raise _GitToolError("allow_existing_ahead must be an explicit boolean")
            allow_upstream_rebase = arguments.get("allow_upstream_rebase", False)
            if not isinstance(allow_upstream_rebase, bool):
                raise _GitToolError("allow_upstream_rebase must be a boolean when provided")

            before = _git_status_payload(project_root)
            operation = _git_operation_in_progress(project_root)
            if operation:
                raise _GitToolError(f"Git {operation} is already in progress")
            if before["detached"]:
                raise _GitToolError("Cannot publish a detached HEAD")
            if not before["upstream"]:
                raise _GitToolError(f"Branch {before['branch']} has no upstream")
            staged_before = _run_git(
                project_root,
                ["diff", "--cached", "--name-only", "--"],
                timeout=30,
            )
            if staged_before:
                raise _GitToolError(
                    "The repository already has staged changes; publish them or unstage them explicitly first"
                )

            branch = str(before["branch"])
            remote = _run_git(
                project_root,
                ["config", "--get", f"branch.{branch}.remote"],
                timeout=15,
            )
            merge_ref = _run_git(
                project_root,
                ["config", "--get", f"branch.{branch}.merge"],
                timeout=15,
            )
            remotes = set(_run_git(project_root, ["remote"], timeout=15).splitlines())
            if not remote or remote == "." or remote not in remotes:
                raise _GitToolError(f"Branch {branch} has no valid remote")
            if not re.fullmatch(r"refs/heads/[A-Za-z0-9._/-]+", merge_ref or ""):
                raise _GitToolError(f"Branch {branch} has an invalid upstream ref")

            _run_git(project_root, ["fetch", "--no-tags", remote], timeout=180)
            counts = _run_git(
                project_root,
                ["rev-list", "--left-right", "--count", "HEAD...@{upstream}"],
                timeout=30,
            ).split()
            if len(counts) != 2:
                raise _GitToolError(f"Could not determine ahead/behind counts: {' '.join(counts)}")
            ahead, behind = int(counts[0]), int(counts[1])
            if behind and ahead:
                raise _GitToolError(
                    f"Branch is diverged after fetch (ahead {ahead}, behind {behind}); synchronize first"
                )
            if ahead and not allow_existing_ahead:
                raise _GitToolError(
                    f"Branch already has {ahead} unpublished commit(s); set allow_existing_ahead=true to publish them"
                )
            if behind and not allow_upstream_rebase:
                # This path rebuilds the commit on top of upstream and moves the
                # branch, so it changes more of the repository than an ordinary
                # publish. It is opt-in for the same reason allow_existing_ahead is.
                raise _GitToolError(
                    f"Branch is {behind} commit(s) behind its upstream. Set "
                    "allow_upstream_rebase=true to rebuild the commit on top of upstream and "
                    "fast-forward this branch, or synchronize with git_sync_ff_only first"
                )

            selected_changes = _run_git(
                project_root,
                ["status", "--porcelain=v1", "--untracked-files=all", "--", *paths],
                timeout=30,
            )
            if not selected_changes:
                raise _GitToolError("None of the selected paths has a change to publish")

            old_head = _run_git(project_root, ["rev-parse", "HEAD"], timeout=15)
            if behind:
                upstream_head = _run_git(project_root, ["rev-parse", "@{upstream}"], timeout=15)
                commit, push_output = _publish_paths_on_upstream(
                    project_root,
                    paths=paths,
                    message=message,
                    branch=branch,
                    remote=remote,
                    merge_ref=merge_ref,
                    old_head=old_head,
                    upstream_head=upstream_head,
                )
                final = _git_status_payload(project_root)
                final.update(
                    {
                        "commit": commit,
                        "commit_message": message,
                        "published_paths": paths,
                        "published_existing_commits": 0,
                        "published_on_upstream": True,
                        "pushed_remote": remote,
                        "push_result": push_output[-1000:],
                    }
                )
                return _synthetic_tool_response(req_id, final)

            staged = False
            try:
                _run_git(project_root, ["add", "--all", "--", *paths], timeout=60)
                staged = True
                staged_paths = [
                    line.strip()
                    for line in _run_git(
                        project_root,
                        ["diff", "--cached", "--name-only", "--"],
                        timeout=30,
                    ).splitlines()
                    if line.strip()
                ]
                if not staged_paths:
                    raise _GitToolError("Selected paths produced no staged changes")
                allowed_keys = {
                    path.casefold() if os.name == "nt" else path
                    for path in paths
                }
                unexpected = [
                    path for path in staged_paths
                    if (path.casefold() if os.name == "nt" else path) not in allowed_keys
                ]
                if unexpected:
                    raise _GitToolError(
                        "Staging selected an unexpected path: " + ", ".join(unexpected[:3])
                    )
                _run_git(
                    project_root,
                    [
                        "-c",
                        f"core.hooksPath={os.devnull}",
                        "commit",
                        "--no-gpg-sign",
                        "-m",
                        message,
                    ],
                    timeout=120,
                )
                staged = False
            except _GitToolError:
                if staged and _run_git(project_root, ["rev-parse", "HEAD"], timeout=15) == old_head:
                    try:
                        _run_git(project_root, ["restore", "--staged", "--", *paths], timeout=30)
                    except _GitToolError:
                        pass
                raise

            commit = _run_git(project_root, ["rev-parse", "HEAD"], timeout=15)
            if commit == old_head:
                raise _GitToolError("Commit did not advance HEAD")
            push_output = _run_git(
                project_root,
                ["push", "--porcelain", remote, f"HEAD:{merge_ref}"],
                timeout=180,
            )
            _run_git(project_root, ["fetch", "--no-tags", remote], timeout=180)
            upstream_head = _run_git(project_root, ["rev-parse", "@{upstream}"], timeout=15)
            if upstream_head != commit:
                raise _GitToolError(
                    f"Push verification failed: local={commit}, upstream={upstream_head}"
                )
            final = _git_status_payload(project_root)
            final.update(
                {
                    "commit": commit,
                    "commit_message": message,
                    "published_paths": paths,
                    "published_existing_commits": ahead,
                    "pushed_remote": remote,
                    "push_result": push_output[-1000:],
                }
            )
            return _synthetic_tool_response(req_id, final)
    except (_GitToolError, TypeError, ValueError) as exc:
        return _synthetic_tool_response(
            req_id,
            {"success": False, "error": str(exc)},
            is_error=True,
        )


def _configure_vfx_texture(asset_path: str, usage: str) -> tuple[bool, str]:
    srgb = usage not in ("mask", "tileable_noise", "flowmap", "normal")
    compression = {
        "mask": "TC_MASKS",
        "tileable_noise": "TC_MASKS",
        "flowmap": "TC_MASKS",
        "normal": "TC_NORMALMAP",
    }.get(usage, "TC_DEFAULT")
    code = f'''import unreal, json
asset_path = {json.dumps(asset_path)}
tex = unreal.load_asset(asset_path)
if not isinstance(tex, unreal.Texture2D):
    raise RuntimeError("Imported asset is not Texture2D: " + asset_path)
tex.modify()
tex.set_editor_property("srgb", {str(srgb)})
tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.{compression})
tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_EFFECTS)
tex.set_editor_property("never_stream", True)
tex.set_editor_property("use_new_mip_filter", True)
tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
if {str(usage in ("mask", "tileable_noise", "flowmap"))}:
    tex.set_editor_property("oodle_preserve_extremes", True)
print(json.dumps({{"success": True, "asset_path": asset_path, "size": [tex.blueprint_get_size_x(), tex.blueprint_get_size_y()]}}))
'''
    payload, error = _call_ue_tool("execute_python_code", {"code": code})
    if error:
        return False, error
    saved, error = _call_ue_tool("manage_asset", {"action": "save", "asset_path": asset_path})
    if error:
        return False, error
    return bool(payload is not None and saved is not None), ""


def handle_generate_vfx_texture(req_id, arguments: dict) -> dict:
    try:
        import revvy_gen_texture

        allowed_arguments = {
            "subject",
            "usage",
            "style",
            "destination_path",
            "asset_name",
            "size",
            "grid_columns",
            "grid_rows",
            "quality_threshold",
            "attempts",
            "model",
            "replace_existing",
        }
        unknown_arguments = sorted(set(arguments) - allowed_arguments)
        if unknown_arguments:
            raise ValueError(
                "unsupported generate_vfx_texture argument(s): "
                + ", ".join(unknown_arguments)
            )

        def string_argument(name, *, default=None, required=False):
            if name not in arguments:
                if required:
                    raise TypeError(f"{name} must be a string")
                return default
            value = arguments[name]
            if not isinstance(value, str):
                raise TypeError(f"{name} must be a string")
            return value.strip()

        def integer_argument(name, default, minimum, maximum):
            value = arguments.get(name, default)
            if type(value) is not int:
                raise TypeError(f"{name} must be an integer")
            if not minimum <= value <= maximum:
                raise ValueError(f"{name} must be between {minimum} and {maximum}")
            return value

        core = revvy_gen_texture.vfx_core
        subject = string_argument("subject", required=True)
        usage = string_argument("usage", required=True).lower()
        style = string_argument("style", default="production game VFX")
        if len(style) > 1200:
            raise ValueError("style must contain at most 1200 characters")
        destination_value = string_argument("destination_path", required=True)
        if len(destination_value) > 512:
            raise ValueError("destination_path must contain at most 512 characters")
        destination = core.validate_content_folder(destination_value)
        asset_name_value = string_argument("asset_name", required=True)
        if len(asset_name_value) > 128:
            raise ValueError("asset_name must contain at most 128 characters")
        asset_name = core.validate_texture_name(asset_name_value)
        size = integer_argument("size", 1024, 512, 1024)
        if size not in (512, 1024):
            raise ValueError("size must be 512 or 1024")
        columns = integer_argument("grid_columns", 1, 1, 8)
        rows = integer_argument("grid_rows", 1, 1, 8)
        threshold = integer_argument("quality_threshold", 72, 50, 95)
        attempts = integer_argument("attempts", 2, 1, 3)
        model = string_argument("model", default=None)
        if model is not None and not re.fullmatch(
            r"[A-Za-z0-9][A-Za-z0-9._:/-]{0,127}",
            model,
        ):
            raise ValueError("model must be a valid Codex model identifier")
        replace_existing = arguments.get("replace_existing", False)
        if type(replace_existing) is not bool:
            raise TypeError("replace_existing must be a boolean")
        if replace_existing:
            raise ValueError(
                "replace_existing=true is unavailable: Unreal texture import and "
                "post-import configuration do not provide an atomic rollback contract; "
                "the existing asset was preserved"
            )
        core.build_texture_prompt(subject, usage, style=style, size=size, grid=(columns, rows))
    except (ImportError, TypeError, ValueError) as exc:
        return _synthetic_tool_response(
            req_id, {"success": False, "error": str(exc)}, is_error=True
        )

    config = load_runtime_config()
    active_project = _project_from_config(config, "active_project")
    installed_project = _project_from_config(config, "installed_project")
    project = active_project or installed_project
    project_dir = pathlib.Path(str((project or {}).get("path", ""))).expanduser()
    if not project_dir.is_dir():
        return _synthetic_tool_response(
            req_id,
            {"success": False, "error": "No verified Unreal project directory is configured"},
            is_error=True,
        )

    asset_path = f"{destination}/{asset_name}"
    with _vfx_asset_transaction(asset_path):
        # Serialize the find/generate/import transaction per Unreal asset path.
        # Different assets still generate concurrently, while same-name calls
        # cannot both observe "missing" and race into a destructive import.
        ue_running = _ue_running(config, timeout=2.0)
        if ue_running:
            found, error = _call_ue_tool(
                "manage_asset",
                {"action": "find", "asset_path": asset_path},
            )
            if error:
                return _synthetic_tool_response(
                    req_id,
                    {"success": False, "error": error},
                    is_error=True,
                )
            if found and found.get("found") and not replace_existing:
                return _synthetic_tool_response(
                    req_id,
                    {"success": False, "error": f"Asset already exists: {asset_path}"},
                    is_error=True,
                )

        output_dir = STATE_DIR / "generated-vfx" / time.strftime("%Y%m%d")
        try:
            output_dir.mkdir(parents=True, exist_ok=True)
        except OSError as exc:
            return _synthetic_tool_response(
                req_id,
                {"success": False, "error": f"Could not create VFX staging directory: {exc}"},
                is_error=True,
            )
        source_png = output_dir / (
            f"{asset_name}_{time.time_ns()}_{secrets.token_hex(4)}.png"
        )
        try:
            generated = revvy_gen_texture.generate_vfx_texture_file(
                subject,
                source_png,
                project_dir,
                usage=usage,
                style=style,
                size=size,
                grid=(columns, rows),
                quality_threshold=threshold,
                max_attempts=attempts,
                model=model,
                timeout=300,
            )
        except (OSError, ValueError) as exc:
            generated = {"success": False, "message": str(exc), "source_png": ""}

        if not generated.get("success"):
            generated.update({"asset_path": asset_path, "imported": False})
            return _synthetic_tool_response(req_id, generated, is_error=True)

        if not ue_running:
            generated.update(
                {
                    "success": True,
                    "asset_path": asset_path,
                    "imported": False,
                    "message": (
                        "Texture passed QA and was staged; "
                        "launch Unreal and import the source PNG"
                    ),
                }
            )
            return _synthetic_tool_response(req_id, generated)

        imported, error = _call_ue_tool(
            "manage_asset",
            {
                "action": "import",
                "source_file_path": str(source_png),
                "destination_path": destination,
                "asset_name": asset_name,
            },
        )
        if error:
            generated.update(
                {
                    "success": False,
                    "imported": False,
                    "asset_path": asset_path,
                    "error": error,
                }
            )
            return _synthetic_tool_response(req_id, generated, is_error=True)

        configured, error = _configure_vfx_texture(asset_path, usage)
        generated.update(
            {
                "success": bool(configured),
                "imported": True,
                "configured": bool(configured),
                "asset_path": asset_path,
                "import_result": imported,
            }
        )
        if error:
            generated["error"] = error
        return _synthetic_tool_response(req_id, generated, is_error=not configured)

# ---------------------------------------------------------------------------
# HTTP handler
# ---------------------------------------------------------------------------


def is_trusted_origin(
    origin: str | None,
    config: dict | None = None,
    *,
    expected_host: str | None = None,
) -> bool:
    """Allow native clients, the exact loopback endpoint, and configured origins."""
    if not origin:
        return True
    runtime_config = config if config is not None else load_runtime_config()
    allowed_origins = runtime_config.get("allowed_origins", [])
    if isinstance(allowed_origins, list) and origin in allowed_origins:
        return True
    if not expected_host:
        return False
    try:
        parsed_origin = urllib.parse.urlsplit(origin)
        parsed_host = urllib.parse.urlsplit(f"//{expected_host}")
        origin_name = str(parsed_origin.hostname or "").casefold()
        expected_name = str(parsed_host.hostname or "").casefold()
        loopback_names = {"localhost", "127.0.0.1", "::1"}
        return bool(
            parsed_origin.scheme in {"http", "https"}
            and origin_name in loopback_names
            and origin_name == expected_name
            and parsed_origin.port == parsed_host.port
            and parsed_origin.path in {"", "/"}
            and not parsed_origin.query
            and not parsed_origin.fragment
            and parsed_origin.username is None
            and parsed_origin.password is None
        )
    except ValueError:
        return False


class ProxyHandler(BaseHTTPRequestHandler):

    def log_message(self, fmt, *args):
        pass

    def _is_mcp_path(self) -> bool:
        return self.path in {"/mcp", "/mcp/safe"}

    def _read_only_endpoint(self) -> bool:
        return self.path == "/mcp/safe"

    def _has_full_access_bearer(self) -> bool:
        supplied = self.headers.get("Authorization", "")
        prefix = "Bearer "
        token = supplied[len(prefix):] if supplied.startswith(prefix) else ""
        return bool(_FULL_ACCESS_TOKEN and token) and secrets.compare_digest(
            token, _FULL_ACCESS_TOKEN
        )

    def _full_access_authorized(self) -> bool:
        if self.path != "/mcp":
            return True
        if self._has_full_access_bearer():
            return True
        log("Rejected unauthenticated Full access MCP request")
        self._json(
            401,
            {
                "error": (
                    "Full access authorization is required. Use /mcp/safe for read-only "
                    "access, or send 'Authorization: Bearer $REVVY_FULL_ACCESS_TOKEN'."
                )
            },
        )
        return False

    def _origin_allowed(self) -> bool:
        origin = self.headers.get("Origin")
        if is_trusted_origin(origin, expected_host=self.headers.get("Host")):
            return True
        log(f"Rejected non-local Origin: {origin}")
        self._json(403, {"error": "Origin is not allowed"}, include_cors=False)
        return False

    def _send_cors(self):
        origin = self.headers.get("Origin")
        if origin and is_trusted_origin(
            origin, expected_host=self.headers.get("Host")
        ):
            self.send_header("Access-Control-Allow-Origin", origin)
            self.send_header("Vary", "Origin, Access-Control-Request-Headers")
        self.send_header("Access-Control-Allow-Methods", "POST, GET, DELETE, OPTIONS")
        allowed_headers = [
            "Content-Type",
            "Authorization",
            "MCP-Protocol-Version",
            "Mcp-Method",
            "Mcp-Name",
            "mcp-session-id",
        ]
        seen_headers = {header.casefold() for header in allowed_headers}
        requested_headers = self.headers.get("Access-Control-Request-Headers", "")
        for requested in requested_headers.split(","):
            requested = requested.strip()
            folded = requested.casefold()
            if (
                folded.startswith("mcp-param-")
                and _HTTP_FIELD_TOKEN_RE.fullmatch(requested) is not None
                and folded not in seen_headers
            ):
                allowed_headers.append(requested)
                seen_headers.add(folded)
        self.send_header(
            "Access-Control-Allow-Headers",
            ", ".join(allowed_headers),
        )

    def do_OPTIONS(self):
        if not self._is_mcp_path() or not self._origin_allowed():
            if not self._is_mcp_path():
                self.send_error(404)
            return
        self.send_response(204)
        self._send_cors()
        self.end_headers()

    def do_GET(self):
        if not self._origin_allowed():
            return
        if self.path == "/health":
            self._json(200, build_health(include_private=self._has_full_access_bearer()))
            return
        if self.path not in ("/", "/mcp", "/mcp/safe"):
            self.send_error(404)
            return
        if not self._full_access_authorized():
            return

        accept = self.headers.get("Accept", "")
        if self.path == "/" or "text/event-stream" not in accept:
            self._text(200, "Revvy proxy running")
            return

        log("SSE stream opened")
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "keep-alive")
        self._send_cors()
        self.end_headers()

        try:
            while True:
                self.wfile.write(b": heartbeat\n\n")
                self.wfile.flush()
                time.sleep(15)
        except (BrokenPipeError, ConnectionResetError, OSError):
            log("SSE stream closed")

    def do_DELETE(self):
        if not self._is_mcp_path() or not self._origin_allowed():
            if not self._is_mcp_path():
                self.send_error(404)
            return
        if not self._full_access_authorized():
            return
        # The proxy owns the client-facing session; backend calls are stateless.
        self._text(200, "Session terminated")

    def do_POST(self):
        self._modern_request = False
        if not self._is_mcp_path():
            self.send_error(404)
            return
        if not self._origin_allowed():
            return

        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            self._jsonrpc_error(None, -32600, "Invalid Content-Length", status=400)
            return
        if length <= 0 or length > MAX_REQUEST_BYTES:
            self._jsonrpc_error(None, -32600, "Request body size is invalid", status=413)
            return
        raw_body = self.rfile.read(length)
        if len(raw_body) != length:
            self._jsonrpc_error(None, -32600, "Incomplete request body", status=400)
            return
        if not self._full_access_authorized():
            return
        lower_headers = self._lower_headers()

        try:
            rpc = json.loads(raw_body)
        except json.JSONDecodeError:
            self._jsonrpc_error(None, -32700, "Parse error", status=400)
            return
        if not isinstance(rpc, dict):
            self._jsonrpc_error(None, -32600, "Invalid Request", status=400)
            return
        if rpc.get("jsonrpc") != "2.0" or not isinstance(rpc.get("method"), str):
            self._jsonrpc_error(rpc.get("id"), -32600, "Invalid Request", status=400)
            return

        method = rpc["method"]
        req_id = rpc.get("id")
        is_notification = "id" not in rpc
        params = rpc.get("params") or {}
        if not isinstance(params, dict):
            self._jsonrpc_error(req_id, -32602, "Params must be an object")
            return

        is_modern, modern_error = _modern_request_validation(rpc, lower_headers)
        self._modern_request = is_modern
        if modern_error is not None:
            self._jsonrpc_error(
                req_id,
                modern_error["code"],
                modern_error["message"],
                status=modern_error["status"],
                data=modern_error.get("data"),
            )
            return
        if is_modern and is_notification:
            self._jsonrpc_error(
                None,
                -32600,
                "Modern MCP Streamable HTTP methods require a JSON-RPC request id",
                status=400,
            )
            return
        if is_modern and method not in _MODERN_LOCAL_METHODS:
            self._jsonrpc_error(req_id, -32601, "Method not found", status=404)
            return

        request_tool_definition = None
        upstream_catalog_revision = None
        upstream_owned = False
        if is_modern and method == "tools/call":
            tool_name = params.get("name")
            arguments = params.get("arguments") or {}
            if not isinstance(arguments, dict):
                self._jsonrpc_error(req_id, -32602, "Tool arguments must be an object")
                return
            upstream_owned = isinstance(tool_name, str) and UPSTREAM_MANAGER.owns_namespace(
                tool_name
            )
            if upstream_owned:
                try:
                    upstream_definition = UPSTREAM_MANAGER.definition_for(
                        tool_name,
                        require_read_only=self._read_only_endpoint(),
                    )
                    if upstream_definition is not None:
                        request_tool_definition = upstream_definition.definition
                        upstream_catalog_revision = (
                            upstream_definition.catalog_revision
                        )
                    else:
                        raise UpstreamError(
                            f"External MCP tool definition is unavailable: {tool_name}"
                        )
                except MutationDenied as exc:
                    log(f"{tool_name} -> external MCP mutation denied before activation")
                    self._raw_json(
                        _synthetic_tool_response(
                            req_id,
                            {"success": False, "error": str(exc)},
                            is_error=True,
                        )
                    )
                    return
                except UpstreamError as exc:
                    log(f"{tool_name} -> external MCP activation error: {exc}")
                    self._raw_json(
                        _synthetic_tool_response(
                            req_id,
                            {"success": False, "error": str(exc)},
                            is_error=True,
                        )
                    )
                    return
            else:
                request_tool_definition = next(
                    (
                        tool
                        for tool in merged_tools(
                            allow_upstream_start=not self._read_only_endpoint()
                        )
                        if isinstance(tool, dict) and tool.get("name") == tool_name
                    ),
                    None,
                )
            if request_tool_definition is not None:
                header_error = _modern_tool_header_validation(
                    arguments, lower_headers, request_tool_definition
                )
                if header_error is not None:
                    self._jsonrpc_error(
                        req_id,
                        header_error["code"],
                        header_error["message"],
                        status=header_error["status"],
                    )
                    return

        response_fields = []
        response_omit = []
        if method == "tools/call":
            raw_arguments = params.get("arguments") or {}
            if isinstance(raw_arguments, dict):
                arguments, response_fields, response_omit, has_controls = (
                    _extract_response_controls(raw_arguments)
                )
                if has_controls:
                    params = dict(params)
                    params["arguments"] = arguments
                    rpc = dict(rpc)
                    rpc["params"] = params
                    raw_body = json.dumps(rpc, ensure_ascii=False).encode("utf-8")

        if method == "server/discover" and not is_modern:
            self._jsonrpc_error(req_id, -32601, "Method not found")
            return

        if is_modern and method == "server/discover":
            log(f"server/discover ({MODERN_PROTOCOL_VERSION})")
            self._jsonrpc(
                req_id,
                {
                    "supportedVersions": [MODERN_PROTOCOL_VERSION],
                    "capabilities": {"tools": {}, "resources": {}},
                    "instructions": (
                        "Revvy exposes engine tools through a persistent local proxy. "
                        "Query current state before mutation, load the relevant skill, "
                        "activate dormant typed bridges with manage_upstreams, and verify "
                        "the result after each change. Skill indexes are "
                        "available as skill:// resources for progressive disclosure."
                    ),
                    "ttlMs": 300_000,
                    "cacheScope": "public",
                },
            )
            return

        if method == "initialize":
            requested = params.get("protocolVersion", "2024-11-05")
            negotiated = (
                requested
                if requested in SUPPORTED_PROTOCOL_VERSIONS
                else SUPPORTED_PROTOCOL_VERSIONS[0]
            )
            log(f"initialize (offline-capable, protocol {negotiated})")
            self._jsonrpc(req_id, {
                "protocolVersion": negotiated,
                "capabilities": {"tools": {}},
                "serverInfo": {
                    **SERVER_INFO,
                    "instructions": (
                        "Revvy exposes Unreal Engine tools through a persistent local proxy. "
                        "Query current state before mutation, load the relevant skill, "
                        "activate dormant typed bridges with manage_upstreams, and verify the "
                        "result after each change. Call revvy_status when Unreal is unreachable."
                    ),
                },
            })
            return

        if is_notification:
            # Safe sessions never forward notifications: some MCP servers execute a
            # tools/call notification even though JSON-RPC does not return a result.
            if self._read_only_endpoint():
                log(f"{method} -> ignored by read-only MCP endpoint")
            else:
                forward_body, forward_headers = (
                    _legacy_bridge_request(rpc, lower_headers)
                    if is_modern
                    else (raw_body, lower_headers)
                )
                forward_to_ue(forward_body, forward_headers)
            self.send_response(202)
            self._send_cors()
            self.end_headers()
            return

        if method == "tools/list":
            tools = merged_tools(
                allow_upstream_start=not self._read_only_endpoint()
            )
            if self._read_only_endpoint():
                visible = (
                    tool
                    for tool in tools
                    if _tool_visible_to_safe(str(tool.get("name") or ""), tool)
                )
                tools = [
                    tool for tool in map(_annotate_safe_tool, visible) if tool is not None
                ]
            if is_modern:
                tools = _modern_tools(tools)
            log(f"tools/list -> {len(tools)} tools")
            result = {"tools": tools}
            if is_modern:
                result.update(
                    {"ttlMs": MODERN_CACHE_TTL_MS, "cacheScope": "private"}
                )
            self._jsonrpc(req_id, result)
            return

        if is_modern and method == "resources/list":
            result = list_skill_resources_payload()
            if is_modern:
                result.update(
                    {"ttlMs": MODERN_CACHE_TTL_MS, "cacheScope": "private"}
                )
            self._jsonrpc(req_id, result)
            return

        if is_modern and method == "resources/templates/list":
            result = list_skill_resource_templates_payload()
            if is_modern:
                result.update(
                    {"ttlMs": MODERN_CACHE_TTL_MS, "cacheScope": "public"}
                )
            self._jsonrpc(req_id, result)
            return

        if is_modern and method == "resources/read":
            uri = params.get("uri")
            try:
                result = read_skill_resource_payload(uri)
            except (OSError, UnicodeError, ValueError) as exc:
                self._jsonrpc_error(
                    req_id,
                    -32602,
                    f"Resource not found: {exc}",
                    data={"uri": uri},
                )
                return
            if is_modern:
                result.update(
                    {"ttlMs": MODERN_CACHE_TTL_MS, "cacheScope": "private"}
                )
            self._jsonrpc(req_id, result)
            return

        if method == "ping":
            self._jsonrpc(req_id, {})
            return

        if method == "tools/call":
            tool_name = params.get("name", "")
            if not isinstance(tool_name, str) or not tool_name:
                self._jsonrpc_error(req_id, -32602, "Missing tool name")
                return
            arguments = params.get("arguments") or {}
            if not isinstance(arguments, dict):
                self._jsonrpc_error(req_id, -32602, "Tool arguments must be an object")
                return
            upstream_owned = upstream_owned or UPSTREAM_MANAGER.owns_namespace(tool_name)
            if upstream_owned:
                stable_request_id = f"revvy_{secrets.token_hex(16)}"
                try:
                    result = UPSTREAM_MANAGER.call_tool(
                        tool_name,
                        arguments,
                        require_read_only=self._read_only_endpoint(),
                        request_id=stable_request_id,
                        expected_catalog_revision=upstream_catalog_revision,
                    )
                except MutationAcknowledgementLost as exc:
                    log(f"{tool_name} -> external MCP acknowledgement lost")
                    details = exc.details
                    self._raw_json(
                        _synthetic_tool_response(
                            req_id,
                            {
                                "success": False,
                                "error": {
                                    "code": "upstream_ack_lost",
                                    "message": str(exc),
                                    "details": details,
                                },
                                **details,
                            },
                            is_error=True,
                        )
                    )
                    return
                except MutationDenied as exc:
                    log(f"{tool_name} -> external MCP mutation denied")
                    self._raw_json(
                        _synthetic_tool_response(
                            req_id,
                            {"success": False, "error": str(exc)},
                            is_error=True,
                        )
                    )
                    return
                except UpstreamError as exc:
                    log(f"{tool_name} -> external MCP error: {exc}")
                    self._raw_json(
                        _synthetic_tool_response(
                            req_id,
                            {"success": False, "error": str(exc)},
                            is_error=True,
                        )
                    )
                    return
                log(f"{tool_name} -> handled by external MCP")
                self._raw_json(
                    _shape_tool_response(
                        {"jsonrpc": "2.0", "id": req_id, "result": result},
                        response_fields,
                        response_omit,
                    )
                )
                return
            if self._read_only_endpoint():
                definition = request_tool_definition
                if definition is None:
                    definitions = {
                        str(tool.get("name")): tool
                        for tool in merged_tools(allow_upstream_start=False)
                        if isinstance(tool, dict) and tool.get("name")
                    }
                    definition = definitions.get(tool_name)
                denial = _safe_action_denial(tool_name, definition, arguments)
                if denial is not None:
                    log(f"{tool_name} -> blocked by read-only MCP endpoint")
                    self._raw_json(
                        _synthetic_tool_response(
                            req_id,
                            {"success": False, "error": denial},
                            is_error=True,
                        )
                    )
                    return
            if tool_name == "revvy_status":
                log("revvy_status -> handled by proxy")
                self._raw_json(
                    _shape_tool_response(
                        handle_revvy_status(req_id), response_fields, response_omit
                    )
                )
                return
            if tool_name == "manage_skills":
                log("manage_skills -> handled by proxy")
                self._raw_json(
                    _shape_tool_response(
                        handle_manage_skills(req_id, arguments),
                        response_fields,
                        response_omit,
                    )
                )
                return
            if tool_name == "manage_upstreams":
                log("manage_upstreams -> handled by proxy")
                self._raw_json(
                    _shape_tool_response(
                        handle_manage_upstreams(
                            req_id,
                            arguments,
                            allow_activation=not self._read_only_endpoint(),
                        ),
                        response_fields,
                        response_omit,
                    )
                )
                return
            if tool_name in {
                "git_status",
                "git_asset_history",
                "git_lfs_object_status",
                "git_lfs_marker_search",
                "git_restore_lfs_objects",
                "git_sync_ff_only",
                "git_publish_paths",
            }:
                log(f"{tool_name} -> handled by proxy")
                handler = {
                    "git_status": handle_git_status,
                    "git_asset_history": handle_git_asset_history,
                    "git_lfs_object_status": handle_git_lfs_object_status,
                    "git_lfs_marker_search": handle_git_lfs_marker_search,
                    "git_restore_lfs_objects": handle_git_restore_lfs_objects,
                    "git_sync_ff_only": handle_git_sync_ff_only,
                    "git_publish_paths": handle_git_publish_paths,
                }[tool_name]
                self._raw_json(
                    _shape_tool_response(
                        handler(req_id, arguments), response_fields, response_omit
                    )
                )
                return
            if tool_name == "generate_vfx_texture":
                log("generate_vfx_texture -> handled by proxy")
                self._raw_json(
                    _shape_tool_response(
                        handle_generate_vfx_texture(req_id, arguments),
                        response_fields,
                        response_omit,
                    )
                )
                return

        forward_body, forward_headers = (
            _legacy_bridge_request(rpc, lower_headers)
            if is_modern
            else (raw_body, lower_headers)
        )
        success, response_bytes = forward_to_ue(
            forward_body,
            forward_headers,
            read_only=self._read_only_endpoint(),
        )
        if success and response_bytes:
            try:
                forwarded_response = json.loads(response_bytes.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError):
                forwarded_response = None
            if isinstance(forwarded_response, dict):
                if method == "tools/call":
                    shaped_response = _add_did_you_mean(
                        forwarded_response, params.get("name", "")
                    )
                    shaped_response = _shape_tool_response(
                        shaped_response, response_fields, response_omit
                    )
                    if shaped_response is not forwarded_response:
                        forwarded_response = shaped_response
                        response_bytes = json.dumps(
                            shaped_response, ensure_ascii=False
                        ).encode("utf-8")
                if is_modern:
                    self._raw_json(forwarded_response)
                    log(f"{method} -> forwarded to UE")
                    return
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(response_bytes)))
            self._send_cors()
            self.end_headers()
            self.wfile.write(response_bytes)
            log(f"{method} -> forwarded to UE")
            return

        ue_msg = response_bytes.decode(errors="replace").strip() if response_bytes else ""
        log(f"{method} -> UE error: {ue_msg or '(no response - UE not running or unreachable)'}")
        if method == "tools/call":
            self._raw_json(
                _add_did_you_mean(
                    ue_error_response(req_id, params.get("name", "unknown"), ue_msg),
                    params.get("name", ""),
                )
            )
        else:
            if response_bytes:
                try:
                    backend_error = json.loads(response_bytes.decode("utf-8"))
                except (UnicodeDecodeError, json.JSONDecodeError):
                    backend_error = None
                if (
                    isinstance(backend_error, dict)
                    and backend_error.get("jsonrpc") == "2.0"
                    and isinstance(backend_error.get("error"), dict)
                ):
                    backend_error["id"] = req_id
                    self._raw_json(backend_error)
                    return
            message = (
                f"Unreal Engine rejected the request: {ue_msg}"
                if ue_msg
                else "Unreal Engine is unavailable"
            )
            self._jsonrpc_error(req_id, -32000, message)

    def _lower_headers(self) -> dict:
        return {k.lower(): v for k, v in self.headers.items()}

    def _jsonrpc(self, req_id, result: dict):
        self._raw_json({"jsonrpc": "2.0", "id": req_id, "result": result})

    def _jsonrpc_error(
        self,
        req_id,
        code: int,
        message: str,
        status: int = 200,
        data: dict | None = None,
    ):
        error = {"code": code, "message": message}
        if data is not None:
            error["data"] = data
        self._json(
            status,
            {"jsonrpc": "2.0", "id": req_id, "error": error},
        )

    def _raw_json(self, data: dict):
        if getattr(self, "_modern_request", False):
            data = _modernize_jsonrpc_response(data)
        self._json(200, data)

    def _json(self, status: int, data: dict, include_cors: bool = True):
        body = json.dumps(data, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        if include_cors:
            self._send_cors()
        self.end_headers()
        self.wfile.write(body)

    def _text(self, status: int, value: str):
        body = value.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self._send_cors()
        self.end_headers()
        self.wfile.write(body)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

class QuietThreadingHTTPServer(ThreadingHTTPServer):
    """Suppress noisy tracebacks from normal client disconnects."""
    daemon_threads = True
    # Python's SO_REUSEADDR behaviour on Windows allows several Revvy
    # processes to bind the exact same loopback port. Connections are then
    # routed to an arbitrary project, which defeats the project-local config
    # ownership checks. Keep quick restarts on POSIX, but make Windows binds
    # exclusive so a collision fails loudly instead of cross-wiring projects.
    allow_reuse_address = os.name != "nt"

    def server_bind(self):
        if os.name == "nt" and hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
            self.socket.setsockopt(
                socket.SOL_SOCKET,
                socket.SO_EXCLUSIVEADDRUSE,
                1,
            )
        super().server_bind()

    def handle_error(self, request, client_address):
        exc_type = sys.exc_info()[0]
        if exc_type in (BrokenPipeError, ConnectionAbortedError, ConnectionResetError):
            return
        super().handle_error(request, client_address)


def _remove_owned_process_state() -> None:
    try:
        process_state = _read_json(PROCESS_PATH)
        if process_state.get("pid") == os.getpid():
            PROCESS_PATH.unlink(missing_ok=True)
    except (OSError, ValueError, json.JSONDecodeError):
        pass


def _serve_until_stopped(server: HTTPServer) -> None:
    """Serve until interrupted and always retire independent upstream sessions."""

    previous_sigterm = None
    if hasattr(signal, "SIGTERM"):
        previous_sigterm = signal.getsignal(signal.SIGTERM)

        def _graceful_sigterm(_signum, _frame):
            # Unwind the main thread through the shutdown path so MCP upstreams
            # started in independent sessions cannot survive proxy termination.
            raise KeyboardInterrupt

        signal.signal(signal.SIGTERM, _graceful_sigterm)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        log("Proxy stopped.")
    finally:
        # Cleanup stages are independent: a socket-close or handler-restore
        # failure must never skip retiring separately-sessioned MCP children.
        try:
            if previous_sigterm is not None:
                signal.signal(signal.SIGTERM, previous_sigterm)
        finally:
            try:
                server.server_close()
            finally:
                try:
                    UPSTREAM_MANAGER.shutdown()
                finally:
                    _remove_owned_process_state()


if __name__ == "__main__":
    try:
        migrate_legacy_backend_token()
    except SecureStoreError as exc:
        log(f"SECURITY ERROR: {exc}")
    try:
        ensure_bridge_hmac_key()
        retire_legacy_ue_backend_token()
    except SecureStoreError as exc:
        log(f"SECURITY ERROR: protected editor capability unavailable: {exc}")
    if _FULL_ACCESS_TOKEN_WARNING:
        log(f"WARNING: {_FULL_ACCESS_TOKEN_WARNING}")
    runtime_config = load_runtime_config()
    if _runtime_config_source() is None:
        log(f"WARNING: revvy-proxy.json not found at {_PROXY_CONFIG_PATH}")
        log("Launch Unreal Engine once or run `revvy install` to create it.")
    elif not runtime_config["_bridge_hmac_key"]:
        log("WARNING: protected bridge authentication key is missing; editor forwarding is disabled.")
    else:
        log("Protected v2 bridge authentication key loaded.")

    if not MANIFEST_PATH.exists():
        log(f"Note: manifest not found at {MANIFEST_PATH}")
        log("Launch Unreal Engine with Revvy once to generate it.")
    else:
        tools = load_manifest()
        log(f"Loaded {len(tools)} tools from manifest")

    try:
        server = QuietThreadingHTTPServer(("127.0.0.1", PROXY_PORT), ProxyHandler)
    except OSError as exc:
        log(
            "ERROR: Revvy MCP Proxy could not bind "
            f"127.0.0.1:{PROXY_PORT}: {exc}"
        )
        raise
    log(f"Revvy MCP Proxy listening on http://127.0.0.1:{PROXY_PORT}/mcp")
    log(f"Forwarding tool calls to UE at http://127.0.0.1:{runtime_config['ue_port']}/mcp")
    STATE_DIR.mkdir(parents=True, exist_ok=True)
    stale_process = stop_stale_managed_proxy()
    process_state = {
        "pid": os.getpid(),
        "proxy_port": PROXY_PORT,
        "script": str(pathlib.Path(__file__).resolve()),
        "started_at": time.time(),
    }
    if stale_process:
        process_state["stale_process"] = stale_process
    PROCESS_PATH.write_text(
        json.dumps(process_state, indent=2) + "\n",
        encoding="utf-8",
    )
    _serve_until_stopped(server)
