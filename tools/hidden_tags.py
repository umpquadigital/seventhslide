#!/usr/bin/env python3
"""Check that every hidden Catch2 tag has its row in docs/BUILDING.md's table.

A hidden test (``[.bench]``, or ``[webengine][.]``) runs only when asked for,
so nothing notices when it stops working. docs/BUILDING.md lists each hidden tag in
a table, with the programs it runs from and what it is; each tag must have a
row there, naming exactly the programs whose files use it
(tests/CMakeLists.txt), and each row a tag still used.

    python3 tools/hidden_tags.py    # exit 1 and say what is not as listed
"""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

# A test case's tags: the string after its name, in TEST_CASE("name", "[a][b]"),
# TEMPLATE_TEST_CASE("name", "[a]", A, B) or TEST_CASE_METHOD(Fixture, "name", "[a]").
TAGS = re.compile(r'\b(?:TEMPLATE_(?:PRODUCT_|LIST_)?TEST_CASE|TEST_CASE|SCENARIO)(?:_METHOD)?\s*\(\s*'
                  r'(?:[^",()]+,\s*)?"(?:[^"\\]|\\.)*"\s*,\s*"((?:\[[^\]]*\])+)"')


def hidden_tags() -> dict[str, set[str]]:
    """Each hidden tag, by the test files that use it. A case hidden by
    ``[.]`` alone is known by its other tags."""
    out: dict[str, set[str]] = {}
    for path in sorted((ROOT / "tests").glob("*.cpp")):
        for match in TAGS.finditer(path.read_text(encoding="utf-8")):
            tags = re.findall(r"\[[^\]]*\]", match.group(1))
            if "[.]" in tags:
                hidden = [t for t in tags if t != "[.]"]
            else:
                hidden = [t for t in tags if t.startswith("[.")]
            for tag in hidden:
                out.setdefault(tag, set()).add(path.name)
    return out


def programs() -> dict[str, str]:
    """The program each test file is built into (tests/CMakeLists.txt)."""
    text = (ROOT / "tests" / "CMakeLists.txt").read_text(encoding="utf-8")
    text = re.sub(r"#[^\n]*", "", text)
    out: dict[str, str] = {}
    for m in re.finditer(r"\bseventhslide_add_tests\(\s*(\w+)([^)]*)\)", text):
        for source in re.findall(r"[\w.]+\.cpp", m.group(2)):
            out[source] = "seventhslide_tests_" + m.group(1)
    for m in re.finditer(r"\badd_executable\(\s*(\w+)([^)]*)\)", text):
        for source in re.findall(r"[\w.]+\.cpp", m.group(2)):
            out.setdefault(source, m.group(1))
    return out


def listed() -> dict[str, set[str]]:
    """docs/BUILDING.md's table of hidden tags: each tag, by the programs it names.
    ``_ui`` is ``seventhslide_tests_ui``."""
    out: dict[str, set[str]] = {}
    in_table = False
    for line in (ROOT / "docs" / "BUILDING.md").read_text(encoding="utf-8").splitlines():
        cells = [c.strip() for c in line.strip().strip("|").split("|")] if line.startswith("|") else []
        if cells[:2] == ["Tag", "Program"]:
            in_table = True
        elif not cells:
            in_table = False
        elif in_table and not set(cells[0]) <= set("-: "):
            tag = cells[0].strip("`")
            names = re.findall(r"`([\w.]+)`", cells[1])
            out[tag] = {"seventhslide_tests" + n if n.startswith("_") else n for n in names}
    return out


def main() -> int:
    table = listed()
    built_in = programs()
    used = hidden_tags()
    failed = False
    for tag, files in sorted(used.items()):
        want = {built_in.get(f, f"{f} (in no program)") for f in files}
        if tag not in table:
            failed = True
            print(f"{tag} ({', '.join(sorted(files))}) has no row in docs/BUILDING.md's hidden tests")
        elif table[tag] != want:
            failed = True
            print(f"{tag}: docs/BUILDING.md names {', '.join(sorted(table[tag]))}; "
                  f"its tests are in {', '.join(sorted(want))}")
    for tag in sorted(set(table) - set(used)):
        failed = True
        print(f"{tag} has a row in docs/BUILDING.md's hidden tests, and no hidden test has it")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
