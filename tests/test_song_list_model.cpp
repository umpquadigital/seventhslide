#include <catch2/catch_test_macros.hpp>

#include "song_list_model.hpp"
#include "test_qt_app.hpp"

#include <seventhslide/model/content.hpp>

#include <QAbstractItemModel>
#include <QObject>
#include <QString>

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

using seventhslide::app::SongListModel;
using seventhslide::model::Song;

namespace {

Song make_song(std::int64_t id, std::string title,
               std::vector<seventhslide::model::Author> authors = {},
               std::string songbook = {}, std::string entry = {})
{
    Song s;
    s.id = id;
    s.title = std::move(title);
    s.authors = std::move(authors);
    s.songbook_name = std::move(songbook);
    s.songbook_entry = std::move(entry);
    return s;
}

QString title_at(const SongListModel& model, int row)
{
    return model.data(model.index(row), SongListModel::TitleRole).toString();
}

} // namespace

TEST_CASE("SongListModel filter exposes only matching rows", "[app][songs]")
{
    seventhslide::test::ensure_qt_application();
    SongListModel model;
    model.set_songs({
        make_song(1, "Amazing Grace", {"Newton"}, "Hymnal", "45"),
        make_song(2, "How Great Thou Art", {"Hine"}),
        make_song(3, "Holy Holy Holy", {}, "Grace Hymns"),
    });
    REQUIRE(model.rowCount() == 3);

    model.setFilter(QStringLiteral("grace"));
    REQUIRE(model.rowCount() == 2);
    REQUIRE(title_at(model, 0) == QStringLiteral("Amazing Grace"));
    REQUIRE(title_at(model, 1) == QStringLiteral("Holy Holy Holy"));

    model.setFilter(QStringLiteral("HINE"));
    REQUIRE(model.rowCount() == 1);
    REQUIRE(title_at(model, 0) == QStringLiteral("How Great Thou Art"));
    REQUIRE(model.data(model.index(0), SongListModel::SongIdRole).toLongLong() == 2);

    // A songbook's number finds its song, though the list shows only authors.
    model.setFilter(QStringLiteral("#45"));
    REQUIRE(model.rowCount() == 1);
    REQUIRE(title_at(model, 0) == QStringLiteral("Amazing Grace"));

    model.setFilter(QStringLiteral("   "));
    REQUIRE(model.rowCount() == 3);

    model.setFilter(QStringLiteral("nope"));
    REQUIRE(model.rowCount() == 0);

    model.setFilter(QStringLiteral("holy"));
    model.set_songs({
        make_song(1, "Amazing Grace", {"Newton"}, "Hymnal", "45"),
        make_song(2, "How Great Thou Art", {"Hine"}),
        make_song(3, "Holy Holy Holy", {}, "Grace Hymns"),
    });
    REQUIRE(model.filter() == QStringLiteral("holy"));
    REQUIRE(model.rowCount() == 1);
    REQUIRE(title_at(model, 0) == QStringLiteral("Holy Holy Holy"));

    model.setFilter(QString());
    REQUIRE(model.rowCount() == 3);
}

TEST_CASE("SongListModel finds a song by one of its topics", "[app][songs]")
{
    seventhslide::test::ensure_qt_application();
    SongListModel model;
    auto advent = make_song(1, "O Come, O Come Emmanuel");
    advent.topics = {"Advent", "Second Coming"};
    auto grace = make_song(2, "Amazing Grace");
    grace.topics = {"Assurance"};
    model.set_songs({advent, grace});

    model.setFilter(QStringLiteral("second com"));
    REQUIRE(model.rowCount() == 1);
    CHECK(title_at(model, 0) == QStringLiteral("O Come, O Come Emmanuel"));
    model.setFilter(QStringLiteral("assurance"));
    REQUIRE(model.rowCount() == 1);
    CHECK(title_at(model, 0) == QStringLiteral("Amazing Grace"));
    // Not across two topics.
    model.setFilter(QStringLiteral("advent second"));
    CHECK(model.rowCount() == 0);
}

TEST_CASE("SongListModel rename does not reset the model", "[app][songs]")
{
    seventhslide::test::ensure_qt_application();
    SongListModel model;
    int resets = 0;
    int changes = 0;
    QObject::connect(&model, &QAbstractItemModel::modelReset, [&] { ++resets; });
    QObject::connect(&model, &QAbstractItemModel::dataChanged, [&] { ++changes; });

    model.set_songs({
        make_song(1, "Amazing Grace"),
        make_song(2, "How Great Thou Art"),
        make_song(3, "Holy Holy Holy"),
    });
    REQUIRE(resets == 1);
    REQUIRE(model.rowCount() == 3);

    model.set_songs({
        make_song(1, "Amazing Grace"),
        make_song(2, "How Great Thou Art (live)"),
        make_song(3, "Holy Holy Holy"),
    });
    REQUIRE(resets == 1);
    REQUIRE(changes >= 1);
    // The model sorts by title itself: Amazing, Holy, How.
    REQUIRE(title_at(model, 2) == QStringLiteral("How Great Thou Art (live)"));
    REQUIRE(model.data(model.index(2), SongListModel::SongIdRole).toLongLong() == 2);

    // Same identities in a new order still keep the view, in title order.
    const int changes_before_sort = changes;
    model.set_songs({
        make_song(3, "Holy Holy Holy"),
        make_song(1, "Amazing Grace"),
        make_song(2, "How Great Thou Art (live)"),
    });
    REQUIRE(resets == 1);
    REQUIRE(changes > changes_before_sort);
    REQUIRE(title_at(model, 0) == QStringLiteral("Amazing Grace"));
    REQUIRE(title_at(model, 1) == QStringLiteral("Holy Holy Holy"));

    model.set_songs({
        make_song(1, "Amazing Grace"),
        make_song(2, "How Great Thou Art (live)"),
    });
    // One gone: that row alone.
    REQUIRE(resets == 1);
    REQUIRE(model.rowCount() == 2);
}

TEST_CASE("SongListModel changes by the rows that came and went, and keeps the view", "[app][songs]")
{
    seventhslide::test::ensure_qt_application();
    SongListModel model;
    model.set_songs({make_song(1, "Beulah Land"), make_song(2, "Crown Him"), make_song(3, "Doxology")});
    int resets = 0;
    std::vector<std::pair<int, int>> inserted;
    std::vector<std::pair<int, int>> removed;
    QObject::connect(&model, &QAbstractItemModel::modelReset, [&] { ++resets; });
    QObject::connect(&model, &QAbstractItemModel::rowsInserted,
                     [&](const QModelIndex&, int first, int last) { inserted.emplace_back(first, last); });
    QObject::connect(&model, &QAbstractItemModel::rowsRemoved,
                     [&](const QModelIndex&, int first, int last) { removed.emplace_back(first, last); });

    // A song added above the others, and one gone below them, as a sync brings.
    model.change_songs({make_song(4, "All Hail the Power")}, {3});
    CHECK(resets == 0);
    CHECK(removed == std::vector<std::pair<int, int>>{{2, 2}});
    CHECK(inserted == std::vector<std::pair<int, int>>{{0, 0}});
    REQUIRE(model.rowCount() == 3);
    CHECK(title_at(model, 0) == QStringLiteral("All Hail the Power"));
    CHECK(title_at(model, 1) == QStringLiteral("Beulah Land"));
    CHECK(title_at(model, 2) == QStringLiteral("Crown Him"));

    // Renamed so it moves, while another is added: the row goes and comes
    // back, and the others stay.
    inserted.clear();
    removed.clear();
    model.change_songs({make_song(1, "Zion"), make_song(5, "Blessed Assurance")}, {});
    CHECK(resets == 0);
    CHECK(removed == std::vector<std::pair<int, int>>{{1, 1}});
    CHECK(inserted == std::vector<std::pair<int, int>>{{1, 1}, {3, 3}});
    CHECK(title_at(model, 0) == QStringLiteral("All Hail the Power"));
    CHECK(title_at(model, 1) == QStringLiteral("Blessed Assurance"));
    CHECK(title_at(model, 2) == QStringLiteral("Crown Him"));
    CHECK(title_at(model, 3) == QStringLiteral("Zion"));

    // Within a search, the same.
    model.setFilter(QStringLiteral("o"));
    resets = 0;
    const int shown = model.rowCount();
    model.change_songs({make_song(6, "Over the Sunset")}, {});
    CHECK(resets == 0);
    CHECK(model.rowCount() == shown + 1);

    // Every row replaced is another list.
    model.setFilter({});
    resets = 0;
    model.set_songs({make_song(7, "Rock of Ages")});
    CHECK(resets == 1);
    CHECK(model.rowCount() == 1);
}

TEST_CASE("SongListModel keeps the view through a refresh of the same rows", "[app][songs]")
{
    seventhslide::test::ensure_qt_application();
    SongListModel model;
    int resets = 0;
    int about = 0;
    int changes = 0;
    QObject::connect(&model, &QAbstractItemModel::modelReset, [&] { ++resets; });
    // Every reset is announced first: a view drops its rows only after it.
    QObject::connect(&model, &QAbstractItemModel::modelAboutToBeReset, [&] {
        CHECK(about == resets);
        ++about;
    });
    QObject::connect(&model, &QAbstractItemModel::dataChanged, [&] { ++changes; });

    // Empty to empty says nothing.
    model.set_songs({});
    CHECK(resets == 0);
    CHECK(changes == 0);

    model.set_songs({make_song(1, "Amazing Grace"), make_song(2, "Be Thou My Vision")});
    REQUIRE(resets == 1);
    model.setFilter(QStringLiteral("vision"));
    REQUIRE(resets == 2);
    REQUIRE(model.rowCount() == 1);
    // The same visible song: rows updated in place.
    model.set_songs({make_song(1, "Amazing Grace"), make_song(2, "Be Thou My Vision")});
    CHECK(resets == 2);
    CHECK(changes == 1);
    // A different song now passes the filter: a reset.
    model.set_songs({make_song(1, "Amazing Grace"), make_song(3, "Vision of Glory")});
    CHECK(resets == 3);
    CHECK(title_at(model, 0) == QStringLiteral("Vision of Glory"));
    // Unchanged filter text does nothing.
    model.setFilter(QStringLiteral("vision"));
    CHECK(resets == 3);
    CHECK(about == resets);
}

namespace {

Song with(Song s, std::string lyrics, std::vector<std::string> topics = {}, std::string key = {})
{
    s.lyrics = std::move(lyrics);
    s.topics = std::move(topics);
    s.key = std::move(key);
    return s;
}

QString line_at(const SongListModel& model, int row)
{
    return model.data(model.index(row), SongListModel::MatchedLineRole).toString();
}

} // namespace

TEST_CASE("SongListModel finds a song by a line of its lyrics", "[app][songs]")
{
    seventhslide::test::ensure_qt_application();
    SongListModel model;
    model.set_songs({
        with(make_song(1, "Rock of Ages", {"Toplady"}),
             "---[Verse:1]---\nRock of ages, cleft for me,\nLet me hide myself in Thee\n\n"
             "---[Verse:2]---\nCould my [G]tears for <b>ever</b> flow,\n[---][---]\nAll for sin"),
        with(make_song(2, "Amazing Grace", {"Newton"}),
             "Amazing grace! How sweet the sound\nThat saved a wretch like me"),
        with(make_song(3, "Sweet Hour of Prayer"), "Sweet hour of prayer"),
    });

    // Chords and markup are not in the way, nor case or punctuation.
    model.setFilter(QStringLiteral("TEARS FOR EVER FLOW"));
    REQUIRE(model.rowCount() == 1);
    CHECK(title_at(model, 0) == QStringLiteral("Rock of Ages"));
    CHECK(line_at(model, 0) == QStringLiteral("Could my tears for ever flow,"));

    // Across the end of a line.
    model.setFilter(QStringLiteral("cleft for me let me hide"));
    REQUIRE(model.rowCount() == 1);
    CHECK(line_at(model, 0) == QStringLiteral("Rock of ages, cleft for me,"));

    // Titles that match come before lyrics that do, and name no line.
    model.setFilter(QStringLiteral("sweet"));
    REQUIRE(model.rowCount() == 2);
    CHECK(title_at(model, 0) == QStringLiteral("Sweet Hour of Prayer"));
    CHECK(line_at(model, 0).isEmpty());
    CHECK(title_at(model, 1) == QStringLiteral("Amazing Grace"));
    CHECK(line_at(model, 1) == QStringLiteral("Amazing grace! How sweet the sound"));

    // A section header is not a lyric.
    model.setFilter(QStringLiteral("verse"));
    CHECK(model.rowCount() == 0);
}

TEST_CASE("SongListModel finds a song by its songbook number, topic or key", "[app][songs]")
{
    seventhslide::test::ensure_qt_application();
    SongListModel model;
    model.set_songs({
        with(make_song(1, "Joy to the World", {}, "Hymnal", "45"), "", {"Christmas"}, "D"),
        with(make_song(2, "Silent Night", {}, "Hymnal", "145"), "", {"Christmas"}, "Bb"),
        with(make_song(3, "Be Thou My Vision", {}, "Psalter", "450"), "", {"Devotion"}, "Eb"),
        with(make_song(4, "Here I Am Lord", {}, "Hymnal", "45a"), "", {}, "F#m"),
    });
    const auto titles = [&] {
        QStringList out;
        for (int r = 0; r < model.rowCount(); ++r) {
            out.push_back(title_at(model, r));
        }
        return out;
    };

    // A number whole: 45 and 45a, not 145 or 450.
    model.setFilter(QStringLiteral("45"));
    CHECK(titles() == QStringList{"Here I Am Lord", "Joy to the World"});
    model.setFilter(QStringLiteral("hymnal #145"));
    CHECK(titles() == QStringList{"Silent Night"});
    model.setFilter(QStringLiteral("psalter"));
    CHECK(titles() == QStringList{"Be Thou My Vision"});

    model.setFilter(QStringLiteral("christmas"));
    CHECK(titles() == QStringList{"Joy to the World", "Silent Night"});

    // A key whole, with or without "key", flats and sharps either way.
    model.setFilter(QStringLiteral("key bb"));
    CHECK(titles() == QStringList{"Silent Night"});
    // A key alone lists the songs in it first, then titles with the letter.
    model.setFilter(QStringLiteral("B\u266d"));
    CHECK(titles() == QStringList{"Silent Night", "Be Thou My Vision"});
    model.setFilter(QStringLiteral("key: f#m"));
    CHECK(titles() == QStringList{"Here I Am Lord"});
    // No song is in E: E-flat is not E.
    model.setFilter(QStringLiteral("key e"));
    CHECK(model.rowCount() == 0);
}

TEST_CASE("SongListModel changes only the rows of the songs that changed", "[app][songs]")
{
    seventhslide::test::ensure_qt_application();
    SongListModel model;
    model.set_songs({with(make_song(1, "Amazing Grace"), "[V1]\nHow sweet the sound"),
                     with(make_song(2, "Be Thou My Vision"), "[V1]\nO Lord of my heart"),
                     with(make_song(3, "Crown Him"), "[V1]\nWith many crowns")});
    int resets = 0;
    std::vector<std::pair<int, int>> changed;
    QObject::connect(&model, &QAbstractItemModel::modelReset, [&] { ++resets; });
    QObject::connect(&model, &QAbstractItemModel::dataChanged,
                     [&](const QModelIndex& from, const QModelIndex& to) {
                         changed.emplace_back(from.row(), to.row());
                     });

    // New words, the same place in the list: that row alone, and the search
    // finds the new words.
    model.change_songs({with(make_song(2, "Be Thou My Vision"), "[V1]\nHigh King of Heaven")}, {});
    CHECK(resets == 0);
    CHECK(changed == std::vector<std::pair<int, int>>{{1, 1}});
    model.setFilter(QStringLiteral("high king"));
    REQUIRE(model.rowCount() == 1);
    CHECK(line_at(model, 0) == QStringLiteral("High King of Heaven"));
    model.setFilter({});
    resets = 0;
    changed.clear();

    // A new title moves the song: every row is told, and the view kept.
    model.change_songs({make_song(1, "Down to the River")}, {});
    CHECK(resets == 0);
    CHECK(changed == std::vector<std::pair<int, int>>{{0, 2}});
    CHECK(title_at(model, 2) == QStringLiteral("Down to the River"));
    changed.clear();

    // One added and one gone: those rows, and the view kept.
    model.change_songs({make_song(4, "All Hail the Power")}, {3});
    CHECK(resets == 0);
    REQUIRE(model.rowCount() == 3);
    CHECK(title_at(model, 0) == QStringLiteral("All Hail the Power"));
    CHECK(title_at(model, 1) == QStringLiteral("Be Thou My Vision"));
    auto ids = model.song_ids();
    std::ranges::sort(ids);
    CHECK(ids == std::vector<std::int64_t>{1, 2, 4});

    // Nothing changed: nothing said.
    changed.clear();
    model.change_songs({}, {});
    CHECK(resets == 0);
    CHECK(changed.empty());
}
