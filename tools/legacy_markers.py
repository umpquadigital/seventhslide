#!/usr/bin/env python3
"""Check that src/ keeps no path for what an earlier build made.

No build of SeventhSlide was released, so nothing reads what an earlier one
wrote: a library at another schema version is refused (db/schema.hpp), and no
reader keeps a fallback for an old name (docs/CODING.md). A ``k_legacy_``
name, or a comment justifying code by an earlier build, an older install,
a pre-release or how things were before the split, is how such a path comes
back. This fails on any of MARKERS in src/, unless ALLOWED says why it is
right there, and on an allowance that matches nothing.

    python3 tools/legacy_markers.py    # exit 1 and list each one found
"""
import re
import sys

from _check import report, source_files, unused

SUFFIXES = {".cpp", ".hpp", ".h", ".mm", ".qml", ".js", ".mjs", ".css", ".html",
            ".txt", ".in"}
MARKERS = ("k_legacy_", "before the split", "earlier build", "pre-release", "older install")
MARKER = re.compile("|".join(map(re.escape, MARKERS)), re.IGNORECASE)

# Each file, the markers it may hold, and why they are right there.
ALLOWED = {
    "src/core/db/schema.cpp": {
        # The message for a library at another version: development builds
        # made libraries at earlier versions, and this build refuses them.
        "pre-release": "the refusal of a library a development build made",
    },
}


def main() -> int:
    problems = []
    used = set()
    for rel, text in source_files(["src"], SUFFIXES):
        allowed = {m.lower() for m in ALLOWED.get(rel, {})}
        for n, line in enumerate(text.split("\n"), 1):
            for m in MARKER.finditer(line):
                marker = m.group(0).lower()
                if marker in allowed:
                    used.add((rel, marker))
                else:
                    problems.append(f"{rel}:{n}: {line.strip()}")
                    break
    problems += unused("legacy_markers.py", [
        f'{rel}: "{marker}"' for rel, markers in ALLOWED.items() for marker in markers
        if (rel, marker.lower()) not in used])
    return report("legacy markers", problems)


if __name__ == "__main__":
    sys.exit(main())
