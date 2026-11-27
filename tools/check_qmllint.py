#!/usr/bin/env python3
"""Fail when qmllint reports anything at warning level or above.

qmllint 6.8's own --max-warnings / MaxWarnings path crashes on this module, so
CI builds the seventhslide_ui_qmllint_json target and runs this over its
report instead. Which categories count as warnings is src/ui/.qmllint.ini.

Usage: check_qmllint.py <build>/seventhslide_ui_qmllint.json
"""

import json
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    with open(sys.argv[1], encoding="utf-8") as f:
        report = json.load(f)
    problems = []
    for entry in report.get("files", []):
        for w in entry.get("warnings", []):
            if w.get("type") in ("warning", "critical"):
                problems.append(
                    f'{entry.get("filename")}:{w.get("line")}:{w.get("column")}: '
                    f'{w.get("message")} [{w.get("id")}]'
                )
        if not entry.get("success", True) and not entry.get("warnings"):
            problems.append(f'{entry.get("filename")}: qmllint failed on this file')
    for p in problems:
        print(p)
    print(f"qmllint: {len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
