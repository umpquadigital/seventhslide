#!/usr/bin/env python3
"""Check that a view types into the control kit's fields, not one of its own.

A text field a window types into is a ``DialogField``, or a kit control built
on one (src/ui/controls), so every field has one height, one face and one
focus edge (docs/CODING.md). A view that makes its own from a bare
``TextInput { ... }``, or from Qt Quick Controls' ``TextField`` or
``TextArea``, is a copy of DialogField, which drifts from it. This fails on
one of those in src/ui's QML outside src/ui/controls. ALLOWED lists the few
fields, each by its file and its ``id``, that have a part of their own that
no control has; a second field in one of those files fails, and an entry
that matches nothing fails too. Comments are not read.

    python3 tools/text_fields.py    # exit 1 and list each one found
"""
import re
import sys

from _check import blank, line_of, report, source_files, unused

CONTROLS = "src/ui/controls/"
# (file, the field's id): why the field is its own.
ALLOWED = {
    ("src/ui/operator/LibraryPanel.qml", "libSearchInput"):
        "the library's search: its glyph and clear button, and keys each tab answers",
    ("src/ui/operator/ControllerAddressBar.qml", "address"):
        "the browser's address bar: its globe, a whole-address first click, a refusal's edge",
    ("src/ui/editors/slide/SlideSorter.qml", "cueRenameInput"):
        "a cue renamed in place on its row, in the row's ink, with no field face",
    ("src/ui/editors/slide/RibbonField.qml", "input"):
        "the ribbon's number field: a control of its own, with its stepper and the ribbon's height",
}

FIELD = re.compile(r"\b(?:TextInput|TextField|TextArea)\s*\{")
ID = re.compile(r"\bid\s*:\s*(\w+)")


def field_id(code: str, brace: int) -> str:
    """The ``id`` the object whose ``{`` is at ``brace`` in ``code`` gives
    itself, or "": read from its own lines, not its children's."""
    depth, own = 0, []
    for c in code[brace:]:
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                break
        own.append(c if depth == 1 else " ")
    m = ID.search("".join(own))
    return m.group(1) if m else ""


def main() -> int:
    problems = []
    used = set()
    for rel, text in source_files(["src/ui"], {".qml"}):
        if rel.startswith(CONTROLS):
            continue
        lines = text.split("\n")
        code = blank(text)
        for m in FIELD.finditer(code):
            field = (rel, field_id(code, m.end() - 1))
            if field in ALLOWED:
                used.add(field)
                continue
            n = line_of(text, m.start())
            problems.append(f"{rel}:{n}: {lines[n - 1].strip()} (a DialogField, or a control built on one)")
    problems += unused("text_fields.py", [f"{f}: {i}" for f, i in sorted(set(ALLOWED) - used)])
    return report("text fields", problems)


if __name__ == "__main__":
    sys.exit(main())
