#!/usr/bin/env python3
"""Check that every QML type, JS file and C++ QML type has a user.

A file the build lists is not a build error when nothing uses it: Qt compiles
it into the module, and it sits there, read by no one and drifting from what
the rest does. This fails on each of these that no other source names:

- a QML file under src/ui: its type, ``Name {``, ``Name.x``, ``: Name`` (not
  C++'s ``::Name``), ``property Name``, ``as Name``, ``instanceof Name``,
  ``list<Name>``; or its file, ``"Name.qml"`` (a Loader's source,
  Qt.createComponent), or ``"Name"`` in loadFromModule or the tests'
  create_from_module (any other string is only a word: ``qsTr("Close")``);
- a JS file under src/ui: ``Name.js`` in an import (``import "x/Name.js" as
  Y`` in QML, ``.import`` in JS);
- a class in src/app marked QML_ELEMENT, QML_SINGLETON or QML_NAMED_ELEMENT:
  its QML name, as for a QML file, or its C++ name.

The users are the .qml, .js, .cpp, .hpp and .mm files under src and tests,
comments and forward declarations (``class Name;``) left out; a file does
not use itself, nor a class its own .hpp and .cpp. CMakeLists.txt is not a user; a test is.

    python3 tools/qml_unused.py    # exit 1 and list each one nothing uses
"""

# The `str | None` annotations on macOS's own Python 3.9 too.
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
UI = ROOT / "src" / "ui"
APP = ROOT / "src" / "app"
SUFFIXES = {".qml", ".js", ".cpp", ".hpp", ".mm"}

# Used, though no source names them as above: the QML name. None today; each
# needs a reason (a Loader whose source is a computed string, say).
ALLOWED: set[str] = set()

# ``//`` and ``/* */`` comments that start a line or follow a space: not a
# URL's ``//`` nor a ``"image/*"``.
LINE_COMMENT = re.compile(r"(?:^|(?<=\s))//.*$", re.MULTILINE)
BLOCK_COMMENT = re.compile(r"(?:^|(?<=\s))/\*.*?\*/", re.DOTALL)
# ``class Name;``: says a type exists, not that anything uses it.
FORWARD = re.compile(r"^\s*(?:class|struct)\s+\w+\s*;", re.MULTILINE)
# What makes a class or namespace a QML type, and the class or namespace it is
# in: the innermost one still open where it stands (of ``a::b``, ``b``).
QML_MACRO = re.compile(r"\bQML_(ELEMENT|SINGLETON|NAMED_ELEMENT\((\w+)\))")
SCOPE = re.compile(r"^\s*(?:class|struct|namespace)\s+(?:\w+::)*(\w+)\b[^;\n]*$", re.MULTILINE)
WORD = re.compile(r"\w+")


def code(path: pathlib.Path) -> str:
    text = path.read_text(encoding="utf-8")
    return FORWARD.sub("", LINE_COMMENT.sub("", BLOCK_COMMENT.sub("", text)))


def scope_at(text: str, at: int) -> str | None:
    """The innermost class, struct or namespace whose braces hold ``at``."""
    inner = None
    for s in SCOPE.finditer(text, 0, at):
        brace = text.find("{", s.end(1))
        if brace == -1 or brace > at:
            continue
        depth = 0
        for ch in text[brace:at]:
            depth += {"{": 1, "}": -1}.get(ch, 0)
        if depth > 0:
            inner = s.group(1)
    return inner


def qml_use(name: str) -> re.Pattern:
    n = re.escape(name)
    return re.compile(
        rf"\b{n}\s*[{{.]"
        rf"|(?:(?<!:):(?!:)|\bproperty|\bas|\binstanceof|<)\s*{n}\b"
        rf"|[\"'/]{n}\.qml[\"']"
        rf"|\b(?:loadFromModule|create_from_module)\([^)]*[\"']{n}[\"']")


def js_use(name: str) -> re.Pattern:
    return re.compile(rf"[\"'/]{re.escape(name)}\.js[\"']")


def cpp_use(name: str) -> re.Pattern:
    return re.compile(rf"\b{re.escape(name)}\b")


def native_types() -> list[tuple[str, list[str], list[re.Pattern], set[pathlib.Path]]]:
    """(label, its names, what names it, its own files) for each C++ QML type."""
    out = []
    for header in sorted(APP.glob("*.hpp")):
        text = code(header)
        own = {header, header.with_suffix(".cpp")}
        # C++ name -> QML name: QML_NAMED_ELEMENT's, else the C++ one.
        names: dict[str, str] = {}
        for m in QML_MACRO.finditer(text):
            scope = scope_at(text, m.start())
            if scope:
                names.setdefault(scope, scope)
                if m.group(2):
                    names[scope] = m.group(2)
        for cpp_name, qml_name in names.items():
            out.append((f"{header.relative_to(ROOT)}: {qml_name}", [qml_name, cpp_name],
                        [qml_use(qml_name), cpp_use(cpp_name)], own))
    return out


def main() -> int:
    sources = {p: code(p) for top in ("src", "tests")
               for p in sorted((ROOT / top).rglob("*")) if p.suffix in SUFFIXES}
    # Which sources hold each word: only those can name it.
    holding: dict[str, set[pathlib.Path]] = {}
    for path, text in sources.items():
        for word in set(WORD.findall(text)):
            holding.setdefault(word, set()).add(path)

    checks = []
    for path in sorted(UI.rglob("*.qml")):
        checks.append((str(path.relative_to(ROOT)), [path.stem], [qml_use(path.stem)], {path}))
    for path in sorted(UI.rglob("*.js")):
        checks.append((str(path.relative_to(ROOT)), [path.stem], [js_use(path.stem)], {path}))
    checks += native_types()

    problems = []
    for label, names, uses, own in checks:
        if names[0] in ALLOWED:
            continue
        users = set().union(*(holding.get(n, set()) for n in names)) - own
        if not any(u.search(sources[path]) for path in sorted(users) for u in uses):
            problems.append(f"{label}: no source names it")
    for problem in problems:
        print(problem)
    print(f"qml unused: {len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
