#!/usr/bin/env python3
"""Bring a library from schema 12 to schema 14: Song, Bible and Media themes.

A one-off: no released build has made a library, so SeventhSlide itself has no
migrations, and a library at another version is refused (db/schema.hpp). The
tables are as they were; what changes is the themes and the theme choices
stored in them:

- An output's text themes become its Song, Bible and Media themes: each text
  theme is kept as one of each kind, under its own id, holding only that kind's
  design contexts and settings (theme::filter_theme_style). So every theme
  choice stored by id still names a theme, and nothing plays differently.
- Every stored choice ({"text": id, "bg": id} per output) is made per category,
  {"song": {"theme": id, "bg": id}, "bible": {...}, "media": {"theme": id}},
  for the categories what holds it takes its looks from:
    an output's defaults, and a style profile's copy of them: song and Bible as
      they were; media the song default's theme, which media played under;
    a service's choices: every category;
    a song's theme chips (the song, a rundown song, a profile's copy): song;
    a rundown item's: its kind's (a Bible passage's Bible, a picture's media
      theme, a divider's song background; a Freeform item takes none).
  A media item's background was never painted (it fills the screen over
  black), so none is kept.

Each record's shared copy (what sync last shared) is converted the same way,
and the conversion marks nothing to send: every computer sharing a library
converts its own copy, so all of them hold the same records after. A build at
schema 12 refuses a converted library, and its sync refuses what a converted
computer sends. So every computer and server sharing a library goes to the new
build together, each with its copy converted.

SeventhSlide must be closed: a library it has open is refused. So is one with
changes received but not yet applied (sync_held). A copy of the database is
kept beside it first, and a library already converted is left alone.

Usage: tools/convert_library_schema_14.py LIBRARY [LIBRARY ...]
Each LIBRARY is a library's folder or its seventhslide.db. Exit status 0 when
every one is at schema 14 at the end. Needs Python 3 alone.
"""

import copy
import json
import sqlite3
import sys
import time
from pathlib import Path

FROM_VERSION = 12
TO_VERSION = 14
DATABASE = "seventhslide.db"

# What each kind of theme stores (model/theme_keys.hpp) and which design
# contexts are its (theme/theme_kind.cpp), word for word.
THEME_KEYS = {
    "song": {
        "balance_wrapped_lines",
        "balance_wrapped_strength",
        "elements",
        "fluid_slides",
        "follow_lines",
        "prevent_mixed_active",
        "show_title_slides",
        "title_slide_show_lines",
        "transitions",
        "verse_gap",
    },
    "bible": {"bible_fluid_verses", "elements", "transitions"},
    "media": {"elements", "transitions"},
}
CONTEXTS = {
    "song": {"song", "titles"},
    "bible": {"bible"},
    "media": {"image", "video", "web", "live"},
}
PER_CONTEXT_KEYS = {"elements", "transitions"}
TEXT_KINDS = ("song", "bible", "media")

# The category a kind of content takes its text theme from, and the one it
# takes its background from (theme::theme_needs), by service item type.
MEDIA_TYPES = ("image", "image_folder", "video", "audio", "web", "live")
NEEDS = {"song": ("song", "song"), "bible": ("bible", "bible"), "divider": (None, "song")}
NEEDS.update({t: ("media", None) for t in MEDIA_TYPES})


class Refused(Exception):
    """Why a library was left as it was, in words for people."""


def dumped(value):
    """``value`` as SeventhSlide writes JSON: compact, keys sorted, UTF-8."""
    return json.dumps(value, separators=(",", ":"), ensure_ascii=False, sort_keys=True)


# --- What is converted ------------------------------------------------------


# What an element set holds beside its contexts' lists.
ELEMENT_SET_FIELDS = {"canvas", "next_id"}


def own_contexts(kind, per_context):
    """Only ``kind``'s contexts' entries, and the element set's own fields
    (theme::own_contexts_only). An old Audio context's goes: an audio item's
    fades are each output's now, and are not kept."""
    if not isinstance(per_context, dict):
        return per_context
    return {
        key: value
        for key, value in per_context.items()
        if key in CONTEXTS[kind] or key in ELEMENT_SET_FIELDS
    }


def theme_style_as(kind, style):
    """``style`` as a ``kind`` theme stores it (theme::filter_theme_style)."""
    if not isinstance(style, dict):
        return {}
    return {
        key: own_contexts(kind, value) if key in PER_CONTEXT_KEYS else value
        for key, value in style.items()
        if key in THEME_KEYS[kind]
    }


def old_ids(pick):
    """An old ``{"text", "bg"}`` pick's ids, trimmed; "" for one not set."""
    if not isinstance(pick, dict):
        return "", ""

    def one(name):
        value = pick.get(name)
        return value.strip() if isinstance(value, str) else ""

    return one("text"), one("bg")


def picks(theme_id, bg_id, theme_categories, bg_categories):
    """The new per-category picks: ``theme_id`` for each of
    ``theme_categories``, ``bg_id`` for each of ``bg_categories``."""
    out = {}
    for category in theme_categories:
        if theme_id:
            out.setdefault(category, {})["theme"] = theme_id
    for category in bg_categories:
        if bg_id:
            out.setdefault(category, {})["bg"] = bg_id
    return out


def category_defaults(old):
    """An output's defaults: song and Bible as they were, and for media the
    song default's theme, which media played under."""
    old = old if isinstance(old, dict) else {}
    song_theme, song_bg = old_ids(old.get("song"))
    bible_theme, bible_bg = old_ids(old.get("bible"))
    out = picks(song_theme, song_bg, ["song", "media"], ["song"])
    out.update(picks(bible_theme, bible_bg, ["bible"], ["bible"]))
    return out


def output_map(old, convert_pick):
    """A map of picks by output id, each converted; outputs left with none go."""
    if not isinstance(old, dict):
        return {}
    out = {}
    for output_id, pick in old.items():
        converted = convert_pick(*old_ids(pick))
        if converted:
            out[output_id] = converted
    return out


def service_map(old):
    """A service's picks: every category takes them."""
    return output_map(old, lambda t, b: picks(t, b, TEXT_KINDS, ["song", "bible"]))


def content_map(item_type, old):
    """A rundown item's own picks, for what its kind takes."""
    theme_category, bg_category = NEEDS.get(item_type, (None, None))
    return output_map(
        old,
        lambda t, b: picks(
            t, b, [theme_category] if theme_category else [], [bg_category] if bg_category else []
        ),
    )


def song_pack(old):
    """A song's theme chips, each choosing song themes."""
    if not isinstance(old, dict) or not isinstance(old.get("arrangements"), list):
        return old
    out = copy.deepcopy(old)
    for arrangement in out["arrangements"]:
        chips = arrangement.get("chips") if isinstance(arrangement, dict) else None
        if isinstance(chips, dict):
            for chip_id, chip in chips.items():
                chips[chip_id] = content_map("song", chip)
    return out


def output_data(data):
    """An output: its text themes as one of each kind, its defaults per category."""
    out = dict(data)
    text_themes = out.pop("text_themes", [])
    text_themes = text_themes if isinstance(text_themes, list) else []
    for kind in TEXT_KINDS:
        themes = []
        for theme in text_themes:
            if isinstance(theme, dict):
                entry = dict(theme)
                entry["style"] = theme_style_as(kind, theme.get("style"))
                themes.append(entry)
        out[f"{kind}_themes"] = themes
    out["category_defaults"] = category_defaults(data.get("category_defaults"))
    return out


def item_data(item_type, data):
    if not isinstance(data, dict) or "theme_map" not in data:
        return data
    out = dict(data)
    if item_type == "song":
        out["theme_map"] = song_pack(data["theme_map"])
    elif item_type == "freeform":
        # A Freeform item's slides carry their own design: it takes none.
        out["theme_map"] = {}
    else:
        out["theme_map"] = content_map(item_type, data["theme_map"])
    return out


def profile_data(data):
    if not isinstance(data, dict):
        return data
    out = dict(data)
    if isinstance(data.get("outputs"), dict):
        out["outputs"] = {k: category_defaults(v) for k, v in data["outputs"].items()}
    if isinstance(data.get("songs"), dict):
        out["songs"] = {k: song_pack(v) for k, v in data["songs"].items()}
    return out


def as_text(convert):
    """``convert`` over a column holding JSON as text; text that is not JSON,
    or none, stays as it is."""

    def run(text, *args):
        if not isinstance(text, str):
            return text
        try:
            value = json.loads(text)
        except ValueError:
            return text
        converted = convert(*args, value)
        return text if converted == value else dumped(converted)

    return run


# Each converted column: its table, the column, and the conversion (given the
# row's item type where it takes one). Sync's shared copy of a record holds the
# column under the same name.
COLUMNS = [
    ("outputs", "data", as_text(output_data)),
    ("songs", "theme_map", as_text(song_pack)),
    ("services", "theme_map", as_text(service_map)),
    ("service_items", "data", as_text(item_data)),
    ("style_profiles", "data", as_text(profile_data)),
]


# --- The library ------------------------------------------------------------


def database_of(library):
    path = Path(library)
    if path.is_dir():
        path = path / DATABASE
    if not path.is_file():
        raise Refused("there is no library database here")
    return path


def backup_path(database):
    stamp = time.strftime("%Y%m%d-%H%M%S")
    path = database.with_name(f"{database.name}.before-schema-{TO_VERSION}-{stamp}")
    at = 1
    while path.exists():
        path = database.with_name(f"{database.name}.before-schema-{TO_VERSION}-{stamp}-{at}")
        at += 1
    return path


def keep_copy(connection, database):
    """Copy the database beside itself, checked; where it went."""
    path = backup_path(database)
    kept = sqlite3.connect(path)
    try:
        connection.backup(kept)
        if kept.execute("PRAGMA quick_check").fetchone()[0] != "ok":
            raise Refused("the database did not copy cleanly, so it may be damaged")
    finally:
        kept.close()
    return path


def record_key(table, row):
    """The key sync knows a row by: an output by the id in its data, the rest
    by their uid."""
    if table == "outputs":
        try:
            return json.loads(row["data"]).get("id")
        except (ValueError, AttributeError):
            return None
    return row["uid"]


def convert_rows(connection):
    """Convert every column and its shared copy; how many rows changed."""
    changed = 0
    for table, column, convert in COLUMNS:
        uid = "NULL AS uid" if table == "outputs" else "uid"
        item_type = "item_type" if table == "service_items" else "NULL AS item_type"
        rows = connection.execute(
            f"SELECT id, {uid}, {item_type}, {column} AS value FROM {table} ORDER BY id"
        ).fetchall()
        for row in rows:
            args = (row["item_type"],) if table == "service_items" else ()
            new_value = convert(row["value"], *args)
            key = record_key(table, {"data": row["value"], "uid": row["uid"]})
            if new_value != row["value"]:
                connection.execute(
                    f"UPDATE {table} SET {column} = ? WHERE id = ?", (new_value, row["id"])
                )
                changed += 1
            if key is None:
                continue
            shared = connection.execute(
                "SELECT document FROM sync_shared WHERE record_table = ? AND record_key = ?",
                (table, key),
            ).fetchone()
            if shared is None or shared["document"] is None:
                continue
            document = json.loads(shared["document"])
            if not isinstance(document, dict) or column not in document:
                continue
            item = (document.get("item_type"),) if table == "service_items" else ()
            converted = convert(document[column], *item)
            if converted != document[column]:
                document[column] = converted
                connection.execute(
                    "UPDATE sync_shared SET document = ? WHERE record_table = ? AND record_key = ?",
                    (dumped(document), table, key),
                )
    return changed


def marks(connection):
    """What the outbox has marked to send, as (table, key)."""
    rows = connection.execute("SELECT record_table, record_key FROM sync_outbox").fetchall()
    return {(row[0], row[1]) for row in rows}


def convert(database):
    """Convert ``database``; what became of it, in words."""
    connection = sqlite3.connect(database, timeout=1, isolation_level=None)
    connection.row_factory = sqlite3.Row
    try:
        # Any connection to a database in WAL mode holds a lock on the file for
        # as long as it is open, so this is refused while SeventhSlide has it
        # open, not only while it is writing. SQLite takes the lock this way only
        # when asked before the first read. Held until the connection closes.
        connection.execute("PRAGMA locking_mode = EXCLUSIVE")
        try:
            version = connection.execute("PRAGMA user_version").fetchone()[0]
            connection.execute("BEGIN EXCLUSIVE")
            connection.execute("COMMIT")
        except sqlite3.OperationalError as ex:
            if "locked" in str(ex) or "busy" in str(ex):
                raise Refused("it is open, so close SeventhSlide first") from ex
            raise
        if version == TO_VERSION:
            return f"already at schema {TO_VERSION}"
        if version != FROM_VERSION:
            raise Refused(f"it is at schema {version}, and this converts only {FROM_VERSION}")
        held = connection.execute("SELECT COUNT(*) FROM sync_held").fetchone()[0]
        if held:
            raise Refused(
                "it holds changes from another computer not yet applied: open it in the "
                "old build and let it sync first"
            )

        kept = keep_copy(connection, database)
        connection.execute("BEGIN IMMEDIATE")
        try:
            marked_before = marks(connection)
            changed = convert_rows(connection)
            # Every computer converts its own copy: nothing is sent for it.
            for mark in marks(connection) - marked_before:
                connection.execute(
                    "DELETE FROM sync_outbox WHERE record_table = ? AND record_key = ?", mark
                )
            connection.execute(f"PRAGMA user_version = {TO_VERSION}")
            connection.execute("COMMIT")
        except Exception:
            connection.execute("ROLLBACK")
            raise
        return (
            f"converted to schema {TO_VERSION} ({changed} records); the old database is "
            f"kept as {kept.name}"
        )
    finally:
        connection.close()


def main():
    if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help"):
        print(__doc__)
        return 2
    failed = False
    for library in sys.argv[1:]:
        try:
            print(f"{library}: {convert(database_of(library))}")
        except Refused as ex:
            print(f"{library}: not converted: {ex}", file=sys.stderr)
            failed = True
        except (sqlite3.Error, ValueError) as ex:
            print(f"{library}: not converted: {ex}", file=sys.stderr)
            failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
