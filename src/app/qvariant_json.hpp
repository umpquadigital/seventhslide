#pragma once

// QVariant / QAbstractItemModel -> nlohmann::json.
//
// The remote mirrors state the operator UI already projects through
// QAbstractListModels, and those models are not dumb: LineMapModel strips
// bible-reference prefixes and truncates labels, ServiceItemModel computes
// display ordinals and resolves stack icons. Serialising the models rather than
// re-deriving their contents keeps one implementation of that logic. Which
// fields go on the wire, and under what names, is the remote's own list
// (WireField): a role QML adds or renames does not change what a phone gets.

#include <nlohmann/json.hpp>

#include <QVariant>

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

QT_BEGIN_NAMESPACE
class QAbstractItemModel;
QT_END_NAMESPACE

namespace seventhslide::app {

/// Recursive QVariant -> JSON. QML hands JS objects and arrays over as wrapper
/// types (QJSValue and friends), so anything convertible to a map or a list is
/// walked as one; whatever is left degrades to its string form.
[[nodiscard]] nlohmann::json to_json(const QVariant& value);

/// Recursive JSON -> QVariant (objects become QVariantMap, arrays QVariantList).
[[nodiscard]] QVariant to_qvariant(const nlohmann::json& value);

/// A field QML reads and writes as ``qml``, stored in the JSON object under ``key``.
struct QmlField {
    const char* qml;
    std::string_view key;
};

/// ``fields`` of ``object`` as a QML map. The model's own JSON stays the one
/// description of its fields; QML only sees them under its names.
[[nodiscard]] QVariantMap to_qml_fields(const nlohmann::json& object,
                                        std::span<const QmlField> fields);

/// Overwrite the ``fields`` of ``object`` that ``patch`` names, so the model's
/// ``from_json`` reads and normalises them. Fields it leaves out keep their value.
void apply_qml_fields(nlohmann::json& object,
                      const QVariantMap& patch,
                      std::span<const QmlField> fields);

/// One field of a model row on the wire: its name there, and the role it reads.
struct WireField {
    const char* key;
    int role;
};

/// Every row of ``model`` as a JSON array of objects holding ``fields``. A
/// field the model does not answer is null rather than missing.
[[nodiscard]] nlohmann::json model_rows(const QAbstractItemModel& model,
                                        std::span<const WireField> fields);

/// Record ids handed over from QML; null when any entry is not a
/// non-negative integer.
[[nodiscard]] std::optional<std::vector<std::int64_t>> parse_id_list(const QVariantList& raw);

} // namespace seventhslide::app
