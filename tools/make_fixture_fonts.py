#!/usr/bin/env python3
"""Make the test suite's fonts (fixtures/fonts) from Liberation Sans.

The font tests need faces whose shape they know: a family with a real Bold, a
family named for its weight (the way "Open Sans Semibold" ships), a family
with nothing but a Regular, and a family that looks like nothing else: the
others are Liberation Sans, the app's own stand-in for a font it has not got,
so a test that has to see which it painted uses "SSFixture Wide", stretched
to 1.4 times its width. System fonts differ from machine to machine, and
a family the machine also has installed lends Qt its other faces, so these
are Liberation Sans 2.x cut down to printable ASCII and renamed "SSFixture",
a name no machine has. Liberation is under the SIL Open Font License 1.1,
whose Reserved Font Names ("Liberation", "Arimo") a modified version must not
carry; the renamed subsets do not.

Usage: tools/make_fixture_fonts.py [LIBERATION_DIR]
LIBERATION_DIR holds LiberationSans-Regular.ttf and LiberationSans-Bold.ttf
(default /usr/share/fonts/truetype/liberation). Needs fontTools.
"""

import sys
from pathlib import Path

from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.ttLib.ttCollection import TTCollection

OUT = Path(__file__).resolve().parent.parent / "fixtures" / "fonts"

# (output file, source face, family, style, typographic family/style or None,
#  OS/2 weight)
FACES = [
    ("SSFixtureSans-Regular.ttf", "Regular", "SSFixture Sans", "Regular", None, 400),
    ("SSFixtureSans-Bold.ttf", "Bold", "SSFixture Sans", "Bold", None, 700),
    # Legacy naming: the weight is in the family, the style says Regular, and
    # the typographic names say what it is.
    ("SSFixtureSansSemibold-Regular.ttf", "Bold", "SSFixture Sans Semibold", "Regular",
     ("SSFixture Sans", "Semibold"), 600),
    ("SSFixturePlain-Regular.ttf", "Regular", "SSFixture Plain", "Regular", None, 400),
]

# (output file, source face, style, OS/2 weight): SSFixture Wide.
WIDE_FACES = [
    ("SSFixtureWide-Regular.ttf", "Regular", "Regular", 400),
    ("SSFixtureWide-Bold.ttf", "Bold", "Bold", 700),
]
WIDE_FACTOR = 1.4


def rename(font, family, style, typographic, weight):
    name = font["name"]
    full = family if style == "Regular" else f"{family} {style}"
    postscript = full.replace(" ", "")
    if style != "Regular":
        postscript = f"{family.replace(' ', '')}-{style}"
    # Drop every name record, then write only what identifies this face.
    name.names = []
    records = {
        0: "Derived from Liberation Sans (c) 2012 Red Hat, Inc.; "
           "digitized data (c) 2010 Google Corporation. SIL Open Font License 1.1.",
        1: family,
        2: style,
        3: f"SeventhSlide test fixture: {full}",
        4: full,
        5: "Version 1.0",
        6: postscript,
        13: "SIL Open Font License 1.1",
        14: "https://openfontlicense.org",
    }
    if typographic is not None:
        records[16], records[17] = typographic
    for name_id, text in records.items():
        name.setName(text, name_id, 3, 1, 0x409)  # Windows, Unicode BMP, English
        name.setName(text, name_id, 1, 0, 0)  # Macintosh, Roman, English
    os2 = font["OS/2"]
    os2.usWeightClass = weight
    bold = style == "Bold"
    os2.fsSelection = (os2.fsSelection & ~0b1100001) | (0b100000 if bold else 0b1000000)
    font["head"].macStyle = 1 if bold else 0


def subset_options():
    options = subset.Options()
    options.name_IDs = ["*"]
    options.name_languages = ["*"]
    options.layout_features = ["*"]
    options.notdef_outline = True
    return options


def widen(font, factor):
    """Stretch every glyph and its advance by ``factor`` across."""
    glyf = font["glyf"]
    hmtx = font["hmtx"]
    for name in font.getGlyphOrder():
        glyph = glyf[name]
        if glyph.isComposite():
            for component in glyph.components:
                component.x = round(component.x * factor)
        elif glyph.numberOfContours > 0:
            coordinates = glyph.coordinates
            for i, (x, y) in enumerate(coordinates):
                coordinates[i] = (round(x * factor), y)
        glyph.recalcBounds(glyf)
        advance, _ = hmtx[name]
        lsb = glyph.xMin if glyph.numberOfContours != 0 else 0
        hmtx[name] = (round(advance * factor), lsb)
    font["OS/2"].recalcAvgCharWidth(font)


def make_wide(src):
    """SSFixture Wide: without hinting, whose instructions were for the
    unstretched outlines, nor kerning, whose distances were too."""
    options = subset_options()
    options.hinting = False
    options.layout_features = []
    options.drop_tables += ["kern", "GPOS"]
    for out_name, face, style, weight in WIDE_FACES:
        font = TTFont(src / f"LiberationSans-{face}.ttf")
        subsetter = subset.Subsetter(options)
        subsetter.populate(unicodes=range(0x20, 0x7F))
        subsetter.subset(font)
        widen(font, WIDE_FACTOR)
        rename(font, "SSFixture Wide", style, None, weight)
        font.save(OUT / out_name)
        print(f"{out_name}: {(OUT / out_name).stat().st_size} bytes")


def make_collection(src):
    """A collection as a Mac ships one: one family, its weights named as styles."""
    collection = TTCollection()
    for face, style, weight in (("Regular", "Regular", 400), ("Regular", "Light", 300),
                                ("Bold", "Bold", 700)):
        font = TTFont(src / f"LiberationSans-{face}.ttf")
        subsetter = subset.Subsetter(subset_options())
        subsetter.populate(unicodes=range(0x20, 0x7F))
        subsetter.subset(font)
        rename(font, "SSFixture Collection", style, None, weight)
        collection.fonts.append(font)
    out = OUT / "SSFixtureCollection.ttc"
    collection.save(out)
    print(f"{out.name}: {out.stat().st_size} bytes")


def main():
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("/usr/share/fonts/truetype/liberation")
    OUT.mkdir(parents=True, exist_ok=True)
    options = subset_options()
    for out_name, face, family, style, typographic, weight in FACES:
        font = TTFont(src / f"LiberationSans-{face}.ttf")
        subsetter = subset.Subsetter(options)
        subsetter.populate(unicodes=range(0x20, 0x7F))
        subsetter.subset(font)
        rename(font, family, style, typographic, weight)
        font.save(OUT / out_name)
        print(f"{out_name}: {(OUT / out_name).stat().st_size} bytes")
    make_wide(src)
    make_collection(src)


if __name__ == "__main__":
    main()
