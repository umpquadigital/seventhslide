#pragma once

// Author credits - one name plus the roles that name is credited for.
// Seam: plain C++20 + nlohmann/json (no Qt), same as the rest of ``model/*``.
//
// Stored in ``songs.authors`` and in service-item snapshots, and passed to
// QML, in one shape (``authors_to_json`` / ``authors_from_json``):
//
//   [{"name": "Ada", "roles": [{"type": "words"},
//                              {"type": "translation", "lang": "es"}]}]

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::model {

/// Canonical role slugs. ``words`` / ``music`` / ``translation`` are the
/// OpenLyrics ``<author type>`` vocabulary, so import/export round-trips; the
/// rest extend it for what churches actually credit.
inline constexpr std::string_view k_author_role_words = "words";
inline constexpr std::string_view k_author_role_music = "music";
inline constexpr std::string_view k_author_role_translation = "translation";
inline constexpr std::string_view k_author_role_arrangement = "arrangement";
/// Free-text escape hatch: the credit is whatever ``AuthorRole::label`` says.
inline constexpr std::string_view k_author_role_other = "other";

inline constexpr std::string_view k_author_role_types[] = {
    k_author_role_words,
    k_author_role_music,
    k_author_role_translation,
    k_author_role_arrangement,
    k_author_role_other,
};

/// One credit. ``label`` carries the wording for ``other``; ``lang`` carries the
/// OpenLyrics ``lang`` attribute for ``translation``. Both stay empty otherwise
/// and are preserved untouched so a foreign file survives a round trip.
struct AuthorRole {
    std::string type;
    std::string label;
    std::string lang;

    [[nodiscard]] bool operator==(const AuthorRole&) const noexcept = default;
};

/// An author is a name first, so a name converts to an author with no roles:
/// ``{"Ada", "Bob"}`` is a credit list.
struct Author {
    std::string name;
    std::vector<AuthorRole> roles;

    Author() = default;
    // NOLINTNEXTLINE(google-explicit-constructor)
    Author(std::string author_name) : name(std::move(author_name)) {}
    // A literal is one conversion short of the above.
    // NOLINTNEXTLINE(google-explicit-constructor)
    Author(const char* author_name) : name(author_name ? author_name : "") {}
    Author(std::string author_name, std::vector<AuthorRole> author_roles)
        : name(std::move(author_name)), roles(std::move(author_roles))
    {
    }

    [[nodiscard]] bool operator==(const Author&) const noexcept = default;

    /// Append ``role`` unless an equivalent one is already there. Roles with no
    /// type, and ``other`` roles with no label, say nothing and are dropped.
    void add_role(AuthorRole role);
};

/// Canonical slug for ``raw`` - case-, space- and punctuation-insensitive, and
/// tolerant of the synonyms other song formats use ("lyrics", "composer",
/// "arranger", ...). Empty when ``raw`` names no known role.
[[nodiscard]] std::string normalize_author_role_type(std::string_view raw);

/// Roles an imported song file names with one ``type`` string. ``wordsMusic``
/// expands to both roles; an unrecognised non-empty string is kept verbatim as
/// an ``other`` role rather than thrown away.
[[nodiscard]] std::vector<AuthorRole> author_roles_from_type_string(std::string_view raw);

/// Read ``authors_to_json``'s shape. Never throws; unusable entries are skipped
/// and role types are normalised as ``Author::add_role`` does.
[[nodiscard]] std::vector<Author> authors_from_json(const nlohmann::json& value);

[[nodiscard]] nlohmann::json authors_to_json(const std::vector<Author>& authors);

/// Names joined for display ("Ada, Charles"). Blank names are skipped.
[[nodiscard]] std::string join_author_names(const std::vector<Author>& authors,
                                            std::string_view separator = ", ");

}  // namespace seventhslide::model
