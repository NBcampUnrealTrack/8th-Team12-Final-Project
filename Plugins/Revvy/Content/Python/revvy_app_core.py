"""Pure helpers and local-CLI adapters for the Revvy desktop application."""

from __future__ import annotations

import http.client
import json
import hashlib
import os
import pathlib
import random
import re
import secrets
import shutil
import socket
import stat
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request
import urllib.parse
import zipfile

from dataclasses import asdict, dataclass, field
from typing import Any, Callable, Iterable, Mapping, Sequence

from revvy_secure_store import (
    BRIDGE_AUTH_FILE_ENV,
    SECURITY_DIR_ENV,
    UE_CAPABILITY_FILE_ENV,
    read_secure_json,
    security_dir,
    write_secure_json,
)
from revvy_processes import terminate_process_tree

try:
    import tomllib
except ImportError:  # pragma: no cover - Python 3.10 fallback
    tomllib = None


APP_VERSION = "1.5.4"
CHAT_PROMPT_VERSION = ""
PROVIDER_CODEX = "codex"
PROVIDER_CLAUDE = "claude"
PROVIDER_KIMI = "kimi"
PROVIDERS = (PROVIDER_CODEX, PROVIDER_CLAUDE, PROVIDER_KIMI)

PERMISSION_SAFE = "safe"
PERMISSION_FULL = "full"
PERMISSION_MODES = (PERMISSION_SAFE, PERMISSION_FULL)
FULL_ACCESS_TOKEN_ENV = "REVVY_FULL_ACCESS_TOKEN"

ENGINE_UNREAL = "unreal"
ENGINE_UNITY = "unity"
ENGINE_GODOT = "godot"
ENGINES = (ENGINE_UNREAL, ENGINE_UNITY, ENGINE_GODOT)
GODOT_EXECUTABLE_ENV_NAMES = (
    "REVVY_GODOT_EXECUTABLE",
    "GODOT_EXECUTABLE",
    "GODOT4",
)

PROVIDER_LABELS = {
    PROVIDER_CODEX: "Codex (ChatGPT login)",
    PROVIDER_CLAUDE: "Claude Code (local login)",
    PROVIDER_KIMI: "Kimi Code (local login)",
}

CLAUDE_MODELS = ("default", "fable", "opus", "sonnet")
KIMI_MODELS = ("default",)
KIMI_PROMPT_MAX_CHARS = 28_000
KIMI_SUBAGENT_TIMEOUT_ENV = "KIMI_SUBAGENT_TIMEOUT_MS"
KIMI_SUBAGENT_TIMEOUT_MS = 7_200_000
KIMI_PROMPT_TRUNCATION_NOTICE = (
    "[Older conversation content omitted to fit Kimi's Windows command-line limit.]\n\n"
)

VOICE_CONFIG_FILENAME = "revvy-voice.json"
_VOICE_FALLBACK = {
    "version": "revvy-voice-1",
    "persona": (
        "당신은 언리얼 에디터 안에 사는 토끼 로봇 마스코트 레비입니다. "
        "밝고 다정한 해요체로 말해요. 채팅 UI에 이미 레비 아바타 아이콘이 표시되므로 "
        "본문에 🐰 같은 장식용 이모지는 넣지 마세요(꼭 필요한 기능적 표시는 예외). "
        "귀여운 말투여도 기술 내용은 정확하고 구체적으로 설명해요. "
        "【언어 규칙 — 항상 적용, 최우선】 어떤 언어로 묻든, 시스템 프롬프트나 도구 설명이 영어여도, "
        "응답은 무조건 한국어 해요체로만 작성해요. 영어로 답하지 마세요. "
        "코드·식별자·로그·파일 경로 같은 원문은 그대로 두되, 설명과 대화 문장은 전부 한국어로 써요. "
        "응답은 마크다운으로 간결하게 작성해요."
    ),
    "tool_statuses": {},
    "unknown_tool_status": {
        "icon": "🐾",
        "template": "{tool} 도구를 살짝 써볼게요",
    },
    "thinking_statuses": [
        "깡총깡총 생각 중",
        "당근 씹으며 고민 중",
        "귀를 쫑긋 세우는 중",
        "아이디어 굴리는 중",
        "에디터 둘러보는 중",
        "코드 냄새 맡는 중",
        "뚝딱뚝딱 작업 중",
        "반짝반짝 고민 중",
        "점프 준비 중",
        "마법 부리는 중",
    ],
    "turn_statuses": {
        "working": "레비가 열심히 답변을 만들고 있어요...",
        "completed": "─ ✅ 레비 응답 끝 (도구 {count}단계 사용).",
        "paused": (
            "─ ⏸ 턴 한도({count}단계)에 도달해 멈췄어요. 작업이 안 끝났을 수 있어요. "
            '"계속"이라고 보내면 이어서 할게요.'
        ),
    },
}


def revvy_voice_path_candidates() -> list[pathlib.Path]:
    candidates: list[pathlib.Path] = []
    search_roots = [
        pathlib.Path(sys.executable).resolve().parent,
        pathlib.Path(__file__).resolve().parent,
    ]
    for root in search_roots:
        for candidate_root in (root, *root.parents):
            if (candidate_root / "Revvy.uplugin").is_file():
                candidates.append(
                    candidate_root / "Content" / "instructions" / VOICE_CONFIG_FILENAME
                )
                break
    if getattr(sys, "frozen", False) and hasattr(sys, "_MEIPASS"):
        candidates.append(
            pathlib.Path(sys._MEIPASS)
            / "Content"
            / "instructions"
            / VOICE_CONFIG_FILENAME
        )
    candidates.append(
        pathlib.Path(__file__).resolve().parent.parent
        / "instructions"
        / VOICE_CONFIG_FILENAME
    )
    return list(dict.fromkeys(candidates))


def load_revvy_voice_config() -> dict[str, Any]:
    for candidate in revvy_voice_path_candidates():
        try:
            payload = json.loads(candidate.read_text(encoding="utf-8-sig"))
        except (OSError, UnicodeError, json.JSONDecodeError):
            continue
        if (
            isinstance(payload, dict)
            and isinstance(payload.get("persona"), str)
            and isinstance(payload.get("tool_statuses"), dict)
            and isinstance(payload.get("unknown_tool_status"), dict)
            and isinstance(payload.get("thinking_statuses"), list)
            and isinstance(payload.get("turn_statuses"), dict)
        ):
            return payload
    return dict(_VOICE_FALLBACK)


REVVY_VOICE = load_revvy_voice_config()
REVVY_PERSONA = str(REVVY_VOICE["persona"]).strip()
_VOICE_FINGERPRINT = hashlib.sha256(
    json.dumps(
        REVVY_VOICE,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
).hexdigest()[:12]
EXTERNAL_OPERATIONAL_INSTRUCTION = """에디터를 조회하거나 수정할 때는 Revvy MCP 도구를 적극적으로 사용하고,
도구 호출을 답변 본문에 XML이나 코드 형태의 일반 텍스트로 적지 마세요. 현재 작업 디렉토리는
언리얼 프로젝트 루트이므로 소스 코드와 설정 파일을 직접 읽고 검색할 수 있어요.

Always query current Unreal state before changing it. Never guess actor names, asset paths,
or property values. Load a relevant Revvy skill for specialized work, perform one logical
operation at a time, and verify the result. Do not retry a mutating tool call after an
ambiguous timeout. If Unreal is offline, state that clearly instead of fabricating a result.
Automatically load `gpt-sol-image-generation` and `gpt-sol-image-generation/qa-checklist`
before creating or editing an image, concept art, UI art, icon, or generated Unreal texture;
use `screenshots` for capture-only requests.
Automatically load `blender-3d-modeling` before mesh, topology, modifier, material, UV, or
model import/export work, and load `blender-animation-roundtrip` before armature, action,
keyframe, timing, or animation FBX changes. Before either Blender skill, call
`manage_upstreams(action="activate", namespace="blender")`, then refresh the tool catalogue.
Treat the typed tools advertised after activation as capability gates; never replace a missing
native image or Blender MCP capability with arbitrary Python, shell, console, or UI-macro execution.
For Git inspection or synchronization, use Revvy's git_status and git_sync_ff_only MCP tools;
do not run Git through a shell. The sync tool must stop on local changes or divergence.
When the user explicitly requests publishing Revvy itself, use git_publish_paths with only the
verified files from the current task; never include unrelated changes, and explicitly acknowledge
any existing ahead commits before publishing them.

For every action request, keep the user visibly informed:
- Before each major tool or command step, send one concrete Korean status sentence prefixed
  with "[진행] ". Name what you are checking or changing and why.
- After a meaningful step, you may send one short result prefixed with "[완료] ".
- Never send only a promise and stop. Continue with the corresponding tool call in the same turn.
- Progress counters and completed tool calls are not proof that the user's full request is complete.
  Compare the result against every requested deliverable and state exactly what remains.
- Never delegate the entire user request to one subagent. Give each subagent one concrete,
  bounded deliverable, and emit a "[진행] " update before and after a long delegation.
- Do not keep editors, servers, or log viewers as blocking Bash or Agent tasks. Launch persistent
  processes detached on Windows, or use Revvy MCP, then verify them in a separate bounded step.
- If the full scope cannot be finished safely in this turn, finish one coherent verified batch,
  list the exact remaining work, and end truthfully. Never claim total completion prematurely.
- The final answer must not use those prefixes. Report the outcome and verification evidence
  in one to three concise sentences."""

_OPERATIONAL_FINGERPRINT = hashlib.sha256(
    EXTERNAL_OPERATIONAL_INSTRUCTION.encode("utf-8")
).hexdigest()[:12]
CHAT_PROMPT_VERSION = (
    f"{str(REVVY_VOICE.get('version') or 'revvy-voice')}-"
    f"{_VOICE_FINGERPRINT}-{_OPERATIONAL_FINGERPRINT}"
)
SYSTEM_INSTRUCTION = f"{REVVY_PERSONA}\n\n{EXTERNAL_OPERATIONAL_INSTRUCTION}"

CHAT_HISTORY_VERSION = 1
MAX_CHAT_HISTORY_MESSAGES = 240
MAX_CHAT_HISTORY_CHARS = 600_000
PROJECT_STATE_DIRNAME = ".revvy"
PROJECT_DIR_ENV = "REVVY_PROJECT_DIR"
STATE_DIR_ENV = "REVVY_STATE_DIR"
PROJECT_STATE_BOUND_ENV = "REVVY_PROJECT_STATE_BOUND"


def legacy_state_dir(env: dict[str, str] | None = None) -> pathlib.Path:
    """Return the pre-1.5 user-global state directory.

    This path is retained only for one-way migration and machine-wide caches.
    Runtime project/session state belongs under ``<project>/.revvy``.
    """
    values = os.environ if env is None else env
    base = values.get("APPDATA") or values.get("XDG_STATE_HOME")
    if base:
        return pathlib.Path(base).expanduser() / "Revvy"
    return pathlib.Path.home() / ".local" / "state" / "Revvy"


def project_state_dir(project_dir: str | os.PathLike[str]) -> pathlib.Path:
    return pathlib.Path(project_dir).expanduser().resolve() / PROJECT_STATE_DIRNAME


def _ensure_project_state_ignored(
    project_dir: str | os.PathLike[str], state_root: pathlib.Path
) -> None:
    """Keep transient project-local state out of Git without editing root ignores."""
    expected = project_state_dir(project_dir)
    try:
        if not same_project_path(state_root, expected):
            return
        state_root.mkdir(parents=True, exist_ok=True)
        ignore_path = state_root / ".gitignore"
        if ignore_path.exists():
            return
        try:
            with ignore_path.open("x", encoding="utf-8", newline="\n") as handle:
                handle.write("*\n")
        except FileExistsError:
            pass
    except OSError:
        # State writes report their own useful error; ignore setup is best-effort.
        pass


def bind_project_state(
    project_dir: str | os.PathLike[str],
    env: dict[str, str] | None = None,
    *,
    preserve_explicit_overrides: bool = True,
) -> pathlib.Path:
    """Bind this process and its children to one project's ``.revvy`` state.

    Explicit deployment/test overrides remain authoritative on first bind. A
    later bind is an in-app project switch and rewrites the paths atomically as
    one environment context.
    """
    values = os.environ if env is None else env
    project = pathlib.Path(project_dir).expanduser().resolve()
    previous_project = str(values.get(PROJECT_DIR_ENV) or "").strip()
    explicit_state = str(values.get(STATE_DIR_ENV) or "").strip()
    already_bound = str(values.get(PROJECT_STATE_BOUND_ENV) or "").strip() == "1"
    same_binding = bool(previous_project) and same_project_path(previous_project, project)
    preserve_existing = preserve_explicit_overrides and (
        not already_bound or same_binding
    )
    if preserve_existing and explicit_state:
        root = pathlib.Path(explicit_state).expanduser().resolve()
    else:
        root = project_state_dir(project)

    values[PROJECT_DIR_ENV] = str(project)
    values[STATE_DIR_ENV] = str(root)
    managed_paths = {
        "REVVY_CONFIG": root / "revvy-proxy.json",
        "REVVY_MCP_UPSTREAMS_CONFIG": root / "mcp-upstreams.json",
        "REVVY_PROXY_LOG": root / "proxy.log",
    }
    for name, path in managed_paths.items():
        if not (preserve_existing and str(values.get(name) or "").strip()):
            values[name] = str(path)
    values[PROJECT_STATE_BOUND_ENV] = "1"
    _ensure_project_state_ignored(project, root)
    return root


@dataclass(frozen=True)
class Workspace:
    """One project's runtime context, carried instead of installed globally.

    ``bind_project_state`` writes into ``os.environ`` by default, which binds the
    whole process to a single project. That is correct for the CLI and for the
    packaged app's own host project, but it means opening a second project
    silently displaces the first one's config, upstreams and log paths - there is
    only one environment to hold them.

    A Workspace keeps that same mapping in its own dict. Several can exist at
    once, each resolving its own paths and handing its own environment to the
    processes it spawns, so two projects can be worked on side by side without
    either noticing the other.
    """

    project_dir: pathlib.Path
    state_root: pathlib.Path
    env: dict[str, str]

    @classmethod
    def open(
        cls,
        project_dir: str | os.PathLike[str],
        base_env: dict[str, str] | None = None,
        *,
        inherit_overrides: bool = False,
    ) -> "Workspace":
        """Build a workspace for ``project_dir``.

        ``inherit_overrides`` lets this workspace keep an ambient REVVY_CONFIG /
        REVVY_STATE_DIR that the process was launched with. Only the host project
        may do that: if every workspace honoured the same ambient override they
        would all resolve to one state directory, which is the collision this
        class exists to prevent. It defaults to off for that reason.
        """
        values = dict(os.environ if base_env is None else base_env)
        if not inherit_overrides:
            for name in (
                STATE_DIR_ENV,
                "REVVY_CONFIG",
                "REVVY_MCP_UPSTREAMS_CONFIG",
                "REVVY_PROXY_LOG",
            ):
                values.pop(name, None)
        root = bind_project_state(
            project_dir, values, preserve_explicit_overrides=inherit_overrides
        )
        return cls(
            project_dir=pathlib.Path(project_dir).expanduser().resolve(),
            state_root=root,
            env=values,
        )

    @property
    def key(self) -> str:
        """Identity for dictionaries. Windows paths differ only by case."""
        return os.path.normcase(str(self.project_dir))

    @property
    def name(self) -> str:
        return self.project_dir.name or str(self.project_dir)

    def holds(self, project_dir: str | os.PathLike[str]) -> bool:
        return same_project_path(self.project_dir, project_dir)

    def settings_path(self) -> pathlib.Path:
        return settings_path(self.env)

    def attachments_dir(self) -> pathlib.Path:
        return attachments_dir(self.env)

    def chat_history_path(self) -> pathlib.Path:
        return chat_history_path(self.env)

    def config_path(self) -> pathlib.Path:
        return pathlib.Path(
            self.env.get("REVVY_CONFIG", str(self.state_root / "revvy-proxy.json"))
        )

    def child_env(self, extra: dict[str, str] | None = None) -> dict[str, str]:
        """Environment for a process this workspace owns.

        Always a copy: a child's own additions must not leak back and change
        where this workspace resolves its paths.
        """
        values = dict(self.env)
        # Editor/Blender children need the private bridge-key path, but never
        # the client-facing Full bearer. Only the proxy and an explicitly Full
        # AI provider receive that separate capability.
        values.pop(FULL_ACCESS_TOKEN_ENV, None)
        if extra:
            values.update(extra)
        return values

    def install_globally(self) -> None:
        """Push this workspace onto ``os.environ``.

        The transitional bridge for code that still reads process globals -
        ``revvy.configure_runtime_paths`` and anything it initialised at import.
        Only one workspace can be installed at a time, which is precisely the
        limitation being retired, so new code should take ``env`` instead.
        """
        os.environ.update(self.env)


@dataclass(frozen=True)
class ProxyTarget:
    """Immutable identity for one project-local proxy endpoint."""

    mcp_url: str
    expected_config_path: pathlib.Path
    token: str = field(repr=False)


def proxy_target(
    mcp_url: str,
    expected_config_path: str | os.PathLike[str],
    token: str,
) -> ProxyTarget:
    parsed = urllib.parse.urlsplit(str(mcp_url))
    if parsed.scheme != "http" or parsed.hostname not in {"127.0.0.1", "localhost"}:
        raise ValueError("Revvy proxy target must use loopback HTTP")
    if parsed.username or parsed.password or parsed.query or parsed.fragment:
        raise ValueError("Revvy proxy target URL contains unsupported components")
    if not parsed.port or parsed.path.rstrip("/") != "/mcp":
        raise ValueError("Revvy proxy target must point to /mcp on an explicit port")
    if len(str(token)) < 32:
        raise ValueError("Revvy Full access token is missing or invalid")
    return ProxyTarget(
        mcp_url=str(mcp_url),
        expected_config_path=pathlib.Path(expected_config_path).expanduser().resolve(),
        token=str(token),
    )


def _proxy_target_health_url(target: ProxyTarget) -> str:
    parsed = urllib.parse.urlsplit(target.mcp_url)
    return urllib.parse.urlunsplit(
        (parsed.scheme, parsed.netloc, "/health", "", "")
    )


def rpc_request_at_proxy(
    target: ProxyTarget,
    method: str,
    params: Any = None,
    *,
    timeout: float = 10.0,
) -> dict[str, Any]:
    """Call only the captured project proxy after revalidating its identity."""

    health_headers = {
        "Accept": "application/json",
        "Connection": "close",
    }
    health_request = urllib.request.Request(
        _proxy_target_health_url(target),
        headers=health_headers,
    )
    with urllib.request.urlopen(health_request, timeout=min(timeout, 2.0)) as response:
        health = json.loads(response.read().decode("utf-8"))
    expected_fingerprint = hashlib.sha256(target.token.encode("utf-8")).hexdigest()
    if (
        not _proxy_supports_current_app(health, expected_fingerprint)
        or not same_project_path(
            health.get("config_path"),
            target.expected_config_path,
        )
    ):
        raise ConnectionError("Revvy proxy identity changed for this project")

    request_id = int(time.time() * 1000)
    payload: dict[str, Any] = {
        "jsonrpc": "2.0",
        "id": request_id,
        "method": method,
    }
    if params is not None:
        payload["params"] = params
    body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    rpc_headers = {
        **health_headers,
        "Authorization": f"Bearer {target.token}",
        "Content-Type": "application/json",
        "MCP-Protocol-Version": "2025-06-18",
    }
    request = urllib.request.Request(
        target.mcp_url,
        data=body,
        headers=rpc_headers,
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        decoded = json.loads(response.read().decode("utf-8"))
    if not isinstance(decoded, dict):
        raise ValueError("Expected a JSON object response")
    return decoded


def state_dir(env: dict[str, str] | None = None) -> pathlib.Path:
    values = os.environ if env is None else env
    configured = values.get(STATE_DIR_ENV)
    if configured:
        return pathlib.Path(configured).expanduser().resolve()
    configured_project = str(values.get(PROJECT_DIR_ENV) or "").strip()
    if configured_project:
        return project_state_dir(configured_project)
    # Calls supplied an explicit environment for a test/toolchain contract:
    # preserve the historical APPDATA/XDG fallback unless a project was named.
    if env is not None:
        return legacy_state_dir(values)
    host_project = detect_host_project_dir()
    if host_project:
        return project_state_dir(host_project)
    return legacy_state_dir(values)


def settings_path(env: dict[str, str] | None = None) -> pathlib.Path:
    return state_dir(env) / "revvy-app.json"


def attachments_dir(env: dict[str, str] | None = None) -> pathlib.Path:
    return state_dir(env) / "attachments"


def global_cache_dir(env: dict[str, str] | None = None) -> pathlib.Path:
    """Machine cache for reusable installers; never part of project sessions."""
    values = os.environ if env is None else env
    configured = str(values.get("REVVY_CACHE_DIR") or "").strip()
    if configured:
        return pathlib.Path(configured).expanduser().resolve()
    local = str(values.get("LOCALAPPDATA") or "").strip()
    if local:
        return pathlib.Path(local).expanduser() / "Revvy" / "Cache"
    return legacy_state_dir(values) / "Cache"


BLENDER_BRIDGE_CONFIG_FILENAME = "blender-bridge.json"
MCP_UPSTREAMS_CONFIG_FILENAME = "mcp-upstreams.json"
PROXY_CONFIG_FILENAME = "revvy-proxy.json"
BLENDER_BRIDGE_MODULE = "revvy_blender_bridge"
BLENDER_MCP_SERVER_NAME = "blender"
BLENDER_MCP_TOOLS = (
    "blender_bridge_status",
    "blender_job_status",
    "blender_scene_inspect",
    "blender_model_import",
    "blender_mesh_inspect",
    "blender_mesh_edit",
    "blender_modifier_apply",
    "blender_material_assign",
    "blender_uv_inspect",
    "blender_uv_edit",
    "blender_model_export",
    "blender_animation_import_fbx",
    "blender_animation_inspect",
    "blender_animation_adjust",
    "blender_animation_export_fbx",
    "blender_save_copy",
)
BLENDER_MCP_MUTATING_TOOLS = (
    "blender_model_import",
    "blender_mesh_edit",
    "blender_modifier_apply",
    "blender_material_assign",
    "blender_uv_edit",
    "blender_model_export",
    "blender_animation_import_fbx",
    "blender_animation_adjust",
    "blender_animation_export_fbx",
    "blender_save_copy",
)
BLENDER_MCP_READ_ONLY_TOOLS = tuple(
    name for name in BLENDER_MCP_TOOLS if name not in BLENDER_MCP_MUTATING_TOOLS
)
_BLENDER_INTENT_RE = re.compile(
    r"(?i)(?:(?<![A-Za-z0-9_])blender(?![A-Za-z0-9_])|\.blend(?:\b|$)|블렌더)"
)
_BLENDER_NEGATION_RE = re.compile(
    r"(?i)(?:(?:without|do\s+not|don't|avoid)\s+(?:(?:use|using|launch|open|start|activate)\s+)?"
    r"(?:blender|[^\n]{0,40}\.blend(?:\b|$))|"
    r"(?:blender|블렌더)\s*(?:없이|말고|제외|사용하지))"
)
_BLENDER_TASK_RE = re.compile(
    r"(?is)(?:"
    r"\bfbx\b.{0,80}(?:anim(?:ation)?|keyframes?|rig|armature|애니메이션|키|리그)|"
    r"(?:anim(?:ation)?|keyframes?|rig|armature|애니메이션|키|리그).{0,80}\bfbx\b|"
    r"(?:mesh|메시).{0,80}(?:\buv\b|unwrap|언랩).{0,80}"
    r"(?:\bglb\b|\bgltf\b|export|내보내)|"
    r"(?=.{0,120}(?:mesh|메시))(?=.{0,120}(?:\buv\b|unwrap|언랩))"
    r"(?=.{0,120}(?:\bglb\b|\bgltf\b|export|내보내))|"
    r"(?:\bglb\b|\bgltf\b).{0,80}(?:export|내보내)"
    r")"
)
_NON_BLENDER_ENGINE_RE = re.compile(
    r"(?i)\b(?:unreal(?:\s*engine)?\s*\d*|ue\s*\d*|unity\s*\d*(?:3d)?|godot\s*\d*)\b|"
    r"언리얼|유니티|고도"
)
_CONTINUATION_RE = re.compile(r"(?i)\b(?:continue|resume)\b|계속|이어서|마저")


def upstream_namespaces_for_prompt(
    user_message: str,
    history: Sequence[tuple[str, str]] = (),
) -> tuple[str, ...]:
    """Select bridges from explicit intent, scoped workflows, or a continued turn."""
    message = str(user_message or "")
    if _BLENDER_NEGATION_RE.search(message):
        return ()
    if _BLENDER_INTENT_RE.search(message):
        return (BLENDER_MCP_SERVER_NAME,)
    if not _NON_BLENDER_ENGINE_RE.search(message) and _BLENDER_TASK_RE.search(message):
        return (BLENDER_MCP_SERVER_NAME,)
    if _CONTINUATION_RE.search(message):
        prior = "\n".join(
            str(item[1])
            for item in list(history)[-8:]
            if isinstance(item, (tuple, list)) and len(item) >= 2
        )
        if (
            not _BLENDER_NEGATION_RE.search(prior)
            and (
                _BLENDER_INTENT_RE.search(prior)
                or _BLENDER_TASK_RE.search(prior)
            )
        ):
            return (BLENDER_MCP_SERVER_NAME,)
    return ()


def blender_bridge_config_path(
    env: dict[str, str] | None = None,
) -> pathlib.Path:
    values = os.environ if env is None else env
    configured = str(values.get("REVVY_BLENDER_BRIDGE_CONFIG") or "").strip()
    if configured:
        return pathlib.Path(configured).expanduser().resolve()
    return security_dir(values) / BLENDER_BRIDGE_CONFIG_FILENAME


def legacy_blender_bridge_config_path(
    env: dict[str, str] | None = None,
) -> pathlib.Path:
    return state_dir(env) / BLENDER_BRIDGE_CONFIG_FILENAME


def mcp_upstreams_config_path(
    env: dict[str, str] | None = None,
) -> pathlib.Path:
    values = os.environ if env is None else env
    configured = str(values.get("REVVY_MCP_UPSTREAMS_CONFIG") or "").strip()
    if configured:
        return pathlib.Path(configured).expanduser().resolve()
    return state_dir(env) / MCP_UPSTREAMS_CONFIG_FILENAME


def proxy_config_path(env: dict[str, str] | None = None) -> pathlib.Path:
    """Path of the proxy runtime config the editor writes (revvy-proxy.json)."""
    values = os.environ if env is None else env
    configured = str(values.get("REVVY_CONFIG") or "").strip()
    if configured:
        return pathlib.Path(configured).expanduser().resolve()
    return state_dir(values) / PROXY_CONFIG_FILENAME


def _atomic_json_write(path: pathlib.Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(
        f".{path.name}.{os.getpid()}.{secrets.token_hex(8)}.tmp"
    )
    try:
        descriptor = os.open(
            temporary,
            os.O_WRONLY | os.O_CREAT | os.O_EXCL,
            0o600,
        )
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as handle:
            json.dump(payload, handle, ensure_ascii=False, indent=2)
            handle.write("\n")
        try:
            temporary.chmod(stat.S_IRUSR | stat.S_IWUSR)
        except OSError:
            pass
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


def _detect_engine_marker(
    project_dir: str | os.PathLike[str] | None,
) -> str | None:
    """Return the engine identified by project markers, without a fallback."""
    if not project_dir:
        return None
    try:
        root = pathlib.Path(project_dir)
        if any(root.glob("*.uproject")):
            return ENGINE_UNREAL
        if (root / "ProjectSettings").is_dir() and (root / "Assets").is_dir():
            return ENGINE_UNITY
        if (root / "project.godot").is_file():
            return ENGINE_GODOT
    except (OSError, ValueError):
        pass
    return None


def same_project_path(
    left: str | os.PathLike[str] | None,
    right: str | os.PathLike[str] | None,
) -> bool:
    """Compare two project paths using platform path and case semantics."""
    if not left or not right:
        return False
    try:
        return os.path.normcase(os.path.abspath(os.fspath(left))) == os.path.normcase(
            os.path.abspath(os.fspath(right))
        )
    except (OSError, TypeError, ValueError):
        return False


def detect_host_project_dir(anchor: str | os.PathLike[str] | None = None) -> str:
    """Return the nearest Unreal, Unity, or Godot project containing the app."""
    if anchor is None:
        if getattr(sys, "frozen", False):
            anchor = sys.executable
        else:
            anchor = __file__
    try:
        location = pathlib.Path(anchor).resolve()
    except OSError:
        return ""
    start = location if location.is_dir() else location.parent
    for parent in (start, *start.parents):
        if _detect_engine_marker(parent) is not None:
            return str(parent)
    return ""


def detect_engine(project_dir: str | os.PathLike[str] | None) -> str:
    """Best-effort engine guess from project path markers (default: Unreal)."""
    return _detect_engine_marker(project_dir) or ENGINE_UNREAL


PLUGIN_DIRNAME = "Revvy"
PLUGIN_DESCRIPTOR = "Revvy.uplugin"
# Rebuilt by the engine from the source that ships beside them. Carrying an old
# plugin's compiled output forward is worse than having none: Unreal will load
# the stale DLL and report the new version number while running the old code.
PLUGIN_DERIVED_DIRS = ("Binaries", "Intermediate")


def bundled_plugin_root(app_executable: str | os.PathLike[str] | None = None) -> pathlib.Path | None:
    """The plugin payload the installer shipped beside the app, if any.

    A source checkout has no such payload - it *is* the plugin - so this returns
    None there and provisioning falls back to the repository root.
    """
    if app_executable is not None:
        base = pathlib.Path(app_executable)
    elif getattr(sys, "frozen", False):
        base = pathlib.Path(sys.executable)
    else:
        return None
    candidate = (base if base.is_dir() else base.parent) / "Plugin"
    if (candidate / PLUGIN_DESCRIPTOR).is_file():
        return candidate
    return None


def project_plugin_dir(project_dir: str | os.PathLike[str]) -> pathlib.Path:
    return pathlib.Path(project_dir).expanduser().resolve() / "Plugins" / PLUGIN_DIRNAME


def plugin_descriptor_version(plugin_dir: str | os.PathLike[str]) -> str:
    """VersionName from a plugin directory's descriptor, or "" if unreadable."""
    path = pathlib.Path(plugin_dir) / PLUGIN_DESCRIPTOR
    try:
        payload = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError):
        return ""
    return str(payload.get("VersionName") or "").strip()


def provision_unreal_plugin(
    project_dir: str | os.PathLike[str],
    source: str | os.PathLike[str] | None = None,
    *,
    force: bool = False,
) -> tuple[str, str]:
    """Put the Revvy plugin into an Unreal project. Returns (status, detail).

    Statuses: ``installed``, ``updated``, ``current``, ``newer``, ``unavailable``,
    ``not-unreal``. Never silently downgrades: a project holding a newer plugin
    than the app is reported and left alone, because the usual cause is a
    developer working on the plugin in that very project.
    """
    project = pathlib.Path(project_dir).expanduser().resolve()
    if _detect_engine_marker(project) != ENGINE_UNREAL:
        return "not-unreal", "Unreal 프로젝트가 아니에요"
    root = pathlib.Path(source) if source is not None else bundled_plugin_root()
    if root is None or not (root / PLUGIN_DESCRIPTOR).is_file():
        return "unavailable", "설치된 플러그인 원본을 찾지 못했어요"
    target = project_plugin_dir(project)
    shipped = plugin_descriptor_version(root)
    existing = plugin_descriptor_version(target) if target.is_dir() else ""
    if existing and not force:
        if existing == shipped:
            return "current", f"이미 {existing} 이 설치돼 있어요"
        if _version_tuple(existing) > _version_tuple(shipped):
            return "newer", f"프로젝트 쪽이 더 최신이에요 ({existing} > {shipped})"
    if existing:
        # Drop the previous build's derived output before the new source lands.
        for name in PLUGIN_DERIVED_DIRS:
            shutil.rmtree(target / name, ignore_errors=True)
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(root, target, dirs_exist_ok=True)
    status = "updated" if existing else "installed"
    return status, f"Revvy {shipped} 플러그인을 {'갱신' if existing else '설치'}했어요"


GODOT_ADDON_DIRNAME = "revvy"
GODOT_ADDON_CONFIG = "plugin.cfg"
UNITY_PACKAGE_NAME = "com.revstudio.revvy-unity"


def bundled_bridge_root(
    engine: str, app_executable: str | os.PathLike[str] | None = None
) -> pathlib.Path | None:
    """The Godot addon or Unity package shipped beside the installed app.

    The installer lays these out under ``{app}\\Bridges`` mirroring the
    repository, so a source checkout and an install resolve the same way.
    """
    if engine == ENGINE_UNREAL:
        return bundled_plugin_root(app_executable)
    if app_executable is not None:
        base = pathlib.Path(app_executable)
    elif getattr(sys, "frozen", False):
        base = pathlib.Path(sys.executable)
    else:
        base = pathlib.Path(__file__).resolve().parents[2]
        candidate = base / "Bridges"
        return _bridge_payload(candidate, engine)
    return _bridge_payload((base if base.is_dir() else base.parent) / "Bridges", engine)


def _bridge_payload(bridges: pathlib.Path, engine: str) -> pathlib.Path | None:
    if engine == ENGINE_GODOT:
        candidate = bridges / "Godot" / "addons" / GODOT_ADDON_DIRNAME
        return candidate if (candidate / GODOT_ADDON_CONFIG).is_file() else None
    if engine == ENGINE_UNITY:
        candidate = bridges / "Unity" / UNITY_PACKAGE_NAME
        return candidate if (candidate / "package.json").is_file() else None
    return None


def bridge_install_target(project_dir: str | os.PathLike[str], engine: str) -> pathlib.Path:
    """Where this engine expects the bridge to live inside a project."""
    project = pathlib.Path(project_dir).expanduser().resolve()
    if engine == ENGINE_UNREAL:
        return project_plugin_dir(project)
    if engine == ENGINE_GODOT:
        return project / "addons" / GODOT_ADDON_DIRNAME
    return project / "Packages" / UNITY_PACKAGE_NAME


def installed_bridge_version(project_dir: str | os.PathLike[str], engine: str) -> str:
    """The bridge version already inside a project, or "" if none is."""
    target = bridge_install_target(project_dir, engine)
    if engine == ENGINE_UNREAL:
        return plugin_descriptor_version(target)
    if engine == ENGINE_GODOT:
        config = target / GODOT_ADDON_CONFIG
        try:
            for line in config.read_text(encoding="utf-8-sig").splitlines():
                key, _, value = line.partition("=")
                if key.strip() == "version":
                    return value.strip().strip('"')
        except OSError:
            return ""
        return ""
    try:
        payload = json.loads((target / "package.json").read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError):
        return ""
    return str(payload.get("version") or "").strip()


def bridge_payload_version(source: str | os.PathLike[str], engine: str) -> str:
    """The version of a payload directory, read the way that engine records it."""
    root = pathlib.Path(source)
    if engine == ENGINE_UNREAL:
        return plugin_descriptor_version(root)
    if engine == ENGINE_GODOT:
        try:
            for line in (root / GODOT_ADDON_CONFIG).read_text(
                encoding="utf-8-sig"
            ).splitlines():
                key, _, value = line.partition("=")
                if key.strip() == "version":
                    return value.strip().strip('"')
        except OSError:
            return ""
        return ""
    try:
        payload = json.loads((root / "package.json").read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError):
        return ""
    return str(payload.get("version") or "").strip()


def describe_bridge_install(
    project_dir: str | os.PathLike[str], engine: str, source: str | os.PathLike[str] | None = None
) -> dict[str, str]:
    """What installing would do, without doing it.

    The app asks before writing into someone's project, and a confirmation that
    cannot say what it is about to change is not consent. Returns the target
    path, both versions, and the action that would be taken.
    """
    root = pathlib.Path(source) if source is not None else bundled_bridge_root(engine)
    target = bridge_install_target(project_dir, engine)
    shipped = bridge_payload_version(root, engine) if root is not None else ""
    existing = installed_bridge_version(project_dir, engine)
    if root is None:
        action = "unavailable"
    elif not existing:
        action = "install"
    elif existing == shipped:
        action = "current"
    elif _version_tuple(existing) > _version_tuple(shipped):
        action = "newer"
    else:
        action = "update"
    return {
        "engine": engine,
        "action": action,
        "target": str(target),
        "shipped": shipped,
        "existing": existing,
        "source": str(root) if root is not None else "",
    }


def provision_engine_bridge(
    project_dir: str | os.PathLike[str],
    engine: str,
    source: str | os.PathLike[str] | None = None,
    *,
    force: bool = False,
) -> tuple[str, str]:
    """Install the editor bridge for whichever engine a project uses.

    Unreal takes a plugin under Plugins/, Godot an addon under addons/, Unity a
    package under Packages/. Same contract as provision_unreal_plugin: never
    downgrades, and reports rather than guessing.
    """
    if engine == ENGINE_UNREAL:
        return provision_unreal_plugin(project_dir, source, force=force)
    project = pathlib.Path(project_dir).expanduser().resolve()
    if _detect_engine_marker(project) != engine:
        return "not-matching", f"{engine} 프로젝트가 아니에요"
    plan = describe_bridge_install(project, engine, source)
    if plan["action"] == "unavailable":
        return "unavailable", "설치할 브리지 원본을 찾지 못했어요"
    if not force and plan["action"] in ("current", "newer"):
        if plan["action"] == "current":
            return "current", f"이미 {plan['existing']} 이 설치돼 있어요"
        return (
            "newer",
            f"프로젝트 쪽이 더 최신이에요 ({plan['existing']} > {plan['shipped']})",
        )
    target = pathlib.Path(plan["target"])
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(plan["source"], target, dirs_exist_ok=True)
    if engine == ENGINE_UNITY:
        _register_unity_package(project, target)
    status = "updated" if plan["existing"] else "installed"
    verb = "갱신" if plan["existing"] else "설치"
    return status, f"Revvy {plan['shipped']} 브리지를 {verb}했어요"


def _register_unity_package(project: pathlib.Path, target: pathlib.Path) -> None:
    """Add the embedded package to Packages/manifest.json.

    Unity discovers embedded packages under Packages/ on its own, but a manifest
    entry is what survives a package cache reset and what the user sees in the
    Package Manager, so both are written.
    """
    manifest_path = project / "Packages" / "manifest.json"
    try:
        payload = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError):
        payload = {}
    if not isinstance(payload, dict):
        return
    dependencies = payload.setdefault("dependencies", {})
    if not isinstance(dependencies, dict):
        return
    if dependencies.get(UNITY_PACKAGE_NAME):
        return
    dependencies[UNITY_PACKAGE_NAME] = f"file:{target.name}"
    try:
        manifest_path.parent.mkdir(parents=True, exist_ok=True)
        manifest_path.write_text(
            json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
        )
    except OSError:
        # The package still resolves from disk; the manifest is a convenience.
        pass


def _version_tuple(value: str) -> tuple[int, ...]:
    parts: list[int] = []
    for chunk in str(value).split("."):
        digits = "".join(ch for ch in chunk if ch.isdigit())
        if not digits:
            break
        parts.append(int(digits))
    return tuple(parts)


def resolve_engine(
    project_dir: str | os.PathLike[str] | None,
    override: str | None,
    proxy_config_engine: str | None = None,
) -> str:
    """Active engine: explicit override > path markers > matching proxy state."""
    normalized_override = str(override or "").strip().lower()
    if normalized_override in ENGINES:
        return normalized_override
    detected = _detect_engine_marker(project_dir)
    if detected is not None:
        return detected
    normalized_proxy = str(proxy_config_engine or "").strip().lower()
    if normalized_proxy in ENGINES:
        return normalized_proxy
    return ENGINE_UNREAL


def write_proxy_engine(engine: str, path: pathlib.Path | None = None) -> bool:
    """Record the app-selected engine in revvy-proxy.json for other consumers.

    Read-modify-write preserving every other field; the write is atomic
    because the proxy reads this file concurrently. Returns False on an
    invalid engine id or any I/O problem — callers treat it as soft failure.
    """
    normalized = str(engine or "").strip().lower()
    if normalized not in ENGINES:
        return False
    target = proxy_config_path() if path is None else path
    try:
        raw = target.read_text(encoding="utf-8-sig")
    except OSError:
        raw = ""
    if raw.strip():
        try:
            loaded = json.loads(raw)
        except json.JSONDecodeError:
            # A partially rewritten config must not be clobbered.
            return False
        if not isinstance(loaded, dict):
            return False
        payload = dict(loaded)
        payload["engine"] = normalized
    else:
        payload = {
            "engine": normalized,
            "updated_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        }
    try:
        _atomic_json_write(target, payload)
    except OSError:
        return False
    return True


def write_proxy_project_context(
    project_dir: str | os.PathLike[str],
    engine: str,
    path: pathlib.Path | None = None,
) -> bool:
    """Record the desktop app's selected project without claiming a live editor."""
    normalized = str(engine or "").strip().lower()
    if normalized not in ENGINES:
        return False
    try:
        project_root = pathlib.Path(project_dir).expanduser().resolve()
    except (OSError, TypeError, ValueError):
        return False
    if not project_root.is_dir():
        return False

    project_name = project_root.name
    if normalized == ENGINE_UNREAL:
        try:
            project_files = list(project_root.glob("*.uproject"))
        except OSError:
            project_files = []
        if len(project_files) == 1:
            project_name = project_files[0].stem

    target = proxy_config_path() if path is None else path
    try:
        raw = target.read_text(encoding="utf-8-sig")
    except OSError:
        raw = ""
    if raw.strip():
        try:
            loaded = json.loads(raw)
        except json.JSONDecodeError:
            return False
        if not isinstance(loaded, dict):
            return False
        payload = dict(loaded)
    else:
        payload = {}

    payload["engine"] = normalized
    payload["installed_project"] = {
        "name": project_name,
        "path": str(project_root),
    }
    payload["updated_at"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    try:
        _atomic_json_write(target, payload)
    except OSError:
        return False
    return True


def blender_bridge_source_candidates() -> list[pathlib.Path]:
    candidates: list[pathlib.Path] = []
    if getattr(sys, "frozen", False) and hasattr(sys, "_MEIPASS"):
        candidates.append(
            pathlib.Path(sys._MEIPASS)
            / "Content"
            / "Blender"
            / BLENDER_BRIDGE_MODULE
        )
    content_root = pathlib.Path(__file__).resolve().parent.parent
    candidates.append(content_root / "Blender" / BLENDER_BRIDGE_MODULE)
    candidates.append(
        pathlib.Path(sys.executable).resolve().parent
        / "Content"
        / "Blender"
        / BLENDER_BRIDGE_MODULE
    )
    return list(dict.fromkeys(candidates))


def blender_bridge_source_dir() -> pathlib.Path:
    for candidate in blender_bridge_source_candidates():
        if (candidate / "__init__.py").is_file():
            return candidate
    searched = ", ".join(str(path) for path in blender_bridge_source_candidates())
    raise FileNotFoundError(f"Revvy Blender add-on source not found. Searched: {searched}")


def revvy_blender_mcp_command() -> tuple[str, list[str]]:
    executable_dir = pathlib.Path(sys.executable).resolve().parent
    packaged = executable_dir / "RevvyBlenderMCP.exe"
    if getattr(sys, "frozen", False):
        if not packaged.is_file():
            raise FileNotFoundError(f"Revvy Blender MCP executable not found: {packaged}")
        return str(packaged), []
    script = pathlib.Path(__file__).resolve().parent / "revvy_blender_mcp.py"
    if not script.is_file():
        raise FileNotFoundError(f"Revvy Blender MCP script not found: {script}")
    return sys.executable, [str(script)]


def discover_blender_executables(
    env: dict[str, str] | None = None,
) -> list[pathlib.Path]:
    values = os.environ if env is None else env
    candidates: list[pathlib.Path] = []
    configured = str(values.get("REVVY_BLENDER_EXE") or "").strip()
    if configured:
        candidates.append(pathlib.Path(configured).expanduser())
    resolved = shutil.which("blender", path=values.get("PATH"))
    if resolved:
        candidates.append(pathlib.Path(resolved))
    if os.name == "nt":
        for key in ("ProgramFiles", "ProgramW6432", "LOCALAPPDATA"):
            base = values.get(key)
            if not base:
                continue
            root = pathlib.Path(base)
            patterns = (
                "Blender Foundation/Blender */blender.exe",
                "Programs/Blender Foundation/Blender */blender.exe",
            )
            for pattern in patterns:
                try:
                    candidates.extend(root.glob(pattern))
                except OSError:
                    continue
    unique: dict[str, pathlib.Path] = {}
    configured_key = ""
    for candidate in candidates:
        try:
            resolved_candidate = candidate.expanduser().resolve()
        except OSError:
            continue
        if resolved_candidate.is_file():
            key = os.path.normcase(str(resolved_candidate))
            unique.setdefault(key, resolved_candidate)
            if configured and candidate == candidates[0]:
                configured_key = key

    def version_key(path: pathlib.Path) -> tuple[int, ...]:
        match = re.search(r"(\d+(?:\.\d+)+)", path.parent.name)
        return tuple(int(part) for part in match.group(1).split(".")) if match else ()

    ordered = sorted(unique.items(), key=lambda item: version_key(item[1]), reverse=True)
    if configured_key and configured_key in unique:
        ordered = [
            (configured_key, unique[configured_key]),
            *[item for item in ordered if item[0] != configured_key],
        ]
    return [path for _key, path in ordered]


def blender_version(
    executable: str | os.PathLike[str],
    *,
    run: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run,
) -> str:
    completed = run(
        [str(pathlib.Path(executable).resolve()), "--version"],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=20,
        check=False,
    )
    output = f"{completed.stdout or ''}\n{completed.stderr or ''}"
    match = re.search(r"\bBlender\s+(\d+\.\d+)(?:\.\d+)?\b", output)
    if completed.returncode != 0 or not match:
        raise RuntimeError(f"Could not determine Blender version: {output.strip()}")
    return match.group(1)


def _available_loopback_port(preferred: int = 9876) -> int:
    for port in range(preferred, preferred + 32):
        probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            probe.bind(("127.0.0.1", port))
            return port
        except OSError:
            continue
        finally:
            probe.close()
    raise RuntimeError("No free local port is available for the Blender bridge")


def prepare_blender_bridge_configuration(
    project_dir: str | os.PathLike[str],
    *,
    env: dict[str, str] | None = None,
    mcp_command: tuple[str, Sequence[str]] | None = None,
    token_factory: Callable[[int], str] = secrets.token_urlsafe,
) -> dict[str, Any]:
    project = pathlib.Path(project_dir).expanduser().resolve()
    if not project.is_dir() or not any(project.glob("*.uproject")):
        raise ValueError(f"Unreal project directory is invalid: {project}")
    exchange_root = project / "Saved" / "Revvy" / "BlenderExchange"
    exchange_root.mkdir(parents=True, exist_ok=True)

    bridge_path = blender_bridge_config_path(env)
    existing: dict[str, Any] = {}
    loaded = read_secure_json(bridge_path)
    if isinstance(loaded, dict):
        existing = loaded
    legacy_bridge_path = legacy_blender_bridge_config_path(env)
    if not existing and legacy_bridge_path != bridge_path:
        try:
            loaded = json.loads(legacy_bridge_path.read_text(encoding="utf-8-sig"))
            if isinstance(loaded, dict):
                existing = loaded
        except (OSError, UnicodeError, json.JSONDecodeError):
            pass
    token = str(existing.get("token") or "").strip()
    if len(token) < 32:
        token = token_factory(32)
    read_only_token = str(existing.get("read_only_token") or "").strip()
    if len(read_only_token) < 32 or secrets.compare_digest(read_only_token, token):
        read_only_token = secrets.token_urlsafe(32)
    try:
        port = int(existing.get("port", 0))
    except (TypeError, ValueError):
        port = 0
    if not 1024 <= port <= 65535:
        port = _available_loopback_port()

    bridge_config = {
        "version": 1,
        "host": "127.0.0.1",
        "port": port,
        "token": token,
        "read_only_token": read_only_token,
        "exchange_root": str(exchange_root),
    }
    write_secure_json(bridge_path, bridge_config)
    if legacy_bridge_path != bridge_path and legacy_bridge_path.exists():
        _atomic_json_write(
            legacy_bridge_path,
            {
                "version": 1,
                "host": "127.0.0.1",
                "port": port,
                "exchange_root": str(exchange_root),
            },
        )

    command, arguments = mcp_command or revvy_blender_mcp_command()
    upstream_path = mcp_upstreams_config_path(env)
    upstream_payload: dict[str, Any] = {"version": 1, "servers": {}}
    try:
        loaded = json.loads(upstream_path.read_text(encoding="utf-8-sig"))
        if isinstance(loaded, dict) and isinstance(loaded.get("servers"), dict):
            upstream_payload = {"version": 1, "servers": dict(loaded["servers"])}
    except (OSError, UnicodeError, json.JSONDecodeError):
        pass
    upstream_payload["version"] = 1
    upstream_payload["servers"][BLENDER_MCP_SERVER_NAME] = {
        "enabled": True,
        "activation": "on_demand",
        "command": str(command),
        "args": [str(value) for value in arguments],
        "cwd": str(project),
        "timeout_seconds": 240,
        "startup_timeout_seconds": 18,
        "allow_tools": list(BLENDER_MCP_TOOLS),
        "deny_tools": [],
        "read_only_tools": list(BLENDER_MCP_READ_ONLY_TOOLS),
        "mutating_tools": list(BLENDER_MCP_MUTATING_TOOLS),
        "allow_mutations": True,
        "inherit_env": [
            "APPDATA",
            "USERPROFILE",
            "LOCALAPPDATA",
            "TEMP",
            "TMP",
            "PATH",
            "SYSTEMROOT",
            "REVVY_STATE_DIR",
            SECURITY_DIR_ENV,
            UE_CAPABILITY_FILE_ENV,
            "REVVY_BLENDER_BRIDGE_CONFIG",
            "REVVY_MCP_UPSTREAMS_CONFIG",
        ],
    }
    _atomic_json_write(upstream_path, upstream_payload)
    return {
        "bridge_config": bridge_path,
        "upstream_config": upstream_path,
        "exchange_root": exchange_root,
        "port": port,
    }


def repair_blender_upstream_command(
    project_dir: str | os.PathLike[str] | None = None,
    *,
    env: dict[str, str] | None = None,
    mcp_command: tuple[str, Sequence[str]] | None = None,
) -> bool:
    """Rewrite the Blender upstream entry when its stored paths no longer exist.

    A project-local upstream config can still become stale after the project or
    packaged app moves; the proxy then fails with WinError 2. Returns True when
    the entry was stale and has been repaired.
    """
    upstream_path = mcp_upstreams_config_path(env)
    try:
        loaded = json.loads(upstream_path.read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeError, json.JSONDecodeError):
        return False
    if not isinstance(loaded, dict) or not isinstance(loaded.get("servers"), dict):
        return False
    servers = dict(loaded["servers"])
    server = servers.get(BLENDER_MCP_SERVER_NAME)
    if not isinstance(server, dict):
        return False

    stored_command = str(server.get("command") or "").strip()
    stored_args = [str(value) for value in server.get("args") or []]
    stored_cwd = str(server.get("cwd") or "").strip()
    # A bare command name (e.g. "uvx", "python") is valid when it resolves on
    # PATH — is_file() alone would misjudge a working user config as stale.
    command_stale = not stored_command or not (
        pathlib.Path(stored_command).is_file()
        or shutil.which(stored_command) is not None
    )
    args_stale = any(
        value.lower().endswith(".py") and not pathlib.Path(value).is_file()
        for value in stored_args
    )
    cwd_stale = bool(stored_cwd) and not pathlib.Path(stored_cwd).is_dir()
    def configured_names(field: str) -> set[str]:
        value = server.get(field)
        if not isinstance(value, list) or not all(
            isinstance(item, str) for item in value
        ):
            return set()
        return set(value)

    launcher_names = {
        pathlib.Path(stored_command).name.casefold(),
        *(pathlib.Path(value).name.casefold() for value in stored_args),
    }
    managed_launcher = any(
        name == "revvy_blender_mcp.py" or name.startswith("revvyblendermcp")
        for name in launcher_names
    )
    managed_blender_entry = (
        managed_launcher
        and configured_names("allow_tools") == set(BLENDER_MCP_TOOLS)
        and configured_names("read_only_tools")
        == set(BLENDER_MCP_READ_ONLY_TOOLS)
        and configured_names("mutating_tools")
        == set(BLENDER_MCP_MUTATING_TOOLS)
    )
    # Missing means this is an older Revvy-generated entry. Preserve an explicit
    # user choice, including eager, instead of silently overriding it.
    activation_stale = managed_blender_entry and "activation" not in server
    if not (command_stale or args_stale or cwd_stale or activation_stale):
        return False

    server = dict(server)
    if activation_stale:
        server["activation"] = "on_demand"
        # Older Revvy-generated Blender entries predate both lazy activation
        # and its slightly larger cold-start budget. Keep explicit user values,
        # but migrate the managed legacy shape as one coherent contract.
        server.setdefault("startup_timeout_seconds", 18)
    if command_stale or args_stale:
        try:
            command, arguments = mcp_command or revvy_blender_mcp_command()
        except FileNotFoundError:
            return False
        server["command"] = str(command)
        server["args"] = [str(value) for value in arguments]
    if cwd_stale:
        project = pathlib.Path(project_dir).expanduser() if project_dir else None
        if project is not None and project.is_dir():
            server["cwd"] = str(project.resolve())
        else:
            current = pathlib.Path(str(server.get("command") or ""))
            if current.is_file():
                server["cwd"] = str(current.resolve().parent)
            else:
                server.pop("cwd", None)
    servers[BLENDER_MCP_SERVER_NAME] = server
    loaded["version"] = 1
    loaded["servers"] = servers
    _atomic_json_write(upstream_path, loaded)
    return True


def install_blender_bridge(
    project_dir: str | os.PathLike[str],
    *,
    blender_executable: str | os.PathLike[str] | None = None,
    env: dict[str, str] | None = None,
    source_dir: str | os.PathLike[str] | None = None,
    mcp_command: tuple[str, Sequence[str]] | None = None,
    run: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run,
) -> dict[str, Any]:
    values = os.environ if env is None else env
    executable = (
        pathlib.Path(blender_executable).expanduser().resolve()
        if blender_executable
        else next(iter(discover_blender_executables(values)), None)
    )
    if executable is None or not executable.is_file():
        raise FileNotFoundError("Blender executable was not found")
    version = blender_version(executable, run=run)
    appdata = values.get("APPDATA")
    if not appdata:
        raise RuntimeError("APPDATA is unavailable; Blender add-on path cannot be resolved")
    source = (
        pathlib.Path(source_dir).expanduser().resolve()
        if source_dir
        else blender_bridge_source_dir()
    )
    if not (source / "__init__.py").is_file():
        raise FileNotFoundError(f"Invalid Revvy Blender add-on source: {source}")
    addon_dir = (
        pathlib.Path(appdata)
        / "Blender Foundation"
        / "Blender"
        / version
        / "scripts"
        / "addons"
        / BLENDER_BRIDGE_MODULE
    )
    addon_dir.parent.mkdir(parents=True, exist_ok=True)
    if addon_dir.exists():
        shutil.rmtree(addon_dir)
    shutil.copytree(
        source,
        addon_dir,
        ignore=shutil.ignore_patterns("__pycache__", "*.pyc", "*.pyo"),
    )
    configuration = prepare_blender_bridge_configuration(
        project_dir,
        env=values,
        mcp_command=mcp_command,
    )
    enable_expression = (
        "import bpy; "
        f"bpy.ops.preferences.addon_enable(module={BLENDER_BRIDGE_MODULE!r}); "
        "bpy.ops.wm.save_userpref(); "
        f"import {BLENDER_BRIDGE_MODULE} as revvy_bridge; "
        f"print('REVVY_BLENDER_ADDON=' + str({BLENDER_BRIDGE_MODULE!r} in bpy.context.preferences.addons)); "
        "print('REVVY_BLENDER_BRIDGE=' + str(revvy_bridge._RUNTIME.running))"
    )
    completed = run(
        [str(executable), "--background", "--python-expr", enable_expression],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=90,
        check=False,
        env=dict(values),
    )
    output = f"{completed.stdout or ''}\n{completed.stderr or ''}"
    if (
        completed.returncode != 0
        or "REVVY_BLENDER_ADDON=True" not in output
        or "REVVY_BLENDER_BRIDGE=True" not in output
    ):
        raise RuntimeError(f"Blender add-on enable failed: {output.strip()}")
    return {
        **configuration,
        "blender_executable": executable,
        "blender_version": version,
        "addon_dir": addon_dir,
    }


def launch_blender(
    *,
    env: dict[str, str] | None = None,
    popen: Callable[..., subprocess.Popen[Any]] = subprocess.Popen,
) -> subprocess.Popen[Any]:
    values = os.environ if env is None else env
    executables = discover_blender_executables(values)
    if not executables:
        raise FileNotFoundError("Blender executable was not found")
    return popen([str(executables[0])], close_fds=True, env=dict(values))


def stage_image_attachments(
    source_paths: Sequence[str | os.PathLike[str]],
    *,
    destination_dir: str | os.PathLike[str] | None = None,
    env: dict[str, str] | None = None,
) -> list[pathlib.Path]:
    """Copy attachments to Revvy state using names created atomically."""

    if not source_paths:
        return []
    staging_root = (
        attachments_dir(env)
        if destination_dir is None
        else pathlib.Path(destination_dir).expanduser().resolve()
    )
    staging_root.mkdir(parents=True, exist_ok=True)
    owns_turn_dir = destination_dir is None
    target_dir = (
        pathlib.Path(tempfile.mkdtemp(prefix="turn-", dir=staging_root)).resolve()
        if owns_turn_dir
        else staging_root
    )
    staged: list[pathlib.Path] = []
    try:
        for value in source_paths:
            source = pathlib.Path(value).expanduser().resolve(strict=True)
            if not source.is_file():
                raise FileNotFoundError(f"Attachment is not a file: {source}")
            safe_stem = re.sub(r"[^A-Za-z0-9._-]+", "_", source.stem).strip("._")
            prefix = f"{(safe_stem or 'image')[:64]}-"
            descriptor, temporary_name = tempfile.mkstemp(
                prefix=prefix,
                suffix=source.suffix.lower(),
                dir=target_dir,
            )
            os.close(descriptor)
            destination = pathlib.Path(temporary_name).resolve()
            try:
                shutil.copy2(source, destination)
            except Exception:
                destination.unlink(missing_ok=True)
                raise
            staged.append(destination)
    except Exception:
        cleanup_staged_attachments(staged, destination_dir=staging_root)
        if owns_turn_dir:
            try:
                target_dir.rmdir()
            except OSError:
                pass
        raise
    return staged


def cleanup_staged_attachments(
    paths: Iterable[str | os.PathLike[str]],
    *,
    destination_dir: str | os.PathLike[str] | None = None,
    env: dict[str, str] | None = None,
) -> list[pathlib.Path]:
    """Remove only named files inside Revvy's attachment staging directory."""

    root = (
        attachments_dir(env)
        if destination_dir is None
        else pathlib.Path(destination_dir).expanduser()
    ).resolve()
    removed: list[pathlib.Path] = []
    candidate_parents: set[pathlib.Path] = set()
    for value in paths:
        candidate = pathlib.Path(value).expanduser().resolve(strict=False)
        try:
            candidate.relative_to(root)
        except ValueError:
            continue
        if candidate.is_dir():
            continue
        try:
            candidate.unlink()
        except (FileNotFoundError, IsADirectoryError):
            continue
        removed.append(candidate)
        candidate_parents.add(candidate.parent)
    for parent in sorted(candidate_parents, key=lambda path: len(path.parts), reverse=True):
        if parent == root:
            continue
        try:
            parent.relative_to(root)
            parent.rmdir()
        except (ValueError, OSError):
            continue
    return removed


@dataclass
class AppSettings:
    provider: str = PROVIDER_CODEX
    model: str = ""
    permission_mode: str = PERMISSION_SAFE
    reasoning_effort: str = ""
    codex_reasoning_effort: str = ""
    claude_reasoning_effort: str = ""
    kimi_reasoning_effort: str = ""
    effort_settings_version: int = 0
    project_dir: str = ""
    engine_override: str | None = None
    godot_executable: str = ""
    geometry: str = "1180x760"
    theme: str = "light"
    remember_history: bool = True
    codex_thread_id: str = ""
    codex_session_key: str = ""
    claude_session_id: str = ""
    claude_session_key: str = ""
    kimi_session_id: str = ""
    kimi_session_key: str = ""

    @classmethod
    def from_dict(cls, payload: Any) -> "AppSettings":
        if not isinstance(payload, dict):
            return cls()
        provider = str(payload.get("provider", PROVIDER_CODEX))
        if provider not in PROVIDERS:
            provider = PROVIDER_CODEX
        permission_mode = str(payload.get("permission_mode", PERMISSION_SAFE)).strip().lower()
        if permission_mode not in PERMISSION_MODES:
            permission_mode = PERMISSION_SAFE
        geometry = str(payload.get("geometry", "1180x760"))
        if not re.fullmatch(r"\d{3,5}x\d{3,5}(?:[+-]\d+[+-]\d+)?", geometry):
            geometry = "1180x760"
        theme = str(payload.get("theme", "light")).strip().lower()
        if theme not in {"light", "dark"}:
            theme = "light"
        engine_override = payload.get("engine_override")
        if engine_override is not None:
            engine_override = str(engine_override).strip().lower()
            if engine_override not in ENGINES:
                engine_override = None
        legacy_effort = str(payload.get("reasoning_effort", "")).strip()
        try:
            effort_settings_version = max(0, int(payload.get("effort_settings_version", 0)))
        except (TypeError, ValueError):
            effort_settings_version = 0
        return cls(
            provider=provider,
            model=str(payload.get("model", "")).strip(),
            permission_mode=permission_mode,
            reasoning_effort=legacy_effort,
            codex_reasoning_effort=str(
                payload.get(
                    "codex_reasoning_effort",
                    legacy_effort if provider == PROVIDER_CODEX else "",
                )
            ).strip(),
            claude_reasoning_effort=str(
                payload.get(
                    "claude_reasoning_effort",
                    legacy_effort if provider == PROVIDER_CLAUDE else "",
                )
            ).strip(),
            kimi_reasoning_effort=str(
                payload.get(
                    "kimi_reasoning_effort",
                    legacy_effort if provider == PROVIDER_KIMI else "",
                )
            ).strip(),
            effort_settings_version=effort_settings_version,
            project_dir=str(payload.get("project_dir", "")).strip(),
            engine_override=engine_override,
            godot_executable=str(payload.get("godot_executable", "")).strip(),
            geometry=geometry,
            theme=theme,
            remember_history=bool(payload.get("remember_history", True)),
            codex_thread_id=str(payload.get("codex_thread_id", "")).strip(),
            codex_session_key=str(payload.get("codex_session_key", "")).strip(),
            claude_session_id=str(payload.get("claude_session_id", "")).strip(),
            claude_session_key=str(payload.get("claude_session_key", "")).strip(),
            kimi_session_id=str(payload.get("kimi_session_id", "")).strip(),
            kimi_session_key=str(payload.get("kimi_session_key", "")).strip(),
        )


def initial_reasoning_effort(
    settings: AppSettings,
    provider: str,
    configured_effort: str = "",
) -> str:
    if provider == PROVIDER_CODEX:
        saved_effort = settings.codex_reasoning_effort
    elif provider == PROVIDER_CLAUDE:
        saved_effort = settings.claude_reasoning_effort
    else:
        saved_effort = settings.kimi_reasoning_effort
    if settings.effort_settings_version >= 1:
        return saved_effort
    legacy_effort = settings.reasoning_effort if settings.provider == provider else ""
    return configured_effort or saved_effort or legacy_effort


def load_settings(path: pathlib.Path | None = None) -> AppSettings:
    target = settings_path() if path is None else path
    try:
        payload = json.loads(target.read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeError, json.JSONDecodeError):
        return AppSettings()
    return AppSettings.from_dict(payload)


def save_settings(settings: AppSettings, path: pathlib.Path | None = None) -> None:
    target = settings_path() if path is None else path
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_suffix(target.suffix + ".tmp")
    temporary.write_text(
        json.dumps(asdict(settings), indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    os.replace(temporary, target)


def chat_history_path(env: dict[str, str] | None = None) -> pathlib.Path:
    return state_dir(env) / "chat-history.json"


def _history_project_key(project_dir: str | os.PathLike[str]) -> str:
    return os.path.normcase(os.path.abspath(os.path.expanduser(os.fspath(project_dir))))


def _clean_chat_messages(
    messages: Iterable[tuple[str, str]],
) -> list[tuple[str, str]]:
    candidates: list[tuple[str, str]] = []
    for item in messages:
        if not isinstance(item, (list, tuple)) or len(item) != 2:
            continue
        role, content = str(item[0]).strip().lower(), str(item[1]).strip()
        if role in {"user", "assistant"} and content:
            candidates.append((role, content))
    cleaned_reversed: list[tuple[str, str]] = []
    used_chars = 0
    for role, content in reversed(candidates):
        if used_chars + len(content) > MAX_CHAT_HISTORY_CHARS:
            break
        cleaned_reversed.append((role, content))
        used_chars += len(content)
        if len(cleaned_reversed) >= MAX_CHAT_HISTORY_MESSAGES:
            break
    cleaned_reversed.reverse()
    return cleaned_reversed


def _load_chat_history_archive(target: pathlib.Path) -> dict[str, Any]:
    try:
        payload = json.loads(target.read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeError, json.JSONDecodeError):
        return {"version": CHAT_HISTORY_VERSION, "projects": {}}
    if not isinstance(payload, dict) or not isinstance(payload.get("projects"), dict):
        return {"version": CHAT_HISTORY_VERSION, "projects": {}}
    return payload


def _write_chat_history_archive(target: pathlib.Path, payload: dict[str, Any]) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_suffix(target.suffix + ".tmp")
    try:
        temporary.write_text(
            json.dumps(payload, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )
        os.replace(temporary, target)
    finally:
        try:
            temporary.unlink()
        except OSError:
            pass


def load_chat_history(
    project_dir: str | os.PathLike[str],
    path: pathlib.Path | None = None,
) -> list[tuple[str, str]]:
    target = chat_history_path() if path is None else pathlib.Path(path)
    payload = _load_chat_history_archive(target)
    project = payload.get("projects", {}).get(_history_project_key(project_dir), {})
    if not isinstance(project, dict) or not isinstance(project.get("messages"), list):
        return []
    raw_messages = []
    for item in project["messages"]:
        if isinstance(item, dict):
            raw_messages.append((item.get("role", ""), item.get("content", "")))
        elif isinstance(item, (list, tuple)) and len(item) == 2:
            raw_messages.append((item[0], item[1]))
    return _clean_chat_messages(raw_messages)


def save_chat_history(
    project_dir: str | os.PathLike[str],
    messages: Iterable[tuple[str, str]],
    path: pathlib.Path | None = None,
) -> None:
    target = chat_history_path() if path is None else pathlib.Path(path)
    payload = _load_chat_history_archive(target)
    projects = payload.setdefault("projects", {})
    cleaned = _clean_chat_messages(messages)
    projects[_history_project_key(project_dir)] = {
        "project_dir": os.path.abspath(os.path.expanduser(os.fspath(project_dir))),
        "updated_at": int(time.time()),
        "messages": [
            {"role": role, "content": content}
            for role, content in cleaned
        ],
    }
    payload["version"] = CHAT_HISTORY_VERSION
    _write_chat_history_archive(target, payload)


def clear_chat_history(
    project_dir: str | os.PathLike[str],
    path: pathlib.Path | None = None,
) -> None:
    target = chat_history_path() if path is None else pathlib.Path(path)
    payload = _load_chat_history_archive(target)
    projects = payload.get("projects", {})
    if isinstance(projects, dict):
        projects.pop(_history_project_key(project_dir), None)
    payload["version"] = CHAT_HISTORY_VERSION
    payload["projects"] = projects if isinstance(projects, dict) else {}
    _write_chat_history_archive(target, payload)


def _settings_for_project(
    source: AppSettings,
    project_dir: str | os.PathLike[str],
    *,
    include_project_sessions: bool,
) -> AppSettings:
    """Copy preferences while preventing another project's resumable state."""
    payload = asdict(source)
    project = pathlib.Path(project_dir).expanduser().resolve()
    payload["project_dir"] = str(project)
    if not include_project_sessions:
        payload["engine_override"] = None
        payload["godot_executable"] = ""
        payload["permission_mode"] = PERMISSION_SAFE
        for field in (
            "codex_thread_id",
            "codex_session_key",
            "claude_session_id",
            "claude_session_key",
            "kimi_session_id",
            "kimi_session_key",
        ):
            payload[field] = ""
    else:
        permission_mode = str(payload.get("permission_mode") or PERMISSION_SAFE)
        providers = (
            (PROVIDER_CODEX, "codex_thread_id", "codex_session_key"),
            (PROVIDER_CLAUDE, "claude_session_id", "claude_session_key"),
            (PROVIDER_KIMI, "kimi_session_id", "kimi_session_key"),
        )
        for provider, session_field, key_field in providers:
            key = str(payload.get(key_field) or "")
            parts = key.split("|", 4)
            valid = (
                len(parts) == 5
                and parts[0] == provider
                and bool(parts[1])
                and same_project_path(parts[2], project)
                and parts[3] == permission_mode
            )
            if not valid:
                payload[session_field] = ""
                payload[key_field] = ""
    return AppSettings.from_dict(payload)


def _migrate_matching_chat_history(
    project_dir: str | os.PathLike[str],
    source: pathlib.Path,
    destination: pathlib.Path,
) -> bool:
    """Extract only this project's entry from the legacy global archive."""
    if destination.exists() or not source.is_file():
        return False
    messages = load_chat_history(project_dir, source)
    if not messages:
        return False
    save_chat_history(project_dir, messages, destination)
    return True


def load_project_settings(
    project_dir: str | os.PathLike[str],
    *,
    env: dict[str, str] | None = None,
    migrate_legacy: bool = True,
) -> AppSettings:
    """Load settings from ``<project>/.revvy`` with safe one-way migration.

    General UI preferences may seed a new local file. Engine overrides and
    provider session IDs are copied only when the legacy file names the exact
    same project; proxy/PID/log/manifest/attachment state is never migrated.
    """
    project = pathlib.Path(project_dir).expanduser().resolve()
    target = settings_path(env)
    local_history = chat_history_path(env)
    legacy_root = legacy_state_dir(env)
    legacy_settings = legacy_root / "revvy-app.json"
    legacy_history = legacy_root / "chat-history.json"

    if target.is_file():
        loaded = load_settings(target)
        settings = _settings_for_project(
            loaded,
            project,
            include_project_sessions=same_project_path(loaded.project_dir, project),
        )
        if migrate_legacy:
            if settings != loaded:
                save_settings(settings, target)
            _migrate_matching_chat_history(project, legacy_history, local_history)
        return settings

    settings = AppSettings(project_dir=str(project))
    if migrate_legacy and legacy_settings.is_file() and not same_project_path(
        target, legacy_settings
    ):
        previous = load_settings(legacy_settings)
        settings = _settings_for_project(
            previous,
            project,
            include_project_sessions=same_project_path(previous.project_dir, project),
        )

    if migrate_legacy:
        save_settings(settings, target)
        _migrate_matching_chat_history(project, legacy_history, local_history)
    return settings


_CHAT_SECRET_PATTERNS = (
    re.compile(r"(?i)\b(?:authorization\s*:\s*bearer|bearer)\s+[A-Za-z0-9._~+/=-]{16,}"),
    re.compile(r"(?i)\b[A-Z][A-Z0-9_]*(?:TOKEN|SECRET|API_KEY)\s*=\s*[^\s\"']+"),
    re.compile(r"(?<![A-Za-z0-9])(?:sk-[A-Za-z0-9_-]{16,}|rdt_[A-Za-z0-9_-]{16,})"),
)


def _redact_chat_secrets(value: str) -> str:
    redacted = value
    for pattern in _CHAT_SECRET_PATTERNS:
        redacted = pattern.sub("[인증 정보 숨김]", redacted)
    return redacted


def import_codex_session_history(
    path: str | os.PathLike[str],
    *,
    include_commentary: bool = False,
) -> list[tuple[str, str]]:
    """Recover user-visible turns from a Codex rollout without importing tool internals."""

    messages: list[tuple[str, str]] = []
    pending_assistant: list[str] = []

    def flush_assistant() -> None:
        if not pending_assistant:
            return
        content = _redact_chat_secrets("\n\n".join(pending_assistant).strip())
        pending_assistant.clear()
        if content:
            messages.append(("assistant", content))

    try:
        with pathlib.Path(path).open("r", encoding="utf-8-sig") as source:
            for line in source:
                try:
                    event = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if not isinstance(event, dict) or event.get("type") != "event_msg":
                    continue
                payload = event.get("payload", {})
                if not isinstance(payload, dict):
                    continue
                payload_type = str(payload.get("type") or "")
                if payload_type == "user_message":
                    flush_assistant()
                    content = str(payload.get("message") or "").replace("\r\n", "\n").strip()
                    marker = "Current user request:"
                    marker_index = content.rfind(marker)
                    prefix = content[:marker_index] if marker_index >= 0 else ""
                    if marker_index >= 0 and "Revvy" in prefix:
                        content = content.rsplit(marker, 1)[1].strip()
                    content = _redact_chat_secrets(content)
                    if content:
                        messages.append(("user", content))
                    continue
                if payload_type != "agent_message":
                    continue
                phase = str(payload.get("phase") or "")
                if phase == "commentary" and not include_commentary:
                    continue
                if phase not in {"commentary", "final_answer"}:
                    continue
                content = str(payload.get("message") or "").strip()
                if content:
                    pending_assistant.append(content)
                if phase == "final_answer":
                    flush_assistant()
    except (OSError, UnicodeError):
        return []
    flush_assistant()
    return _clean_chat_messages(messages)


class UnrealConfigurationError(ValueError):
    """Raised when a project, engine, or editor target cannot be resolved safely."""


class GodotConfigurationError(ValueError):
    """Raised when a Godot project or editor executable cannot be resolved."""


@dataclass(frozen=True)
class UnrealProjectContext:
    project_file: pathlib.Path
    engine_association: str
    engine_root: pathlib.Path
    editor_target: str


def resolve_uproject(selection: str | os.PathLike[str]) -> pathlib.Path:
    """Resolve a selected project directory or an explicit ``.uproject`` file."""

    selected = pathlib.Path(selection).expanduser()
    if selected.is_file():
        if selected.suffix.casefold() != ".uproject":
            raise UnrealConfigurationError(f"Not an Unreal project file: {selected}")
        return selected.resolve()
    if not selected.is_dir():
        raise UnrealConfigurationError(f"Project path does not exist: {selected}")

    projects = sorted(
        (path for path in selected.iterdir() if path.is_file() and path.suffix.casefold() == ".uproject"),
        key=lambda path: path.name.casefold(),
    )
    if len(projects) != 1:
        raise UnrealConfigurationError(
            f"Expected exactly one .uproject in {selected}, found {len(projects)}."
        )
    return projects[0].resolve()


def resolve_godot_project(selection: str | os.PathLike[str]) -> pathlib.Path:
    """Resolve a Godot project directory or its explicit ``project.godot`` file."""

    selected = pathlib.Path(selection).expanduser()
    if selected.is_file():
        if selected.name.casefold() != "project.godot":
            raise GodotConfigurationError(f"Not a Godot project file: {selected}")
        return selected.resolve()
    if not selected.is_dir():
        raise GodotConfigurationError(f"Project path does not exist: {selected}")
    project_file = selected / "project.godot"
    if not project_file.is_file():
        raise GodotConfigurationError(f"project.godot was not found in {selected}.")
    return project_file.resolve()


def read_engine_association(project_file: str | os.PathLike[str]) -> str:
    project = resolve_uproject(project_file)
    try:
        payload = json.loads(project.read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise UnrealConfigurationError(f"Could not parse {project.name}: {exc}") from exc
    if not isinstance(payload, dict):
        raise UnrealConfigurationError(f"Invalid Unreal project descriptor: {project}")
    association = payload.get("EngineAssociation")
    if not isinstance(association, str) or not association.strip():
        raise UnrealConfigurationError(f"EngineAssociation is missing in {project.name}.")
    return association.strip()


def set_engine_association(
    project_file: str | os.PathLike[str],
    association: str,
) -> None:
    """Rewrite the .uproject EngineAssociation (engine version switch/upgrade)."""

    project = resolve_uproject(project_file)
    association = association.strip()
    if not association:
        raise UnrealConfigurationError("EngineAssociation is empty.")
    try:
        payload = json.loads(project.read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise UnrealConfigurationError(f"Could not parse {project.name}: {exc}") from exc
    if not isinstance(payload, dict):
        raise UnrealConfigurationError(f"Invalid Unreal project descriptor: {project}")
    payload["EngineAssociation"] = association
    project.write_text(
        json.dumps(payload, indent=4, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


def unreal_editor_executable(engine_root: str | os.PathLike[str]) -> pathlib.Path:
    return pathlib.Path(engine_root) / "Engine" / "Binaries" / "Win64" / "UnrealEditor.exe"


def unreal_build_script(engine_root: str | os.PathLike[str]) -> pathlib.Path:
    return pathlib.Path(engine_root) / "Engine" / "Build" / "BatchFiles" / "Build.bat"


def unreal_generate_project_files_script(
    engine_root: str | os.PathLike[str],
) -> pathlib.Path:
    return (
        pathlib.Path(engine_root)
        / "Engine"
        / "Build"
        / "BatchFiles"
        / "GenerateProjectFiles.bat"
    )


def unreal_build_tool_executable(engine_root: str | os.PathLike[str]) -> pathlib.Path:
    return (
        pathlib.Path(engine_root)
        / "Engine"
        / "Binaries"
        / "DotNET"
        / "UnrealBuildTool"
        / "UnrealBuildTool.exe"
    )


def validate_unreal_engine_root(engine_root: str | os.PathLike[str]) -> pathlib.Path:
    root = pathlib.Path(engine_root).expanduser()
    missing = [
        path
        for path in (unreal_editor_executable(root), unreal_build_script(root))
        if not path.is_file()
    ]
    if missing:
        detail = ", ".join(str(path.relative_to(root)) for path in missing)
        raise UnrealConfigurationError(f"Invalid Unreal Engine root {root}: missing {detail}.")
    return root.resolve()


VS_BUILD_TOOLS_BOOTSTRAPPER_URL = "https://aka.ms/vs/17/release/vs_buildtools.exe"
VS_CPP_REQUIRED_COMPONENT = "Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
VS_CPP_INSTALL_COMPONENTS = (
    "Microsoft.VisualStudio.Workload.VCTools",
    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
    "Microsoft.VisualStudio.Component.Windows11SDK.22621",
)
VS_INSTALL_SUCCESS_CODES = frozenset({0, 3010})  # 3010: success, reboot required
VS_INSTALL_CANCELLED_CODE = 1602  # ERROR_INSTALL_USEREXIT / UAC declined (1223)
VS_INSTALL_LAUNCH_FAILED_CODE = 1600  # bootstrapper could not be started at all
VS_INSTALL_MIN_FREE_BYTES = 10 * 1024 ** 3  # VS Build Tools + Windows SDK payloads


def unreal_project_has_cpp_source(project_dir: str | os.PathLike[str]) -> bool:
    """True when the project has C++ targets and therefore needs a compiler."""
    source = pathlib.Path(project_dir).expanduser() / "Source"
    try:
        return source.is_dir() and any(source.glob("*.Target.cs"))
    except OSError:
        return False


def unreal_editor_module_built(
    project_file: str | os.PathLike[str],
    editor_target: str,
) -> bool:
    """True when the project editor module DLL exists under Binaries/Win64."""

    project = resolve_uproject(project_file)
    target = editor_target.strip()
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", target):
        raise UnrealConfigurationError(f"Invalid Unreal target name: {editor_target!r}")
    return (
        project.parent / "Binaries" / "Win64" / f"UnrealEditor-{target}.dll"
    ).is_file()


def vswhere_executable(env: dict[str, str] | None = None) -> pathlib.Path | None:
    values = os.environ if env is None else env
    for key in ("ProgramFiles(x86)", "ProgramFiles"):
        base = values.get(key)
        if not base:
            continue
        candidate = (
            pathlib.Path(base) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
        )
        if candidate.is_file():
            return candidate
    return None


def detect_visual_cpp_toolchain(
    env: dict[str, str] | None = None,
    *,
    run: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run,
    vswhere: str | os.PathLike[str] | None = None,
) -> dict[str, Any]:
    """Locate a Visual Studio or Build Tools install that has the C++ toolset."""
    located = pathlib.Path(vswhere) if vswhere is not None else vswhere_executable(env)
    if located is None:
        return {"found": False, "reason": "vswhere.exe not found"}
    try:
        completed = run(
            [
                str(located),
                "-products",
                "*",
                "-requires",
                VS_CPP_REQUIRED_COMPONENT,
                # UE5 needs the VS2022 (v143) toolset; VC.Tools.x86.x64 alone
                # also matches VS2017/2019 installs that cannot build UE5.
                "-version",
                "[17.0,)",
                "-prerelease",
                "-latest",
                "-utf8",
                "-format",
                "json",
            ],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=30,
            check=False,
            creationflags=int(getattr(subprocess, "CREATE_NO_WINDOW", 0)),
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        return {"found": False, "reason": str(exc)}
    try:
        payload = json.loads(completed.stdout or "[]")
    except json.JSONDecodeError:
        payload = []
    if completed.returncode != 0 or not isinstance(payload, list) or not payload:
        return {"found": False, "reason": "no install provides the C++ toolset"}
    first = payload[0] if isinstance(payload[0], dict) else {}
    return {
        "found": True,
        "name": str(first.get("displayName") or "Visual Studio"),
        "version": str(first.get("installationVersion") or ""),
        "path": str(first.get("installationPath") or ""),
    }


# --- Engine-verified build environment (mirrors UDevEnvironmentService) -------
#
# The truth source is the engine's own Engine/Config/Windows/Windows_SDK.json;
# the fallbacks below are the UE 5.8 values and must match the C++ service in
# Source/Revvy/Private/PythonAPI/UDevEnvironmentService.cpp.
DEFAULT_MIN_MSVC = "14.38.33130"
DEFAULT_BANNED_MSVC = (
    "14.50.0-14.50.35722",
    "14.44.0-14.44.35210",
    "14.40.0-14.43.99999",
    "14.39.0-14.39.99999",
)
DEFAULT_MIN_WINDOWS_SDK = "10.0.19041.0"
_VERSION_DIR_PATTERN = re.compile(r"\d+(?:\.\d+){1,3}")


def _parse_version(value: str) -> tuple[int, int, int, int]:
    """"14.38.33130" -> (14, 38, 33130, 0); tolerant of 2..4 fields."""

    parts: list[int] = []
    for field in str(value).split(".")[:4]:
        try:
            parts.append(int(field))
        except ValueError:
            parts.append(0)
    while len(parts) < 4:
        parts.append(0)
    return tuple(parts)  # type: ignore[return-value]


def compare_versions(left: str, right: str) -> int:
    """-1 / 0 / 1, lexicographic over the four numeric fields."""

    a = _parse_version(left)
    b = _parse_version(right)
    return (a > b) - (a < b)


def version_in_range(version: str, version_range: str) -> bool:
    """Inclusive range "lo-hi"."""

    lo, separator, hi = str(version_range).partition("-")
    if not separator:
        return False
    return compare_versions(version, lo) >= 0 and compare_versions(version, hi) <= 0


def load_unreal_engine_requirements(
    engine_root: str | os.PathLike[str],
) -> tuple[str, tuple[str, ...], str]:
    """(min_msvc, banned_msvc_ranges, min_windows_sdk) from Windows_SDK.json."""

    min_msvc = DEFAULT_MIN_MSVC
    banned = DEFAULT_BANNED_MSVC
    min_windows_sdk = DEFAULT_MIN_WINDOWS_SDK
    path = (
        pathlib.Path(engine_root)
        / "Engine"
        / "Config"
        / "Windows"
        / "Windows_SDK.json"
    )
    try:
        payload = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeError, json.JSONDecodeError):
        return min_msvc, banned, min_windows_sdk
    if not isinstance(payload, dict):
        return min_msvc, banned, min_windows_sdk
    value = payload.get("MinimumVisualCppVersion")
    if isinstance(value, str) and value.strip():
        min_msvc = value.strip()
    value = payload.get("MinVersion")
    if isinstance(value, str) and value.strip():
        min_windows_sdk = value.strip()
    listed = payload.get("BannedVisualCppVersions")
    if isinstance(listed, list):
        banned = tuple(str(item) for item in listed if isinstance(item, str) and item.strip())
    return min_msvc, banned, min_windows_sdk


def list_visual_cpp_installations(
    env: dict[str, str] | None = None,
    *,
    run: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run,
    vswhere: str | os.PathLike[str] | None = None,
) -> list[dict[str, str]]:
    """Every VS2022+ install with the C++ toolset (unlike the detector, no -latest)."""

    located = pathlib.Path(vswhere) if vswhere is not None else vswhere_executable(env)
    if located is None:
        return []
    try:
        completed = run(
            [
                str(located),
                "-products",
                "*",
                "-requires",
                VS_CPP_REQUIRED_COMPONENT,
                "-version",
                "[17.0,)",
                "-prerelease",
                "-utf8",
                "-format",
                "json",
            ],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=30,
            check=False,
            creationflags=int(getattr(subprocess, "CREATE_NO_WINDOW", 0)),
        )
    except (OSError, subprocess.TimeoutExpired):
        return []
    try:
        payload = json.loads(completed.stdout or "[]")
    except json.JSONDecodeError:
        return []
    if completed.returncode != 0 or not isinstance(payload, list):
        return []
    installations: list[dict[str, str]] = []
    for entry in payload:
        if not isinstance(entry, dict):
            continue
        path = str(entry.get("installationPath") or "").strip()
        if not path:
            continue
        installations.append(
            {
                "name": str(entry.get("displayName") or "Visual Studio"),
                "version": str(entry.get("installationVersion") or ""),
                "path": path,
            }
        )
    return installations


def check_unreal_build_environment(
    engine_root: str | os.PathLike[str],
    env: dict[str, str] | None = None,
    *,
    run: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run,
    vswhere: str | os.PathLike[str] | None = None,
) -> dict[str, Any]:
    """Validate MSVC toolsets and the Windows SDK against the engine's rules.

    Mirrors UDevEnvironmentService.check_environment(): a toolset counts only
    when it meets the engine minimum, sits outside every banned range, and its
    cl.exe actually exists; the SDK counts when um/windows.h is present and the
    version meets the engine minimum.
    """

    values = os.environ if env is None else env
    min_msvc, banned_ranges, min_windows_sdk = load_unreal_engine_requirements(engine_root)

    valid_versions: list[str] = []
    rejected: list[dict[str, str]] = []
    for installation in list_visual_cpp_installations(env, run=run, vswhere=vswhere):
        msvc_root = pathlib.Path(installation["path"]) / "VC" / "Tools" / "MSVC"
        try:
            versions = sorted(
                entry.name
                for entry in msvc_root.iterdir()
                if entry.is_dir() and _VERSION_DIR_PATTERN.fullmatch(entry.name)
            )
        except OSError:
            continue
        for version in versions:
            if compare_versions(version, min_msvc) < 0:
                rejected.append({"version": version, "reason": "below_minimum"})
                continue
            if any(version_in_range(version, banned) for banned in banned_ranges):
                rejected.append({"version": version, "reason": "banned_compiler_bug"})
                continue
            cl_exe = msvc_root / version / "bin" / "Hostx64" / "x64" / "cl.exe"
            if cl_exe.is_file():
                valid_versions.append(version)

    sdk_versions: list[str] = []
    sdk_ok = False
    program_files_x86 = str(values.get("ProgramFiles(x86)") or "").strip()
    if program_files_x86:
        kit_include = (
            pathlib.Path(program_files_x86) / "Windows Kits" / "10" / "Include"
        )
        try:
            sdk_candidates = sorted(
                entry.name
                for entry in kit_include.iterdir()
                if entry.is_dir() and _VERSION_DIR_PATTERN.fullmatch(entry.name)
            )
        except OSError:
            sdk_candidates = []
        for version in sdk_candidates:
            # A real SDK ships um/windows.h.
            if not (kit_include / version / "um" / "windows.h").is_file():
                continue
            sdk_versions.append(version)
            if compare_versions(version, min_windows_sdk) >= 0:
                sdk_ok = True

    toolchain_ok = bool(valid_versions)
    return {
        "ok": toolchain_ok and sdk_ok,
        "requirements": {
            "min_msvc": min_msvc,
            "banned_msvc": list(banned_ranges),
            "min_windows_sdk": min_windows_sdk,
        },
        "toolchain": {
            "ok": toolchain_ok,
            "valid_versions": valid_versions,
            "rejected": rejected,
        },
        "windows_sdk": {"ok": sdk_ok, "versions": sdk_versions},
    }


def download_file(
    destination: pathlib.Path,
    *,
    url: str,
    progress: Callable[[str], None] | None = None,
    opener: Callable[[str], Any] | None = None,
    progress_label: str = "설치 프로그램",
) -> pathlib.Path:
    destination.parent.mkdir(parents=True, exist_ok=True)
    open_url = opener or (lambda target: urllib.request.urlopen(target, timeout=60))
    temporary = destination.with_suffix(destination.suffix + ".tmp")
    try:
        with open_url(url) as response, open(temporary, "wb") as sink:
            headers = getattr(response, "headers", None)
            try:
                total = int(headers.get("Content-Length") or 0) if headers else 0
            except (TypeError, ValueError):
                total = 0
            received = 0
            last_percent = -1
            while True:
                chunk = response.read(256 * 1024)
                if not chunk:
                    break
                sink.write(chunk)
                received += len(chunk)
                if progress and total:
                    percent = received * 100 // total
                    if percent != last_percent:
                        last_percent = percent
                        progress(f"{progress_label} 다운로드 중 {percent}%")
        # http.client returns b'' instead of raising when a Content-Length
        # response closes early, so an explicit size check is required to keep
        # a truncated download from being promoted and used.
        if total and received != total:
            raise OSError(
                f"download truncated: received {received} of {total} bytes"
            )
    except BaseException:
        try:
            temporary.unlink()
        except OSError:
            pass
        raise
    os.replace(temporary, destination)
    return destination


def download_vs_build_tools_bootstrapper(
    destination: pathlib.Path,
    *,
    url: str = VS_BUILD_TOOLS_BOOTSTRAPPER_URL,
    progress: Callable[[str], None] | None = None,
    opener: Callable[[str], Any] | None = None,
) -> pathlib.Path:
    return download_file(destination, url=url, progress=progress, opener=opener)


def _powershell_quote(value: str) -> str:
    return "'" + value.replace("'", "''") + "'"


def vs_build_tools_install_command(
    bootstrapper: str | os.PathLike[str],
) -> list[str]:
    """Run the bootstrapper elevated (UAC) and forward its exit code.

    The bootstrapper both installs a fresh Build Tools instance and modifies an
    existing one that lacks the requested components, so one command covers
    first installs and repairs alike.
    """
    arguments = ["--passive", "--wait", "--norestart", "--nocache"]
    for component in VS_CPP_INSTALL_COMPONENTS:
        arguments.extend(("--add", component))
    argument_list = ", ".join(_powershell_quote(value) for value in arguments)
    script = (
        "try { "
        f"$process = Start-Process -FilePath {_powershell_quote(os.fspath(bootstrapper))} "
        f"-ArgumentList {argument_list} -Verb RunAs -Wait -PassThru -ErrorAction Stop; "
        "exit $process.ExitCode "
        "} catch { "
        "$inner = $_.Exception.InnerException; "
        "if ($inner -is [System.ComponentModel.Win32Exception] -and "
        "$inner.NativeErrorCode -eq 1223) { "
        f"exit {VS_INSTALL_CANCELLED_CODE} "
        "} else { "
        f"exit {VS_INSTALL_LAUNCH_FAILED_CODE} "
        "} }"
    )
    return [
        "powershell.exe",
        "-NoProfile",
        "-NonInteractive",
        "-ExecutionPolicy",
        "Bypass",
        "-Command",
        script,
    ]


def install_visual_cpp_build_tools(
    env: dict[str, str] | None = None,
    *,
    progress: Callable[[str], None] | None = None,
    run: Callable[..., subprocess.CompletedProcess[Any]] = subprocess.run,
    download: Callable[..., pathlib.Path] | None = None,
    engine_root: str | os.PathLike[str] | None = None,
    disk_usage: Callable[..., Any] | None = None,
) -> tuple[bool, str]:
    """Download and run the VS Build Tools installer, then verify the toolset.

    Returns (ok, user-facing message). Verification is the source of truth:
    with ``engine_root`` the engine-aware environment check decides (so a
    toolchain the engine bans never reports success); otherwise the vswhere
    probe is used. Installer exit codes alone are not trusted for success.
    Aborts before downloading when the install drive has too little free space.
    """
    notify = progress or (lambda _line: None)
    bootstrapper = global_cache_dir(env) / "downloads" / "vs_buildtools.exe"
    fetch = download or download_vs_build_tools_bootstrapper
    measure = disk_usage or shutil.disk_usage
    probe = f"{bootstrapper.drive}\\" if bootstrapper.drive else "."
    try:
        free_bytes = int(measure(probe).free)
    except (OSError, TypeError, ValueError, AttributeError):
        free_bytes = -1
    if 0 <= free_bytes < VS_INSTALL_MIN_FREE_BYTES:
        return False, (
            "디스크 여유 공간이 부족합니다 "
            f"(설치에 약 {VS_INSTALL_MIN_FREE_BYTES // (1024 ** 3)}GB가 필요하고 "
            f"현재 {free_bytes / (1024 ** 3):.1f}GB 남았습니다). "
            "공간을 확보한 뒤 다시 시도해 주세요."
        )
    try:
        fetch(bootstrapper, progress=notify)
    except (OSError, http.client.HTTPException) as exc:
        # URLError is an OSError subclass; HTTPException (e.g. IncompleteRead
        # on a truncated chunked response) is not.
        return False, (
            f"Visual Studio Build Tools 설치 프로그램을 내려받지 못했습니다 ({exc}). "
            f"브라우저에서 직접 받아 설치해 주세요: {VS_BUILD_TOOLS_BOOTSTRAPPER_URL}"
        )
    notify("Visual Studio Build Tools 설치를 시작합니다 (10~30분 정도 걸립니다)...")
    notify("관리자 권한 확인 창이 뜨면 '예'를 눌러 주세요.")
    try:
        completed = run(
            vs_build_tools_install_command(bootstrapper),
            check=False,
            timeout=3 * 60 * 60,
            creationflags=int(getattr(subprocess, "CREATE_NO_WINDOW", 0)),
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        return False, f"설치 프로그램을 실행하지 못했습니다: {exc}"
    if completed.returncode == VS_INSTALL_CANCELLED_CODE:
        return False, (
            "설치가 취소되었습니다. 관리자 권한 확인 창에서 '예'를 눌러야 설치할 수 있습니다."
        )
    if completed.returncode == VS_INSTALL_LAUNCH_FAILED_CODE:
        return False, (
            "설치 프로그램을 실행하지 못했습니다. 다운로드된 파일이 손상되었거나 "
            "보안 프로그램이 차단했을 수 있습니다. 다시 시도하거나 브라우저에서 "
            f"직접 받아 설치해 주세요: {VS_BUILD_TOOLS_BOOTSTRAPPER_URL}"
        )
    if completed.returncode not in VS_INSTALL_SUCCESS_CODES:
        return False, (
            f"Visual Studio Build Tools 설치가 실패했습니다 (종료 코드 {completed.returncode}). "
            f"브라우저에서 직접 받아 설치해 주세요: {VS_BUILD_TOOLS_BOOTSTRAPPER_URL}"
        )
    suffix = " (재부팅 후 적용됩니다)" if completed.returncode == 3010 else ""
    if engine_root is not None:
        environment = check_unreal_build_environment(engine_root, env)
        if not environment.get("ok"):
            return False, (
                "설치는 끝났지만 엔진이 허용하는 C++ 빌드 도구를 아직 찾지 못했습니다. "
                "PC를 재부팅한 뒤 다시 시도해 주세요."
            )
        valid = environment["toolchain"]["valid_versions"]
        version = max(valid, key=_parse_version) if valid else ""
        label = f"MSVC {version}" if version else "C++ 빌드 도구"
        return True, f"{label} 설치 완료{suffix}"
    detected = detect_visual_cpp_toolchain(env)
    if not detected.get("found"):
        return False, (
            "설치는 끝났지만 C++ 도구를 아직 찾지 못했습니다. "
            "PC를 재부팅한 뒤 다시 시도해 주세요."
        )
    return True, f"{detected['name']} 설치 완료{suffix}"


def _normalized_engine_association(value: str) -> str:
    return value.strip().strip("{}").casefold()


def registry_unreal_engine_candidates(
    engine_association: str,
    registry_module: Any | None = None,
) -> list[pathlib.Path]:
    """Read source builds from HKCU and installed builds from HKLM on Windows."""

    if registry_module is None:
        if os.name != "nt":
            return []
        try:
            import winreg as registry_module  # type: ignore[no-redef]
        except ImportError:  # pragma: no cover - non-Windows defensive fallback
            return []

    candidates: list[pathlib.Path] = []
    association = engine_association.strip()
    normalized = _normalized_engine_association(association)
    key_read = int(getattr(registry_module, "KEY_READ", 0))

    custom_key = None
    try:
        custom_key = registry_module.OpenKey(
            registry_module.HKEY_CURRENT_USER,
            r"SOFTWARE\Epic Games\Unreal Engine\Builds",
            0,
            key_read,
        )
        index = 0
        while True:
            try:
                value_name, value, _ = registry_module.EnumValue(custom_key, index)
            except OSError:
                break
            index += 1
            if (
                _normalized_engine_association(str(value_name)) == normalized
                and isinstance(value, str)
                and value.strip()
            ):
                candidates.append(pathlib.Path(value.strip()))
    except OSError:
        pass
    finally:
        if custom_key is not None:
            registry_module.CloseKey(custom_key)

    version = association[3:] if association.casefold().startswith("ue_") else association
    view_flags: list[int] = []
    for flag in (
        int(getattr(registry_module, "KEY_WOW64_64KEY", 0)),
        int(getattr(registry_module, "KEY_WOW64_32KEY", 0)),
        0,
    ):
        if flag not in view_flags:
            view_flags.append(flag)
    for view_flag in view_flags:
        standard_key = None
        try:
            standard_key = registry_module.OpenKey(
                registry_module.HKEY_LOCAL_MACHINE,
                rf"SOFTWARE\EpicGames\Unreal Engine\{version}",
                0,
                key_read | view_flag,
            )
            value, _ = registry_module.QueryValueEx(standard_key, "InstalledDirectory")
            if isinstance(value, str) and value.strip():
                candidates.append(pathlib.Path(value.strip()))
        except OSError:
            pass
        finally:
            if standard_key is not None:
                registry_module.CloseKey(standard_key)
    return candidates


def well_known_unreal_engine_candidates(
    engine_association: str,
    env: dict[str, str] | None = None,
) -> list[pathlib.Path]:
    values = os.environ if env is None else env
    version = engine_association.strip()
    if version.casefold().startswith("ue_"):
        version = version[3:]
    if not re.fullmatch(r"\d+(?:\.\d+){1,2}[A-Za-z0-9_.-]*", version):
        return []

    program_files: list[pathlib.Path] = []
    for name in ("PROGRAMW6432", "PROGRAMFILES", "ProgramFiles", "PROGRAMFILES(X86)"):
        value = values.get(name)
        if value:
            path = pathlib.Path(value)
            if path not in program_files:
                program_files.append(path)
    if os.name == "nt" and not program_files:
        program_files.append(pathlib.Path(r"C:\Program Files"))
    return [path / "Epic Games" / f"UE_{version}" for path in program_files]


def find_unreal_engine_root(
    engine_association: str,
    *,
    env: dict[str, str] | None = None,
    registry_candidates: Iterable[str | os.PathLike[str]] | None = None,
    well_known_candidates: Iterable[str | os.PathLike[str]] | None = None,
) -> pathlib.Path:
    association = engine_association.strip()
    if not association:
        raise UnrealConfigurationError("EngineAssociation is empty.")
    registry_roots = (
        registry_unreal_engine_candidates(association)
        if registry_candidates is None
        else [pathlib.Path(path) for path in registry_candidates]
    )
    known_roots = (
        well_known_unreal_engine_candidates(association, env)
        if well_known_candidates is None
        else [pathlib.Path(path) for path in well_known_candidates]
    )

    attempted: list[str] = []
    seen: set[str] = set()
    for candidate in (*registry_roots, *known_roots):
        key = os.path.normcase(os.path.abspath(candidate))
        if key in seen:
            continue
        seen.add(key)
        attempted.append(str(candidate))
        try:
            return validate_unreal_engine_root(candidate)
        except UnrealConfigurationError:
            continue
    detail = ", ".join(attempted) if attempted else "no registered or standard locations"
    raise UnrealConfigurationError(
        f"Unreal Engine {association} was not found ({detail})."
    )


UNREAL_DOWNLOAD_URL = "https://www.unrealengine.com/download"


def list_installed_unreal_engines(
    registry_module: Any | None = None,
) -> list[tuple[str, str]]:
    """(version, install root) pairs for launcher-installed engines, newest first."""

    if registry_module is None:
        if os.name != "nt":
            return []
        try:
            import winreg as registry_module  # type: ignore[no-redef]
        except ImportError:  # pragma: no cover - non-Windows defensive fallback
            return []
    key_read = int(getattr(registry_module, "KEY_READ", 0))
    base_key = r"SOFTWARE\EpicGames\Unreal Engine"
    view_flags: list[int] = []
    for flag in (
        int(getattr(registry_module, "KEY_WOW64_64KEY", 0)),
        int(getattr(registry_module, "KEY_WOW64_32KEY", 0)),
        0,
    ):
        if flag not in view_flags:
            view_flags.append(flag)
    found: dict[str, str] = {}
    for view_flag in view_flags:
        try:
            key = registry_module.OpenKey(
                registry_module.HKEY_LOCAL_MACHINE, base_key, 0, key_read | view_flag
            )
        except OSError:
            continue
        try:
            index = 0
            while True:
                try:
                    version = str(registry_module.EnumKey(key, index))
                except OSError:
                    break
                index += 1
                if version in found or not re.fullmatch(r"\d+(?:\.\d+){1,2}", version):
                    continue
                try:
                    subkey = registry_module.OpenKey(
                        registry_module.HKEY_LOCAL_MACHINE,
                        rf"{base_key}\{version}",
                        0,
                        key_read | view_flag,
                    )
                    value, _ = registry_module.QueryValueEx(subkey, "InstalledDirectory")
                    registry_module.CloseKey(subkey)
                except OSError:
                    continue
                if isinstance(value, str) and value.strip():
                    found[version] = value.strip()
        finally:
            registry_module.CloseKey(key)
    return sorted(found.items(), key=lambda item: _parse_version(item[0]), reverse=True)


def epic_games_launcher_executable(
    env: dict[str, str] | None = None,
) -> pathlib.Path | None:
    values = os.environ if env is None else env
    for key in ("ProgramFiles(x86)", "ProgramFiles"):
        base = str(values.get(key) or "").strip()
        if not base:
            continue
        candidate = (
            pathlib.Path(base)
            / "Epic Games"
            / "Launcher"
            / "Portal"
            / "Binaries"
            / "Win64"
            / "EpicGamesLauncher.exe"
        )
        if candidate.is_file():
            return candidate
    return None



def _target_info_editor_names(path: pathlib.Path) -> list[str]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeError, json.JSONDecodeError):
        return []
    targets = payload.get("Targets") if isinstance(payload, dict) else None
    if not isinstance(targets, list):
        return []
    names: list[str] = []
    for target in targets:
        if not isinstance(target, dict):
            continue
        target_type = target.get("Type")
        name = target.get("Name")
        if (
            isinstance(target_type, str)
            and target_type.casefold() == "editor"
            and isinstance(name, str)
            and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name.strip())
        ):
            names.append(name.strip())
    return list(dict.fromkeys(names))


def _strip_csharp_comments(source: str) -> str:
    non_code = re.compile(
        r'@"(?:""|[^"])*"|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
        r"|//[^\r\n]*|/\*.*?\*/",
        flags=re.DOTALL,
    )
    return non_code.sub(lambda match: " " * len(match.group(0)), source)


def _class_body(source: str, start: int) -> str:
    opening = source.find("{", start)
    if opening < 0:
        return ""
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    return ""


def _source_editor_names(source_dir: pathlib.Path) -> list[str]:
    names: list[str] = []
    for target_file in sorted(source_dir.rglob("*.Target.cs")):
        try:
            source = _strip_csharp_comments(target_file.read_text(encoding="utf-8-sig"))
        except (OSError, UnicodeError):
            continue
        for match in re.finditer(
            r"\bclass\s+([A-Za-z_][A-Za-z0-9_]*)\s*:\s*TargetRules\b",
            source,
        ):
            class_name = match.group(1)
            body = _class_body(source, match.end())
            if not re.search(r"\bType\s*=\s*TargetType\s*\.\s*Editor\s*;", body):
                continue
            if not class_name.endswith("Target") or class_name == "Target":
                continue
            names.append(class_name[:-len("Target")])
    return list(dict.fromkeys(names))


def find_editor_target(project_file: str | os.PathLike[str]) -> str:
    project = resolve_uproject(project_file)
    target_info_names = _target_info_editor_names(
        project.parent / "Intermediate" / "TargetInfo.json"
    )
    if len(target_info_names) == 1:
        return target_info_names[0]
    if len(target_info_names) > 1:
        raise UnrealConfigurationError(
            "Multiple Editor targets found in Intermediate/TargetInfo.json: "
            + ", ".join(target_info_names)
        )

    source_names = _source_editor_names(project.parent / "Source")
    if len(source_names) == 1:
        return source_names[0]
    if len(source_names) > 1:
        raise UnrealConfigurationError(
            "Multiple Editor targets found in Source: " + ", ".join(source_names)
        )
    raise UnrealConfigurationError(
        "No Editor target was found in TargetInfo.json or Source/*.Target.cs."
    )


def inspect_unreal_project(
    selection: str | os.PathLike[str],
    *,
    env: dict[str, str] | None = None,
    registry_candidates: Iterable[str | os.PathLike[str]] | None = None,
    well_known_candidates: Iterable[str | os.PathLike[str]] | None = None,
) -> UnrealProjectContext:
    project = resolve_uproject(selection)
    association = read_engine_association(project)
    return UnrealProjectContext(
        project_file=project,
        engine_association=association,
        engine_root=find_unreal_engine_root(
            association,
            env=env,
            registry_candidates=registry_candidates,
            well_known_candidates=well_known_candidates,
        ),
        editor_target=find_editor_target(project),
    )


def build_unreal_editor_command(
    engine_root: str | os.PathLike[str],
    project_file: str | os.PathLike[str],
    editor_target: str,
    configuration: str = "Development",
) -> list[str]:
    root = validate_unreal_engine_root(engine_root)
    project = resolve_uproject(project_file)
    target = editor_target.strip()
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", target):
        raise UnrealConfigurationError(f"Invalid Unreal target name: {editor_target!r}")
    allowed_configurations = {"Debug", "DebugGame", "Development", "Shipping", "Test"}
    if configuration not in allowed_configurations:
        raise UnrealConfigurationError(f"Invalid Unreal build configuration: {configuration}")
    return [
        str(unreal_build_script(root)),
        target,
        "Win64",
        configuration,
        str(project),
        "-WaitMutex",
        "-NoHotReloadFromIDE",
    ]


def build_generate_project_files_command(
    engine_root: str | os.PathLike[str],
    project_file: str | os.PathLike[str],
) -> list[str]:
    root = validate_unreal_engine_root(engine_root)
    project = resolve_uproject(project_file)
    script = unreal_generate_project_files_script(root)
    if script.is_file():
        executable = script.resolve()
    else:
        executable = unreal_build_tool_executable(root)
        if not executable.is_file():
            raise UnrealConfigurationError(
                "Neither GenerateProjectFiles.bat nor UnrealBuildTool.exe was found "
                f"in Unreal Engine {root}."
            )
        executable = executable.resolve()
    installed_flag = (
        "-Rocket"
        if (root / "Engine" / "Build" / "InstalledBuild.txt").is_file()
        else "-Engine"
    )
    return [
        str(executable),
        "-ProjectFiles",
        f"-Project={project}",
        "-Game",
        installed_flag,
        "-Progress",
    ]


def run_unreal_build(
    command: Sequence[str],
    output_callback: Callable[[str], None] | None = None,
    *,
    popen_factory: Callable[..., Any] = subprocess.Popen,
) -> int:
    if not command:
        raise ValueError("Build command is empty.")
    process = popen_factory(
        list(command),
        cwd=str(pathlib.Path(command[0]).parent),
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        bufsize=1,
        creationflags=process_creation_flags(),
    )
    if process.stdout is not None:
        for line in process.stdout:
            if output_callback is not None:
                output_callback(line.rstrip("\r\n"))
    return_code = int(process.wait())
    if return_code != 0:
        raise subprocess.CalledProcessError(return_code, list(command))
    return return_code


UNREAL_MAINTENANCE_CACHE_DIRS = (
    ".vs",
    "Binaries",
    "Intermediate",
    "DerivedDataCache",
)
UNREAL_MAINTENANCE_BLOCKING_PROCESSES = frozenset(
    {
        "devenv.exe",
        "rider64.exe",
        "msbuild.exe",
        "code.exe",
        "clion64.exe",
        "livecodingconsole.exe",
        "shadercompileworker.exe",
        "unrealbuildtool.exe",
        "unrealeditor.exe",
        "unrealeditor-cmd.exe",
    }
)
UNREAL_BUILD_BLOCKING_PROCESSES = frozenset(
    {
        "livecodingconsole.exe",
        "unrealeditor.exe",
        "unrealeditor-cmd.exe",
    }
)
_UNREAL_MAINTENANCE_LOCK = threading.Lock()


def get_running_process_names(
    *,
    run: Callable[..., Any] = subprocess.run,
) -> set[str]:
    """Return executable names without requiring an optional process package."""

    if os.name != "nt":
        return set()
    completed = run(
        ["tasklist", "/FO", "CSV", "/NH"],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
        encoding="utf-8",
        errors="replace",
        creationflags=process_creation_flags(),
        check=False,
    )
    names: set[str] = set()
    for line in str(getattr(completed, "stdout", "") or "").splitlines():
        match = re.match(r'^"([^"]+)"', line.strip())
        if match:
            names.add(match.group(1).casefold())
    return names


def ensure_unreal_build_ready(
    *,
    running_process_names: Iterable[str] | None = None,
) -> None:
    """Reject a normal UBT build while Unreal or Live Coding can hold module DLLs."""

    process_names = {
        str(name).casefold()
        for name in (
            get_running_process_names()
            if running_process_names is None
            else running_process_names
        )
    }
    blocking = sorted(process_names & UNREAL_BUILD_BLOCKING_PROCESSES)
    if not blocking:
        return
    raise UnrealConfigurationError(
        "Unreal Editor 또는 Live Coding이 아직 실행 중입니다 "
        f"({', '.join(blocking)}). 에디터에서 저장한 뒤 완전히 종료하고 "
        "다시 '빌드 + 실행'을 눌러 주세요."
    )


def describe_unreal_build_failure(output_lines: Iterable[str]) -> str:
    """Return an actionable message for common process-lock build failures."""

    output = "\n".join(str(line) for line in output_lines).casefold()
    if "unable to build while live coding is active" in output:
        return (
            "Live Coding이 활성화된 Unreal Editor가 빌드를 막고 있습니다. "
            "에디터를 완전히 종료한 뒤 다시 '빌드 + 실행'을 눌러 주세요."
        )
    if (
        ("lnk1104" in output or "error opening file" in output)
        and "unrealeditor" in output
        and ".dll" in output
    ):
        return (
            "실행 중인 Unreal Editor가 플러그인 DLL을 사용하고 있어 링크할 수 없습니다. "
            "에디터를 완전히 종료한 뒤 다시 '빌드 + 실행'을 눌러 주세요."
        )
    if "turbolink" in output and "c1083" in output and "grpcpp" in output:
        return (
            "TurboLink 플러그인의 gRPC 서드파티 라이브러리가 설치되어 있지 않습니다. "
            "프로젝트의 Scripts\\SetupTurboLink.ps1 을 한 번 실행한 뒤 "
            "다시 '빌드 + 실행'을 눌러 주세요."
        )
    if "c1083" in output and "no such file or directory" in output:
        return (
            "빌드에 필요한 헤더 파일을 찾을 수 없습니다. 플러그인의 서드파티 "
            "라이브러리 설치가 누락되었을 수 있어요. 프로젝트 셋업 스크립트를 "
            "실행한 뒤 다시 '빌드 + 실행'을 눌러 주세요."
        )
    return ""


def _is_reparse_point(path: pathlib.Path) -> bool:
    try:
        attributes = int(getattr(path.lstat(), "st_file_attributes", 0))
    except OSError:
        return False
    reparse_flag = int(getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400))
    return bool(attributes & reparse_flag)


def _validated_cache_directory(project_root: pathlib.Path, name: str) -> pathlib.Path:
    if name not in UNREAL_MAINTENANCE_CACHE_DIRS:
        raise UnrealConfigurationError(f"Unsupported Unreal cache directory: {name}")
    candidate = project_root / name
    if not candidate.exists():
        return candidate
    if not candidate.is_dir():
        raise UnrealConfigurationError(f"Expected a cache directory: {candidate}")
    if candidate.is_symlink() or _is_reparse_point(candidate):
        raise UnrealConfigurationError(
            f"Refusing to remove linked or reparse-point cache directory: {candidate}"
        )
    resolved = candidate.resolve(strict=True)
    if resolved.parent != project_root:
        raise UnrealConfigurationError(
            f"Cache directory resolves outside the project root: {candidate}"
        )
    return candidate


def clean_and_regenerate_unreal_project(
    selection: str | os.PathLike[str],
    progress_callback: Callable[[str], None] | None = None,
    *,
    running_process_names: Iterable[str] | None = None,
    remove_tree: Callable[[str | os.PathLike[str]], Any] = shutil.rmtree,
    popen_factory: Callable[..., Any] = subprocess.Popen,
) -> list[pathlib.Path]:
    """Remove only known generated folders, then regenerate IDE project files."""

    def progress(message: str) -> None:
        if progress_callback is not None:
            progress_callback(message)

    with _UNREAL_MAINTENANCE_LOCK:
        project_file = resolve_uproject(selection)
        engine_association = read_engine_association(project_file)
        engine_root = find_unreal_engine_root(engine_association)
        project_root = project_file.parent.resolve(strict=True)
        process_names = {
            str(name).casefold()
            for name in (
                get_running_process_names()
                if running_process_names is None
                else running_process_names
            )
        }
        blocking = sorted(process_names & UNREAL_MAINTENANCE_BLOCKING_PROCESSES)
        if blocking:
            raise UnrealConfigurationError(
                "Close Unreal Editor and IDE/build processes first: " + ", ".join(blocking)
            )

        targets = [
            _validated_cache_directory(project_root, name)
            for name in UNREAL_MAINTENANCE_CACHE_DIRS
        ]
        removed: list[pathlib.Path] = []
        for target in targets:
            if not target.exists():
                progress(f"건너뜀 · {target.name} 없음")
                continue
            progress(f"정리 중 · {target.name}")
            remove_tree(target)
            if target.exists():
                raise OSError(f"Cache directory still exists after cleanup: {target}")
            removed.append(target)
            progress(f"정리 완료 · {target.name}")

        command = build_generate_project_files_command(
            engine_root,
            project_file,
        )
        progress("프로젝트 파일 재생성 중")
        run_unreal_build(command, output_callback=progress_callback, popen_factory=popen_factory)
        progress("프로젝트 파일 재생성 완료")
        return removed


def launch_unreal_editor(
    engine_root: str | os.PathLike[str],
    project_file: str | os.PathLike[str],
    extra_arguments: Sequence[str] = (),
    *,
    env: dict[str, str] | None = None,
    popen_factory: Callable[..., Any] = subprocess.Popen,
) -> Any:
    root = validate_unreal_engine_root(engine_root)
    project = resolve_uproject(project_file)
    command = [str(unreal_editor_executable(root)), str(project), *extra_arguments]
    return popen_factory(
        command,
        cwd=str(project.parent),
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        creationflags=process_creation_flags(),
        env=dict(os.environ if env is None else env),
    )


GODOT_RELEASE_API_URL = "https://api.github.com/repos/godotengine/godot/releases/latest"


def godot_windows_asset_from_release(payload: Any) -> tuple[str, str]:
    """(file name, download URL) of the Windows editor zip in a GitHub release."""

    assets = payload.get("assets") if isinstance(payload, dict) else None
    if not isinstance(assets, list):
        raise GodotConfigurationError("Godot 릴리스 정보를 읽지 못했습니다.")
    for asset in assets:
        if not isinstance(asset, dict):
            continue
        name = str(asset.get("name") or "")
        lowered = name.casefold()
        # The mono build needs the .NET SDK; the plain build is the safer default.
        if "mono" in lowered:
            continue
        if lowered.startswith("godot_v") and lowered.endswith("_win64.exe.zip"):
            url = str(asset.get("browser_download_url") or "").strip()
            if url:
                return name, url
    raise GodotConfigurationError(
        "릴리스에서 Windows용 Godot 패키지를 찾지 못했습니다."
    )


def download_godot_editor(
    env: dict[str, str] | None = None,
    *,
    progress: Callable[[str], None] | None = None,
    opener: Callable[[str], Any] | None = None,
) -> pathlib.Path:
    """Download the latest stable Godot 4 Windows editor into the machine cache."""

    notify = progress or (lambda _line: None)
    open_url = opener or (lambda target: urllib.request.urlopen(target, timeout=60))
    try:
        with open_url(GODOT_RELEASE_API_URL) as response:
            payload = json.loads(response.read().decode("utf-8"))
    except (OSError, http.client.HTTPException, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise GodotConfigurationError(
            f"Godot 릴리스 정보를 가져오지 못했습니다 ({exc})."
        ) from exc
    name, url = godot_windows_asset_from_release(payload)
    install_root = global_cache_dir(env) / "godot"
    archive = install_root / name
    try:
        download_file(
            archive, url=url, progress=notify, opener=open_url, progress_label="Godot"
        )
    except (OSError, http.client.HTTPException) as exc:
        raise GodotConfigurationError(f"Godot을 내려받지 못했습니다 ({exc}).") from exc
    executable_name = name[: -len(".zip")]
    executable = install_root / executable_name
    try:
        with zipfile.ZipFile(archive) as package:
            member = next(
                (
                    item
                    for item in package.namelist()
                    if pathlib.PurePosixPath(item).name.casefold()
                    == executable_name.casefold()
                ),
                None,
            )
            if member is None:
                raise GodotConfigurationError(
                    "다운로드한 패키지에서 Godot 실행 파일을 찾지 못했습니다."
                )
            with package.open(member) as source, open(executable, "wb") as sink:
                shutil.copyfileobj(source, sink)
    except zipfile.BadZipFile as exc:
        raise GodotConfigurationError(
            f"다운로드한 Godot 패키지가 손상되었습니다: {exc}"
        ) from exc
    try:
        archive.unlink()
    except OSError:
        pass
    return executable


def _looks_like_godot_executable(path: pathlib.Path) -> bool:
    name = path.name.casefold()
    return (
        name in {"godot", "godot.exe", "godot4", "godot4.exe"}
        or name.startswith("godot_")
        or name.startswith("godot-")
        or name.startswith("godot.windows.editor")
    )


def _godot_metadata_executable(project_file: pathlib.Path) -> pathlib.Path | None:
    metadata = project_file.parent / ".godot" / "editor" / "project_metadata.cfg"
    try:
        raw = metadata.read_text(encoding="utf-8-sig")
    except (OSError, UnicodeError):
        return None
    match = re.search(r'^\s*executable_path\s*=\s*("(?:[^"\\]|\\.)*")\s*$', raw, re.MULTILINE)
    if match is None:
        return None
    try:
        value = json.loads(match.group(1))
    except (TypeError, ValueError, json.JSONDecodeError):
        return None
    if not isinstance(value, str) or not value.strip():
        return None
    try:
        candidate = pathlib.Path(value).expanduser().resolve()
    except (OSError, ValueError):
        return None
    return candidate if _looks_like_godot_executable(candidate) else None


def _default_godot_search_roots(
    project_file: pathlib.Path,
    env: Mapping[str, str],
) -> list[pathlib.Path]:
    project_dir = project_file.parent
    roots = [
        project_dir,
        project_dir / "Binaries",
        pathlib.Path(sys.executable).resolve().parent,
    ]
    local_appdata = str(env.get("LOCALAPPDATA") or "").strip()
    if local_appdata:
        local = pathlib.Path(local_appdata).expanduser()
        roots.extend((local / "Programs" / "Godot", local / "Godot"))
    for name in ("ProgramFiles", "ProgramFiles(x86)"):
        value = str(env.get(name) or "").strip()
        if not value:
            continue
        base = pathlib.Path(value).expanduser()
        roots.extend(
            (
                base / "Godot",
                base / "Steam" / "steamapps" / "common" / "Godot Engine",
            )
        )
    user_profile = str(env.get("USERPROFILE") or "").strip()
    if user_profile:
        home = pathlib.Path(user_profile).expanduser()
        roots.extend((home / "Downloads", home / "Desktop"))
    return roots


def discover_godot_executables(
    project: str | os.PathLike[str],
    env: Mapping[str, str] | None = None,
    *,
    configured: str | os.PathLike[str] | None = None,
    search_roots: Sequence[str | os.PathLike[str]] | None = None,
) -> list[pathlib.Path]:
    """Return usable Godot editors in deterministic preference order."""

    project_file = resolve_godot_project(project)
    values = os.environ if env is None else env
    candidates: list[pathlib.Path] = []

    def add(value: str | os.PathLike[str] | None, *, trusted: bool = False) -> None:
        if not value:
            return
        try:
            candidate = pathlib.Path(value).expanduser().resolve()
        except (OSError, TypeError, ValueError):
            return
        if not candidate.is_file() or (not trusted and not _looks_like_godot_executable(candidate)):
            return
        normalized = os.path.normcase(str(candidate))
        if all(os.path.normcase(str(existing)) != normalized for existing in candidates):
            candidates.append(candidate)

    add(configured, trusted=True)
    for name in GODOT_EXECUTABLE_ENV_NAMES:
        add(values.get(name), trusted=True)
    add(_godot_metadata_executable(project_file))

    path_value = values.get("PATH")
    for name in ("godot4", "godot", "godot4.exe", "godot.exe"):
        add(shutil.which(name, path=path_value))

    roots = (
        [pathlib.Path(root).expanduser() for root in search_roots]
        if search_roots is not None
        else _default_godot_search_roots(project_file, values)
    )
    patterns = ("Godot*.exe", "godot*.exe") if os.name == "nt" else ("Godot*", "godot*")
    for root in roots:
        try:
            if root.is_file():
                add(root)
                continue
            matches = [
                match
                for pattern in patterns
                for match in root.glob(pattern)
                if match.is_file()
            ]
        except OSError:
            continue
        for match in sorted(
            matches,
            key=lambda item: ("console" in item.name.casefold(), item.name.casefold()),
        ):
            add(match)
    return candidates


def resolve_godot_executable(
    project: str | os.PathLike[str],
    env: Mapping[str, str] | None = None,
    *,
    configured: str | os.PathLike[str] | None = None,
    search_roots: Sequence[str | os.PathLike[str]] | None = None,
) -> pathlib.Path:
    executables = discover_godot_executables(
        project,
        env,
        configured=configured,
        search_roots=search_roots,
    )
    if executables:
        return executables[0]
    raise GodotConfigurationError(
        "Godot 실행 파일을 찾지 못했습니다. Godot 4.x 실행 파일을 선택하거나 "
        "REVVY_GODOT_EXECUTABLE 환경 변수를 설정해 주세요."
    )


def launch_godot_editor(
    project: str | os.PathLike[str],
    *,
    executable: str | os.PathLike[str] | None = None,
    run_project: bool = False,
    extra_arguments: Sequence[str] = (),
    env: Mapping[str, str] | None = None,
    popen_factory: Callable[..., Any] = subprocess.Popen,
) -> Any:
    """Open a Godot editor, or run the project when ``run_project`` is true."""

    project_file = resolve_godot_project(project)
    project_root = project_file.parent.resolve()
    values = dict(os.environ if env is None else env)
    configured_project = str(values.get(PROJECT_DIR_ENV) or "").strip()
    if not configured_project or not same_project_path(configured_project, project_root):
        state_root = project_state_dir(project_root)
        values[PROJECT_DIR_ENV] = str(project_root)
        values[STATE_DIR_ENV] = str(state_root)
        values["REVVY_CONFIG"] = str(state_root / "revvy-proxy.json")
        values["REVVY_MCP_UPSTREAMS_CONFIG"] = str(
            state_root / "mcp-upstreams.json"
        )
        values["REVVY_PROXY_LOG"] = str(state_root / "proxy.log")
    config_path = pathlib.Path(
        values.get("REVVY_CONFIG")
        or project_state_dir(project_root) / "revvy-proxy.json"
    )
    if not str(values.get("REVVY_EDITOR_PORT") or "").strip():
        try:
            runtime_config = json.loads(config_path.read_text(encoding="utf-8-sig"))
            editor_port = int(runtime_config.get("ue_port", 0))
            if 1 <= editor_port <= 65535:
                values["REVVY_EDITOR_PORT"] = str(editor_port)
        except (OSError, TypeError, ValueError, json.JSONDecodeError):
            pass
    editor = resolve_godot_executable(
        project_file,
        values,
        configured=executable,
    )
    command = [str(editor)]
    if not run_project:
        command.append("--editor")
    command.extend(("--path", str(project_file.parent), *extra_arguments))
    return popen_factory(
        command,
        cwd=str(project_file.parent),
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        creationflags=process_creation_flags(),
        env=values,
    )


def codex_home(env: dict[str, str] | None = None) -> pathlib.Path:
    values = os.environ if env is None else env
    return pathlib.Path(values.get("CODEX_HOME") or pathlib.Path.home() / ".codex")


def _configured_codex_model(home: pathlib.Path) -> str:
    path = home / "config.toml"
    if not path.exists():
        return ""
    try:
        raw = path.read_bytes()
        if tomllib is not None:
            value = tomllib.loads(raw.decode("utf-8-sig")).get("model", "")
            return str(value).strip()
        match = re.search(r'^\s*model\s*=\s*["\']([^"\']+)["\']', raw.decode("utf-8-sig"), re.MULTILINE)
        return match.group(1).strip() if match else ""
    except (OSError, UnicodeError, ValueError):
        return ""


def discover_codex_models(
    env: dict[str, str] | None = None,
) -> tuple[list[str], str]:
    """Read models advertised to the user's current Codex login.

    Codex refreshes ``models_cache.json`` after login. Reading it avoids guessing
    private or staged model slugs and keeps preview variants account-specific.
    """

    home = codex_home(env)
    configured = _configured_codex_model(home)
    discovered: list[tuple[int, str]] = []
    try:
        payload = json.loads((home / "models_cache.json").read_text(encoding="utf-8-sig"))
        models = payload.get("models", []) if isinstance(payload, dict) else []
        for item in models:
            if not isinstance(item, dict) or item.get("visibility") == "hide":
                continue
            slug = str(item.get("slug") or item.get("id") or "").strip()
            if not slug:
                continue
            try:
                priority = int(item.get("priority", 10_000))
            except (TypeError, ValueError):
                priority = 10_000
            discovered.append((priority, slug))
    except (OSError, UnicodeError, json.JSONDecodeError):
        pass

    models: list[str] = []
    for _, slug in sorted(discovered, key=lambda value: (value[0], value[1])):
        if slug not in models:
            models.append(slug)
    if configured and configured not in models:
        models.insert(0, configured)
    if not models:
        models.append(configured or "default")
    default = configured if configured in models else models[0]
    return models, default


def kimi_home(env: dict[str, str] | None = None) -> pathlib.Path:
    values = os.environ if env is None else env
    user = pathlib.Path(values.get("USERPROFILE") or pathlib.Path.home())
    return pathlib.Path(values.get("KIMI_CODE_HOME") or user / ".kimi-code")


def kimi_session_ids_for_project(
    project_dir: str | os.PathLike[str],
    env: dict[str, str] | None = None,
) -> list[str]:
    """Return Kimi session IDs recorded for a project, oldest first."""

    target = os.path.normcase(
        os.path.abspath(os.path.expanduser(os.fspath(project_dir)))
    )
    index_path = kimi_home(env) / "session_index.jsonl"
    session_ids: list[str] = []
    try:
        lines = index_path.read_text(encoding="utf-8-sig").splitlines()
    except (OSError, UnicodeError):
        return session_ids
    for line in lines:
        try:
            payload = json.loads(line)
        except json.JSONDecodeError:
            continue
        if not isinstance(payload, dict):
            continue
        work_dir = str(payload.get("workDir") or payload.get("work_dir") or "").strip()
        session_id = str(
            payload.get("sessionId") or payload.get("session_id") or ""
        ).strip()
        if not work_dir or not session_id:
            continue
        normalized = os.path.normcase(
            os.path.abspath(os.path.expanduser(work_dir))
        )
        if normalized == target and session_id not in session_ids:
            session_ids.append(session_id)
    return session_ids


def latest_kimi_session_id(
    project_dir: str | os.PathLike[str],
    exclude: Iterable[str] = (),
    env: dict[str, str] | None = None,
) -> str:
    excluded = {str(value).strip() for value in exclude if str(value).strip()}
    for session_id in reversed(kimi_session_ids_for_project(project_dir, env)):
        if session_id not in excluded:
            return session_id
    return ""


def discover_kimi_models(
    env: dict[str, str] | None = None,
) -> tuple[list[str], str]:
    """Read model aliases and the default from Kimi Code's config.toml."""

    path = kimi_home(env) / "config.toml"
    default = ""
    models: list[str] = []
    try:
        raw = path.read_bytes().decode("utf-8-sig")
        if tomllib is not None:
            payload = tomllib.loads(raw)
            default = str(payload.get("default_model", "")).strip()
            configured_models = payload.get("models", {})
            if isinstance(configured_models, dict):
                models.extend(
                    str(alias).strip()
                    for alias in configured_models
                    if str(alias).strip()
                )
        else:
            default_match = re.search(
                r'^\s*default_model\s*=\s*["\']([^"\']+)["\']',
                raw,
                re.MULTILINE,
            )
            default = default_match.group(1).strip() if default_match else ""
            models.extend(
                match.group(1).strip()
                for match in re.finditer(
                    r'^\s*\[models\.["\']([^"\']+)["\']\]\s*$',
                    raw,
                    re.MULTILINE,
                )
            )
    except (OSError, UnicodeError, ValueError):
        pass

    unique: list[str] = []
    for model in models:
        if model and model not in unique:
            unique.append(model)
    if default:
        if default in unique:
            unique.remove(default)
        unique.insert(0, default)
    if not unique:
        unique.extend(KIMI_MODELS)
    return unique, default if default in unique else unique[0]


CODEX_REASONING_EFFORTS = frozenset(
    ("minimal", "low", "medium", "high", "xhigh", "max", "ultra")
)
CLAUDE_REASONING_EFFORTS = frozenset(
    ("low", "medium", "high", "xhigh", "max")
)


def configured_codex_reasoning_effort(
    env: dict[str, str] | None = None,
) -> str:
    path = codex_home(env) / "config.toml"
    try:
        raw = path.read_bytes().decode("utf-8-sig")
        if tomllib is not None:
            value = str(tomllib.loads(raw).get("model_reasoning_effort", "")).strip().lower()
        else:
            match = re.search(
                r'^\s*model_reasoning_effort\s*=\s*["\']([^"\']+)["\']',
                raw,
                re.MULTILINE,
            )
            value = match.group(1).strip().lower() if match else ""
    except (OSError, UnicodeError, ValueError):
        return ""
    return value if value in CODEX_REASONING_EFFORTS else ""


def configured_claude_reasoning_effort(
    env: dict[str, str] | None = None,
) -> str:
    values = os.environ if env is None else env
    user = pathlib.Path(values.get("USERPROFILE") or pathlib.Path.home())
    home = pathlib.Path(values.get("CLAUDE_CONFIG_DIR") or user / ".claude")
    try:
        payload = json.loads((home / "settings.json").read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeError, json.JSONDecodeError):
        return ""
    value = str(payload.get("effortLevel", "")).strip().lower() if isinstance(payload, dict) else ""
    return value if value in CLAUDE_REASONING_EFFORTS else ""


def discover_codex_reasoning_levels(
    model: str,
    env: dict[str, str] | None = None,
) -> tuple[list[str], str]:
    """Return reasoning metadata advertised for one visible Codex model."""

    requested = model.strip()
    if not requested or requested == "default":
        return [], ""
    try:
        payload = json.loads(
            (codex_home(env) / "models_cache.json").read_text(encoding="utf-8-sig")
        )
    except (OSError, UnicodeError, json.JSONDecodeError):
        return [], ""
    models = payload.get("models", []) if isinstance(payload, dict) else []
    if not isinstance(models, list):
        return [], ""
    for item in models:
        if not isinstance(item, dict) or item.get("visibility") == "hide":
            continue
        slug = str(item.get("slug") or item.get("id") or "").strip()
        if slug != requested:
            continue
        advertised = item.get("supported_reasoning_levels", [])
        levels: list[str] = []
        if isinstance(advertised, list):
            for entry in advertised:
                if not isinstance(entry, dict):
                    continue
                effort = str(entry.get("effort") or "").strip().lower()
                if effort in CODEX_REASONING_EFFORTS and effort not in levels:
                    levels.append(effort)
        default = str(item.get("default_reasoning_level") or "").strip().lower()
        if default not in CODEX_REASONING_EFFORTS:
            default = ""
        return levels, default
    return [], ""


def resolve_executable(name: str, env: dict[str, str] | None = None) -> str:
    values = os.environ if env is None else env
    executable = shutil.which(name, path=values.get("PATH"))
    if executable:
        return executable
    user = pathlib.Path(values.get("USERPROFILE") or pathlib.Path.home())
    appdata = pathlib.Path(values.get("APPDATA") or user / "AppData" / "Roaming")
    candidates = [
        user / ".local" / "bin" / f"{name}.exe",
        appdata / "npm" / f"{name}.cmd",
    ]
    if name.casefold() == "kimi":
        candidates[:0] = [
            kimi_home(values) / "bin" / "kimi.exe",
            user / ".kimi-code" / "bin" / "kimi.exe",
        ]
    for candidate in candidates:
        if candidate.is_file():
            return str(candidate)
    return name


def toml_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)


def _attachment_path_strings(
    image_paths: Iterable[str | os.PathLike[str]],
) -> list[str]:
    return [str(pathlib.Path(value).expanduser().resolve()) for value in image_paths]


def _validated_reasoning_effort(value: str, allowed: frozenset[str], provider: str) -> str:
    effort = value.strip().lower()
    if effort and effort not in allowed:
        choices = ", ".join(sorted(allowed))
        raise ValueError(f"Unsupported {provider} reasoning effort {value!r}; expected one of: {choices}")
    return effort


def _validated_permission_mode(value: str) -> str:
    mode = value.strip().lower()
    if mode not in PERMISSION_MODES:
        choices = ", ".join(PERMISSION_MODES)
        raise ValueError(f"Unsupported permission mode {value!r}; expected one of: {choices}")
    return mode


def _mcp_url_for_permission(mcp_url: str, permission_mode: str) -> str:
    if permission_mode != PERMISSION_SAFE:
        return mcp_url
    normalized = mcp_url.rstrip("/")
    if normalized.endswith("/mcp/safe"):
        return normalized
    if normalized.endswith("/mcp"):
        return normalized + "/safe"
    return normalized + "/mcp/safe"


def provider_subprocess_environment(
    permission_mode: str,
    full_access_token: str = "",
    environ: Mapping[str, str] | None = None,
) -> dict[str, str]:
    mode = _validated_permission_mode(permission_mode)
    environment = dict(os.environ if environ is None else environ)
    for secret_name in (
        FULL_ACCESS_TOKEN_ENV,
        BRIDGE_AUTH_FILE_ENV,
        SECURITY_DIR_ENV,
        UE_CAPABILITY_FILE_ENV,
        "REVVY_BLENDER_BRIDGE_CONFIG",
    ):
        environment.pop(secret_name, None)
    if mode == PERMISSION_FULL:
        if len(full_access_token) < 32:
            raise ValueError("Revvy Full access token is missing or invalid")
        environment[FULL_ACCESS_TOKEN_ENV] = full_access_token
    return environment


def kimi_subprocess_environment(
    environment: Mapping[str, str],
) -> dict[str, str]:
    """Bound a single Kimi subagent without changing the user's global config."""

    result = dict(environment)
    result[KIMI_SUBAGENT_TIMEOUT_ENV] = str(KIMI_SUBAGENT_TIMEOUT_MS)
    return result


def build_codex_command(
    executable: str,
    model: str,
    project_dir: str,
    mcp_url: str,
    image_paths: Sequence[str | os.PathLike[str]] = (),
    reasoning_effort: str = "",
    permission_mode: str = PERMISSION_SAFE,
) -> list[str]:
    mode = _validated_permission_mode(permission_mode)
    mcp_url = _mcp_url_for_permission(mcp_url, mode)
    command = [executable, "exec"]
    if mode == PERMISSION_SAFE:
        command.append("--ignore-user-config")
    if model and model != "default":
        command.extend(("--model", model))
    effort = _validated_reasoning_effort(
        reasoning_effort, CODEX_REASONING_EFFORTS, "Codex"
    )
    if effort:
        command.extend(("--config", f"model_reasoning_effort={toml_string(effort)}"))
    sandbox_mode = "danger-full-access" if mode == PERMISSION_FULL else "read-only"
    command.extend(
        (
            "--sandbox",
            sandbox_mode,
            "--cd",
            project_dir,
            "--skip-git-repo-check",
            "--config",
            'approval_policy="never"',
            "--config",
            f"sandbox_mode={toml_string(sandbox_mode)}",
            "--config",
            f"mcp_servers.Revvy.url={toml_string(mcp_url)}",
            "--config",
            "mcp_servers.Revvy.required=true",
            "--config",
            'mcp_servers.Revvy.default_tools_approval_mode="approve"',
        )
    )
    if mode == PERMISSION_FULL:
        command.extend(
            (
                "--config",
                (
                    "mcp_servers.Revvy.bearer_token_env_var="
                    f"{toml_string(FULL_ACCESS_TOKEN_ENV)}"
                ),
            )
        )
    for path in _attachment_path_strings(image_paths):
        command.extend(("--image", path))
    command.extend(("--json", "-"))
    return command


def build_codex_resume_command(
    executable: str,
    model: str,
    session_id: str,
    mcp_url: str,
    image_paths: Sequence[str | os.PathLike[str]] = (),
    reasoning_effort: str = "",
    permission_mode: str = PERMISSION_SAFE,
) -> list[str]:
    mode = _validated_permission_mode(permission_mode)
    mcp_url = _mcp_url_for_permission(mcp_url, mode)
    command = [executable, "exec", "resume"]
    if mode == PERMISSION_SAFE:
        command.append("--ignore-user-config")
    if model and model != "default":
        command.extend(("--model", model))
    effort = _validated_reasoning_effort(
        reasoning_effort, CODEX_REASONING_EFFORTS, "Codex"
    )
    if effort:
        command.extend(("--config", f"model_reasoning_effort={toml_string(effort)}"))
    sandbox_mode = "danger-full-access" if mode == PERMISSION_FULL else "read-only"
    command.extend(
        (
            "--skip-git-repo-check",
            "--config",
            'approval_policy="never"',
            "--config",
            f"sandbox_mode={toml_string(sandbox_mode)}",
            "--config",
            f"mcp_servers.Revvy.url={toml_string(mcp_url)}",
            "--config",
            "mcp_servers.Revvy.required=true",
            "--config",
            'mcp_servers.Revvy.default_tools_approval_mode="approve"',
        )
    )
    if mode == PERMISSION_FULL:
        command.extend(
            (
                "--config",
                (
                    "mcp_servers.Revvy.bearer_token_env_var="
                    f"{toml_string(FULL_ACCESS_TOKEN_ENV)}"
                ),
            )
        )
    for path in _attachment_path_strings(image_paths):
        command.extend(("--image", path))
    command.extend(("--json", session_id, "-"))
    return command


def build_claude_command(
    executable: str,
    model: str,
    project_dir: str,
    mcp_url: str,
    session_id: str = "",
    image_paths: Sequence[str | os.PathLike[str]] = (),
    reasoning_effort: str = "",
    permission_mode: str = PERMISSION_SAFE,
) -> list[str]:
    del project_dir  # cwd supplies the project root.
    mode = _validated_permission_mode(permission_mode)
    mcp_url = _mcp_url_for_permission(mcp_url, mode)
    revvy_server = {"type": "http", "url": mcp_url}
    if mode == PERMISSION_FULL:
        revvy_server["headers"] = {
            "Authorization": f"Bearer ${{{FULL_ACCESS_TOKEN_ENV}}}"
        }
    mcp_config = json.dumps(
        {"mcpServers": {"Revvy": revvy_server}},
        separators=(",", ":"),
    )
    command = [
        executable,
        "-p",
        "--output-format",
        "stream-json",
        "--include-partial-messages",
        "--verbose",
        "--mcp-config",
        mcp_config,
    ]
    if mode == PERMISSION_FULL:
        command.append("--dangerously-skip-permissions")
    else:
        command.extend(
            (
                "--strict-mcp-config",
                "--allowedTools",
                "mcp__Revvy,Read,Grep,Glob",
                "--disallowedTools",
                "AskUserQuestion",
            )
        )
    if model and model != "default":
        command.extend(("--model", model))
    effort = _validated_reasoning_effort(
        reasoning_effort, CLAUDE_REASONING_EFFORTS, "Claude"
    )
    if effort:
        command.extend(("--effort", effort))
    added_directories: set[str] = set()
    for path in _attachment_path_strings(image_paths):
        parent = str(pathlib.Path(path).parent)
        key = os.path.normcase(parent)
        if key in added_directories:
            continue
        added_directories.add(key)
        command.extend(("--add-dir", parent))
    if session_id:
        command.extend(("--resume", session_id))
    return command


def build_kimi_command(
    executable: str,
    model: str,
    project_dir: str,
    session_id: str = "",
    image_paths: Sequence[str | os.PathLike[str]] = (),
    permission_mode: str = PERMISSION_SAFE,
) -> list[str]:
    """Build a non-interactive Kimi Code stream command.

    Kimi reads project MCP configuration from ``.kimi-code/mcp.json``. Prompt
    mode does not accept Kimi's interactive permission flags, so the CLI's own
    configured permission policy remains authoritative.
    """

    del project_dir  # cwd supplies the project root.
    _validated_permission_mode(permission_mode)
    command = [executable, "--output-format", "stream-json"]
    if model and model != "default":
        command.extend(("--model", model))
    if session_id:
        command.extend(("--session", session_id))
    added_directories: set[str] = set()
    for path in _attachment_path_strings(image_paths):
        parent = str(pathlib.Path(path).parent)
        key = os.path.normcase(parent)
        if key in added_directories:
            continue
        added_directories.add(key)
        command.extend(("--add-dir", parent))
    # The caller appends the prompt as the final argv value after constructing it.
    command.append("--prompt")
    return command


def ensure_kimi_mcp_config(
    project_dir: str | os.PathLike[str],
    mcp_url: str,
    permission_mode: str = PERMISSION_SAFE,
) -> pathlib.Path:
    """Merge Revvy into Kimi Code's project-local MCP configuration."""

    project = pathlib.Path(project_dir).expanduser().resolve()
    target = project / ".kimi-code" / "mcp.json"
    payload: dict[str, Any] = {}
    if target.exists():
        try:
            loaded = json.loads(target.read_text(encoding="utf-8-sig"))
        except (OSError, UnicodeError, json.JSONDecodeError) as exc:
            raise ValueError(f"Could not parse {target}: {exc}") from exc
        if not isinstance(loaded, dict):
            raise ValueError(f"Expected a JSON object in {target}")
        payload = loaded
    servers = payload.setdefault("mcpServers", {})
    if not isinstance(servers, dict):
        raise ValueError(f"mcpServers must be an object in {target}")
    mode = _validated_permission_mode(permission_mode)
    revvy_server = {
        "url": _mcp_url_for_permission(mcp_url, mode),
    }
    if mode == PERMISSION_FULL:
        revvy_server["bearerTokenEnvVar"] = FULL_ACCESS_TOKEN_ENV
    servers["Revvy"] = revvy_server
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_suffix(target.suffix + ".tmp")
    temporary.write_text(
        json.dumps(payload, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    os.replace(temporary, target)
    return target


def session_key(
    provider: str,
    model: str,
    project_dir: str,
    permission_mode: str = PERMISSION_SAFE,
) -> str:
    mode = _validated_permission_mode(permission_mode)
    normalized = os.path.normcase(os.path.abspath(project_dir))
    return f"{provider}|{model}|{normalized}|{mode}|{CHAT_PROMPT_VERSION}"


def build_turn_prompt(
    user_message: str,
    image_paths: Sequence[str | os.PathLike[str]] = (),
    *,
    provider: str = "",
) -> str:
    """Describe only this turn's staged images alongside the user's message."""

    message = user_message.strip()
    paths = _attachment_path_strings(image_paths)
    if not paths:
        return message
    attachment_block = ["Images attached to this turn:"]
    attachment_block.extend(f"- {path}" for path in paths)
    if provider == PROVIDER_KIMI:
        attachment_block.append(
            "Use the ReadMediaFile tool to open and inspect every attached image before answering."
        )
    elif provider == PROVIDER_CLAUDE:
        attachment_block.append(
            "Use the Read tool to open and inspect every attached image before answering."
        )
    return "\n\n".join((message, "\n".join(attachment_block))).strip()


def limit_kimi_prompt(
    prompt: str,
    max_chars: int = KIMI_PROMPT_MAX_CHARS,
) -> str:
    """Keep Kimi's argv prompt below Windows' process command-line limit."""

    if max_chars <= len(KIMI_PROMPT_TRUNCATION_NOTICE):
        raise ValueError("Kimi prompt limit is too small")
    if len(prompt) <= max_chars:
        return prompt
    remaining = max_chars - len(KIMI_PROMPT_TRUNCATION_NOTICE)
    return KIMI_PROMPT_TRUNCATION_NOTICE + prompt[-remaining:]


def build_chat_prompt(
    user_message: str,
    history: Iterable[tuple[str, str]] = (),
    *,
    max_history_chars: int = 24_000,
    image_paths: Sequence[str | os.PathLike[str]] = (),
    provider: str = "",
) -> str:
    message = build_turn_prompt(user_message, image_paths, provider=provider)
    transcript: list[str] = []
    used = 0
    for role, content in reversed(list(history)):
        clean = str(content).strip()
        if not clean:
            continue
        block = f"[{role}]\n{clean}"
        if used + len(block) > max_history_chars:
            break
        transcript.append(block)
        used += len(block)
    transcript.reverse()
    history_text = "\n\n".join(transcript)
    parts = [SYSTEM_INSTRUCTION]
    if history_text:
        parts.extend(("Conversation so far:", history_text))
    parts.extend(("Current user request:", message))
    return "\n\n".join(parts)


@dataclass
class StreamUpdate:
    text: str = ""
    text_kind: str = ""
    log: str = ""
    error: str = ""
    thread_id: str = ""
    activity_id: str = ""
    activity: str = ""
    activity_state: str = ""
    done: bool = False
    # The tool call itself, when it is one that edits files. Carried so the app
    # can show what changed while the turn is still running - by the time an
    # edit reaches the working tree the moment to point at it has passed.
    tool_name: str = ""
    tool_input: dict[str, Any] = field(default_factory=dict)


def _message_text(value: Any) -> str:
    if isinstance(value, str):
        return value
    if isinstance(value, dict):
        return str(value.get("message") or value.get("text") or value.get("error") or "")
    return ""


def _short_activity(value: Any, limit: int = 160) -> str:
    text = " ".join(str(value or "").split())
    if len(text) <= limit:
        return text
    return text[: max(1, limit - 1)].rstrip() + "…"


_FRIENDLY_TOOL_STATUS_FALLBACK = {
    "bash": ("🐾", "컴퓨터에 명령을 내리는 중이에요"),
    "bashoutput": ("🐾", "실행 결과를 지켜보는 중이에요"),
    "killshell": ("🐾", "돌고 있던 작업을 멈추는 중이에요"),
    "killbash": ("🐾", "돌고 있던 작업을 멈추는 중이에요"),
    "read": ("📖", "파일을 읽어보는 중이에요"),
    "write": ("✏️", "새 파일을 쓰는 중이에요"),
    "edit": ("✏️", "코드를 고치는 중이에요"),
    "multiedit": ("✏️", "코드를 여러 군데 고치는 중이에요"),
    "notebookedit": ("📓", "노트북을 손보는 중이에요"),
    "grep": ("🔍", "코드 속에서 찾아보는 중이에요"),
    "glob": ("🗂️", "파일이 어디 있나 둘러보는 중이에요"),
    "task": ("🤝", "도우미한테 일을 맡기는 중이에요"),
    "todowrite": ("📝", "할 일 목록을 정리하는 중이에요"),
    "websearch": ("🔎", "인터넷을 검색하는 중이에요"),
    "webfetch": ("🌐", "웹페이지를 가져오는 중이에요"),
    "exitplanmode": ("📋", "세운 계획을 보여드릴게요"),
    "slashcommand": ("⚡", "명령을 실행하는 중이에요"),
    "execute_python_code": ("🐍", "에디터에서 파이썬을 돌리는 중이에요"),
    "discover_python_class": ("🔬", "언리얼 기능을 살펴보는 중이에요"),
    "manage_skills": ("📚", "스킬 책을 펼쳐보는 중이에요"),
    "manage_asset": ("🎨", "에셋을 만지작거리는 중이에요"),
    "read_logs": ("📋", "로그를 들여다보는 중이에요"),
    "memory": ("🧠", "기억을 더듬어보는 중이에요"),
}


def _voice_tool_statuses() -> dict[str, tuple[str, str]]:
    statuses: dict[str, tuple[str, str]] = {}
    raw_statuses = REVVY_VOICE.get("tool_statuses", {})
    if isinstance(raw_statuses, dict):
        for name, value in raw_statuses.items():
            if not isinstance(value, dict):
                continue
            icon = str(value.get("icon") or "").strip()
            text = str(value.get("text") or "").strip()
            if icon and text:
                statuses[str(name).casefold()] = (icon, text)
    return statuses or dict(_FRIENDLY_TOOL_STATUS_FALLBACK)


_FRIENDLY_TOOL_STATUSES = _voice_tool_statuses()


def revvy_thinking_status() -> str:
    statuses = REVVY_VOICE.get("thinking_statuses", [])
    choices = [
        str(value).strip()
        for value in statuses
        if isinstance(value, str) and str(value).strip()
    ]
    if not choices:
        choices = list(_VOICE_FALLBACK["thinking_statuses"])
    return random.choice(choices)


def revvy_turn_status(name: str, *, count: int = 0) -> str:
    statuses = REVVY_VOICE.get("turn_statuses", {})
    fallback = _VOICE_FALLBACK["turn_statuses"]
    template = ""
    if isinstance(statuses, dict):
        template = str(statuses.get(name) or "")
    if not template:
        template = str(fallback.get(name) or "")
    try:
        return template.format(count=max(0, int(count)))
    except (KeyError, ValueError):
        return template


def _bare_tool_name(value: str) -> str:
    name = value.strip()
    if "__" in name:
        name = name.rsplit("__", 1)[1]
    if "/" in name:
        name = name.rsplit("/", 1)[1]
    return name


def friendly_tool_activity(tool_name: str) -> str:
    bare = _bare_tool_name(tool_name)
    mapped = _FRIENDLY_TOOL_STATUSES.get(bare.casefold())
    if mapped:
        icon, text = mapped
        return f"{icon} {text}..."
    unknown = REVVY_VOICE.get("unknown_tool_status", {})
    icon = str(unknown.get("icon") or "🐾") if isinstance(unknown, dict) else "🐾"
    template = (
        str(unknown.get("template") or "{tool} 도구를 살짝 써볼게요")
        if isinstance(unknown, dict)
        else "{tool} 도구를 살짝 써볼게요"
    )
    label = _short_activity(bare, limit=70) or "필요한"
    return f"{icon} {template.replace('{tool}', label)}..."


def humanize_activity(value: str) -> str:
    detail = _short_activity(_redact_chat_secrets(value), limit=150)
    if detail.startswith("도구 실행:"):
        return friendly_tool_activity(detail.partition(":")[2])
    if detail.startswith("명령 실행:"):
        return friendly_tool_activity("Bash")
    if detail.startswith("웹 검색:"):
        return friendly_tool_activity("WebSearch")
    replacements = {
        "요청 분석": "요청을 살펴보고 작업 순서를 정리하는 중이에요",
        "응답 완료": "작업 결과와 검증 내용을 정리했어요",
        "응답 실패": "작업 중 문제가 생겨 원인을 정리했어요",
        "파일 변경 적용": friendly_tool_activity("Edit"),
    }
    return replacements.get(detail, detail)


def _agent_progress_update(text: str, phase: str = "") -> StreamUpdate | None:
    clean = text.strip()
    if not clean:
        return None
    prefixes = (
        ("[진행]", "running"),
        ("[완료]", "completed"),
    )
    for prefix, state in prefixes:
        if clean.startswith(prefix):
            activity = clean[len(prefix):].strip(" \t:·-")
            return StreamUpdate(
                activity=activity or ("다음 작업을 진행하는 중이에요" if state == "running" else "단계를 마쳤어요"),
                activity_state=state,
            )
    if phase == "commentary":
        return StreamUpdate(activity=clean, activity_state="running")
    return None


def _codex_item_activity(item: dict[str, Any]) -> str:
    item_type = str(item.get("type") or "item")
    if item_type == "command_execution":
        command = _short_activity(item.get("command"))
        return f"명령 실행: {command}" if command else "명령 실행"
    if item_type == "mcp_tool_call":
        server = _short_activity(item.get("server"))
        tool = _short_activity(item.get("tool") or item.get("name"))
        target = "/".join(value for value in (server, tool) if value)
        return f"도구 실행: {target}" if target else "도구 실행"
    if item_type == "web_search":
        query = _short_activity(item.get("query"))
        return f"웹 검색: {query}" if query else "웹 검색"
    if item_type == "file_change":
        return "파일 변경 적용"
    label = _short_activity(item.get("name") or item.get("tool") or item_type)
    return f"작업 실행: {label}" if label else "작업 실행"


def _codex_item_failed(item: dict[str, Any]) -> bool:
    status = str(item.get("status") or "").casefold()
    if status in {"cancelled", "canceled", "error", "failed"} or item.get("error"):
        return True
    exit_code = item.get("exit_code")
    try:
        return exit_code is not None and int(exit_code) != 0
    except (TypeError, ValueError):
        return True


_KIMI_FAILURE_STATUSES = {
    "cancelled",
    "canceled",
    "error",
    "failed",
    "timeout",
    "timed_out",
}
_KIMI_COMMAND_FAILURE_PATTERN = re.compile(
    r"(?im)^(?:process exited with code|command failed with exit code|exit[ _]?code)"
    r"\s*:?\s*(-?\d+)\.?\s*$"
)


def _explicit_false(value: Any) -> bool:
    return value is False or (
        isinstance(value, str) and value.strip().casefold() == "false"
    )


def _explicit_true(value: Any) -> bool:
    return value is True or (
        isinstance(value, str) and value.strip().casefold() == "true"
    )


def _nonzero_exit_code(value: Any) -> bool:
    if value is None or isinstance(value, bool):
        return False
    try:
        return int(value) != 0
    except (TypeError, ValueError):
        return False


def _kimi_explicit_failure_fields(value: Mapping[str, Any]) -> bool:
    for key in ("isError", "is_error"):
        if key in value and _explicit_true(value[key]):
            return True
    for key in ("exitCode", "exit_code", "returnCode", "return_code"):
        if key in value and _nonzero_exit_code(value[key]):
            return True
    status = str(value.get("status") or "").strip().casefold()
    return status in _KIMI_FAILURE_STATUSES


def _kimi_encoded_failure_envelope(value: Any) -> bool:
    if isinstance(value, str):
        text = value.strip()
        if not text:
            return False
        command_failure = _KIMI_COMMAND_FAILURE_PATTERN.search(text)
        if command_failure and int(command_failure.group(1)) != 0:
            return True
        if text[:1] != "{":
            return False
        try:
            value = json.loads(text)
        except json.JSONDecodeError:
            return False
    if not isinstance(value, dict):
        return False
    for key in ("isError", "is_error"):
        if key in value and _explicit_true(value[key]):
            return True
    for key in ("exitCode", "exit_code", "returnCode", "return_code"):
        if key in value and _nonzero_exit_code(value[key]):
            return True
    error_keys = {"error", "error_code", "error_message"}
    has_error_detail = any(
        key in value and value[key] not in (None, "", False, {}, [])
        for key in error_keys
    )
    return has_error_detail and any(
        key in value and _explicit_false(value[key])
        for key in ("success", "ok")
    )


def kimi_tool_result_failed(event: Mapping[str, Any]) -> bool:
    """Conservatively identify explicit Kimi tool failures."""

    payload = dict(event)
    if _kimi_explicit_failure_fields(payload):
        return True
    top_level_error = payload.get("error")
    if top_level_error not in (None, "", False, {}, []):
        return True
    return _kimi_encoded_failure_envelope(payload.get("content"))


def sanitize_provider_log(value: str, limit: int = 2_000) -> str:
    clean = _redact_chat_secrets(str(value).strip())
    if len(clean) <= limit:
        return clean
    return clean[: max(1, limit - 1)].rstrip() + "…"


def parse_stream_event(provider: str, line: str) -> StreamUpdate:
    stripped = line.strip()
    if not stripped:
        return StreamUpdate()
    try:
        event = json.loads(stripped)
    except json.JSONDecodeError:
        return StreamUpdate(log=stripped)
    if not isinstance(event, dict):
        return StreamUpdate(log=stripped)

    event_type = str(event.get("type", ""))
    if provider == PROVIDER_CODEX:
        if event_type == "thread.started":
            thread_id = str(event.get("thread_id", ""))
            return StreamUpdate(thread_id=thread_id, log=f"Codex thread {thread_id}")
        if event_type == "item.completed":
            item = event.get("item", {})
            if isinstance(item, dict) and item.get("type") == "agent_message":
                text = str(item.get("text", ""))
                progress = _agent_progress_update(text, str(item.get("phase") or ""))
                return progress or StreamUpdate(text=text, text_kind="message")
            if isinstance(item, dict):
                label = str(item.get("type", "item"))
                name = str(item.get("name") or item.get("tool") or item.get("server") or "")
                failed = _codex_item_failed(item)
                return StreamUpdate(
                    log=f"{'Failed' if failed else 'Completed'} {label}{': ' + name if name else ''}",
                    activity_id=str(item.get("id") or ""),
                    activity=_codex_item_activity(item),
                    activity_state="failed" if failed else "completed",
                )
        if event_type == "item.started":
            item = event.get("item", {})
            if isinstance(item, dict):
                label = str(item.get("type", "item"))
                name = str(item.get("name") or item.get("tool") or item.get("server") or "")
                return StreamUpdate(
                    log=f"Running {label}{': ' + name if name else ''}",
                    activity_id=str(item.get("id") or ""),
                    activity=_codex_item_activity(item),
                    activity_state="running",
                )
        if event_type == "turn.started":
            return StreamUpdate(activity="요청 분석", activity_state="running")
        if event_type == "turn.completed":
            usage = event.get("usage", {})
            if isinstance(usage, dict):
                return StreamUpdate(
                    done=True,
                    activity="응답 완료",
                    activity_state="completed",
                    log=(
                        f"Usage: {usage.get('input_tokens', 0)} in / "
                        f"{usage.get('output_tokens', 0)} out"
                    ),
                )
            return StreamUpdate(
                done=True,
                activity="응답 완료",
                activity_state="completed",
            )
        if event_type in ("error", "turn.failed"):
            error = _message_text(event.get("error") or event.get("message"))
            return StreamUpdate(
                error=error or "Codex request failed",
                activity="응답 실패",
                activity_state="failed",
                done=True,
            )
        return StreamUpdate(log=event_type or stripped)

    if provider == PROVIDER_CLAUDE:
        if event_type == "stream_event":
            inner = event.get("event", {})
            if isinstance(inner, dict):
                if inner.get("type") == "content_block_delta":
                    delta = inner.get("delta", {})
                    if isinstance(delta, dict) and delta.get("type") == "text_delta":
                        return StreamUpdate(text=str(delta.get("text", "")), text_kind="delta")
                if inner.get("type") == "content_block_start":
                    block = inner.get("content_block", {})
                    if isinstance(block, dict) and block.get("type") == "tool_use":
                        name = _short_activity(block.get("name"))
                        arguments = block.get("input")
                        return StreamUpdate(
                            activity_id=str(block.get("id") or ""),
                            activity=f"도구 실행: {name}" if name else "도구 실행",
                            activity_state="running",
                            tool_name=str(block.get("name") or ""),
                            tool_input=arguments if isinstance(arguments, dict) else {},
                        )
            return StreamUpdate()
        if event_type == "user":
            message = event.get("message", {})
            content = message.get("content", []) if isinstance(message, dict) else []
            for item in content if isinstance(content, list) else []:
                if not isinstance(item, dict) or item.get("type") != "tool_result":
                    continue
                failed = bool(item.get("is_error"))
                return StreamUpdate(
                    activity_id=str(item.get("tool_use_id") or ""),
                    activity="도구 실행",
                    activity_state="failed" if failed else "completed",
                )
            return StreamUpdate()
        if event_type == "system":
            session_id = str(event.get("session_id", ""))
            model = str(event.get("model", ""))
            detail = " / ".join(value for value in (session_id, model) if value)
            return StreamUpdate(thread_id=session_id, log=f"Claude session {detail}".strip())
        if event_type == "assistant":
            message = event.get("message", {})
            content = message.get("content", []) if isinstance(message, dict) else []
            text = "".join(
                str(item.get("text", ""))
                for item in content
                if isinstance(item, dict) and item.get("type") == "text"
            )
            progress = _agent_progress_update(text)
            if progress:
                return progress
            # content_block_start arrives before the arguments have streamed in,
            # so the assembled assistant message is where an edit's old/new text
            # is actually complete.
            for item in content if isinstance(content, list) else []:
                if not isinstance(item, dict) or item.get("type") != "tool_use":
                    continue
                arguments = item.get("input")
                if isinstance(arguments, dict) and arguments:
                    return StreamUpdate(
                        text=text,
                        text_kind="snapshot" if text else "",
                        activity_id=str(item.get("id") or ""),
                        tool_name=str(item.get("name") or ""),
                        tool_input=arguments,
                    )
            return StreamUpdate(text=text, text_kind="snapshot")
        if event_type == "result":
            is_error = bool(event.get("is_error")) or event.get("subtype") in (
                "error_during_execution",
                "error_max_turns",
            )
            result = _message_text(event.get("result") or event.get("error"))
            if is_error:
                return StreamUpdate(
                    error=result or "Claude Code request failed",
                    activity="응답 실패",
                    activity_state="failed",
                    done=True,
                )
            return StreamUpdate(
                text=result,
                text_kind="final",
                activity="응답 완료",
                activity_state="completed",
                done=True,
            )
        return StreamUpdate(log=event_type or stripped)

    if provider == PROVIDER_KIMI:
        role = str(event.get("role", "")).casefold()
        if role == "meta":
            session_id = str(event.get("session_id", "")).strip()
            if event_type == "session.resume_hint":
                return StreamUpdate(
                    thread_id=session_id,
                    log=f"Kimi session {session_id}".strip(),
                    activity="응답 완료",
                    activity_state="completed",
                    done=True,
                )
            return StreamUpdate(
                thread_id=session_id,
                log=event_type or stripped,
            )
        if role == "assistant":
            tool_calls = event.get("tool_calls", [])
            content = _message_text(event.get("content"))
            if isinstance(tool_calls, list) and tool_calls:
                names: list[str] = []
                activity_id = ""
                for item in tool_calls:
                    if not isinstance(item, dict):
                        continue
                    activity_id = activity_id or str(item.get("id") or "")
                    function = item.get("function", {})
                    if isinstance(function, dict):
                        name = str(function.get("name") or "").strip()
                        if name:
                            names.append(name)
                label = ", ".join(names)
                return StreamUpdate(
                    text=content,
                    text_kind="message" if content else "",
                    activity_id=activity_id,
                    activity=f"도구 실행: {label}" if label else "도구 실행",
                    activity_state="running",
                )
            progress = _agent_progress_update(content)
            return progress or StreamUpdate(text=content, text_kind="message")
        if role == "tool":
            failed = kimi_tool_result_failed(event)
            return StreamUpdate(
                activity_id=str(event.get("tool_call_id") or ""),
                activity="도구 실행",
                activity_state="failed" if failed else "completed",
                log="Kimi tool failed" if failed else "",
            )
        if role == "error" or event_type in {"error", "request.error"}:
            error = _message_text(event.get("content") or event.get("error"))
            return StreamUpdate(
                error=error or "Kimi Code request failed",
                activity="응답 실패",
                activity_state="failed",
                done=True,
            )
        return StreamUpdate(log=event_type or stripped)

    return StreamUpdate(error=f"Unknown provider: {provider}")


def parse_tool_arguments(raw: str) -> dict[str, Any]:
    value = json.loads(raw or "{}")
    if not isinstance(value, dict):
        raise ValueError("Tool arguments must be a JSON object.")
    return value


def tool_list_from_response(response: dict[str, Any]) -> list[dict[str, Any]]:
    if not isinstance(response, dict):
        raise ValueError("Invalid MCP response.")
    if isinstance(response.get("error"), dict):
        raise ValueError(_message_text(response["error"]) or "MCP request failed.")
    result = response.get("result", {})
    tools = result.get("tools", []) if isinstance(result, dict) else []
    if not isinstance(tools, list):
        raise ValueError("MCP tools/list did not return an array.")
    return [tool for tool in tools if isinstance(tool, dict) and tool.get("name")]


def tool_result_text(response: dict[str, Any]) -> str:
    if isinstance(response.get("error"), dict):
        raise ValueError(_message_text(response["error"]) or "MCP request failed.")
    result = response.get("result", {})
    if isinstance(result, dict):
        content = result.get("content", [])
        if isinstance(content, list):
            texts = [
                str(item.get("text", ""))
                for item in content
                if isinstance(item, dict) and item.get("type") == "text"
            ]
            if texts:
                return "\n".join(texts)
    return json.dumps(response, indent=2, ensure_ascii=False)


def kimi_auth_configured(env: dict[str, str] | None = None) -> bool:
    """Return whether Kimi's selected model has a local credential configured."""

    root = kimi_home(env)
    path = root / "config.toml"
    try:
        raw = path.read_bytes().decode("utf-8-sig")
        if tomllib is None:
            return bool(
                re.search(
                    r'^\s*api_key\s*=\s*["\'][^"\']+["\']',
                    raw,
                    re.MULTILINE,
                )
            )
        payload = tomllib.loads(raw)
    except (OSError, UnicodeError, ValueError):
        return False
    default_model = str(payload.get("default_model", "")).strip()
    models = payload.get("models", {})
    model = models.get(default_model, {}) if isinstance(models, dict) else {}
    provider_id = str(model.get("provider", "")).strip() if isinstance(model, dict) else ""
    providers = payload.get("providers", {})
    provider = providers.get(provider_id, {}) if isinstance(providers, dict) else {}
    if not isinstance(provider, dict):
        return False
    for key in ("api_key", "access_token", "refresh_token", "token"):
        if str(provider.get(key, "")).strip():
            return True
    provider_env = provider.get("env", {})
    if isinstance(provider_env, dict) and any(
        str(value).strip() for value in provider_env.values()
    ):
        return True
    credential_names = {
        f"{provider_id}.json",
        f"{provider_id.rsplit(':', 1)[-1]}.json",
    }
    for name in credential_names:
        try:
            credential = json.loads(
                (root / "credentials" / name).read_text(encoding="utf-8-sig")
            )
        except (OSError, UnicodeError, json.JSONDecodeError):
            continue
        if isinstance(credential, dict) and any(
            str(credential.get(key, "")).strip()
            for key in ("access_token", "refresh_token", "token")
        ):
            return True
    return False


def login_status_command(provider: str, env: dict[str, str] | None = None) -> list[str]:
    if provider == PROVIDER_CODEX:
        return [resolve_executable("codex", env), "login", "status"]
    if provider == PROVIDER_CLAUDE:
        return [resolve_executable("claude", env), "auth", "status", "--json"]
    if provider == PROVIDER_KIMI:
        return [resolve_executable("kimi", env), "doctor"]
    raise ValueError(f"Unknown provider: {provider}")


def process_creation_flags() -> int:
    if os.name != "nt":
        return 0
    return subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP


def run_login_status(provider: str, timeout: float = 10.0) -> tuple[bool, str]:
    command = login_status_command(provider)
    try:
        completed = subprocess.run(
            command,
            stdin=subprocess.DEVNULL,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout,
            creationflags=process_creation_flags(),
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        return False, str(exc)
    detail = (completed.stdout or completed.stderr or "").strip()
    ok = completed.returncode == 0
    if provider == PROVIDER_KIMI and ok:
        if not kimi_auth_configured():
            return False, "Kimi CLI is installed, but no credential is configured. Run `kimi login`."
        return True, "Kimi CLI configuration and local credentials are ready."
    return ok, detail


def tail_text(path: pathlib.Path, line_count: int = 300) -> str:
    try:
        with path.open("r", encoding="utf-8", errors="replace") as handle:
            lines = handle.readlines()
    except OSError as exc:
        return str(exc)
    return "".join(lines[-max(1, line_count):])


def find_support_proxy_executable(app_executable: str | None = None) -> pathlib.Path:
    executable = pathlib.Path(app_executable or sys.executable).resolve()
    suffix = ".exe" if os.name == "nt" else ""
    return executable.with_name(f"RevvyProxy-{APP_VERSION}{suffix}")


def _proxy_supports_current_app(
    health: Any,
    expected_full_access_fingerprint: str = "",
) -> bool:
    if not isinstance(health, dict):
        return False
    capabilities = health.get("capabilities")
    try:
        api_version = int(health.get("proxy_api_version", 0))
    except (TypeError, ValueError):
        return False
    return (
        api_version >= 5
        and isinstance(capabilities, list)
        and {
            "mcp_upstreams",
            "on_demand_mcp_upstreams",
            "blender_bridge",
            "safe_read_only_endpoint",
            "full_access_auth",
        }.issubset(
            {str(value) for value in capabilities}
        )
        and (
            not expected_full_access_fingerprint
            or health.get("full_access_token_fingerprint")
            == expected_full_access_fingerprint
        )
    )


def start_proxy_service(revvy_module: Any, app_executable: str | None = None) -> int:
    """Start the normal Python proxy, or the packaged support executable."""

    if not getattr(sys, "frozen", False) and not hasattr(
        revvy_module, "ensure_runtime_config"
    ):
        return int(revvy_module.start_proxy(quiet=True))
    config = revvy_module.ensure_runtime_config()
    token_getter = getattr(revvy_module, "full_access_token", None)
    full_access_token = str(token_getter()) if callable(token_getter) else ""
    expected_fingerprint = (
        hashlib.sha256(full_access_token.encode("utf-8")).hexdigest()
        if full_access_token
        else ""
    )
    health = revvy_module.get_health(config)
    if _proxy_supports_current_app(health, expected_fingerprint):
        return int(revvy_module.EXIT_OK)
    if not health:
        raw_probe = getattr(revvy_module, "_request_proxy_health", None)
        foreign_health = raw_probe(config) if callable(raw_probe) else None
        if foreign_health:
            # Fixed ports are still single-slot. Never stop or adopt a proxy
            # whose config belongs to another project's .revvy directory.
            return int(revvy_module.EXIT_PROXY_UNAVAILABLE)
    if health:
        stopped = revvy_module._stop_managed_proxies(
            config, include_configured_port=True, quiet=True
        )
        if stopped != revvy_module.EXIT_OK:
            return int(stopped)
        remaining_health = revvy_module.get_health(config)
        if remaining_health:
            if _proxy_supports_current_app(
                remaining_health, expected_fingerprint
            ):
                return int(revvy_module.EXIT_OK)
            # A managed-process cleanup that reports success is not proof that
            # the listener went away. Never let start_proxy() adopt a stubborn
            # API-incompatible process merely because its /health still answers.
            return int(revvy_module.EXIT_PROXY_UNAVAILABLE)
    if not getattr(sys, "frozen", False):
        started = int(revvy_module.start_proxy(quiet=True))
        if started != revvy_module.EXIT_OK:
            return started
        final_health = revvy_module.get_health(config)
        return int(
            revvy_module.EXIT_OK
            if _proxy_supports_current_app(final_health, expected_fingerprint)
            else revvy_module.EXIT_PROXY_UNAVAILABLE
        )

    stopped = revvy_module._stop_managed_proxies(
        config, include_configured_port=False, quiet=True
    )
    if stopped != revvy_module.EXIT_OK:
        return int(stopped)

    support = find_support_proxy_executable(app_executable)
    if not support.is_file():
        raise FileNotFoundError(f"Revvy proxy support executable not found: {support}")
    pathlib.Path(revvy_module.LOG_PATH).parent.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    environment["PYTHONUTF8"] = "1"
    runtime_state = pathlib.Path(
        getattr(
            revvy_module,
            "STATE_DIR",
            pathlib.Path(revvy_module.CONFIG_PATH).parent,
        )
    )
    environment["REVVY_STATE_DIR"] = str(runtime_state)
    environment["REVVY_CONFIG"] = str(revvy_module.CONFIG_PATH)
    environment["REVVY_MCP_UPSTREAMS_CONFIG"] = str(
        os.environ.get("REVVY_MCP_UPSTREAMS_CONFIG")
        or runtime_state / "mcp-upstreams.json"
    )
    environment["REVVY_PROXY_LOG"] = str(revvy_module.LOG_PATH)
    environment[FULL_ACCESS_TOKEN_ENV] = full_access_token
    with pathlib.Path(revvy_module.LOG_PATH).open("ab") as log_handle:
        process = subprocess.Popen(
            [str(support)],
            cwd=str(support.parent),
            env=environment,
            stdin=subprocess.DEVNULL,
            stdout=log_handle,
            stderr=subprocess.STDOUT,
            creationflags=process_creation_flags(),
            start_new_session=os.name != "nt",
        )

    deadline = time.monotonic() + 8.0
    while time.monotonic() < deadline:
        if _proxy_supports_current_app(
            revvy_module.get_health(config, timeout=1.0),
            expected_fingerprint,
        ):
            return int(revvy_module.EXIT_OK)
        time.sleep(0.1)
    if process.poll() is None:
        terminate_process_tree(process.pid, timeout=5.0)
    return int(revvy_module.EXIT_PROXY_UNAVAILABLE)


def rpc_request_with_proxy(
    revvy_module: Any,
    method: str,
    params: Any = None,
    *,
    timeout: float = 10.0,
    app_executable: str | None = None,
) -> Any:
    """Ensure the source or packaged proxy is ready before making an RPC request."""

    code = start_proxy_service(revvy_module, app_executable)
    if code != revvy_module.EXIT_OK:
        raise ConnectionError(f"Revvy proxy failed to start (exit code {code})")
    return revvy_module.rpc_request(method, params, timeout=timeout)
