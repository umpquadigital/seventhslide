#pragma once

// Tolerant reads of one key out of a JSON object.
//
// Every one of these returns the fallback for a missing key, a null, a wrong
// type, or a value that is not an object at all - so a caller can read a blob
// it did not write without checking each step.
//
// Two integer readers exist on purpose:
//   ``int_or``       rounds (a geometry value of 47.6 means 48)
//   ``style_int``    truncates, because that is what the painters and the
//                    layout engine have always done with a theme's behaviour
//                    keys, and their output is pixel-compared.
//
// Plain C++20 + nlohmann/json (no Qt).

#include <nlohmann/json.hpp>

#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::json {

[[nodiscard]] inline const nlohmann::json* find(const nlohmann::json& j, const char* key)
{
    if (!j.is_object()) {
        return nullptr;
    }
    const auto it = j.find(key);
    return it == j.end() ? nullptr : &*it;
}

/// ``j[key]`` when ``j`` is an object holding it, else null. ``Json`` is
/// ``nlohmann::json`` or ``const nlohmann::json``, for a walk that rewrites.
template <class Json>
[[nodiscard]] Json* member(Json& j, std::string_view key)
{
    if (!j.is_object()) {
        return nullptr;
    }
    const auto it = j.find(key);
    return it == j.end() ? nullptr : &*it;
}

[[nodiscard]] inline std::string str_or(const nlohmann::json& j,
                                        const char* key,
                                        std::string_view fallback = {})
{
    const auto* v = find(j, key);
    return v != nullptr && v->is_string() ? v->get<std::string>() : std::string(fallback);
}

[[nodiscard]] inline double num_or(const nlohmann::json& j, const char* key, double fallback)
{
    const auto* v = find(j, key);
    return v != nullptr && v->is_number() ? v->get<double>() : fallback;
}

/// Rounded to the nearest integer. One no ``int`` holds (a hand-edited
/// 1e12) is the fallback, not whatever its cast would wrap to.
[[nodiscard]] inline int int_or(const nlohmann::json& j, const char* key, int fallback)
{
    const auto* v = find(j, key);
    if (v == nullptr || !v->is_number()) {
        return fallback;
    }
    const double rounded = std::round(v->get<double>());
    if (!(rounded >= std::numeric_limits<int>::min() && rounded <= std::numeric_limits<int>::max())) {
        return fallback;
    }
    return static_cast<int>(rounded);
}

/// Truncated toward zero: the flat-style-key reader the painters use.
[[nodiscard]] inline int style_int(const nlohmann::json& j, const char* key, int fallback)
{
    const auto* v = find(j, key);
    if (v == nullptr) {
        return fallback;
    }
    if (v->is_number_integer()) {
        return v->get<int>();
    }
    if (v->is_number_float()) {
        return static_cast<int>(v->get<double>());
    }
    return fallback;
}

/// A boolean, or an integer read as C truth (a theme may store 0 / 1).
[[nodiscard]] inline bool bool_or(const nlohmann::json& j, const char* key, bool fallback)
{
    const auto* v = find(j, key);
    if (v == nullptr) {
        return fallback;
    }
    if (v->is_boolean()) {
        return v->get<bool>();
    }
    if (v->is_number_integer()) {
        return v->get<int>() != 0;
    }
    return fallback;
}

/// Booleans only: the style readers take no integer.
[[nodiscard]] inline bool style_bool(const nlohmann::json& j, const char* key, bool fallback)
{
    const auto* v = find(j, key);
    return v != nullptr && v->is_boolean() ? v->get<bool>() : fallback;
}

[[nodiscard]] inline nlohmann::json object_or_empty(const nlohmann::json& j, const char* key)
{
    const auto* v = find(j, key);
    return v != nullptr && v->is_object() ? *v : nlohmann::json::object();
}

[[nodiscard]] inline nlohmann::json array_or_empty(const nlohmann::json& j, const char* key)
{
    const auto* v = find(j, key);
    return v != nullptr && v->is_array() ? *v : nlohmann::json::array();
}

/// The strings in ``j`` when it is an array, skipping anything else in it;
/// empty when ``j`` is not an array.
[[nodiscard]] inline std::vector<std::string> strings_in(const nlohmann::json& j)
{
    std::vector<std::string> out;
    if (j.is_array()) {
        for (const auto& el : j) {
            if (el.is_string()) {
                out.push_back(el.get<std::string>());
            }
        }
    }
    return out;
}

/// ``strings_in`` of ``j[key]``.
[[nodiscard]] inline std::vector<std::string> strings_or_empty(const nlohmann::json& j,
                                                               const char* key)
{
    const auto* v = find(j, key);
    return v != nullptr ? strings_in(*v) : std::vector<std::string>{};
}

}  // namespace seventhslide::json
