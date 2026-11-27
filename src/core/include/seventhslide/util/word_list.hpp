#pragma once

// Words someone types into a list - theme tags, song topics, service tags:
// tidied the same way everywhere.

#include "seventhslide/util/strings.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace seventhslide::util {

struct WordListRules {
    /// Words kept; the rest are dropped.
    std::size_t max_words = 24;
    /// Bytes one word may take, cut at a character boundary.
    std::size_t max_bytes = 40;
    /// Store words lower-case (filter tags) or as typed (topics people read).
    bool lower_case = false;
};

/// ``raw`` tidied: inner spaces collapsed and the ends trimmed, each word cut to
/// ``max_bytes`` without splitting a UTF-8 sequence, empty words dropped, and a
/// word that repeats an earlier one (ignoring case) dropped, keeping the first
/// spelling. Order is kept; at most ``max_words`` remain.
[[nodiscard]] inline std::vector<std::string> normalize_word_list(
    const std::vector<std::string>& raw, const WordListRules& rules)
{
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    for (const auto& part : raw) {
        std::string word = collapse_ws(part);
        if (rules.lower_case) {
            word = to_lower(word);
        }
        if (word.size() > rules.max_bytes) {
            word = trim(utf8_prefix(word, rules.max_bytes));
        }
        if (word.empty() || !seen.insert(to_lower(word)).second) {
            continue;
        }
        out.push_back(std::move(word));
        if (out.size() >= rules.max_words) {
            break;
        }
    }
    return out;
}

}  // namespace seventhslide::util
