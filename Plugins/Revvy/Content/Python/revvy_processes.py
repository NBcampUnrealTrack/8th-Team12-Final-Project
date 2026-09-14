"""Small cross-platform helpers for processes Revvy explicitly owns."""

from __future__ import annotations

import os
import pathlib
import signal
import subprocess
import time
import ctypes
from ctypes import wintypes


_WINDOWS_JOB_ATTRIBUTE = "_revvy_windows_job_handle"
_POSIX_GROUP_ATTRIBUTE = "_revvy_posix_process_group"


def _safe_int(value: object) -> int:
    """A stored handle/pid that is not a real number counts as absent.

    Process objects reaching these helpers are not always real Popen instances
    (wrappers, test doubles) — tree management is best-effort, so a bad value
    must degrade to 0, never raise out of a cleanup path.
    """

    try:
        return int(value)  # type: ignore[call-overload]
    except (TypeError, ValueError):
        return 0


def attach_process_tree(process: subprocess.Popen) -> bool:
    """Attach an owned child to a durable tree identity immediately after spawn.

    A PID is not enough once a launcher exits before its descendants. Windows
    jobs and a remembered POSIX process group let cancellation still reach the
    tree in that parent-exit window. The job intentionally has no
    KILL_ON_JOB_CLOSE flag: a normally completed AI turn may have launched an
    editor the user expects to keep running.
    """

    if os.name != "nt":
        try:
            group = os.getpgid(int(process.pid))
            if group == os.getpgrp():
                return False
            setattr(process, _POSIX_GROUP_ATTRIBUTE, group)
            return True
        except (AttributeError, OSError, TypeError, ValueError):
            return False

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
    kernel32.CreateJobObjectW.restype = wintypes.HANDLE
    kernel32.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
    kernel32.AssignProcessToJobObject.restype = wintypes.BOOL
    kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel32.CloseHandle.restype = wintypes.BOOL
    job = kernel32.CreateJobObjectW(None, None)
    if not job:
        return False
    raw_handle = _safe_int(getattr(process, "_handle", 0))
    if not raw_handle:
        kernel32.CloseHandle(job)
        return False
    process_handle = wintypes.HANDLE(raw_handle)
    if not process_handle or not kernel32.AssignProcessToJobObject(job, process_handle):
        kernel32.CloseHandle(job)
        return False
    setattr(process, _WINDOWS_JOB_ATTRIBUTE, int(job))
    return True


def release_process_tree(process: subprocess.Popen) -> bool:
    """Release Revvy's durable tree handle without terminating descendants."""

    if os.name != "nt":
        try:
            delattr(process, _POSIX_GROUP_ATTRIBUTE)
        except AttributeError:
            pass
        return True
    handle = _safe_int(getattr(process, _WINDOWS_JOB_ATTRIBUTE, 0))
    if not handle:
        return False
    setattr(process, _WINDOWS_JOB_ATTRIBUTE, 0)
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel32.CloseHandle.restype = wintypes.BOOL
    return bool(kernel32.CloseHandle(wintypes.HANDLE(handle)))


def terminate_attached_process_tree(
    process: subprocess.Popen,
    *,
    timeout: float = 5.0,
) -> bool:
    """Terminate an attached job/group, falling back to the PID-based helper."""

    if os.name == "nt":
        handle = _safe_int(getattr(process, _WINDOWS_JOB_ATTRIBUTE, 0))
        if handle:
            setattr(process, _WINDOWS_JOB_ATTRIBUTE, 0)
            kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
            kernel32.TerminateJobObject.argtypes = [wintypes.HANDLE, wintypes.UINT]
            kernel32.TerminateJobObject.restype = wintypes.BOOL
            kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
            kernel32.CloseHandle.restype = wintypes.BOOL
            terminated = bool(
                kernel32.TerminateJobObject(wintypes.HANDLE(handle), 1)
            )
            closed = bool(kernel32.CloseHandle(wintypes.HANDLE(handle)))
            return terminated and closed
        pid = _safe_int(getattr(process, "pid", 0))
        return terminate_process_tree(pid, timeout=timeout) if pid else False

    group = getattr(process, _POSIX_GROUP_ATTRIBUTE, None)
    if isinstance(group, int) and group > 0 and group != os.getpgrp():
        try:
            os.killpg(group, signal.SIGTERM)
            deadline = time.monotonic() + max(0.0, float(timeout))
            while time.monotonic() < deadline:
                try:
                    os.killpg(group, 0)
                except ProcessLookupError:
                    return True
                except PermissionError:
                    return False
                time.sleep(0.05)
            os.killpg(group, signal.SIGKILL)
            return True
        except ProcessLookupError:
            return True
        except (OSError, TypeError, ValueError):
            return False
        finally:
            try:
                delattr(process, _POSIX_GROUP_ATTRIBUTE)
            except AttributeError:
                pass
    pid = _safe_int(getattr(process, "pid", 0))
    return terminate_process_tree(pid, timeout=timeout) if pid else False


def _windows_taskkill_path() -> str:
    for variable in ("SystemRoot", "WINDIR"):
        root = str(os.environ.get(variable) or "").strip()
        if not root:
            continue
        candidate = pathlib.Path(root) / "System32" / "taskkill.exe"
        if candidate.is_file():
            return str(candidate)
    return "taskkill.exe"


def terminate_process_tree(pid: int, *, timeout: float = 5.0) -> bool:
    """Stop an owned PID and its descendants; return whether tree stop succeeded.

    Windows one-file applications have an outer bootloader and an inner runtime
    process. Terminating only the parent can orphan that runtime, so use the OS
    tree-aware utility without a shell. Callers must establish ownership before
    invoking this helper; it deliberately does not infer process identity.
    """

    try:
        target_pid = int(pid)
    except (TypeError, ValueError):
        return False
    if target_pid <= 0:
        return False
    if os.name != "nt":
        try:
            process_group = os.getpgid(target_pid)
            # Never signal the caller's own group. Revvy launches owned POSIX
            # children in a new session, but this guard keeps the helper safe
            # if a future caller forgets that isolation contract.
            if process_group == os.getpgrp():
                return False
            os.killpg(process_group, signal.SIGTERM)
            deadline = time.monotonic() + max(0.0, float(timeout))
            while time.monotonic() < deadline:
                try:
                    os.killpg(process_group, 0)
                except ProcessLookupError:
                    return True
                except PermissionError:
                    return False
                time.sleep(0.05)
            try:
                os.killpg(process_group, signal.SIGKILL)
            except ProcessLookupError:
                return True
            return True
        except ProcessLookupError:
            return True
        except (AttributeError, OSError, TypeError, ValueError):
            return False

    creationflags = int(getattr(subprocess, "CREATE_NO_WINDOW", 0))
    try:
        completed = subprocess.run(
            [
                _windows_taskkill_path(),
                "/PID",
                str(target_pid),
                "/T",
                "/F",
            ],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
            timeout=max(0.1, float(timeout)),
            creationflags=creationflags,
        )
    except (OSError, subprocess.TimeoutExpired, TypeError, ValueError):
        return False
    return completed.returncode == 0
