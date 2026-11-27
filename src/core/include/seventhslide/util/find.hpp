#pragma once

// Look an element up by one of its fields.
//
// Plain C++20 (no Qt).

#include <algorithm>
#include <functional>
#include <memory>
#include <ranges>
#include <type_traits>
#include <utility>

namespace seventhslide::util {

/**
 * The first element of ``range`` whose ``proj`` equals ``key``, or null.
 *
 * ``proj`` is usually a data member, as in ``find_by(stacks, id, &StackDef::id)``.
 * The pointer is const exactly when ``range`` is, so a const and a mutable
 * lookup are the same call. A temporary container is refused at compile
 * time: the pointer would outlive it.
 */
template <std::ranges::forward_range Range, typename Key, typename Proj = std::identity>
    requires std::is_lvalue_reference_v<Range> || std::ranges::borrowed_range<Range>
[[nodiscard]] auto find_by(Range&& range, const Key& key, Proj proj = {})
{
    const auto it = std::ranges::find(range, key, std::move(proj));
    return it == std::ranges::end(range) ? nullptr : std::addressof(*it);
}

}  // namespace seventhslide::util
