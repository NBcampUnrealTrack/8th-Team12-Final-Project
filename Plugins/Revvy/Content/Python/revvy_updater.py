"""Background update checks for the packaged Revvy desktop app.

Modelled on what Orca does, because that shape is already proven on this
machine: keep a staging directory outside the install, download the next
installer into ``pending/``, verify it against a hash the feed published, and
only hand it to the installer when the user says so. Nothing is applied under
the running app's feet, and a half-finished download is never runnable.

The feed is deliberately a plain URL to a JSON document. ``file:`` works, so a
shared folder or a NAS is a feed with no server and no account; ``https:`` works
for a release host. Choosing between them is configuration, not a rewrite.

This module never installs anything. It reports what is available and stages the
bytes; running the installer is a separate, explicit act - see
``RevvyUpdater.pending``.
"""

from __future__ import annotations

import dataclasses
import hashlib
import json
import os
import pathlib
import shutil
import tempfile
import threading
import urllib.error
import urllib.parse
import urllib.request

import revvy_app_core as core

FEED_ENV = "REVVY_UPDATE_FEED"
CHANNEL_ENV = "REVVY_UPDATE_CHANNEL"
CACHE_DIRNAME = "revvy-updater"
DEFAULT_CHANNEL = "stable"
# Big enough for a PyInstaller onefile bundle and small enough that a wrong URL
# cannot fill the disk before anyone notices.
MAX_INSTALLER_BYTES = 512 * 1024 * 1024
NETWORK_TIMEOUT_SECONDS = 20.0
# file: and https: only. A feed is a thing the user configured, but it is still
# input, and ftp:/gopher: handlers have no business being reachable from it.
ALLOWED_SCHEMES = ("https", "file")


class UpdateError(RuntimeError):
    """A check or download failed for a reason worth showing the user."""


@dataclasses.dataclass(frozen=True)
class UpdateInfo:
    """One release as the feed describes it."""

    version: str
    url: str
    sha256: str
    size: int = 0
    notes: str = ""
    channel: str = DEFAULT_CHANNEL

    @classmethod
    def from_payload(cls, payload: object, base_url: str = "") -> "UpdateInfo":
        if not isinstance(payload, dict):
            raise UpdateError("update feed is not a JSON object")
        version = str(payload.get("version") or "").strip()
        url = str(payload.get("url") or "").strip()
        digest = str(payload.get("sha256") or "").strip().lower()
        if not version:
            raise UpdateError("update feed has no version")
        if not url:
            raise UpdateError("update feed has no url")
        if len(digest) != 64 or not all(c in "0123456789abcdef" for c in digest):
            # Without a usable digest there is nothing to verify the download
            # against, and an unverified installer is worse than no update.
            raise UpdateError("update feed has no usable sha256")
        if base_url:
            url = urllib.parse.urljoin(base_url, url)
        try:
            size = int(payload.get("size") or 0)
        except (TypeError, ValueError):
            size = 0
        return cls(
            version=version,
            url=url,
            sha256=digest,
            size=max(0, size),
            notes=str(payload.get("notes") or "").strip(),
            channel=str(payload.get("channel") or DEFAULT_CHANNEL).strip(),
        )


def parse_version(value: str) -> tuple[int, ...]:
    """Numeric parts of a version, for ordering.

    Anything non-numeric is dropped rather than guessed at: comparing '1.6.0rc1'
    to '1.6.0' by inventing a rule is how an app talks itself into a downgrade.
    """
    parts: list[int] = []
    for chunk in str(value).split("."):
        digits = "".join(ch for ch in chunk if ch.isdigit())
        if not digits:
            break
        parts.append(int(digits))
    return tuple(parts)


def is_newer(candidate: str, current: str) -> bool:
    left, right = parse_version(candidate), parse_version(current)
    if not left:
        return False
    return left > right


def _validated_url(url: str) -> str:
    parsed = urllib.parse.urlparse(url)
    if parsed.scheme not in ALLOWED_SCHEMES:
        raise UpdateError(f"unsupported update URL scheme: {parsed.scheme or 'none'}")
    return url


def _fetch(url: str, limit: int) -> bytes:
    request = urllib.request.Request(
        _validated_url(url), headers={"User-Agent": f"Revvy/{core.APP_VERSION}"}
    )
    try:
        with urllib.request.urlopen(  # noqa: S310 - scheme checked above
            request, timeout=NETWORK_TIMEOUT_SECONDS
        ) as response:
            payload = response.read(limit + 1)
    except (urllib.error.URLError, OSError, ValueError) as exc:
        raise UpdateError(f"could not reach the update feed: {exc}") from exc
    if len(payload) > limit:
        raise UpdateError("update download exceeded the size limit")
    return payload


class RevvyUpdater:
    """Checks a feed and stages the installer it points at.

    Staging lives beside the app's other machine-wide state rather than inside
    the install directory, so an update that is downloaded but not yet applied
    survives the installer replacing the program folder.
    """

    def __init__(
        self,
        feed_url: str = "",
        current_version: str = "",
        cache_dir: pathlib.Path | None = None,
        env: dict[str, str] | None = None,
    ) -> None:
        values = os.environ if env is None else env
        self.feed_url = (feed_url or str(values.get(FEED_ENV) or "")).strip()
        self.channel = str(values.get(CHANNEL_ENV) or DEFAULT_CHANNEL).strip()
        self.current_version = current_version or core.APP_VERSION
        self.cache_dir = cache_dir or (core.global_cache_dir(values) / CACHE_DIRNAME)
        self._lock = threading.Lock()

    @property
    def configured(self) -> bool:
        return bool(self.feed_url)

    @property
    def pending_dir(self) -> pathlib.Path:
        return self.cache_dir / "pending"

    @property
    def state_path(self) -> pathlib.Path:
        return self.pending_dir / "update-info.json"

    def check(self) -> UpdateInfo | None:
        """Ask the feed what the newest release is. None means already current."""
        if not self.configured:
            return None
        raw = _fetch(self.feed_url, 1024 * 1024)
        try:
            payload = json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise UpdateError(f"update feed is not valid JSON: {exc}") from exc
        if isinstance(payload, dict) and isinstance(payload.get("channels"), dict):
            channel = payload["channels"].get(self.channel)
            if channel is None:
                raise UpdateError(f"update feed has no '{self.channel}' channel")
            payload = channel
        info = UpdateInfo.from_payload(payload, base_url=self.feed_url)
        if not is_newer(info.version, self.current_version):
            return None
        return info

    def pending(self) -> UpdateInfo | None:
        """A previously staged update that is still valid, if there is one.

        Re-verifies the file on disk. A download interrupted by a crash leaves a
        truncated installer, and running one is worse than downloading again.
        """
        try:
            payload = json.loads(self.state_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            return None
        try:
            info = UpdateInfo.from_payload(payload)
        except UpdateError:
            return None
        installer = self.installer_path(info)
        if not installer.is_file():
            return None
        if _sha256_file(installer) != info.sha256:
            self.discard()
            return None
        if not is_newer(info.version, self.current_version):
            # Already applied. Leaving it staged would offer the running version
            # to itself forever.
            self.discard()
            return None
        return info

    def installer_path(self, info: UpdateInfo) -> pathlib.Path:
        name = pathlib.PurePosixPath(urllib.parse.urlparse(info.url).path).name
        # The feed does not get to choose a path. Only the leaf name is used, and
        # only after it is reduced to something that cannot traverse.
        safe = "".join(ch for ch in name if ch.isalnum() or ch in "._-")
        return self.pending_dir / (safe or "revvy-setup.exe")

    def download(self, info: UpdateInfo) -> pathlib.Path:
        """Stage the installer, verifying it before it becomes visible."""
        with self._lock:
            self.pending_dir.mkdir(parents=True, exist_ok=True)
            limit = min(MAX_INSTALLER_BYTES, info.size * 2 or MAX_INSTALLER_BYTES)
            payload = _fetch(info.url, limit)
            digest = hashlib.sha256(payload).hexdigest()
            if digest != info.sha256:
                raise UpdateError("downloaded installer failed its checksum")
            target = self.installer_path(info)
            # Write to a temporary name and rename into place: a reader that
            # catches the file mid-write must never find a runnable prefix.
            handle, temporary = tempfile.mkstemp(dir=str(self.pending_dir))
            try:
                with os.fdopen(handle, "wb") as stream:
                    stream.write(payload)
                os.replace(temporary, target)
            except OSError:
                pathlib.Path(temporary).unlink(missing_ok=True)
                raise
            self.state_path.write_text(
                json.dumps(dataclasses.asdict(info), ensure_ascii=False, indent=2)
                + "\n",
                encoding="utf-8",
            )
            return target

    def discard(self) -> None:
        shutil.rmtree(self.pending_dir, ignore_errors=True)


def _sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()
