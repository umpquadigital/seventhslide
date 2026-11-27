#!/usr/bin/env python3
"""Check that docs/SERVER.md names every command the server takes, and no other.

src/server/command_line.cpp reads the word after ``--server`` as the command
(``word == "join"``); docs/SERVER.md's list of them, its "The commands"
section, must show each as ``seventhslide --server WORD``. This fails on any it
leaves out, and on a ``seventhslide --server WORD`` anywhere in the doc that
names no command.

    python3 tools/server_commands.py    # exit 1 and list each one missing or unknown
"""
import pathlib
import re
import sys

from _check import line_of

ROOT = pathlib.Path(__file__).resolve().parent.parent
COMMAND_LINE = ROOT / "src" / "server" / "command_line.cpp"
DOC = ROOT / "docs" / "SERVER.md"
COMMAND = re.compile(r'\bword == "([^"]+)"')
SECTION = "## The commands"
# A command the doc runs: a word after --server, not an option or "...".
DOC_COMMAND = re.compile(r"seventhslide --server ([a-z][\w-]*)")


def commands_section(doc: str) -> str:
    """The doc's "The commands" section, up to the next heading of its level."""
    start = doc.find("\n" + SECTION + "\n")
    if start < 0:
        return ""
    end = doc.find("\n## ", start + 1)
    return doc[start:] if end < 0 else doc[start:end]


def main() -> int:
    commands = sorted(set(COMMAND.findall(COMMAND_LINE.read_text(encoding="utf-8"))))
    if not commands:
        print(f"server commands: none found in {COMMAND_LINE.relative_to(ROOT)}; "
              "has the parser changed?")
        return 1
    text = DOC.read_text(encoding="utf-8")
    doc = commands_section(text)
    if not doc:
        print(f"server commands: {DOC.relative_to(ROOT)} has no \"{SECTION}\" section")
        return 1
    problems = [f"{DOC.relative_to(ROOT)}: \"{SECTION}\" has no `seventhslide --server {c}`"
                for c in commands
                if not re.search(r"seventhslide --server " + re.escape(c) + r"\b", doc)]
    for m in DOC_COMMAND.finditer(text):
        if m.group(1) not in commands:
            n = line_of(text, m.start())
            problems.append(f"{DOC.relative_to(ROOT)}:{n}: `seventhslide --server {m.group(1)}` "
                            f"is no command of {COMMAND_LINE.relative_to(ROOT)}")
    for problem in problems:
        print(problem)
    print(f"server commands: {len(commands)} checked, {len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
