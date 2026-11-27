#!/usr/bin/env python3
"""Find QML files that reference an `id` belonging to a different file.

Extracting a panel out of a big QML file moves ids with it. A binding left
behind that still names one of them is not a build error and not a load error:
QML resolves it when the binding happens to run, and raises a ReferenceError
into the log. A guarded one (`typeof canvasStage !== "undefined"`) does not
even do that - it silently stops doing its job.

qmllint reports most of these, but as `[unqualified]`, the same category as the
harmless habit of writing `foo` for `root.foo`. This keeps only the subset that
is guaranteed to throw: a name that is declared as an `id:` somewhere in the
project but not in the file that uses it.

qmllint does not look inside a function nested in another (a callback passed
to `map` or `forEach`, an arrow function in a function body), and does not
count `typeof name` as a use. So each file's code is also scanned for the
other files' ids itself: `name.member` or `typeof name`, where the file does
not declare `name` in any way (an id, a property, a function, a variable, a
parameter).

    python3 tools/qml_stray_ids.py $(find src/ui -name '*.qml')

Needs qmllint on PATH or at QMLLINT; the import path is QMLLINT_I, build/qml by default.
"""
import collections
import os
import pathlib
import re
import subprocess
import sys

ID_RE = re.compile(r"^\s*id:\s*(\w+)\s*$", re.M)
# Either severity: src/ui/.qmllint.ini sets how loud the category is, and
# while it is "info" qmllint prints these as Info lines.
WARN_RE = re.compile(r"^(?:Warning|Info): (.+?):(\d+):(\d+): Unqualified access", re.M)


# What a view gives each delegate; a model object or id elsewhere may share
# the name.
DELEGATE_NAMES = {"model", "modelData", "index"}

# Everything else a file can call a name of its own.
DECLARED_RES = [
    re.compile(r"\bproperty\s+[\w<>.]+\s+(\w+)"),
    re.compile(r"\b(?:function|signal)\s+(\w+)"),
    re.compile(r"\b(?:let|const|var)\s+(\w+)"),
    re.compile(r"\bcomponent\s+(\w+)"),
    re.compile(r"\b(\w+)\s*=>"),
]
# Parameter lists: a function's, an arrow function's, a signal's.
PARAMS_RES = [
    re.compile(r"\bfunction\s*\w*\s*\(([^)]*)\)"),
    re.compile(r"\(([^()]*)\)\s*=>"),
    re.compile(r"\bsignal\s+\w+\s*\(([^)]*)\)"),
]


def ids_in(text: str) -> set[str]:
    return set(ID_RE.findall(text))


def code_only(text: str) -> str:
    """``text`` with comments and string contents blanked, keeping every
    line and column where it was."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        elif c in "\"'`":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(c + re.sub(r"[^\n]", " ", text[i + 1:j - 1]) + text[j - 1:j])
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def declared_in(code: str) -> set[str]:
    names = set(ID_RE.findall(code))
    for r in DECLARED_RES:
        names.update(r.findall(code))
    for r in PARAMS_RES:
        for params in r.findall(code):
            names.update(re.findall(r"(\w+)\s*(?:[,=]|$)", params.strip()))
            # A signal's are "type name".
            names.update(re.findall(r"\w+\s+(\w+)", params))
    return names


def text_uses(code: str, name: str):
    """(line, column) of each ``name.member`` or ``typeof name`` in ``code``."""
    pattern = re.compile(
        r"(?<![\w.$])" + re.escape(name) + r"\b(?=\s*\.)"
        r"|(?<=typeof )" + re.escape(name) + r"\b")
    for m in pattern.finditer(code):
        line = code.count("\n", 0, m.start()) + 1
        col = m.start() - code.rfind("\n", 0, m.start())
        yield line, col


def main() -> int:
    files = [pathlib.Path(p) for p in sys.argv[1:]]
    if not files:
        print(__doc__)
        return 2
    qmllint = os.environ.get("QMLLINT", "qmllint")
    inc = os.environ.get("QMLLINT_I", "build/qml")

    text = {f: f.read_text(encoding="utf-8") for f in files}
    code = {f: code_only(t) for f, t in text.items()}
    ids = {f: ids_in(t) for f, t in code.items()}
    # Every id the project declares anywhere, and where.
    owners: dict[str, set[pathlib.Path]] = collections.defaultdict(set)
    for f, names in ids.items():
        for n in names:
            owners[n].add(f)

    findings = 0
    for f in files:
        lines = text[f].split("\n")
        found: dict[tuple[int, str], int] = {}

        proc = subprocess.run([qmllint, "-I", inc, str(f)],
                              capture_output=True, text=True, encoding="utf-8", errors="replace")
        for path, line_s, col_s in WARN_RE.findall(proc.stdout + proc.stderr):
            line, col = int(line_s), int(col_s)
            if line > len(lines):
                continue
            m = re.match(r"\w+", lines[line - 1][col - 1:])
            if m:
                found.setdefault((line, m.group(0)), col)

        own = declared_in(code[f])
        for name in owners:
            if name in own:
                continue
            for line, col in text_uses(code[f], name):
                found.setdefault((line, name), col)

        for (line, name) in sorted(found):
            # Only ids, and only ids this file does not declare. A delegate's
            # own context names are not ids, whatever some file calls its id.
            if name not in owners or name in ids[f] or name in DELEGATE_NAMES:
                continue
            where = ", ".join(sorted(p.name for p in owners[name]))
            print(f"{f}:{line}: `{name}` is an id in {where}, not here")
            print(f"    {lines[line - 1].strip()}")
            findings += 1

    print(f"\n{findings} stray id reference(s)" if findings else "\nno stray id references")
    return 1 if findings else 0


if __name__ == "__main__":
    raise SystemExit(main())
