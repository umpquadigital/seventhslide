#!/usr/bin/env python3
"""Compare two sets of operator UI screenshots pixel for pixel.

The shots come from the hidden test "UI shots" (tests/ui_shots.cpp):

    SEVENTHSLIDE_SHOTS_DIR=before ./seventhslide_tests_dev "[.shots]"
    ... change the QML, rebuild ...
    SEVENTHSLIDE_SHOTS_DIR=after ./seventhslide_tests_dev "[.shots]"
    python3 tools/ui_shots_compare.py before after --diffs diffs

A shot that differs is listed with its changed pixel count and bounding box;
with --diffs, an image per differing shot shows the "after" picture dimmed
with every changed pixel in red. Exit status 1 when anything differs.

--mask DIR names a second "before" run. Pixels that differ between the two
"before" runs change from run to run on their own (a caret, a clock) and are
left out of the comparison; they are reported once as noise.
"""
import argparse
import pathlib
import sys

from PIL import Image, ImageChops


def changed(a: Image.Image, b: Image.Image) -> Image.Image:
    """A 1-bit image, set where ``a`` and ``b`` differ."""
    diff = ImageChops.difference(a.convert("RGBA"), b.convert("RGBA"))
    # Any channel that differs at all counts.
    return diff.convert("L").point(lambda v: 255 if v else 0, mode="1")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("before", type=pathlib.Path)
    parser.add_argument("after", type=pathlib.Path)
    parser.add_argument("--mask", type=pathlib.Path,
                        help="a second 'before' run; what differs between the two is noise")
    parser.add_argument("--diffs", type=pathlib.Path, help="write diff images here")
    args = parser.parse_args()

    before = {p.name: p for p in args.before.glob("*.png")}
    after = {p.name: p for p in args.after.glob("*.png")}
    problems = 0
    for name in sorted(before.keys() - after.keys()):
        print(f"missing in after: {name}")
        problems += 1
    for name in sorted(after.keys() - before.keys()):
        print(f"new in after: {name}")
    if args.diffs:
        args.diffs.mkdir(parents=True, exist_ok=True)

    same = 0
    for name in sorted(before.keys() & after.keys()):
        a = Image.open(before[name])
        b = Image.open(after[name])
        if a.size != b.size:
            print(f"{name}: size {a.size} -> {b.size}")
            problems += 1
            continue
        mask = None
        if args.mask and (args.mask / name).exists():
            noise = changed(a, Image.open(args.mask / name))
            if noise.getbbox():
                print(f"{name}: noise between the before runs in {noise.getbbox()}, ignored")
                mask = noise
        diff = changed(a, b)
        if mask is not None:
            diff = ImageChops.logical_and(diff, ImageChops.invert(mask))
        box = diff.getbbox()
        if not box:
            same += 1
            continue
        count = sum(1 for v in diff.getdata() if v)
        print(f"{name}: {count} pixels differ in {box}")
        problems += 1
        if args.diffs:
            out = Image.blend(b.convert("RGB"), Image.new("RGB", b.size, (0, 0, 0)), 0.6)
            out.paste((255, 0, 0), mask=diff)
            out.save(args.diffs / name)
    print(f"{same} identical, {problems} differ or missing")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
