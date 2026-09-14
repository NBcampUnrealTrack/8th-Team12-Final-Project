"""What changed, in a form a Text widget can paint.

Orca's backend never ships a patch - it returns both file contents whole and
lets the editor compute the diff. That contract is the right one to copy even
though the renderer here is nothing like Monaco: a patch is a lossy, quoted,
re-parsed representation of something both sides already have, and every bug in
a diff pipeline lives in the parser.

So this module takes two strings and produces lines in four kinds, which is all
a Tk ``Text`` needs to know to tag them:

    add      a line only in the new version
    del      a line only in the old version
    context  a line in both
    meta     a header - file path, hunk marker, truncation notice

Intra-line spans are computed too. Monaco highlights the changed characters
inside a modified line for free; here it costs one more SequenceMatcher pass,
and without it a one-character change looks identical to a rewritten line.
"""

from __future__ import annotations

import dataclasses
import difflib
import pathlib

# Caps, all for the same reason: a generated file or a binary that slipped
# through must not be able to freeze the window.
MAX_DIFF_LINES = 4000
MAX_LINE_LENGTH = 2000
# Lines of unchanged text kept around each change. Three is the git default and
# is enough to recognise where you are without burying the change.
CONTEXT_LINES = 3
# Below this ratio the two lines are different lines, not an edit of one, and
# character highlighting turns into confetti.
INTRALINE_MIN_RATIO = 0.5

KIND_ADD = "add"
KIND_DEL = "del"
KIND_CONTEXT = "context"
KIND_META = "meta"


@dataclasses.dataclass(frozen=True)
class DiffLine:
    kind: str
    text: str
    old_number: int | None = None
    new_number: int | None = None
    # (start, end) character ranges within `text` that actually differ.
    spans: tuple[tuple[int, int], ...] = ()


@dataclasses.dataclass
class FileDiff:
    """One file's change, plus the counts the header shows."""

    path: str
    lines: list[DiffLine]
    added: int = 0
    removed: int = 0
    binary: bool = False
    truncated: bool = False

    @property
    def changed(self) -> bool:
        return bool(self.added or self.removed or self.binary)

    @property
    def summary(self) -> str:
        if self.binary:
            return "바이너리"
        parts = []
        if self.added:
            parts.append(f"+{self.added}")
        if self.removed:
            parts.append(f"-{self.removed}")
        return " ".join(parts) or "변경 없음"


def _split(text: str) -> list[str]:
    if not text:
        return []
    lines = text.splitlines()
    return [line[:MAX_LINE_LENGTH] for line in lines]


def intraline_spans(old: str, new: str) -> tuple[tuple[tuple[int, int], ...], tuple[tuple[int, int], ...]]:
    """Character ranges that differ between two versions of one line.

    Returns (old_spans, new_spans). Empty when the lines are too dissimilar to
    be an edit of each other - highlighting every character of a rewritten line
    tells the reader nothing they cannot already see.
    """
    matcher = difflib.SequenceMatcher(None, old, new, autojunk=False)
    if matcher.quick_ratio() < INTRALINE_MIN_RATIO:
        return (), ()
    old_spans: list[tuple[int, int]] = []
    new_spans: list[tuple[int, int]] = []
    for tag, i1, i2, j1, j2 in matcher.get_opcodes():
        if tag in ("replace", "delete") and i2 > i1:
            old_spans.append((i1, i2))
        if tag in ("replace", "insert") and j2 > j1:
            new_spans.append((j1, j2))
    return tuple(old_spans), tuple(new_spans)


def diff_text(
    original: str,
    modified: str,
    path: str = "",
    *,
    context: int = CONTEXT_LINES,
    max_lines: int = MAX_DIFF_LINES,
) -> FileDiff:
    """Compare two file contents. Neither side is read from disk here."""
    old_lines = _split(original)
    new_lines = _split(modified)
    result = FileDiff(path=path, lines=[])
    if path:
        result.lines.append(DiffLine(KIND_META, path))
    matcher = difflib.SequenceMatcher(None, old_lines, new_lines, autojunk=False)
    groups = list(matcher.get_grouped_opcodes(context))
    if not groups:
        return result
    def emit(line: DiffLine) -> bool:
        """Append one line. False once the cap is reached.

        Checked per line, not per opcode: a single replace covering a whole
        generated file is one opcode, so an opcode-level check lets the exact
        case the cap exists for straight through.
        """
        if len(result.lines) >= max_lines:
            result.truncated = True
            result.lines.append(
                DiffLine(KIND_META, f"… 나머지는 생략했어요 ({max_lines}줄 초과)")
            )
            return False
        result.lines.append(line)
        return True

    for index, group in enumerate(groups):
        if index or len(result.lines) > (1 if path else 0):
            first = group[0]
            if not emit(DiffLine(KIND_META, f"@@ -{first[1] + 1} +{first[3] + 1} @@")):
                return result
        for tag, i1, i2, j1, j2 in group:
            if tag == "equal":
                for offset in range(i2 - i1):
                    if not emit(
                        DiffLine(
                            KIND_CONTEXT,
                            old_lines[i1 + offset],
                            i1 + offset + 1,
                            j1 + offset + 1,
                        )
                    ):
                        return result
                continue
            removed = old_lines[i1:i2]
            added = new_lines[j1:j2]
            # A one-for-one replacement is an edited line; pair them up so the
            # changed characters can be marked on both sides.
            pairs = len(removed) == len(added)
            for offset, line in enumerate(removed):
                spans = ()
                if pairs:
                    spans, _ = intraline_spans(line, added[offset])
                if not emit(DiffLine(KIND_DEL, line, i1 + offset + 1, None, spans)):
                    return result
                result.removed += 1
            for offset, line in enumerate(added):
                spans = ()
                if pairs:
                    _, spans = intraline_spans(removed[offset], line)
                if not emit(DiffLine(KIND_ADD, line, None, j1 + offset + 1, spans)):
                    return result
                result.added += 1
    return result


def diff_from_tool_call(name: str, arguments: dict, path_hint: str = "") -> FileDiff | None:
    """A preview of an edit an agent is making, from its own tool call.

    Deliberately not a real diff. The agent has told us the exact old and new
    text, so every old line is a deletion and every new line an addition - there
    is nothing to infer. This is what makes a change visible the moment the tool
    call streams in, long before it reaches the working tree.

    Field names vary across the CLIs Revvy drives, so several spellings are
    accepted rather than one being declared correct.
    """
    if name not in ("Edit", "MultiEdit", "Write", "str_replace", "apply_patch", "edit_file"):
        return None
    if not isinstance(arguments, dict):
        return None
    path = str(
        arguments.get("file_path")
        or arguments.get("path")
        or arguments.get("filePath")
        or path_hint
        or ""
    )
    old = arguments.get("old_string") or arguments.get("oldString") or arguments.get("old") or ""
    new = (
        arguments.get("new_string")
        or arguments.get("newString")
        or arguments.get("new")
        or arguments.get("content")
        or arguments.get("file_text")
        or ""
    )
    if not isinstance(old, str) or not isinstance(new, str):
        return None
    if not old and not new:
        return None
    display = pathlib.Path(path).name if path else "(경로 없음)"
    result = FileDiff(path=display, lines=[DiffLine(KIND_META, display)])
    for line in _split(old)[:MAX_DIFF_LINES]:
        result.lines.append(DiffLine(KIND_DEL, line))
        result.removed += 1
    for line in _split(new)[:MAX_DIFF_LINES]:
        result.lines.append(DiffLine(KIND_ADD, line))
        result.added += 1
    return result


def diff_against_head(project_dir: str, relative_path: str) -> FileDiff:
    """A file's working-tree change versus the last commit.

    Both sides are fetched whole - `git show HEAD:path` and the file itself -
    and compared here, never by parsing `git diff` output.
    """
    import revvy_files

    root = pathlib.Path(project_dir)
    code, original = revvy_files._run_git(root, "show", f"HEAD:{relative_path}")
    if code != 0:
        original = ""
    target = root / relative_path
    try:
        document = revvy_files.read_document(target)
    except OSError:
        return diff_text(original, "", relative_path)
    if document.binary:
        result = FileDiff(path=relative_path, lines=[DiffLine(KIND_META, relative_path)], binary=True)
        return result
    return diff_text(original, document.text, relative_path)
