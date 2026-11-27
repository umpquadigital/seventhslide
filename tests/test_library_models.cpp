#include <catch2/catch_test_macros.hpp>

#include "library_models.hpp"
#include "list_model_replace.hpp"
#include "test_qt_app.hpp"

#include <seventhslide/model/library.hpp>

#include <QAbstractItemModel>
#include <QObject>
#include <QString>

using seventhslide::app::BibleListModel;
using seventhslide::app::MediaBrowserEntry;
using seventhslide::app::MediaBrowserModel;
using seventhslide::model::Bible;

namespace {

Bible make_bible(std::int64_t id, std::string name)
{
    Bible b;
    b.id = id;
    b.name = std::move(name);
    return b;
}

MediaBrowserEntry make_file(std::int64_t id, std::string filename, std::string name)
{
    MediaBrowserEntry e;
    e.kind = MediaBrowserEntry::Kind::File;
    e.id = id;
    e.filename = std::move(filename);
    e.name = std::move(name);
    return e;
}

} // namespace

TEST_CASE("BibleListModel rename does not reset the model", "[app][library-models]")
{
    seventhslide::test::ensure_qt_application();
    BibleListModel model;
    int resets = 0;
    QObject::connect(&model, &QAbstractItemModel::modelReset, [&] { ++resets; });

    model.set_bibles({make_bible(10, "KJV"), make_bible(11, "ESV")});
    REQUIRE(resets == 1);

    model.set_bibles({make_bible(10, "Authorized"), make_bible(11, "ESV")});
    REQUIRE(resets == 1);
    REQUIRE(model.data(model.index(0), BibleListModel::NameRole).toString()
            == QStringLiteral("Authorized"));

    model.set_bibles({make_bible(10, "Authorized"), make_bible(11, "ESV"),
                      make_bible(12, "NIV")});
    REQUIRE(resets == 2);
}

TEST_CASE("MediaBrowserModel rename does not reset the model", "[app][library-models]")
{
    seventhslide::test::ensure_qt_application();
    MediaBrowserModel model;
    int resets = 0;
    int changes = 0;
    QObject::connect(&model, &QAbstractItemModel::modelReset, [&] { ++resets; });
    QObject::connect(&model, &QAbstractItemModel::dataChanged, [&] { ++changes; });

    model.set_entries({make_file(1, "a.jpg", "Alpha"), make_file(2, "b.jpg", "Beta")});
    REQUIRE(resets == 1);

    model.set_entries({make_file(1, "a.jpg", "Alpha renamed"), make_file(2, "b.jpg", "Beta")});
    REQUIRE(resets == 1);
    REQUIRE(changes >= 1);
    REQUIRE(model.data(model.index(0), MediaBrowserModel::NameRole).toString()
            == QStringLiteral("Alpha renamed"));

    // One gone, and one come: those rows alone.
    int inserted = 0;
    int removed = 0;
    QObject::connect(&model, &QAbstractItemModel::rowsInserted, [&] { ++inserted; });
    QObject::connect(&model, &QAbstractItemModel::rowsRemoved, [&] { ++removed; });
    model.set_entries({make_file(1, "a.jpg", "Alpha renamed")});
    REQUIRE(resets == 1);
    REQUIRE(removed == 1);
    model.set_entries({make_file(3, "0.jpg", "First"), make_file(1, "a.jpg", "Alpha renamed")});
    REQUIRE(resets == 1);
    REQUIRE(inserted == 1);
    REQUIRE(model.data(model.index(0), MediaBrowserModel::NameRole).toString() == QStringLiteral("First"));

    // Another folder, or a search, starts over though a row is in both.
    model.set_entries({make_file(3, "0.jpg", "First")}, QStringLiteral("7/"));
    REQUIRE(resets == 2);
    model.set_entries({make_file(3, "0.jpg", "First"), make_file(4, "1.jpg", "Second")}, QStringLiteral("7/"));
    REQUIRE(resets == 2);
    model.set_entries({make_file(3, "0.jpg", "First")}, QStringLiteral("7/fir"));
    REQUIRE(resets == 3);
}

TEST_CASE("kept_rows keeps as many rows as hold their order", "[app][library-models]")
{
    using Keys = std::vector<int>;
    const auto kept = [](const Keys& before, const Keys& after) {
        return seventhslide::app::kept_rows(before, after);
    };
    const auto marks = [](std::initializer_list<int> list) {
        std::vector<char> out;
        for (const int mark : list) {
            out.push_back(static_cast<char>(mark));
        }
        return out;
    };

    // Rows come and gone around those that stay.
    auto rows = kept({1, 2, 3}, {0, 1, 3, 4});
    REQUIRE(rows.has_value());
    CHECK(rows->before == marks({1, 0, 1}));
    CHECK(rows->after == marks({0, 1, 1, 0}));

    // The row that moved goes, not the many it moved past.
    rows = kept({1, 2, 3, 4, 5}, {2, 3, 4, 5, 1, 6});
    REQUIRE(rows.has_value());
    CHECK(rows->before == marks({0, 1, 1, 1, 1}));
    CHECK(rows->after == marks({1, 1, 1, 1, 0, 0}));

    // Nothing in common, or keys that repeat: not the same list.
    CHECK_FALSE(kept({1, 2}, {3, 4}).has_value());
    CHECK_FALSE(kept({}, {1}).has_value());
    CHECK_FALSE(kept({1}, {}).has_value());
    CHECK_FALSE(kept({1, 1, 2}, {1, 2}).has_value());
    CHECK_FALSE(kept({1, 2}, {1, 2, 2}).has_value());
}

TEST_CASE("MediaBrowserModel freeform-shaped entries keep id and thumb", "[app][library-models]")
{
    seventhslide::test::ensure_qt_application();
    MediaBrowserModel model;
    MediaBrowserEntry e;
    e.kind = MediaBrowserEntry::Kind::File;
    e.id = 42;
    e.name = "Welcome";
    e.thumb_url = "image://freeformthumb/42?k=abc&t=1";
    model.set_entries({e});
    REQUIRE(model.rowCount() == 1);
    REQUIRE(model.data(model.index(0), MediaBrowserModel::IdRole).toLongLong() == 42);
    REQUIRE(model.data(model.index(0), MediaBrowserModel::FilenameRole).toString().isEmpty());
    REQUIRE(model.data(model.index(0), MediaBrowserModel::ThumbUrlRole).toString()
                .startsWith(QStringLiteral("image://freeformthumb/42")));
    REQUIRE(model.entryIds(QStringLiteral("file")).size() == 1);
}
