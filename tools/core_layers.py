#!/usr/bin/env python3
"""Check that src/core's modules include only the modules beneath them.

Core is one static library, so a cycle between its modules still builds; this
is the check that keeps the layering in docs/CODING.md true. A module may
include its own headers and those of any module in a lower tier. Modules in
the same tier are independent of each other.

    python3 tools/core_layers.py        # exit 1 and list each wrong include

Core must not include src/devsupport either (that library is for tests and
tools only).
"""

# The `str | None` annotations on macOS's own Python 3.9 too.
from __future__ import annotations

import pathlib
import re
import sys

# Lowest first. Keep in step with docs/CODING.md.
TIERS = [
    ["util"],
    ["json", "geom", "platform", "media", "crypto"],
    ["fonts", "library", "update"],
    ["model"],
    ["parse"],
    ["canvas"],
    ["layout"],
    ["freeform"],
    ["theme", "db"],
    ["build", "sync", "exchange"],
    ["scene"],
    ["session"],
]

ROOT = pathlib.Path(__file__).resolve().parent.parent / "src" / "core"
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]seventhslide/(\w+)/')


def module_of(path: pathlib.Path) -> str | None:
    rel = path.relative_to(ROOT).parts
    if rel[0] == "include":
        rel = rel[2:]  # include/seventhslide/<module>/...
    return rel[0] if len(rel) > 1 else None


def main() -> int:
    tier = {m: i for i, names in enumerate(TIERS) for m in names}
    problems: list[str] = []
    for path in sorted(ROOT.rglob("*")):
        if path.suffix not in (".hpp", ".cpp"):
            continue
        mod = module_of(path)
        if mod is None:
            continue
        if mod not in tier:
            problems.append(f"{path.relative_to(ROOT)}: module '{mod}' has no tier")
            continue
        for n, line in enumerate(path.read_text(encoding="utf-8").split("\n"), 1):
            m = INCLUDE.match(line)
            if not m or m.group(1) == mod:
                continue
            dep = m.group(1)
            where = f"{path.relative_to(ROOT)}:{n}"
            if dep == "devsupport":
                problems.append(f"{where}: core includes devsupport")
            elif dep not in tier:
                problems.append(f"{where}: '{dep}' has no tier")
            elif tier[dep] >= tier[mod]:
                problems.append(f"{where}: {mod} includes {dep}/")
    for p in problems:
        print(p)
    print(f"core layers: {len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
