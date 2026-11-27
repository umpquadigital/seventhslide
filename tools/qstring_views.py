#!/usr/bin/env python3
"""Check that a string view becomes a QString as docs/CODING.md spells it.

A ``std::string_view`` (or ``std::string``) becomes a ``QString`` with
``QString::fromUtf8(view)``, which takes it as a ``QByteArrayView``. Spelled
out as ``fromUtf8(view.data(), view.size())`` it is longer and says nothing
more, and ``fromUtf8(view.data())`` with no length reads to a NUL a view does
not promise. This fails on ``fromUtf8(``, ``fromLatin1(`` or ``fromLocal8Bit(``
given a ``.data()``, with a length or without, in src/, wherever the call
breaks its line.

    python3 tools/qstring_views.py    # exit 1 and list each one found
"""
import re
import sys

from _check import line_of, report, source_files

SUFFIXES = {".cpp", ".hpp", ".h", ".mm"}
SPELLED_OUT = re.compile(r"from(?:Utf8|Latin1|Local8Bit)\([^()]*\.data\(\)\s*[,)]")


def main() -> int:
    problems = []
    for rel, text in source_files(["src"], SUFFIXES):
        lines = text.split("\n")
        for m in SPELLED_OUT.finditer(text):
            n = line_of(text, m.start())
            problems.append(f"{rel}:{n}: {lines[n - 1].strip()} (QString::fromUtf8(view))")
    return report("qstring views", problems)


if __name__ == "__main__":
    sys.exit(main())
