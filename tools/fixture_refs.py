#!/usr/bin/env python3
"""Check that every file under fixtures/ is one a test or tool reads.

A fixture nothing reads is dead weight that looks like evidence: a golden
nobody compares, kept up to date by hand or not at all. A file counts as read
when tests/, src/, tools/ or the CMake files name it in one of these ways:

- its path under fixtures/ (``songs/import/plain.txt``);
- its file name, in a file that also names its directory (``plain.txt``
  with ``import``);
- its stem, its directory and the rest of its name, each as a string, for
  the tests that build ``stem + ".xml"`` and ``stem + ".expected.json"``
  (``require_matches_golden("openlyrics_basic")`` with ``"songs"`` and
  ``".expected.json"``);
- an id in a manifest a test iterates, by the names in MANIFESTS.

A directory's README.md and the fixture fonts' licence are read by people,
not tests, so they are not checked.

    python3 tools/fixture_refs.py    # exit 1 and list each one nothing reads
"""
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
FIXTURES = ROOT / "fixtures"
SEARCHED = ("tests", "src", "tools")
DOCS = {"README.md", "OFL.txt"}

# Fixtures a test finds through a manifest rather than by name. For each
# manifest, the files each id in its "fixtures" list names, and a file that
# builds that name from the id; that file must still read the manifest and
# build the name, so a test that stops iterating stops vouching for them.
MANIFESTS = {
    "oracle/manifest.json": [
        # Also test_scene_dump.cpp, test_slide_builder.cpp and the render-smoke tool.
        ("oracle/{}.json", "tests/test_session.cpp", '(id + ".json")'),
        # The goldens: what the render-smoke tool writes and the test compares.
        ("render_smoke/{}_line0.png", "tests/test_render_smoke.cpp", '(id + "_line0.png")'),
    ],
}


def sources() -> dict[str, str]:
    """Each searched file's text, by its path from the root."""
    paths = [p for d in SEARCHED for p in (ROOT / d).rglob("*") if p.is_file()]
    paths += [ROOT / "CMakeLists.txt", *(ROOT / "cmake").glob("*.cmake")]
    me = pathlib.Path(__file__).resolve()
    out = {}
    for p in paths:
        if p.resolve() == me:
            continue
        try:
            out[p.relative_to(ROOT).as_posix()] = p.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue  # a binary: an image, a font
    return out


def listed(texts: dict[str, str], problems: list[str]) -> set[str]:
    """The fixtures MANIFESTS vouches for, checking each entry still holds."""
    out = set()
    for manifest, entries in MANIFESTS.items():
        ids = json.loads((FIXTURES / manifest).read_text(encoding="utf-8"))["fixtures"]
        for pattern, reader, builds in entries:
            text = texts.get(reader, "")
            if pathlib.PurePath(manifest).name not in text or builds not in text:
                problems.append(f"tools/fixture_refs.py: {reader} no longer builds "
                                f"{pattern} from {manifest}")
                continue
            out.update(pattern.format(i) for i in ids)
    return out


def named(rel: str, texts: dict[str, str]) -> bool:
    path = pathlib.PurePosixPath(rel)
    name = re.compile(r"(?<![\w.-])" + re.escape(path.name) + r"(?![\w.])")
    folder = re.compile(r"\b" + re.escape(path.parent.name) + r"\b")
    stem, dot, rest = path.name.partition(".")
    built = ('"' + stem + '"', '"' + path.parent.name + '"', '"' + dot + rest + '"')
    for text in texts.values():
        if rel in text:
            return True
        # The file name, where the folder is named too: not schema/library.txt
        # vouching for a bibles/library.txt.
        if name.search(text) and folder.search(text):
            return True
        # Stem, folder and the rest of the name, each a string: not the stem of
        # a live zefania_sample.xml vouching for a zefania_sample.old.xml.
        if dot and all(part in text for part in built):
            return True
    return False


def main() -> int:
    texts = sources()
    problems = []
    vouched = listed(texts, problems)
    for path in sorted(p for p in FIXTURES.rglob("*") if p.is_file()):
        rel = path.relative_to(FIXTURES).as_posix()
        if path.name in DOCS or rel in vouched or named(rel, texts):
            continue
        problems.append(f"fixtures/{rel}: nothing reads it")
    for problem in problems:
        print(problem)
    print(f"fixture refs: {len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
