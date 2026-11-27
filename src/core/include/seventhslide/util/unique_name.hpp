#pragma once

// A display name no other row in a list already has, compared without case.
//
// Plain C++20 (no Qt).

#include "seventhslide/util/strings.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace seventhslide::util {

/// The names ``items`` already use (``name_of`` each), in lower case. The row
/// at ``skip`` is left out, so a row being renamed may keep its own name.
template <class Item, class NameOf>
[[nodiscard]] std::unordered_set<std::string> taken_names(const std::vector<Item>& items,
                                                          NameOf name_of,
                                                          std::optional<std::size_t> skip = {})
{
    std::unordered_set<std::string> taken;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (skip && i == *skip) {
            continue;
        }
        taken.insert(to_lower(std::invoke(name_of, items[i])));
    }
    return taken;
}

/**
 * ``candidate`` when ``taken`` (see taken_names) does not hold it; otherwise
 * the first of ``suffix_base`` + " 2", " 3", ... that it does not hold.
 *
 * The suffix base is separate because a list may shorten a taken name before
 * numbering it (outputs do) while still keeping a free name whole.
 */
[[nodiscard]] inline std::string unique_name(std::string candidate,
                                             std::string_view suffix_base,
                                             const std::unordered_set<std::string>& taken)
{
    if (!taken.contains(to_lower(candidate))) {
        return candidate;
    }
    for (int n = 2;; ++n) {
        std::string next = std::string(suffix_base) + " " + std::to_string(n);
        if (!taken.contains(to_lower(next))) {
            return next;
        }
    }
}

}  // namespace seventhslide::util
