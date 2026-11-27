#!/usr/bin/env python3
"""Check that an editor's state keeps no note of which menu is open.

Which menu is open is the view's: each menu keeps its own open flag, and
AppMenu closes itself on Escape, on a press anywhere, and when the item it
hangs from (its ``anchorItem``) is hidden (docs/CODING.md). A state that
keeps one (a ``menuKey`` with its ``setMenu``, a ``destMenuVisible``), or
what a menu is open for or hangs from (a ``destMenuId``, a
``destMenuAnchor``), is a second way of opening and closing it, which the
view then has to keep in step with the first. This fails on any property of
a ``*State.qml`` under src/ui whose name or type speaks of a menu, a popup or
a dropdown (a ``property bool menuOpen``, a ``property AppMenu picker``),
wherever on its line it is declared, and on those names anywhere in one. A
state may still list what a menu offers through functions, and ask its views
to close theirs with a signal. It goes by names, so a flag called something
else (a ``pickerShown``) gets past it. Comments are not read.

    python3 tools/menu_state.py    # exit 1 and list each one found
"""
import re
import sys

from _check import blank, line_of, report, source_files

NAMES = re.compile(r"\b(?:menuKey|openMenuKey|setMenu)\b")
# A property's declaration, anywhere on its line: its type and its name.
PROPERTY = re.compile(r"\bproperty\s+(\S+)\s+(\w+)")
MENU_WORD = re.compile(r"menu|popup|dropdown", re.I)


def main() -> int:
    problems = []
    for rel, text in source_files(["src/ui"], {".qml"}):
        if not rel.endswith("State.qml"):
            continue
        lines = text.split("\n")
        code = blank(text, strings=False)
        hits = {line_of(text, m.start()) for m in NAMES.finditer(code)}
        hits |= {line_of(text, m.start()) for m in PROPERTY.finditer(code)
                 if MENU_WORD.search(m.group(1)) or MENU_WORD.search(m.group(2))}
        for n in sorted(hits):
            problems.append(f"{rel}:{n}: {lines[n - 1].strip()} "
                            "(a menu keeps whether it is open, not the state)")
    return report("menu state", problems)


if __name__ == "__main__":
    sys.exit(main())
