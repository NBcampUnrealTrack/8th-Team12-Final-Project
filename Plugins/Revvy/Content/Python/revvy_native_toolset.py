"""Optional UE 5.8 ToolsetRegistry bridge for Revvy.

The official Unreal MCP server is intentionally unauthenticated in UE 5.8.  Revvy
must therefore not expose its Full-access dispatcher through a Python toolset:
doing so would turn an unauthenticated official-MCP connection into an implicit
Revvy capability.

This module provides a fail-closed adapter with three properties:

* discovery prefers the current Editor instance's in-process catalogue, falls
  back to Revvy's generated manifest for compatibility, and includes only the
  explicit unauthenticated-native allow-list after read-only annotation checks;
* execution requires a native ``RevvyNativeToolsetBridgeService`` implementation
  that reuses Revvy's existing ReadOnly scope gate in-process;
* UE's ``FGlobalSandbox`` is accessed only through a future
  ``RevvySandboxBridgeService`` wrapper.  Sandbox mutation methods additionally
  require an opaque, one-time approval handle and are not registered as official
  MCP tools by this module.

There is deliberately no HTTP fallback.  Unreal Python Toolset calls run on the
game thread; synchronously calling Revvy's HTTP server from one would deadlock
when that server marshals the requested editor operation back to the game thread.

The module imports outside Unreal for unit testing.  Call
``register_native_toolset()`` from Revvy's editor bootstrap to opt in.

UE 5.8 marks both ToolsetRegistry and ModelContextProtocol ``NoRedist``.  Revvy
must not declare either as a packaged hard dependency or redistribute their
code; this adapter is discovered only when the user's engine installation
already exposes the APIs.
"""

from __future__ import annotations

import json
import os
import re
from pathlib import Path
from typing import Any, Iterable


_MAX_MANIFEST_BYTES = 8 * 1024 * 1024
_MAX_ARGUMENT_BYTES = 256 * 1024
_MAX_RESULT_BYTES = 4 * 1024 * 1024
_MAX_LIST_RESULTS = 100

_TARGET_RE = re.compile(r"^[A-Za-z0-9_][A-Za-z0-9_.:-]{0,199}$")
_SANDBOX_NAME_RE = re.compile(r"^[A-Za-z0-9_][A-Za-z0-9_.-]{0,63}$")

NATIVE_BRIDGE_SERVICE = "RevvyNativeToolsetBridgeService"
SANDBOX_BRIDGE_SERVICE = "RevvySandboxBridgeService"

# The official MCP endpoint may be unauthenticated. This allow-list is mirrored
# and authoritatively enforced in FMCPServer, so ReadOnly alone never implies
# that a tool is safe to expose without Revvy authentication.
UNAUTHENTICATED_NATIVE_TOOLS = frozenset(
    {
        "discover_python_class",
        "discover_python_function",
        "discover_python_module",
        "list_python_subsystems",
        # Same exposure class as discover_python_class: reflection over loaded
        # types (native C++ view), no project file contents, no logs, read-only.
        "lookup_class",
    }
)

# Preserve registrations across importlib.reload so a live Editor does not lose
# the class/callback handles and leave a stale Toolset behind.
_TOOLSET_CLASS: type | None = globals().get("_TOOLSET_CLASS")
_TOOLSET_REGISTRY: Any = globals().get("_TOOLSET_REGISTRY")
_SHUTDOWN_CALLBACK_HANDLE: Any = globals().get("_SHUTDOWN_CALLBACK_HANDLE")
_SHUTDOWN_UNREAL_MODULE: Any = globals().get("_SHUTDOWN_UNREAL_MODULE")


class NativeScopeDenied(PermissionError):
    """The action is read-only but not safe for an unauthenticated native client."""


def _default_manifest_path() -> Path:
    """Return the path used by ``FMCPServer::ExportToolManifest``."""
    state_override = os.environ.get("REVVY_STATE_DIR", "").strip()
    if state_override:
        return Path(state_override) / "tools-manifest.json"
    try:
        import unreal  # type: ignore[import-not-found]

        project_dir = str(unreal.Paths.project_dir() or "").strip()
        if project_dir:
            return Path(project_dir).resolve() / ".revvy" / "tools-manifest.json"
    except (AttributeError, ImportError, OSError):
        pass
    app_data = os.environ.get("APPDATA", "").strip()
    if app_data:
        return Path(app_data) / "Revvy" / "tools-manifest.json"
    xdg_state_home = os.environ.get("XDG_STATE_HOME", "").strip()
    if xdg_state_home:
        return Path(xdg_state_home) / "Revvy" / "tools-manifest.json"
    return Path.home() / ".local" / "state" / "Revvy" / "tools-manifest.json"


def _json_text(payload: dict[str, Any]) -> str:
    return json.dumps(payload, ensure_ascii=False, separators=(",", ":"))


def _error(code: str, message: str, **details: Any) -> dict[str, Any]:
    payload: dict[str, Any] = {
        "success": False,
        "error": {"code": code, "message": message},
    }
    if details:
        payload["error"]["details"] = details
    return payload


def _success(**payload: Any) -> dict[str, Any]:
    return {"success": True, **payload}


def _decode_bridge_result(value: Any) -> Any:
    """Decode a JSON FString returned by a native bridge without requiring JSON."""
    if isinstance(value, str):
        encoded = value.encode("utf-8")
        if len(encoded) > _MAX_RESULT_BYTES:
            raise ValueError("Native bridge result exceeds the 4 MiB limit")
        try:
            return json.loads(value)
        except json.JSONDecodeError:
            return value
    return value


def _parse_object_json(arguments_json: str) -> dict[str, Any]:
    if not isinstance(arguments_json, str):
        raise ValueError("arguments_json must be a JSON string")
    if len(arguments_json.encode("utf-8")) > _MAX_ARGUMENT_BYTES:
        raise ValueError("arguments_json exceeds the 256 KiB limit")
    def reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, item in pairs:
            if key in result:
                raise ValueError(f"arguments_json contains duplicate key: {key}")
            result[key] = item
        return result

    try:
        value = json.loads(
            arguments_json,
            object_pairs_hook=reject_duplicate_keys,
        )
    except json.JSONDecodeError as exc:
        raise ValueError(f"arguments_json is invalid JSON: {exc.msg}") from exc
    except RecursionError as exc:
        raise ValueError("arguments_json nesting is too deep") from exc
    if not isinstance(value, dict):
        raise ValueError("arguments_json must decode to an object")
    return value


def _normalize_target(value: str) -> str:
    target = str(value or "").strip()
    if not _TARGET_RE.fullmatch(target):
        raise ValueError(
            "action must be 1-200 characters using letters, digits, '_', '.', ':', or '-'"
        )
    return target


def _read_only_actions(tool: dict[str, Any]) -> tuple[str, ...]:
    annotations = tool.get("annotations")
    if not isinstance(annotations, dict):
        return ()
    values = annotations.get("readOnlyActions")
    if not isinstance(values, list):
        return ()
    result: list[str] = []
    for value in values:
        if isinstance(value, str):
            normalized = value.strip().lower()
            if normalized and normalized not in result:
                result.append(normalized)
    return tuple(result)


def _is_fully_read_only(tool: dict[str, Any]) -> bool:
    annotations = tool.get("annotations")
    return bool(
        isinstance(annotations, dict)
        and annotations.get("readOnlyHint") is True
        and annotations.get("destructiveHint") is not True
    )


class ReadOnlyToolCatalog:
    """Read and filter Revvy's generated MCP manifest.

    MCP annotations are treated as an allow-list.  Missing or malformed
    annotations are unsafe, matching Revvy's current server behavior.
    """

    def __init__(
        self,
        manifest_path: str | os.PathLike[str] | None = None,
        *,
        manifest_provider: Any = None,
        allowed_tools: Iterable[str] | None = None,
    ):
        self.manifest_path = (
            Path(manifest_path) if manifest_path is not None else _default_manifest_path()
        )
        self.manifest_provider = manifest_provider
        self.allowed_tools = frozenset(
            str(name).strip()
            for name in (
                UNAUTHENTICATED_NATIVE_TOOLS
                if allowed_tools is None
                else allowed_tools
            )
            if str(name).strip()
        )

    def _load(self) -> list[dict[str, Any]]:
        provider_error: Exception | None = None
        if callable(self.manifest_provider):
            try:
                raw = self.manifest_provider()
                if not isinstance(raw, str):
                    raise ValueError("in-process native catalogue must be a JSON string")
                if len(raw.encode("utf-8")) > _MAX_MANIFEST_BYTES:
                    raise ValueError("in-process native catalogue exceeds the 8 MiB limit")
                root = json.loads(raw)
                if isinstance(root, dict):
                    root = root.get("tools")
                if not isinstance(root, list):
                    raise ValueError("in-process native catalogue must contain a tool array")
                return [entry for entry in root if isinstance(entry, dict)]
            except Exception as exc:  # Unreal reflection exceptions vary by build.
                provider_error = exc

        try:
            size = self.manifest_path.stat().st_size
        except OSError as exc:
            if provider_error is not None:
                raise ValueError(
                    "Revvy tool catalogue is unavailable in-process "
                    f"({provider_error}) and on disk at {self.manifest_path}"
                ) from exc
            raise FileNotFoundError(
                f"Revvy tool manifest is unavailable at {self.manifest_path}"
            ) from exc
        if size > _MAX_MANIFEST_BYTES:
            raise ValueError("Revvy tool manifest exceeds the 8 MiB limit")
        try:
            root = json.loads(self.manifest_path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, json.JSONDecodeError) as exc:
            raise ValueError(f"Revvy tool manifest could not be read: {exc}") from exc
        if isinstance(root, dict):
            root = root.get("tools")
        if not isinstance(root, list):
            raise ValueError("Revvy tool manifest must contain a tool array")
        return [entry for entry in root if isinstance(entry, dict)]

    def _is_allowed(self, tool: dict[str, Any]) -> bool:
        name = tool.get("name")
        annotations = tool.get("annotations")
        return bool(
            isinstance(name, str)
            and name in self.allowed_tools
            and isinstance(annotations, dict)
            and annotations.get("destructiveHint") is not True
            and annotations.get("openWorldHint") is not True
        )

    def _safe_summary(self, tool: dict[str, Any]) -> dict[str, Any] | None:
        name = tool.get("name")
        if not isinstance(name, str) or not name:
            return None
        if not self._is_allowed(tool):
            return None
        fully_read_only = _is_fully_read_only(tool)
        actions = _read_only_actions(tool)
        if not fully_read_only and not actions:
            return None
        return {
            "name": name,
            "description": str(tool.get("description") or ""),
            "fully_read_only": fully_read_only,
            "read_only_actions": list(actions),
        }

    def list_tools(self, query: str = "") -> list[dict[str, Any]]:
        needle = str(query or "").strip().lower()
        result: list[dict[str, Any]] = []
        for tool in self._load():
            summary = self._safe_summary(tool)
            if summary is None:
                continue
            haystack = " ".join(
                (
                    summary["name"],
                    summary["description"],
                    " ".join(summary["read_only_actions"]),
                )
            ).lower()
            if needle and needle not in haystack:
                continue
            result.append(summary)
        result.sort(key=lambda entry: entry["name"].lower())
        return result[:_MAX_LIST_RESULTS]

    def resolve(
        self,
        target: str,
        arguments: dict[str, Any] | None = None,
    ) -> tuple[dict[str, Any], str | None, dict[str, Any]]:
        """Resolve ``tool`` or ``tool.read_only_action`` and enforce its scope."""
        normalized_target = _normalize_target(target)
        tools = {
            str(tool.get("name")): tool
            for tool in self._load()
            if isinstance(tool.get("name"), str)
        }

        tool = tools.get(normalized_target)
        requested_action: str | None = None
        if tool is None and "." in normalized_target:
            tool_name, requested_action = normalized_target.rsplit(".", 1)
            tool = tools.get(tool_name)
        else:
            tool_name = normalized_target

        if tool is None:
            raise LookupError(f"Unknown Revvy action: {normalized_target}")

        if not self._is_allowed(tool):
            raise NativeScopeDenied(
                f"{tool_name} is outside the unauthenticated native allow-list"
            )

        fully_read_only = _is_fully_read_only(tool)
        allowed_actions = _read_only_actions(tool)
        if not fully_read_only and not allowed_actions:
            raise PermissionError(f"{tool_name} is not explicitly read-only")

        resolved_arguments = dict(arguments or {})
        action_keys = [
            key
            for key in resolved_arguments
            if isinstance(key, str) and key.lower() == "action"
        ]
        if len(action_keys) > 1:
            raise ValueError("arguments_json contains ambiguous action keys")
        argument_action = (
            resolved_arguments.pop(action_keys[0]) if action_keys else None
        )
        if requested_action is not None:
            requested_action = requested_action.strip().lower()
            if argument_action is not None and str(argument_action).strip().lower() != requested_action:
                raise ValueError("Action suffix conflicts with arguments_json.action")
            resolved_arguments["action"] = requested_action
        elif argument_action is not None:
            requested_action = str(argument_action).strip().lower()
            resolved_arguments["action"] = requested_action

        if not fully_read_only:
            if not requested_action:
                raise PermissionError(
                    f"{tool_name} requires one of its explicit read-only actions"
                )
            if requested_action not in allowed_actions:
                raise PermissionError(
                    f"{tool_name}.{requested_action} requires Revvy Full access"
                )

        return tool, requested_action, resolved_arguments

    def describe(self, target: str) -> dict[str, Any]:
        tool, action, _ = self.resolve(target)
        definition = dict(tool)
        allowed_actions = list(_read_only_actions(tool))
        definition["revvyNativeScope"] = {
            "fullyReadOnly": _is_fully_read_only(tool),
            "readOnlyActions": allowed_actions,
        }
        if action is not None:
            definition["revvyNativeScope"]["selectedAction"] = action
            schema = definition.get("inputSchema")
            if isinstance(schema, dict):
                schema = json.loads(json.dumps(schema))
                properties = schema.get("properties")
                if isinstance(properties, dict):
                    action_schema = properties.get("action")
                    if isinstance(action_schema, dict):
                        action_schema["enum"] = [action]
                definition["inputSchema"] = schema
        return definition


class UnrealReadOnlyDispatcher:
    """Thin adapter to a native bridge that must enforce Revvy ReadOnly scope."""

    def __init__(self, unreal_module: Any):
        self._unreal = unreal_module

    @property
    def service(self) -> Any:
        return getattr(self._unreal, NATIVE_BRIDGE_SERVICE, None)

    def is_available(self) -> bool:
        return callable(getattr(self.service, "call_read_only_action", None))

    def call_read_only_action(self, tool_name: str, arguments_json: str) -> Any:
        if not self.is_available():
            raise RuntimeError(
                f"unreal.{NATIVE_BRIDGE_SERVICE}.call_read_only_action is unavailable"
            )
        return self.service.call_read_only_action(tool_name, arguments_json)

    def get_read_only_catalog_json(self) -> str:
        method = getattr(self.service, "get_read_only_catalog_json", None)
        if not callable(method):
            raise RuntimeError(
                f"unreal.{NATIVE_BRIDGE_SERVICE}.get_read_only_catalog_json "
                "is unavailable"
            )
        return method()


class UnrealSandboxBackend:
    """Adapter contract for the small C++ wrapper required by ``FGlobalSandbox``."""

    def __init__(self, unreal_module: Any):
        self._unreal = unreal_module

    @property
    def service(self) -> Any:
        return getattr(self._unreal, SANDBOX_BRIDGE_SERVICE, None)

    def is_available(self) -> bool:
        required = ("get_sandbox_status", "get_sandbox_changes")
        return self.service is not None and all(
            callable(getattr(self.service, name, None)) for name in required
        )

    def get_status(self) -> Any:
        if not self.is_available():
            raise RuntimeError(f"unreal.{SANDBOX_BRIDGE_SERVICE} is unavailable")
        return self.service.get_sandbox_status()

    def get_changes(self) -> Any:
        if not self.is_available():
            raise RuntimeError(f"unreal.{SANDBOX_BRIDGE_SERVICE} is unavailable")
        return self.service.get_sandbox_changes()

    def begin_approved(
        self, name: str, description: str, approval_handle: str
    ) -> Any:
        method = getattr(self.service, "begin_approved_sandbox", None)
        if not callable(method):
            raise RuntimeError("Approved sandbox begin is unavailable")
        return method(name, description, approval_handle)

    def persist_approved(
        self, files_json: str, approval_handle: str
    ) -> Any:
        method = getattr(self.service, "persist_approved_sandbox", None)
        if not callable(method):
            raise RuntimeError("Approved sandbox persist is unavailable")
        return method(files_json, approval_handle)

    def discard_approved(
        self, files_json: str, approval_handle: str
    ) -> Any:
        method = getattr(self.service, "discard_approved_sandbox", None)
        if not callable(method):
            raise RuntimeError("Approved sandbox discard is unavailable")
        return method(files_json, approval_handle)

    def leave_approved(self, approval_handle: str) -> Any:
        method = getattr(self.service, "leave_approved_sandbox", None)
        if not callable(method):
            raise RuntimeError("Approved sandbox leave is unavailable")
        return method(approval_handle)


class RevvyNativeFacade:
    """Pure-Python behavior exposed through the optional native Toolset."""

    def __init__(
        self,
        catalog: ReadOnlyToolCatalog,
        dispatcher: Any = None,
        sandbox: Any = None,
    ):
        self.catalog = catalog
        self.dispatcher = dispatcher
        self.sandbox = sandbox

    def list_actions(self, query: str = "") -> str:
        try:
            actions = self.catalog.list_tools(query)
            return _json_text(_success(count=len(actions), actions=actions))
        except (OSError, ValueError) as exc:
            return _json_text(_error("catalog_unavailable", str(exc)))

    def describe_action(self, action: str) -> str:
        try:
            return _json_text(_success(action=self.catalog.describe(action)))
        except LookupError as exc:
            return _json_text(_error("unknown_action", str(exc)))
        except NativeScopeDenied as exc:
            return _json_text(_error("native_scope_denied", str(exc)))
        except PermissionError as exc:
            return _json_text(_error("full_access_required", str(exc)))
        except (OSError, ValueError) as exc:
            return _json_text(_error("invalid_request", str(exc)))

    def call_read_only_action(
        self, action: str, arguments_json: str = "{}"
    ) -> str:
        try:
            arguments = _parse_object_json(arguments_json)
            tool, selected_action, arguments = self.catalog.resolve(action, arguments)
            tool_name = str(tool["name"])
        except LookupError as exc:
            return _json_text(_error("unknown_action", str(exc)))
        except NativeScopeDenied as exc:
            return _json_text(_error("native_scope_denied", str(exc)))
        except PermissionError as exc:
            return _json_text(_error("full_access_required", str(exc)))
        except (OSError, ValueError) as exc:
            return _json_text(_error("invalid_request", str(exc)))

        if self.dispatcher is None or not self.dispatcher.is_available():
            return _json_text(
                _error(
                    "native_bridge_unavailable",
                    "Read-only execution needs Revvy's in-process native bridge; "
                    "HTTP fallback is disabled to prevent game-thread deadlock.",
                    required_service=NATIVE_BRIDGE_SERVICE,
                )
            )

        compact_arguments = json.dumps(
            arguments, ensure_ascii=False, separators=(",", ":")
        )
        try:
            result = _decode_bridge_result(
                self.dispatcher.call_read_only_action(tool_name, compact_arguments)
            )
        except Exception as exc:  # Unreal reflection exceptions vary by engine build.
            return _json_text(_error("native_bridge_error", str(exc)))
        if isinstance(result, dict) and (
            result.get("success") is False or result.get("isError") is True
        ):
            return _json_text(
                {
                    "success": False,
                    "tool": tool_name,
                    "action": selected_action,
                    "result": result,
                }
            )
        return _json_text(
            _success(
                tool=tool_name,
                action=selected_action,
                result=result,
            )
        )

    def sandbox_status(self) -> str:
        return self._sandbox_read("status")

    def sandbox_changes(self) -> str:
        return self._sandbox_read("changes")

    def _sandbox_read(self, operation: str) -> str:
        if self.sandbox is None or not self.sandbox.is_available():
            return _json_text(
                _error(
                    "sandbox_bridge_unavailable",
                    "UE 5.8 FGlobalSandbox is C++-only and needs Revvy's native wrapper.",
                    required_service=SANDBOX_BRIDGE_SERVICE,
                )
            )
        try:
            raw = (
                self.sandbox.get_status()
                if operation == "status"
                else self.sandbox.get_changes()
            )
            return _json_text(_success(**{operation: _decode_bridge_result(raw)}))
        except Exception as exc:  # Unreal reflection exceptions vary by engine build.
            return _json_text(_error("sandbox_bridge_error", str(exc)))


class ApprovedSandboxController:
    """Internal mutation API; intentionally not registered with official MCP.

    The native service must validate that ``approval_handle`` is unexpired,
    one-time, and bound to the exact operation and arguments.  Python-side checks
    are defense in depth, not the authority boundary.
    """

    def __init__(self, sandbox: Any):
        self.sandbox = sandbox

    @staticmethod
    def _approval(value: str) -> str:
        handle = str(value or "")
        if not 16 <= len(handle) <= 512 or any(ch.isspace() for ch in handle):
            raise ValueError("approval_handle must be an opaque 16-512 character token")
        return handle

    @staticmethod
    def _files(values: Iterable[str]) -> list[str]:
        result: list[str] = []
        for value in values:
            path = str(value or "").strip()
            if not path or len(path) > 4096:
                raise ValueError("sandbox file paths must be non-empty and at most 4096 characters")
            if path not in result:
                result.append(path)
            if len(result) > 5000:
                raise ValueError("at most 5000 sandbox files may be approved at once")
        if not result:
            raise ValueError("an explicit non-empty sandbox file list is required")
        return result

    def begin(self, name: str, description: str, approval_handle: str) -> Any:
        sandbox_name = str(name or "").strip()
        if not _SANDBOX_NAME_RE.fullmatch(sandbox_name):
            raise ValueError(
                "sandbox name must be 1-64 characters using letters, digits, '_', '.', or '-'"
            )
        clean_description = str(description or "").strip()
        if len(clean_description) > 512:
            raise ValueError("sandbox description exceeds 512 characters")
        return _decode_bridge_result(
            self.sandbox.begin_approved(
                sandbox_name,
                clean_description,
                self._approval(approval_handle),
            )
        )

    def persist(
        self, files: Iterable[str], approval_handle: str
    ) -> Any:
        approved_files = self._files(files)
        return _decode_bridge_result(
            self.sandbox.persist_approved(
                json.dumps(approved_files, ensure_ascii=False, separators=(",", ":")),
                self._approval(approval_handle),
            )
        )

    def discard(
        self, files: Iterable[str], approval_handle: str
    ) -> Any:
        approved_files = self._files(files)
        return _decode_bridge_result(
            self.sandbox.discard_approved(
                json.dumps(approved_files, ensure_ascii=False, separators=(",", ":")),
                self._approval(approval_handle),
            )
        )

    def leave(self, approval_handle: str) -> Any:
        return _decode_bridge_result(
            self.sandbox.leave_approved(self._approval(approval_handle))
        )


def create_default_facade(
    unreal_module: Any,
    manifest_path: str | os.PathLike[str] | None = None,
) -> RevvyNativeFacade:
    dispatcher = UnrealReadOnlyDispatcher(unreal_module)
    return RevvyNativeFacade(
        ReadOnlyToolCatalog(
            manifest_path,
            manifest_provider=dispatcher.get_read_only_catalog_json,
        ),
        dispatcher,
        UnrealSandboxBackend(unreal_module),
    )


def _build_toolset_class(
    unreal_module: Any,
    toolset_module: Any,
    facade: RevvyNativeFacade,
) -> type:
    """Build the Python UClass using Epic's UE 5.8 decorator contract."""

    def list_actions(query: str = "") -> str:
        """List Revvy actions available to an unauthenticated read-only native session.

        Args:
            query: Optional case-insensitive name, description, or action filter.

        Returns:
            Compact JSON containing only explicitly read-only Revvy actions.
        """
        return facade.list_actions(query)

    def describe_action(action: str) -> str:
        """Describe one explicitly read-only Revvy tool or ``tool.action``.

        Args:
            action: Revvy tool name, optionally followed by a read-only action suffix.

        Returns:
            Compact JSON containing the filtered schema and safety scope.
        """
        return facade.describe_action(action)

    def call_read_only_action(
        action: str, arguments_json: str = "{}"
    ) -> str:
        """Call a Revvy action through its existing in-process ReadOnly scope gate.

        Args:
            action: Revvy tool name, optionally followed by a read-only action suffix.
            arguments_json: JSON object passed to the Revvy tool.

        Returns:
            Compact JSON containing the Revvy result or a fail-closed error.
        """
        return facade.call_read_only_action(action, arguments_json)

    def sandbox_status() -> str:
        """Return UE 5.8 global sandbox status through Revvy's read-only wrapper."""
        return facade.sandbox_status()

    def sandbox_changes() -> str:
        """List active UE 5.8 sandbox changes without persisting or discarding them."""
        return facade.sandbox_changes()

    attributes = {
        "__module__": __name__,
        "__doc__": (
            "Read-only Revvy interoperability tools for Unreal Engine 5.8. "
            "Full-access and sandbox mutation remain behind Revvy authorization."
        ),
        "list_actions": toolset_module.tool_call(staticmethod(list_actions)),
        "describe_action": toolset_module.tool_call(staticmethod(describe_action)),
        "call_read_only_action": toolset_module.tool_call(
            staticmethod(call_read_only_action)
        ),
        "sandbox_status": toolset_module.tool_call(staticmethod(sandbox_status)),
        "sandbox_changes": toolset_module.tool_call(staticmethod(sandbox_changes)),
    }
    python_class = type(
        "RevvyNativeToolset",
        (unreal_module.ToolsetDefinition,),
        attributes,
    )
    return unreal_module.uclass()(python_class)


def _on_python_shutdown() -> None:
    global _SHUTDOWN_CALLBACK_HANDLE, _SHUTDOWN_UNREAL_MODULE
    _SHUTDOWN_CALLBACK_HANDLE = None
    _SHUTDOWN_UNREAL_MODULE = None
    unregister_native_toolset(remove_shutdown_callback=False)


def _ensure_shutdown_callback(unreal_module: Any) -> None:
    global _SHUTDOWN_CALLBACK_HANDLE, _SHUTDOWN_UNREAL_MODULE
    if _SHUTDOWN_CALLBACK_HANDLE is not None:
        return
    register = getattr(unreal_module, "register_python_shutdown_callback", None)
    if not callable(register):
        return
    _SHUTDOWN_CALLBACK_HANDLE = register(_on_python_shutdown)
    _SHUTDOWN_UNREAL_MODULE = unreal_module


def _remove_shutdown_callback() -> None:
    global _SHUTDOWN_CALLBACK_HANDLE, _SHUTDOWN_UNREAL_MODULE
    handle = _SHUTDOWN_CALLBACK_HANDLE
    unreal_module = _SHUTDOWN_UNREAL_MODULE
    _SHUTDOWN_CALLBACK_HANDLE = None
    _SHUTDOWN_UNREAL_MODULE = None
    unregister = getattr(
        unreal_module,
        "unregister_python_shutdown_callback",
        None,
    )
    if handle is not None and callable(unregister):
        unregister(handle)


def register_native_toolset(
    unreal_module: Any = None,
    toolset_module: Any = None,
    facade: RevvyNativeFacade | None = None,
) -> dict[str, Any]:
    """Register Revvy's optional read-only Toolset with UE 5.8.

    Returns a structured status instead of raising so editor startup remains
    healthy when ToolsetRegistry is disabled, unavailable in a commandlet, or an
    older Unreal version is used.
    """
    global _TOOLSET_CLASS, _TOOLSET_REGISTRY

    if unreal_module is None:
        try:
            import unreal as unreal_module  # type: ignore[no-redef]
        except ImportError:
            return _error("unreal_unavailable", "The Unreal Python module is unavailable")
    if toolset_module is None:
        try:
            import toolset_registry as toolset_module  # type: ignore[no-redef]
        except ImportError:
            return _error(
                "toolset_registry_unavailable",
                "The UE 5.8 ToolsetRegistry Python package is unavailable",
            )

    registry = getattr(unreal_module, "ToolsetRegistry", None)
    if (
        registry is None
        or not hasattr(unreal_module, "ToolsetDefinition")
        or not callable(getattr(unreal_module, "uclass", None))
        or not callable(getattr(toolset_module, "tool_call", None))
    ):
        return _error(
            "toolset_registry_unavailable",
            "Required UE 5.8 ToolsetRegistry reflection APIs are unavailable",
        )
    try:
        if not registry.is_available():
            return _error(
                "toolset_registry_inactive",
                "ToolsetRegistry is inactive in this editor context",
            )
    except Exception as exc:
        return _error("toolset_registry_probe_failed", str(exc))

    if _TOOLSET_CLASS is None or _TOOLSET_REGISTRY is not registry:
        try:
            active_facade = facade or create_default_facade(unreal_module)
            _TOOLSET_CLASS = _build_toolset_class(
                unreal_module, toolset_module, active_facade
            )
            _TOOLSET_REGISTRY = registry
        except Exception as exc:
            _TOOLSET_CLASS = None
            _TOOLSET_REGISTRY = None
            return _error("toolset_build_failed", str(exc))

    try:
        registered = bool(registry.is_toolset_class_registered(_TOOLSET_CLASS))
        if not registered:
            registry.register_toolset_class(_TOOLSET_CLASS)
            registered = bool(
                registry.is_toolset_class_registered(_TOOLSET_CLASS)
            )
    except Exception as exc:
        return _error("toolset_registration_failed", str(exc))
    if not registered:
        _TOOLSET_CLASS = None
        _TOOLSET_REGISTRY = None
        return _error(
            "toolset_registration_rejected",
            "ToolsetRegistry did not retain the Revvy Toolset class",
        )

    try:
        _ensure_shutdown_callback(unreal_module)
    except Exception as exc:
        try:
            if registry.is_toolset_class_registered(_TOOLSET_CLASS):
                registry.unregister_toolset_class(_TOOLSET_CLASS)
        finally:
            _TOOLSET_CLASS = None
            _TOOLSET_REGISTRY = None
        return _error("toolset_shutdown_hook_failed", str(exc))

    return _success(
        registered=True,
        toolset_class=_TOOLSET_CLASS.__name__,
        access_scope="read_only",
        sandbox_mutation_exposed=False,
    )


def unregister_native_toolset(
    *,
    remove_shutdown_callback: bool = True,
) -> dict[str, Any]:
    """Unregister the optional Python Toolset, primarily for hot reload/tests."""
    global _TOOLSET_CLASS, _TOOLSET_REGISTRY

    if _TOOLSET_CLASS is None or _TOOLSET_REGISTRY is None:
        if remove_shutdown_callback:
            try:
                _remove_shutdown_callback()
            except Exception as exc:
                return _error("toolset_shutdown_hook_removal_failed", str(exc))
        return _success(unregistered=False)
    try:
        if _TOOLSET_REGISTRY.is_available() and _TOOLSET_REGISTRY.is_toolset_class_registered(
            _TOOLSET_CLASS
        ):
            _TOOLSET_REGISTRY.unregister_toolset_class(_TOOLSET_CLASS)
    except Exception as exc:
        return _error("toolset_unregistration_failed", str(exc))
    finally:
        _TOOLSET_CLASS = None
        _TOOLSET_REGISTRY = None
    if remove_shutdown_callback:
        try:
            _remove_shutdown_callback()
        except Exception as exc:
            return _error("toolset_shutdown_hook_removal_failed", str(exc))
    return _success(unregistered=True)


def native_bridge_contract() -> dict[str, Any]:
    """Document the minimal C++/Python integration expected by this module."""
    return {
        NATIVE_BRIDGE_SERVICE: {
            "get_read_only_catalog_json": ["return:FString"],
            "call_read_only_action": [
                "tool_name:FString",
                "arguments_json:FString",
                "return:FString",
            ],
            "security": (
                "Must enforce FMCPServer's explicit unauthenticated-native allow-list "
                "and EMCPAccessScope::ReadOnly gate; never expose read_logs, "
                "open-world tools, or FToolRegistry::ExecuteTool unchecked."
            ),
        },
        SANDBOX_BRIDGE_SERVICE: {
            "read_only": [
                "get_sandbox_status() -> FString",
                "get_sandbox_changes() -> FString",
            ],
            "approved_mutation": [
                "begin_approved_sandbox(name, description, approval_handle) -> FString",
                "persist_approved_sandbox(files_json, approval_handle) -> FString",
                "discard_approved_sandbox(files_json, approval_handle) -> FString",
                "leave_approved_sandbox(approval_handle) -> FString",
            ],
            "security": (
                "Mutation methods must consume an unexpired one-time approval "
                "bound to the exact operation and argument hash."
            ),
        },
    }
