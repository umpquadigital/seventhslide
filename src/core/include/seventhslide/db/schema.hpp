#pragma once

// The library database schema and its version.
//
// The version lives in SQLite's own ``PRAGMA user_version``. This build makes
// and reads exactly ``k_schema_version``; a library at any other version is
// refused, as no released build has made one to upgrade. The number goes on
// from the ones development builds made, so a library one of them made is
// refused rather than read as something it is not. The schema every
// library has is checked in as fixtures/schema/library.txt (test_schema.cpp
// holds the two the same), so a change to it is a reviewed diff. The first
// change after a release brings back a migration from that version, and a
// copy of the file kept before it runs.
//
// Every record that syncs has a ``uid`` beside its ``id``: the ``id`` names
// it in this file, the ``uid`` on every computer. The library has one too
// (``library_uid``). ``sync_tables.hpp`` says how each table syncs.
//
// Freeform documents stored inside the database follow their own
// ``freeform::k_schema_version``: read at that version, refused at any other
// (``freeform::document_refusal``).

#include "seventhslide/db/database.hpp"

#include <string>
#include <string_view>

namespace seventhslide::db {

/// Schema version this build creates and reads.
inline constexpr int k_schema_version = 15;

/// ``library_meta``'s key for the library's own uid.
inline constexpr std::string_view k_library_uid_key = "library_uid";
/// ``library_meta``'s key saying the shipped fonts have been copied in. It syncs,
/// so another computer does not copy in again what has since been removed.
inline constexpr std::string_view k_bundled_fonts_key = "bundled_fonts_taken";
/// The same for the shipped Bibles.
inline constexpr std::string_view k_bundled_bibles_key = "bundled_bibles_taken";

/// Version recorded in the database (0 for one that has never had a schema).
[[nodiscard]] int schema_version(Database& db);

/// The library's own uid: 32 lower-case hex digits, made with the library and
/// never changed, so it names the library on every computer that has it,
/// whatever each calls it. A copy of the library file has the same one.
/// Throws when there is none.
[[nodiscard]] std::string library_uid(Database& db);

/// Create the schema in an empty database, at ``k_schema_version``.
void create_schema(Database& db);

/**
 * Make ``db`` ready to use: create the schema in an empty database, or check
 * that an existing library is at ``k_schema_version``. True when it created
 * the schema: the database was empty, a library made just now.
 *
 * Throws ``std::runtime_error`` with a message fit to show the operator when
 * the file is not a SeventhSlide library, was made by another build, or the
 * schema cannot be created. A database is left as it was when creating the
 * schema fails.
 */
bool ensure_schema(Database& db);

}  // namespace seventhslide::db
