#!/usr/bin/env python3
"""Check that the QML views take their colours from Style.qml.

Colours come from Style.qml's role tokens (docs/CODING.md): a view that writes
one itself is a second copy of a token, which drifts. This fails, in src/ui's
QML and JavaScript, on

  - a "#hex" colour;
  - a named colour ("black", "white", ...) given to a colour, a fill or an
    ink, not compared against (a ``tone: "blue"`` is a name, not a colour);
  - ``Icons.url(name, "...")``: a glyph's ink written out.

``transparent`` is no colour and may be written. Style.qml is where the
colours are, and ColorPicker draws a spectrum and starts on white; both are
left out. ALLOWED lists the few others that are right as they are; one that
matches nothing fails too. Comments are not read.

    python3 tools/colour_literals.py    # exit 1 and list each one found
"""
import re
import sys

from _check import blank, line_of, report, source_files, unused

SKIPPED = {"src/ui/Style.qml", "src/ui/controls/ColorPicker.qml"}
# (file, what it writes): why it is right.
ALLOWED = {
    ("src/ui/editors/slide/RibbonGallery.qml", '"fade_through_black" ? "black"'):
        "the card shows the transition's own black, which no theme changes",
}

# The SVG colour names QML takes, but transparent.
NAMES = """aliceblue antiquewhite aqua aquamarine azure beige bisque black
blanchedalmond blue blueviolet brown burlywood cadetblue chartreuse chocolate
coral cornflowerblue cornsilk crimson cyan darkblue darkcyan darkgoldenrod
darkgray darkgreen darkgrey darkkhaki darkmagenta darkolivegreen darkorange
darkorchid darkred darksalmon darkseagreen darkslateblue darkslategray
darkslategrey darkturquoise darkviolet deeppink deepskyblue dimgray dimgrey
dodgerblue firebrick floralwhite forestgreen fuchsia gainsboro ghostwhite gold
goldenrod gray grey green greenyellow honeydew hotpink indianred indigo ivory
khaki lavender lavenderblush lawngreen lemonchiffon lightblue lightcoral
lightcyan lightgoldenrodyellow lightgray lightgreen lightgrey lightpink
lightsalmon lightseagreen lightskyblue lightslategray lightslategrey
lightsteelblue lightyellow lime limegreen linen magenta maroon mediumaquamarine
mediumblue mediumorchid mediumpurple mediumseagreen mediumslateblue
mediumspringgreen mediumturquoise mediumvioletred midnightblue mintcream
mistyrose moccasin navajowhite navy oldlace olive olivedrab orange orangered
orchid palegoldenrod palegreen paleturquoise palevioletred papayawhip peachpuff
peru pink plum powderblue purple red rosybrown royalblue saddlebrown salmon
sandybrown seagreen seashell sienna silver skyblue slateblue slategray
slategrey snow springgreen steelblue tan teal thistle tomato turquoise violet
wheat white whitesmoke yellow yellowgreen""".split()

HEX = re.compile(r"""["']#(?:[0-9a-fA-F]{3,4}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})["']""")
# What something is compared with, ``=== "black"``: taken out before NAMED.
COMPARED = re.compile(r"""[=!]==?\s*["'][^"']*["']""")
NAMED = re.compile(r"""["'](?:""" + "|".join(NAMES) + r""")["']""", re.IGNORECASE)
# A line that sets a colour: ``color:``, ``border.color:``, ``textColor:``,
# ``fillStyle =``, ``ink:``, ``property color x:``. The name ends in the word
# (so not ``link:`` or ``fillMode:``), which starts it or is capitalised.
SETS_COLOUR = re.compile(
    r"(?<!\w)(?:\w*(?:Colou?r|Ink|Fill|Stroke)|colou?r|ink|fill|stroke)(?:Style|Colou?r)?"
    r"\s*[:=](?!=)"
    r"|\bproperty\s+colou?r\s+\w+\s*:")
# Read across lines, so a call broken over them is one; its name may be a call.
ICON_INK = re.compile(r"""Icons\.url\((?:[^(),]|\([^()]*\))*,\s*["']""")


def check(rel: str, text: str, used: set) -> list[str]:
    raw_lines = text.split("\n")
    code = blank(text, strings=False)  # comments blanked, lines kept
    hits = set()
    for n, line in enumerate(code.split("\n"), 1):
        if HEX.search(line) or (SETS_COLOUR.search(line) and NAMED.search(COMPARED.sub("", line))):
            hits.add(n)
    for m in ICON_INK.finditer(code):
        hits.add(line_of(code, m.start()))
    problems = []
    for n in sorted(hits):
        line = raw_lines[n - 1].strip()
        allowed = [(f, what) for f, what in ALLOWED if f == rel and what in line]
        if allowed:
            used.update(allowed)
        else:
            problems.append(f"{rel}:{n}: {line}")
    return problems


def main() -> int:
    problems = []
    used = set()
    for rel, text in source_files(["src/ui"], {".qml", ".js"}, skip=SKIPPED):
        problems += check(rel, text, used)
    problems += unused("colour_literals.py", [f'{f}: {what}' for f, what in ALLOWED
                                              if (f, what) not in used])
    return report("colour literals", problems)


if __name__ == "__main__":
    sys.exit(main())
