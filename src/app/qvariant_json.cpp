#include "qvariant_json.hpp"

#include <QAbstractItemModel>
#include <QJSValue>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace seventhslide::app {

nlohmann::json to_json(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return nullptr;
    }
    switch (value.typeId()) {
    case QMetaType::Bool:
        return value.toBool();
    case QMetaType::Int:
    case QMetaType::Short:
        return value.toInt();
    case QMetaType::UInt:
    case QMetaType::UShort:
        return value.toUInt();
    // ``long`` is 64 bits on Linux and macOS, 32 on Windows.
    case QMetaType::Long:
    case QMetaType::LongLong:
        return static_cast<std::int64_t>(value.toLongLong());
    case QMetaType::ULong:
    case QMetaType::ULongLong:
        return static_cast<std::uint64_t>(value.toULongLong());
    case QMetaType::Float:
    case QMetaType::Double:
        return value.toDouble();
    case QMetaType::QString:
        return value.toString().toStdString();
    case QMetaType::QStringList: {
        nlohmann::json out = nlohmann::json::array();
        for (const QString& s : value.toStringList()) {
            out.push_back(s.toStdString());
        }
        return out;
    }
    case QMetaType::QVariantList: {
        nlohmann::json out = nlohmann::json::array();
        for (const QVariant& v : value.toList()) {
            out.push_back(to_json(v));
        }
        return out;
    }
    case QMetaType::QVariantMap:
    case QMetaType::QVariantHash: {
        nlohmann::json out = nlohmann::json::object();
        const QVariantMap map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            out[it.key().toStdString()] = to_json(it.value());
        }
        return out;
    }
    default:
        break;
    }

    // A JS value from QML (a Q_INVOKABLE's QVariant argument) arrives wrapped
    // as a QJSValue. Its own conversion keeps an array an array; asking the
    // QVariant for a map first would read ["a", "b"] as {"0": "a", "1": "b"}.
    if (value.metaType() == QMetaType::fromType<QJSValue>()) {
        const QVariant plain = value.value<QJSValue>().toVariant();
        // Never recurse on the same wrapper (a value toVariant cannot unwrap).
        if (plain.metaType() != QMetaType::fromType<QJSValue>()) {
            return to_json(plain);
        }
    }

    // Any other wrapper a map or a list can be read from.
    if (value.canConvert<QVariantMap>()) {
        const QVariantMap map = value.toMap();
        nlohmann::json out = nlohmann::json::object();
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            out[it.key().toStdString()] = to_json(it.value());
        }
        return out;
    }
    if (value.canConvert<QVariantList>()) {
        nlohmann::json out = nlohmann::json::array();
        for (const QVariant& v : value.toList()) {
            out.push_back(to_json(v));
        }
        return out;
    }
    return value.toString().toStdString();
}

QVariant to_qvariant(const nlohmann::json& value)
{
    switch (value.type()) {
    case nlohmann::json::value_t::null:
        return {};
    case nlohmann::json::value_t::boolean:
        return value.get<bool>();
    case nlohmann::json::value_t::number_integer:
        return QVariant::fromValue(value.get<std::int64_t>());
    case nlohmann::json::value_t::number_unsigned:
        return QVariant::fromValue(value.get<std::uint64_t>());
    case nlohmann::json::value_t::number_float:
        return value.get<double>();
    case nlohmann::json::value_t::string:
        return QString::fromStdString(value.get<std::string>());
    case nlohmann::json::value_t::array: {
        QVariantList out;
        out.reserve(static_cast<qsizetype>(value.size()));
        for (const auto& el : value) {
            out.push_back(to_qvariant(el));
        }
        return out;
    }
    case nlohmann::json::value_t::object: {
        QVariantMap out;
        for (auto it = value.begin(); it != value.end(); ++it) {
            out.insert(QString::fromStdString(it.key()), to_qvariant(it.value()));
        }
        return out;
    }
    default:
        return {};
    }
}

nlohmann::json model_rows(const QAbstractItemModel& model, std::span<const WireField> fields)
{
    nlohmann::json rows = nlohmann::json::array();
    const int count = model.rowCount();
    for (int r = 0; r < count; ++r) {
        const QModelIndex idx = model.index(r, 0);
        nlohmann::json row = nlohmann::json::object();
        for (const WireField& field : fields) {
            row[field.key] = to_json(model.data(idx, field.role));
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

std::optional<std::vector<std::int64_t>> parse_id_list(const QVariantList& raw)
{
    std::vector<std::int64_t> ids;
    ids.reserve(static_cast<std::size_t>(raw.size()));
    for (const auto& v : raw) {
        bool ok = false;
        const auto id = v.toLongLong(&ok);
        if (!ok || id < 0) {
            return std::nullopt;
        }
        ids.push_back(id);
    }
    return ids;
}

QVariantMap to_qml_fields(const nlohmann::json& object, std::span<const QmlField> fields)
{
    QVariantMap out;
    for (const auto& field : fields) {
        const auto it = object.find(field.key);
        if (it != object.end()) {
            out.insert(QString::fromLatin1(field.qml), to_qvariant(*it));
        }
    }
    return out;
}

void apply_qml_fields(nlohmann::json& object,
                      const QVariantMap& patch,
                      std::span<const QmlField> fields)
{
    for (const auto& field : fields) {
        const auto name = QString::fromLatin1(field.qml);
        if (patch.contains(name)) {
            object[std::string(field.key)] = to_json(patch.value(name));
        }
    }
}

} // namespace seventhslide::app
