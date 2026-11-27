#!/usr/bin/env python3
"""Check that no source, test or fixture writes a name nothing reads.

SeventhSlide reads only what this build writes: no reader keeps a fallback for
a name no build writes any more (docs/CODING.md). A test or fixture that writes
one tests nothing, and is how a reader for it comes back. This fails on
any of RETIRED, as a quoted string, in src/, tests/ or fixtures/.

    python3 tools/retired_keys.py    # exit 1 and list each one found
"""
import re
import sys

from _check import report, source_files

# Each name, and what says it now.
RETIRED = {
    "enable_fade": "a theme's per-context transitions",
    "fade_duration": "a theme's per-context transitions",
    "highlight_font_size": "an element's active_line",
    "dim_color": "an element's other_lines",
    "dissolve": "the transition type fade",
    "service_groups": "a service's tags",
    "text_themes": "an output's song_themes, bible_themes and media_themes",
    "quick_transitions": "a take's Fade length, take_fade_ms",
}

DIRS = ("src", "tests", "fixtures")
# A one-off conversion's test writes the old names on purpose: the library it
# converts from. Each goes with its tool.
CONVERSION_TESTS = {"tests/test_convert_schema_14.cpp"}
SUFFIXES = {".cpp", ".hpp", ".h", ".qml", ".js", ".json", ".txt", ".py"}
QUOTED = re.compile(r'"(' + "|".join(map(re.escape, RETIRED)) + r')"')


def main() -> int:
    problems = []
    for rel, text in source_files(DIRS, SUFFIXES, skip=CONVERSION_TESTS):
        for n, line in enumerate(text.split("\n"), 1):
            for m in QUOTED.finditer(line):
                name = m.group(1)
                problems.append(f"{rel}:{n}: \"{name}\" (now {RETIRED[name]})")
    return report("retired keys", problems)


if __name__ == "__main__":
    sys.exit(main())
