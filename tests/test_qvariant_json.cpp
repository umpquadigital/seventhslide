// QVariant / QAbstractItemModel -> JSON, the serialiser under every remote payload.
//
// The design note on qvariant_json.hpp is that the remote reuses the operator
// UI's models rather than re-deriving their contents, and sends the fields it
// lists. That only holds if model_rows reads exactly those fields and if to_json
// never drops a value on the floor - an unhandled type or an invalid QVariant
// must still produce a key, or the client sees a field that silently vanished.

#include "test_qt_app.hpp"

#include <catch2/catch_test_macros.hpp>

#include "qml_ids.hpp"
#include "qvariant_json.hpp"

#include <QAbstractListModel>
#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJSEngine>
#include <QJSValue>
#include <QModelIndex>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariant>
#include <QVariantHash>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>
#include <iterator>
#include <limits>

using seventhslide::app::model_rows;
using seventhslide::app::WireField;
using seventhslide::app::to_json;
using seventhslide::app::to_qvariant;

namespace {

/// Two rows, four roles - including one role the model deliberately does not
/// answer, which is how a real model behaves for a row that lacks the field.
class FakeRowModel final : public QAbstractListModel {
public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        CountRole,
        FlagRole,
        UnansweredRole,
    };

    int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : 2;
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= 2) {
            return {};
        }
        const bool first = index.row() == 0;
        switch (role) {
        case NameRole:
            return first ? QStringLiteral("alpha") : QStringLiteral("beta");
        case CountRole:
            return first ? 7 : 0;
        case FlagRole:
            return first;
        default:
            return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {{NameRole, "name"},
                {CountRole, "count"},
                {FlagRole, "flag"},
                {UnansweredRole, "unanswered"}};
    }
};

class EmptyModel final : public QAbstractListModel {
public:
    int rowCount(const QModelIndex&) const override { return 0; }
    QVariant data(const QModelIndex&, int) const override { return {}; }
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole + 1, "x"}}; }
};

} // namespace

TEST_CASE("to_json maps only an invalid QVariant to null", "[app][qvariant_json]")
{
    // An unset role (QVariant()) is the one thing that becomes JSON null.
    REQUIRE(to_json(QVariant()).is_null());

    // A QVariant *holding* a null QString is still a valid string variant in
    // Qt 6, so it serialises as "" rather than null. Worth pinning: the remote
    // client distinguishes "field absent" from "field empty", and a change here
    // would flip every blank title from "" to null.
    const nlohmann::json null_string = to_json(QVariant::fromValue(QString()));
    REQUIRE(null_string.is_string());
    REQUIRE(null_string == "");
}

TEST_CASE("to_json preserves numeric types and width", "[app][qvariant_json]")
{
    REQUIRE(to_json(QVariant(true)) == true);
    REQUIRE(to_json(QVariant(false)) == false);
    REQUIRE(to_json(QVariant(true)).is_boolean());

    REQUIRE(to_json(QVariant(42)) == 42);
    REQUIRE(to_json(QVariant(-42)) == -42);
    REQUIRE(to_json(QVariant(0)) == 0);

    REQUIRE(to_json(QVariant(3.5)) == 3.5);
    REQUIRE(to_json(QVariant(3.5)).is_number_float());
    REQUIRE(to_json(QVariant(1.5f)) == 1.5);

    // Row ids are qint64 in this codebase; narrowing them to int would corrupt
    // every id past 2^31 rather than fail.
    const std::int64_t big = std::numeric_limits<std::int32_t>::max() + std::int64_t{1};
    REQUIRE(to_json(QVariant(static_cast<qlonglong>(big))) == big);
    REQUIRE(to_json(QVariant(static_cast<qlonglong>(-big))) == -big);

    const std::uint64_t big_u = std::uint64_t{1} << 40U;
    REQUIRE(to_json(QVariant(static_cast<qulonglong>(big_u))) == big_u);

    // ``long`` keeps its width too (64 bits on Linux and macOS).
    const long long_max = std::numeric_limits<long>::max();
    REQUIRE(QVariant::fromValue(long_max).typeId() == QMetaType::Long);
    REQUIRE(to_json(QVariant::fromValue(long_max)) == static_cast<std::int64_t>(long_max));
    const unsigned long ulong_max = std::numeric_limits<unsigned long>::max();
    REQUIRE(to_json(QVariant::fromValue(ulong_max)) == static_cast<std::uint64_t>(ulong_max));

    REQUIRE(to_json(QVariant(static_cast<uint>(7))) == 7U);
}

TEST_CASE("to_json maps strings and string lists", "[app][qvariant_json]")
{
    REQUIRE(to_json(QVariant(QStringLiteral("hello"))) == "hello");
    REQUIRE(to_json(QVariant(QStringLiteral(""))).is_string());

    // Non-ASCII must survive the QString -> std::string hop intact.
    REQUIRE(to_json(QVariant(QStringLiteral("Es ist ein Ros entsprungen — éü")))
            == "Es ist ein Ros entsprungen — éü");

    const QStringList list{QStringLiteral("a"), QStringLiteral("b")};
    const nlohmann::json j = to_json(QVariant(list));
    REQUIRE(j.is_array());
    REQUIRE(j.size() == 2);
    REQUIRE(j[0] == "a");
    REQUIRE(j[1] == "b");

    REQUIRE(to_json(QVariant(QStringList{})).is_array());
    REQUIRE(to_json(QVariant(QStringList{})).empty());
}

TEST_CASE("to_json recurses through lists and maps", "[app][qvariant_json]")
{
    // FolderImagesRole and the freeform payloads nest, so the recursion is not
    // hypothetical.
    QVariantMap inner;
    inner.insert(QStringLiteral("id"), 3);
    inner.insert(QStringLiteral("label"), QStringLiteral("verse"));

    QVariantList list;
    list.append(1);
    list.append(QStringLiteral("two"));
    list.append(inner);
    list.append(QVariant());

    QVariantMap outer;
    outer.insert(QStringLiteral("rows"), list);
    outer.insert(QStringLiteral("ok"), true);

    const nlohmann::json j = to_json(QVariant(outer));
    REQUIRE(j.is_object());
    REQUIRE(j["ok"] == true);
    REQUIRE(j["rows"].is_array());
    REQUIRE(j["rows"].size() == 4);
    REQUIRE(j["rows"][0] == 1);
    REQUIRE(j["rows"][1] == "two");
    REQUIRE(j["rows"][2]["id"] == 3);
    REQUIRE(j["rows"][2]["label"] == "verse");
    REQUIRE(j["rows"][3].is_null());

    REQUIRE(to_json(QVariant(QVariantList{})).is_array());
    REQUIRE(to_json(QVariant(QVariantMap{})).is_object());
}

TEST_CASE("to_json reads a JS array or object passed from QML", "[app][qvariant_json]")
{
    // A Q_INVOKABLE taking a QVariant receives a JS value from QML wrapped as a
    // QJSValue. An array must stay an array: theme tags typed in the designer
    // reach ThemeController::normalizeTags this way.
    seventhslide::test::ensure_qt_application();
    QJSEngine engine;
    const nlohmann::json tags =
        to_json(QVariant::fromValue(engine.evaluate(QStringLiteral("['live', 'stage']"))));
    REQUIRE(tags.is_array());
    REQUIRE(tags.size() == 2);
    REQUIRE(tags[0] == "live");
    REQUIRE(tags[1] == "stage");

    const nlohmann::json obj = to_json(
        QVariant::fromValue(engine.evaluate(QStringLiteral("({ name: 'x', ids: [1, 2] })"))));
    REQUIRE(obj.is_object());
    REQUIRE(obj["name"] == "x");
    REQUIRE(obj["ids"].is_array());
    REQUIRE(obj["ids"].size() == 2);
}

TEST_CASE("to_json degrades unhandled types to their string form", "[app][qvariant_json]")
{
    // The contract on the header. A QDateTime or QUrl reaching the remote must
    // arrive as readable text rather than as an empty object.
    const QDateTime when = QDateTime::fromSecsSinceEpoch(1'700'000'000);
    const nlohmann::json j_when = to_json(QVariant(when));
    REQUIRE(j_when.is_string());
    REQUIRE(j_when.get<std::string>() == QVariant(when).toString().toStdString());
    REQUIRE_FALSE(j_when.get<std::string>().empty());

    const QUrl url(QStringLiteral("https://example.org/a?b=c"));
    const nlohmann::json j_url = to_json(QVariant(url));
    REQUIRE(j_url.is_string());
    REQUIRE(j_url == "https://example.org/a?b=c");
}

TEST_CASE("to_qvariant round-trips JSON through QVariant", "[app][qvariant_json]")
{
    // Theme and style edits cross this boundary in both directions; the old
    // facade copy went through QJsonDocument text, which folded every integer
    // into a double.
    const auto json = nlohmann::json::parse(
        R"({"name":"Main","font_size":48,"opacity":0.5,"on":true,"tags":["a","b"],
            "nested":{"x":1},"nothing":null})");
    const QVariant v = to_qvariant(json);
    const QVariantMap map = v.toMap();
    REQUIRE(map.value(QStringLiteral("name")).toString() == QStringLiteral("Main"));
    REQUIRE(map.value(QStringLiteral("font_size")).toInt() == 48);
    REQUIRE(map.value(QStringLiteral("opacity")).toDouble() == 0.5);
    REQUIRE(map.value(QStringLiteral("on")).toBool());
    REQUIRE(map.value(QStringLiteral("tags")).toList().size() == 2);
    REQUIRE(map.value(QStringLiteral("nested")).toMap().value(QStringLiteral("x")).toInt() == 1);
    REQUIRE(map.value(QStringLiteral("nothing")).isNull());

    // Back again, unchanged - including the integer staying an integer.
    const auto again = to_json(v);
    REQUIRE(again == json);
    REQUIRE(again.at("font_size").is_number_integer());
}

TEST_CASE("to_json walks a QVariantHash like a map", "[app][qvariant_json]")
{
    QVariantHash hash;
    hash.insert(QStringLiteral("a"), 1);
    const auto json = to_json(QVariant::fromValue(hash));
    REQUIRE(json.is_object());
    REQUIRE(json.at("a") == 1);
}

TEST_CASE("model_rows projects every row through the listed fields", "[app][qvariant_json]")
{
    seventhslide::test::ensure_qt_application();

    FakeRowModel model;
    // Wire names of the caller's choosing; "flag" is not sent.
    constexpr WireField fields[] = {
        {"name", FakeRowModel::NameRole},
        {"total", FakeRowModel::CountRole},
        {"unanswered", FakeRowModel::UnansweredRole},
    };
    const nlohmann::json rows = model_rows(model, fields);

    REQUIRE(rows.is_array());
    REQUIRE(rows.size() == 2);

    REQUIRE(rows[0]["name"] == "alpha");
    REQUIRE(rows[0]["total"] == 7);
    REQUIRE_FALSE(rows[0].contains("count"));
    REQUIRE_FALSE(rows[0].contains("flag"));
    REQUIRE(rows[1]["name"] == "beta");
    REQUIRE(rows[1]["total"] == 0);

    // A role the model does not answer still yields a key, explicitly null.
    // Dropping the key instead would make the field indistinguishable from one
    // the client build does not know about.
    REQUIRE(rows[0].contains("unanswered"));
    REQUIRE(rows[0]["unanswered"].is_null());

    // Every row carries the same key set.
    for (const nlohmann::json& row : rows) {
        REQUIRE(row.size() == std::size(fields));
    }
}

TEST_CASE("model_rows on an empty model is an empty array", "[app][qvariant_json]")
{
    seventhslide::test::ensure_qt_application();

    EmptyModel model;
    constexpr WireField fields[] = {{"x", Qt::UserRole + 1}};
    const nlohmann::json rows = model_rows(model, fields);
    REQUIRE(rows.is_array());
    REQUIRE(rows.empty());
    // An empty array, not null - the client iterates it without a guard.
    REQUIRE_FALSE(rows.is_null());
}

TEST_CASE("QML ids and positions use -1 for none", "[app][qml]")
{
    using seventhslide::app::optional_id;
    using seventhslide::app::optional_index;
    using seventhslide::app::qml_id;

    STATIC_CHECK_FALSE(optional_id(-1).has_value());
    STATIC_CHECK_FALSE(optional_id(-7).has_value());
    STATIC_CHECK(optional_id(0) == 0);
    STATIC_CHECK(optional_id(42) == 42);
    STATIC_CHECK_FALSE(optional_index(-1).has_value());
    STATIC_CHECK(optional_index(3) == 3);
    STATIC_CHECK(qml_id(std::nullopt) == -1);
    STATIC_CHECK(qml_id(0) == 0);
    STATIC_CHECK(qml_id(optional_id(9)) == 9);
}
