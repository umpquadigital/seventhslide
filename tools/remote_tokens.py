#!/usr/bin/env python3
"""Check that the web remote's look comes from Style.qml.

The remote (src/remote/web) cannot read Style.qml, so css/tokens.css copies
the tokens it uses: one custom property per Style token, the name in kebab
case (``--panel-row-hover`` for ``panelRowHover``), the same value. This
fails when

  - a token names nothing in Style.qml, or has another value there;
  - any other stylesheet of the remote writes a colour itself (#hex, rgb(),
    hsl()), rather than through a token;
  - the page's theme colour (index.html, manifest.webmanifest) is not
    Style's pageBg.

Tokens the remote needs that the app has no use for (a touch target, the
phone's safe areas) are ``--remote-...`` and may not be colours.

    python3 tools/remote_tokens.py
"""
import json
import pathlib
import re
import sys

TOP = pathlib.Path(__file__).resolve().parent.parent
STYLE = TOP / "src" / "ui" / "Style.qml"
WEB = TOP / "src" / "remote" / "web"
TOKENS = WEB / "css" / "tokens.css"

PROPERTY_RE = re.compile(
    r'^\s*readonly property (color|int|real) (\w+):\s*(.+?)\s*(?://.*)?$', re.M)
TOKEN_RE = re.compile(r'^\s*--([\w-]+)\s*:\s*([^;]+);', re.M)
COLOUR_LITERAL_RE = re.compile(r'#[0-9a-fA-F]{3,8}\b|\b(?:rgba?|hsla?)\s*\(')


def number(expr: str) -> float:
    """A Style number: a literal, or a fraction such as ``71 / 255``."""
    if not re.fullmatch(r'[\d.\s/*+-]+', expr):
        raise ValueError(expr)
    return float(eval(expr, {"__builtins__": {}}))  # digits and operators only


def colour(text: str):
    """(r, g, b, a) of a colour as Style.qml or CSS writes it, a in 0..1;
    None for something that is not a colour."""
    text = text.strip().strip('"')
    m = re.fullmatch(r'#([0-9a-fA-F]+)', text)
    if m:
        h = m.group(1)
        if len(h) == 3:
            return tuple(int(c * 2, 16) for c in h) + (1.0,)
        if len(h) == 6:
            return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4)) + (1.0,)
        if len(h) == 8:
            return None  # ambiguous: see the callers
        return None
    m = re.fullmatch(r'(?:Qt\.rgba|rgba?)\((.*)\)', text)
    if m:
        parts = [p.strip() for p in m.group(1).split(',')]
        if len(parts) not in (3, 4):
            return None
        if text.startswith('Qt.'):
            r, g, b = (round(number(p) * 255) for p in parts[:3])
        else:
            r, g, b = (round(number(p)) for p in parts[:3])
        a = number(parts[3]) if len(parts) == 4 else 1.0
        return (r, g, b, a)
    return None


def qml_colour(text: str):
    """A Style.qml colour; #aarrggbb puts alpha first, as QML reads it."""
    t = text.strip().strip('"')
    if re.fullmatch(r'#[0-9a-fA-F]{8}', t):
        h = t[1:]
        return (int(h[2:4], 16), int(h[4:6], 16), int(h[6:8], 16), int(h[0:2], 16) / 255)
    return colour(t)


def css_colour(text: str):
    """A CSS colour; #rrggbbaa puts alpha last, as CSS reads it."""
    t = text.strip()
    if re.fullmatch(r'#[0-9a-fA-F]{8}', t):
        h = t[1:]
        return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), int(h[6:8], 16) / 255)
    return colour(t)


def style_tokens() -> dict:
    """Style.qml's colours and numbers, aliases resolved."""
    raw = {name: (kind, value) for kind, name, value in PROPERTY_RE.findall(STYLE.read_text(encoding="utf-8"))}
    out: dict = {}

    def resolve(name, depth=0):
        if name in out:
            return out[name]
        kind, value = raw[name]
        alias = re.fullmatch(r'(?:style\.)?(\w+)', value)
        if alias and alias.group(1) in raw and depth < 10:
            out[name] = resolve(alias.group(1), depth + 1)
        elif kind == "color":
            out[name] = ("color", qml_colour(value))
        else:
            try:
                out[name] = ("number", number(value))
            except ValueError:
                out[name] = (kind, None)  # computed; not a token the remote can copy
        return out[name]

    for name in raw:
        resolve(name)
    return out


def camel(kebab: str) -> str:
    head, *rest = kebab.split('-')
    return head + ''.join(word[:1].upper() + word[1:] for word in rest)


def same_colour(a, b) -> bool:
    return a is not None and b is not None and a[:3] == b[:3] and abs(a[3] - b[3]) < 0.006


def check() -> list[str]:
    problems: list[str] = []
    style = style_tokens()
    if not TOKENS.exists():
        return [f"{TOKENS.relative_to(TOP)} is missing"]

    for name, value in TOKEN_RE.findall(TOKENS.read_text(encoding="utf-8")):
        value = value.strip()
        where = f"tokens.css --{name}"
        if name.startswith('remote-'):
            if COLOUR_LITERAL_RE.search(value):
                problems.append(f"{where}: the remote's own tokens may not be colours")
            continue
        key = camel(name)
        if key not in style:
            problems.append(f"{where}: Style.qml has no {key}")
            continue
        kind, want = style[key]
        if kind == "color":
            got = css_colour(value)
            if got is None:
                problems.append(f"{where}: {value!r} is not a colour; Style's {key} is one")
            elif not same_colour(got, want):
                problems.append(f"{where}: {value} is not Style's {key} {want}")
        elif kind == "number":
            m = re.fullmatch(r'(-?[\d.]+)(px)?', value)
            if not m or abs(float(m.group(1)) - want) > 1e-6:
                problems.append(f"{where}: {value} is not Style's {key} ({want:g})")
        else:
            problems.append(f"{where}: Style's {key} is computed, so it cannot be copied")

    sheets = sorted((WEB / "css").glob("*.css")) + sorted(WEB.glob("*.css"))
    for sheet in sheets:
        if sheet == TOKENS:
            continue
        for number_, line in enumerate(sheet.read_text(encoding="utf-8").split("\n"), 1):
            code = re.sub(r'/\*.*?\*/', '', line)
            if COLOUR_LITERAL_RE.search(code):
                problems.append(f"{sheet.relative_to(TOP)}:{number_}: a colour written out; "
                                "use a token from tokens.css")

    page_bg = style["pageBg"][1]
    theme = re.search(r'<meta name="theme-color" content="([^"]+)"', (WEB / "index.html").read_text(encoding="utf-8"))
    if not theme or not same_colour(css_colour(theme.group(1)), page_bg):
        problems.append("index.html: theme-color is not Style's pageBg")
    manifest = json.loads((WEB / "manifest.webmanifest").read_text(encoding="utf-8"))
    for key in ("theme_color", "background_color"):
        if not same_colour(css_colour(manifest.get(key, "")), page_bg):
            problems.append(f"manifest.webmanifest: {key} is not Style's pageBg")
    return problems


def main() -> int:
    problems = check()
    for p in problems:
        print(p, file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
