#pragma once

// A small library for the operator-window UI tests: four songs, a Bible
// chapter, four images and a service holding the songs.

#include <catch2/catch_test_macros.hpp>

#include "library_controller.hpp"
#include "media_controller.hpp"
#include "service_item_model.hpp"
#include "facade_all.hpp"

#include <seventhslide/db/bible_repository.hpp>
#include <seventhslide/db/media_repository.hpp>
#include <seventhslide/db/song_repository.hpp>
#include <seventhslide/model/content.hpp>

#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QQuickItem>
#include <QSet>
#include <QVariant>

#include <filesystem>
#include <initializer_list>
#include <string>
#include <vector>

namespace seventhslide::test::ui {

using app::SessionFacade;

inline constexpr auto k_ctrl = Qt::ControlModifier;
inline constexpr auto k_shift = Qt::ShiftModifier;

struct Library {
    std::vector<qint64> songs;  // in list order (by title)
    std::vector<qint64> items;  // service item ids, in rundown order
};

inline Library build(SessionFacade& facade, const std::filesystem::path& dir)
{
    Library lib;
    REQUIRE(facade.open_database(dir / "library.db"));
    for (const char* title : {"Alpha", "Bravo", "Charlie", "Delta"}) {
        seventhslide::model::Song song;
        song.title = title;
        song.lyrics = "[V1]\nLine";
        song.verse_order = "v1";
        lib.songs.push_back(facade.song_repo()->insert(song));
    }
    std::vector<seventhslide::model::BibleVerse> verses;
    for (int n = 16; n <= 20; ++n) {
        seventhslide::model::BibleVerse v;
        v.book = "John";
        v.chapter = 3;
        v.verse_num = n;
        v.text = "Verse " + std::to_string(n);
        verses.push_back(v);
    }
    const auto bible = facade.bible_repo()->import_bible("Test Bible", "", verses);
    REQUIRE(facade.library()->selectBible(bible));
    REQUIRE(facade.library()->resolveBibleRef(QStringLiteral("John 3"))
                .value(QStringLiteral("success"))
                .toBool());

    const auto images = dir / "media" / "images";
    std::filesystem::create_directories(images);
    for (const char* name : {"a.png", "b.png", "c.png", "d.png"}) {
        QImage img(32, 18, QImage::Format_RGB32);
        img.fill(Qt::darkCyan);
        REQUIRE(img.save(QString::fromStdString((images / name).string())));
        facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Image, name, name);
    }
    facade.refresh_library();

    REQUIRE(facade.library()->createService(QStringLiteral("Sunday")) > 0);
    for (const auto id : lib.songs) {
        REQUIRE(facade.library()->addSongToService(id));
    }
    for (int r = 0; r < facade.serviceItems()->rowCount(); ++r) {
        lib.items.push_back(
            facade.serviceItems()
                ->data(facade.serviceItems()->index(r, 0),
                       seventhslide::app::ServiceItemModel::ItemIdRole)
                .toLongLong());
    }
    REQUIRE(lib.items.size() == 4);
    return lib;
}

inline QSet<qint64> id_set(const QVariant& v)
{
    QSet<qint64> out;
    for (const auto& x : v.toList()) {
        out.insert(x.toLongLong());
    }
    return out;
}

inline QSet<qint64> ids(std::initializer_list<qint64> list)
{
    return QSet<qint64>(list.begin(), list.end());
}

/// A point inside ``view`` below its last row: empty space, where a marquee starts.
inline QPoint below_rows(QQuickItem* view)
{
    return view->mapToScene(QPointF(view->width() / 2, view->height() - 6)).toPoint();
}


}  // namespace seventhslide::test::ui
