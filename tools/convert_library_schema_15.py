#!/usr/bin/env python3
"""Bring a library from schema 14 to schema 15: a service's and an audio
file's own audio fades.

A one-off: no released build has made a library, so SeventhSlide itself has no
migrations, and a library at another version is refused (db/schema.hpp). What
changes:

- services and audio_files each gain a column, audio_fades: the fades that
  service sets for its audio, and that file for itself, only the fields each
  sets. Every one starts empty, setting none, so everything fades as it did:
  by each output's own.
- Each such record's shared copy (what sync last shared) gains the same empty
  field, and the conversion marks nothing to send: every computer sharing a
  library converts its own copy, so all of them hold the same records after.
  A build at schema 14 refuses a converted library, and its sync refuses what
  a converted computer sends. So every computer and server sharing a library
  goes to the new build together, each with its copy converted.

A library at schema 12 is brought to 14 first, by convert_library_schema_14.py.

SeventhSlide must be closed: a library it has open is refused. So is one with
changes received but not yet applied (sync_held). A copy of the database is
kept beside it first, and a library already converted is left alone.

Usage: tools/convert_library_schema_15.py LIBRARY [LIBRARY ...]
Each LIBRARY is a library's folder or its seventhslide.db. Exit status 0 when
every one is at schema 15 at the end. Needs Python 3 alone.
"""
import json
import sqlite3
import sys
import time
from pathlib import Path

FROM_VERSION = 14
TO_VERSION = 15
DATABASE = "seventhslide.db"

# Each table's new column, as db/schema.cpp makes it: last in the table.
COLUMN = "audio_fades"
COLUMN_SQL = "audio_fades TEXT NOT NULL DEFAULT '{}'"
TABLES = ("services", "audio_files")
EMPTY = "{}"


class Refused(Exception):
    """Why a library was left as it was, in words for people."""


def dumped(value):
    """``value`` as SeventhSlide writes JSON: compact, keys sorted, UTF-8."""
    return json.dumps(value, separators=(",", ":"), ensure_ascii=False, sort_keys=True)


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


def columns(connection, table):
    return {row[1] for row in connection.execute(f"PRAGMA table_info({table})")}


def add_columns(connection):
    """Give each table its column, unless it has it; how many did."""
    added = 0
    for table in TABLES:
        if COLUMN not in columns(connection, table):
            connection.execute(f"ALTER TABLE {table} ADD COLUMN {COLUMN_SQL}")
            added += 1
    return added


def convert_shared(connection):
    """Give each shared copy of those tables' records the empty field; how
    many changed."""
    changed = 0
    rows = connection.execute(
        "SELECT record_table, record_key, document FROM sync_shared WHERE record_table IN "
        f"({', '.join('?' for _ in TABLES)})",
        TABLES,
    ).fetchall()
    for row in rows:
        if row["document"] is None:
            continue
        document = json.loads(row["document"])
        if not isinstance(document, dict) or COLUMN in document:
            continue
        document[COLUMN] = EMPTY
        connection.execute(
            "UPDATE sync_shared SET document = ? WHERE record_table = ? AND record_key = ?",
            (dumped(document), row["record_table"], row["record_key"]),
        )
        changed += 1
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
            hint = (
                "; bring it to 14 first with convert_library_schema_14.py"
                if version == 12
                else ""
            )
            raise Refused(
                f"it is at schema {version}, and this converts only {FROM_VERSION}{hint}"
            )
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
            add_columns(connection)
            shared = convert_shared(connection)
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
            f"converted to schema {TO_VERSION} ({shared} shared records); the old database "
            f"is kept as {kept.name}"
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
