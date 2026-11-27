"""What the tree checks share: the files they read, where a match is, and how
they report.

Each check imports it as ``_check``; ``python3 tools/X.py`` puts tools/ on
``sys.path`` from any directory.
"""
from __future__ import annotations

import pathlib
from collections.abc import Iterable, Iterator

ROOT = pathlib.Path(__file__).resolve().parent.parent


def source_files(dirs: Iterable[str], suffixes: Iterable[str],
                 skip: Iterable[str] = ()) -> Iterator[tuple[str, str]]:
    """Each file under ``dirs`` (of ROOT) with one of ``suffixes``, in order:
    its path from ROOT and its text. A file is left out when ``skip`` holds
    its path from ROOT or the name of a folder it is in."""
    suffixes, skip = set(suffixes), set(skip)
    for d in dirs:
        for path in sorted((ROOT / d).rglob("*")):
            if path.suffix not in suffixes or not path.is_file():
                continue
            rel = path.relative_to(ROOT)
            if rel.as_posix() in skip or skip & set(rel.parts[:-1]):
                continue
            yield rel.as_posix(), path.read_text(encoding="utf-8", errors="replace")


def line_of(text: str, pos: int) -> int:
    """The line (from 1) that ``pos`` in ``text`` is on."""
    return text.count("\n", 0, pos) + 1


def in_number(text: str, pos: int) -> bool:
    """Whether the ``'`` at ``pos`` is inside a number (1'000, 0xFF'FF), not
    the start of a character (u'.', L'x')."""
    k = pos
    while k and (text[k - 1].isalnum() or text[k - 1] in "_'"):
        k -= 1
    return k < pos and text[k].isdigit()


def blank(text: str, strings: bool = True) -> str:
    """``text`` with its comments blanked, and the insides of its strings and
    characters too unless ``strings`` is False; lengths and lines kept."""
    out = list(text)
    i, n = 0, len(text)

    def clear(a: int, b: int, comment: bool = False) -> None:
        if not comment and not strings:
            return  # a string's inside, kept
        for k in range(a, b):
            if out[k] != "\n":
                out[k] = " "

    while i < n:
        c = text[i]
        if text.startswith("//", i):
            end = text.find("\n", i)
            end = n if end < 0 else end
            clear(i, end, comment=True)
            i = end
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            clear(i, end, comment=True)
            i = end
        elif c == "R" and text.startswith('R"', i) and not (i and (text[i - 1].isalnum() or text[i - 1] == "_")):
            open_paren = text.find("(", i + 2)
            delim = text[i + 2:open_paren]
            end = text.find(")" + delim + '"', open_paren)
            end = n if end < 0 else end + len(delim) + 2
            clear(i + 2, end - 1)
            i = end
        elif c in "\"'":
            if c == "'" and in_number(text, i):
                i += 1  # a digit separator, 1'000
                continue
            # A string ends at its line's end at the latest: a quote that
            # opens none (JavaScript's /'/g) blanks no more than that line.
            k = i + 1
            while k < n and text[k] != c and text[k] != "\n":
                k += 2 if text[k] == "\\" else 1
            clear(i + 1, k)
            i = k + 1
        else:
            i += 1
    return "".join(out)


def unused(script: str, entries: Iterable[str]) -> list[str]:
    """A problem for each of a check's allowances that ``entries`` names as
    matching nothing: one gone frees no room for another."""
    return [f"tools/{script}: ALLOWED's {e} matches nothing; remove it" for e in entries]


def report(label: str, problems: list[str]) -> int:
    """Print ``problems`` and their count; the exit status (1 for any)."""
    for problem in problems:
        print(problem)
    print(f"{label}: {len(problems)} problem(s)")
    return 1 if problems else 0
