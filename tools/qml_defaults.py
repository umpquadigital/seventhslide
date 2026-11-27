#!/usr/bin/env python3
"""Check that the editors' QML supplies no default of its own.

A value an editor shows comes from C++ as it paints (docs/CODING.md): the
session's maps carry every field, defaults filled in, so a view never writes
``x || 75``, ``x || "#ffd400"`` or ``x || "linear"`` - a second copy of a C++
constant, which drifts (the canvas once showed a chord gap of 0 where the
output painted 15). Only a null guard that falls back to nothing (``|| 0``,
``|| ""``, ``|| []``, ``|| ({})``) is allowed, and a function's optional
argument takes a default parameter (``function open(tab = "general")``).

    python3 tools/qml_defaults.py            # every editor (src/ui/editors)
    python3 tools/qml_defaults.py FILE.qml   # just these
"""
import os
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
EDITORS = [ROOT / "src" / "ui" / "editors"]
# ``|| <number other than 0>`` or ``|| "<anything>"``.
DEFAULT_RE = re.compile(r'\|\|\s*(?:(?!0\b)\d[\d.]*|"[^"]+")')


def shown(path: pathlib.Path) -> str:
    """``path`` from the repository's root, or as given when it is outside."""
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return os.fspath(path)


def check(path: pathlib.Path) -> list[str]:
    out = []
    for n, line in enumerate(path.read_text(encoding="utf-8").split("\n"), 1):
        if DEFAULT_RE.search(line):
            out.append(f"{shown(path)}:{n}: {line.strip()}")
    return out


def main() -> int:
    paths = [pathlib.Path(a).resolve() for a in sys.argv[1:]] or sorted(
        p for d in EDITORS for p in d.rglob("*.qml"))
    problems = [p for path in paths for p in check(path)]
    for p in problems:
        print(p)
    print(f"qml defaults: {len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
