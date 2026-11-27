#!/usr/bin/env python3
"""Fetch the Bibles SeventhSlide ships (third_party/bibles), each an OSIS file
named for the abbreviation a library gives it ("KJV.xml").

The King James and American Standard versions are the OSIS files of
github.com/bzerangue/osis-bibles, at the commit below, as served. The Berean
Standard Bible has no OSIS edition: its publisher's plain text (a verse a line,
"Genesis 1:1<TAB>In the beginning ...") is written out as OSIS, the words as
they are, a verse with none left out as the reader leaves it out. Each is in
the public domain; SOURCES.txt says where each came from, when, and the
SHA-256 of what was downloaded.

A Bible already there is left alone unless --refresh is given: a library takes
its copy once, and a Bible is only replaced on purpose.

Usage: tools/fetch_bundled_bibles.py [--refresh] [ABBREVIATION ...]   (default: every one)
"""

import argparse
import datetime
import hashlib
import sys
import urllib.request
from pathlib import Path
from xml.sax.saxutils import escape

OUT = Path(__file__).resolve().parent.parent / "third_party" / "bibles"

OSIS_BIBLES_COMMIT = "455e1913afae458445b49102d3e0375158dfba03"
OSIS_BIBLES = f"https://raw.githubusercontent.com/bzerangue/osis-bibles/{OSIS_BIBLES_COMMIT}"

# Abbreviation: (title, URL, how it is made).
BIBLES = {
    "KJV": ("King James Version", f"{OSIS_BIBLES}/en/kjv.xml", "osis"),
    "BSB": ("Berean Standard Bible", "https://bereanbible.com/bsb.txt", "bsb_text"),
    "ASV": ("American Standard Version", f"{OSIS_BIBLES}/en/asv.xml", "osis"),
}

# The BSB text's book names, in its order, as OSIS book ids.
BSB_BOOKS = {
    "Genesis": "Gen", "Exodus": "Exod", "Leviticus": "Lev", "Numbers": "Num",
    "Deuteronomy": "Deut", "Joshua": "Josh", "Judges": "Judg", "Ruth": "Ruth",
    "1 Samuel": "1Sam", "2 Samuel": "2Sam", "1 Kings": "1Kgs", "2 Kings": "2Kgs",
    "1 Chronicles": "1Chr", "2 Chronicles": "2Chr", "Ezra": "Ezra", "Nehemiah": "Neh",
    "Esther": "Esth", "Job": "Job", "Psalm": "Ps", "Proverbs": "Prov",
    "Ecclesiastes": "Eccl", "Song of Solomon": "Song", "Isaiah": "Isa",
    "Jeremiah": "Jer", "Lamentations": "Lam", "Ezekiel": "Ezek", "Daniel": "Dan",
    "Hosea": "Hos", "Joel": "Joel", "Amos": "Amos", "Obadiah": "Obad",
    "Jonah": "Jonah", "Micah": "Mic", "Nahum": "Nah", "Habakkuk": "Hab",
    "Zephaniah": "Zeph", "Haggai": "Hag", "Zechariah": "Zech", "Malachi": "Mal",
    "Matthew": "Matt", "Mark": "Mark", "Luke": "Luke", "John": "John", "Acts": "Acts",
    "Romans": "Rom", "1 Corinthians": "1Cor", "2 Corinthians": "2Cor",
    "Galatians": "Gal", "Ephesians": "Eph", "Philippians": "Phil",
    "Colossians": "Col", "1 Thessalonians": "1Thess", "2 Thessalonians": "2Thess",
    "1 Timothy": "1Tim", "2 Timothy": "2Tim", "Titus": "Titus", "Philemon": "Phlm",
    "Hebrews": "Heb", "James": "Jas", "1 Peter": "1Pet", "2 Peter": "2Pet",
    "1 John": "1John", "2 John": "2John", "3 John": "3John", "Jude": "Jude",
    "Revelation": "Rev",
}


def download(url: str) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": "fetch_bundled_bibles"})
    with urllib.request.urlopen(request, timeout=120) as response:
        return response.read()


def bsb_text_to_osis(data: bytes, title: str) -> bytes:
    """The BSB's text file as OSIS: its two header lines are the rights, then a
    "Verse" heading line, then a verse a line."""
    lines = data.decode("utf-8-sig").splitlines()
    rights = " ".join(line.strip() for line in lines[:2])
    if lines[2].split("\t")[0] != "Verse":
        raise SystemExit("bsb.txt: not the layout this script knows")
    out = [
        "<?xml version='1.0' encoding='UTF-8'?>",
        "<osis xmlns='http://www.bibletechnologies.net/2003/OSIS/namespace'>",
        "  <osisText osisIDWork='BSB' osisRefWork='Bible' xml:lang='en'>",
        "    <header>",
        "      <work osisWork='BSB'>",
        f"        <title>{escape(title)}</title>",
        f"        <rights>{escape(rights)}</rights>",
        "      </work>",
        "    </header>",
    ]
    book = chapter = None
    verses = 0
    for number, line in enumerate(lines[3:], start=4):
        if not line.strip():
            continue
        ref, _, text = line.partition("\t")
        name, _, cv = ref.rpartition(" ")
        c, _, v = cv.partition(":")
        if name not in BSB_BOOKS or not c.isdigit() or not v.isdigit():
            raise SystemExit(f"bsb.txt line {number}: no verse reference: {ref!r}")
        osis_book = BSB_BOOKS[name]
        if osis_book != book:
            if book is not None:
                out += ["      </chapter>", "    </div>"]
            out.append(f"    <div type='book' osisID='{osis_book}'>")
            book, chapter = osis_book, None
        if c != chapter:
            if chapter is not None:
                out.append("      </chapter>")
            out.append(f"      <chapter osisID='{book}.{c}'>")
            chapter = c
        out.append(f"        <verse osisID='{book}.{c}.{v}'>{escape(text.strip())}</verse>")
        verses += 1
    out += ["      </chapter>", "    </div>", "  </osisText>", "</osis>", ""]
    if verses < 31000:
        raise SystemExit(f"bsb.txt: only {verses} verses")
    return "\n".join(out).encode("utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--refresh", action="store_true", help="fetch even what is there")
    parser.add_argument("abbreviations", nargs="*", help="default: every one")
    args = parser.parse_args()
    wanted = args.abbreviations or list(BIBLES)
    unknown = [a for a in wanted if a not in BIBLES]
    if unknown:
        print(f"not shipped: {', '.join(unknown)}", file=sys.stderr)
        return 1

    OUT.mkdir(parents=True, exist_ok=True)
    sources = OUT / "SOURCES.txt"
    kept = {}
    if sources.exists():
        for line in sources.read_text(encoding="utf-8").splitlines():
            abbreviation, tab, rest = line.partition("\t")
            if tab and abbreviation in BIBLES:
                kept[abbreviation] = rest

    today = datetime.date.today().isoformat()
    for abbreviation in wanted:
        title, url, kind = BIBLES[abbreviation]
        path = OUT / f"{abbreviation}.xml"
        if path.exists() and not args.refresh and abbreviation in kept:
            print(f"{abbreviation}: there already")
            continue
        data = download(url)
        digest = hashlib.sha256(data).hexdigest()
        path.write_bytes(data if kind == "osis" else bsb_text_to_osis(data, title))
        how = "as served" if kind == "osis" else "written out as OSIS"
        kept[abbreviation] = f"{title}\t{url}\t{how}\tfetched {today}\tSHA-256 {digest}"
        print(f"{abbreviation}: {path.stat().st_size} bytes")

    lines = [
        "Fetched by tools/fetch_bundled_bibles.py. Each is in the public domain.",
        "Abbreviation, title, source, how it was made, when, and the SHA-256 of",
        "what was downloaded:",
        "",
    ]
    lines += [f"{a}\t{kept[a]}" for a in BIBLES if a in kept]
    sources.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
