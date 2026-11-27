#pragma once

// A name for each enumerator and back, for the canvas's stored forms.
// Internal to src/core/canvas.

#include "seventhslide/json/read.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>
#include <string_view>

namespace seventhslide::canvas {

/// A name for each enumerator and back. The first row is the default, and
/// what an unknown name reads as.
template <class E>
struct Named {
    E value;
    std::string_view name;
};

template <class E, std::size_t N>
[[nodiscard]] std::string_view name_of(const Named<E> (&table)[N], E value) noexcept
{
    for (const auto& row : table) {
        if (row.value == value) {
            return row.name;
        }
    }
    return table[0].name;
}

template <class E, std::size_t N>
[[nodiscard]] E value_of(const Named<E> (&table)[N], std::string_view name) noexcept
{
    for (const auto& row : table) {
        if (row.name == name) {
            return row.value;
        }
    }
    return table[0].value;
}

template <class E, std::size_t N>
[[nodiscard]] E enum_or(const nlohmann::json& j, const char* key, const Named<E> (&table)[N],
                        E fallback)
{
    const auto* v = json::find(j, key);
    return v != nullptr && v->is_string() ? value_of(table, v->get_ref<const std::string&>())
                                          : fallback;
}

}  // namespace seventhslide::canvas
