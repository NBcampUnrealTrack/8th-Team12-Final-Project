"""StdIO MCP upstream support for the persistent Revvy proxy.

Configuration is intentionally small and strict.  Revvy only inherits a
minimal process environment plus explicitly named variables; environment
values and credentials do not belong in mcp-upstreams.json.
"""

from __future__ import annotations

import json
import os
import pathlib
import queue
import re
import secrets
import subprocess
import threading
import time
from collections import deque
from dataclasses import dataclass
from types import MappingProxyType
from typing import Mapping

from revvy_processes import (
    attach_process_tree,
    terminate_attached_process_tree,
)


PROTOCOL_VERSION = "2025-06-18"
_SERVER_NAME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9_-]{0,31}$")
_TOOL_NAME_RE = re.compile(r"^[A-Za-z0-9_.:-]{1,128}$")
_ENV_NAME_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
_SECRET_ARGUMENT_RE = re.compile(
    r"(?i)(?:api[_-]?key|access[_-]?token|auth[_-]?token|password|secret|credential)"
    r"(?:\s|=|:|$)"
)
_SECRET_TEXT_RE = re.compile(
    r"(?i)\b(api[_-]?key|access[_-]?token|auth[_-]?token|token|auth(?:orization)?|"
    r"password|secret|credential)"
    r"\s*[:=]\s*([^\s,;]+)"
)
_BEARER_RE = re.compile(r"(?i)\bBearer\s+[A-Za-z0-9._~+/=-]+")
_URL_CREDENTIAL_RE = re.compile(r"(https?://)[^/@\s]+@", re.IGNORECASE)

_FIXED_ENV_NAMES = {
    "APPDATA",
    "COMSPEC",
    "HOME",
    "HOMEDRIVE",
    "HOMEPATH",
    "LANG",
    "LOCALAPPDATA",
    "PATH",
    "PATHEXT",
    "PROGRAMDATA",
    "PROGRAMFILES",
    "PROGRAMFILES(X86)",
    "PROGRAMW6432",
    "SYSTEMDRIVE",
    "SYSTEMROOT",
    "TEMP",
    "TMP",
    "USERPROFILE",
    "WINDIR",
}
_SERVER_KEYS = {
    "enabled",
    "activation",
    "transport",
    "command",
    "args",
    "cwd",
    "timeout_seconds",
    "startup_timeout_seconds",
    "allow_tools",
    "deny_tools",
    "read_only_tools",
    "mutating_tools",
    "allow_mutations",
    "inherit_env",
    "compact_catalog",
}

# Spawning the child and completing initialize/tools list is a different kind of
# wait from a tool call: a real Blender or Unity bridge boots in seconds. Billing
# both to timeout_seconds meant that tuning tool calls to be responsive quietly
# made the upstream unstartable.
DEFAULT_STARTUP_TIMEOUT_SECONDS = 15.0

# A startup budget that large would otherwise be paid again on every call to a
# dead upstream, because route_for re-runs tools() when it finds no route.
START_RETRY_COOLDOWN_SECONDS = 5.0


class UpstreamError(RuntimeError):
    """An upstream could not be configured, started, or called."""


class MutationDenied(UpstreamError):
    """A configured mutating tool was called without explicit permission."""


class MutationAcknowledgementLost(UpstreamError):
    """A mutation was written to an upstream, but no definitive response arrived."""

    def __init__(self, job_id: str, poll_with: str | None):
        self.job_id = job_id
        self.may_have_started = True
        self.poll_with = poll_with
        super().__init__(
            "The external MCP mutation may have started, but its acknowledgement "
            "was lost. Poll the job before any retry."
        )

    @property
    def details(self) -> dict:
        return {
            "job_id": self.job_id,
            "may_have_started": self.may_have_started,
            "poll_with": self.poll_with,
        }


@dataclass(frozen=True)
class ServerConfig:
    name: str
    namespace: str
    activation: str
    command: str
    args: tuple[str, ...]
    cwd: str | None
    timeout_seconds: float
    startup_timeout_seconds: float
    allow_tools: frozenset[str]
    deny_tools: frozenset[str]
    read_only_tools: frozenset[str]
    mutating_tools: frozenset[str]
    allow_mutations: bool
    inherit_env: tuple[str, ...]
    # Fold this server's tools into two meta tools (list + call) to keep large
    # upstream catalogues from flooding every client session's context window.
    compact_catalog: bool = False


# Meta upstream-tool names a compact server publishes instead of its catalogue.
# They live in the same `<namespace>__<name>` space as real tools; a compact
# server's own tools stay reachable only through the call meta tool.
COMPACT_LIST_TOOL = "tools"
COMPACT_CALL_TOOL = "call"


def _is_compact_meta(config: ServerConfig, upstream_name: str) -> bool:
    # getattr like the activation reads elsewhere: tests and older callers hand
    # in config doubles that predate the field.
    return bool(getattr(config, "compact_catalog", False)) and upstream_name in (
        COMPACT_LIST_TOOL,
        COMPACT_CALL_TOOL,
    )


@dataclass(frozen=True)
class ToolRoute:
    server_name: str
    upstream_name: str
    mutating: bool
    generation: int
    catalog_revision: int


@dataclass(frozen=True)
class ToolDefinition:
    definition: dict
    catalog_revision: int


def _string_list(value, field: str, *, tool_names: bool = False) -> tuple[str, ...]:
    if value is None:
        return ()
    if not isinstance(value, list) or len(value) > 256:
        raise UpstreamError(f"{field} must be an array with at most 256 entries")
    result: list[str] = []
    for item in value:
        if not isinstance(item, str) or not item or "\x00" in item or "\n" in item:
            raise UpstreamError(f"{field} contains an invalid string")
        if tool_names and not _TOOL_NAME_RE.fullmatch(item):
            raise UpstreamError(f"{field} contains an invalid MCP tool name")
        result.append(item)
    return tuple(result)


def _parse_server(name: str, value: object) -> ServerConfig | None:
    if not _SERVER_NAME_RE.fullmatch(name):
        raise UpstreamError(f"Invalid upstream server name: {name!r}")
    if not isinstance(value, dict):
        raise UpstreamError(f"Server {name!r} must be an object")
    unknown = set(value) - _SERVER_KEYS
    if unknown:
        raise UpstreamError(
            f"Server {name!r} has unsupported fields: {', '.join(sorted(unknown))}"
        )
    enabled = value.get("enabled", True)
    if not isinstance(enabled, bool):
        raise UpstreamError(f"Server {name!r} enabled must be boolean")
    if not enabled:
        return None
    if value.get("transport", "stdio") != "stdio":
        raise UpstreamError(f"Server {name!r} only supports stdio transport")

    # Project-local configuration is not an authorization boundary.  A cloned
    # repository must not gain code execution merely because a client asked
    # Revvy for its tool catalogue, so omitted activation is deliberately lazy.
    activation = value.get("activation", "on_demand")
    if not isinstance(activation, str) or activation not in {
        "eager",
        "on_demand",
    }:
        raise UpstreamError(
            f"Server {name!r} activation must be eager or on_demand"
        )

    command = value.get("command")
    if (
        not isinstance(command, str)
        or not command.strip()
        or "\x00" in command
        or "\n" in command
    ):
        raise UpstreamError(f"Server {name!r} command must be a non-empty string")
    args = _string_list(value.get("args", []), f"{name}.args")
    if any(_SECRET_ARGUMENT_RE.search(argument) for argument in args):
        raise UpstreamError(
            f"Server {name!r} args appear to contain credentials; inherit an env name instead"
        )

    cwd = value.get("cwd")
    if cwd is not None:
        if not isinstance(cwd, str) or not cwd.strip() or "\x00" in cwd or "\n" in cwd:
            raise UpstreamError(f"Server {name!r} cwd must be a non-empty string")
        cwd = str(pathlib.Path(cwd).expanduser().resolve())

    timeout = value.get("timeout_seconds", 30.0)
    if isinstance(timeout, bool):
        raise UpstreamError(f"Server {name!r} timeout_seconds must be numeric")
    try:
        timeout = float(timeout)
    except (TypeError, ValueError) as exc:
        raise UpstreamError(f"Server {name!r} timeout_seconds must be numeric") from exc
    if not 0.5 <= timeout <= 300.0:
        raise UpstreamError(f"Server {name!r} timeout_seconds must be between 0.5 and 300")

    startup_timeout = value.get("startup_timeout_seconds")
    if startup_timeout is None:
        # Never shorter than the call budget an operator already accepted.
        startup_timeout = max(timeout, DEFAULT_STARTUP_TIMEOUT_SECONDS)
    else:
        if isinstance(startup_timeout, bool):
            raise UpstreamError(f"Server {name!r} startup_timeout_seconds must be numeric")
        try:
            startup_timeout = float(startup_timeout)
        except (TypeError, ValueError) as exc:
            raise UpstreamError(
                f"Server {name!r} startup_timeout_seconds must be numeric"
            ) from exc
        if not 0.5 <= startup_timeout <= 300.0:
            raise UpstreamError(
                f"Server {name!r} startup_timeout_seconds must be between 0.5 and 300"
            )

    allow_tools = frozenset(
        _string_list(value.get("allow_tools", []), f"{name}.allow_tools", tool_names=True)
    )
    if not allow_tools:
        raise UpstreamError(
            f"Server {name!r} allow_tools must explicitly list every exposed tool"
        )
    deny_tools = frozenset(
        _string_list(value.get("deny_tools", []), f"{name}.deny_tools", tool_names=True)
    )
    if "read_only_tools" not in value or "mutating_tools" not in value:
        raise UpstreamError(
            f"Server {name!r} must explicitly classify read_only_tools and mutating_tools"
        )
    read_only_tools = frozenset(
        _string_list(
            value.get("read_only_tools", []),
            f"{name}.read_only_tools",
            tool_names=True,
        )
    )
    mutating_tools = frozenset(
        _string_list(
            value.get("mutating_tools", []),
            f"{name}.mutating_tools",
            tool_names=True,
        )
    )
    if read_only_tools & mutating_tools:
        raise UpstreamError(
            f"Server {name!r} read_only_tools and mutating_tools must be disjoint"
        )
    if read_only_tools | mutating_tools != allow_tools:
        raise UpstreamError(
            f"Server {name!r} must classify every allow_tools entry exactly once"
        )
    allow_mutations = value.get("allow_mutations", False)
    if not isinstance(allow_mutations, bool):
        raise UpstreamError(f"Server {name!r} allow_mutations must be boolean")
    compact_catalog = value.get("compact_catalog", False)
    if not isinstance(compact_catalog, bool):
        raise UpstreamError(f"Server {name!r} compact_catalog must be boolean")

    inherit_env = _string_list(value.get("inherit_env", []), f"{name}.inherit_env")
    if any(not _ENV_NAME_RE.fullmatch(env_name) for env_name in inherit_env):
        raise UpstreamError(f"Server {name!r} inherit_env contains an invalid env name")

    return ServerConfig(
        name=name,
        namespace=name.casefold(),
        activation=activation,
        command=command,
        args=args,
        cwd=cwd,
        timeout_seconds=timeout,
        startup_timeout_seconds=startup_timeout,
        allow_tools=allow_tools,
        deny_tools=deny_tools,
        read_only_tools=read_only_tools,
        mutating_tools=mutating_tools,
        allow_mutations=allow_mutations,
        inherit_env=inherit_env,
        compact_catalog=compact_catalog,
    )


def load_config(path: pathlib.Path) -> dict[str, ServerConfig]:
    if not path.exists():
        return {}
    try:
        with path.open("r", encoding="utf-8-sig") as handle:
            raw = json.load(handle)
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise UpstreamError(f"Could not read upstream configuration: {exc}") from exc
    if not isinstance(raw, dict):
        raise UpstreamError("Upstream configuration must be an object")
    unknown = set(raw) - {"version", "servers"}
    if unknown:
        raise UpstreamError(
            f"Unsupported upstream configuration fields: {', '.join(sorted(unknown))}"
        )
    if raw.get("version", 1) != 1:
        raise UpstreamError("Unsupported upstream configuration version")
    servers = raw.get("servers", {})
    if not isinstance(servers, dict):
        raise UpstreamError("Upstream servers must be an object")
    parsed: dict[str, ServerConfig] = {}
    namespaces: set[str] = set()
    for name, value in servers.items():
        if not isinstance(name, str):
            raise UpstreamError("Upstream server names must be strings")
        server = _parse_server(name, value)
        if server is None:
            continue
        if server.namespace in namespaces:
            raise UpstreamError(f"Duplicate upstream namespace: {server.namespace}")
        namespaces.add(server.namespace)
        parsed[name] = server
    return parsed


def _redact(text: str, inherited_values: tuple[str, ...] = ()) -> str:
    redacted = _SECRET_TEXT_RE.sub(r"\1=***", text)
    redacted = _BEARER_RE.sub("Bearer ***", redacted)
    redacted = _URL_CREDENTIAL_RE.sub(r"\1***@", redacted)
    for value in inherited_values:
        if len(value) >= 4:
            redacted = redacted.replace(value, "***")
    return redacted[:2000]


class StdioMcpClient:
    def __init__(self, config: ServerConfig):
        self.config = config
        self._process: subprocess.Popen[str] | None = None
        self._reader: threading.Thread | None = None
        self._stderr_reader: threading.Thread | None = None
        self._lifecycle_lock = threading.RLock()
        self._write_lock = threading.Lock()
        self._pending_lock = threading.Lock()
        self._state_lock = threading.Lock()
        self._pending: dict[int | str, queue.Queue[object]] = {}
        self._next_id = 1
        self._tools: tuple[dict, ...] = ()
        self._ready = False
        self._last_error = ""
        self._stderr_tail: deque[str] = deque(maxlen=8)
        self._started_at: float | None = None
        self._stopping = False
        self._retired = False
        self._inherited_values: tuple[str, ...] = ()
        self._start_failure_at = 0.0

    def _running(self) -> bool:
        process = self._process
        return process is not None and process.poll() is None

    def _environment(self) -> dict[str, str]:
        names = _FIXED_ENV_NAMES | set(self.config.inherit_env)
        env = {name: os.environ[name] for name in names if name in os.environ}
        env["PYTHONIOENCODING"] = "utf-8"
        env["PYTHONUTF8"] = "1"
        self._inherited_values = tuple(
            os.environ[name]
            for name in self.config.inherit_env
            if name in os.environ and os.environ[name]
        )
        return env

    def ensure_started(self) -> None:
        with self._lifecycle_lock:
            if self._retired:
                raise UpstreamError("Upstream client was retired after configuration changed")
            if self._running() and self._ready:
                return
            # A recent start failure is not retried immediately: route_for re-runs
            # tools() whenever it finds no route, so a dead upstream would charge
            # the whole startup budget to every single call.
            if (
                self._start_failure_at
                and time.monotonic() - self._start_failure_at
                < START_RETRY_COOLDOWN_SECONDS
            ):
                raise UpstreamError(self._last_error or "Upstream failed to start")
            self._shutdown_locked()
            self._stopping = False
            self._ready = False
            startup_deadline = (
                time.monotonic() + self.config.startup_timeout_seconds
            )

            def remaining_startup_budget(method: str) -> float:
                remaining = startup_deadline - time.monotonic()
                if remaining <= 0:
                    message = f"Upstream request timed out: {method}"
                    self._set_error(message)
                    raise UpstreamError(message)
                return remaining

            creationflags = (
                int(getattr(subprocess, "CREATE_NO_WINDOW", 0)) if os.name == "nt" else 0
            )
            try:
                self._process = subprocess.Popen(
                    [self.config.command, *self.config.args],
                    stdin=subprocess.PIPE,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                    encoding="utf-8",
                    errors="replace",
                    bufsize=1,
                    cwd=self.config.cwd,
                    env=self._environment(),
                    creationflags=creationflags,
                    start_new_session=os.name != "nt",
                )
                if not attach_process_tree(self._process):
                    # A configured upstream may itself be a launcher. Without a
                    # durable job/process-group identity, its parent can exit
                    # before shutdown and leave arbitrary descendants behind.
                    terminate_attached_process_tree(self._process, timeout=3.0)
                    raise OSError(
                        "could not attach the upstream to an owned process tree"
                    )
            except (OSError, ValueError) as exc:
                self._set_error(f"Could not start upstream process: {exc}")
                self._start_failure_at = time.monotonic()
                raise UpstreamError(self._last_error) from exc
            self._started_at = time.time()
            self._reader = threading.Thread(
                target=self._read_stdout,
                args=(self._process,),
                name=f"RevvyMcpStdout-{self.config.namespace}",
                daemon=True,
            )
            self._stderr_reader = threading.Thread(
                target=self._read_stderr,
                args=(self._process,),
                name=f"RevvyMcpStderr-{self.config.namespace}",
                daemon=True,
            )
            self._reader.start()
            self._stderr_reader.start()
            try:
                # The handshake gets the startup budget, not the per-call one: the
                # child still has to finish interpreter start-up and its imports.
                self._request(
                    "initialize",
                    {
                        "protocolVersion": PROTOCOL_VERSION,
                        "capabilities": {},
                        "clientInfo": {"name": "Revvy-Proxy", "version": "2.0.0"},
                    },
                    timeout=remaining_startup_budget("initialize"),
                )
                self._notify("notifications/initialized", {})
                response = self._request(
                    "tools/list", {}, timeout=remaining_startup_budget("tools/list")
                )
                tools = response.get("tools", [])
                if not isinstance(tools, list):
                    raise UpstreamError("Upstream tools/list returned an invalid result")
                validated: list[dict] = []
                for tool in tools:
                    if not isinstance(tool, dict):
                        continue
                    tool_name = tool.get("name")
                    if not isinstance(tool_name, str) or not _TOOL_NAME_RE.fullmatch(tool_name):
                        continue
                    if tool_name not in self.config.allow_tools:
                        continue
                    if tool_name in self.config.deny_tools:
                        continue
                    annotations = (
                        tool.get("annotations")
                        if isinstance(tool.get("annotations"), dict)
                        else {}
                    )
                    annotations["readOnlyHint"] = (
                        tool_name in self.config.read_only_tools
                    )
                    validated.append(
                        {
                            "name": tool_name,
                            "title": str(tool.get("title", "")),
                            "description": str(tool.get("description", "")),
                            "inputSchema": (
                                tool.get("inputSchema")
                                if isinstance(tool.get("inputSchema"), dict)
                                else {"type": "object", "properties": {}}
                            ),
                            "annotations": annotations,
                            "outputSchema": (
                                tool.get("outputSchema")
                                if isinstance(tool.get("outputSchema"), dict)
                                else None
                            ),
                        }
                    )
                self._tools = tuple(validated)
                self._ready = True
                self._set_error("")
                self._start_failure_at = 0.0
            except UpstreamError:
                self._start_failure_at = time.monotonic()
                self._shutdown_locked()
                raise

    def tools(self) -> tuple[dict, ...]:
        self.ensure_started()
        return self._tools

    def is_ready(self) -> bool:
        """Return whether the discovered catalogue still has a live process."""
        with self._lifecycle_lock:
            return self._running() and self._ready

    def call_tool(
        self,
        name: str,
        arguments: dict,
        *,
        request_id: str | None = None,
        poll_with: str | None = None,
        allow_restart: bool = True,
    ) -> dict:
        if allow_restart:
            self.ensure_started()
        elif not self.is_ready():
            raise UpstreamError("Upstream process changed after tool validation")
        mutating = name in self.config.mutating_tools
        if mutating and not self.config.allow_mutations:
            raise MutationDenied(
                f"Mutation is disabled for {self.config.namespace}__{name}; "
                "set allow_mutations=true only for a trusted typed bridge"
            )
        known = {tool["name"] for tool in self._tools}
        if name not in known:
            raise UpstreamError(f"Unknown upstream tool: {self.config.namespace}__{name}")
        stable_request_id = request_id
        if mutating and stable_request_id is None:
            stable_request_id = f"revvy_{secrets.token_hex(16)}"
        result = self._request(
            "tools/call",
            {"name": name, "arguments": arguments},
            request_id=stable_request_id,
            uncertain_job_id=stable_request_id if mutating else None,
            poll_with=poll_with,
        )
        if not isinstance(result, dict):
            raise UpstreamError("Upstream tools/call returned an invalid result")
        return result

    def _request(
        self,
        method: str,
        params: dict,
        *,
        request_id: int | str | None = None,
        uncertain_job_id: str | None = None,
        poll_with: str | None = None,
        timeout: float | None = None,
    ) -> dict:
        process = self._process
        if process is None or process.poll() is not None:
            raise UpstreamError("Upstream process is not running")
        with self._pending_lock:
            if request_id is None:
                request_id = self._next_id
                self._next_id += 1
            if request_id in self._pending:
                raise UpstreamError(f"Duplicate upstream request id: {request_id}")
            response_queue: queue.Queue[object] = queue.Queue(maxsize=1)
            self._pending[request_id] = response_queue
        payload = {
            "jsonrpc": "2.0",
            "id": request_id,
            "method": method,
            "params": params,
        }
        write_completed = False
        try:
            try:
                self._write(payload)
            except UpstreamError as exc:
                # TextIO write/flush is not transactional: the child can read
                # and execute a complete line before flush reports a broken
                # pipe.  Once a mutating write is attempted, classify every
                # transport failure conservatively so callers poll the stable
                # job id instead of replaying the operation.
                if uncertain_job_id is not None:
                    raise MutationAcknowledgementLost(
                        uncertain_job_id,
                        poll_with,
                    ) from exc
                raise
            write_completed = True
            try:
                response = response_queue.get(
                    timeout=self.config.timeout_seconds if timeout is None else timeout
                )
            except queue.Empty as exc:
                message = f"Upstream request timed out: {method}"
                self._set_error(message)
                with self._lifecycle_lock:
                    if self._process is process:
                        self._shutdown_locked()
                if uncertain_job_id is not None and write_completed:
                    raise MutationAcknowledgementLost(
                        uncertain_job_id,
                        poll_with,
                    ) from exc
                raise UpstreamError(message) from exc
        finally:
            with self._pending_lock:
                self._pending.pop(request_id, None)
        if isinstance(response, BaseException):
            if uncertain_job_id is not None and write_completed:
                raise MutationAcknowledgementLost(
                    uncertain_job_id,
                    poll_with,
                ) from response
            raise UpstreamError(str(response))
        if not isinstance(response, dict):
            if uncertain_job_id is not None and write_completed:
                raise MutationAcknowledgementLost(
                    uncertain_job_id,
                    poll_with,
                )
            raise UpstreamError("Upstream returned an invalid response")
        error = response.get("error")
        if isinstance(error, dict):
            message = _redact(
                str(error.get("message", "Upstream request failed")),
                self._inherited_values,
            )
            raise UpstreamError(message)
        if "error" in response:
            if uncertain_job_id is not None and write_completed:
                raise MutationAcknowledgementLost(
                    uncertain_job_id,
                    poll_with,
                )
            raise UpstreamError("Upstream returned an invalid error response")
        result = response.get("result")
        if not isinstance(result, dict):
            if uncertain_job_id is not None and write_completed:
                raise MutationAcknowledgementLost(
                    uncertain_job_id,
                    poll_with,
                )
            raise UpstreamError("Upstream response has no object result")
        return result

    def _notify(self, method: str, params: dict) -> None:
        self._write({"jsonrpc": "2.0", "method": method, "params": params})

    def _write(self, payload: dict) -> None:
        process = self._process
        if process is None or process.stdin is None or process.poll() is not None:
            raise UpstreamError("Upstream process is not writable")
        data = json.dumps(payload, ensure_ascii=False, separators=(",", ":")) + "\n"
        try:
            with self._write_lock:
                process.stdin.write(data)
                process.stdin.flush()
        except (BrokenPipeError, OSError, ValueError) as exc:
            raise UpstreamError("Upstream process closed its input") from exc

    def _read_stdout(self, process: subprocess.Popen[str]) -> None:
        if process.stdout is None:
            return
        try:
            for line in process.stdout:
                line = line.strip()
                if not line:
                    continue
                try:
                    message = json.loads(line)
                except json.JSONDecodeError:
                    self._set_error("Upstream wrote non-JSON data to stdout")
                    continue
                if not isinstance(message, dict) or "id" not in message:
                    continue
                response_id = message.get("id")
                if isinstance(response_id, bool) or not isinstance(response_id, (int, str)):
                    continue
                with self._pending_lock:
                    target = self._pending.get(response_id)
                if target is not None:
                    try:
                        target.put_nowait(message)
                    except queue.Full:
                        pass
        except (OSError, ValueError) as exc:
            if self._process is process and not self._stopping:
                self._set_error(f"Upstream stdout failed: {exc}")
        finally:
            if self._process is process and not self._stopping:
                self._set_error("Upstream process exited")
                self._fail_pending(UpstreamError("Upstream process exited"))

    def _read_stderr(self, process: subprocess.Popen[str]) -> None:
        if process.stderr is None:
            return
        try:
            for line in process.stderr:
                sanitized = _redact(line.strip(), self._inherited_values)
                if sanitized and self._process is process:
                    with self._state_lock:
                        self._stderr_tail.append(sanitized)
        except (OSError, ValueError):
            return

    def _set_error(self, message: str) -> None:
        with self._state_lock:
            self._last_error = _redact(message, self._inherited_values)

    @property
    def last_error(self) -> str:
        with self._state_lock:
            return self._last_error

    def _fail_pending(self, error: BaseException) -> None:
        with self._pending_lock:
            targets = list(self._pending.values())
        for target in targets:
            try:
                target.put_nowait(error)
            except queue.Full:
                pass

    def shutdown(self) -> None:
        with self._lifecycle_lock:
            self._shutdown_locked()

    def retire(self) -> None:
        with self._lifecycle_lock:
            self._retired = True
            self._shutdown_locked()

    def _shutdown_locked(self) -> None:
        process = self._process
        if process is None:
            self._tools = ()
            self._ready = False
            return
        self._stopping = True
        self._fail_pending(UpstreamError("Upstream process is shutting down"))
        if process.stdin is not None:
            try:
                process.stdin.close()
            except OSError:
                pass
        tree_retired = False
        if process.poll() is None:
            try:
                # A stdio MCP should exit on EOF. Give a one-file child enough
                # time to unwind its inner runtime before escalating.
                process.wait(timeout=0.5)
            except subprocess.TimeoutExpired:
                tree_stopped = terminate_attached_process_tree(
                    process, timeout=3.0
                )
                tree_retired = True
                try:
                    if not tree_stopped:
                        process.terminate()
                    process.wait(timeout=2.0)
                except (OSError, subprocess.TimeoutExpired):
                    try:
                        process.kill()
                        process.wait(timeout=1.0)
                    except (OSError, subprocess.TimeoutExpired):
                        pass
            except OSError:
                pass
        # The stdio launcher can exit before a child it spawned. A remembered
        # Windows job/POSIX group remains valid after that parent-exit window,
        # so always retire the attachment even when poll() is already non-None.
        if not tree_retired:
            terminate_attached_process_tree(process, timeout=3.0)
        for thread in (self._reader, self._stderr_reader):
            if thread is not None and thread is not threading.current_thread():
                thread.join(timeout=0.5)
        for stream in (process.stdout, process.stderr):
            if stream is not None:
                try:
                    stream.close()
                except OSError:
                    pass
        self._process = None
        self._reader = None
        self._stderr_reader = None
        self._tools = ()
        self._ready = False
        self._stopping = False

    def snapshot(self) -> dict:
        process = self._process
        running = process is not None and process.poll() is None
        with self._state_lock:
            error = self._last_error
            stderr_tail = list(self._stderr_tail)
        return {
            "configured": True,
            "running": running,
            "ready": running and self._ready,
            "pid": process.pid if running and process is not None else None,
            "tools": len(self._tools),
            "allow_mutations": self.config.allow_mutations,
            "last_error": error or None,
            "stderr_tail": stderr_tail,
            "started_at": self._started_at,
            "retired": self._retired,
        }


class UpstreamManager:
    def __init__(self, config_path: pathlib.Path):
        self.config_path = pathlib.Path(config_path)
        self._lock = threading.RLock()
        self._signature: tuple[int, int] | None | object = object()
        self._clients: dict[str, StdioMcpClient] = {}
        self._catalogs: dict[str, tuple[dict, ...]] = {}
        # Full (unfolded) discovered catalogues of compact servers, keyed by
        # server name — the local answer behind each `<ns>__tools` meta call.
        self._compact_raw: dict[str, tuple[dict, ...]] = {}
        self._active_servers: set[str] = set()
        self._discovery_locks: dict[str, threading.Lock] = {}
        self._catalog_revision = 0
        # A removed namespace remains owned until this proxy exits. This keeps a
        # stale external tool call from falling through to an editor tool with a
        # colliding name after a configuration reload.
        self._known_namespaces: set[str] = set()
        self._routes: Mapping[str, ToolRoute] = MappingProxyType({})
        self._config_error = ""
        self._generation = 0

    def _file_signature(self) -> tuple[int, int] | None:
        try:
            stat = self.config_path.stat()
            return stat.st_mtime_ns, stat.st_size
        except OSError:
            return None

    def reload_if_changed(self) -> None:
        with self._lock:
            # Read and compare the signature while holding the same lock that
            # installs it. Otherwise an older stat result can arrive after a
            # newer reload and unnecessarily retire the current clients.
            signature = self._file_signature()
            if signature == self._signature:
                return
            old_clients = list(self._clients.values())
            try:
                servers = load_config(self.config_path)
                config_error = ""
            except UpstreamError as exc:
                servers = {}
                config_error = _redact(str(exc))
            # No newly configured child may start until every replaced child has
            # released its stdio/port/bridge resources. Public manager operations
            # share this lock, so the transition is invisible and non-overlapping.
            for client in old_clients:
                client.retire()
            self._clients = {}
            self._catalogs = {}
            self._compact_raw = {}
            self._active_servers = set()
            self._discovery_locks = {}
            self._routes = MappingProxyType({})
            self._signature = signature
            self._generation += 1
            self._clients = {
                name: StdioMcpClient(server) for name, server in servers.items()
            }
            self._discovery_locks = {
                name: threading.Lock() for name in self._clients
            }
            self._known_namespaces.update(
                server.namespace for server in servers.values()
            )
            self._config_error = config_error

    @staticmethod
    def _compact_catalog_and_routes(
        server_name: str,
        client: StdioMcpClient,
        tools: tuple[dict, ...],
        generation: int,
        catalog_revision: int,
    ) -> tuple[tuple[dict, ...], dict[str, ToolRoute], tuple[dict, ...]]:
        namespace = client.config.namespace
        raw_entries: list[dict] = []
        for tool in tools:
            upstream_name = tool["name"]
            raw_entries.append(
                {
                    "name": upstream_name,
                    "title": tool.get("title", ""),
                    "description": tool.get("description", ""),
                    "inputSchema": tool.get(
                        "inputSchema", {"type": "object", "properties": {}}
                    ),
                    "annotations": tool.get("annotations", {}),
                    "mutating": upstream_name in client.config.mutating_tools,
                }
            )
        any_mutating = any(entry["mutating"] for entry in raw_entries)
        list_name = f"{namespace}__{COMPACT_LIST_TOOL}"
        call_name = f"{namespace}__{COMPACT_CALL_TOOL}"
        exposed = (
            {
                "name": list_name,
                "title": f"{server_name} tool catalogue",
                "description": (
                    f"List the {len(raw_entries)} tools of the external MCP server "
                    f"'{server_name}' (names, descriptions, input schemas). Call this "
                    f"first, then run a tool with {call_name}."
                ),
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "name": {
                            "type": "string",
                            "description": "Optional case-insensitive substring filter on tool names.",
                        }
                    },
                    "additionalProperties": False,
                },
                "annotations": {"readOnlyHint": True},
            },
            {
                "name": call_name,
                "title": f"{server_name} tool call",
                "description": (
                    f"Run one tool of the external MCP server '{server_name}' by name. "
                    f"Discover names and argument schemas with {list_name} first."
                ),
                "inputSchema": {
                    "type": "object",
                    "required": ["tool"],
                    "properties": {
                        "tool": {
                            "type": "string",
                            "description": "The upstream tool name (without namespace prefix).",
                        },
                        "arguments": {
                            "type": "object",
                            "description": "Arguments for that tool, per its input schema.",
                        },
                    },
                    "additionalProperties": False,
                },
                "annotations": {},
            },
        )
        routes = {
            list_name: ToolRoute(
                server_name=server_name,
                upstream_name=COMPACT_LIST_TOOL,
                mutating=False,
                generation=generation,
                catalog_revision=catalog_revision,
            ),
            # Conservatively mutating when ANY folded tool mutates: the Safe
            # endpoint must not reach a mutation through the meta tool, and the
            # per-call inner check below still blocks read-only violations.
            call_name: ToolRoute(
                server_name=server_name,
                upstream_name=COMPACT_CALL_TOOL,
                mutating=any_mutating,
                generation=generation,
                catalog_revision=catalog_revision,
            ),
        }
        return exposed, routes, tuple(raw_entries)

    @staticmethod
    def _catalog_and_routes(
        server_name: str,
        client: StdioMcpClient,
        tools: tuple[dict, ...],
        generation: int,
        catalog_revision: int,
    ) -> tuple[tuple[dict, ...], dict[str, ToolRoute], tuple[dict, ...]]:
        if getattr(client.config, "compact_catalog", False):
            return UpstreamManager._compact_catalog_and_routes(
                server_name, client, tools, generation, catalog_revision
            )
        exposed: list[dict] = []
        routes: dict[str, ToolRoute] = {}
        for tool in tools:
            upstream_name = tool["name"]
            exposed_name = f"{client.config.namespace}__{upstream_name}"
            exposed.append(
                {
                    "name": exposed_name,
                    "title": tool.get("title", ""),
                    "description": tool.get("description", ""),
                    "inputSchema": tool.get(
                        "inputSchema", {"type": "object", "properties": {}}
                    ),
                    "annotations": tool.get("annotations", {}),
                    **(
                        {"outputSchema": tool["outputSchema"]}
                        if isinstance(tool.get("outputSchema"), dict)
                        else {}
                    ),
                }
            )
            routes[exposed_name] = ToolRoute(
                server_name=server_name,
                upstream_name=upstream_name,
                mutating=upstream_name in client.config.mutating_tools,
                generation=generation,
                catalog_revision=catalog_revision,
            )
        return tuple(exposed), routes, ()

    def _publish_catalog_locked(
        self,
        server_name: str,
        catalog: tuple[dict, ...],
        routes: dict[str, ToolRoute],
        compact_raw: tuple[dict, ...] = (),
    ) -> None:
        if compact_raw:
            self._compact_raw[server_name] = compact_raw
        else:
            self._compact_raw.pop(server_name, None)
        self._catalogs[server_name] = catalog
        merged = {
            name: route
            for name, route in self._routes.items()
            if route.server_name != server_name
        }
        merged.update(routes)
        self._routes = MappingProxyType(merged)

    def _drop_catalog_locked(self, server_name: str) -> None:
        self._catalogs.pop(server_name, None)
        self._compact_raw.pop(server_name, None)
        self._routes = MappingProxyType(
            {
                name: route
                for name, route in self._routes.items()
                if route.server_name != server_name
            }
        )

    def _prune_stale_catalogs_locked(self) -> None:
        for server_name in tuple(self._catalogs):
            client = self._clients.get(server_name)
            if client is None or not client.is_ready():
                self._drop_catalog_locked(server_name)

    def _discover_client(
        self,
        server_name: str,
        client: StdioMcpClient,
        generation: int,
    ) -> bool:
        with self._lock:
            discovery_lock = self._discovery_locks.get(server_name)
        if discovery_lock is None:
            return False
        # A single client can be rediscovered from tools/list, direct calls, and
        # explicit activation. Serialize the complete discover-and-publish epoch
        # so an older process result cannot overwrite a newer catalogue.
        with discovery_lock:
            with self._lock:
                current = (
                    generation == self._generation
                    and self._clients.get(server_name) is client
                    and self._discovery_locks.get(server_name) is discovery_lock
                )
                if not current:
                    return False
                # Another caller may have completed discovery while this caller
                # waited for the per-server lock. Reuse that exact live session.
                # If its process died, remove every route before client.tools()
                # is allowed to start a replacement process. A validated call
                # can then never use an old catalogue revision against the new
                # stdio session during the discover-before-publish window.
                if server_name in self._catalogs:
                    if client.is_ready():
                        self._active_servers.add(server_name)
                        return True
                    self._drop_catalog_locked(server_name)
            try:
                tools = client.tools()
            except UpstreamError as exc:
                client._set_error(str(exc))
                with self._lock:
                    if (
                        generation == self._generation
                        and self._clients.get(server_name) is client
                        and self._discovery_locks.get(server_name) is discovery_lock
                    ):
                        self._drop_catalog_locked(server_name)
                        raise
                return False
            with self._lock:
                current = (
                    generation == self._generation
                    and self._clients.get(server_name) is client
                    and self._discovery_locks.get(server_name) is discovery_lock
                )
                if not current:
                    return False
                self._catalog_revision += 1
                catalog, routes, compact_raw = self._catalog_and_routes(
                    server_name,
                    client,
                    tools,
                    generation,
                    self._catalog_revision,
                )
                self._publish_catalog_locked(server_name, catalog, routes, compact_raw)
                self._active_servers.add(server_name)
                return True

    @staticmethod
    def _is_visible(
        server_name: str, client: StdioMcpClient, active: set[str]
    ) -> bool:
        return (
            getattr(client.config, "activation", "on_demand") == "eager"
            or server_name in active
        )

    def tools(self, *, allow_start: bool = True) -> list[dict]:
        """Return the published catalogue, optionally without starting children.

        Safe/read-only HTTP requests pass ``allow_start=False``.  This is a
        security boundary rather than a performance hint: project-local MCP
        configuration can name an arbitrary executable, so an unauthenticated
        catalogue request must never launch it.  Catalogues that a Full session
        already activated remain visible while their exact process is healthy.
        """
        for _attempt in range(3):
            self.reload_if_changed()
            with self._lock:
                generation = self._generation
                active = set(self._active_servers)
                clients = [
                    (name, client)
                    for name, client in self._clients.items()
                    if self._is_visible(name, client, active)
                ]
                self._prune_stale_catalogs_locked()
                undiscovered = [
                    (name, client)
                    for name, client in clients
                    if name not in self._catalogs
                ]
            if allow_start:
                for server_name, client in undiscovered:
                    try:
                        self._discover_client(server_name, client, generation)
                    except UpstreamError:
                        continue
            with self._lock:
                current = (
                    generation == self._generation
                    and all(self._clients.get(name) is client for name, client in clients)
                )
                if current:
                    visible_names = {
                        name
                        for name, client in self._clients.items()
                        if self._is_visible(name, client, self._active_servers)
                    }
                    return [
                        tool
                        for name in sorted(visible_names)
                        for tool in self._catalogs.get(name, ())
                    ]
        return []

    def _configured_target_locked(
        self, exposed_name: str
    ) -> tuple[str, StdioMcpClient, str] | None:
        candidates = []
        for server_name, client in self._clients.items():
            prefix = f"{client.config.namespace}__"
            if exposed_name.startswith(prefix):
                candidates.append(
                    (len(prefix), server_name, client, exposed_name[len(prefix):])
                )
        if not candidates:
            return None
        _length, server_name, client, upstream_name = max(
            candidates, key=lambda item: item[0]
        )
        return server_name, client, upstream_name

    def activate(
        self,
        namespace: str,
        *,
        exposed_name: str | None = None,
        require_read_only: bool = False,
    ) -> list[dict]:
        requested = str(namespace or "").strip().casefold()
        if not requested:
            raise UpstreamError("External MCP namespace is required")
        for _attempt in range(3):
            self.reload_if_changed()
            with self._lock:
                match = next(
                    (
                        (name, client)
                        for name, client in self._clients.items()
                        if client.config.namespace == requested
                    ),
                    None,
                )
                if match is None:
                    raise UpstreamError(f"Unknown external MCP namespace: {namespace}")
                server_name, client = match
                generation = self._generation
                if exposed_name is not None:
                    target = self._configured_target_locked(exposed_name)
                    if (
                        target is None
                        or target[0] != server_name
                        or target[1] is not client
                    ):
                        raise UpstreamError(
                            f"Unknown or stale external MCP tool: {exposed_name}"
                        )
                    upstream_name = target[2]
                    if not _is_compact_meta(client.config, upstream_name) and (
                        upstream_name not in client.config.allow_tools
                        or upstream_name in client.config.deny_tools
                    ):
                        raise UpstreamError(
                            f"External MCP tool is not allowed: {exposed_name}"
                        )
                    if (
                        require_read_only
                        and upstream_name in client.config.mutating_tools
                    ):
                        raise MutationDenied(
                            f"{exposed_name} is mutating and is blocked by the Safe/read-only endpoint"
                        )
                self._active_servers.add(server_name)
                catalog = self._catalogs.get(server_name)
                if catalog is not None and client.is_ready():
                    return list(catalog)
                if catalog is not None:
                    self._drop_catalog_locked(server_name)
            try:
                published = self._discover_client(server_name, client, generation)
            except UpstreamError:
                with self._lock:
                    current = (
                        generation == self._generation
                        and self._clients.get(server_name) is client
                    )
                    if (
                        current
                        and getattr(client.config, "activation", "on_demand")
                        == "on_demand"
                        and server_name not in self._catalogs
                    ):
                        self._active_servers.discard(server_name)
                if current:
                    raise
                continue
            if not published:
                continue
            with self._lock:
                if (
                    generation == self._generation
                    and self._clients.get(server_name) is client
                ):
                    return list(self._catalogs.get(server_name, ()))
        raise UpstreamError(
            f"External MCP namespace changed repeatedly while activating: {requested}"
        )

    def owns_namespace(self, exposed_name: str) -> bool:
        self.reload_if_changed()
        with self._lock:
            namespaces = set(self._known_namespaces)
        return any(exposed_name.startswith(f"{namespace}__") for namespace in namespaces)

    def route_for(
        self, exposed_name: str, *, require_read_only: bool = False
    ) -> ToolRoute | None:
        """Return the authoritative route, activating only its exact server."""
        self.reload_if_changed()
        with self._lock:
            route = self._routes.get(exposed_name)
            if route is not None and route.generation != self._generation:
                route = None
            if route is not None:
                routed_client = self._clients.get(route.server_name)
                if routed_client is None or not routed_client.is_ready():
                    self._drop_catalog_locked(route.server_name)
                    route = None
            target = (
                self._configured_target_locked(exposed_name)
                if route is None
                else None
            )
            if target is not None:
                _server_name, client, upstream_name = target
                if not _is_compact_meta(client.config, upstream_name) and (
                    upstream_name not in client.config.allow_tools
                    or upstream_name in client.config.deny_tools
                ):
                    return None
                if require_read_only and upstream_name in client.config.mutating_tools:
                    raise MutationDenied(
                        f"{exposed_name} is mutating and is blocked by the Safe/read-only endpoint"
                    )
                namespace = client.config.namespace
            else:
                namespace = ""
        # Safe callers may reuse an already-published read-only route, but they
        # never authorize spawning or restarting the configured executable.
        if route is None and namespace and require_read_only:
            return None
        if route is None and namespace:
            try:
                self.activate(
                    namespace,
                    exposed_name=exposed_name,
                    require_read_only=require_read_only,
                )
            except MutationDenied:
                raise
            except UpstreamError:
                return None
            with self._lock:
                route = self._routes.get(exposed_name)
                if route is not None and route.generation != self._generation:
                    route = None
        if require_read_only and route is not None and route.mutating:
            raise MutationDenied(
                f"{exposed_name} is mutating and is blocked by the Safe/read-only endpoint"
            )
        return route

    def definition_for(
        self, exposed_name: str, *, require_read_only: bool = False
    ) -> ToolDefinition | None:
        for _attempt in range(2):
            route = self.route_for(exposed_name, require_read_only=require_read_only)
            if route is None:
                return None
            with self._lock:
                if (
                    route.generation != self._generation
                    or self._routes.get(exposed_name) != route
                ):
                    continue
                definition = next(
                    (
                        tool
                        for tool in self._catalogs.get(route.server_name, ())
                        if tool.get("name") == exposed_name
                    ),
                    None,
                )
                if definition is None:
                    return None
                return ToolDefinition(definition, route.catalog_revision)
        return None

    def call_tool(
        self,
        exposed_name: str,
        arguments: dict,
        *,
        require_read_only: bool = False,
        request_id: str | None = None,
        expected_catalog_revision: int | None = None,
    ) -> dict:
        if require_read_only:
            self.reload_if_changed()
            with self._lock:
                target = self._configured_target_locked(exposed_name)
                if target is not None:
                    _server_name, client, upstream_name = target
                    if upstream_name in client.config.mutating_tools:
                        raise MutationDenied(
                            f"{exposed_name} is mutating and is blocked by the Safe/read-only endpoint"
                        )
        for _attempt in range(2):
            route = self.route_for(
                exposed_name,
                require_read_only=require_read_only,
            )
            if route is None:
                break
            with self._lock:
                client = self._clients.get(route.server_name)
                current = (
                    route.generation == self._generation
                    and self._routes.get(exposed_name) == route
                )
                if not current or client is None:
                    continue
                if (
                    expected_catalog_revision is not None
                    and route.catalog_revision != expected_catalog_revision
                ):
                    raise UpstreamError(
                        "External MCP tool definition changed after validation; "
                        "refresh tools/list and retry the call"
                    )
                if require_read_only and route.mutating:
                    raise MutationDenied(
                        f"{exposed_name} is mutating and is blocked by the Safe/read-only endpoint"
                    )
                if _is_compact_meta(client.config, route.upstream_name):
                    if route.upstream_name == COMPACT_LIST_TOOL:
                        return self._compact_list_result_locked(
                            route.server_name, arguments
                        )
                    return self._compact_call_locked(
                        client,
                        route,
                        arguments,
                        require_read_only=require_read_only,
                        request_id=request_id,
                    )
                stable_request_id = request_id
                if route.mutating and stable_request_id is None:
                    stable_request_id = f"revvy_{secrets.token_hex(16)}"
                if route.mutating:
                    return client.call_tool(
                        route.upstream_name,
                        arguments,
                        request_id=stable_request_id,
                        poll_with=self._poll_tool_for_route_locked(route),
                        allow_restart=False,
                    )
                return client.call_tool(
                    route.upstream_name,
                    arguments,
                    allow_restart=False,
                )
        raise UpstreamError(self._unroutable_reason(exposed_name))

    def _compact_list_result_locked(self, server_name: str, arguments: dict) -> dict:
        """Answer a `<ns>__tools` meta call locally from the stored catalogue."""

        entries = self._compact_raw.get(server_name, ())
        filter_text = str((arguments or {}).get("name", "") or "").strip().casefold()
        tools = []
        for entry in entries:
            if filter_text and filter_text not in str(entry["name"]).casefold():
                continue
            tools.append(
                {
                    "name": entry["name"],
                    "title": entry["title"],
                    "description": entry["description"],
                    "inputSchema": entry["inputSchema"],
                    "annotations": entry["annotations"],
                    "read_only": not entry["mutating"],
                }
            )
        payload = {"server": server_name, "count": len(tools), "tools": tools}
        return {
            "content": [
                {
                    "type": "text",
                    "text": json.dumps(payload, ensure_ascii=False, indent=1),
                }
            ],
            "isError": False,
        }

    def _compact_call_locked(
        self,
        client: StdioMcpClient,
        route: ToolRoute,
        arguments: dict,
        *,
        require_read_only: bool,
        request_id: str | None,
    ) -> dict:
        """Route a `<ns>__call` meta call to the named folded tool.

        client.call_tool re-validates the inner name against the discovered
        catalogue and enforces allow_mutations; the checks here add the config
        allow/deny gate and the Safe/read-only gate at the INNER tool level.
        """

        args = arguments or {}
        inner_name = args.get("tool")
        inner_args = args.get("arguments") or {}
        if not isinstance(inner_name, str) or not inner_name.strip():
            raise UpstreamError(
                f"{client.config.namespace}__{COMPACT_CALL_TOOL} requires a 'tool' string argument"
            )
        inner_name = inner_name.strip()
        if not isinstance(inner_args, dict):
            raise UpstreamError(
                f"{client.config.namespace}__{COMPACT_CALL_TOOL} 'arguments' must be an object"
            )
        if (
            inner_name not in client.config.allow_tools
            or inner_name in client.config.deny_tools
        ):
            raise UpstreamError(
                f"External MCP tool is not allowed: {client.config.namespace}__{inner_name}"
            )
        if require_read_only and inner_name in client.config.mutating_tools:
            raise MutationDenied(
                f"{client.config.namespace}__{inner_name} is mutating and is "
                "blocked by the Safe/read-only endpoint"
            )
        if inner_name in client.config.mutating_tools:
            stable_request_id = request_id
            if stable_request_id is None:
                stable_request_id = f"revvy_{secrets.token_hex(16)}"
            return client.call_tool(
                inner_name,
                inner_args,
                request_id=stable_request_id,
                poll_with=self._compact_poll_hint_locked(route.server_name, client),
                allow_restart=False,
            )
        return client.call_tool(inner_name, inner_args, allow_restart=False)

    def _compact_poll_hint_locked(
        self, server_name: str, client: StdioMcpClient
    ) -> str | None:
        """Name a job-status poll path that survives the catalogue folding."""

        entries = self._compact_raw.get(server_name, ())
        names = [
            str(entry["name"])
            for entry in entries
            if not entry["mutating"]
            and (
                entry["name"] in {"blender_job_status", "job_status"}
                or str(entry["name"]).endswith("_job_status")
            )
        ]
        if not names:
            return None
        names.sort(
            key=lambda name: (
                name != "blender_job_status",
                name != "job_status",
                name,
            )
        )
        return (
            f'{client.config.namespace}__{COMPACT_CALL_TOOL} {{"tool": "{names[0]}"}}'
        )

    def _unroutable_reason(self, exposed_name: str) -> str:
        """Say why a call could not be routed, instead of blaming the tool name.

        A configured upstream that failed to start owns the real explanation.
        Reporting "unknown tool" for it leaves a caller unable to tell a typo from
        a dead bridge, and the true error only reachable through health_snapshot().
        """
        with self._lock:
            config_error = self._config_error
            client = next(
                (
                    candidate
                    for candidate in self._clients.values()
                    if exposed_name.startswith(f"{candidate.config.namespace}__")
                ),
                None,
            )
        if client is None:
            if config_error:
                return f"External MCP upstream configuration is unusable: {config_error}"
            return f"Unknown or stale external MCP tool: {exposed_name}"
        reason = client.last_error or "the upstream published no matching tool"
        return (
            f"External MCP upstream {client.config.namespace!r} did not publish "
            f"{exposed_name}: {reason}"
        )

    def _poll_tool_for_route_locked(self, route: ToolRoute) -> str | None:
        candidates = [
            (exposed_name, candidate)
            for exposed_name, candidate in self._routes.items()
            if candidate.server_name == route.server_name
            and not candidate.mutating
            and (
                candidate.upstream_name in {"blender_job_status", "job_status"}
                or candidate.upstream_name.endswith("_job_status")
            )
        ]
        if not candidates:
            return None
        candidates.sort(
            key=lambda item: (
                item[1].upstream_name != "blender_job_status",
                item[1].upstream_name != "job_status",
                item[0],
            )
        )
        return candidates[0][0]

    @property
    def routes(self) -> Mapping[str, ToolRoute]:
        with self._lock:
            return self._routes

    def health_snapshot(self) -> dict:
        self.reload_if_changed()
        with self._lock:
            self._prune_stale_catalogs_locked()
            clients = dict(self._clients)
            active = set(self._active_servers)
            visible = set(self._catalogs)
            config_error = self._config_error
        servers = {}
        for name, client in clients.items():
            snapshot = client.snapshot()
            activation = getattr(client.config, "activation", "on_demand")
            activated = activation == "eager" or name in active
            if snapshot.get("ready"):
                state = "ready"
            elif snapshot.get("running"):
                state = "starting"
            elif snapshot.get("last_error"):
                state = "failed"
            elif activated:
                state = "active"
            else:
                state = "dormant"
            servers[client.config.namespace] = {
                **snapshot,
                "activation": activation,
                "activated": activated,
                "state": state,
                "catalog_visible": name in visible,
                "configured_tools": len(client.config.allow_tools),
            }
        return {
            "config_path": str(self.config_path),
            "config_found": self.config_path.exists(),
            "config_error": config_error or None,
            "servers": servers,
        }

    def activation_snapshot(self) -> dict:
        snapshot = self.health_snapshot()
        servers = {}
        for namespace, server in snapshot["servers"].items():
            servers[namespace] = {
                key: server.get(key)
                for key in (
                    "activation",
                    "activated",
                    "state",
                    "running",
                    "ready",
                    "catalog_visible",
                    "tools",
                    "configured_tools",
                )
            }
        return {
            "success": not bool(snapshot.get("config_error")),
            "config_error": snapshot.get("config_error"),
            "servers": servers,
        }

    def shutdown(self) -> None:
        with self._lock:
            clients = list(self._clients.values())
            self._clients = {}
            self._catalogs = {}
            self._compact_raw = {}
            self._active_servers = set()
            self._discovery_locks = {}
            self._routes = MappingProxyType({})
            self._generation += 1
        for client in clients:
            client.retire()
