#!/usr/bin/env python3
"""Check that src/app's controllers hold no other controller than they may.

The area controllers share state (OpenLibrary, PreviewResources, the session)
and announce changes by signals; SessionFacade::wire_controllers decides what
follows. A controller that holds another controller is a dependency docs/CODING.md
has to name. This lists every ``SomethingController&`` or ``*`` a header in
src/app holds or takes, other than its own class's, and fails on any not in
ALLOWED. It knows a controller by that name alone: a class that is one, named
otherwise, is not seen.

    python3 tools/controller_deps.py    # exit 1 and list each one not allowed

SessionFacade, the composition root, and main.cpp make them all, so they are
not checked.
"""
import pathlib
import re
import sys

# Header -> the controllers it may hold. Keep in step with docs/CODING.md.
ALLOWED = {
    # The use cases that reach across areas by what they are.
    "live_controller.hpp": {"LibraryController", "FreeformLibraryController",
                            "MediaController"},
    "export_controller.hpp": {"LibraryController", "FreeformLibraryController",
                              "MediaController"},
    # An import adds to every library a file can hold: Freeform, media,
    # fonts and services.
    "freeform_import_controller.hpp": {"FreeformLibraryController", "MediaController",
                                       "FontController", "LibraryController"},
    "convert_controller.hpp": {"ToolsController", "MediaController"},
    "download_controller.hpp": {"ToolsController", "MediaController"},
    "output_preview_controller.hpp": {"PlaybackController"},
    "background_theme_copier.hpp": {"MediaController"},
    "slide_edit_session.hpp": {"ThemeController"},
    # A service's first slide, from the rows it reads out of the library.
    "service_thumbs.hpp": {"LibraryController"},
    # Parts of one area: they are their controller's.
    "media_library.hpp": {"MediaController"},
    "sync_admin.hpp": {"SyncController"},
    "ytdlp_updater.hpp": {"ToolsController"},
    "canvas_rich_edit.hpp": {"FontController"},
    # Items a view binds to the controller it shows.
    "audio_meter_item.hpp": {"MixerController"},
}

SKIPPED = {"session_facade.hpp"}

ROOT = pathlib.Path(__file__).resolve().parent.parent / "src" / "app"
HELD = re.compile(r"\b([A-Z]\w*Controller)\s*[&*]")
OWN = re.compile(r"^\s*class\s+(\w+)\b[^;]*$", re.MULTILINE)
COMMENT = re.compile(r"//.*?$", re.MULTILINE)


def main() -> int:
    problems = []
    for header in sorted(ROOT.glob("*.hpp")):
        if header.name in SKIPPED:
            continue
        text = COMMENT.sub("", header.read_text(encoding="utf-8"))
        own = set(OWN.findall(text))
        held = {name for name in HELD.findall(text) if name not in own}
        for name in sorted(held - ALLOWED.get(header.name, set())):
            problems.append(f"src/app/{header.name}: holds {name}")
    for problem in problems:
        print(problem)
    print(f"controller dependencies: {len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
