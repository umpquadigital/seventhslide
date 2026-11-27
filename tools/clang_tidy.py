#!/usr/bin/env python3
"""Run clang-tidy, with the checks .clang-tidy names, over the project's
sources, and fail on anything it finds.

The sources are each .cpp in src/ and tests/ that the build's
compile_commands.json compiles: not third_party/, nor what the build makes
(moc, qrc, QML type registrations), nor a platform's files this build leaves
out. The headers checked are the project's own: under this checkout's src/
and tests/, by their full paths, and not in the build folder, by its, so
neither the folders the checkout sits in nor the build folder's name or
place changes which. The build must have made what the sources include
first: a build, or, without building, its moc files and the Wayland
protocol's header, as docs/BUILDING.md says.

The compile commands are GCC's. Clang warns where GCC does not, and the
project's targets build with -Werror (SEVENTHSLIDE_WARNINGS_AS_ERRORS), so
each file is given -Wno-error: a compiler warning is the build's to report,
not this check's, and .clang-tidy's checks are errors of their own.

    python3 tools/clang_tidy.py [-p build] [-j N] [file ...]

clang-tidy is CLANG_TIDY, or clang-tidy-20 or clang-tidy on PATH; CI's is
20. Files named are checked alone, otherwise every source is. The build
folder and the files are paths from the current directory. A clang-tidy
can take most of a gigabyte, so four files are checked at once unless -j
says otherwise: run every source with more where memory allows.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys

from _check import ROOT, report

DIRS = ("src", "tests")
EXTRA_ARGS = ["--extra-arg=-Wno-error"]
# path:line:col: warning: what [check], as clang-tidy prints a finding.
FINDING = re.compile(r"^(?P<path>[^\s:][^:]*):(?P<line>\d+):(?P<col>\d+): "
                     r"(?:warning|error): (?P<what>.*) \[(?P<check>[\w.,-]+)\]$")


def clang_tidy() -> str | None:
    if os.environ.get("CLANG_TIDY"):
        return os.environ["CLANG_TIDY"]
    return shutil.which("clang-tidy-20") or shutil.which("clang-tidy")


def sources(build: pathlib.Path) -> list[pathlib.Path]:
    """Each project .cpp the build compiles, once, in order."""
    with open(build / "compile_commands.json", encoding="utf-8") as f:
        entries = json.load(f)
    found = set()
    for e in entries:
        path = pathlib.Path(e["directory"], e["file"]).resolve()
        try:
            rel = path.relative_to(ROOT)
        except ValueError:
            continue
        if path.suffix == ".cpp" and rel.parts[0] in DIRS and not path.is_relative_to(build):
            found.add(path)
    return sorted(found)


def regex_path(path: pathlib.Path) -> str:
    """``path`` as a regex that matches it alone (clang-tidy's are POSIX)."""
    return re.sub(r"([\\.^$|()\[\]{}*+?])", r"\\\1", path.as_posix())


def header_filters(build: pathlib.Path) -> list[str]:
    """clang-tidy's arguments for the headers it checks, as the docstring says."""
    return [f"--header-filter=^{regex_path(ROOT)}/(src|tests)/",
            f"--exclude-header-filter=^{regex_path(build)}/"]


def shown(path: pathlib.Path) -> pathlib.Path:
    """``path`` from ROOT, when it is in the tree."""
    return path.relative_to(ROOT) if path.is_relative_to(ROOT) else path


def tidy(exe: str, build: pathlib.Path, path: pathlib.Path) -> tuple[pathlib.Path, int, str]:
    run = subprocess.run([exe, "--quiet", "-p", str(build), *header_filters(build), *EXTRA_ARGS,
                          str(path)],
                         cwd=ROOT, capture_output=True, text=True)
    return path, run.returncode, run.stdout + run.stderr


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("-p", dest="build", default="build",
                        help="the build folder with compile_commands.json, from the current "
                             "directory (build)")
    parser.add_argument("-j", dest="jobs", type=int, default=min(4, os.cpu_count() or 1),
                        help="files checked at once (4: a whole run belongs on the build "
                             "server, with more)")
    parser.add_argument("files", nargs="*", help="only these files, from the current directory")
    args = parser.parse_args()

    exe = clang_tidy()
    if exe is None:
        print("clang-tidy not found: install clang-tidy-20, or set CLANG_TIDY", file=sys.stderr)
        return 2
    build = pathlib.Path(args.build).resolve()
    files = [pathlib.Path(f).resolve() for f in args.files] or sources(build)

    problems, seen = [], set()
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for path, code, out in pool.map(lambda p: tidy(exe, build, p), files):
            found = False
            for line in out.splitlines():
                m = FINDING.match(line)
                if not m:
                    continue
                found = True
                where = shown(pathlib.Path(m["path"]).resolve())
                check = m["check"].split(",")[0]  # without its ",-warnings-as-errors"
                problem = f"{where}:{m['line']}:{m['col']}: {m['what']} [{check}]"
                if problem not in seen:  # a header's, once for every file with it
                    seen.add(problem)
                    problems.append(problem)
            if code != 0 and not found:
                problems.append(f"{shown(path)}: clang-tidy failed:\n{out.strip()}")
    print(f"{len(files)} file(s) checked")
    return report("clang-tidy", problems)


if __name__ == "__main__":
    sys.exit(main())
