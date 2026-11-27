#pragma once

// Strict readers of a ribbon value, for the property tables and the editor.
// Seam: plain C++20 + nlohmann/json.

#include <nlohmann/json.hpp>

#include <cmath>
#include <optional>
#include <string>
#include <string_view>

namespace seventhslide::canvas {

/// The span a number property is held to, in the units its field stores
/// (a share as 0 to 1, not as a percentage). The property's writer clamps to
/// it, and the editors' fields take their limits from it.
struct Range {
    double min = 0.0;
    double max = 0.0;

    [[nodiscard]] constexpr double clamp(double n) const noexcept
    {
        return n < min ? min : (n > max ? max : n);
    }
    /// For a property that takes whole numbers, whose limits are whole.
    [[nodiscard]] constexpr int clamp(int n) const noexcept
    {
        return static_cast<int>(clamp(static_cast<double>(n)));
    }

    [[nodiscard]] bool operator==(const Range&) const noexcept = default;
};

/// ``{"min": …, "max": …}``, as a view reads a range.
inline void to_json(nlohmann::json& j, const Range& range)
{
    j = nlohmann::json{{"min", range.min}, {"max", range.max}};
}

/// Strict readers for a property value: the value as the type a field takes,
/// or nothing when it is another type. A ribbon sends typed values, so a
/// mismatch is a mistake to report rather than something to coerce.
namespace property_value {

/// A finite number. Booleans are not numbers here.
[[nodiscard]] inline std::optional<double> number(const nlohmann::json& v)
{
    if (!v.is_number()) {
        return std::nullopt;
    }
    const double d = v.get<double>();
    return std::isfinite(d) ? std::optional<double>(d) : std::nullopt;
}

/// A finite number, rounded to the nearest whole one. QML sends whole numbers
/// as doubles, so 48.0 is 48.
[[nodiscard]] inline std::optional<int> whole(const nlohmann::json& v)
{
    const auto d = number(v);
    if (!d || std::abs(*d) > 1e9) {
        return std::nullopt;
    }
    return static_cast<int>(std::lround(*d));
}

[[nodiscard]] inline std::optional<bool> flag(const nlohmann::json& v)
{
    return v.is_boolean() ? std::optional<bool>(v.get<bool>()) : std::nullopt;
}

[[nodiscard]] inline std::optional<std::string> text(const nlohmann::json& v)
{
    return v.is_string() ? std::optional<std::string>(v.get<std::string>()) : std::nullopt;
}

/// An enumerator by its exact stored name: ``parse`` is the lenient reader
/// stored data uses, ``name`` its inverse. A name that does not survive the
/// round trip (an alias, a typo) is refused.
template <class Parse, class Name>
[[nodiscard]] auto named(const nlohmann::json& v, Parse parse, Name name)
    -> std::optional<decltype(parse(std::string_view{}))>
{
    const auto s = text(v);
    if (!s) {
        return std::nullopt;
    }
    const auto parsed = parse(std::string_view(*s));
    if (name(parsed) != std::string_view(*s)) {
        return std::nullopt;
    }
    return parsed;
}

}  // namespace property_value

}  // namespace seventhslide::canvas
