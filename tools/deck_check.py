#!/usr/bin/env python3
"""Deck check: compare SeventhSlide's rendering of the target decks with LibreOffice's.

A verification tool, NOT a PowerPoint importer: SeventhSlide has no such
feature and this is not one. Every deck in DECKS is converted, slide by slide,
to a Freeform document in SeventhSlide's stored JSON, the hidden test
"[.deck-check]" (tests/deck_check.cpp) renders each document with the real
painters, and this script compares the renders with LibreOffice's reference
PNGs.

Nothing derived from a deck goes into the repository: the generated documents,
the media and the renders live under DIR (/tmp/ss_deck by default).

    # 1. convert every deck, extract its media, gather the fonts it names
    python3 tools/deck_check.py convert [--only NAME]

    # 2. where the tests are built (see the header of tests/deck_check.cpp):
    #    SEVENTHSLIDE_DECK_DIR=<dir>/decks ./bin/seventhslide_tests_dev "[.deck-check]"
    #    which writes <dir>/decks/<deck>/out/s-NNN.png

    # 3. score against the references, write contact sheets to <dir>/sheets
    python3 tools/deck_check.py score [--only NAME] [--worst N]

    # 4. slides that use each feature, reference beside ours (<dir>/sheets/features-N.png)
    python3 tools/deck_check.py features

Scores are the mean absolute difference (0-255, all three channels) after a
slight blur, on the reference's pixel grid. They say where to look: a poor
score is often the converter or LibreOffice rather than the app.
"""
import argparse
import collections
import colorsys
import json
import math
import os
import pathlib
import posixpath
import re
import shutil
import struct
import subprocess
import sys
import zipfile
import xml.etree.ElementTree as ET

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont, ImageStat

# The decks are the owner's and are not in the repository: a PowerPoint/
# folder of .pptx files and a reference/ folder of LibreOffice's renders, one
# folder per deck. SEVENTHSLIDE_EXAMPLE_DECKS names the folder; by default it
# is exampleslides/ in the checkout, which git ignores.
SOURCE = pathlib.Path(os.environ.get(
    "SEVENTHSLIDE_EXAMPLE_DECKS",
    pathlib.Path(__file__).resolve().parent.parent / "exampleslides"))
DECKS = sorted(p.stem for p in (SOURCE / "PowerPoint").glob("*.pptx"))
CANVAS_W = 1920
SCHEMA = "seventhslide.freeform"
EMU_PER_PT = 12700

NS = {
    "a": "http://schemas.openxmlformats.org/drawingml/2006/main",
    "p": "http://schemas.openxmlformats.org/presentationml/2006/main",
    "r": "http://schemas.openxmlformats.org/officeDocument/2006/relationships",
    "mc": "http://schemas.openxmlformats.org/markup-compatibility/2006",
    "p14": "http://schemas.microsoft.com/office/powerpoint/2010/main",
    "a14": "http://schemas.microsoft.com/office/drawing/2010/main",
    "asvg": "http://schemas.microsoft.com/office/drawing/2016/SVG/main",
    "dgm": "http://schemas.openxmlformats.org/drawingml/2006/diagram",
    "c": "http://schemas.openxmlformats.org/drawingml/2006/chart",
}
RID = "{%s}" % NS["r"]
IMAGE_EXT = {".png", ".jpg", ".jpeg", ".jfif", ".gif", ".bmp", ".tif", ".tiff", ".webp", ".svg"}


def slug(name: str) -> str:
    """The folder name of a deck: safe in paths and shells."""
    return re.sub(r"[^A-Za-z0-9]+", "-", name).strip("-")


def tag(el):
    return el.tag.rsplit("}", 1)[-1]


def pct(val, default=100000):
    return float(val if val is not None else default) / 100000.0


# ---------------------------------------------------------------- the package
class Pkg:
    """The zip of a deck: its XML parts, parsed once, and their relationships."""

    def __init__(self, path):
        self.zip = zipfile.ZipFile(path)
        self.names = set(self.zip.namelist())
        self._xml = {}
        self._rels = {}

    def xml(self, part):
        if part not in self._xml:
            self._xml[part] = ET.fromstring(self.zip.read(part))
        return self._xml[part]

    def rels(self, part):
        if part in self._rels:
            return self._rels[part]
        folder = posixpath.dirname(part)
        rel_part = posixpath.join(folder, "_rels", posixpath.basename(part) + ".rels")
        out = {}
        if rel_part in self.names:
            for r in ET.fromstring(self.zip.read(rel_part)):
                target = r.get("Target", "")
                if r.get("TargetMode") != "External":
                    target = target[1:] if target.startswith("/") else posixpath.normpath(
                        posixpath.join(folder, target))
                out[r.get("Id")] = (r.get("Type", "").rsplit("/", 1)[-1], target,
                                    r.get("TargetMode"))
        self._rels[part] = out
        return out

    def rel_of(self, part, kind):
        for _rid, (rtype, target, _mode) in self.rels(part).items():
            if rtype == kind:
                return target
        return None


# ---------------------------------------------------------------- colours
def rgb_to_hsl(r, g, b):
    h, l, s = colorsys.rgb_to_hls(r, g, b)
    return h, s, l


def hsl_to_rgb(h, s, l):
    return colorsys.hls_to_rgb(h, l, s)


def hex_rgb(s):
    s = (s or "000000").strip()[:6].ljust(6, "0")
    return tuple(int(s[i:i + 2], 16) / 255.0 for i in (0, 2, 4))


NAMED_SYS = {"windowText": "000000", "window": "ffffff", "menuText": "000000"}
PRESET_COLORS = {"black": "000000", "white": "ffffff", "red": "ff0000", "green": "008000",
                 "blue": "0000ff", "yellow": "ffff00", "gray": "808080", "grey": "808080",
                 "orange": "ffa500"}


def color_css(rgba):
    r, g, b, a = rgba
    rgb = "%02x%02x%02x" % tuple(max(0, min(255, round(v * 255))) for v in (r, g, b))
    if a >= 0.999:
        return "#" + rgb
    return "#%02x%s" % (max(0, min(255, round(a * 255))), rgb)


def resolve_color(el, ctx, ph=None):
    """(r, g, b, a) of a colour element (srgbClr, schemeClr...), its transforms applied."""
    kind = tag(el)
    rgb = (0.0, 0.0, 0.0)
    if kind == "srgbClr":
        rgb = hex_rgb(el.get("val"))
    elif kind == "sysClr":
        rgb = hex_rgb(el.get("lastClr") or NAMED_SYS.get(el.get("val"), "000000"))
    elif kind == "prstClr":
        rgb = hex_rgb(PRESET_COLORS.get(el.get("val", "").lower(), "000000"))
    elif kind == "scrgbClr":
        rgb = tuple(pct(el.get(c)) for c in "rgb")
    elif kind == "hslClr":
        rgb = hsl_to_rgb(float(el.get("hue", 0)) / 21600000.0, pct(el.get("sat")), pct(el.get("lum")))
    elif kind == "schemeClr":
        name = el.get("val")
        if name == "phClr":
            if ph is not None:
                rgb = ph[:3]
        else:
            rgb = ctx.scheme_color(name)
    r, g, b = rgb
    a = 1.0
    if kind == "schemeClr" and el.get("val") == "phClr" and ph is not None:
        a = ph[3]
    for op in el:
        name = tag(op)
        v = pct(op.get("val"), 0) if op.get("val") is not None else 0.0
        if name == "alpha":
            a = v
        elif name == "alphaMod":
            a *= v
        elif name == "alphaOff":
            a += v
        elif name in ("lumMod", "lumOff", "satMod", "satOff", "hueMod", "hueOff"):
            h, s, l = rgb_to_hsl(r, g, b)
            if name == "lumMod":
                l *= v
            elif name == "lumOff":
                l += v
            elif name == "satMod":
                s *= v
            elif name == "satOff":
                s += v
            elif name == "hueMod":
                h = (h * v) % 1.0
            elif name == "hueOff":
                h = (h + float(op.get("val", 0)) / 21600000.0) % 1.0
            r, g, b = hsl_to_rgb(h, min(1, max(0, s)), min(1, max(0, l)))
        elif name == "tint":
            # "10% of the input colour combined with 90% white".
            r, g, b = (c * v + (1 - v) for c in (r, g, b))
        elif name == "shade":
            r, g, b = (c * v for c in (r, g, b))
        elif name == "comp":
            r, g, b = (1 - r, 1 - g, 1 - b)
        elif name == "inv":
            r, g, b = (1 - r, 1 - g, 1 - b)
        elif name == "gray":
            r = g = b = 0.3 * r + 0.59 * g + 0.11 * b
        elif name in ("red", "green", "blue"):
            if name == "red":
                r = v
            elif name == "green":
                g = v
            else:
                b = v
    return (max(0, min(1, r)), max(0, min(1, g)), max(0, min(1, b)), max(0, min(1, a)))


def first_color(el, ctx, ph=None):
    if el is None:
        return None
    for child in el:
        if tag(child) in ("srgbClr", "schemeClr", "sysClr", "prstClr", "scrgbClr", "hslClr"):
            return resolve_color(child, ctx, ph)
    return None


# ---------------------------------------------------------------- theme
class Theme:
    def __init__(self, root):
        self.colors = {}
        for scheme in root.findall("a:themeElements/a:clrScheme", NS):
            for c in scheme:
                inner = c[0] if len(c) else None
                if inner is None:
                    continue
                if tag(inner) == "srgbClr":
                    self.colors[tag(c)] = hex_rgb(inner.get("val"))
                elif tag(inner) == "sysClr":
                    self.colors[tag(c)] = hex_rgb(inner.get("lastClr")
                                                  or NAMED_SYS.get(inner.get("val"), "000000"))
        fonts = root.find("a:themeElements/a:fontScheme", NS)
        self.major = self.minor = "Calibri"
        if fonts is not None:
            major = fonts.find("a:majorFont/a:latin", NS)
            minor = fonts.find("a:minorFont/a:latin", NS)
            self.major = major.get("typeface") if major is not None else self.major
            self.minor = minor.get("typeface") if minor is not None else self.minor
        fmt = root.find("a:themeElements/a:fmtScheme", NS)
        self.fills = list(fmt.find("a:fillStyleLst", NS)) if fmt is not None else []
        self.lines = list(fmt.find("a:lnStyleLst", NS)) if fmt is not None else []
        self.effects = list(fmt.find("a:effectStyleLst", NS)) if fmt is not None else []
        self.bg_fills = list(fmt.find("a:bgFillStyleLst", NS)) if fmt is not None else []


# ---------------------------------------------------------------- a slide's context
class SlideCtx:
    """A slide's parts and what they make of colours and sizes."""

    def __init__(self, conv, slide_part):
        self.conv = conv
        self.pkg = conv.pkg
        self.slide_part = slide_part
        self.layout_part = conv.pkg.rel_of(slide_part, "slideLayout")
        self.master_part = conv.pkg.rel_of(self.layout_part, "slideMaster")
        self.slide = conv.pkg.xml(slide_part)
        self.layout = conv.pkg.xml(self.layout_part)
        self.master = conv.pkg.xml(self.master_part)
        theme_part = conv.pkg.rel_of(self.master_part, "theme")
        self.theme = conv.theme_of(theme_part)
        clr = self.master.find("p:clrMap", NS)
        self.clr_map = dict(clr.attrib) if clr is not None else {}
        for over in (self.layout, self.slide):
            mapping = over.find("p:clrMapOvr/a:overrideClrMapping", NS)
            if mapping is not None:
                self.clr_map = dict(mapping.attrib)
        self.part = slide_part  # whose relationships the element being read uses

    def scheme_color(self, name):
        name = self.clr_map.get(name, name)
        return self.theme.colors.get(name, (0.0, 0.0, 0.0))


# ---------------------------------------------------------------- fonts on this machine
FONT_CACHE = {}
FONT_SUBSTITUTED = set()


def fc_query(family, bold, italic):
    """The font file fontconfig gives for a request: LibreOffice's view of the machine."""
    key = (family.lower(), bold, italic)
    if key not in FONT_CACHE:
        pat = re.sub(r"([:\-,])", r"\\\1", family)
        pat += ":weight=%d:slant=%d" % (200 if bold else 80, 100 if italic else 0)
        try:
            res = subprocess.run(["fc-match", "-f", "%{file}\n%{family}\n%{index}\n", pat],
                                 capture_output=True, text=True, timeout=20)
            lines = res.stdout.split("\n")
            FONT_CACHE[key] = (lines[0], lines[1], int(lines[2] or 0))
        except (OSError, subprocess.SubprocessError, ValueError, IndexError):
            FONT_CACHE[key] = ("", "", 0)
        got = FONT_CACHE[key][1].lower()
        if family.lower() not in [f.strip().lower() for f in got.split(",")]:
            FONT_SUBSTITUTED.add(family)
    return FONT_CACHE[key]


METRICS_CACHE = {}


def font_metrics(family, bold=False):
    """A font's vertical metrics as shares of its size: what PowerPoint sets a line by
    (the Windows metrics, which LibreOffice's line heights agree with) and what Qt reports
    (hhea, or the typographic metrics if the font asks for them)."""
    key = (family.lower(), bold)
    if key not in METRICS_CACHE:
        path, _names, index = fc_query(family, bold, False)
        out = {"win_ascent": 0.95, "win_descent": 0.25, "qt_ascent": 0.8, "qt_descent": 0.2}
        try:
            with open(path, "rb") as f:
                data = f.read()
            base = 0
            if data[:4] == b"ttcf":
                base = struct.unpack(">I", data[12 + 4 * index:16 + 4 * index])[0]
            tables = {}
            for i in range(struct.unpack(">H", data[base + 4:base + 6])[0]):
                rec = base + 12 + 16 * i
                tables[data[rec:rec + 4]] = struct.unpack(">I", data[rec + 8:rec + 12])[0]
            upem = struct.unpack(">H", data[tables[b"head"] + 18:tables[b"head"] + 20])[0]
            hhea = tables[b"hhea"]
            hh_a, hh_d, hh_g = struct.unpack(">hhh", data[hhea + 4:hhea + 10])
            win_a, win_d = hh_a, -hh_d
            typo_a, typo_d, use_typo = hh_a, hh_d, False
            os2 = tables.get(b"OS/2")
            if os2 is not None:
                fs_sel = struct.unpack(">H", data[os2 + 62:os2 + 64])[0]
                typo_a, typo_d = struct.unpack(">hh", data[os2 + 68:os2 + 72])
                win_a, win_d = struct.unpack(">HH", data[os2 + 74:os2 + 78])
                use_typo = bool(fs_sel & 0x80)
            qt_a, qt_d = (typo_a, -typo_d) if use_typo and typo_a else (hh_a, -hh_d)
            if win_a + win_d <= 0:
                win_a, win_d = hh_a + hh_g, -hh_d
            out = {"win_ascent": win_a / upem, "win_descent": win_d / upem,
                   "qt_ascent": qt_a / upem, "qt_descent": qt_d / upem}
        except (OSError, KeyError, struct.error):
            pass
        METRICS_CACHE[key] = out
    return METRICS_CACHE[key]


def natural_ratio(family, bold=False):
    """Single line spacing over the size: the Windows metrics, or for a family this
    machine lacks about 1.2, which is what LibreOffice's renders measure at whatever
    font stands in for it (taken from the Helvetica Neue and Bookman Old Style decks)."""
    m = font_metrics(family, bold)
    if family in FONT_SUBSTITUTED:
        return 1.2
    ratio = m["win_ascent"] + m["win_descent"]
    return ratio if 0.8 <= ratio <= 2.5 else 1.2


# ---------------------------------------------------------------- the geometry stack
def mat_identity():
    return (1.0, 0.0, 0.0, 1.0, 0.0, 0.0)


def mat_mul(m, n):
    """m after n: applying the result is n first, then m."""
    a, b, c, d, e, f = m
    g, h, i, j, k, l = n
    return (a * g + c * h, b * g + d * h, a * i + c * j, b * i + d * j,
            a * k + c * l + e, b * k + d * l + f)


def mat_apply(m, x, y):
    a, b, c, d, e, f = m
    return (a * x + c * y + e, b * x + d * y + f)


def group_matrix(x, y, w, h, ch_x, ch_y, ch_w, ch_h, rot, flip_h, flip_v):
    """From a group's child space to its parent's."""
    sx = w / ch_w if ch_w else 1.0
    sy = h / ch_h if ch_h else 1.0
    cx, cy = x + w / 2.0, y + h / 2.0
    r = math.radians(rot)
    cos, sin = math.cos(r), math.sin(r)
    fx = -1.0 if flip_h else 1.0
    fy = -1.0 if flip_v else 1.0
    # child -> frame, then flip and turn about the group's middle
    translate = (1.0, 0.0, 0.0, 1.0, -(ch_x + ch_w / 2.0), -(ch_y + ch_h / 2.0))
    scale = (sx * fx, 0.0, 0.0, sy * fy, 0.0, 0.0)
    turn = (cos, sin, -sin, cos, 0.0, 0.0)
    place = (1.0, 0.0, 0.0, 1.0, cx, cy)
    return mat_mul(place, mat_mul(turn, mat_mul(scale, translate)))


def place_frame(matrix, x, y, w, h, rot, flip_h, flip_v):
    """The frame of a child under ``matrix``: (x, y, w, h, rot, flip_h, flip_v)."""
    if matrix == mat_identity():
        return x, y, w, h, rot, flip_h, flip_v
    cx, cy = mat_apply(matrix, x + w / 2.0, y + h / 2.0)
    a, b, c, d, _e, _f = matrix
    r = math.radians(rot)
    # The child's axes under the matrix: its widths, its turn, its handedness.
    ex = (a * math.cos(r) + c * math.sin(r), b * math.cos(r) + d * math.sin(r))
    ey = (-a * math.sin(r) + c * math.cos(r), -b * math.sin(r) + d * math.cos(r))
    lw, lh = math.hypot(*ex), math.hypot(*ey)
    ex_u = (ex[0] / lw if lw else 1.0, ex[1] / lw if lw else 0.0)
    ey_u = (ey[0] / lh if lh else 0.0, ey[1] / lh if lh else 1.0)
    new_w, new_h = w * lw, h * lh
    fx = -1.0 if flip_h else 1.0
    fy = -1.0 if flip_v else 1.0
    # The child's own flips, as columns of its frame.
    col1 = (ex_u[0] * fx, ex_u[1] * fx)
    col2 = (ey_u[0] * fy, ey_u[1] * fy)
    det = col1[0] * col2[1] - col1[1] * col2[0]
    if det >= 0:
        new_rot = math.degrees(math.atan2(col1[1], col1[0]))
        new_flip_h = new_flip_v = False
    else:
        # R(rot) * diag(-1, 1): the first column of the turn is -col1.
        new_rot = math.degrees(math.atan2(-col1[1], -col1[0]))
        new_flip_h = True
        new_flip_v = False
    return cx - new_w / 2.0, cy - new_h / 2.0, new_w, new_h, new_rot % 360.0, new_flip_h, new_flip_v


# ---------------------------------------------------------------- reading the slide
SHAPE_KINDS = {
    "rect": "rectangle", "roundRect": "rounded_rect", "ellipse": "ellipse", "triangle": "triangle",
    "rtTriangle": "right_triangle", "diamond": "rhombus", "cloudCallout": "cloud", "cloud": "cloud",
    "wedgeEllipseCallout": "ellipse", "wedgeRoundRectCallout": "rounded_rect",
    "wedgeRectCallout": "rectangle", "flowChartProcess": "rectangle",
    "flowChartAlternateProcess": "rounded_rect", "flowChartDecision": "rhombus",
    "flowChartConnector": "ellipse", "donut": "ellipse", "snip1Rect": "rectangle",
    "snip2SameRect": "rectangle", "round1Rect": "rounded_rect", "round2SameRect": "rounded_rect",
    "plaque": "rounded_rect", "frame": "rectangle", "heart": "ellipse", "sun": "ellipse",
    "star4": "star", "star5": "star", "star6": "star", "star7": "star", "star8": "star",
    "star10": "star", "star12": "star", "star16": "star", "star24": "star", "star32": "star",
    "pentagon": "polygon", "hexagon": "polygon", "octagon": "polygon", "heptagon": "polygon",
    "decagon": "polygon", "dodecagon": "polygon",
    "rightArrow": "arrow", "leftArrow": "arrow", "upArrow": "arrow", "downArrow": "arrow",
    "leftRightArrow": "double_arrow", "upDownArrow": "double_arrow",
    "chevron": "arrow", "homePlate": "arrow",
}
EXACT_KINDS = {"rect", "roundRect", "ellipse", "triangle", "rtTriangle", "diamond", "cloudCallout",
               "star5", "pentagon", "hexagon", "octagon", "rightArrow"}
POLY_POINTS = {"pentagon": 5, "hexagon": 6, "octagon": 8, "heptagon": 7, "decagon": 10,
               "dodecagon": 12}
TEXT_INSET = {"ellipse": 0.146, "triangle": 0.25, "rtTriangle": 0.2, "diamond": 0.25,
              "roundRect": 0.05, "cloudCallout": 0.15, "cloud": 0.15, "star5": 0.2,
              "hexagon": 0.12, "pentagon": 0.15, "octagon": 0.1}
LINE_GEOMS = {"line", "lineInv", "straightConnector1", "bentConnector2", "bentConnector3",
              "bentConnector4", "bentConnector5", "curvedConnector2", "curvedConnector3",
              "curvedConnector4", "curvedConnector5"}
DASHES = {"solid": "solid", "dash": "dash", "lgDash": "dash", "sysDash": "dash", "dot": "dot",
          "sysDot": "dot", "dashDot": "dash_dot", "lgDashDot": "dash_dot",
          "lgDashDotDot": "dash_dot", "sysDashDot": "dash_dot", "sysDashDotDot": "dash_dot"}


class Merged:
    """Paragraph and run properties gathered level by level from a chain of styles."""

    def __init__(self):
        self.ppr = {}
        self.rpr = {}

    def add_ppr(self, el, with_defaults=True):
        if el is None:
            return
        for k, v in el.attrib.items():
            self.ppr[k] = v
        for child in el:
            t = tag(child)
            if t in ("lnSpc", "spcBef", "spcAft"):
                self.ppr[t] = child
            elif t in ("buNone", "buAutoNum", "buChar", "buBlip"):
                self.ppr["bu"] = child
            elif t == "defRPr" and with_defaults:
                self.add_rpr(child)
            elif t in ("buClr", "buClrTx", "buSzPct", "buSzPts", "buSzTx", "buFont", "buFontTx"):
                self.ppr[t] = child
        return self

    def add_rpr(self, el):
        if el is None:
            return
        for k, v in el.attrib.items():
            self.rpr[k] = v
        for child in el:
            t = tag(child)
            if t in ("noFill", "solidFill", "gradFill", "blipFill", "pattFill", "grpFill"):
                self.rpr["fill"] = child
            elif t in ("ln", "effectLst", "effectDag", "latin", "ea", "cs", "sym", "highlight",
                       "hlinkClick", "hlinkMouseOver"):
                self.rpr[t] = child

    def copy(self):
        out = Merged()
        out.ppr = dict(self.ppr)
        out.rpr = dict(self.rpr)
        return out


class Tally(collections.Counter):
    """The counts of a deck, and which slides each feature or limit was on."""

    def __init__(self):
        super().__init__()
        self.slide = 0
        self.on_slides = collections.defaultdict(set)

    def __setitem__(self, key, value):
        super().__setitem__(key, value)
        if isinstance(key, str) and key.split(":")[0] in ("feature", "limit", "skip", "approx"):
            self.on_slides[key].add(self.slide)


class Converter:
    """One deck: its package, its themes and the counts of what it could not convert."""

    def __init__(self, path, out_dir, media_videos=True):
        self.pkg = Pkg(path)
        self.out_dir = pathlib.Path(out_dir)
        self.images = self.out_dir / "images"
        self.videos = self.out_dir / "videos"
        self.media_videos = media_videos
        self.images.mkdir(parents=True, exist_ok=True)
        self.videos.mkdir(parents=True, exist_ok=True)
        self.counts = Tally()
        self.themes = {}
        self.extracted = {}
        self.fonts_used = set()
        self.z = 0
        self.next_id = 1
        pres = self.pkg.xml("ppt/presentation.xml")
        size = pres.find("p:sldSz", NS)
        self.cx, self.cy = int(size.get("cx")), int(size.get("cy"))
        self.scale = CANVAS_W / self.cx
        self.px_per_pt = self.scale * EMU_PER_PT
        self.canvas_h = round(self.cy * self.scale)
        self.default_style = pres.find("p:defaultTextStyle", NS)
        self.slides = []
        rels = self.pkg.rels("ppt/presentation.xml")
        for sld in pres.find("p:sldIdLst", NS):
            rid = sld.get(RID + "id")
            if rid in rels:
                self.slides.append(rels[rid][1])

    def theme_of(self, part):
        if part not in self.themes:
            self.themes[part] = Theme(self.pkg.xml(part))
        return self.themes[part]

    def obj_id(self):
        self.next_id += 1
        return "o%d" % self.next_id

    def obj(self, ctx, typ, frame, name, opacity=1.0):
        x, y, w, h, rot, flip_h, flip_v = frame
        self.z += 1
        out = {"id": self.obj_id(), "type": typ, "x": round(x, 2), "y": round(y, 2),
               "w": round(max(w, 0.0), 2), "h": round(max(h, 0.0), 2),
               "rotation": round(rot % 360.0, 3), "opacity": round(opacity, 3), "z": self.z}
        if name:
            out["name"] = name[:60]
        if flip_h:
            out["flip_h"] = True
        if flip_v:
            out["flip_v"] = True
        return out

    def emu(self, v):
        return float(v) * self.scale if v is not None else None

    def pt(self, hundredths):
        return float(hundredths) / 100.0 * self.px_per_pt

    # --- media -------------------------------------------------------------
    def extract(self, ctx, rid, video=False):
        """Copy a relationship's media into the deck's folder; its file name, or None."""
        if not rid:
            return None
        rel = self.pkg.rels(ctx.part).get(rid)
        if rel is None:
            return None
        _type, target, mode = rel
        if mode == "External":
            self.counts["skip:external_media"] += 1
            return None
        if target not in self.pkg.names:
            self.counts["skip:missing_media"] += 1
            return None
        base = posixpath.basename(target)
        ext = pathlib.Path(base).suffix.lower()
        into = self.videos if video else self.images
        key = (target, video)
        if key not in self.extracted:
            if video and not self.media_videos:
                self.extracted[key] = base
                return base
            if not video and ext not in IMAGE_EXT:
                self.counts["skip:unreadable_image:" + ext] += 1
                self.extracted[key] = None
                return None
            (into / base).write_bytes(self.pkg.zip.read(target))
            self.extracted[key] = base
        return self.extracted[key]

    # --- fills, lines, effects ------------------------------------------------
    def read_fill(self, parent, ctx, ph=None):
        """A fill description from whatever fill element ``parent`` holds, or None."""
        if parent is None:
            return None
        for el in parent:
            t = tag(el)
            if t == "noFill":
                return {"k": "none"}
            if t == "solidFill":
                c = first_color(el, ctx, ph)
                return {"k": "solid", "c": c} if c else None
            if t == "gradFill":
                return self.read_gradient(el, ctx, ph)
            if t == "blipFill":
                return {"k": "blip", "el": el}
            if t == "pattFill":
                self.counts["approx:pattern_fill"] += 1
                c = first_color(el.find("a:fgClr", NS), ctx, ph) or (0, 0, 0, 1)
                bg = first_color(el.find("a:bgClr", NS), ctx, ph) or (1, 1, 1, 1)
                return {"k": "solid", "c": tuple((c[i] + bg[i]) / 2 for i in range(4))}
            if t == "grpFill":
                return {"k": "group"}
        return None

    def read_gradient(self, el, ctx, ph):
        stops = []
        for gs in el.findall("a:gsLst/a:gs", NS):
            c = first_color(gs, ctx, ph)
            if c:
                stops.append({"at": round(float(gs.get("pos", 0)) / 100000.0, 4), "color": color_css(c)})
        stops.sort(key=lambda s: s["at"])
        grad = {"kind": "linear", "angle": 90, "stops": stops}
        lin = el.find("a:lin", NS)
        path = el.find("a:path", NS)
        if lin is not None:
            grad["angle"] = round(float(lin.get("ang", 0)) / 60000.0, 3)
        elif path is not None:
            grad["kind"] = "radial"
            rect = path.find("a:fillToRect", NS)
            if path.get("path") != "circle":
                self.counts["limit:gradient_path_shape_or_rect"] += 1
            if rect is not None:
                l, t, r, b = (float(rect.get(k, 0)) / 100000.0 for k in "ltrb")
                grad["center_x"] = round((l + 1.0 - r) / 2.0, 4)
                grad["center_y"] = round((t + 1.0 - b) / 2.0, 4)
        return {"k": "grad", "g": grad, "first": stops[0]["color"] if stops else "#000000"}

    def style_fill(self, sp, ctx):
        style = sp.find("p:style", NS)
        if style is None:
            return None
        ref = style.find("a:fillRef", NS)
        if ref is None or ref.get("idx", "0") == "0":
            return None
        idx = int(ref.get("idx"))
        ph = first_color(ref, ctx)
        fills = ctx.theme.fills if idx < 1000 else ctx.theme.bg_fills
        idx = idx - 1 if idx < 1000 else idx - 1001
        if 0 <= idx < len(fills):
            holder = ET.Element("h")
            holder.append(fills[idx])
            return self.read_fill(holder, ctx, ph)
        return None

    def apply_fill(self, out, fill, ctx, group_fill=None):
        """Write a fill description onto a JSON object."""
        if fill is not None and fill["k"] == "group":
            fill = group_fill
        if fill is None or fill["k"] == "none":
            out["fill_kind"] = "none"
            out["fill"] = ""
            return False
        if fill["k"] == "solid":
            out["fill"] = color_css(fill["c"])
            return True
        if fill["k"] == "grad":
            out["fill_kind"] = "gradient"
            out["fill"] = fill["first"]
            out["gradient"] = fill["g"]
            self.counts["feature:gradient"] += 1
            return True
        if fill["k"] == "blip":
            blip = fill["el"].find("a:blip", NS)
            file = self.extract(ctx, self.blip_rid(blip))
            if not file:
                self.counts["skip:picture_fill_no_media"] += 1
                out["fill_kind"] = "none"
                out["fill"] = ""
                return False
            pic = {"file": file, "fit": "fill"}
            crop = self.read_crop(fill["el"].find("a:srcRect", NS), fill["el"])
            if crop:
                pic["crop"] = crop
            if fill["el"].find("a:tile", NS) is not None:
                self.counts["approx:tile_fill"] += 1
                pic["fit"] = "cover"
            alpha = blip.find("a:alphaModFix", NS) if blip is not None else None
            if alpha is not None:
                out["opacity"] = round(pct(alpha.get("amt")) * out.get("opacity", 1.0), 3)
            out["fill_kind"] = "picture"
            out["fill"] = ""
            out["fill_picture"] = pic
            self.counts["feature:picture_fill"] += 1
            return True
        return False

    @staticmethod
    def blip_rid(blip):
        """A blip's picture: its embedded one, or an SVG-only blip's SVG."""
        if blip is None:
            return None
        rid = blip.get(RID + "embed")
        if rid:
            return rid
        svg = blip.find("a:extLst/a:ext/asvg:svgBlip", NS)
        return svg.get(RID + "embed") if svg is not None else None

    def read_crop(self, src, holder=None):
        """A crop from an srcRect and from a stretch's fillRect insets, shares of the picture.

        A negative inset stretches the picture past its frame, which shows as a crop;
        a positive one shrinks it inside the frame, which SeventhSlide cannot do."""
        crop = {"left": 0.0, "top": 0.0, "right": 0.0, "bottom": 0.0}
        keys = (("left", "l"), ("top", "t"), ("right", "r"), ("bottom", "b"))
        if src is not None:
            for k, a in keys:
                crop[k] = max(0.0, pct(src.get(a), 0))
        fill_rect = holder.find("a:stretch/a:fillRect", NS) if holder is not None else None
        if fill_rect is not None and any(fill_rect.get(a) for _k, a in keys):
            insets = {k: pct(fill_rect.get(a), 0) for k, a in keys}
            if any(v > 0 for v in insets.values()):
                self.counts["limit:picture_inset_inside_frame"] += 1
            span_x = 1.0 - insets["left"] - insets["right"]
            span_y = 1.0 - insets["top"] - insets["bottom"]
            if span_x > 0 and span_y > 0:
                vis_x = 1.0 - crop["left"] - crop["right"]
                vis_y = 1.0 - crop["top"] - crop["bottom"]
                for k, span, vis, start in (("left", span_x, vis_x, crop["left"]),
                                            ("right", span_x, vis_x, crop["right"]),
                                            ("top", span_y, vis_y, crop["top"]),
                                            ("bottom", span_y, vis_y, crop["bottom"])):
                    crop[k] = start + max(0.0, -insets[k]) / span * vis
            self.counts["feature:picture_fillrect_inset"] += 1
        return crop if any(crop.values()) else None

    def read_line(self, sp_pr, sp, ctx):
        """(colour, width_px, dash, start, end) or None."""
        ln = sp_pr.find("a:ln", NS) if sp_pr is not None else None
        style = sp.find("p:style", NS) if sp is not None else None
        ref = style.find("a:lnRef", NS) if style is not None else None
        ref_ln = None
        ref_color = None
        if ref is not None and ref.get("idx", "0") != "0":
            idx = int(ref.get("idx")) - 1
            ref_color = first_color(ref, ctx)
            if 0 <= idx < len(ctx.theme.lines):
                ref_ln = ctx.theme.lines[idx]
        fill = self.read_fill(ln, ctx) if ln is not None else None
        if fill is None and ref_ln is not None:
            fill = self.read_fill(ref_ln, ctx, ref_color)
        if fill is None or fill["k"] == "none":
            return None
        if fill["k"] == "grad":
            color = fill["first"]
            self.counts["approx:gradient_line"] += 1
        elif fill["k"] == "solid":
            color = color_css(fill["c"])
        else:
            self.counts["approx:other_line_fill"] += 1
            color = "#808080"
        width = ln.get("w") if ln is not None and ln.get("w") else None
        if width is None and ref_ln is not None:
            width = ref_ln.get("w")
        width_px = max(float(width if width is not None else 9525) * self.scale, 0.75)
        dash = "solid"
        for holder in (ln, ref_ln):
            el = holder.find("a:prstDash", NS) if holder is not None else None
            if el is not None:
                dash = DASHES.get(el.get("val"), "solid")
                break
        ends = {}
        for key, name in (("headEnd", "line_start"), ("tailEnd", "line_end")):
            el = ln.find("a:" + key, NS) if ln is not None else None
            if el is not None and el.get("type", "none") != "none":
                ends[name] = "dot" if el.get("type") == "oval" else "arrow"
        return color, width_px, dash, ends

    def apply_line(self, out, line):
        if line is None:
            out["stroke"] = ""
            out["stroke_width"] = 0
            return
        color, width, dash, ends = line
        out["stroke"] = color
        out["stroke_width"] = round(min(width, 200.0), 3)
        if dash != "solid":
            out["line_dash"] = dash
        out.update(ends)

    def read_effects(self, parent, ctx, ph=None):
        """A dict of the outer shadow, glow and soft edge in an effect list."""
        out = {}
        if parent is None:
            return out
        for el in parent:
            t = tag(el)
            if t == "outerShdw":
                c = first_color(el, ctx, ph) or (0, 0, 0, 0.4)
                out["shadow"] = {"on": True, "color": color_css(c),
                                 "blur": round(float(el.get("blurRad", 0)) * self.scale * 0.5, 2),
                                 "distance": round(float(el.get("dist", 0)) * self.scale, 2),
                                 "angle": round(float(el.get("dir", 0)) / 60000.0, 2)}
            elif t == "glow":
                c = first_color(el, ctx, ph) or (1, 1, 1, 0.5)
                out["glow"] = {"on": True, "color": color_css(c),
                               "size": round(float(el.get("rad", 0)) * self.scale, 2)}
            elif t == "softEdge":
                out["soft_edge"] = round(float(el.get("rad", 0)) * self.scale, 2)
            elif t in ("innerShdw", "reflection", "prstShdw", "fillOverlay", "blur"):
                self.counts["skip:effect_" + t] += 1
        return out

    def shape_effects(self, sp_pr, sp, ctx):
        """The effects of a shape: its own list, or its style's effect reference."""
        if sp_pr is not None:
            lst = sp_pr.find("a:effectLst", NS)
            if lst is not None:
                return self.read_effects(lst, ctx)
        style = sp.find("p:style", NS) if sp is not None else None
        ref = style.find("a:effectRef", NS) if style is not None else None
        if ref is not None and ref.get("idx", "0") != "0":
            idx = int(ref.get("idx")) - 1
            ph = first_color(ref, ctx)
            if 0 <= idx < len(ctx.theme.effects):
                holder = ctx.theme.effects[idx].find("a:effectLst", NS)
                return self.read_effects(holder, ctx, ph)
        return {}

    # --- placeholders -------------------------------------------------------
    @staticmethod
    def placeholder(sp):
        return sp.find("p:nvSpPr/p:nvPr/p:ph", NS) if tag(sp) == "sp" else sp.find(
            "p:nvPicPr/p:nvPr/p:ph", NS) if tag(sp) == "pic" else None

    @staticmethod
    def find_placeholder(root, ph_type, ph_idx, by_type_only=False):
        """A placeholder on a layout or master: by index on a layout, by type above it."""
        tree = root.find("p:cSld/p:spTree", NS)
        if tree is None:
            return None
        norm = {"ctrTitle": "title", "subTitle": "body", "obj": "body"}
        wanted = norm.get(ph_type, ph_type)
        by_type = None
        for node in tree.iter():
            if tag(node) not in ("sp", "pic"):
                continue
            ph = Converter.placeholder(node)
            if ph is None:
                continue
            if not by_type_only and ph_idx is not None and ph.get("idx") == ph_idx:
                return node
            this = norm.get(ph.get("type", "body"), ph.get("type", "body"))
            if this == wanted and by_type is None:
                by_type = node
        return by_type

    def inherited(self, sp, ctx):
        """[master ph, layout ph, slide element] for a placeholder (missing ones dropped)."""
        ph = self.placeholder(sp)
        if ph is None:
            return [sp]
        ph_type = ph.get("type", "obj")
        ph_idx = ph.get("idx", "0")
        layout_sp = self.find_placeholder(ctx.layout, ph_type, ph_idx)
        layout_ph_type = ph_type
        if layout_sp is not None:
            layout_ph_type = self.placeholder(layout_sp).get("type", ph_type)
        master_sp = self.find_placeholder(ctx.master, layout_ph_type, None, by_type_only=True)
        return [e for e in (master_sp, layout_sp, sp) if e is not None]

    def transform_of(self, chain):
        """Off, ext and turn of the first element of the chain (slide first) that has one."""
        for sp in reversed(chain):
            xfrm = sp.find("p:spPr/a:xfrm", NS)
            if xfrm is None and tag(sp) == "pic":
                xfrm = sp.find("p:spPr/a:xfrm", NS)
            if xfrm is not None and xfrm.find("a:off", NS) is not None:
                return xfrm
        return None

    def frame_of(self, xfrm, matrix):
        off = xfrm.find("a:off", NS)
        ext = xfrm.find("a:ext", NS)
        x = float(off.get("x", 0))
        y = float(off.get("y", 0))
        w = float(ext.get("cx", 0))
        h = float(ext.get("cy", 0))
        rot = float(xfrm.get("rot", 0)) / 60000.0
        flip_h = xfrm.get("flipH") == "1"
        flip_v = xfrm.get("flipV") == "1"
        return place_frame(matrix, x, y, w, h, rot, flip_h, flip_v)

    def scaled(self, frame):
        x, y, w, h, r, fh, fv = frame
        s = self.scale
        return (x * s, y * s, w * s, h * s, r, fh, fv)

    # --- geometry -----------------------------------------------------------
    def cust_paths(self, cust):
        """Every subpath of a custom geometry as (points in 0..1, closed)."""
        paths = cust.findall("a:pathLst/a:path", NS)
        subs = []
        any_filled = False
        for path in paths:
            pw = float(path.get("w", 0) or 0)
            ph = float(path.get("h", 0) or 0)
            filled = path.get("fill", "norm") != "none"
            any_filled = any_filled or filled
            cur = None
            start = None
            pts = []

            def flush(closed):
                nonlocal pts
                if len(pts) >= 2:
                    subs.append((pts, closed or filled, filled))
                pts = []

            def pt_of(node):
                pt = node.find("a:pt", NS) if node.tag != "{%s}pt" % NS["a"] else node
                try:
                    x = float(pt.get("x", 0))
                    y = float(pt.get("y", 0))
                except (TypeError, ValueError):
                    return (0.0, 0.0)
                return (x / pw if pw else 0.0, y / ph if ph else 0.0)

            for cmd in path:
                t = tag(cmd)
                if t == "moveTo":
                    flush(False)
                    cur = start = pt_of(cmd)
                    pts.append(cur)
                elif t == "lnTo":
                    cur = pt_of(cmd)
                    pts.append(cur)
                elif t == "cubicBezTo":
                    p = [pt_of(n) for n in cmd.findall("a:pt", NS)]
                    if len(p) == 3 and cur is not None:
                        self.counts["feature:custom_curve"] += 1
                        for i in range(1, 9):
                            u = i / 8.0
                            v = 1 - u
                            x = v**3 * cur[0] + 3 * v * v * u * p[0][0] + 3 * v * u * u * p[1][0] + u**3 * p[2][0]
                            y = v**3 * cur[1] + 3 * v * v * u * p[0][1] + 3 * v * u * u * p[1][1] + u**3 * p[2][1]
                            pts.append((x, y))
                        cur = p[2]
                elif t == "quadBezTo":
                    p = [pt_of(n) for n in cmd.findall("a:pt", NS)]
                    if len(p) == 2 and cur is not None:
                        for i in range(1, 9):
                            u = i / 8.0
                            v = 1 - u
                            x = v * v * cur[0] + 2 * v * u * p[0][0] + u * u * p[1][0]
                            y = v * v * cur[1] + 2 * v * u * p[0][1] + u * u * p[1][1]
                            pts.append((x, y))
                        cur = p[1]
                elif t == "arcTo":
                    if cur is None or not pw or not ph:
                        continue
                    wr = float(cmd.get("wR", 0)) / pw
                    hr = float(cmd.get("hR", 0)) / ph
                    st = math.radians(float(cmd.get("stAng", 0)) / 60000.0)
                    sw = math.radians(float(cmd.get("swAng", 0)) / 60000.0)
                    cx = cur[0] - wr * math.cos(st)
                    cy = cur[1] - hr * math.sin(st)
                    for i in range(1, 17):
                        a = st + sw * i / 16.0
                        pts.append((cx + wr * math.cos(a), cy + hr * math.sin(a)))
                    cur = pts[-1]
                elif t == "close":
                    flush(True)
                    cur = start
                    if cur is not None:
                        pts = [cur]
            if len(pts) >= 2:
                flush(False)
        return subs

    def emit_shape_kind(self, out, geom):
        """Fill in shape_kind and its adjustments from a prstGeom."""
        name = geom.get("prst", "rect")
        kind = SHAPE_KINDS.get(name)
        if kind is None:
            kind = "rectangle"
            self.counts["approx:shape:" + name] += 1
        elif name not in EXACT_KINDS:
            self.counts["approx:shape:" + name] += 1
        out["shape_kind"] = kind
        self.counts["feature:shape:" + kind] += 1
        adj = {g.get("name"): g.get("fmla", "") for g in geom.findall("a:avLst/a:gd", NS)}
        val = lambda key, d: float(adj[key].split()[-1]) if key in adj and adj[key].split() else d
        if kind == "rounded_rect":
            out["shape_corner"] = round(val("adj", 16667) / 100000.0, 4)
        elif kind == "polygon":
            out["shape_points"] = POLY_POINTS.get(name, 6)
        elif kind == "star":
            digits = re.sub(r"\D", "", name)
            out["shape_points"] = int(digits) if digits else 5
            out["shape_inner"] = round(val("adj", 19098 if name == "star5" else 37500) / 50000.0, 4)
        elif kind in ("arrow", "double_arrow"):
            out["shape_thickness"] = round(val("adj1", 50000) / 100000.0, 4)
            out["shape_head"] = round(min(1.0, val("adj2", 50000) / 100000.0), 4)
        return name

    def line_points(self, name, flip_h, flip_v):
        """A connector's points in its own box, before its turn."""
        x0, y0, x1, y1 = (0.0, 0.0, 1.0, 1.0)
        if name.startswith("bentConnector"):
            pts = [(x0, y0), (0.5, y0), (0.5, y1), (x1, y1)]
        elif name.startswith("curvedConnector"):
            pts = [(x0, y0), (0.5, 0.2), (0.5, 0.8), (x1, y1)]
        else:
            pts = [(x0, y0), (x1, y1)]
        out = []
        for x, y in pts:
            out.append((1.0 - x if flip_h else x, 1.0 - y if flip_v else y))
        return out

    # --- text ---------------------------------------------------------------
    def text_chain(self, sp, chain, ctx):
        """The style elements a shape's text inherits, lowest priority first."""
        styles = []
        ph = self.placeholder(sp)
        if self.default_style is not None:
            styles.append(self.default_style)
        if ph is not None:
            kind = ph.get("type", "obj")
            tx = ctx.master.find("p:txStyles", NS)
            if tx is not None:
                if kind in ("title", "ctrTitle"):
                    styles.append(tx.find("p:titleStyle", NS))
                elif kind in ("subTitle", "body", "obj", "pic", "tbl", "chart", "dgm", "media"):
                    styles.append(tx.find("p:bodyStyle", NS))
                else:
                    styles.append(tx.find("p:otherStyle", NS))
        for node in chain:
            body = node.find("p:txBody", NS)
            if body is not None:
                styles.append(body.find("a:lstStyle", NS))
        return [s for s in styles if s is not None]

    def level_style(self, styles, level):
        merged = Merged()
        name = "lvl%dpPr" % (level + 1)
        for st in styles:
            merged.add_ppr(st.find("a:defPPr", NS))
            merged.add_ppr(st.find("a:" + name, NS))
        return merged

    def body_prop(self, chain, name, default=None):
        for node in reversed(chain):
            el = node.find("p:txBody/a:bodyPr", NS)
            if el is not None and el.get(name) is not None:
                return el.get(name)
        return default

    def body_child(self, chain, names):
        for node in reversed(chain):
            el = node.find("p:txBody/a:bodyPr", NS)
            if el is None:
                continue
            for child in el:
                if tag(child) in names:
                    return child
        return None

    def run_props(self, level, rpr_el, run_ctx):
        out = level.copy()
        if rpr_el is not None:
            out.add_rpr(rpr_el)
        return out

    def font_of(self, rpr, ctx):
        latin = rpr.get("latin")
        face = latin.get("typeface") if latin is not None else None
        if not face or face.startswith("+mn"):
            return ctx.theme.minor
        if face.startswith("+mj"):
            return ctx.theme.major
        return face

    def read_paragraphs(self, body, styles, ctx):
        """A shape's paragraphs: each its level, its merged properties and runs."""
        out = []
        for para in body.findall("a:p", NS):
            ppr_el = para.find("a:pPr", NS)
            level = int(ppr_el.get("lvl", 0)) if ppr_el is not None else 0
            merged = self.level_style(styles, level)
            if ppr_el is not None:
                # A defRPr here (Keynote's exports carry their sizes this way) counts,
                # as it does in LibreOffice's renders of those decks.
                merged.add_ppr(ppr_el)
            base = Merged()
            base.rpr = dict(merged.rpr)
            runs = []
            for child in para:
                t = tag(child)
                if t == "br":
                    runs.append({"br": True, "rpr": base.copy() if child.find("a:rPr", NS) is None
                                 else self.run_props(base, child.find("a:rPr", NS), ctx)})
                elif t in ("r", "fld"):
                    text_el = child.find("a:t", NS)
                    text = text_el.text if text_el is not None and text_el.text else ""
                    props = self.run_props(base, child.find("a:rPr", NS), ctx)
                    if t == "fld":
                        self.counts["feature:field"] += 1
                    runs.append({"text": text, "rpr": props})
            end = para.find("a:endParaRPr", NS)
            out.append({"level": level, "merged": merged, "runs": runs,
                        "end": self.run_props(base, end, ctx), "pPr": ppr_el})
        return out

    def run_color(self, props, ctx, default):
        fill = props.rpr.get("fill")
        if props.rpr.get("hlinkClick") is not None:
            rgb = ctx.scheme_color("hlink")
            return (*rgb, 1.0)
        if fill is None:
            return default
        t = tag(fill)
        if t == "noFill":
            return (0.0, 0.0, 0.0, 0.0)
        if t == "solidFill":
            return first_color(fill, ctx) or default
        if t == "gradFill":
            stops = fill.findall("a:gsLst/a:gs", NS)
            self.counts["approx:gradient_text"] += 1
            return first_color(stops[0], ctx) or default if stops else default
        return default

    def convert_text(self, sp, chain, ctx, frame, typ_name, shape_fill_has):
        """A Text object for a shape's words, or None if it has none."""
        body = sp.find("p:txBody", NS)
        if body is None:
            return None
        styles = self.text_chain(sp, chain, ctx)
        paragraphs = self.read_paragraphs(body, styles, ctx)
        has_text = any(r.get("text", "").strip() for p in paragraphs for r in p["runs"]
                       if not r.get("br"))
        if not has_text:
            return None

        # style font colour reference for shapes that use a p:style
        style = sp.find("p:style", NS)
        default_rgb = None
        font_ref = style.find("a:fontRef", NS) if style is not None else None
        if font_ref is not None:
            c = first_color(font_ref, ctx)
            if c:
                default_rgb = c
        if default_rgb is None:
            default_rgb = (*ctx.scheme_color("tx1"), 1.0)

        # autofit
        norm = self.body_child(chain, {"normAutofit"})
        no_autofit = self.body_child(chain, {"noAutofit"})
        sp_auto = self.body_child(chain, {"spAutoFit"})
        font_scale = 1.0
        line_reduction = 0.0
        autofit = None
        if norm is not None and self.body_child([sp], {"noAutofit", "spAutoFit"}) is None:
            font_scale = pct(norm.get("fontScale"), 100000)
            line_reduction = pct(norm.get("lnSpcReduction"), 0)
            if font_scale < 0.999:
                autofit = "shrink"
                self.counts["feature:autofit"] += 1
        elif no_autofit is not None:
            pass
        elif sp_auto is not None:
            self.counts["feature:spautofit"] += 1

        # each run, resolved
        lines = []  # list of lines, each list of run dicts
        para_of_line = []
        current = []
        para_meta = []
        for pi, para in enumerate(paragraphs):
            para_meta.append(para)
            current = []
            had_any = False
            for run in para["runs"]:
                if run.get("br"):
                    lines.append(current)
                    para_of_line.append(pi)
                    current = []
                    continue
                text = run["text"]
                if text == "":
                    continue
                had_any = True
                props = run["rpr"]
                current.append(self.resolve_run(text, props, ctx, default_rgb, font_scale))
            if not current and not had_any:
                current = [self.resolve_run("", para["end"], ctx, default_rgb, font_scale)]
                current[0]["empty"] = True
            lines.append(current)
            para_of_line.append(pi)

        runs = [r for ln in lines for r in ln if not r.get("empty")]
        if not runs:
            return None
        chars = collections.Counter()
        for r in runs:
            chars[r["font"]] += max(1, len(r["text"]))
        family = chars.most_common(1)[0][0]
        self.fonts_used.add(family)
        box_px = max(r["px"] for r in runs)
        box_size = max(1, int(round(box_px)))
        colors = collections.Counter()
        for r in runs:
            colors[r["css"]] += max(1, len(r["text"]))
        font_color = colors.most_common(1)[0][0]
        all_bold = all(r["bold"] for r in runs)
        all_italic = all(r["italic"] for r in runs)
        all_under = all(r["underline"] for r in runs)

        x, y, w, h, rot, flip_h, flip_v = frame
        if flip_h or flip_v:
            self.counts["approx:flipped_text"] += 1
        # PowerPoint keeps text unmirrored in a flipped shape (flipV reads upside down).
        if flip_v:
            rot = rot + 180.0
        if float(self.body_prop(chain, "rot", 0) or 0):
            self.counts["limit:text_rotation_body"] += 1
        if self.body_prop(chain, "vert", "horz") not in ("horz", None):
            self.counts["limit:vertical_text"] += 1

        out = self.obj(ctx, "text", (x, y, w, h, rot, False, False), typ_name)
        out["font_family"] = family
        out["font_size"] = box_size
        out["font_color"] = font_color
        out["bold"] = all_bold
        out["italic"] = all_italic
        out["underline"] = all_under

        # alignment (box-level in SeventhSlide)
        aligns = collections.Counter()
        for para in paragraphs:
            if any(r.get("text", "").strip() for r in para["runs"] if not r.get("br")):
                aligns[para["merged"].ppr.get("algn", "l")] += 1
        if len(aligns) > 1:
            self.counts["feature:paragraph_alignment"] += 1
        algn = aligns.most_common(1)[0][0] if aligns else "l"
        out["text_align"] = {"ctr": "center", "r": "right", "just": "left", "justLow": "left",
                             "dist": "left", "thaiDist": "left"}.get(algn, "left")
        if algn in ("just", "justLow", "dist"):
            self.counts["approx:justified"] += 1
        anchor = self.body_prop(chain, "anchor", "t")
        out["vertical_align"] = {"ctr": "middle", "b": "bottom"}.get(anchor, "top")
        if self.body_prop(chain, "anchorCtr", "0") == "1":
            self.counts["approx:anchor_center"] += 1

        # line spacing: box-level, from the first paragraph with words
        ratio = natural_ratio(family, all_bold)
        visible_paras = [p for p in paragraphs
                         if any(r.get("text", "").strip() for r in p["runs"] if not r.get("br"))]
        factor = self.line_factor(visible_paras[0], box_px, ratio, line_reduction)
        if abs(self.line_factor(visible_paras[0], box_px, ratio, 0.0) - ratio) > 0.01:
            self.counts["feature:line_spacing"] += 1
        factors = {round(self.line_factor(p, box_px, ratio, line_reduction), 2) for p in visible_paras}
        if len(factors) > 1:
            self.counts["feature:paragraph_line_spacing"] += 1
        factor = max(0.8, min(3.0, factor))
        out["line_height"] = round(factor, 3)
        out["_shift"] = self.baseline_shift(lines, out["line_height"], ratio, family, all_bold)

        # An empty paragraph keeps its own height: a share of the box's line height.
        markup_lines = []
        for ln, pi in zip(lines, para_of_line):
            para = paragraphs[pi]
            if not ln or (len(ln) == 1 and ln[0].get("empty")):
                target = self.para_height_px(para, ln[0]["px"] if ln else box_px, ratio, line_reduction)
                ln = [{"empty": True, "px": target / factor, "css": font_color, "font": family,
                       "bold": False, "italic": False, "underline": False, "text": "", "rise": 0}]
            for r in ln:
                self.fonts_used.add(r["font"])
            # A line with a style of its own takes nothing from its box (layout::styled_line):
            # bold and the rest have to be on its runs.
            styled = any(r.get("empty") or round(r["px"] / box_size * 100.0) != 100 or r["css"] != font_color
                         or r["font"] != family or r["rise"] for r in ln)
            parts = [self.run_markup(r, family, box_size, font_color,
                                     all_bold and not styled, all_italic and not styled,
                                     all_under and not styled)
                     for r in ln]
            # An empty paragraph's height is its run's size, set above.
            empty = len(ln) == 1 and ln[0].get("empty")
            markup_lines.append(self.line_format(para, visible_paras[0], out, factor, box_px, box_size,
                                                 ratio, line_reduction, len(lines) > len(paragraphs), empty)
                                + "".join(parts))
        out["text"] = "\n".join(markup_lines)
        if re.search(r"\{[^}]*\}", out["text"]):
            self.counts["unsure:curly_braces_in_text"] += 1
        # letter spacing, box-level
        spacing = collections.Counter(r["spc"] for r in runs)
        if len(spacing) > 1:
            self.counts["limit:mixed_letter_spacing"] += 1
        spc = spacing.most_common(1)[0][0]
        out["letter_spacing"] = round(spc, 3)
        if spc:
            self.counts["feature:letter_spacing"] += 1

        # paragraph part
        para = self.paragraph_props(paragraphs, chain, lines, para_of_line, ctx, autofit, box_px, ratio)
        if para:
            out["paragraph"] = para

        # text effects (box-level)
        effects = {}
        outlines = [r["outline"] for r in runs if r["outline"] is not None]
        if outlines:
            self.counts["feature:text_outline"] += 1
            if len(set(json.dumps(o) for o in outlines)) > 1 or len(outlines) < len(runs):
                self.counts["limit:mixed_text_outline"] += 1
            effects["text_outline"] = outlines[0]
        shadows = [r["shadow"] for r in runs if r["shadow"] is not None]
        if shadows:
            self.counts["feature:text_shadow"] += 1
            if len(shadows) < len(runs):
                self.counts["limit:mixed_text_shadow"] += 1
            effects["text_shadow"] = shadows[0]
        out["_effects"] = effects
        if self.body_child(chain, {"prstTxWarp"}) is not None:
            warp = self.body_child(chain, {"prstTxWarp"})
            if warp.get("prst") not in (None, "textNoShape"):
                self.counts["skip:wordart_warp"] += 1
        return out, autofit, shape_fill_has

    def line_factor(self, para, box_px, ratio, line_reduction):
        """A paragraph's line height over the box's font size."""
        lnspc = para["merged"].ppr.get("lnSpc")
        if lnspc is not None and len(lnspc):
            v = lnspc[0]
            if tag(v) == "spcPct":
                return pct(v.get("val")) * ratio * (1 - line_reduction)
            if tag(v) == "spcPts":
                return float(v.get("val")) / 100.0 * self.px_per_pt / box_px if box_px else ratio
        return ratio * (1 - line_reduction)

    def para_height_px(self, para, run_px, ratio, line_reduction):
        """The height of one line of ``para`` set at ``run_px``."""
        lnspc = para["merged"].ppr.get("lnSpc")
        if lnspc is not None and len(lnspc) and tag(lnspc[0]) == "spcPts":
            return float(lnspc[0].get("val")) / 100.0 * self.px_per_pt
        pct_val = pct(lnspc[0].get("val")) if lnspc is not None and len(lnspc) else 1.0
        return run_px * ratio * pct_val * (1 - line_reduction)

    def baseline_shift(self, lines, line_height, ratio, family, bold):
        """How far to move the words down so their baselines land where PowerPoint's do.

        SeventhSlide centres a line's font in its line box (half the leading above);
        PowerPoint hangs the first line from the top of the frame by its Windows ascent and
        the last line's descent from the bottom. Returned for top, bottom and middle anchors."""
        sized = [max((r["px"] for r in ln), default=0.0) for ln in lines if ln]
        sized = [s for s in sized if s > 0] or [0.0]
        m = dict(font_metrics(family, bold))
        if family in FONT_SUBSTITUTED:
            m["win_ascent"], m["win_descent"] = 0.95, 0.25
        spacing = line_height / ratio if ratio else 1.0

        def shifts(size):
            box = math.ceil(size * line_height)
            qt_h = (m["qt_ascent"] + m["qt_descent"]) * size
            baseline = max(0.0, (box - qt_h) / 2.0) + m["qt_ascent"] * size
            target_top = (spacing * ratio - m["win_descent"]) * size
            down_top = target_top - baseline
            target_bottom = m["win_descent"] * size
            down_bottom = (box - baseline) - target_bottom
            return down_top, down_bottom

        down_top, _unused = shifts(sized[0])
        _unused, down_bottom = shifts(sized[-1])
        return {"top": down_top, "bottom": down_bottom, "middle": (down_top + down_bottom) / 2.0}

    def resolve_run(self, text, props, ctx, default_rgb, font_scale):
        rpr = props.rpr
        sz = float(rpr.get("sz", 1800))
        px = self.pt(sz) * font_scale
        font = self.font_of(rpr, ctx)
        bold = rpr.get("b") in ("1", "true")
        italic = rpr.get("i") in ("1", "true")
        fc_query(font, False, False)  # note whether this family is on the machine
        if font in FONT_SUBSTITUTED and re.search(r"\b(bold|black|heavy)\b", font, re.I):
            bold = True  # a family like "Gotham Bold": LibreOffice's stand-in is bold too
        underline = rpr.get("u", "none") not in ("none", "")
        color = self.run_color(props, ctx, default_rgb)
        cap = rpr.get("cap", "none")
        if cap == "all":
            text = text.upper()
        elif cap == "small":
            self.counts["approx:small_caps"] += 1
            text = text.upper()
        if rpr.get("strike", "noStrike") != "noStrike":
            self.counts["limit:strikethrough"] += 1
        if rpr.get("highlight") is not None:
            self.counts["limit:text_highlight"] += 1
        if rpr.get("hlinkClick") is not None:
            underline = True
        spc = self.pt(float(rpr.get("spc", 0))) if rpr.get("spc") else 0.0
        rise = 0
        baseline = int(rpr.get("baseline", 0))
        if baseline > 0:
            rise = baseline / 1000.0
            px *= 2.0 / 3.0
        elif baseline < 0:
            self.counts["limit:subscript"] += 1
            px *= 2.0 / 3.0
        outline = None
        ln = rpr.get("ln")
        if ln is not None:
            if ln.find("a:noFill", NS) is None:
                lfill = self.read_fill(ln, ctx)
                if lfill and lfill["k"] == "solid":
                    outline = {"color": color_css(lfill["c"]),
                               "width": round(max(float(ln.get("w", 9525)) * self.scale, 0.5), 2)}
        shadow = None
        lst = rpr.get("effectLst")
        if lst is not None:
            fx = self.read_effects(lst, ctx)
            shadow = fx.get("shadow")
            if "glow" in fx:
                self.counts["skip:text_glow"] += 1
        return {"text": text, "px": px, "font": font, "bold": bold, "italic": italic,
                "underline": underline, "css": color_css(color), "rise": rise, "spc": spc,
                "outline": outline, "shadow": shadow, "alpha": color[3]}

    def line_format(self, para, first, box, factor, box_px, box_size, ratio, line_reduction, has_br, empty):
        """The <p=...> a paragraph needs for what differs from the box's first one."""
        items = []
        names = {"ctr": "center", "r": "right", "just": "left", "justLow": "left", "dist": "left",
                 "thaiDist": "left"}
        align = names.get(para["merged"].ppr.get("algn", "l"), "left")
        if align != box["text_align"]:
            items.append("align:" + align)
        pct = lambda px: max(0, int(round(px / box_size * 100.0)))
        if not has_br:
            for key, name in (("spcBef", "before"), ("spcAft", "after")):
                own = self.spacing_px(para["merged"].ppr.get(key), box_px, ratio)
                if own != self.spacing_px(first["merged"].ppr.get(key), box_px, ratio):
                    items.append("%s:%d" % (name, pct(own)))
        at = lambda p: float(p["merged"].ppr.get("marL", 0)) + float(p["merged"].ppr.get("indent", 0))
        extra = (at(para) - at(first)) * self.scale
        if extra > 0.5:
            items.append("indent:%d" % pct(extra))
        own = max(0.8, min(3.0, self.line_factor(para, box_px, ratio, line_reduction)))
        if not empty and abs(own - factor) > 0.01:
            items.append("line:%d" % int(round(own * 100.0)))
        return "<p=%s>" % ",".join(items) if items else ""

    @staticmethod
    def run_markup(r, family, box_size, font_color, all_bold, all_italic, all_under):
        size_pct = int(round(r["px"] / box_size * 100.0))
        text = r["text"].replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
        if r.get("empty"):
            # An empty paragraph's size is its end mark's: a lone space keeps the run.
            return "<size=%d> </size>" % max(1, min(999, size_pct))
        out = text
        if r["underline"] and not all_under:
            out = "<u>%s</u>" % out
        if r["italic"] and not all_italic:
            out = "<i>%s</i>" % out
        if r["bold"] and not all_bold:
            out = "<b>%s</b>" % out
        if r["rise"]:
            out = "<rise=%d>%s</rise>" % (max(1, min(100, int(round(r["rise"] * size_pct / 100.0)))), out)
        if r["css"] != font_color:
            out = "<color=%s>%s</color>" % (r["css"], out)
        if r["font"] != family:
            out = "<font=%s>%s</font>" % (r["font"], out)
        if size_pct != 100:
            out = "<size=%d>%s</size>" % (max(1, min(999, size_pct)), out)
        return out

    def paragraph_props(self, paragraphs, chain, lines, para_of_line, ctx, autofit, box_px, ratio):
        """The box-level paragraph part of the stored object."""
        out = {}
        s = self.scale
        l_ins = float(self.body_prop(chain, "lIns", 91440)) * s
        t_ins = float(self.body_prop(chain, "tIns", 45720)) * s
        r_ins = float(self.body_prop(chain, "rIns", 91440)) * s
        b_ins = float(self.body_prop(chain, "bIns", 45720)) * s
        wrap = self.body_prop(chain, "wrap", "square")
        out["_wrap"] = wrap

        visible = [p for p in paragraphs if any(r.get("text", "").strip() for r in p["runs"] if not r.get("br"))]
        first = visible[0] if visible else paragraphs[0]
        ppr = first["merged"].ppr
        mar_l = float(ppr.get("marL", 0)) * s
        indent = float(ppr.get("indent", 0)) * s
        mar_r = float(ppr.get("marR", 0)) * s

        # bullets, box-level
        kinds = collections.Counter()
        for p in visible:
            bu = p["merged"].ppr.get("bu")
            if bu is None or tag(bu) == "buNone":
                kinds["none"] += 1
            elif tag(bu) == "buAutoNum":
                kinds["number"] += 1
            else:
                kinds["disc"] += 1
                if tag(bu) == "buBlip":
                    self.counts["approx:picture_bullet"] += 1
                elif bu.get("char") not in ("•", "●", "∙", "·", None):
                    self.counts["approx:bullet_char:%s" % bu.get("char")] += 1
        if len(kinds) > 1:
            self.counts["limit:mixed_bullets"] += 1
        bullets = kinds.most_common(1)[0][0] if kinds else "none"
        if bullets != "none":
            out["bullets"] = bullets
            self.counts["feature:bullets"] += 1
        if any(p["level"] != first["level"] for p in visible):
            self.counts["feature:paragraph_levels"] += 1

        left = l_ins
        if bullets != "none":
            left = l_ins + max(0.0, mar_l + indent)
        elif indent > 0:
            left = l_ins + mar_l
            out["indent"] = round(indent, 2)
        else:
            left = l_ins + max(0.0, mar_l + indent)
            if indent < 0 and len(lines) > 1:
                self.counts["limit:hanging_indent_no_bullet"] += 1
        margins = {"left": round(left, 2), "top": round(t_ins, 2),
                   "right": round(r_ins + mar_r, 2), "bottom": round(b_ins, 2)}
        out["margins"] = margins

        # spacing: per box; a br inside a paragraph would pick it up
        para_count = len(paragraphs)
        spc_b = self.spacing_px(ppr.get("spcBef"), box_px, ratio)
        spc_a = self.spacing_px(ppr.get("spcAft"), box_px, ratio)
        all_same_b = all(self.spacing_px(p["merged"].ppr.get("spcBef"), box_px, ratio) == spc_b
                         for p in visible)
        all_same_a = all(self.spacing_px(p["merged"].ppr.get("spcAft"), box_px, ratio) == spc_a
                         for p in visible)
        if not all_same_b or not all_same_a:
            self.counts["feature:paragraph_spacing"] += 1
        has_br = len(lines) > para_count
        if para_count <= 1:
            spc_b = spc_a = 0.0  # one paragraph: nothing between
        elif has_br and (spc_b or spc_a):
            self.counts["limit:para_spacing_with_line_breaks"] += 1
            spc_b = spc_a = 0.0
        if spc_b:
            out["space_before"] = round(spc_b, 2)
        if spc_a:
            out["space_after"] = round(spc_a, 2)

        if autofit:
            out["autofit"] = autofit
        cols = int(self.body_prop(chain, "numCol", 1) or 1)
        if cols > 1:
            out["columns"] = min(cols, 8)
            out["column_gap"] = round(float(self.body_prop(chain, "spcCol", 0)) * s, 2)
            self.counts["feature:columns"] += 1
        return out

    def spacing_px(self, el, box_px, ratio):
        if el is None or not len(el):
            return 0.0
        v = el[0]
        if tag(v) == "spcPct":
            return pct(v.get("val"), 0) * box_px * ratio
        if tag(v) == "spcPts":
            return float(v.get("val", 0)) / 100.0 * self.px_per_pt
        return 0.0

    def finish_text(self, result, x, y, w, h, framed=False):
        """Apply the wrap="none" widening and the placement set aside by convert_text."""
        obj, _autofit, _has = result
        para = obj.get("paragraph", {})
        wrap = para.pop("_wrap", "square")
        if not para:
            obj.pop("paragraph", None)
        shift = obj.pop("_shift", {}).get(obj["vertical_align"], 0.0)
        if shift:
            if not framed:
                obj["y"] = round(obj["y"] + shift, 2)
            else:
                # A frame stays where it is: the words move inside it.
                margins = obj.setdefault("paragraph", {}).setdefault("margins", {})
                if obj["vertical_align"] == "top":
                    margins["top"] = round(max(0.0, margins.get("top", 0.0) + shift), 2)
                elif obj["vertical_align"] == "bottom":
                    margins["bottom"] = round(max(0.0, margins.get("bottom", 0.0) - shift), 2)
                else:
                    key = "top" if shift > 0 else "bottom"
                    margins[key] = round(margins.get(key, 0.0) + 2 * abs(shift), 2)
        if (not framed and not _autofit and not obj.get("paragraph", {}).get("autofit")
                and not os.environ.get("DECK_CHECK_KEEP_CLIP")):
            # PowerPoint and LibreOffice let words overflow a box; SeventhSlide clips
            # them to it (reported as a finding). So what lies under stays visible, a box
            # with nothing behind its words gets the room to overflow in.
            spare = 900.0
            if obj["vertical_align"] == "top":
                obj["h"] = round(obj["h"] + spare, 2)
            elif obj["vertical_align"] == "bottom":
                obj["y"] = round(obj["y"] - spare, 2)
                obj["h"] = round(obj["h"] + spare, 2)
            else:
                obj["y"] = round(obj["y"] - spare / 2, 2)
                obj["h"] = round(obj["h"] + spare, 2)
            self.counts["approx:grew_box_so_overflow_shows"] += 1
        if wrap == "none":
            self.counts["approx:wrap_none"] += 1
            # A box that does not wrap grows from its aligned side; give it room.
            spare = max(800.0, obj["w"] * 2)
            if obj["text_align"] == "center":
                obj["x"] -= spare / 2
                obj["w"] += spare
            elif obj["text_align"] == "right":
                obj["x"] -= spare
                obj["w"] += spare
            else:
                obj["w"] += spare
        return obj

    # --- the elements -------------------------------------------------------
    def convert_tree(self, tree, ctx, matrix, output, group_fill=None, in_master=False):
        for el in tree:
            t = tag(el)
            if t == "AlternateContent":
                self.convert_alternate(el, ctx, matrix, output, group_fill, in_master)
            elif t == "sp":
                self.convert_sp(el, ctx, matrix, output, group_fill, in_master)
            elif t == "pic":
                self.convert_pic(el, ctx, matrix, output)
            elif t == "cxnSp":
                self.convert_line(el, ctx, matrix, output)
            elif t == "grpSp":
                self.convert_group(el, ctx, matrix, output, in_master)
            elif t == "graphicFrame":
                self.count_graphic(el)
            elif t not in ("nvGrpSpPr", "grpSpPr", "extLst"):
                self.counts["skip:unknown_" + t] += 1

    def convert_alternate(self, el, ctx, matrix, output, group_fill, in_master):
        for branch in el:
            if tag(branch) == "Choice":
                kids = [c for c in branch if tag(c) in ("sp", "pic", "grpSp", "cxnSp", "graphicFrame")]
                if kids:
                    self.convert_tree(kids, ctx, matrix, output, group_fill, in_master)
                    return
            elif tag(branch) == "Fallback":
                self.convert_tree(list(branch), ctx, matrix, output, group_fill, in_master)
                return

    def count_graphic(self, el):
        graphic = el.find("a:graphic/a:graphicData", NS)
        uri = graphic.get("uri", "") if graphic is not None else ""
        if "diagram" in uri:
            self.counts["skip:smartart"] += 1
        elif "chart" in uri:
            self.counts["skip:chart"] += 1
        elif "table" in uri:
            self.counts["skip:table"] += 1
        elif "ole" in uri:
            self.counts["skip:ole"] += 1
        else:
            self.counts["skip:other_graphic"] += 1

    def convert_group(self, el, ctx, matrix, output, in_master):
        xfrm = el.find("p:grpSpPr/a:xfrm", NS)
        if xfrm is None:
            self.convert_tree(list(el), ctx, matrix, output, None, in_master)
            return
        off = xfrm.find("a:off", NS)
        ext = xfrm.find("a:ext", NS)
        ch_off = xfrm.find("a:chOff", NS)
        ch_ext = xfrm.find("a:chExt", NS)
        if off is None or ext is None or ch_off is None or ch_ext is None:
            self.counts["skip:group_without_transform"] += 1
            return
        child = group_matrix(float(off.get("x")), float(off.get("y")), float(ext.get("cx")),
                             float(ext.get("cy")), float(ch_off.get("x")), float(ch_off.get("y")),
                             float(ch_ext.get("cx")), float(ch_ext.get("cy")),
                             float(xfrm.get("rot", 0)) / 60000.0, xfrm.get("flipH") == "1",
                             xfrm.get("flipV") == "1")
        fill = self.read_fill(el.find("p:grpSpPr", NS), ctx)
        self.counts["feature:group"] += 1
        self.convert_tree(list(el), ctx, mat_mul(matrix, child), output, fill, in_master)

    @staticmethod
    def hidden(el):
        for path in ("p:nvSpPr/p:cNvPr", "p:nvPicPr/p:cNvPr", "p:nvCxnSpPr/p:cNvPr"):
            nv = el.find(path, NS)
            if nv is not None:
                return nv.get("hidden") == "1", nv.get("name", "")
        return False, ""

    def convert_sp(self, sp, ctx, matrix, output, group_fill, in_master):
        hidden, name = self.hidden(sp)
        if hidden:
            self.counts["skip:hidden_shape"] += 1
            return
        ph = self.placeholder(sp)
        if in_master and ph is not None:
            return  # a master's or layout's placeholder is a prompt, never drawn
        chain = self.inherited(sp, ctx)
        xfrm = self.transform_of(chain)
        if xfrm is None:
            self.counts["skip:no_transform"] += 1
            return
        frame_emu = self.frame_of(xfrm, matrix)
        frame = self.scaled(frame_emu)
        if frame[2] <= 0.1 and frame[3] <= 0.1:
            self.counts["skip:zero_size"] += 1
            return

        sp_pr = sp.find("p:spPr", NS)
        fill = None
        for node in reversed(chain):
            fill = self.read_fill(node.find("p:spPr", NS), ctx)
            if fill is not None:
                break
        if fill is None:
            fill = self.style_fill(sp, ctx)
        line_src = sp_pr
        if ph is not None and (sp_pr is None or sp_pr.find("a:ln", NS) is None):
            for node in reversed(chain[:-1]):
                other = node.find("p:spPr/a:ln", NS)
                if other is not None:
                    line_src = node.find("p:spPr", NS)
                    break
        line = self.read_line(line_src, sp, ctx)
        effects = self.shape_effects(sp_pr, sp, ctx)
        self.count_3d(sp_pr)

        geom = None
        cust = None
        for node in reversed(chain):
            pr = node.find("p:spPr", NS)
            if pr is None:
                continue
            geom = pr.find("a:prstGeom", NS)
            cust = pr.find("a:custGeom", NS)
            if geom is not None or cust is not None:
                break
        name_prst = geom.get("prst", "rect") if geom is not None else ("cust" if cust is not None else "rect")

        text_result = self.convert_text(sp, chain, ctx, frame, name, fill)
        fill_paints = fill is not None and fill.get("k") not in ("none", "group") or (
            fill is not None and fill.get("k") == "group" and group_fill is not None
            and group_fill.get("k") != "none")

        if name_prst in LINE_GEOMS:
            self.emit_line(sp, name_prst, frame, name, line, effects, ctx, output, text_result, fill)
            return

        if cust is not None:
            self.emit_custom(sp, cust, frame, name, fill, group_fill, line, effects, ctx, output, text_result)
            return

        if name_prst == "rect" and text_result is not None:
            # A rectangle with words: one Text object, with the frame behind them.
            obj = self.finish_text(text_result, *frame[:4],
                                   framed=fill_paints or line is not None)
            fx = obj.pop("_effects")
            if fill_paints or line is not None:
                self.apply_fill(obj, fill, ctx, group_fill)
                if obj.get("fill_kind") == "none" and not obj.get("fill"):
                    obj.pop("fill_kind")
                    obj.pop("fill", None)
                if line is not None:
                    self.apply_line(obj, line)
                fx = self.frame_effects(effects, fx, True)
            else:
                # A shadow on a shape with no fill and no line falls on the words.
                fx = self.frame_effects(effects, fx, False)
            self.apply_effects(obj, fx)
            self.record_text_features(obj)
            output.append(obj)
            return

        if not fill_paints and line is None and text_result is None:
            return  # nothing to draw (an empty placeholder, an empty text box)

        if fill_paints or line is not None:
            obj = self.obj(ctx, "shape", frame, name)
            self.apply_fill(obj, fill, ctx, group_fill)
            self.apply_line(obj, line)
            self.emit_shape_kind(obj, geom if geom is not None else ET.Element("x", {"prst": "rect"}))
            self.apply_effects(obj, self.frame_effects(effects, {}, True))
            output.append(obj)

        if text_result is not None:
            obj = self.finish_text(text_result, *frame[:4])
            self.place_inscribed(obj, name_prst)
            fx = obj.pop("_effects")
            self.apply_effects(obj, fx)
            # a Text object drawn after the shape stays above it
            self.z += 1
            obj["z"] = self.z
            self.record_text_features(obj)
            output.append(obj)

    def frame_effects(self, effects, text_fx, frame_paints):
        """The effects of a box given whether its frame paints."""
        out = dict(text_fx)
        if not effects:
            return out
        if frame_paints:
            out.update(effects)
            return out
        if "shadow" in effects and "text_shadow" not in out:
            out["text_shadow"] = effects["shadow"]
        if "glow" in effects:
            self.counts["skip:glow_no_frame"] += 1
        return out

    @staticmethod
    def apply_effects(obj, fx):
        if fx:
            obj["effects"] = fx

    def record_text_features(self, obj):
        self.counts["feature:text_box"] += 1

    def place_inscribed(self, obj, prst):
        inset = TEXT_INSET.get(prst, 0.0)
        if inset:
            dx, dy = obj["w"] * inset, obj["h"] * inset
            obj["x"] += dx
            obj["y"] += dy
            obj["w"] -= 2 * dx
            obj["h"] -= 2 * dy

    def count_3d(self, sp_pr):
        if sp_pr is None:
            return
        if sp_pr.find("a:sp3d", NS) is not None:
            s3 = sp_pr.find("a:sp3d", NS)
            if s3.get("extrusionH") or s3.find("a:bevelT", NS) is not None or s3.find("a:bevelB", NS) is not None:
                self.counts["skip:3d_bevel"] += 1
        if sp_pr.find("a:scene3d", NS) is not None:
            cam = sp_pr.find("a:scene3d/a:camera", NS)
            if cam is not None and cam.get("prst") not in (None, "orthographicFront"):
                self.counts["skip:3d_camera"] += 1

    def emit_line(self, sp, name_prst, frame, name, line, effects, ctx, output, text_result, fill):
        if line is None:
            self.counts["skip:line_without_stroke"] += 1
            return
        x, y, w, h, rot, flip_h, flip_v = frame
        pts = self.line_points(name_prst, flip_h, flip_v)
        # A line has no thickness of box; the painter needs one.
        if w < 1.0:
            x -= (1.0 - w) / 2.0
            w = 1.0
        if h < 1.0:
            y -= (1.0 - h) / 2.0
            h = 1.0
        obj = self.obj(ctx, "shape", (x, y, w, h, rot, False, False), name)
        obj["shape_kind"] = "drawn"
        obj["shape_path"] = [[round(px, 5), round(py, 5)] for px, py in pts]
        if name_prst == "curvedConnector3" or name_prst.startswith("curved"):
            obj["shape_path"] = [[round(px, 5), round(py, 5), 1] for px, py in pts]
        obj["fill_kind"] = "none"
        obj["fill"] = ""
        self.apply_line(obj, line)
        self.counts["feature:line"] += 1
        self.apply_effects(obj, self.frame_effects(effects, {}, True))
        output.append(obj)
        if text_result is not None:
            self.counts["skip:text_on_line"] += 1

    def emit_custom(self, sp, cust, frame, name, fill, group_fill, line, effects, ctx, output, text_result):
        subs = self.cust_paths(cust)
        if not subs:
            self.counts["skip:custom_empty_path"] += 1
            if text_result is not None:
                obj = self.finish_text(text_result, *frame[:4])
                self.apply_effects(obj, obj.pop("_effects"))
                output.append(obj)
            return
        self.counts["feature:custom_shape"] += 1
        no_fill = fill is None or fill.get("k") == "none"
        if len(subs) > 1 and (line is None or no_fill):
            # One path, its pieces joined along a seam: holes stay holes.
            joined = []
            for pts, _closed, _filled in subs:
                joined.extend(pts)
                joined.append(pts[0])
            subs_final = [(joined, True, True)]
            self.counts["approx:joined_subpaths"] += 1
        else:
            subs_final = subs
            if len(subs) > 1:
                self.counts["approx:split_subpaths"] += 1
        drawn_any = False
        for pts, closed, filled in subs_final:
            if not filled and line is None:
                continue
            obj = self.obj(ctx, "shape", frame, name)
            obj["shape_kind"] = "drawn"
            obj["shape_path"] = [[round(px, 4), round(py, 4)] for px, py in pts]
            if closed:
                obj["shape_closed"] = True
            self.apply_fill(obj, fill if filled else {"k": "none"}, ctx, group_fill)
            self.apply_line(obj, line)
            self.apply_effects(obj, self.frame_effects(effects, {}, True))
            output.append(obj)
            drawn_any = True
        if text_result is not None:
            obj = self.finish_text(text_result, *frame[:4])
            self.apply_effects(obj, obj.pop("_effects"))
            self.z += 1
            obj["z"] = self.z
            output.append(obj)
        if not drawn_any:
            self.counts["skip:custom_nothing_drawn"] += 1

    def convert_line(self, el, ctx, matrix, output):
        hidden, name = self.hidden(el)
        if hidden:
            self.counts["skip:hidden_shape"] += 1
            return
        xfrm = el.find("p:spPr/a:xfrm", NS)
        if xfrm is None:
            self.counts["skip:no_transform"] += 1
            return
        frame = self.scaled(self.frame_of(xfrm, matrix))
        sp_pr = el.find("p:spPr", NS)
        geom = sp_pr.find("a:prstGeom", NS)
        prst = geom.get("prst", "line") if geom is not None else "line"
        if prst == "cust":
            pass
        line = self.read_line(sp_pr, el, ctx)
        effects = self.shape_effects(sp_pr, el, ctx)
        self.emit_line(el, prst if prst in LINE_GEOMS else "line", frame, name, line, effects, ctx,
                       output, None, None)

    def convert_pic(self, pic, ctx, matrix, output):
        hidden, name = self.hidden(pic)
        if hidden:
            self.counts["skip:hidden_shape"] += 1
            return
        chain = [pic]
        if self.placeholder(pic) is not None:
            chain = self.inherited(pic, ctx)
        xfrm = self.transform_of(chain)
        if xfrm is None:
            self.counts["skip:no_transform"] += 1
            return
        frame = self.scaled(self.frame_of(xfrm, matrix))
        blip_fill = pic.find("p:blipFill", NS)
        blip = blip_fill.find("a:blip", NS) if blip_fill is not None else None
        rid = self.blip_rid(blip)
        nv = pic.find("p:nvPicPr/p:nvPr", NS)
        video_el = nv.find("a:videoFile", NS) if nv is not None else None
        audio_el = nv.find("a:audioFile", NS) if nv is not None else None
        media = None
        if nv is not None:
            media = nv.find("p:extLst/p:ext/p14:media", NS)
        if audio_el is not None and video_el is None:
            self.counts["limit:audio_icon_as_picture"] += 1
        file = self.extract(ctx, rid)
        if video_el is not None or (media is not None and audio_el is None):
            self.convert_video(pic, ctx, frame, name, file, video_el, media, output)
            return
        if not file:
            self.counts["skip:picture_no_media"] += 1
            return
        sp_pr = pic.find("p:spPr", NS)
        crop = self.read_crop(blip_fill.find("a:srcRect", NS), blip_fill)
        geom = sp_pr.find("a:prstGeom", NS) if sp_pr is not None else None
        prst = geom.get("prst", "rect") if geom is not None else "rect"
        self.count_3d(sp_pr)
        effects = self.shape_effects(sp_pr, pic, ctx)
        alpha, recolor, tint = self.read_blip(blip, ctx)
        cust = sp_pr.find("a:custGeom", NS) if sp_pr is not None else None
        if cust is not None:
            # A picture cut to a drawn outline: the outline, filled with the picture.
            before = len(output)
            self.emit_custom(pic, cust, frame, name, {"k": "blip", "el": blip_fill}, None,
                             self.read_line(sp_pr, pic, ctx), effects, ctx, output, None)
            if alpha is not None:
                for added in output[before:]:
                    added["opacity"] = round(alpha, 3)
            self.counts["feature:picture_in_shape"] += 1
            if recolor:
                self.counts["limit:picture_shape_no_recolor"] += 1
            return
        if prst in ("rect", "snip1Rect") or geom is None:
            obj = self.obj(ctx, "image", frame, name)
            obj["filename"] = file
            obj["object_fit"] = "fill"
            picture = {}
            if crop:
                picture["crop"] = crop
                self.counts["feature:picture_crop"] += 1
            if recolor:
                picture["recolor"] = recolor
                if tint:
                    picture["tint"] = tint
                self.counts["feature:picture_recolor"] += 1
            if alpha is not None:
                picture["transparency"] = round(1 - alpha, 4)
                self.counts["feature:picture_transparency"] += 1
            if picture:
                obj["picture"] = picture
            self.apply_effects(obj, effects)
            output.append(obj)
            self.counts["feature:picture"] += 1
        else:
            obj = self.obj(ctx, "shape", frame, name)
            self.emit_shape_kind(obj, geom)
            fill = {"file": file, "fit": "fill"}
            if crop:
                fill["crop"] = crop
            obj["fill_kind"] = "picture"
            obj["fill"] = ""
            obj["fill_picture"] = fill
            obj["stroke"] = ""
            obj["stroke_width"] = 0
            if alpha is not None:
                obj["opacity"] = round(alpha, 3)
            self.apply_effects(obj, effects)
            output.append(obj)
            self.counts["feature:picture_in_shape"] += 1
            self.counts["limit:picture_shape_no_recolor" if (recolor or alpha) else "feature:ok"] += 1
        line = self.read_line(sp_pr, pic, ctx)
        if line is not None:
            # A picture has no outline of its own: a rectangle over it.
            over = self.obj(ctx, "shape", frame, name + " (outline)")
            over["shape_kind"] = "rectangle" if prst == "rect" or geom is None else obj.get("shape_kind", "rectangle")
            over["fill_kind"] = "none"
            over["fill"] = ""
            self.apply_line(over, line)
            output.append(over)
            self.counts["approx:picture_outline_as_shape"] += 1

    def read_blip(self, blip, ctx):
        alpha = None
        recolor = None
        tint = None
        if blip is None:
            return alpha, recolor, tint
        for el in blip:
            t = tag(el)
            if t == "alphaModFix":
                alpha = pct(el.get("amt"))
            elif t == "grayscl":
                recolor = "grayscale"
            elif t == "duotone":
                colors = [first_color_of(c, ctx) for c in el]
                colors = [c for c in colors if c]
                if colors:
                    recolor = "tint"
                    tint = color_css((*colors[-1][:3], 1.0))
                else:
                    self.counts["approx:duotone_no_color"] += 1
            elif t in ("biLevel", "clrChange", "clrRepl", "lum", "alphaBiLevel", "alphaCeiling",
                       "alphaFloor", "alphaInv", "alphaMod", "alphaRepl", "blur", "fillOverlay", "hsl",
                       "tint"):
                self.counts["limit:picture_effect_" + t] += 1
        return alpha, recolor, tint

    def convert_video(self, pic, ctx, frame, name, poster, video_el, media, output):
        rid = None
        if media is not None:
            rid = media.get(RID + "embed")
        if not rid and video_el is not None:
            rid = video_el.get(RID + "link") or video_el.get(RID + "embed")
        file = self.extract(ctx, rid, video=True) if rid else None
        if not file:
            self.counts["skip:video_no_media"] += 1
            if poster:
                # The poster alone, so the slide still shows something.
                obj = self.obj(ctx, "image", frame, name)
                obj["filename"] = poster
                obj["object_fit"] = "fill"
                output.append(obj)
            return
        obj = self.obj(ctx, "video", frame, name)
        obj["filename"] = file
        obj["object_fit"] = "fill"
        video = {"start": "on_click", "loop": False, "mute": False}
        if poster:
            video["poster"] = poster
        else:
            self.counts["limit:video_without_poster"] += 1
        obj["video"] = video
        output.append(obj)
        self.counts["feature:video"] += 1

    # --- backgrounds --------------------------------------------------------
    def background(self, ctx, output):
        """The slide's background (slide, then layout, then master)."""
        for root, part in ((ctx.slide, ctx.slide_part), (ctx.layout, ctx.layout_part),
                           (ctx.master, ctx.master_part)):
            bg = root.find("p:cSld/p:bg", NS)
            if bg is None:
                continue
            ctx.part = part
            pr = bg.find("p:bgPr", NS)
            ref = bg.find("p:bgRef", NS)
            fill = None
            if pr is not None:
                fill = self.read_fill(pr, ctx)
            elif ref is not None:
                idx = int(ref.get("idx", 0))
                ph = first_color(ref, ctx)
                lists = ctx.theme.bg_fills if idx >= 1001 else ctx.theme.fills
                pos = idx - 1001 if idx >= 1001 else idx - 1
                if 0 <= pos < len(lists):
                    holder = ET.Element("h")
                    holder.append(lists[pos])
                    fill = self.read_fill(holder, ctx, ph)
            if fill is not None:
                return self.apply_background(fill, ctx, output, part)
        return {"kind": "color", "color": "#ffffff"}

    def apply_background(self, fill, ctx, output, part):
        if fill["k"] == "solid":
            self.counts["feature:background_color"] += 1
            return {"kind": "color", "color": color_css((*fill["c"][:3], 1.0))}
        if fill["k"] == "grad":
            self.counts["feature:background_gradient"] += 1
            # A slide background has no gradient: a full-slide shape behind everything.
            obj = self.obj(ctx, "shape", (0, 0, CANVAS_W, self.canvas_h, 0, False, False), "background")
            obj["shape_kind"] = "rectangle"
            obj["fill_kind"] = "gradient"
            obj["fill"] = fill["first"]
            obj["gradient"] = fill["g"]
            obj["stroke"] = ""
            obj["stroke_width"] = 0
            obj["z"] = -10000
            output.append(obj)
            return {"kind": "color", "color": fill["first"][:7] if len(fill["first"]) == 7 else "#000000"}
        if fill["k"] == "blip":
            blip = fill["el"].find("a:blip", NS)
            saved = ctx.part
            ctx.part = part
            file = self.extract(ctx, self.blip_rid(blip))
            ctx.part = saved
            if not file:
                self.counts["skip:background_picture_no_media"] += 1
                return {"kind": "color", "color": "#ffffff"}
            crop = self.read_crop(fill["el"].find("a:srcRect", NS), fill["el"])
            alpha = blip.find("a:alphaModFix", NS)
            tile = fill["el"].find("a:tile", NS) is not None
            aspect_ok = self.image_aspect_ok(file)
            if crop or alpha is not None or tile or not aspect_ok:
                self.counts["approx:background_picture_as_object"] += 1
                obj = self.obj(ctx, "image", (0, 0, CANVAS_W, self.canvas_h, 0, False, False), "background")
                obj["filename"] = file
                obj["object_fit"] = "cover" if tile else "fill"
                picture = {}
                if crop:
                    picture["crop"] = crop
                if alpha is not None:
                    picture["transparency"] = round(1 - pct(alpha.get("amt")), 4)
                if picture:
                    obj["picture"] = picture
                obj["z"] = -9999
                output.append(obj)
                return {"kind": "color", "color": "#ffffff"}
            self.counts["feature:background_picture"] += 1
            return {"kind": "image", "image": file}
        self.counts["skip:background_other"] += 1
        return {"kind": "color", "color": "#ffffff"}

    def image_aspect_ok(self, file):
        try:
            with Image.open(self.images / file) as im:
                ratio = im.width / im.height
        except OSError:
            return True
        return abs(ratio - CANVAS_W / self.canvas_h) < 0.015

    # --- a whole slide ------------------------------------------------------
    def convert_slide(self, slide_part):
        ctx = SlideCtx(self, slide_part)
        self.z = 0
        output = []
        background = self.background(ctx, output)
        show_master = ctx.slide.get("showMasterSp", "1") != "0"
        if show_master:
            if ctx.layout.get("showMasterSp", "1") != "0":
                tree = ctx.master.find("p:cSld/p:spTree", NS)
                if tree is not None:
                    ctx.part = ctx.master_part
                    self.convert_tree(list(tree), ctx, mat_identity(), output, None, True)
            tree = ctx.layout.find("p:cSld/p:spTree", NS)
            if tree is not None:
                ctx.part = ctx.layout_part
                self.convert_tree(list(tree), ctx, mat_identity(), output, None, True)
        tree = ctx.slide.find("p:cSld/p:spTree", NS)
        ctx.part = slide_part
        self.convert_tree(list(tree), ctx, mat_identity(), output)
        self.tally_slide(output)
        slide = {"id": "s%d" % self.next_slide_id(), "objects": output}
        if background:
            slide["background"] = background
        return slide

    def tally_slide(self, objects):
        """What the finished objects show, for the per-feature sheets."""
        for obj in objects:
            fx = obj.get("effects", {})
            if obj.get("rotation"):
                self.counts["feature:rotation"] += 1
            if obj.get("flip_h") or obj.get("flip_v"):
                self.counts["feature:flip"] += 1
            if obj.get("opacity", 1.0) < 0.999 or len(str(obj.get("fill", ""))) == 9:
                self.counts["feature:transparency"] += 1
            if "shadow" in fx:
                self.counts["feature:shape_shadow"] += 1
            if "glow" in fx:
                self.counts["feature:glow"] += 1
            if fx.get("soft_edge"):
                self.counts["feature:soft_edge"] += 1
            if obj.get("shape_kind") == "drawn" and obj.get("shape_closed"):
                self.counts["feature:custom_drawn"] += 1
            if obj.get("picture", {}).get("transparency"):
                self.counts["feature:picture_transparency_used"] += 1

    def next_slide_id(self):
        self.next_id += 1
        return self.next_id

    def visible_slides(self):
        out = []
        for part in self.slides:
            root = self.pkg.xml(part)
            if root.get("show", "1") == "0":
                self.counts["hidden_slides"] += 1
                continue
            out.append(part)
        return out

    def convert(self, name):
        slides = []
        for number, part in enumerate(self.visible_slides(), 1):
            self.counts.slide = number
            slides.append(self.convert_slide(part))
        doc = {"schema": SCHEMA, "schema_version": 3, "name": name, "line_control": False,
               "next_id": self.next_id + 10, "cues": [], "fields": {},
               "decks": [{"output": "out-main",
                          "canvas": {"width": CANVAS_W, "height": self.canvas_h},
                          "slides": slides,
                          "line_to_slide": list(range(len(slides)))}]}
        return doc


def first_color_of(el, ctx):
    t = tag(el)
    if t in ("srgbClr", "schemeClr", "sysClr", "prstClr", "scrgbClr", "hslClr"):
        return resolve_color(el, ctx)
    return None


def shipped_static(family, bold, italic):
    """The static face SeventhSlide ships for ``family`` (third_party/fonts), or None."""
    folder = pathlib.Path(__file__).resolve().parent.parent / "third_party" / "fonts" / family
    style = ("Bold" if bold else "") + ("Italic" if italic else "") or "Regular"
    for path in sorted(folder.glob("*-%s.[ot]tf" % style)):
        return path
    return None


def collect_fonts(families, out_root):
    """The fonts the decks name, as this machine resolves them, for the harness to load."""
    fonts_dir = out_root / "fonts"
    fonts_dir.mkdir(parents=True, exist_ok=True)
    manifest = []
    variable_families = set()
    seen_file = {}
    for family in sorted(families):
        faces = []
        for bold in (False, True):
            for italic in (False, True):
                path, names, _index = fc_query(family, bold, italic)
                if not path or not pathlib.Path(path).is_file():
                    continue
                if "[" in pathlib.Path(path).name:
                    # A variable file: the static face SeventhSlide ships, if it ships it.
                    static = shipped_static(names.split(",")[0].strip(), bold, italic)
                    if static:
                        path = str(static)
                    else:
                        variable_families.add(family)
                suffix = pathlib.Path(path).suffix
                if path not in seen_file:
                    target = "font%04d%s" % (len(seen_file), suffix)
                    shutil.copy2(path, fonts_dir / target)
                    seen_file[path] = target
                sub = ("Bold " if bold else "") + ("Italic" if italic else "")
                sub = sub.strip() or "Regular"
                faces.append({"subfamily": sub, "file": "fonts/" + seen_file[path],
                              "variable": "[" in pathlib.Path(path).name})
        if faces:
            manifest.append({"family": family, "faces": faces})
    (fonts_dir / "fonts.json").write_text(json.dumps(manifest, indent=0), encoding="utf-8")
    if variable_families:
        print("variable files used as they are:", ", ".join(sorted(variable_families)))
    return manifest


def convert_all(args):
    root = pathlib.Path(args.dir)
    decks_root = root / "decks"
    decks_root.mkdir(parents=True, exist_ok=True)
    families = set()
    for name in DECKS:
        if args.only and not any(o.lower() in name.lower() for o in args.only):
            continue
        pptx = SOURCE / "PowerPoint" / (name + ".pptx")
        out = decks_root / slug(name)
        print(name, "...", flush=True)
        conv = Converter(pptx, out, media_videos=not args.no_videos)
        doc = conv.convert(name)
        (out / "document.json").write_text(json.dumps(doc, ensure_ascii=False), encoding="utf-8")
        conv.counts["slides_visible"] = len(doc["decks"][0]["slides"])
        (out / "counts.json").write_text(json.dumps(conv.counts, indent=1, sort_keys=True), encoding="utf-8")
        (out / "features.json").write_text(json.dumps(
            {k: sorted(v) for k, v in conv.counts.on_slides.items()}, indent=0, sort_keys=True), encoding="utf-8")
        families |= conv.fonts_used
        print("   ", conv.counts["slides_visible"], "visible slides,",
              sum(v for k, v in conv.counts.items() if k.startswith("skip:")), "skipped elements")
    manifest = collect_fonts(families, decks_root)
    print("fonts:", len(manifest), "families;", "substituted locally:",
          ", ".join(sorted(FONT_SUBSTITUTED)) or "none")


# ---------------------------------------------------------------- scoring
def ref_paths(ref_dir):
    def num(p):
        return int(re.search(r"(\d+)", p.stem).group(1))
    return sorted(ref_dir.glob("s-*.png"), key=num)


def score_pair(ref, ours):
    ref = ref.convert("RGB")
    ours = ours.convert("RGB").resize(ref.size, Image.LANCZOS)
    blur = ImageFilter.GaussianBlur(1.0)
    diff = ImageChops.difference(ref.filter(blur), ours.filter(blur))
    return sum(ImageStat.Stat(diff).mean) / 3.0


def contact_sheet(pairs, path, deck, cell_w=640):
    """Reference left, ours right; ``pairs`` is a list of (label, ref, ours)."""
    label_h = 22
    rows = []
    for _lab, ref, _ours in pairs:
        rows.append(round(cell_w * ref.height / ref.width))
    width = cell_w * 2 + 10
    height = sum(rows) + (label_h + 6) * len(pairs) + 26
    sheet = Image.new("RGB", (width, height), (40, 40, 40))
    draw = ImageDraw.Draw(sheet)
    draw.text((6, 5), deck + "  (reference left, SeventhSlide right)", fill=(255, 255, 255))
    y = 24
    for (lab, ref, ours), rh in zip(pairs, rows):
        draw.text((6, y + 3), lab, fill=(255, 220, 120))
        y += label_h
        sheet.paste(ref.convert("RGB").resize((cell_w, rh), Image.LANCZOS), (0, y))
        sheet.paste(ours.convert("RGB").resize((cell_w, rh), Image.LANCZOS), (cell_w + 10, y))
        y += rh + 6
    sheet.save(path)


def score_all(args):
    root = pathlib.Path(args.dir)
    sheets = root / "sheets"
    sheets.mkdir(parents=True, exist_ok=True)
    overall = []
    for name in DECKS:
        if args.only and not any(o.lower() in name.lower() for o in args.only):
            continue
        s = slug(name)
        out_dir = root / "decks" / s / "out"
        ref_dir = SOURCE / "reference" / name
        refs = ref_paths(ref_dir)
        ours = sorted(out_dir.glob("s-*.png"))
        counts = {}
        counts_path = root / "decks" / s / "counts.json"
        if counts_path.exists():
            counts = json.loads(counts_path.read_text(encoding="utf-8"))
        if len(refs) != len(ours):
            print("%s: %d references, %d renders" % (name, len(refs), len(ours)))
        scores = []
        for i, (r, o) in enumerate(zip(refs, ours), 1):
            with Image.open(r) as a, Image.open(o) as b:
                scores.append((score_pair(a, b), i, r, o))
        if not scores:
            continue
        mean = sum(x[0] for x in scores) / len(scores)
        worst = sorted(scores, reverse=True)
        lines = ["%s: %d slides, mean %.2f, worst %.2f (slide %d)" % (
            name, len(scores), mean, worst[0][0], worst[0][1])]
        lines.append("slide  score  (reference file)")
        for sc, i, r, _o in scores:
            lines.append("%5d  %5.2f  %s" % (i, sc, r.name))
        lines.append("")
        lines.append("counts: " + json.dumps(counts, sort_keys=True))
        (sheets / (s + ".txt")).write_text("\n".join(lines) + "\n", encoding="utf-8")
        (root / "decks" / s / "scores.json").write_text(json.dumps({str(i): sc for sc, i, _r, _o in scores}), encoding="utf-8")
        overall.append(lines[0])
        print(lines[0])
        n = min(args.worst, len(worst))
        for page in range(0, n, args.per_sheet):
            pairs = []
            for sc, i, r, o in worst[page:page + args.per_sheet]:
                a = Image.open(r)
                b = Image.open(o)
                pairs.append(("slide %d  score %.2f" % (i, sc), a, b))
            contact_sheet(pairs, sheets / ("%s-worst-%d.png" % (s, page // args.per_sheet + 1)), name)
    (sheets / "ALL.txt").write_text("\n".join(overall) + "\n", encoding="utf-8")


FEATURES = [
    ("text boxes", "feature:text_box"), ("pictures", "feature:picture"),
    ("backgrounds (colour)", "feature:background_color"),
    ("backgrounds (picture)", "feature:background_picture"),
    ("backgrounds (picture as object)", "approx:background_picture_as_object"),
    ("backgrounds (gradient)", "feature:background_gradient"),
    ("line spacing", "feature:line_spacing"), ("rotation", "feature:rotation"),
    ("transparency", "feature:transparency"), ("text outline", "feature:text_outline"),
    ("letter spacing", "feature:letter_spacing"), ("shape shadow", "feature:shape_shadow"),
    ("custom-drawn shapes", "feature:custom_drawn"), ("picture crop", "feature:picture_crop"),
    ("groups", "feature:group"), ("picture in shape", "feature:picture_in_shape"),
    ("picture fill", "feature:picture_fill"), ("text shadow", "feature:text_shadow"),
    ("bullets", "feature:bullets"), ("soft edge", "feature:soft_edge"), ("flip", "feature:flip"),
    ("glow (shape)", "feature:glow"), ("gradient fill", "feature:gradient"),
    ("autofit", "feature:autofit"), ("video", "feature:video"), ("line / connector", "feature:line"),
    ("cloud callout", "feature:shape:cloud"), ("ellipse", "feature:shape:ellipse"),
]


def feature_sheets(args):
    """For each feature, a few slides that use it, reference beside ours."""
    root = pathlib.Path(args.dir)
    sheets = root / "sheets"
    sheets.mkdir(parents=True, exist_ok=True)
    index = collections.defaultdict(list)
    for name in DECKS:
        deck = root / "decks" / slug(name)
        try:
            features = json.loads((deck / "features.json").read_text(encoding="utf-8"))
            scores = json.loads((deck / "scores.json").read_text(encoding="utf-8"))
        except OSError:
            continue
        refs = ref_paths(SOURCE / "reference" / name)
        for key, slides in features.items():
            for n in slides:
                if str(n) in scores and n <= len(refs):
                    index[key].append((scores[str(n)], name, n, refs[n - 1],
                                       deck / "out" / ("s-%03d.png" % n)))
    rows, summary = [], []
    for label, key in FEATURES:
        found = sorted(index.get(key, []), key=lambda e: e[0])
        summary.append("%-34s %4d slides%s" % (label, len(found),
                       "; best %.1f median %.1f worst %.1f" % (found[0][0], found[len(found) // 2][0], found[-1][0]) if found else ""))
        if not found:
            continue
        # Two different decks if there are two: the best, and the middle, and the worst.
        picks = [found[0], found[len(found) // 2], found[-1]]
        rows.append((label, picks))
    per_sheet = args.per_sheet
    cell = args.cell
    for page in range(0, len(rows), per_sheet):
        width = 3 * (2 * cell + 8) + 20
        heights = []
        for _label, picks in rows[page:page + per_sheet]:
            with Image.open(picks[0][3]) as im:
                heights.append(round(cell * im.height / im.width))
        sheet = Image.new("RGB", (width, sum(heights) + 26 * len(heights) + 10), (40, 40, 40))
        draw = ImageDraw.Draw(sheet)
        y = 4
        for (label, picks), rh in zip(rows[page:page + per_sheet], heights):
            draw.text((6, y + 3), label, fill=(120, 220, 255))
            x = 150
            for sc, name, n, _ref, _ours in picks:
                draw.text((x, y + 3), "%s s%d (%.1f)" % (name.strip()[:30], n, sc), fill=(255, 220, 120))
                x += 2 * cell + 8
            y += 22
            x = 4
            for _sc, _name, _n, ref, ours in picks:
                if ours.exists():
                    with Image.open(ref) as a, Image.open(ours) as b:
                        sheet.paste(a.convert("RGB").resize((cell, rh), Image.LANCZOS), (x, y))
                        sheet.paste(b.convert("RGB").resize((cell, rh), Image.LANCZOS), (x + cell + 2, y))
                x += 2 * cell + 8
            y += rh + 4
        sheet.save(sheets / ("features-%d.png" % (page // per_sheet + 1)))
    (sheets / "FEATURES.txt").write_text("\n".join(summary) + "\n", encoding="utf-8")
    print("\n".join(summary))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--dir", default="/tmp/ss_deck", help="generated data (default /tmp/ss_deck)")
    sub = parser.add_subparsers(dest="cmd", required=True)
    conv = sub.add_parser("convert", help="convert the decks")
    conv.add_argument("--only", nargs="*", help="only decks whose name contains one of these")
    conv.add_argument("--no-videos", action="store_true", help="do not copy video files")
    conv.set_defaults(func=convert_all)
    score = sub.add_parser("score", help="score the renders and make contact sheets")
    score.add_argument("--only", nargs="*")
    score.add_argument("--worst", type=int, default=12, help="slides on the sheets, per deck")
    score.add_argument("--per-sheet", type=int, default=6, help="slides per sheet")
    score.set_defaults(func=score_all)
    feat = sub.add_parser("features", help="sheets of slides that use each feature")
    feat.add_argument("--per-sheet", type=int, default=9, help="features per sheet")
    feat.add_argument("--cell", type=int, default=300, help="width of each picture")
    feat.set_defaults(func=feature_sheets)
    args = parser.parse_args()
    args.func(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
