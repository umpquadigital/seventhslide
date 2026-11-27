#pragma once

// Ids and list positions crossing to and from QML, where -1 means none.

#include <QStringList>
#include <QVariantList>
#include <QtTypes>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace seventhslide::app {

/// A record id handed over from QML, which passes -1 (any negative) for none.
[[nodiscard]] constexpr std::optional<std::int64_t> optional_id(qint64 id) noexcept
{
    return id < 0 ? std::nullopt : std::optional<std::int64_t>(id);
}

/// A list position handed over from QML, which passes -1 (any negative) for
/// none, meaning the end of the list.
[[nodiscard]] constexpr std::optional<int> optional_index(int index) noexcept
{
    return index < 0 ? std::nullopt : std::optional<int>(index);
}

/// A record id for QML: -1 for none.
[[nodiscard]] constexpr qint64 qml_id(std::optional<std::int64_t> id) noexcept
{
    return id ? static_cast<qint64>(*id) : qint64(-1);
}

/// A list of string ids handed over from QML (object ids, stack ids), in
/// order. Nothing is dropped: an empty id stays for the callee to refuse.
[[nodiscard]] inline std::vector<std::string> ids_from_list(const QStringList& ids)
{
    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(ids.size()));
    for (const auto& id : ids) {
        out.push_back(id.toStdString());
    }
    return out;
}

/// The same from a JS array, which reaches C++ as a QVariantList; each entry
/// is read as text (a number becomes its digits).
[[nodiscard]] inline std::vector<std::string> ids_from_list(const QVariantList& ids)
{
    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(ids.size()));
    for (const auto& id : ids) {
        out.push_back(id.toString().toStdString());
    }
    return out;
}

}  // namespace seventhslide::app
