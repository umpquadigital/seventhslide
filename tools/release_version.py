#!/usr/bin/env python3
"""Check a release tag against the version in CMakeLists.txt, and say what to build.

A release is tagged ``v`` and the project version (``v1.3.0``); a pre-release
adds a label after a dash (``v1.3.0-beta.2``), which becomes
SEVENTHSLIDE_VERSION_LABEL. A tag whose numbers are not the project's is
refused, so the version is changed in CMakeLists.txt first, not only in the
tag. Without a tag (a release workflow started by hand on a branch), the
project version is built as it is.

Prints ``name=value`` lines, as $GITHUB_OUTPUT takes them:

    version=1.3.0-beta.2   what the app calls itself
    numbers=1.3.0          the project version alone (a Windows file's version)
    label=beta.2           SEVENTHSLIDE_VERSION_LABEL (empty for a release)
    prerelease=true        whether GitHub marks the release a pre-release

    python3 tools/release_version.py --tag v1.3.0-beta.2   # exit 1 if it does not match
    python3 tools/release_version.py                       # no tag: the project version
"""
from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

# The project() call's VERSION, the version's only home.
PROJECT_VERSION = re.compile(r"^project\(SeventhSlide\s.*?^\s*VERSION\s+(\d+\.\d+\.\d+)\s*$",
                             re.MULTILINE | re.DOTALL)
# v1.3.0 or v1.3.0-beta.2: the label letters, digits and dots, as CMakeLists.txt
# takes it.
TAG = re.compile(r"^v(\d+\.\d+\.\d+)(?:-([0-9A-Za-z]+(?:\.[0-9A-Za-z]+)*))?$")


def project_version(cmake_text: str) -> str:
    match = PROJECT_VERSION.search(cmake_text)
    if not match:
        raise ValueError("CMakeLists.txt has no project(SeventhSlide VERSION x.y.z)")
    return match.group(1)


def release(tag: str | None, version: str) -> dict[str, str]:
    """What to build for ``tag`` (None: no tag) when the project is ``version``."""
    if tag is None:
        return {"version": version, "numbers": version, "label": "", "prerelease": "false"}
    match = TAG.match(tag)
    if not match:
        raise ValueError(f"tag {tag!r} is not v1.2.3 or v1.2.3-label (letters, digits, dots)")
    numbers, label = match.group(1), match.group(2) or ""
    if numbers != version:
        raise ValueError(f"tag {tag} is version {numbers}, but CMakeLists.txt says {version}: "
                         "change the project VERSION there first")
    return {
        "version": f"{numbers}-{label}" if label else numbers,
        "numbers": numbers,
        "label": label,
        "prerelease": "true" if label else "false",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--tag", help="the release tag (v1.3.0, v1.3.0-beta.2)")
    args = parser.parse_args()
    try:
        version = project_version((ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))
        out = release(args.tag, version)
    except ValueError as error:
        print(f"release_version: {error}", file=sys.stderr)
        return 1
    for name, value in out.items():
        print(f"{name}={value}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
