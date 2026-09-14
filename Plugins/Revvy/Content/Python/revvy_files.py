"""Project file tree, git decoration, and safe file reads/writes.

The model follows what Orca settled on, because the reasons hold here too:

* A node carries no children and no status. Children live in a separate cache
  keyed by directory, status in a separate map keyed by relative path. A status
  refresh then never disturbs the tree, and expanding a directory never
  disturbs status.
* Loading is lazy - one directory per expand, never a full walk. A game project
  has tens of thousands of files under Content/ and Intermediate/, and walking
  them to draw a sidebar is how an editor becomes unusable.
* Every git call is capped. An un-ignored build directory can produce a status
  listing large enough to be the whole problem.

The one deliberate divergence: this module also owns file reads and writes,
because the thing being guarded against here is specific to Revvy. An agent
edits files while the user has them open. A write that does not check whether
the file changed underneath it will silently discard the agent's work, or the
user's.
"""

from __future__ import annotations

import dataclasses
import hashlib
import os
import pathlib
import subprocess
import time

# One letter each, the vocabulary git itself uses. Six states is enough to
# describe every row and few enough to read at a glance.
STATUS_LETTERS = {
    "modified": "M",
    "added": "A",
    "deleted": "D",
    "renamed": "R",
    "untracked": "U",
    "copied": "C",
}
# Which status a directory shows when its children disagree. Deleted is absent
# on purpose: a folder is not deleted because one file inside it was.
STATUS_RANK = {
    "deleted": 5,
    "modified": 4,
    "added": 3,
    "untracked": 3,
    "renamed": 2,
    "copied": 1,
}
FOLDER_ROLLUP_EXCLUDED = ("deleted",)
_PORCELAIN = {
    "M": "modified",
    "A": "added",
    "D": "deleted",
    "R": "renamed",
    "C": "copied",
    "?": "untracked",
}

# Caps. Each exists because the unbounded version has a failure mode worth
# naming, not because a number felt right.
MAX_STATUS_ENTRIES = 2000       # a huge un-ignored folder must not stall the UI
MAX_DIR_ENTRIES = 4000          # one directory listing
MAX_EDITABLE_BYTES = 2 * 1024 * 1024
GIT_TIMEOUT_SECONDS = 15.0

# Directories worth never expanding into by default. These are engine-derived
# and routinely hold six figures of files.
NOISY_DIRNAMES = frozenset(
    {
        "Intermediate",
        "DerivedDataCache",
        "Saved",
        "Binaries",
        ".git",
        "Library",
        "Temp",
        "obj",
        "node_modules",
        "__pycache__",
        ".godot",
    }
)
TEXT_SUFFIXES = frozenset(
    {
        ".py", ".pyw", ".cpp", ".h", ".hpp", ".c", ".cs", ".gd", ".json", ".md",
        ".txt", ".yml", ".yaml", ".ini", ".cfg", ".xml", ".uplugin", ".uproject",
        ".build", ".target", ".sh", ".ps1", ".bat", ".cmd", ".toml", ".gitignore",
        ".tres", ".tscn", ".godot", ".asmdef", ".shader", ".usf", ".ush", ".css",
        ".html", ".js", ".ts", ".sql", ".log",
    }
)


@dataclasses.dataclass(frozen=True)
class FileNode:
    """One row. Deliberately without children or status - see the module docstring."""

    name: str
    path: pathlib.Path
    relative: str
    is_dir: bool
    depth: int

    @property
    def noisy(self) -> bool:
        return self.is_dir and self.name in NOISY_DIRNAMES

    @property
    def editable(self) -> bool:
        if self.is_dir:
            return False
        if self.path.suffix.lower() in TEXT_SUFFIXES:
            return True
        return not self.path.suffix


@dataclasses.dataclass
class FileDocument:
    """A file's text plus the signature of the bytes it came from.

    ``signature`` is what makes a safe write possible: if the file on disk no
    longer hashes to this, something else - almost always an agent - wrote to it
    since the read, and saving would throw that away.
    """

    path: pathlib.Path
    text: str
    signature: str
    truncated: bool = False
    binary: bool = False
    encoding: str = "utf-8"


class FileTree:
    """Lazy directory listing for one project, with git decoration."""

    def __init__(self, root: str | os.PathLike[str]) -> None:
        self.root = pathlib.Path(root).expanduser().resolve()
        self._dirs: dict[pathlib.Path, list[FileNode]] = {}
        self._status: dict[str, str] = {}
        self._folder_status: dict[str, str] = {}
        self._status_truncated = False
        self._status_stamp = 0.0

    # -- listing ------------------------------------------------------------
    def children(self, directory: pathlib.Path | None = None, *, refresh: bool = False) -> list[FileNode]:
        target = pathlib.Path(directory) if directory is not None else self.root
        if not refresh and target in self._dirs:
            return self._dirs[target]
        nodes = self._read_dir(target)
        self._dirs[target] = nodes
        return nodes

    def forget(self, directory: pathlib.Path) -> None:
        self._dirs.pop(pathlib.Path(directory), None)

    def _read_dir(self, directory: pathlib.Path) -> list[FileNode]:
        try:
            entries = list(os.scandir(directory))
        except OSError:
            return []
        depth = len(directory.relative_to(self.root).parts) if directory != self.root else 0
        nodes: list[FileNode] = []
        for entry in entries[:MAX_DIR_ENTRIES]:
            try:
                is_dir = entry.is_dir(follow_symlinks=False)
            except OSError:
                continue
            path = pathlib.Path(entry.path)
            try:
                relative = path.relative_to(self.root).as_posix()
            except ValueError:
                continue
            nodes.append(
                FileNode(
                    name=entry.name,
                    path=path,
                    relative=relative,
                    is_dir=is_dir,
                    depth=depth + 1,
                )
            )
        # Directories first, then case-insensitive by name: the order every file
        # browser uses, and the only one that is predictable while typing.
        nodes.sort(key=lambda node: (not node.is_dir, node.name.lower()))
        return nodes

    # -- git ---------------------------------------------------------------
    def refresh_status(self) -> bool:
        """Re-read git status. Returns False when the project is not a repo."""
        entries = _git_status_entries(self.root)
        if entries is None:
            self._status = {}
            self._folder_status = {}
            return False
        self._status_truncated = len(entries) >= MAX_STATUS_ENTRIES
        self._status = entries
        self._folder_status = _roll_up_folders(entries)
        self._status_stamp = time.monotonic()
        return True

    def status_of(self, node: FileNode) -> str:
        """The status letter for a row, or "" when it has none."""
        if node.is_dir:
            state = self._folder_status.get(node.relative, "")
        else:
            state = self._status.get(node.relative, "")
        return STATUS_LETTERS.get(state, "")

    @property
    def status_truncated(self) -> bool:
        return self._status_truncated

    def changed_paths(self) -> list[str]:
        return sorted(self._status)


def _roll_up_folders(entries: dict[str, str]) -> dict[str, str]:
    """Give every ancestor directory the strongest status beneath it."""
    folders: dict[str, str] = {}
    for relative, state in entries.items():
        if state in FOLDER_ROLLUP_EXCLUDED:
            continue
        parts = relative.split("/")[:-1]
        for index in range(len(parts)):
            key = "/".join(parts[: index + 1])
            current = folders.get(key)
            if current is None or STATUS_RANK.get(state, 0) > STATUS_RANK.get(current, 0):
                folders[key] = state
    return folders


def _run_git(root: pathlib.Path, *args: str) -> tuple[int, str]:
    try:
        result = subprocess.run(
            ["git", *args],
            cwd=str(root),
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=GIT_TIMEOUT_SECONDS,
            check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return 1, ""
    return result.returncode, result.stdout


def _git_status_entries(root: pathlib.Path) -> dict[str, str] | None:
    """Path -> state for the working tree, or None when this is not a repo."""
    code, output = _run_git(root, "rev-parse", "--is-inside-work-tree")
    if code != 0 or output.strip() != "true":
        return None
    code, output = _run_git(root, "status", "--porcelain=v1", "-z", "--no-renames")
    if code != 0:
        return {}
    entries: dict[str, str] = {}
    for record in output.split("\0"):
        if len(record) < 4 or len(entries) >= MAX_STATUS_ENTRIES:
            continue
        index_state, work_state, path = record[0], record[1], record[3:]
        # The working-tree column wins: what the user sees in the editor is the
        # working tree, and an unstaged edit on top of a staged one is still an
        # edit they have not committed.
        state = _PORCELAIN.get(work_state) or _PORCELAIN.get(index_state)
        if state:
            entries[path.replace("\\", "/")] = state
    return entries


def file_signature(path: str | os.PathLike[str]) -> str:
    """Hash of a file's bytes, or "" if it cannot be read."""
    try:
        return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()
    except OSError:
        return ""


def read_document(path: str | os.PathLike[str]) -> FileDocument:
    """Load a file for viewing or editing, capturing its signature."""
    target = pathlib.Path(path)
    try:
        raw = target.read_bytes()
    except OSError as exc:
        raise OSError(f"파일을 읽지 못했어요: {exc}") from exc
    signature = hashlib.sha256(raw).hexdigest()
    truncated = len(raw) > MAX_EDITABLE_BYTES
    if truncated:
        raw = raw[:MAX_EDITABLE_BYTES]
    if b"\0" in raw[:8192]:
        return FileDocument(target, "", signature, truncated, binary=True)
    for encoding in ("utf-8-sig", "utf-8", "cp949", "latin-1"):
        try:
            return FileDocument(target, raw.decode(encoding), signature, truncated, encoding=encoding)
        except UnicodeDecodeError:
            continue
    return FileDocument(target, raw.decode("utf-8", "replace"), signature, truncated)


class ExternalEditError(RuntimeError):
    """The file changed on disk since it was read.

    Raised rather than resolved because only the user can say whose version
    wins, and picking for them is how an agent's work disappears.
    """


def write_document(document: FileDocument, text: str, *, force: bool = False) -> FileDocument:
    """Save edited text, refusing if the file changed since it was read."""
    target = document.path
    if not force and document.signature:
        current = file_signature(target)
        if current and current != document.signature:
            raise ExternalEditError(
                "이 파일이 열어둔 뒤 디스크에서 바뀌었어요. 레비가 수정했을 수 있어요."
            )
    encoding = "utf-8" if document.encoding == "utf-8-sig" else document.encoding
    payload = text.encode(encoding, "replace")
    # Write beside the target and replace: a reader that catches the write
    # halfway must never see a truncated source file.
    temporary = target.with_name(f".{target.name}.revvy-tmp")
    try:
        temporary.write_bytes(payload)
        os.replace(temporary, target)
    except OSError as exc:
        temporary.unlink(missing_ok=True)
        raise OSError(f"파일을 저장하지 못했어요: {exc}") from exc
    return dataclasses.replace(
        document, text=text, signature=hashlib.sha256(payload).hexdigest()
    )
