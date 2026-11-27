#!/usr/bin/env python3
"""Check that every `dialog.X` an extracted QML panel uses exists on its state.

A dialog's panels reach its view-model through one typed property,
``required property SomethingState dialog``. QML resolves the names used on it
lazily, so a name that does not exist is `undefined` at runtime, not a build
error - this is the check that catches it, for every panel in src/ui.

A delegate that the object it reaches into creates cannot name that type (Qt
refuses the cycle), so it holds it untyped and says what it is in a comment:
``required property var browser // MediaBrowser``. Its ``browser.X`` names are
checked against that type and the QML types it is built on.

    python3 tools/panel_refs.py                 # every panel with a typed `dialog`
    python3 tools/panel_refs.py PANEL.qml ...   # just these
"""
import re
import sys
import pathlib

UI = pathlib.Path(__file__).resolve().parent.parent / "src" / "ui"
DIALOG_RE = re.compile(r'^\s{4}required property (\w+) dialog\s*$', re.M)
UNTYPED_RE = re.compile(r'^\s{4}required property var (\w+)\s*//\s*(\w+)\s*$', re.M)


def members(path: pathlib.Path) -> set[str]:
    out: set[str] = set()
    for line in path.read_text(encoding="utf-8").split("\n"):
        m = re.match(r'\s{4}(?:(?:required|readonly)\s+)*property\s+(?:alias\s+)?[\w.<>]+\s+(\w+)', line)
        if m:
            out.add(m.group(1))
        m = re.match(r'\s{4}(?:function|signal)\s+(\w+)', line)
        if m:
            out.add(m.group(1))
    return out


# What every Qt Quick Item (and Rectangle) has, for a type built on one.
QT_ITEM_MEMBERS = {
    "x", "y", "z", "width", "height", "implicitWidth", "implicitHeight", "visible",
    "enabled", "opacity", "parent", "children", "focus", "activeFocus", "clip",
    "color", "radius", "border", "anchors", "mapToItem", "mapFromItem",
    "forceActiveFocus", "contains", "childAt", "state", "states",
}


def type_members(name: str) -> set[str]:
    """``name``'s members and those of the types under it: the QML types in
    src/ui, then a Qt Quick Item's."""
    out: set[str] = set()
    while True:
        found = list(UI.rglob(f"{name}.qml"))
        if not found:
            return out | QT_ITEM_MEMBERS
        text = found[0].read_text(encoding="utf-8")
        out |= members(found[0])
        base = re.search(r'^([A-Z]\w*)\s*\{', text, re.M)
        if not base:
            return out
        name = base.group(1)


def main() -> int:
    panels = [pathlib.Path(p) for p in sys.argv[1:]] or sorted(UI.rglob("*.qml"))
    by_state: dict[tuple[str, str], list[pathlib.Path]] = {}
    for panel in panels:
        text = panel.read_text(encoding="utf-8")
        m = DIALOG_RE.search(text)
        if m:
            by_state.setdefault((m.group(1), "dialog"), []).append(panel)
        for m in UNTYPED_RE.finditer(text):
            by_state.setdefault((m.group(2), m.group(1)), []).append(panel)
    bad = 0
    for (state, name), users in sorted(by_state.items()):
        print(f"== {state}")
        bad += check(type_members(state), name, users)
    return 1 if bad else 0


def check(declared: set[str], name: str, panels: list[pathlib.Path]) -> int:
    bad = 0
    for panel in panels:
        used = set(re.findall(rf'\b{name}\.(\w+)', panel.read_text(encoding="utf-8")))
        missing = sorted(u for u in used if u not in declared)
        print(f"{panel.name:32} uses {len(used):3}  missing: {missing or 'none'}")
        bad += len(missing)
    print("OK" if not bad else f"{bad} unresolved reference(s)")
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
