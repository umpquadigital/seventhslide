// Multi-select in the operator's lists, driven with the mouse: click selects
// one row, Ctrl toggles a row, Shift selects from the anchor (Ctrl+Shift adds
// that range), pressing a selected row keeps the selection until release (so
// it can be dragged), and dragging over empty space draws a marquee.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "library_controller.hpp"
#include "media_controller.hpp"
#include "operator_ui.hpp"
#include "facade_all.hpp"
#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"
#include "ui_library_fixture.hpp"
#include "ui_test_helpers.hpp"

#include <seventhslide/db/bible_repository.hpp>
#include <seventhslide/db/media_repository.hpp>
#include <seventhslide/db/song_repository.hpp>
#include <seventhslide/model/content.hpp>

#include <QColor>
#include <QImage>
#include <QSet>
#include <QVariant>

#include <algorithm>
#include <filesystem>
#include <vector>

using seventhslide::app::SessionFacade;
using seventhslide::test::centre_of;
using seventhslide::test::click;
using seventhslide::test::drag;
using seventhslide::test::list_view_of;
using seventhslide::test::OperatorUi;
using seventhslide::test::qml_object;
using seventhslide::test::row_of;
using namespace seventhslide::test::ui;


TEST_CASE("Selecting songs in the library", "[ui][selection]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    const Library lib = build(facade, dir.path);
    OperatorUi ui(facade);
    QObject* panel = ui.find_component("LibraryPanel");
    REQUIRE(panel != nullptr);
    REQUIRE(panel->setProperty("tabIndex", 0));
    QTest::qWait(50);
    QQuickItem* list = list_view_of(panel, facade.library()->property("songs").value<QObject*>());
    REQUIRE(list != nullptr);
    auto* w = ui.window();
    QObject* tab = qml_object(panel, "SongsTab");
    REQUIRE(tab != nullptr);
    const auto selected = [tab] { return id_set(tab->property("selectedSongIds")); };
    const auto current = [tab] { return tab->property("selectedSongId").toLongLong(); };
    const auto& s = lib.songs;

    click(w, centre_of(row_of(list, 0)));
    CHECK(selected() == ids({s[0]}));
    CHECK(current() == s[0]);
    CHECK(tab->property("selectedSongTitle").toString() == QStringLiteral("Alpha"));

    click(w, centre_of(row_of(list, 2)), k_ctrl);
    CHECK(selected() == ids({s[0], s[2]}));
    CHECK(current() == s[2]);

    // Shift reaches from the anchor (the last row clicked), and leaves it there.
    click(w, centre_of(row_of(list, 3)), k_shift);
    CHECK(selected() == ids({s[2], s[3]}));
    click(w, centre_of(row_of(list, 1)), k_shift);
    CHECK(selected() == ids({s[1], s[2]}));
    CHECK(current() == s[1]);

    // Ctrl takes a selected row back out.
    click(w, centre_of(row_of(list, 1)), k_ctrl);
    CHECK(selected() == ids({s[2]}));

    // Ctrl+Shift adds the range to what is selected.
    click(w, centre_of(row_of(list, 0)));
    click(w, centre_of(row_of(list, 3)), k_ctrl);
    click(w, centre_of(row_of(list, 1)), k_ctrl | k_shift);
    CHECK(selected() == ids({s[0], s[1], s[2], s[3]}));

    // A plain click on a selected row keeps the selection while pressed (for
    // a drag) and narrows it to that row on release.
    QTest::mousePress(w, Qt::LeftButton, {}, centre_of(row_of(list, 2)));
    QTest::qWait(10);
    CHECK(selected().size() == 4);
    QTest::mouseRelease(w, Qt::LeftButton, {}, centre_of(row_of(list, 2)));
    QTest::qWait(10);
    CHECK(selected() == ids({s[2]}));

    // A marquee from empty space up into row 1 takes rows 1 to 3.
    drag(w, below_rows(list), centre_of(row_of(list, 1)));
    CHECK(selected() == ids({s[1], s[2], s[3]}));
    // With Ctrl it adds to the selection.
    click(w, centre_of(row_of(list, 0)));
    drag(w, below_rows(list), centre_of(row_of(list, 3)), k_ctrl);
    CHECK(selected() == ids({s[0], s[3]}));
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Selecting Bible verses in the library", "[ui][selection]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    (void)build(facade, dir.path);
    OperatorUi ui(facade);
    QObject* panel = ui.find_component("LibraryPanel");
    REQUIRE(panel != nullptr);
    REQUIRE(panel->setProperty("tabIndex", 2));
    QTest::qWait(50);
    QQuickItem* list =
        list_view_of(panel, facade.library()->property("bibleVerses").value<QObject*>());
    REQUIRE(list != nullptr);
    REQUIRE(list->property("count").toInt() == 5);
    auto* w = ui.window();
    const auto selected = [panel] {
        std::vector<int> out;
        for (const auto& v : qml_object(panel, "BiblesTab")->property("selectedVerses").toList()) {
            out.push_back(v.toInt());
        }
        return out;
    };

    click(w, centre_of(row_of(list, 0)));
    CHECK(selected() == std::vector{16});
    // Verses stay in reading order whatever order they were picked in.
    click(w, centre_of(row_of(list, 4)), k_ctrl);
    click(w, centre_of(row_of(list, 2)), k_ctrl);
    CHECK(selected() == std::vector{16, 18, 20});

    // Shift reaches from the anchor and leaves it there, as in every list.
    click(w, centre_of(row_of(list, 1)));
    click(w, centre_of(row_of(list, 3)), k_shift);
    CHECK(selected() == std::vector{17, 18, 19});
    click(w, centre_of(row_of(list, 0)), k_shift);
    CHECK(selected() == std::vector{16, 17});

    QTest::mousePress(w, Qt::LeftButton, {}, centre_of(row_of(list, 1)));
    QTest::qWait(10);
    CHECK(selected() == std::vector{16, 17});
    QTest::mouseRelease(w, Qt::LeftButton, {}, centre_of(row_of(list, 1)));
    QTest::qWait(10);
    CHECK(selected() == std::vector{17});

    drag(w, below_rows(list), centre_of(row_of(list, 2)));
    CHECK(selected() == std::vector{18, 19, 20});
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Selecting items in the service", "[ui][selection]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    const Library lib = build(facade, dir.path);
    OperatorUi ui(facade);
    QObject* column = ui.find_component("ServicePanel");
    REQUIRE(column != nullptr);
    QTest::qWait(50);
    QQuickItem* list = list_view_of(column, facade.serviceItems());
    REQUIRE(list != nullptr);
    auto* w = ui.window();
    const auto selected = [column] { return id_set(column->property("selectedItemIds")); };
    const auto current = [column] { return column->property("selectedItemId").toLongLong(); };
    const auto& it = lib.items;

    click(w, centre_of(row_of(list, 0)));
    CHECK(selected() == ids({it[0]}));
    CHECK(column->property("selectedItemType").toString() == QStringLiteral("song"));
    click(w, centre_of(row_of(list, 2)), k_ctrl);
    CHECK(selected() == ids({it[0], it[2]}));
    CHECK(current() == it[2]);
    click(w, centre_of(row_of(list, 3)), k_shift);
    CHECK(selected() == ids({it[2], it[3]}));
    click(w, centre_of(row_of(list, 1)), k_shift);
    CHECK(selected() == ids({it[1], it[2]}));

    click(w, centre_of(row_of(list, 0)), k_ctrl | k_shift);
    CHECK(selected() == ids({it[0], it[1], it[2]}));

    QTest::mousePress(w, Qt::LeftButton, {}, centre_of(row_of(list, 1)));
    QTest::qWait(10);
    CHECK(selected().size() == 3);
    QTest::mouseRelease(w, Qt::LeftButton, {}, centre_of(row_of(list, 1)));
    QTest::qWait(10);
    CHECK(selected() == ids({it[1]}));

    drag(w, below_rows(list), centre_of(row_of(list, 2)));
    CHECK(selected() == ids({it[2], it[3]}));
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Selecting images in the media browser", "[ui][selection]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    (void)build(facade, dir.path);
    const auto folder = facade.media_repo()
                            ->folders(seventhslide::db::MediaKind::Image)
                            .create("Backgrounds");
    REQUIRE(folder > 0);
    facade.refresh_library();
    // The browser shows rows or tiles; both select alike.
    const QString mode = GENERATE(QStringLiteral("list"), QStringLiteral("icons"));
    facade.media()->setMediaViewMode(mode);
    INFO("view " << mode.toStdString());
    OperatorUi ui(facade);
    QObject* panel = ui.find_component("LibraryPanel");
    REQUIRE(panel != nullptr);
    REQUIRE(panel->setProperty("tabIndex", 4));
    QTest::qWait(50);
    QObject* browser = nullptr;
    for (QObject* o : panel->findChildren<QObject*>()) {
        if (QByteArray(o->metaObject()->className()).startsWith("MediaBrowser_QMLTYPE_")
            && o->property("isImage").toBool()) {
            browser = o;
        }
    }
    REQUIRE(browser != nullptr);
    QObject* model = facade.media()->browser(seventhslide::db::MediaKind::Image);
    QQuickItem* list = list_view_of(browser, model);
    REQUIRE(list != nullptr);
    // The folder, then a.png .. d.png.
    REQUIRE(list->property("count").toInt() == 5);
    // Tiles exist only for the icon view: a Repeater makes one per file,
    // shown or not.
    const auto tiles_in = [](const auto& self, QQuickItem* item) -> int {
        int n = QByteArray(item->metaObject()->className()).startsWith("MediaBrowserTile_QMLTYPE_");
        for (QQuickItem* child : item->childItems()) {
            n += self(self, child);
        }
        return n;
    };
    auto* browser_item = qobject_cast<QQuickItem*>(browser);
    REQUIRE(browser_item != nullptr);
    CHECK(tiles_in(tiles_in, browser_item) == (mode == QStringLiteral("list") ? 0 : 5));
    auto* w = ui.window();
    const auto files = [browser] {
        QStringList out = browser->property("selectedFilenames").toStringList();
        out.sort();
        return out;
    };
    const auto folders = [browser] { return id_set(browser->property("selectedFolderIds")); };

    click(w, centre_of(row_of(list, 1)));
    CHECK(files() == QStringList{"a.png"});
    CHECK(browser->property("selectedFilename").toString() == QStringLiteral("a.png"));
    click(w, centre_of(row_of(list, 0)), k_ctrl);
    CHECK(folders() == ids({folder}));
    CHECK(files() == QStringList{"a.png"});

    click(w, centre_of(row_of(list, 2)));
    click(w, centre_of(row_of(list, 4)), k_shift);
    CHECK(files() == (QStringList{"b.png", "c.png", "d.png"}));
    CHECK(folders().isEmpty());
    click(w, centre_of(row_of(list, 0)), k_shift);
    CHECK(files() == (QStringList{"a.png", "b.png"}));
    CHECK(folders() == ids({folder}));

    QTest::mousePress(w, Qt::LeftButton, {}, centre_of(row_of(list, 1)));
    QTest::qWait(10);
    CHECK(files().size() == 2);
    QTest::mouseRelease(w, Qt::LeftButton, {}, centre_of(row_of(list, 1)));
    QTest::qWait(10);
    CHECK(files() == QStringList{"a.png"});
    CHECK(folders().isEmpty());

    if (mode == QStringLiteral("list")) {
        drag(w, below_rows(list), centre_of(row_of(list, 3)));
        CHECK(files() == (QStringList{"c.png", "d.png"}));
    } else {
        // Tiles: a box from empty space to the far corner of the last tile
        // covers just that tile.
        QQuickItem* last = row_of(list, 4);
        const QPoint corner =
            last->mapToScene(QPointF(last->width() - 4, last->height() - 4)).toPoint();
        drag(w, corner + QPoint(0, 30), corner);
        CHECK(files() == QStringList{"d.png"});
    }
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Selecting Freeform items in the library", "[ui][selection]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    (void)build(facade, dir.path);
    std::vector<qint64> items;
    for (const char* name : {"One", "Two", "Three"}) {
        items.push_back(facade.freeformLibrary()->createFreeform(QString::fromLatin1(name)));
        REQUIRE(items.back() > 0);
    }
    facade.media()->setMediaViewMode(QStringLiteral("list"));
    OperatorUi ui(facade);
    QObject* panel = ui.find_component("LibraryPanel");
    REQUIRE(panel != nullptr);
    REQUIRE(panel->setProperty("tabIndex", 1));
    QTest::qWait(50);
    QQuickItem* list = list_view_of(panel, facade.freeformLibrary()->property("browser").value<QObject*>());
    REQUIRE(list != nullptr);
    REQUIRE(list->property("count").toInt() == 3);
    QObject* browser = nullptr;
    for (QObject* o : panel->findChildren<QObject*>()) {
        if (QByteArray(o->metaObject()->className()).startsWith("MediaBrowser_QMLTYPE_")
            && o->property("isFreeform").toBool()) {
            browser = o;
        }
    }
    REQUIRE(browser != nullptr);
    auto* w = ui.window();
    const auto selected = [browser] { return id_set(browser->property("selectedItemIds")); };
    // Rows are by name: One, Three, Two.
    const auto row_named = [&](const char* name) {
        for (int r = 0; r < 3; ++r) {
            if (seventhslide::test::visible_text_under(row_of(list, r), QString::fromLatin1(name)))
                return row_of(list, r);
        }
        FAIL("no row " << name);
        return static_cast<QQuickItem*>(nullptr);
    };
    click(w, centre_of(row_named("One")));
    CHECK(selected() == ids({items[0]}));
    click(w, centre_of(row_named("Two")), k_ctrl);
    CHECK(selected() == ids({items[0], items[1]}));
    CHECK(browser->property("selectedItemId").toLongLong() == items[1]);
    CHECK(browser->property("selectedFilenames").toStringList().isEmpty());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// Rows of the rundown move by dragging; a selected group moves together.
TEST_CASE("Dragging rundown rows reorders the service", "[ui][selection][drag]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    const Library lib = build(facade, dir.path);
    OperatorUi ui(facade);
    QObject* column = ui.find_component("ServicePanel");
    REQUIRE(column != nullptr);
    QTest::qWait(50);
    QQuickItem* list = list_view_of(column, facade.serviceItems());
    REQUIRE(list != nullptr);
    auto* w = ui.window();
    const auto order = [&facade] {
        std::vector<qint64> out;
        for (int r = 0; r < facade.serviceItems()->rowCount(); ++r) {
            out.push_back(facade.serviceItems()
                              ->data(facade.serviceItems()->index(r, 0),
                                     seventhslide::app::ServiceItemModel::ItemIdRole)
                              .toLongLong());
        }
        return out;
    };
    const auto lower_half = [](QQuickItem* row) {
        return row->mapToScene(QPointF(row->width() / 2, row->height() * 0.8)).toPoint();
    };
    const auto& it = lib.items;

    drag(w, centre_of(row_of(list, 0)), lower_half(row_of(list, 2)));
    CHECK(order() == std::vector{it[1], it[2], it[0], it[3]});

    // Rows 0 and 1 (items 1 and 2) selected, dragged by row 1 to the end.
    click(w, centre_of(row_of(list, 0)));
    click(w, centre_of(row_of(list, 1)), k_ctrl);
    drag(w, centre_of(row_of(list, 1)), lower_half(row_of(list, 3)));
    CHECK(order() == std::vector{it[0], it[3], it[1], it[2]});
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}
