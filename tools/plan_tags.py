#!/usr/bin/env python3
"""Check that no comment points at the deleted editor plan.

The plan the slide editor was built from is gone, so a tag naming one of its
decisions, questions, sections or stages, or the overhaul it planned, points
at nothing. A comment says what it means itself, or names the docs/CODING.md
section that does. This fails on any such tag in src/, tests/ or tools/,
unless ALLOWED says why it is right there, and on an allowance that matches
nothing.

    python3 tools/plan_tags.py    # exit 1 and list each one found
"""
import re
import sys

from _check import report, source_files, unused

DIRS = ("src", "tests", "tools")
SUFFIXES = {".cpp", ".hpp", ".h", ".mm", ".qml", ".js", ".mjs", ".css", ".html",
            ".py", ".txt", ".md", ".in"}
SKIP = {"node_modules", "playwright-report", "test-results"}
# A decision by number in brackets (not a word such as D3D11), a section of
# the plan, in brackets or by name, a decision or question of the plan, a
# stage in brackets, or the overhaul the plan was for. None of them is
# written out here, or this file would hold one.
TAG = re.compile(
    r"\(D[0-9]+\b"
    r"|\(plan [0-9]"
    r"|\b[Pp]lan (?:D|Q)[0-9]"
    r"|\b(?i:plan(?:'s)?[\s,(]*section [0-9])"
    r"|\((?i:stage) [0-9]"
    r"|(?i:\beditor[ ]overhaul\b)")

# (file, what its line says): why it is no tag.
ALLOWED: dict[tuple[str, str], str] = {
    # e.g. ("src/render/colour.cpp", "D65)"): "the CIE illuminant, not a decision",
}


def main() -> int:
    problems = []
    used = set()
    for rel, text in source_files(DIRS, SUFFIXES, skip=SKIP):
        for n, line in enumerate(text.split("\n"), 1):
            if not TAG.search(line):
                continue
            allowed = [(f, what) for f, what in ALLOWED if f == rel and what in line]
            if allowed:
                used.update(allowed)
            else:
                problems.append(f"{rel}:{n}: {line.strip()}")
    problems += unused("plan_tags.py", [f'{f}: "{what}"' for f, what in ALLOWED
                                        if (f, what) not in used])
    return report("plan tags", problems)


if __name__ == "__main__":
    sys.exit(main())
