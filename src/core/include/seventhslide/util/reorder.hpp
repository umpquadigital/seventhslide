#pragma once

// Put a list into a caller-given order without ever losing an element.
//
// Plain C++20 (no Qt).

#include <cstddef>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace seventhslide::util {

/// Why ``reorder_by_key`` refused an order.
enum class ReorderError {
    None,
    /// The order names a different number of elements than the list holds.
    WrongCount,
    /// The order names the same key twice.
    DuplicateKey,
    /// The order names a key no element has.
    UnknownKey,
};

/**
 * Rearrange ``items`` so that element ``i`` is the one whose ``key_of`` equals
 * ``order[i]``.
 *
 * The whole order is checked before anything moves, so on any error ``items``
 * is exactly as it was. On success the keys are distinct and as many as the
 * elements, and each key names a different element, so every element is placed
 * exactly once. Problems are reported in the order they are checked: the count
 * first, then each key in turn.
 */
template <typename T, typename Key, typename KeyOf>
[[nodiscard]] ReorderError reorder_by_key(std::vector<T>& items,
                                          const std::vector<Key>& order,
                                          KeyOf&& key_of)
{
    if (order.size() != items.size()) {
        return ReorderError::WrongCount;
    }

    std::unordered_map<Key, std::size_t> position;
    position.reserve(items.size());
    for (std::size_t i = 0; i < items.size(); ++i) {
        position.try_emplace(key_of(items[i]), i);
    }

    std::vector<std::size_t> source;
    source.reserve(order.size());
    std::unordered_set<Key> seen;
    seen.reserve(order.size());
    for (const auto& key : order) {
        if (!seen.insert(key).second) {
            return ReorderError::DuplicateKey;
        }
        const auto it = position.find(key);
        if (it == position.end()) {
            return ReorderError::UnknownKey;
        }
        source.push_back(it->second);
    }

    std::vector<T> next;
    next.reserve(items.size());
    for (const std::size_t i : source) {
        next.push_back(std::move(items[i]));
    }
    items = std::move(next);
    return ReorderError::None;
}

}  // namespace seventhslide::util
