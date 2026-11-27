#!/usr/bin/env python3
"""Check that a file chooser's formats are not written out in QML.

Which files a picture, a video or a sound is made from is C++'s list
(``media_extensions`` in src/core/db/media_kind.cpp): a file chooser offers
them as ``session.media.mediaFilePatterns(kind)``. A filter that writes them
out (``"Videos (*.mp4 *.mov)"``) is a second copy, which offers what the
library refuses and leaves out what it takes. This fails, in src/ui's QML and
JavaScript, on

  - a ``*.ext`` of a media kind's extension, always;
  - any other ``*.ext``, unless ALLOWED names it for that file: the few
    formats (a ZIP export, a font, a song) that no list in C++ keeps for a
    chooser. An extension of an entry that matches nothing fails too.

Each kind of ``MediaKind`` must have its list read, with at least one
extension: ``media_extensions`` returning it for the kind, and the list's
quoted ``".ext"`` items in media_kind.cpp. A list moved, or written so it is
not read, fails here rather than letting its kind's patterns through.
Comments are not read.

    python3 tools/file_filters.py    # exit 1 and list each one found
"""
from __future__ import annotations

import re
import sys

from _check import ROOT, blank, line_of, report, source_files, unused

MEDIA_KIND = "src/core/include/seventhslide/db/media_kind.hpp"
MEDIA_KINDS = "src/core/db/media_kind.cpp"
# (file, the extensions it may write): why they are its own.
ALLOWED = {
    ("src/ui/operator/SongsTab.qml", "xml txt cho crd chopro chordpro chord pro usr bin"):
        "the song files the importers read, each of which says what it is; the filters only narrow the list",
    ("src/ui/settings/AppSettingsDialog.qml", "ttf otf ttc otc"):
        "the font files a family is added from",
    ("src/ui/settings/AppSettingsDialog.qml", "xml"):
        "a Bible's one format",
    ("src/ui/dialogs/ServiceExportPage.qml", "zip"):
        "the one file a service is exported to",
    ("src/ui/dialogs/ExportImagesDialog.qml", "zip"):
        "the one file the pictures are exported to",
}

PATTERN = re.compile(r"\*\.(\w+)")


def media_extensions() -> tuple[set[str], list[str]]:
    """Every media kind's extensions, without the dot, as C++ lists them; and
    a problem for each kind whose list is not read."""
    header = blank((ROOT / MEDIA_KIND).read_text(encoding="utf-8"), strings=False)
    source = blank((ROOT / MEDIA_KINDS).read_text(encoding="utf-8"), strings=False)
    enum = re.search(r"\benum\s+class\s+MediaKind\b[^{]*\{([^}]*)\}", header)
    kinds = [k.split("=")[0].strip() for k in enum.group(1).split(",")] if enum else []
    kinds = [k for k in kinds if k]
    if not kinds:
        return set(), [f"{MEDIA_KIND}: no MediaKind enum read"]
    body = re.search(r"\bmedia_extensions\s*\(\s*MediaKind\s+\w+\s*\)[^;{]*\{(.*?)\n\}", source, re.S)
    lists = dict(re.findall(r"\bcase\s+MediaKind::(\w+)\s*:\s*return\s+(\w+)\s*;",
                            body.group(1) if body else ""))
    found, problems = set(), []
    for kind in kinds:
        name = lists.get(kind)
        if name is None:
            problems.append(f"{MEDIA_KINDS}: media_extensions has no "
                            f"`case MediaKind::{kind}: return <list>;` (tools/file_filters.py reads it)")
            continue
        # The list's definition: the statement naming it that has its items.
        exts = []
        for m in re.finditer(rf"\b{name}\b[^;]*;", source):
            exts = re.findall(r'"\.(\w+)"', m.group(0))
            if exts:
                break
        if not exts:
            problems.append(f"{MEDIA_KINDS}: no \".ext\" items read for {name}, "
                            f"MediaKind::{kind}'s list (tools/file_filters.py reads it)")
        found.update(ext.lower() for ext in exts)
    return found, problems


def main() -> int:
    media, problems = media_extensions()
    if problems:
        return report("file filters", problems)
    used = set()
    for rel, text in source_files(["src/ui"], {".qml", ".js"}):
        lines = text.split("\n")
        found: dict[int, dict[str, list[str]]] = {}  # line: why, its patterns
        for m in PATTERN.finditer(blank(text, strings=False)):
            n = line_of(text, m.start())
            ext = m.group(1).lower()
            allowed = [(f, exts, ext) for f, exts in ALLOWED if f == rel and ext in exts.split()]
            if ext in media:
                why = "a media kind's, so session.media.mediaFilePatterns(kind)"
            elif allowed:
                used.update(allowed)
                continue
            else:
                why = "written out"
            found.setdefault(n, {}).setdefault(why, []).append("*." + m.group(1))
        for n, whys in sorted(found.items()):
            said = "; ".join(f"{' '.join(patterns)}: {why}" for why, patterns in whys.items())
            problems.append(f"{rel}:{n}: {lines[n - 1].strip()} ({said})")
    problems += unused("file_filters.py", [f"{f}: *.{ext}" for f, exts in ALLOWED
                                           for ext in exts.split() if (f, exts, ext) not in used])
    return report("file filters", problems)


if __name__ == "__main__":
    sys.exit(main())
