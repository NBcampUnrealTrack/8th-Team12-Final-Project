from __future__ import annotations

import ctypes
import json
import os
import pathlib
import re
import secrets
import stat
import time
from ctypes import wintypes
from typing import Any, Mapping


SECURITY_DIR_ENV = "REVVY_SECURITY_DIR"
UE_CAPABILITY_FILE_ENV = "REVVY_UE_CAPABILITY_FILE"
UE_CAPABILITY_FILENAME = "ue-backend.json"
UE_TOKEN_FIELD = "ue_backend_token"
BRIDGE_AUTH_FILE_ENV = "REVVY_BRIDGE_AUTH_FILE"
BRIDGE_AUTH_FILENAME = "bridge-auth.json"
BRIDGE_HMAC_FIELD = "bridge_hmac_key"


class SecureStoreError(RuntimeError):
    pass


def security_dir(env: Mapping[str, str] | None = None) -> pathlib.Path:
    values = os.environ if env is None else env
    configured = str(values.get(SECURITY_DIR_ENV) or "").strip()
    if configured:
        return pathlib.Path(configured).expanduser().resolve()
    local_appdata = str(values.get("LOCALAPPDATA") or "").strip()
    if local_appdata:
        return pathlib.Path(local_appdata).expanduser() / "Revvy" / "Security"
    if os.name == "nt":
        return pathlib.Path.home() / "AppData" / "Local" / "Revvy" / "Security"
    base = str(values.get("XDG_STATE_HOME") or "").strip()
    if base:
        return pathlib.Path(base).expanduser() / "Revvy" / "Security"
    return pathlib.Path.home() / ".local" / "state" / "Revvy" / "Security"


def ue_capability_path(env: Mapping[str, str] | None = None) -> pathlib.Path:
    values = os.environ if env is None else env
    configured = str(values.get(UE_CAPABILITY_FILE_ENV) or "").strip()
    if configured:
        return pathlib.Path(configured).expanduser().resolve()
    return security_dir(values) / UE_CAPABILITY_FILENAME


def bridge_auth_path(env: Mapping[str, str] | None = None) -> pathlib.Path:
    values = os.environ if env is None else env
    configured = str(values.get(BRIDGE_AUTH_FILE_ENV) or "").strip()
    if configured:
        return pathlib.Path(configured).expanduser().resolve()
    return security_dir(values) / BRIDGE_AUTH_FILENAME


class _SidAndAttributes(ctypes.Structure):
    _fields_ = [("Sid", ctypes.c_void_p), ("Attributes", wintypes.DWORD)]


class _TokenUser(ctypes.Structure):
    _fields_ = [("User", _SidAndAttributes)]


def _current_windows_user_sid() -> str:
    advapi32 = ctypes.WinDLL("advapi32", use_last_error=True)
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    advapi32.OpenProcessToken.argtypes = [
        wintypes.HANDLE,
        wintypes.DWORD,
        ctypes.POINTER(wintypes.HANDLE),
    ]
    advapi32.OpenProcessToken.restype = wintypes.BOOL
    advapi32.GetTokenInformation.argtypes = [
        wintypes.HANDLE,
        ctypes.c_int,
        ctypes.c_void_p,
        wintypes.DWORD,
        ctypes.POINTER(wintypes.DWORD),
    ]
    advapi32.GetTokenInformation.restype = wintypes.BOOL
    advapi32.ConvertSidToStringSidW.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(wintypes.LPWSTR),
    ]
    advapi32.ConvertSidToStringSidW.restype = wintypes.BOOL
    kernel32.GetCurrentProcess.restype = wintypes.HANDLE
    kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel32.LocalFree.argtypes = [ctypes.c_void_p]
    kernel32.LocalFree.restype = ctypes.c_void_p

    token = wintypes.HANDLE()
    if not advapi32.OpenProcessToken(
        kernel32.GetCurrentProcess(), 0x0008, ctypes.byref(token)
    ):
        raise SecureStoreError(
            f"OpenProcessToken failed: {ctypes.WinError(ctypes.get_last_error())}"
        )
    try:
        required = wintypes.DWORD()
        advapi32.GetTokenInformation(token, 1, None, 0, ctypes.byref(required))
        if required.value <= 0:
            raise SecureStoreError(
                "GetTokenInformation did not return a TokenUser buffer size"
            )
        buffer = ctypes.create_string_buffer(required.value)
        if not advapi32.GetTokenInformation(
            token, 1, buffer, required.value, ctypes.byref(required)
        ):
            raise SecureStoreError(
                f"GetTokenInformation failed: {ctypes.WinError(ctypes.get_last_error())}"
            )
        user = ctypes.cast(buffer, ctypes.POINTER(_TokenUser)).contents
        sid_text = wintypes.LPWSTR()
        if not advapi32.ConvertSidToStringSidW(
            user.User.Sid, ctypes.byref(sid_text)
        ):
            raise SecureStoreError(
                f"ConvertSidToStringSid failed: {ctypes.WinError(ctypes.get_last_error())}"
            )
        try:
            return str(sid_text.value)
        finally:
            kernel32.LocalFree(ctypes.cast(sid_text, ctypes.c_void_p))
    finally:
        kernel32.CloseHandle(token)


def _set_windows_private_dacl(path: pathlib.Path, *, directory: bool) -> None:
    advapi32 = ctypes.WinDLL("advapi32", use_last_error=True)
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    advapi32.ConvertStringSecurityDescriptorToSecurityDescriptorW.argtypes = [
        wintypes.LPCWSTR,
        wintypes.DWORD,
        ctypes.POINTER(ctypes.c_void_p),
        ctypes.POINTER(wintypes.DWORD),
    ]
    advapi32.ConvertStringSecurityDescriptorToSecurityDescriptorW.restype = wintypes.BOOL
    advapi32.GetSecurityDescriptorDacl.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(wintypes.BOOL),
        ctypes.POINTER(ctypes.c_void_p),
        ctypes.POINTER(wintypes.BOOL),
    ]
    advapi32.GetSecurityDescriptorDacl.restype = wintypes.BOOL
    advapi32.GetSecurityDescriptorOwner.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_void_p),
        ctypes.POINTER(wintypes.BOOL),
    ]
    advapi32.GetSecurityDescriptorOwner.restype = wintypes.BOOL
    advapi32.SetNamedSecurityInfoW.argtypes = [
        wintypes.LPWSTR,
        ctypes.c_int,
        wintypes.DWORD,
        ctypes.c_void_p,
        ctypes.c_void_p,
        ctypes.c_void_p,
        ctypes.c_void_p,
    ]
    advapi32.SetNamedSecurityInfoW.restype = wintypes.DWORD
    kernel32.LocalFree.argtypes = [ctypes.c_void_p]
    kernel32.LocalFree.restype = ctypes.c_void_p

    user_sid = _current_windows_user_sid()
    flags = "OICI" if directory else ""
    sddl = (
        f"O:{user_sid}"
        "D:P"
        f"(A;{flags};FA;;;SY)"
        f"(A;{flags};FA;;;BA)"
        f"(A;{flags};FA;;;{user_sid})"
    )
    descriptor = ctypes.c_void_p()
    if not advapi32.ConvertStringSecurityDescriptorToSecurityDescriptorW(
        sddl, 1, ctypes.byref(descriptor), None
    ):
        raise SecureStoreError(
            "ConvertStringSecurityDescriptorToSecurityDescriptor failed: "
            f"{ctypes.WinError(ctypes.get_last_error())}"
        )
    try:
        present = wintypes.BOOL()
        defaulted = wintypes.BOOL()
        dacl = ctypes.c_void_p()
        owner = ctypes.c_void_p()
        owner_defaulted = wintypes.BOOL()
        if not advapi32.GetSecurityDescriptorDacl(
            descriptor, ctypes.byref(present), ctypes.byref(dacl), ctypes.byref(defaulted)
        ) or not present.value:
            raise SecureStoreError("The private security descriptor has no DACL")
        if not advapi32.GetSecurityDescriptorOwner(
            descriptor, ctypes.byref(owner), ctypes.byref(owner_defaulted)
        ) or not owner:
            raise SecureStoreError("The private security descriptor has no owner")
        mutable_path = ctypes.create_unicode_buffer(str(path))
        result = advapi32.SetNamedSecurityInfoW(
            mutable_path,
            1,
            0x00000001 | 0x00000004 | 0x80000000,
            owner,
            None,
            dacl,
            None,
        )
        if result != 0:
            raise SecureStoreError(
                f"SetNamedSecurityInfo failed for {path}: {ctypes.WinError(result)}"
            )
    finally:
        kernel32.LocalFree(descriptor)


def _reject_link(path: pathlib.Path) -> None:
    try:
        is_junction = getattr(path, "is_junction", lambda: False)()
        if path.is_symlink() or is_junction:
            raise SecureStoreError(f"Refusing to use linked security path: {path}")
    except OSError as exc:
        raise SecureStoreError(f"Could not inspect security path {path}: {exc}") from exc


def secure_path(path: pathlib.Path, *, directory: bool) -> None:
    target = pathlib.Path(path)
    if not target.exists():
        raise SecureStoreError(f"Security path does not exist: {target}")
    _reject_link(target)
    try:
        if os.name == "nt":
            _set_windows_private_dacl(target, directory=directory)
        else:
            target.chmod(
                stat.S_IRUSR
                | stat.S_IWUSR
                | (stat.S_IXUSR if directory else 0)
            )
    except SecureStoreError:
        raise
    except OSError as exc:
        raise SecureStoreError(f"Could not secure {target}: {exc}") from exc


def ensure_security_dir(path: pathlib.Path) -> pathlib.Path:
    target = pathlib.Path(path)
    target.mkdir(parents=True, exist_ok=True)
    if not target.is_dir():
        raise SecureStoreError(f"Security path is not a directory: {target}")
    secure_path(target, directory=True)
    return target


def write_secure_json(path: pathlib.Path, payload: Mapping[str, Any]) -> None:
    target = pathlib.Path(path)
    ensure_security_dir(target.parent)
    if target.exists():
        secure_path(target, directory=False)
    temporary = target.with_name(f".{target.name}.{os.getpid()}.{secrets.token_hex(8)}.tmp")
    try:
        descriptor = os.open(
            temporary,
            os.O_WRONLY | os.O_CREAT | os.O_EXCL,
            0o600,
        )
        os.close(descriptor)
        secure_path(temporary, directory=False)
        temporary.write_text(
            json.dumps(dict(payload), ensure_ascii=True, indent=2) + "\n",
            encoding="utf-8",
        )
        os.replace(temporary, target)
        try:
            secure_path(target, directory=False)
        except Exception:
            target.unlink(missing_ok=True)
            raise
    finally:
        temporary.unlink(missing_ok=True)


def read_secure_json(path: pathlib.Path) -> dict[str, Any]:
    target = pathlib.Path(path)
    if not target.is_file():
        return {}
    ensure_security_dir(target.parent)
    secure_path(target, directory=False)
    try:
        payload = json.loads(target.read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise SecureStoreError(f"Could not read secure config {target}: {exc}") from exc
    if not isinstance(payload, dict):
        raise SecureStoreError(f"Secure config must be a JSON object: {target}")
    return payload


def read_ue_backend_token(
    env: Mapping[str, str] | None = None,
) -> str:
    payload = read_secure_json(ue_capability_path(env))
    token = str(payload.get(UE_TOKEN_FIELD) or "").strip()
    return token if len(token) >= 32 else ""


def write_ue_backend_token(
    token: str,
    env: Mapping[str, str] | None = None,
) -> None:
    normalized = str(token).strip()
    if len(normalized) < 32:
        raise SecureStoreError("UE backend token must contain at least 32 characters")
    write_secure_json(
        ue_capability_path(env),
        {"version": 1, UE_TOKEN_FIELD: normalized},
    )


def ensure_ue_backend_token(
    env: Mapping[str, str] | None = None,
) -> str:
    """Return the legacy editor bearer, retained only for upgrade compatibility.

    New bridges use ``ensure_bridge_hmac_key`` and never send that key on the
    wire. Several older Revvy entry points could start concurrently, so this
    compatibility helper still serializes initial creation for old installs.
    """

    existing = read_ue_backend_token(env)
    if existing:
        return existing

    target = ue_capability_path(env)
    ensure_security_dir(target.parent)
    lock_path = target.with_name(target.name + ".lock")
    deadline = time.monotonic() + 5.0
    descriptor: int | None = None
    while descriptor is None:
        try:
            descriptor = os.open(
                lock_path,
                os.O_WRONLY | os.O_CREAT | os.O_EXCL,
                0o600,
            )
            os.write(
                descriptor,
                f"{os.getpid()} {int(time.time())}\n".encode("ascii"),
            )
        except FileExistsError:
            try:
                age = time.time() - lock_path.stat().st_mtime
            except OSError:
                age = 0.0
            # A dead creator must not brick the capability store forever.
            # The generous age avoids stealing a live lock on slow or remote
            # user profiles; token creation normally completes in milliseconds.
            if age >= 30.0:
                try:
                    lock_path.unlink()
                    continue
                except OSError:
                    pass
            if time.monotonic() >= deadline:
                raise SecureStoreError(
                    f"Timed out waiting for editor capability lock: {lock_path}"
                )
            time.sleep(0.025)
    try:
        os.close(descriptor)
        descriptor = None
        secure_path(lock_path, directory=False)
        existing = read_ue_backend_token(env)
        if existing:
            return existing
        generated = secrets.token_urlsafe(48)
        write_ue_backend_token(generated, env)
        confirmed = read_ue_backend_token(env)
        if confirmed != generated:
            raise SecureStoreError(
                "Protected editor capability changed during initial creation"
            )
        return generated
    finally:
        if descriptor is not None:
            os.close(descriptor)
        try:
            lock_path.unlink(missing_ok=True)
        except OSError:
            pass


def read_bridge_hmac_key(
    env: Mapping[str, str] | None = None,
) -> str:
    payload = read_secure_json(bridge_auth_path(env))
    if payload and (
        payload.get("version") != 2
        or payload.get("algorithm") != "hmac-sha1-exact-v2"
    ):
        raise SecureStoreError("Unsupported Revvy bridge authentication format")
    key = str(payload.get(BRIDGE_HMAC_FIELD) or "").strip()
    return key if len(key) >= 32 else ""


def ensure_bridge_hmac_key(
    env: Mapping[str, str] | None = None,
) -> str:
    """Return the v2 bridge HMAC key, creating it under an owner-only lock."""

    existing = read_bridge_hmac_key(env)
    if existing:
        return existing
    target = bridge_auth_path(env)
    ensure_security_dir(target.parent)
    lock_path = target.with_name(target.name + ".lock")
    deadline = time.monotonic() + 5.0
    descriptor: int | None = None
    while descriptor is None:
        try:
            descriptor = os.open(
                lock_path,
                os.O_WRONLY | os.O_CREAT | os.O_EXCL,
                0o600,
            )
            os.write(
                descriptor,
                f"{os.getpid()} {int(time.time())}\n".encode("ascii"),
            )
        except FileExistsError:
            try:
                age = time.time() - lock_path.stat().st_mtime
            except OSError:
                age = 0.0
            if age >= 30.0:
                try:
                    lock_path.unlink()
                    continue
                except OSError:
                    pass
            if time.monotonic() >= deadline:
                raise SecureStoreError(
                    f"Timed out waiting for bridge authentication lock: {lock_path}"
                )
            time.sleep(0.025)
    try:
        os.close(descriptor)
        descriptor = None
        secure_path(lock_path, directory=False)
        existing = read_bridge_hmac_key(env)
        if existing:
            return existing
        generated = secrets.token_urlsafe(48)
        write_secure_json(
            target,
            {
                "version": 2,
                "algorithm": "hmac-sha1-exact-v2",
                BRIDGE_HMAC_FIELD: generated,
            },
        )
        confirmed = read_bridge_hmac_key(env)
        if confirmed != generated:
            raise SecureStoreError(
                "Protected bridge authentication key changed during creation"
            )
        return generated
    finally:
        if descriptor is not None:
            os.close(descriptor)
        try:
            lock_path.unlink(missing_ok=True)
        except OSError:
            pass


def retire_legacy_ue_backend_token(
    env: Mapping[str, str] | None = None,
) -> None:
    """Remove the former wire bearer after the independent v2 key exists."""

    target = ue_capability_path(env)
    try:
        target.unlink(missing_ok=True)
    except OSError as exc:
        raise SecureStoreError(
            f"Could not retire legacy editor bearer {target}: {exc}"
        ) from exc


def _configured_project_paths(config: Mapping[str, Any]) -> list[pathlib.Path]:
    values: list[str] = []
    direct = config.get("project_path")
    if isinstance(direct, str):
        values.append(direct)
    for key in ("active_project", "installed_project"):
        entry = config.get(key)
        if isinstance(entry, Mapping) and isinstance(entry.get("path"), str):
            values.append(str(entry["path"]))

    projects: list[pathlib.Path] = []
    seen: set[str] = set()
    for value in values:
        if not value.strip():
            continue
        try:
            project = pathlib.Path(value).expanduser().resolve(strict=True)
        except OSError:
            continue
        if not project.is_dir() or not any(project.glob("*.uproject")):
            continue
        key = os.path.normcase(str(project))
        if key not in seen:
            seen.add(key)
            projects.append(project)
    return projects


def _decode_preserving_bom(raw: bytes) -> tuple[str, str]:
    if raw.startswith(b"\xef\xbb\xbf"):
        return raw.decode("utf-8-sig"), "utf-8-sig"
    if raw.startswith(b"\xff\xfe"):
        return raw.decode("utf-16"), "utf-16"
    if raw.startswith(b"\xfe\xff"):
        return raw.decode("utf-16"), "utf-16"
    return raw.decode("utf-8"), "utf-8"


def scrub_legacy_editor_api_keys(
    config: Mapping[str, Any],
    *,
    require_project: bool,
) -> int:
    """Remove only Revvy.MCPServer/ApiKey from validated project editor INIs."""
    projects = _configured_project_paths(config)
    if require_project and not projects:
        raise SecureStoreError(
            "Legacy UE token migration could not validate its Unreal project path"
        )
    removed = 0
    section_pattern = re.compile(r"^\s*\[([^\]]+)\]\s*(?:[;#].*)?$")
    key_pattern = re.compile(r"^\s*ApiKey\s*=", re.IGNORECASE)
    for project in projects:
        ini_path = (
            project
            / "Saved"
            / "Config"
            / "WindowsEditor"
            / "EditorPerProjectUserSettings.ini"
        )
        if not ini_path.is_file():
            continue
        try:
            ini_path.resolve().relative_to(project)
            raw = ini_path.read_bytes()
            text, encoding = _decode_preserving_bom(raw)
        except (OSError, UnicodeError, ValueError) as exc:
            raise SecureStoreError(
                f"Could not inspect legacy Unreal editor config {ini_path}: {exc}"
            ) from exc
        in_revvy_section = False
        changed = False
        kept: list[str] = []
        for line in text.splitlines(keepends=True):
            section = section_pattern.match(line.rstrip("\r\n"))
            if section:
                in_revvy_section = (
                    section.group(1).strip().casefold() == "revvy.mcpserver"
                )
            if in_revvy_section and key_pattern.match(line):
                changed = True
                removed += 1
                continue
            kept.append(line)
        if not changed:
            continue
        temporary = ini_path.with_name(f".{ini_path.name}.{os.getpid()}.revvy.tmp")
        try:
            temporary.write_bytes("".join(kept).encode(encoding))
            os.replace(temporary, ini_path)
        except OSError as exc:
            raise SecureStoreError(
                f"Could not scrub legacy Unreal editor token from {ini_path}: {exc}"
            ) from exc
        finally:
            temporary.unlink(missing_ok=True)
    return removed
