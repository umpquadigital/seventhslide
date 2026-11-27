#pragma once

// Freeform ``{token}`` / ``{-token}`` discovery and substitution.
// Seam: plain C++20 (no Qt).

#include "seventhslide/freeform/document.hpp"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::freeform {

/// ``[a-z][a-z0-9_-]{0,27}`` - lowercase, no spaces, max 28 chars.
inline constexpr int k_max_field_name = 28;

[[nodiscard]] bool is_valid_field_name(std::string_view name) noexcept;

/// Lowercase, spaces to ``_``, drop illegal chars, max 28. Empty if not valid.
[[nodiscard]] std::string sanitize_field_name(std::string_view raw);

struct TokenHit {
    std::string name;
    bool hide_line_if_empty = false;
};

/// Valid tokens across every text object, first occurrence of each name in
/// document order. ``hide_line_if_empty`` is set when *any* occurrence spells
/// the ``{-name}`` form.
[[nodiscard]] std::vector<TokenHit> collect_document_tokens(const Document& doc);

/// Names of ``collect_document_tokens``, first-seen order.
[[nodiscard]] std::vector<std::string> collect_field_names(const Document& doc);

/// Fill ``{token}`` / ``{-token}``. Only ``is_valid_field_name`` keys substitute;
/// illegal braces stay literal (``{Title}`` is not ``{title}``).
[[nodiscard]] std::string substitute_fields(
    std::string_view text, const std::map<std::string, std::string>& values);

/// Occurrences of ``{name}`` / ``{-name}`` across the document - every one, not
/// the first per text. Reaches the same text as ``collect_document_tokens``.
[[nodiscard]] int count_field_usage(const Document& doc, std::string_view name);

/// Rewrite every ``{old}`` / ``{-old}`` to ``{new}`` / ``{-new}``, keeping the
/// hide form per occurrence. An empty ``new_name`` removes the token text.
/// Returns the number of occurrences changed.
int rewrite_field_token(Document& doc, std::string_view old_name, std::string_view new_name);


/// Drop fields whose names could never be a token. Call on Save - unused but
/// legal fields are the operator's to keep or delete.
void prune_invalid_fields(Document& doc);

} // namespace seventhslide::freeform
