from __future__ import annotations

import ctypes
import json
import os
import pathlib
import secrets
import stat
from ctypes import wintypes
from typing import Any, Mapping


SECURITY_DIR_ENV = "REVVY_SECURITY_DIR"


class SecurityError(RuntimeError):
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
        raise SecurityError(
            f"OpenProcessToken failed: {ctypes.WinError(ctypes.get_last_error())}"
        )
    try:
        required = wintypes.DWORD()
        advapi32.GetTokenInformation(token, 1, None, 0, ctypes.byref(required))
        if required.value <= 0:
            raise SecurityError("GetTokenInformation did not return a buffer size")
        buffer = ctypes.create_string_buffer(required.value)
        if not advapi32.GetTokenInformation(
            token, 1, buffer, required.value, ctypes.byref(required)
        ):
            raise SecurityError(
                f"GetTokenInformation failed: {ctypes.WinError(ctypes.get_last_error())}"
            )
        user = ctypes.cast(buffer, ctypes.POINTER(_TokenUser)).contents
        sid_text = wintypes.LPWSTR()
        if not advapi32.ConvertSidToStringSidW(
            user.User.Sid, ctypes.byref(sid_text)
        ):
            raise SecurityError(
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
        raise SecurityError(
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
            raise SecurityError("The private security descriptor has no DACL")
        if not advapi32.GetSecurityDescriptorOwner(
            descriptor, ctypes.byref(owner), ctypes.byref(owner_defaulted)
        ) or not owner:
            raise SecurityError("The private security descriptor has no owner")
        result = advapi32.SetNamedSecurityInfoW(
            ctypes.create_unicode_buffer(str(path)),
            1,
            0x00000001 | 0x00000004 | 0x80000000,
            owner,
            None,
            dacl,
            None,
        )
        if result != 0:
            raise SecurityError(
                f"SetNamedSecurityInfo failed for {path}: {ctypes.WinError(result)}"
            )
    finally:
        kernel32.LocalFree(descriptor)


def _secure_path(path: pathlib.Path, *, directory: bool) -> None:
    if not path.exists():
        raise SecurityError(f"Security path does not exist: {path}")
    is_junction = getattr(path, "is_junction", lambda: False)()
    if path.is_symlink() or is_junction:
        raise SecurityError(f"Refusing to use linked security path: {path}")
    if os.name == "nt":
        _set_windows_private_dacl(path, directory=directory)
    else:
        path.chmod(
            stat.S_IRUSR
            | stat.S_IWUSR
            | (stat.S_IXUSR if directory else 0)
        )


def _ensure_security_dir(path: pathlib.Path) -> None:
    path.mkdir(parents=True, exist_ok=True)
    if not path.is_dir():
        raise SecurityError(f"Security path is not a directory: {path}")
    _secure_path(path, directory=True)


def write_secure_json(path: pathlib.Path, payload: Mapping[str, Any]) -> None:
    target = pathlib.Path(path)
    _ensure_security_dir(target.parent)
    if target.exists():
        _secure_path(target, directory=False)
    temporary = target.with_name(f".{target.name}.{os.getpid()}.{secrets.token_hex(8)}.tmp")
    try:
        descriptor = os.open(
            temporary,
            os.O_WRONLY | os.O_CREAT | os.O_EXCL,
            0o600,
        )
        os.close(descriptor)
        _secure_path(temporary, directory=False)
        temporary.write_text(
            json.dumps(dict(payload), ensure_ascii=True, indent=2) + "\n",
            encoding="utf-8",
        )
        os.replace(temporary, target)
        try:
            _secure_path(target, directory=False)
        except Exception:
            target.unlink(missing_ok=True)
            raise
    finally:
        temporary.unlink(missing_ok=True)


def read_secure_json(path: pathlib.Path) -> dict[str, Any]:
    target = pathlib.Path(path)
    if not target.is_file():
        return {}
    _ensure_security_dir(target.parent)
    _secure_path(target, directory=False)
    try:
        payload = json.loads(target.read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise SecurityError(f"Could not read secure config {target}: {exc}") from exc
    if not isinstance(payload, dict):
        raise SecurityError(f"Secure config must be a JSON object: {target}")
    return payload
