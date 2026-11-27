#!/usr/bin/env python3
"""Fetch the fonts SeventhSlide ships (third_party/fonts) from Google Fonts.

Each family goes in a folder named exactly for it, as FontStore finds it: static
Regular, Bold, Italic and Bold Italic files, as many of those as the family has,
beside its licence. A weight of its own ("Open Sans Light") is a family of its
own, as the files name it: its Regular and Italic. Files are as Google serves
them, never changed. The whole set is listed in SOURCES.txt with each family's
version.

A family in KEPT holds files that are not Google's, put there by hand: it is
never fetched, even by --refresh, and SOURCES.txt says what it holds.

A family's folder that is already there is left alone unless --refresh is given:
a font's bytes are what a library's copy is known by, so a font is only replaced
on purpose.

Usage: tools/fetch_bundled_fonts.py [--refresh] [FAMILY ...]   (default: every one)
Needs fontTools.
"""

import argparse
import re
import shutil
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

from fontTools.ttLib import TTFont

OUT = Path(__file__).resolve().parent.parent / "third_party" / "fonts"

# A basic set, as an office suite has: what people expect to find, a font or two
# for each kind of design, and the same widths as Calibri, Cambria and Georgia.
# Not every good font: each one more is one more to look past in the list. All
# under the SIL Open Font License. Liberation Sans, Serif and Mono are not
# fetched (docs/BUILDING.md).
FAMILIES = [
    # Sans
    "Carlito", "Montserrat", "Nunito", "Open Sans", "Poppins", "Raleway", "Roboto",
    # Serif, and a slab
    "Bitter", "Caladea", "EB Garamond", "Gelasio", "Libre Baskerville", "Lora",
    "Merriweather", "Playfair Display",
    # Display
    "Abril Fatface", "Anton", "Archivo Black", "Bebas Neue", "Oswald",
    # Script and casual
    "Caveat", "Comic Neue", "Dancing Script", "Great Vibes", "Lobster", "Pacifico",
]

# Weights of their own: the family as its files name it, then the family on Google
# Fonts and the weight.
WEIGHTS = {
    "Montserrat Light": ("Montserrat", 300),
    "Montserrat Medium": ("Montserrat", 500),
    "Open Sans Light": ("Open Sans", 300),
    "Open Sans SemiBold": ("Open Sans", 600),
}

# Not fetched: what each holds, for SOURCES.txt. Its files are as their makers
# released them, never changed; the folder names the family (FontStore).
KEPT = {
    # The older design the libraries already use, as SeventhSlide's EB Garamond.
    # It has no bold, so Bold and Bold Italic are Google's (Version 1.003).
    "EB Garamond": "Regular and Italic: EB Garamond 08 Version 0.016 (Georg Duffner, 2012);"
                   " Bold and Bold Italic: Version 1.003 from Google Fonts",
}

CSS = "https://fonts.googleapis.com/css2?family={family}{axes}"
# Everything a family may have, then less of it: the service answers 400 when
# asked for a style the family does not have.
AXES = [":ital,wght@0,400;0,700;1,400;1,700", ":wght@400;700", ":ital@0;1", ""]
LICENCE = "https://raw.githubusercontent.com/google/fonts/main/ofl/{slug}/OFL.txt"
# An older browser's name gets one whole TrueType file per style.
AGENT = "Mozilla/4.0"
FACE = re.compile(
    r"font-style:\s*(\w+);\s*font-weight:\s*(\d+);.*?src:\s*url\((https://[^)]+\.ttf)\)", re.S
)
STYLE_NAMES = {("normal", "400"): "Regular", ("normal", "700"): "Bold",
               ("italic", "400"): "Italic", ("italic", "700"): "Bold Italic"}


def get(url):
    request = urllib.request.Request(url, headers={"User-Agent": AGENT})
    with urllib.request.urlopen(request, timeout=60) as reply:
        return reply.read()


def faces_of(family):
    """(style name, url) for each of the family's four styles it has."""
    weight = None
    if family in WEIGHTS:
        family, weight = WEIGHTS[family]
    quoted = urllib.parse.quote_plus(family)
    for axes in AXES if weight is None else [f":ital,wght@0,{weight};1,{weight}", f":wght@{weight}"]:
        try:
            css = get(CSS.format(family=quoted, axes=axes)).decode()
        except urllib.error.HTTPError as ex:
            if ex.code == 400:
                continue
            raise
        faces = {}
        for style, face_weight, url in FACE.findall(css):
            if weight is not None and face_weight == str(weight):
                faces["Regular" if style == "normal" else "Italic"] = url
            elif weight is None and (style, face_weight) in STYLE_NAMES:
                faces[STYLE_NAMES[(style, face_weight)]] = url
        if "Regular" in faces:
            return faces
    raise SystemExit(f"{family}: Google Fonts has no regular face of it")


def check(path, family, style):
    """Refuse a file that is not the face asked for; its version otherwise."""
    font = TTFont(path)
    name = font["name"]
    found = (name.getDebugName(1), name.getDebugName(2))
    if found != (family, style):
        raise SystemExit(f"{family} {style}: the file says it is {found}")
    if "fvar" in font:
        raise SystemExit(f"{family} {style}: a variable font, not a static one")
    return (name.getDebugName(5) or "").split(";")[0].strip()


def fetch(family, scratch):
    """Fetch ``family`` into ``scratch`` and on into OUT; its version."""
    folder = scratch / family
    folder.mkdir()
    version = ""
    for style, url in faces_of(family).items():
        path = folder / f"{family.replace(' ', '')}-{style.replace(' ', '')}.ttf"
        path.write_bytes(get(url))
        version = check(path, family, style)
    slug = WEIGHTS.get(family, (family,))[0].lower().replace(" ", "")
    licence = get(LICENCE.format(slug=slug))
    if b"SIL OPEN FONT LICENSE" not in licence.upper():
        raise SystemExit(f"{family}: its licence is not the Open Font License")
    (folder / "OFL.txt").write_bytes(licence)
    target = OUT / family
    if target.exists():
        shutil.rmtree(target)
    shutil.copytree(folder, target)
    return version


def version_of(family):
    """The version of a family already in OUT."""
    file = sorted((OUT / family).glob("*.ttf"))[0]
    return (TTFont(file)["name"].getDebugName(5) or "").split(";")[0].strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--refresh", action="store_true", help="replace what is already there")
    parser.add_argument("families", nargs="*", help="which ones (default: every one)")
    args = parser.parse_args()
    every = FAMILIES + list(WEIGHTS)
    unknown = set(args.families) - set(every)
    if unknown:
        raise SystemExit(f"Not in the list: {sorted(unknown)}")
    kept = set(args.families) & set(KEPT)
    if kept:
        raise SystemExit(f"Not fetched, kept as they are (KEPT): {sorted(kept)}")

    # Downloads wait in a folder of their own until they have been checked.
    with tempfile.TemporaryDirectory(prefix="ss-fonts-") as scratch:
        for family in args.families or every:
            if family in KEPT or ((OUT / family).exists() and not args.refresh):
                continue
            print(f"{family}: {fetch(family, Path(scratch))}", flush=True)
            time.sleep(0.2)

    lines = ["Fetched from Google Fonts by tools/fetch_bundled_fonts.py, as served,",
             "except where a line says otherwise.",
             "The licence of each family is the OFL.txt in its folder.", ""]
    lines += [f"{family}\t{KEPT.get(family) or version_of(family)}" for family in sorted(every)
              if (OUT / family).exists()]
    (OUT / "SOURCES.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    sys.exit(main())
