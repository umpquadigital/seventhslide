#!/usr/bin/env python3
"""Check that every name a C++ class shows QML is one QML uses.

A ``Q_INVOKABLE`` or ``Q_PROPERTY`` is how QML reaches C++ by name, and it
costs what plain C++ does not: a camelCase name (docs/CODING.md), arguments
turned into QVariants and back, a door a view may open. One that QML stopped
using, kept for C++ or a test that calls it directly, is plain C++ instead,
snake_case; one nothing uses is deleted. This fails on each, in src, that
nothing below uses:

- a ``Q_INVOKABLE``, called in src/ui's QML or JavaScript as ``x.name(``, or
  as ``name(`` on its own (a view inside the C++ type it is on; not in a
  file with a ``function`` or ``signal`` of that name), or passed uncalled
  as ``x.name`` (``Qt.callLater(x.name)``, ``s.connect(x.name)``);
- a ``Q_PROPERTY``, in the same files: read or set as ``x.name``, bound as
  ``name:`` (an object's binding, a key of the map createObject is given),
  animated as ``Behavior on name {``, handled as ``onNameChanged`` (whatever
  its NOTIFY signal is called), or named as a string to a ``property:``, a
  ``properties:`` (each name between its commas) or a ``[]`` lookup;
- either, by its string in src's C++ (tests are not users): given to
  ``invokeMethod``, ``property``, ``setProperty``, ``QQmlProperty``,
  ``indexOfProperty``, ``indexOfMethod``, ``Q_CLASSINFO``, or ``SIGNAL`` or
  ``SLOT`` (as one of the lookup's own arguments, not inside a lambda or a
  call given to it); or used, as above, in QML that C++ keeps in a raw
  string (``R"(import QtQuick ...)"``).

The remote does not count: its bridge calls the controllers as C++, and the
web remote sends commands by their names on the wire (remote_protocol.hpp),
never through the meta-object. Comments are not read, nor strings but where
said. A name is known by its spelling alone: a property ``count`` is used
wherever any ``x.count`` is.

ALLOWED lists what a test must reach through the meta-object (a QML test of
a C++ type), each with why; one that matches nothing fails too.

    python3 tools/qml_api_unused.py    # exit 1 and list each one nothing uses
"""
from __future__ import annotations

import re
import sys

from _check import blank, line_of, report, source_files, unused

CPP = {".cpp", ".hpp", ".h", ".mm"}

# "Class::name": why a test needs it by name. None today.
ALLOWED: dict[str, str] = {}

INVOKABLE = re.compile(r"\bQ_INVOKABLE\b[^;{(]*?\b(\w+)\s*\(")
PROPERTY = re.compile(r"\bQ_PROPERTY\s*\(([^)]*)\)")
PROPERTY_WORDS = {"READ", "WRITE", "MEMBER", "RESET", "NOTIFY", "BINDABLE"}
# A class or struct with a body (not ``enum class``, not ``class X;``).
CLASS = re.compile(r"(?<!enum\s)\b(?:class|struct)\s+(?:\w+\s+)*?(\w+)\s*(?:final\s*)?(?::[^;{()]*)?\{")

# In QML and JS, comments and strings blanked.
CALL = re.compile(r"\.\s*(\w+)\s*\(")
BARE_CALL = re.compile(r"(?<![\w.$])(\w+)\s*\(")
FUNCTION = re.compile(r"\bfunction\s+(\w+)")
SIGNAL = re.compile(r"\bsignal\s+(\w+)")
MEMBER = re.compile(r"\.\s*(\w+)\b(?!\s*\()")
BINDING = re.compile(r"(?:^|[{;,])\s*(\w+)\s*:(?!:)", re.MULTILINE)
ON = re.compile(r"\bon\s+(\w+)\s*\{")
HANDLER = re.compile(r"\bon([A-Z]\w*)Changed\b")
# In QML and JS, comments blanked, strings kept.
NAMED = re.compile(r"\bproperty\s*:\s*[\"'](\w+)[\"']|\[\s*[\"'](\w+)[\"']\s*\]")
NAMED_LIST = re.compile(r"\bproperties\s*:\s*[\"']([\w\s,]*)[\"']")
# In C++, comments blanked, strings kept.
LOOKUP = re.compile(r"\b(?:invokeMethod|property|setProperty|QQmlProperty(?:::read|::write)?|"
                    r"indexOfProperty|indexOfMethod|Q_CLASSINFO)\s*\(")
SIGNAL_SLOT = re.compile(r"\b(?:SIGNAL|SLOT)\s*\(\s*(\w+)")
STRING = re.compile(r'"(\w+)"')
RAW_QML = re.compile(r'R"(\w*)\((\s*import\s+Qt.*?)\)\1"', re.DOTALL)


def closing(text: str, at: int) -> int:
    """Where the bracket opened at ``at`` closes."""
    depth = 0
    for k in range(at, len(text)):
        if text[k] in "({[":
            depth += 1
        elif text[k] in ")}]":
            depth -= 1
            if depth == 0:
                return k
    return len(text)


def given_strings(code: str, kept: str, at: int) -> list[str]:
    """The strings among the arguments of the call whose ``(`` is at ``at``
    in ``code`` (strings blanked; ``kept`` with them): not one inside a
    lambda's body or another call it is given."""
    end = closing(code, at)
    out = []
    depth, k = 0, at
    for m in STRING.finditer(kept, at, end):
        for c in code[k:m.start()]:
            depth += 1 if c in "({[" else -1 if c in ")}]" else 0
        k = m.start()
        if depth == 1:
            out.append(m.group(1))
    return out


def declarations() -> list[tuple[str, int, str, str, str]]:
    """(file, line, class, name, what) for each Q_INVOKABLE and Q_PROPERTY."""
    out = []
    for rel, text in source_files(["src"], CPP):
        code = blank(text)
        classes = [(m.end() - 1, closing(code, m.end() - 1), m.group(1)) for m in CLASS.finditer(code)]

        def owner(at: int) -> str:
            inside = [(start, name) for start, end, name in classes if start < at < end]
            return max(inside)[1] if inside else "?"

        for m in INVOKABLE.finditer(code):
            out.append((rel, line_of(text, m.start()), owner(m.start()), m.group(1), "Q_INVOKABLE"))
        for m in PROPERTY.finditer(code):
            words = m.group(1).split()
            head = next((i for i, w in enumerate(words) if w in PROPERTY_WORDS), len(words))
            name = re.findall(r"\w+", " ".join(words[:head]))[-1]
            out.append((rel, line_of(text, m.start()), owner(m.start()), name, "Q_PROPERTY"))
    return out


def qml_uses(code: str, kept: str, calls: set[str], props: set[str]) -> None:
    """Add what QML or JS ``code`` (strings blanked; ``kept`` with them) uses."""
    # A bare call of a name the file has a function or signal of is to that
    # (and a signal's declaration is no call).
    defined = set(FUNCTION.findall(code)) | set(SIGNAL.findall(code))
    calls.update(CALL.findall(code))
    calls.update(set(BARE_CALL.findall(code)) - defined)
    props.update(MEMBER.findall(code))
    props.update(BINDING.findall(code))
    props.update(ON.findall(code))
    props.update(h[0].lower() + h[1:] for h in HANDLER.findall(code))
    props.update(a or b for a, b in NAMED.findall(kept))
    props.update(n.strip() for names in NAMED_LIST.findall(kept) for n in names.split(",") if n.strip())


def main() -> int:
    calls: set[str] = set()
    props: set[str] = set()
    for _, text in source_files(["src/ui"], {".qml", ".js"}):
        qml_uses(blank(text), blank(text, strings=False), calls, props)
    by_string: set[str] = set()
    for _, text in source_files(["src"], CPP):
        code, kept = blank(text), blank(text, strings=False)
        for m in LOOKUP.finditer(kept):
            by_string.update(given_strings(code, kept, m.end() - 1))
        by_string.update(SIGNAL_SLOT.findall(kept))
        for m in RAW_QML.finditer(kept):
            qml_uses(blank(m.group(2)), blank(m.group(2), strings=False), calls, props)

    # An invokable passed uncalled, as x.name, is among props.
    used = {"Q_INVOKABLE": calls | props, "Q_PROPERTY": props}
    problems = []
    allowed = set()
    for rel, n, cls, name, what in declarations():
        if name in used[what] or name in by_string:
            continue
        if f"{cls}::{name}" in ALLOWED:
            allowed.add(f"{cls}::{name}")
            continue
        problems.append(f"{rel}:{n}: {cls}::{name} (a {what} no QML uses: make it plain C++, "
                        f"snake_case, or delete it)")
    problems += unused("qml_api_unused.py", sorted(set(ALLOWED) - allowed))
    return report("qml api unused", problems)


if __name__ == "__main__":
    sys.exit(main())
