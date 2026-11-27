// The library toolbar acts on the selection of the tab showing: add to the
// service, edit, duplicate, delete, save as Freeform. Each test selects with
// the mouse and reads the result from the session.

#include <catch2/catch_test_macros.hpp>

#include "fake_live_receiver.hpp"
#include "fake_pointer_confinement.hpp"
#include "fake_web_renderer.hpp"
#include "live_library_controller.hpp"
#include "live_sources.hpp"
#include "web_sources.hpp"
#include "slide_edit_session.hpp"
#include "freeform_library_controller.hpp"
#include "freeform_import_controller.hpp"
#include "media_controller.hpp"
#include "media_import_helpers.hpp"
#include "operator_ui.hpp"
#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"
#include "ui_library_fixture.hpp"
#include "ui_test_helpers.hpp"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QMouseEvent>
#include <QQuickItem>
#include <QSet>
#include <QSettings>
#include <QSignalSpy>
#include <QVariantList>
#include <QUrl>
#include <QWheelEvent>

#include <seventhslide/exchange/bundle_io.hpp>
#include <seventhslide/freeform/doc_edit.hpp>
#include <seventhslide/model/output_library.hpp>

#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <vector>

using seventhslide::test::centre_of;
using seventhslide::test::click;
using seventhslide::test::list_view_of;
using seventhslide::app::MediaBrowserModel;
using seventhslide::test::OperatorUi;
using seventhslide::test::qml_object;
using seventhslide::test::row_of;
using seventhslide::test::visible_items_of;
using seventhslide::test::visible_text_under;
using namespace seventhslide::test::ui;

namespace {

QStringList rundown_titles(SessionFacade& facade)
{
    QStringList out;
    auto* m = facade.serviceItems();
    for (int r = 0; r < m->rowCount(); ++r) {
        out.push_back(m->data(m->index(r, 0), seventhslide::app::ServiceItemModel::TitleRole)
                          .toString());
    }
    return out;
}

void call(QObject* o, const char* method)
{
    INFO("method " << method);
    REQUIRE(QMetaObject::invokeMethod(o, method));
    QTest::qWait(10);
}

/// The tab the library panel is showing (a LibraryTab), whose toolbar
/// actions the buttons run.
QObject* active_tab(QObject* panel)
{
    QObject* tab = panel->property("activeTab").value<QObject*>();
    REQUIRE(tab != nullptr);
    return tab;
}

/// Ask the toolbar to delete, and accept the confirm it shows.
void delete_confirmed(QObject* panel)
{
    call(panel, "toolDelete");
    QObject* confirm = nullptr;
    for (QObject* o : panel->findChildren<QObject*>()) {
        if (QByteArray(o->metaObject()->className()).startsWith("AppDialog_QMLTYPE_")
            && o->property("open").toBool()) {
            confirm = o;
        }
    }
    REQUIRE(confirm != nullptr);
    call(confirm, "submit");
    CHECK_FALSE(confirm->property("open").toBool());
}

/// The MediaBrowser whose ``is_kind`` flag (isImage, isAudio, isFreeform...) is set.
QObject* browser_of(QObject* panel, const char* is_kind)
{
    for (QObject* o : panel->findChildren<QObject*>()) {
        if (QByteArray(o->metaObject()->className()).startsWith("MediaBrowser_QMLTYPE_")
            && o->property(is_kind).toBool()) {
            return o;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("Library toolbar on songs", "[ui][library-tools]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    const Library lib = build(facade, dir.path);
    // Not in the service, so it can be deleted.
    seventhslide::model::Song spare;
    spare.title = "Echo";
    spare.lyrics = "[V1]\nLine";
    const auto echo = facade.song_repo()->insert(spare);
    facade.refresh_library();
    OperatorUi ui(facade);
    QObject* panel = ui.find_component("LibraryPanel");
    REQUIRE(panel != nullptr);
    REQUIRE(panel->setProperty("tabIndex", 0));
    QTest::qWait(50);
    QQuickItem* list = list_view_of(panel, facade.library()->property("songs").value<QObject*>());
    REQUIRE(list != nullptr);
    auto* w = ui.window();

    // Add: the selected songs go to the end of the service.
    click(w, centre_of(row_of(list, 0)));
    click(w, centre_of(row_of(list, 2)), k_ctrl);
    REQUIRE(panel->property("canAdd").toBool());
    const int before = facade.serviceItems()->rowCount();
    call(panel, "toolAdd");
    const QStringList titles = rundown_titles(facade);
    REQUIRE(titles.size() == before + 2);
    CHECK(titles.mid(before) == (QStringList{"Alpha", "Charlie"}));

    // Insert adds what is selected, as Add does.
    click(w, centre_of(row_of(list, 3)));
    QTest::keyClick(w, Qt::Key_Insert);
    CHECK(rundown_titles(facade).size() == before + 3);
    // And so does Ctrl+Return (Command+Return), for a keyboard with no Insert.
    QTest::keyClick(w, Qt::Key_Return, Qt::ControlModifier);
    CHECK(rundown_titles(facade).size() == before + 4);

    // Edit opens the song editor on the one selected.
    click(w, centre_of(row_of(list, 1)));
    REQUIRE(panel->property("canEdit").toBool());
    call(active_tab(panel), "editSelected");
    QObject* editor = qml_object(panel, "SongEditDialog");
    REQUIRE(editor != nullptr);
    CHECK(editor->property("open").toBool());
    REQUIRE(QMetaObject::invokeMethod(editor, "close"));
    QTest::qWait(20);

    // Delete asks first, then deletes the selection.
    click(w, centre_of(row_of(list, 4)));
    REQUIRE(panel->property("canDelete").toBool());
    delete_confirmed(panel);
    CHECK(facade.library()->getSong(echo).value(QStringLiteral("title")).toString().isEmpty());
    CHECK(active_tab(panel)->property("selectedSongIds").toList().isEmpty());

    // Save as Freeform: one song, named after it. (It opens the save dialog.)
    click(w, centre_of(row_of(list, 1)));
    REQUIRE(panel->property("canConvert").toBool());
    QSignalSpy convert(panel, SIGNAL(requestSaveAsFreeform(int, QString)));
    call(active_tab(panel), "convertSelected");
    REQUIRE(convert.size() == 1);
    CHECK(convert.front().at(0).toLongLong() == lib.songs[1]);
    CHECK(convert.front().at(1).toString() == QStringLiteral("Bravo (edit)"));
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("One song file imports at once, several ask which", "[ui][library-tools]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    (void)build(facade, dir.path);
    const auto unreadable = dir.path / "Sunday set.pro";
    {
        std::ofstream out(unreadable, std::ios::binary);
        out.write("\x0a\x05hi\0\x12", 6);
    }
    const auto fixtures = std::filesystem::path{SEVENTHSLIDE_FIXTURES_DIR} / "songs" / "import";
    const auto path_of = [](const std::filesystem::path& p) {
        return QString::fromStdString(p.string());
    };
    OperatorUi ui(facade);
    QObject* panel = ui.find_component("LibraryPanel");
    REQUIRE(panel != nullptr);
    REQUIRE(panel->setProperty("tabIndex", 0));
    QTest::qWait(50);
    QObject* tab = ui.find_component("SongsTab");
    QObject* window = ui.find_component("SongImportDialog");
    REQUIRE(tab != nullptr);
    REQUIRE(window != nullptr);
    auto* songs = facade.library()->songs();
    const int before = songs->rowCount();

    // One file: no window.
    const auto pick = [&](const QStringList& paths) {
        REQUIRE(QMetaObject::invokeMethod(tab, "importPicked", Q_ARG(QVariant, QVariant(paths))));
    };
    pick({path_of(fixtures / "songselect_2023.txt")});
    CHECK_FALSE(window->property("open").toBool());
    CHECK(songs->rowCount() == before + 1);

    // One file that gives no song says why, with no window either.
    pick({path_of(unreadable)});
    CHECK_FALSE(window->property("open").toBool());
    auto* report = tab->findChild<QObject*>(QStringLiteral("songImportReport"));
    REQUIRE(report != nullptr);
    CHECK(report->property("open").toBool());
    CHECK(report->property("title").toString() == QStringLiteral("Nothing was imported"));
    CHECK(report->property("blurb").toString()
          == QStringLiteral("Sunday set.pro (ProPresenter 7): Format not supported"));
    REQUIRE(QMetaObject::invokeMethod(report, "close"));

    // Several: a row each, the unreadable first, nothing saved yet.
    pick({path_of(fixtures / "environments.cho"), path_of(unreadable),
          path_of(fixtures / "holy_holy.usr")});
    REQUIRE(window->property("open").toBool());
    CHECK(songs->rowCount() == before + 1);
    QTest::qWait(50);
    auto* list = window->findChild<QQuickItem*>(QStringLiteral("songImportList"));
    REQUIRE(list != nullptr);
    auto* w = ui.window();
    auto* window_item = qobject_cast<QQuickItem*>(window);
    REQUIRE(window_item != nullptr);
    CHECK(window->property("title").toString() == QStringLiteral("Import Songs"));
    CHECK(window->property("acceptLabel").toString() == QStringLiteral("Import Songs"));
    const auto selected = [&](const char* text) {
        return visible_text_under(window_item, QString::fromLatin1(text)) != nullptr;
    };
    CHECK(selected("2 of 2 selected"));
    REQUIRE(visible_text_under(list, QStringLiteral("Sunday set.pro")) != nullptr);
    REQUIRE(visible_text_under(list, QStringLiteral("ProPresenter 7")) != nullptr);
    REQUIRE(visible_text_under(list, QStringLiteral("Format not supported")) != nullptr);
    REQUIRE(visible_text_under(list, QStringLiteral("ChordPro")) != nullptr);

    // The unreadable row cannot be checked; a song's row can be unchecked.
    click(w, centre_of(row_of(list, 0)));
    CHECK(selected("2 of 2 selected"));
    click(w, centre_of(row_of(list, 1)));
    CHECK(selected("1 of 2 selected"));

    // "All songs": all, then none, which Import cannot take.
    auto* all = window->findChild<QQuickItem*>(QStringLiteral("songImportAll"));
    REQUIRE(all != nullptr);
    click(w, centre_of(all));
    CHECK(selected("2 of 2 selected"));
    click(w, centre_of(all));
    CHECK(selected("0 of 2 selected"));
    CHECK_FALSE(window->property("acceptEnabled").toBool());
    click(w, centre_of(row_of(list, 2)));
    CHECK(selected("1 of 2 selected"));

    // Import saves what is checked, once.
    REQUIRE(QMetaObject::invokeMethod(window, "submit"));
    CHECK_FALSE(window->property("open").toBool());
    CHECK(songs->rowCount() == before + 2);

    // Cancel saves nothing.
    pick({path_of(fixtures / "environments.cho"), path_of(fixtures / "holy_holy.usr")});
    REQUIRE(QMetaObject::invokeMethod(window, "close"));
    QTest::qWait(50);
    CHECK(songs->rowCount() == before + 2);
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Library toolbar on Bible verses", "[ui][library-tools]")
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
    auto* w = ui.window();

    // Two runs of verses: two passages, and no single passage to convert.
    click(w, centre_of(row_of(list, 0)));
    click(w, centre_of(row_of(list, 1)), k_ctrl);
    click(w, centre_of(row_of(list, 3)), k_ctrl);
    CHECK(active_tab(panel)->property("addItemCount").toInt() == 2);
    CHECK_FALSE(panel->property("canConvert").toBool());
    const int before = facade.serviceItems()->rowCount();
    call(panel, "toolAdd");
    CHECK(facade.serviceItems()->rowCount() == before + 2);

    click(w, centre_of(row_of(list, 0)));
    click(w, centre_of(row_of(list, 1)), k_shift);
    REQUIRE(panel->property("canConvert").toBool());
    QSignalSpy convert(panel, SIGNAL(requestSaveBibleAsFreeform(int, int, QString)));
    call(active_tab(panel), "convertSelected");
    REQUIRE(convert.size() == 1);
    CHECK(convert.front().at(0).toInt() == 16);
    CHECK(convert.front().at(1).toInt() == 17);
    CHECK(convert.front().at(2).toString().endsWith(QStringLiteral(" (edit)")));
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("The Bibles tab's book suggestions sit evenly in their list", "[ui][library-tools]")
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
    QObject* tab = active_tab(panel);
    QObject* popup = tab->findChild<QObject*>(QStringLiteral("bookSuggestions"));
    REQUIRE(popup != nullptr);

    // No more room under the rows than over them.
    tab->setProperty("suggestions", QVariantList{QStringLiteral("John")});
    REQUIRE(QTest::qWaitFor([&] { return popup->property("opened").toBool(); }, 2000));
    auto* body = popup->property("contentItem").value<QQuickItem*>();
    REQUIRE(body != nullptr);
    const qreal rows = body->property("contentHeight").toReal();
    CHECK(body->y() > 0);
    CHECK(popup->property("height").toReal() - body->y() - rows == body->y());

    // As many as the search can suggest stop at the list's height and scroll.
    QVariantList many;
    for (int i = 0; i < 12; ++i) {
        many.push_back(QStringLiteral("Book %1").arg(i));
    }
    tab->setProperty("suggestions", many);
    REQUIRE(QTest::qWaitFor([&] { return body->property("contentHeight").toReal() > rows; }, 2000));
    CHECK(popup->property("height").toReal() == 180);
    CHECK(body->property("interactive").toBool());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("The arrow keys keep the highlighted book suggestion in sight", "[ui][library-tools]")
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
    QObject* tab = active_tab(panel);
    auto* w = ui.window();
    auto* field = tab->property("searchAnchor").value<QQuickItem*>();
    REQUIRE(field != nullptr);
    click(w, centre_of(field));
    QObject* popup = tab->findChild<QObject*>(QStringLiteral("bookSuggestions"));
    REQUIRE(popup != nullptr);

    const auto books = [](const QString& name) {
        QVariantList out;
        for (int i = 0; i < 12; ++i) {
            out.push_back(name.arg(i));
        }
        return out;
    };
    tab->setProperty("suggestions", books(QStringLiteral("Book %1")));
    REQUIRE(QTest::qWaitFor([&] { return popup->property("opened").toBool(); }, 2000));
    auto* body = popup->property("contentItem").value<QQuickItem*>();
    REQUIRE(body != nullptr);
    const auto index = [&] { return tab->property("suggestIndex").toInt(); };
    const auto top = [&] { return body->property("contentY").toReal(); };
    const auto press = [&](Qt::Key key) {
        QTest::keyClick(w, key);
        QTest::qWait(30);
    };
    const auto highlighted_in_sight = [&] {
        const QString name = QStringLiteral("Book %1").arg(index());
        QQuickItem* row = visible_text_under(w->contentItem(), name);
        if (row == nullptr) {
            return false;
        }
        const QRectF at = body->mapRectFromItem(row, row->boundingRect());
        return at.top() >= 0 && at.bottom() <= body->height();
    };

    // Down to the last book, then back up to the first.
    for (int i = 1; i < 12; ++i) {
        press(Qt::Key_Down);
        CHECK(index() == i);
        CHECK(highlighted_in_sight());
    }
    CHECK(top() > 0);
    press(Qt::Key_Down);
    CHECK(index() == 11);
    for (int i = 10; i >= 0; --i) {
        press(Qt::Key_Up);
        CHECK(index() == i);
        CHECK(highlighted_in_sight());
    }
    CHECK(top() == 0);

    // Rows sliding under a mouse left on the list do not take the highlight.
    const QPointF middle = body->mapToScene(QPointF(body->width() / 2, body->height() / 2));
    QTest::mouseMove(w, middle.toPoint());
    QTest::qWait(30);
    int expected = index();
    while (expected < 11) {
        press(Qt::Key_Down);
        ++expected;
        CHECK(index() == expected);
        CHECK(highlighted_in_sight());
    }
    CHECK(top() > 0);

    // Moving the mouse takes the highlight to the row it is on.
    const QPoint higher = middle.toPoint() - QPoint(0, 46);
    QTest::mouseMove(w, higher);
    QTest::mouseMove(w, higher + QPoint(2, 0));
    QTest::qWait(30);
    CHECK(index() < 11);
    QQuickItem* pointed = visible_text_under(w->contentItem(), QStringLiteral("Book %1").arg(index()));
    REQUIRE(pointed != nullptr);
    const QPointF pointer(higher + QPoint(2, 0));
    CHECK(pointed->contains(pointed->mapFromScene(pointer)));

    // The wheel scrolling the list under a still mouse does take it: Return
    // must not accept a book scrolled out of sight.
    const qreal before_wheel = top();
    QWheelEvent wheel(pointer, w->mapToGlobal(pointer), QPoint(), QPoint(0, 120), Qt::NoButton,
                      Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(w, &wheel);
    // Scrolled, come to rest, and the row under the mouse highlighted.
    REQUIRE(QTest::qWaitFor(
        [&] {
            QQuickItem* row =
                visible_text_under(w->contentItem(), QStringLiteral("Book %1").arg(index()));
            return top() < before_wheel && !body->property("moving").toBool() && row != nullptr
                   && row->contains(row->mapFromScene(pointer));
        },
        2000));
    QQuickItem* wheeled = visible_text_under(w->contentItem(), QStringLiteral("Book %1").arg(index()));
    REQUIRE(wheeled != nullptr);
    CHECK(wheeled->contains(wheeled->mapFromScene(pointer)));

    // Another list starts at its top.
    tab->setProperty("suggestions", books(QStringLiteral("Other %1")));
    QTest::qWait(30);
    CHECK(popup->property("opened").toBool());
    CHECK(top() == 0);
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Library toolbar on images", "[ui][library-tools]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    (void)build(facade, dir.path);
    facade.media()->setMediaViewMode(QStringLiteral("list"));
    OperatorUi ui(facade);
    QObject* panel = ui.find_component("LibraryPanel");
    REQUIRE(panel != nullptr);
    REQUIRE(panel->setProperty("tabIndex", 4));
    QTest::qWait(50);
    QObject* browser = browser_of(panel, "isImage");
    REQUIRE(browser != nullptr);
    QQuickItem* list =
        list_view_of(browser, facade.media()->browser(seventhslide::db::MediaKind::Image));
    REQUIRE(list != nullptr);
    auto* w = ui.window();

    click(w, centre_of(row_of(list, 0)));
    click(w, centre_of(row_of(list, 1)), k_ctrl);
    const int before = facade.serviceItems()->rowCount();
    call(panel, "toolAdd");
    const QStringList titles = rundown_titles(facade);
    REQUIRE(titles.size() > before);
    CHECK(titles.mid(before).join(' ').contains(QStringLiteral("a.png")));
    CHECK(titles.mid(before).join(' ').contains(QStringLiteral("b.png")));

    click(w, centre_of(row_of(list, 2)));
    REQUIRE(panel->property("canDelete").toBool());
    delete_confirmed(panel);
    QStringList left;
    for (const auto& f : facade.media_repo()->list_files(seventhslide::db::MediaKind::Image)) {
        left.push_back(QString::fromStdString(f.filename));
    }
    CHECK_FALSE(left.contains(QStringLiteral("c.png")));
    CHECK(left.contains(QStringLiteral("d.png")));
    CHECK(browser->property("selectedFilenames").toStringList().isEmpty());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Deleting an image still on its way says it goes everywhere", "[ui][library-tools][sync]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    (void)build(facade, dir.path);
    facade.media()->setMediaViewMode(QStringLiteral("list"));
    OperatorUi ui(facade);
    QObject* panel = ui.find_component("LibraryPanel");
    REQUIRE(panel != nullptr);
    REQUIRE(panel->setProperty("tabIndex", 4));
    QTest::qWait(50);
    QObject* browser = browser_of(panel, "isImage");
    REQUIRE(browser != nullptr);
    QQuickItem* list =
        list_view_of(browser, facade.media()->browser(seventhslide::db::MediaKind::Image));
    REQUIRE(list != nullptr);
    click(ui.window(), centre_of(row_of(list, 2)));
    const auto picked = browser->property("selectedFilename").toString();
    REQUIRE_FALSE(picked.isEmpty());

    // What the confirm asks.
    const auto asked = [&] {
        call(panel, "toolDelete");
        QObject* confirm = nullptr;
        for (QObject* o : panel->findChildren<QObject*>()) {
            if (QByteArray(o->metaObject()->className()).startsWith("AppDialog_QMLTYPE_")
                && o->property("open").toBool()) {
                confirm = o;
            }
        }
        REQUIRE(confirm != nullptr);
        const auto text = confirm->property("blurb").toString();
        call(confirm, "close");
        return text;
    };
    CHECK_FALSE(asked().contains(QStringLiteral("every computer")));

    using seventhslide::app::FileArrival;
    facade.media()->set_arrivals({{"images/" + picked.toStdString(), {FileArrival::State::Queued, 0, 0}}});
    const auto warned = asked();
    CHECK(warned.contains(QStringLiteral("delete this image")));
    CHECK(warned.contains(QStringLiteral("on its way")));
    CHECK(warned.contains(QStringLiteral("every computer")));
    // Asked, not done.
    bool still_there = false;
    for (const auto& f : facade.media_repo()->list_files(seventhslide::db::MediaKind::Image)) {
        still_there = still_there || QString::fromStdString(f.filename) == picked;
    }
    CHECK(still_there);
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Library toolbar on audio", "[ui][library-tools][audio]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    (void)build(facade, dir.path);  // a service is open
    const auto audio = facade.media_root() / "audio";
    std::filesystem::create_directories(audio);
    for (const char* name : {"hymn.mp3", "postlude.flac", "prelude.wav"}) {
        std::ofstream(audio / name) << name;
        facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Audio, name, name);
    }
    facade.refresh_library();
    facade.media()->setMediaViewMode(QStringLiteral("list"));

    OperatorUi ui(facade);
    QObject* panel = ui.find_component("LibraryPanel");
    REQUIRE(panel != nullptr);
    REQUIRE(QMetaObject::invokeMethod(panel, "selectTab", Q_ARG(QVariant, 5)));
    QTest::qWait(50);
    QObject* browser = browser_of(panel, "isAudio");
    REQUIRE(browser != nullptr);
    REQUIRE(active_tab(panel) == browser);
    CHECK(browser->property("title").toString() == QStringLiteral("Audio"));
    QQuickItem* list =
        list_view_of(browser, facade.media()->browser(seventhslide::db::MediaKind::Audio));
    REQUIRE(list != nullptr);
    auto* w = ui.window();

    click(w, centre_of(row_of(list, 0)));
    CHECK(browser->property("selectedFilenames").toStringList()
          == QStringList{QStringLiteral("hymn.mp3")});
    CHECK(panel->property("canEdit").toBool());
    CHECK(panel->property("canDelete").toBool());

    // The Service button adds the file as an audio item.
    REQUIRE(panel->property("canAdd").toBool());
    const int before = facade.serviceItems()->rowCount();
    call(panel, "toolAdd");
    REQUIRE(facade.serviceItems()->rowCount() == before + 1);
    const auto added = facade.serviceItems()->index(before, 0);
    CHECK(facade.serviceItems()->data(added, seventhslide::app::ServiceItemModel::TitleRole).toString()
          == QStringLiteral("hymn.mp3"));
    CHECK(facade.serviceItems()->data(added, seventhslide::app::ServiceItemModel::ItemTypeRole).toString()
          == QStringLiteral("audio"));

    // Opening a file plays it on the audio stack, sound only.
    REQUIRE(QMetaObject::invokeMethod(browser, "activateEntry",
                                      Q_ARG(QVariant, QStringLiteral("file")), Q_ARG(QVariant, -1),
                                      Q_ARG(QVariant, QStringLiteral("hymn.mp3")),
                                      Q_ARG(QVariant, false)));
    QTest::qWait(10);
    CHECK(facade.session().focused_stack().program.audio_mode_active());
    CHECK(facade.session().focused_stack().program.playback().filename == "hymn.mp3");
    CHECK(facade.program()->mediaTransport());
    CHECK_FALSE(facade.program()->videoMode());

    // A file a service plays cannot be deleted; one that none plays can.
    QVariant refusal;
    REQUIRE(QMetaObject::invokeMethod(browser, "deleteSelected", Q_RETURN_ARG(QVariant, refusal)));
    CHECK_FALSE(refusal.toString().isEmpty());
    CHECK(std::filesystem::exists(audio / "hymn.mp3"));

    click(w, centre_of(row_of(list, 2)));
    CHECK(browser->property("selectedFilenames").toStringList()
          == QStringList{QStringLiteral("prelude.wav")});
    delete_confirmed(panel);
    CHECK_FALSE(std::filesystem::exists(audio / "prelude.wav"));
    CHECK(facade.media()->browser(seventhslide::db::MediaKind::Audio)->rowCount() == 2);
    CHECK(browser->property("selectedFilenames").toStringList().isEmpty());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("A media tab's empty text goes when a file arrives in either view",
          "[ui][library-tools][audio]")
{
    seventhslide::test::ensure_qt_application();
    for (const auto* mode : {"icons", "list"}) {
        INFO("view " << mode);
        const seventhslide::test::TempDir dir;
        SessionFacade facade;
        REQUIRE(facade.open_database(dir.path / "library.db"));
        facade.media()->setMediaViewMode(QString::fromLatin1(mode));
        OperatorUi ui(facade);
        QObject* panel = ui.find_component("LibraryPanel");
        REQUIRE(panel != nullptr);
        REQUIRE(QMetaObject::invokeMethod(panel, "selectTab", Q_ARG(QVariant, 5)));
        QTest::qWait(20);
        REQUIRE(browser_of(panel, "isAudio") != nullptr);
        auto* content = ui.window()->contentItem();
        REQUIRE(visible_text_under(content, QStringLiteral("No audio")) != nullptr);

        const auto src = dir.path / "hymn.mp3";
        std::ofstream(src) << "x";
        (void)seventhslide::test::wait_for_import(
            *facade.media(),
            facade.media()->import_files(seventhslide::db::MediaKind::Audio,
                                         {QString::fromStdString(src.string())}));
        QTest::qWait(20);
        CHECK(facade.media()->browser(seventhslide::db::MediaKind::Audio)->rowCount() == 1);
        CHECK(visible_text_under(content, QStringLiteral("No audio")) == nullptr);
        INFO(ui.warnings());
        CHECK(ui.warnings().empty());
    }
}

TEST_CASE("The Bibles tab stops saying none are imported once one is",
          "[ui][library-tools]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    OperatorUi ui(facade);
    QObject* panel = ui.find_component("LibraryPanel");
    REQUIRE(panel != nullptr);
    REQUIRE(QMetaObject::invokeMethod(panel, "selectTab", Q_ARG(QVariant, 2)));
    QTest::qWait(20);
    auto* content = ui.window()->contentItem();
    REQUIRE(visible_text_under(content, QStringLiteral("No Bibles imported")) != nullptr);

    seventhslide::model::BibleVerse verse;
    verse.book = "John";
    verse.chapter = 3;
    verse.verse_num = 16;
    verse.text = "For God so loved the world";
    (void)facade.bible_repo()->import_bible("Test Bible", "", {verse});
    facade.library()->refresh_bibles();
    QTest::qWait(20);
    CHECK(visible_text_under(content, QStringLiteral("No Bibles imported")) == nullptr);
}

TEST_CASE("Library toolbar on Freeform", "[ui][library-tools]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    (void)build(facade, dir.path);
    facade.media()->setMediaViewMode(QStringLiteral("list"));
    OperatorUi ui(facade);
    QObject* panel = ui.find_component("LibraryPanel");
    REQUIRE(panel != nullptr);
    REQUIRE(panel->setProperty("tabIndex", 1));
    QTest::qWait(50);
    auto* model = facade.freeformLibrary()->property("browser").value<QAbstractItemModel*>();
    REQUIRE(model != nullptr);
    REQUIRE(model->rowCount() == 0);

    // New: an untitled item, open in the editor.
    call(active_tab(panel), "createItem");
    CHECK(model->rowCount() == 1);
    CHECK(facade.slideEdit()->active());
    facade.slideEdit()->cancel();
    QTest::qWait(20);

    QQuickItem* list = list_view_of(browser_of(panel, "isFreeform"), model);
    REQUIRE(list != nullptr);
    click(ui.window(), centre_of(row_of(list, 0)));
    REQUIRE(panel->property("canDuplicate").toBool());
    call(active_tab(panel), "duplicateSelected");
    CHECK(model->rowCount() == 2);

    // Both deleted from the tab.
    QObject* browser = browser_of(panel, "isFreeform");
    for (int r = 0; r < model->rowCount(); ++r) {
        REQUIRE(QMetaObject::invokeMethod(browser, "selectItem",
                                          Q_ARG(QVariant, model->data(model->index(r, 0), MediaBrowserModel::IdRole)),
                                          Q_ARG(QVariant, r > 0)));
    }
    QVariant refused;
    REQUIRE(QMetaObject::invokeMethod(browser, "deleteSelected", Q_RETURN_ARG(QVariant, refused)));
    CHECK(refused.toString().isEmpty());
    CHECK(model->rowCount() == 0);
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// Freeform's Import: the file is read, the window shows its items and where
// each of its outputs goes, and Import brings the item into the library.
TEST_CASE("Library toolbar on Freeform imports a Freeform file", "[ui][library-tools][freeform-files]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    (void)build(facade, dir.path);
    facade.media()->setMediaViewMode(QStringLiteral("list"));
    OperatorUi ui(facade);
    QObject* panel = ui.find_component("LibraryPanel");
    REQUIRE(panel != nullptr);
    REQUIRE(panel->setProperty("tabIndex", 1));
    QTest::qWait(50);
    QObject* browser = browser_of(panel, "isFreeform");
    REQUIRE(browser != nullptr);
    const auto has_choice = [](QObject* tab, const char* list, const QString& id) {
        for (const auto& c : tab->property(list).toList()) {
            if (c.toMap().value(QStringLiteral("id")).toString() == id) {
                return true;
            }
        }
        return false;
    };
    CHECK(has_choice(browser, "createChoices", QStringLiteral("importFile")));
    CHECK(has_choice(browser, "exportChoices", QStringLiteral("file")));

    // A file of one item, for this library's outputs.
    namespace ex = seventhslide::exchange;
    ex::BundleSource source;
    std::vector<std::string> ids;
    for (const auto& oc : facade.session().outputs()) {
        source.outputs.push_back({oc.id, oc.name, seventhslide::model::output_canvas(oc)});
        ids.push_back(oc.id);
    }
    source.items.push_back({"Welcome", {}, {}, seventhslide::freeform::make_document("Welcome", ids), {}});
    const auto file = dir.path / "Welcome.ssff";
    REQUIRE(ex::write_bundle(source, file, {}).ok);

    REQUIRE(QMetaObject::invokeMethod(browser, "freeformImportDialog"));
    QObject* window = qml_object(browser, "FreeformImportDialog");
    REQUIRE(window != nullptr);
    REQUIRE(QMetaObject::invokeMethod(window, "read", Q_ARG(QVariant, QUrl::fromLocalFile(QString::fromStdString(file.string())).toString())));
    REQUIRE(QTest::qWaitFor([&] { return window->property("open").toBool(); }, 10000));
    CHECK(window->property("items").toList().size() == 1);
    const auto mapping = window->property("mapping").toList();
    REQUIRE(mapping.size() == static_cast<qsizetype>(ids.size()));
    CHECK(mapping[0].toString().toStdString() == ids[0]);
    CHECK(window->property("acceptEnabled").toBool());

    auto* model = facade.freeformLibrary()->property("browser").value<QAbstractItemModel*>();
    REQUIRE(model != nullptr);
    const int before = model->rowCount();
    call(window, "submit");
    CHECK(QTest::qWaitFor([&] { return model->rowCount() == before + 1; }, 10000));
    CHECK_FALSE(window->property("open").toBool());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// Switching tabs from the strip: a song search stays, a media search starts
// empty each time its tab shows, and the Bibles tab names its translation.
TEST_CASE("Library tab strip keeps and resets each tab's search", "[ui][library-tools]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    (void)build(facade, dir.path);  // a Bible selected
    OperatorUi ui(facade);
    QObject* panel = ui.find_component("LibraryPanel");
    REQUIRE(panel != nullptr);
    const auto select_tab = [panel](int idx) {
        REQUIRE(QMetaObject::invokeMethod(panel, "selectTab", Q_ARG(QVariant, idx)));
        QTest::qWait(10);
    };
    auto* songs = facade.library()->property("songs").value<QAbstractItemModel*>();
    auto* images = facade.media()->browser(seventhslide::db::MediaKind::Image);
    REQUIRE(songs != nullptr);
    REQUIRE(images != nullptr);
    const int all_songs = songs->rowCount();
    const int all_images = images->rowCount();
    REQUIRE(all_images > 0);

    select_tab(0);
    REQUIRE(active_tab(panel)->setProperty("search", QStringLiteral("Alpha")));
    CHECK(songs->rowCount() == 1);

    select_tab(4);
    CHECK(active_tab(panel)->property("search").toString().isEmpty());
    REQUIRE(active_tab(panel)->setProperty("search", QStringLiteral("no such image")));
    CHECK(images->rowCount() == 0);

    // Back on Songs the search is still there; the image search is not.
    select_tab(0);
    CHECK(active_tab(panel)->property("search").toString() == QStringLiteral("Alpha"));
    CHECK(songs->rowCount() == 1);
    CHECK(images->rowCount() == all_images);
    select_tab(4);
    CHECK(active_tab(panel)->property("search").toString().isEmpty());

    select_tab(2);
    CHECK(active_tab(panel)->property("translationLabel").toString()
          == facade.library()->property("selectedBibleName").toString());
    CHECK(all_songs > 1);
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Browser and Live are on the tab strip's second page; the tab showing is remembered",
          "[ui][library-tools]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    (void)build(facade, dir.path);

    {
        OperatorUi ui(facade);
        auto* w = ui.window();
        QTest::qWait(100);  // laid out
        QObject* panel = ui.find_component("LibraryPanel");
        REQUIRE(panel != nullptr);
        auto* strip = qobject_cast<QQuickItem*>(qml_object(panel, "PanelTabStrip"));
        REQUIRE(strip != nullptr);
        CHECK(strip->property("pageCount").toInt() == 2);
        CHECK(strip->property("page").toInt() == 0);
        CHECK(panel->property("tabIndex").toInt() == 0);
        // The strip on ``page`` and slid there.
        const auto turned_to = [&](int page) {
            return QTest::qWaitFor(
                [&] {
                    return strip->property("page").toInt() == page
                           && strip->property("slide").toDouble() == page;
                },
                5000);
        };

        // The arrow turns the page and opens nothing.
        auto* arrow = strip->findChild<QQuickItem*>(QStringLiteral("moreTabsArrow"));
        REQUIRE(arrow != nullptr);
        REQUIRE(arrow->isVisible());
        click(w, centre_of(arrow));
        CHECK(turned_to(1));
        CHECK(strip->property("page").toInt() == 1);
        CHECK(panel->property("tabIndex").toInt() == 0);

        // A tab on it opens like any other, with its own search and tools.
        QQuickItem* browser = visible_text_under(strip, QStringLiteral("Browser"));
        REQUIRE(browser != nullptr);
        click(w, centre_of(browser));
        QTest::qWait(50);
        CHECK(panel->property("tabIndex").toInt() == 6);
        CHECK(active_tab(panel)->property("title").toString() == QStringLiteral("Browser"));
        CHECK(active_tab(panel)->property("searchPlaceholder").toString()
              == QStringLiteral("Search Browser…"));
        CHECK(active_tab(panel)->property("offersCreate").toBool());
        CHECK_FALSE(panel->property("canAdd").toBool());
        CHECK(facade.library()->shownTab() == QStringLiteral("browser"));

        // Back turns the page and leaves the tab open.
        auto* back = strip->findChild<QQuickItem*>(QStringLiteral("moreTabsBack"));
        REQUIRE(back != nullptr);
        click(w, centre_of(back));
        CHECK(turned_to(0));
        CHECK(strip->property("page").toInt() == 0);
        CHECK(panel->property("tabIndex").toInt() == 6);

        QQuickItem* songs = visible_text_under(strip, QStringLiteral("Songs"));
        REQUIRE(songs != nullptr);
        click(w, centre_of(songs));
        QTest::qWait(50);
        CHECK(panel->property("tabIndex").toInt() == 0);
        CHECK(facade.library()->shownTab() == QStringLiteral("songs"));

        // The page follows a tab opened another way.
        REQUIRE(QMetaObject::invokeMethod(panel, "selectTab", Q_ARG(QVariant, 7)));
        CHECK(turned_to(1));
        CHECK(strip->property("page").toInt() == 1);
        CHECK(facade.library()->shownTab() == QStringLiteral("live"));
        CHECK(ui.warnings().empty());
    }

    // Kept on this computer, so a new session reads it.
    {
        SessionFacade again;
        CHECK(again.library()->shownTab() == QStringLiteral("live"));
    }

    // The window opens on the tab remembered, on its page.
    {
        OperatorUi ui(facade, QStringLiteral("live"));
        QObject* panel = ui.find_component("LibraryPanel");
        REQUIRE(panel != nullptr);
        CHECK(panel->property("tabIndex").toInt() == 7);
        CHECK(qml_object(panel, "PanelTabStrip")->property("page").toInt() == 1);
    }
    // One it has no tab for (from a newer version, say) opens Songs.
    {
        OperatorUi ui(facade, QStringLiteral("nope"));
        QObject* panel = ui.find_component("LibraryPanel");
        REQUIRE(panel != nullptr);
        CHECK(panel->property("tabIndex").toInt() == 0);
    }
}

TEST_CASE("The tab strip pages by what fits, and no title runs into the next",
          "[ui][library-tools]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    (void)build(facade, dir.path);
    OperatorUi ui(facade);
    QObject* panel = ui.find_component("LibraryPanel");
    REQUIRE(panel != nullptr);
    auto* strip = qobject_cast<QQuickItem*>(qml_object(panel, "PanelTabStrip"));
    REQUIRE(strip != nullptr);

    const qreal min_w = strip->property("tabMinW").toReal();
    const qreal arrow_w = strip->property("arrowW").toReal();
    REQUIRE(min_w > 0);

    const auto places = [&] { return strip->property("places").toList(); };
    const auto page_of = [&](const QVariant& place) { return place.toMap()["page"].toInt(); };

    for (const qreal width : {600.0, 360.0, 240.0, 150.0, 100.0, 40.0}) {
        strip->setWidth(width);
        INFO("strip width " << width);
        const auto all = places();
        REQUIRE(all.size() == 8);
        int previous_page = 0;
        qreal previous_end = 0;
        int on_page = 0;
        for (const auto& place : all) {
            const auto p = place.toMap();
            const int page = p["page"].toInt();
            if (page != previous_page) {
                CHECK(page == previous_page + 1);
                previous_end = 0;
                on_page = 0;
            }
            ++on_page;
            CHECK(on_page <= 6);
            // Each page's tabs in a row, inside the strip, leaving its arrows.
            const qreal x = p["x"].toReal();
            const qreal end = x + p["width"].toReal();
            CHECK(x >= previous_end - 0.001);
            CHECK(x >= (page > 0 ? arrow_w : 0) - 0.001);
            CHECK(end <= width - (page < page_of(all.back()) ? arrow_w : 0) + 0.001);
            // Room for the title, whenever a page has room for a tab at all.
            if (width - 2 * arrow_w >= min_w) {
                CHECK(p["width"].toReal() >= min_w - 0.001);
            }
            previous_page = page;
            previous_end = end;
        }
    }

    // At the column's default width all six first tabs fit, the widest title
    // in bold whole, and Browser and Live are on the next page.
    strip->setWidth(360);
    const auto default_places = places();
    CHECK(page_of(default_places.at(5)) == 0);
    CHECK(page_of(default_places.at(6)) == 1);
    CHECK(page_of(default_places.at(7)) == 1);
    REQUIRE(QMetaObject::invokeMethod(panel, "selectTab", Q_ARG(QVariant, 1)));
    QQuickItem* freeform = visible_text_under(strip, QStringLiteral("Freeform"));
    REQUIRE(freeform != nullptr);
    QTest::qWait(30);
    CHECK_FALSE(freeform->property("truncated").toBool());
    REQUIRE(QMetaObject::invokeMethod(panel, "selectTab", Q_ARG(QVariant, 0)));

    // The narrower, the more pages; the open tab's page shows.
    strip->setWidth(100);
    const int pages = strip->property("pageCount").toInt();
    CHECK(pages > 2);
    REQUIRE(QMetaObject::invokeMethod(panel, "selectTab", Q_ARG(QVariant, 7)));
    CHECK(strip->property("page").toInt() == pages - 1);
    CHECK(strip->property("currentIndex").toInt() == 7);

    // Wide enough again, the page follows the tab back: it is not left past the end.
    strip->setWidth(400);
    CHECK(strip->property("page").toInt() == strip->property("pageCount").toInt() - 1);
    CHECK(strip->property("pageCount").toInt() == 2);

    // Arrows at the ends: none going back from the first page, none on from the last.
    auto* back = strip->findChild<QQuickItem*>(QStringLiteral("moreTabsBack"));
    auto* on = strip->findChild<QQuickItem*>(QStringLiteral("moreTabsArrow"));
    REQUIRE(back != nullptr);
    REQUIRE(on != nullptr);
    CHECK(back->isVisible());
    CHECK_FALSE(on->isVisible());
    REQUIRE(QMetaObject::invokeMethod(panel, "selectTab", Q_ARG(QVariant, 0)));
    CHECK_FALSE(back->isVisible());
    CHECK(on->isVisible());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("The service toolbar ends with the gear, then the more button", "[ui][library-tools]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    (void)build(facade, dir.path);
    OperatorUi ui(facade);
    QTest::qWait(100);  // laid out
    auto* toolbar = qobject_cast<QQuickItem*>(ui.find_component("ServiceToolbar"));
    REQUIRE(toolbar != nullptr);

    QStringList icons;
    const std::function<void(QQuickItem*)> walk = [&](QQuickItem* item) {
        for (QQuickItem* child : item->childItems()) {
            if (QByteArray(child->metaObject()->className()).startsWith("ServiceToolButton_QMLTYPE_")) {
                icons.push_back(child->property("icon").toString());
            }
            walk(child);
        }
    };
    walk(toolbar);
    INFO(icons.join(QLatin1Char(',')).toStdString());
    REQUIRE(icons.size() >= 2);
    CHECK(icons.at(icons.size() - 2) == QStringLiteral("settings"));
    CHECK(icons.back() == QStringLiteral("more"));
}

TEST_CASE("Settings arrange the Library's tabs: hide, reorder, and a hidden tab closes",
          "[ui][library-tools][settings]")
{
    seventhslide::test::ensure_qt_application();
    // QSettings outlives a test: another's strip must not be arranged by this.
    struct Forget {
        Forget() { forget(); }
        ~Forget() { forget(); }
        static void forget()
        {
            QSettings().remove(QStringLiteral("library/tabOrder"));
            QSettings().remove(QStringLiteral("library/hiddenTabs"));
            QSettings().remove(QStringLiteral("library/tabsShown"));
        }
    } const forget;
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    (void)build(facade, dir.path);

    {
        OperatorUi ui(facade);
        QObject* panel = ui.find_component("LibraryPanel");
        QObject* dialog = ui.find_component("AppSettingsDialog");
        REQUIRE(panel != nullptr);
        REQUIRE(dialog != nullptr);
        QObject* general = qml_object(dialog, "SettingsGeneralState");
        REQUIRE(general != nullptr);
        auto* strip = qobject_cast<QQuickItem*>(qml_object(panel, "PanelTabStrip"));
        REQUIRE(strip != nullptr);
        const auto titles = [&] { return strip->property("titles").toStringList(); };

        // Settings and the panel name the tabs alike, in the same default order.
        CHECK(titles()
              == QStringList{"Songs", "Freeform", "Bibles", "Videos", "Images", "Audio", "Browser",
                             "Live"});
        REQUIRE(QMetaObject::invokeMethod(dialog, "openDialog"));
        QTest::qWait(50);
        QStringList labelled;
        for (QQuickItem* row :
             seventhslide::test::visible_qml_items_of(qobject_cast<QQuickItem*>(dialog), "GroupRow")) {
            if (row->objectName() == QStringLiteral("libraryTabRow")) {
                labelled.push_back(row->property("labelText").toString());
            }
        }
        CHECK(labelled == titles());

        // Nothing changes until Save.
        REQUIRE(QMetaObject::invokeMethod(general, "toggleTab", Q_ARG(QVariant, "songs")));
        CHECK(general->property("hiddenTabs").toStringList() == QStringList{"songs"});
        CHECK(titles().first() == QStringLiteral("Songs"));
        CHECK(panel->property("tabIndex").toInt() == 0);

        // The tab showing was hidden: the strip's first takes over.
        general->setProperty("tabOrder", QStringList{"live", "bibles", "songs", "freeform",
                                                     "videos", "images", "audio", "browser"});
        REQUIRE(QMetaObject::invokeMethod(dialog, "save"));
        QTest::qWait(30);
        CHECK(titles()
              == QStringList{"Live", "Bibles", "Freeform", "Videos", "Images", "Audio", "Browser"});
        CHECK(active_tab(panel)->property("title").toString() == QStringLiteral("Live"));
        CHECK(facade.library()->shownTab() == QStringLiteral("live"));
        CHECK(strip->property("currentIndex").toInt() == 0);

        // A click on the strip opens the tab it shows, wherever that sits.
        QQuickItem* bibles = visible_text_under(strip, QStringLiteral("Bibles"));
        REQUIRE(bibles != nullptr);
        click(ui.window(), centre_of(bibles));
        QTest::qWait(50);
        CHECK(active_tab(panel)->property("title").toString() == QStringLiteral("Bibles"));
        CHECK(strip->property("currentIndex").toInt() == 1);

        // A row moves by its tile, which shows its tab's own glyph; the switch
        // takes a real click. Nothing moves until Save.
        REQUIRE(QMetaObject::invokeMethod(dialog, "openDialog"));
        QTest::qWait(50);
        auto* sheet = qobject_cast<QQuickItem*>(dialog);
        auto* page = qobject_cast<QQuickItem*>(qml_object(dialog, "SettingsGeneralPanel"));
        REQUIRE(page != nullptr);
        page->setProperty("contentY", page->property("contentHeight").toReal() - page->height());
        QTest::qWait(50);
        std::vector<QQuickItem*> handles;
        QSet<QString> glyphs;
        for (QQuickItem* item : visible_items_of(sheet, "QQuickItem")) {
            if (item->objectName() == QStringLiteral("libraryTabHandle")) {
                handles.push_back(item);
                glyphs.insert(item->property("iconSource").toString());
                CHECK(item->property("text").toString().isEmpty());
            }
        }
        REQUIRE(handles.size() == 8);
        CHECK(glyphs.size() == 8);
        // Images, after the order above, dropped in the upper half of the row
        // before it.
        REQUIRE(general->property("tabOrder").toStringList().at(5) == QStringLiteral("images"));
        seventhslide::test::drag(ui.window(), centre_of(handles.at(5)),
                                 centre_of(handles.at(4)) - QPoint(0, 8));
        QTest::qWait(30);
        CHECK(general->property("tabOrder").toStringList()
              == QStringList{"live", "bibles", "songs", "freeform", "images", "videos", "audio",
                             "browser"});
        CHECK(titles()
              == QStringList{"Live", "Bibles", "Freeform", "Videos", "Images", "Audio", "Browser"});

        QQuickItem* switched = nullptr;
        for (QQuickItem* item : visible_items_of(sheet, "QQuickItem")) {
            if (item->objectName() == QStringLiteral("libraryTabToggle")) {
                switched = item;  // the last row's: Browser
            }
        }
        REQUIRE(switched != nullptr);
        click(ui.window(), centre_of(switched));
        QTest::qWait(30);
        CHECK(general->property("hiddenTabs").toStringList()
              == QStringList{"songs", "browser"});
        REQUIRE(QMetaObject::invokeMethod(general, "toggleTab", Q_ARG(QVariant, "browser")));

        // How many show at once is one of as many choices as there are tabs.
        CHECK(general->property("tabsShown").toInt() == 6);
        CHECK(general->property("tabsShownOptions").toList().size() == 8);
        general->setProperty("tabsShown", 3);
        CHECK(strip->property("pageLimit").toInt() == 6);
        REQUIRE(QMetaObject::invokeMethod(dialog, "save"));
        QTest::qWait(30);
        CHECK(facade.library()->tabsShown() == 3);
        CHECK(strip->property("pageLimit").toInt() == 3);
        strip->setWidth(500);
        {
            const auto places = strip->property("places").toList();
            CHECK(places.at(2).toMap()["page"].toInt() == 0);
            CHECK(places.at(3).toMap()["page"].toInt() == 1);
        }
        // All of them, with room: one page and no arrow.
        REQUIRE(QMetaObject::invokeMethod(dialog, "openDialog"));
        CHECK(general->property("tabsShown").toInt() == 3);
        general->setProperty("hiddenTabs", QStringList{});
        general->setProperty("tabsShown", 8);
        REQUIRE(QMetaObject::invokeMethod(dialog, "save"));
        QTest::qWait(30);
        strip->setWidth(500);
        CHECK(strip->property("pageCount").toInt() == 1);
        CHECK_FALSE(strip->findChild<QQuickItem*>(QStringLiteral("moreTabsArrow"))->isVisible());
        // And a narrow panel still shows fewer, whatever was asked for.
        strip->setWidth(220);
        CHECK(strip->property("pageCount").toInt() > 1);
        REQUIRE(QMetaObject::invokeMethod(dialog, "openDialog"));
        general->setProperty("hiddenTabs", QStringList{"songs"});
        REQUIRE(QMetaObject::invokeMethod(dialog, "save"));
        QTest::qWait(30);

        // Hiding all but one: the last can't be switched off.
        REQUIRE(QMetaObject::invokeMethod(dialog, "openDialog"));
        for (const char* id : {"live", "bibles", "freeform", "videos", "images", "audio"}) {
            REQUIRE(QMetaObject::invokeMethod(general, "toggleTab", Q_ARG(QVariant, id)));
        }
        REQUIRE(QMetaObject::invokeMethod(general, "toggleTab", Q_ARG(QVariant, "browser")));
        CHECK(general->property("hiddenTabs").toStringList().size() == 7);
        REQUIRE(QMetaObject::invokeMethod(dialog, "save"));
        QTest::qWait(30);
        CHECK(titles() == QStringList{"Browser"});
        CHECK(strip->property("pageCount").toInt() == 1);
        INFO(ui.warnings());
        CHECK(ui.warnings().empty());
    }

    // Kept on this computer, and read by a new session.
    {
        SessionFacade again;
        CHECK(again.library()->hiddenTabs().size() == 7);
        CHECK(again.library()->tabOrder().first() == QStringLiteral("live"));
        CHECK(again.library()->tabsShown() == 8);
    }

    // Nonsense kept for how many show at once is the default.
    QSettings().setValue(QStringLiteral("library/tabsShown"), 0);
    {
        SessionFacade again;
        CHECK(again.library()->tabsShown() == 6);
    }

    // A tab it no longer knows is dropped; one it does not yet is added at the
    // end; all hidden shows them all. The window opens on the first if the one
    // remembered is hidden.
    facade.library()->setTabOrder({"gone", "audio"});
    facade.library()->setHiddenTabs({"songs"});
    {
        OperatorUi ui(facade, QStringLiteral("songs"));
        QObject* panel = ui.find_component("LibraryPanel");
        REQUIRE(panel != nullptr);
        auto* strip = qml_object(panel, "PanelTabStrip");
        CHECK(strip->property("titles").toStringList()
              == QStringList{"Audio", "Freeform", "Bibles", "Videos", "Images", "Browser", "Live"});
        CHECK(active_tab(panel)->property("title").toString() == QStringLiteral("Audio"));
        CHECK(ui.warnings().empty());
    }
    // One still showing is remembered, wherever Songs has gone.
    {
        OperatorUi ui(facade, QStringLiteral("bibles"));
        QObject* panel = ui.find_component("LibraryPanel");
        REQUIRE(panel != nullptr);
        CHECK(active_tab(panel)->property("title").toString() == QStringLiteral("Bibles"));
        CHECK(facade.library()->shownTab() == QStringLiteral("bibles"));
    }
    facade.library()->setHiddenTabs({"songs", "freeform", "bibles", "videos", "images", "audio",
                                     "browser", "live"});
    {
        OperatorUi ui(facade);
        QObject* panel = ui.find_component("LibraryPanel");
        REQUIRE(panel != nullptr);
        CHECK(qml_object(panel, "PanelTabStrip")->property("titles").toStringList().size() == 8);
    }
}

TEST_CASE("Library toolbar on Browser: web pages are added, edited, filed and deleted",
          "[ui][library-tools][web]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    (void)build(facade, dir.path);
    facade.media()->setMediaViewMode(QStringLiteral("list"));

    OperatorUi ui(facade);
    QTest::qWait(50);
    QObject* panel = ui.find_component("LibraryPanel");
    REQUIRE(panel != nullptr);
    REQUIRE(QMetaObject::invokeMethod(panel, "selectTab", Q_ARG(QVariant, 6)));
    QTest::qWait(50);
    QObject* browser = browser_of(panel, "isWeb");
    REQUIRE(browser != nullptr);
    REQUIRE(active_tab(panel) == browser);
    CHECK(browser->property("title").toString() == QStringLiteral("Browser"));
    CHECK_FALSE(browser->property("offersConvert").toBool());
    auto* pages = facade.webLibrary()->browser();
    CHECK(pages->rowCount() == 0);

    // + opens the page dialog, empty; an address that is not one keeps Add off.
    call(active_tab(panel), "createItem");
    QObject* dialog = qml_object(browser, "WebPageDialog");
    REQUIRE(dialog != nullptr);
    REQUIRE(dialog->property("open").toBool());
    dialog->setProperty("url", QStringLiteral("not an address"));
    CHECK_FALSE(dialog->property("acceptEnabled").toBool());
    dialog->setProperty("url", QStringLiteral("example.com/countdown"));
    dialog->setProperty("name", QStringLiteral("Countdown"));
    REQUIRE(dialog->property("acceptEnabled").toBool());
    call(dialog, "submit");
    REQUIRE(pages->rowCount() == 1);
    const auto first = pages->index(0);
    CHECK(pages->data(first, MediaBrowserModel::NameRole).toString() == QStringLiteral("Countdown"));
    const auto id = pages->data(first, MediaBrowserModel::IdRole).toLongLong();
    const auto stored = facade.webLibrary()->page(id);
    CHECK(stored.value(QStringLiteral("url")).toString()
          == QStringLiteral("https://example.com/countdown"));
    CHECK(stored.value(QStringLiteral("width")).toInt() == 1920);
    // The new page is selected, so Edit opens it, and Add puts it in the
    // service under its name.
    REQUIRE(panel->property("canEdit").toBool());
    REQUIRE(panel->property("canAdd").toBool());
    const int before = facade.serviceItems()->rowCount();
    call(panel, "toolAdd");
    REQUIRE(rundown_titles(facade).size() == before + 1);
    CHECK(rundown_titles(facade).last() == QStringLiteral("Countdown"));

    call(panel->property("activeTab").value<QObject*>(), "editSelected");
    REQUIRE(dialog->property("open").toBool());
    CHECK(dialog->property("pageId").toLongLong() == id);
    CHECK(dialog->property("url").toString() == QStringLiteral("https://example.com/countdown"));
    dialog->setProperty("customSize", true);
    // Larger than 4K is allowed, with a warning of its cost.
    dialog->setProperty("pageWidth", 7680);
    dialog->setProperty("pageHeight", 4320);
    CHECK(dialog->property("acceptEnabled").toBool());
    CHECK(dialog->property("sizeHeavy").toBool());
    auto* heavy = dialog->findChild<QQuickItem*>(QStringLiteral("webPageHeavy"));
    REQUIRE(heavy != nullptr);
    CHECK(heavy->isVisible());
    dialog->setProperty("pageWidth", 3840);
    dialog->setProperty("pageHeight", 2160);
    CHECK_FALSE(dialog->property("sizeHeavy").toBool());
    dialog->setProperty("pageWidth", 1080);
    dialog->setProperty("pageHeight", 1920);
    dialog->setProperty("zoom", 150);
    call(dialog, "submit");
    const auto edited = facade.webLibrary()->page(id);
    CHECK(edited.value(QStringLiteral("width")).toInt() == 1080);
    CHECK(edited.value(QStringLiteral("height")).toInt() == 1920);
    CHECK(edited.value(QStringLiteral("zoom")).toDouble() == 1.5);
    CHECK(pages->rowCount() == 1);

    // Filed in a folder, and found there by a search for its address.
    const auto folder = facade.webLibrary()->createFolder(QStringLiteral("Screens"));
    REQUIRE(folder >= 0);

    // A folder goes into a service as its pages, one item each.
    const auto second = facade.webLibrary()
                             ->savePage(-1, {{QStringLiteral("url"), QStringLiteral("example.com")}})
                             .value(QStringLiteral("id"))
                             .toLongLong();
    REQUIRE(second >= 0);
    REQUIRE(QMetaObject::invokeMethod(browser, "selectFolder", Q_ARG(QVariant, QVariant(folder)),
                                      Q_ARG(QVariant, false)));
    CHECK(panel->property("canAdd").toBool());
    QVariant payload;
    REQUIRE(QMetaObject::invokeMethod(browser, "serviceAddPayload", Q_RETURN_ARG(QVariant, payload)));
    CHECK(payload.toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("web_folder"));

    // A drag of two pages says how many move.
    REQUIRE(QMetaObject::invokeMethod(browser, "selectItem", Q_ARG(QVariant, QVariant(id)),
                                      Q_ARG(QVariant, false)));
    REQUIRE(QMetaObject::invokeMethod(browser, "selectItem", Q_ARG(QVariant, QVariant(second)),
                                      Q_ARG(QVariant, true)));
    REQUIRE(QMetaObject::invokeMethod(browser, "beginDragFrom", Q_ARG(QVariant, QStringLiteral("file")),
                                      Q_ARG(QVariant, QVariant(id)), Q_ARG(QVariant, QString())));
    REQUIRE(QMetaObject::invokeMethod(browser, "updateDragAt",
                                      Q_ARG(QVariant, QVariant::fromValue<QObject*>(browser)),
                                      Q_ARG(QVariant, 20), Q_ARG(QVariant, 20),
                                      Q_ARG(QVariant, QStringLiteral("Countdown"))));
    CHECK(browser->property("dragGhostLabel").toString() == QStringLiteral("2 items"));
    REQUIRE(QMetaObject::invokeMethod(browser, "clearDrag"));
    // Duplicated and deleted from the tab, as its toolbar does.
    const int had = pages->rowCount();
    REQUIRE(QMetaObject::invokeMethod(browser, "selectItem", Q_ARG(QVariant, QVariant(second)),
                                      Q_ARG(QVariant, false)));
    REQUIRE(QMetaObject::invokeMethod(browser, "duplicateSelected"));
    REQUIRE(pages->rowCount() == had + 1);
    qint64 copy = -1;
    for (int r = 0; r < pages->rowCount(); ++r) {
        const auto row = pages->data(pages->index(r), MediaBrowserModel::IdRole).toLongLong();
        if (pages->data(pages->index(r), MediaBrowserModel::KindRole).toString() == QStringLiteral("file")
            && row != id && row != second) {
            copy = row;
        }
    }
    REQUIRE(copy >= 0);
    REQUIRE(QMetaObject::invokeMethod(browser, "selectItem", Q_ARG(QVariant, QVariant(copy)),
                                      Q_ARG(QVariant, true)));
    QVariant refused;
    REQUIRE(QMetaObject::invokeMethod(browser, "deleteSelected", Q_RETURN_ARG(QVariant, refused)));
    CHECK(refused.toString().isEmpty());
    CHECK(pages->rowCount() == had - 1);
    CHECK(facade.webLibrary()->page(second).isEmpty());
    CHECK(facade.webLibrary()->moveItems({QVariant::fromValue<qint64>(id)}, folder).isEmpty());
    facade.webLibrary()->setFolder(folder);
    CHECK(browser->property("currentFolderId").toLongLong() == folder);
    browser->setProperty("search", QStringLiteral("countdown"));
    QTest::qWait(10);
    bool found = false;
    for (int r = 0; r < pages->rowCount(); ++r) {
        found = found || pages->data(pages->index(r), MediaBrowserModel::IdRole).toLongLong() == id;
    }
    CHECK(found);
    browser->setProperty("search", QString());

    // Opened (a double-click), the page goes live, and the Controller shows
    // it, its address above the buttons, and Reload and Previous in place of
    // the slide steps.
    seventhslide::test::FakeWebRenderers renderers;
    facade.webLibrary()->thumbs().set_settle_ms(0);
    facade.set_web_renderer_factory(renderers.factory());

    // The page has its thumbnail in the list once drawn.
    QDeadlineTimer thumbs_deadline(3000);
    const auto thumb_of = [&] {
        for (int r = 0; r < pages->rowCount(); ++r) {
            if (pages->data(pages->index(r), MediaBrowserModel::IdRole).toLongLong() == id) {
                return pages->data(pages->index(r), MediaBrowserModel::ThumbUrlRole).toString();
            }
        }
        return QString();
    };
    while (thumb_of().isEmpty()) {
        REQUIRE_FALSE(thumbs_deadline.hasExpired());
        QTest::qWait(10);
    }
    CHECK(thumb_of().startsWith(QStringLiteral("file://")));

    REQUIRE(QMetaObject::invokeMethod(browser, "activateEntry", Q_ARG(QVariant, QStringLiteral("file")),
                                      Q_ARG(QVariant, QVariant(id)), Q_ARG(QVariant, QString()),
                                      Q_ARG(QVariant, false)));
    QTest::qWait(20);
    REQUIRE(facade.program()->webMode());
    // One renderer for the outputs (the thumbnails have their own).
    REQUIRE(facade.web_sources()->count() == 1);
    auto* live_page = dynamic_cast<seventhslide::test::FakeWebRenderer*>(
        facade.web_sources()->renderer(facade.program()->webSource().toStdString()));
    REQUIRE(live_page != nullptr);
    QObject* content = ui.window();
    auto* preview = seventhslide::test::program_well(content)->findChild<QQuickItem*>(QStringLiteral("liveWebPreview"));
    REQUIRE(preview != nullptr);
    CHECK(preview->isVisible());
    // The transport's buttons are a Repeater's: found by walking the items.
    const auto transport = [&ui](const char* action) -> QQuickItem* {
        const QString name = QStringLiteral("transport_") + QLatin1String(action);
        std::vector<QQuickItem*> todo{ui.window()->contentItem()};
        while (!todo.empty()) {
            QQuickItem* item = todo.back();
            todo.pop_back();
            if (item->objectName() == name && item->isVisible()) {
                return item;
            }
            for (auto* child : item->childItems()) {
                todo.push_back(child);
            }
        }
        return nullptr;
    };
    CHECK(transport("prev") == nullptr);
    CHECK(transport("next") == nullptr);
    auto* address = seventhslide::test::program_well(content)->findChild<QQuickItem*>(QStringLiteral("webAddress"));
    REQUIRE(address != nullptr);
    CHECK(address->isVisible());
    CHECK(address->property("text").toString() == QStringLiteral("https://example.com/countdown"));
    auto* reload = transport("webReload");
    REQUIRE(reload != nullptr);
    REQUIRE(reload->isVisible());
    // Each step drawn beside its word, as the web remote's are.
    CHECK_FALSE(reload->property("iconSource").toString().isEmpty());
    CHECK_FALSE(reload->property("iconAfter").toBool());
    click(ui.window(), centre_of(reload));
    CHECK(live_page->reloads_ == 1);

    // A link followed: the address follows it, and Previous goes back.
    auto* previous = transport("webBack");
    REQUIRE(previous != nullptr);
    CHECK_FALSE(previous->property("iconSource").toString().isEmpty());
    CHECK_FALSE(previous->isEnabled());
    live_page->follow(QUrl(QStringLiteral("https://example.com/next")));
    QTest::qWait(10);
    CHECK(address->property("text").toString() == QStringLiteral("https://example.com/next"));
    REQUIRE(previous->isEnabled());
    click(ui.window(), centre_of(previous));
    CHECK(address->property("text").toString() == QStringLiteral("https://example.com/countdown"));
    CHECK_FALSE(previous->isEnabled());

    // The page is left alone until clicked into: a pointer passing over it
    // is not the page's.
    QQuickItem* live_web = nullptr;
    for (auto* item : preview->findChildren<QQuickItem*>()) {
        if (QByteArray(item->metaObject()->className()).contains("LiveWebItem")) {
            live_web = item;
        }
    }
    REQUIRE(live_web != nullptr);
    REQUIRE(live_web->width() > 0);
    auto& inputs = live_page->inputs_;
    const auto heard = [&inputs](QEvent::Type type) {
        return std::ranges::any_of(inputs, [type](const auto& in) { return in.type == type; });
    };
    // (Hover is sent to the view itself: the test's window makes none of its own.)
    const QPointF over = live_web->mapToScene(QPointF(live_web->width() / 2, live_web->height() / 2));
    const auto hover_over = [&] {
        QHoverEvent move(QEvent::HoverMove, over, over, over);
        QCoreApplication::sendEvent(live_web, &move);
    };
    inputs.clear();
    hover_over();
    CHECK(inputs.empty());
    CHECK_FALSE(live_web->property("interacting").toBool());

    // The page is delivered its input in a window of its own, as the real
    // renderer's is: a click there must not take the click from the view here.
    QQuickWindow page_window;
    page_window.setGeometry(0, 0, 1080, 1920);
    struct PageArea : QQuickItem {
        int presses = 0;
        int releases = 0;
        PageArea() { setAcceptedMouseButtons(Qt::LeftButton); }
        void mousePressEvent(QMouseEvent* e) override
        {
            ++presses;
            e->accept();
        }
        void mouseReleaseEvent(QMouseEvent* e) override
        {
            ++releases;
            e->accept();
        }
    } page_area;
    page_area.setParentItem(page_window.contentItem());
    page_area.setSize(QSizeF(1080, 1920));
    live_page->page_window_ = &page_window;

    // The pointer is held as the windowing system would hold it.
    seventhslide::test::PointerHoldLog holds;
    facade.set_pointer_confinement(
        std::make_unique<seventhslide::test::FakePointerConfinement>(holds));
    const auto page_area_now = [live_web] {
        return live_web->mapRectToScene(QRectF(0, 0, live_web->width(), live_web->height()));
    };

    // The first click takes the page and goes no further; the tab on the
    // rim says how to give it back.
    click(ui.window(), centre_of(live_web));
    CHECK(live_web->property("interacting").toBool());
    CHECK(live_web->hasActiveFocus());
    CHECK_FALSE(heard(QEvent::MouseButtonPress));
    auto* tab = preview->findChild<QQuickItem*>(QStringLiteral("webPageTab"));
    REQUIRE(tab != nullptr);
    CHECK(tab->isVisible());
    const QString using_page = QStringLiteral("Using the page ·");
    CHECK(visible_text_under(tab, using_page) != nullptr);

    // While it is taken, the pointer is held on the page, and follows it
    // where the layout moves it.
    REQUIRE(holds.holds.size() == 1);
    CHECK(holds.holds[0].held);
    CHECK(holds.holds[0].window == ui.window());
    CHECK(holds.holds[0].area == page_area_now());
    const QSize window_size = ui.window()->size();
    const QRectF area_before = page_area_now();
    ui.window()->resize(window_size.width() - 160, window_size.height() - 80);
    QTest::qWait(60);
    REQUIRE(page_area_now() != area_before);
    CHECK(holds.holds[0].moves >= 1);
    CHECK(holds.holds[0].area == page_area_now());
    ui.window()->resize(window_size);
    QTest::qWait(60);
    CHECK(holds.holds[0].area == page_area_now());

    // From then on a click lands on the same place on the page, press and
    // release both, and the keys go to the page, not to the app's shortcuts.
    inputs.clear();
    click(ui.window(), centre_of(live_web));
    const auto press = std::ranges::find_if(inputs, [](const auto& in) {
        return in.type == QEvent::MouseButtonPress;
    });
    REQUIRE(press != inputs.end());
    // The page is 1080 x 1920 (portrait, as edited above).
    CHECK(std::abs(press->at.x() - 540) < 4);
    CHECK(std::abs(press->at.y() - 960) < 4);
    CHECK(heard(QEvent::MouseButtonRelease));
    CHECK(page_area.presses == 1);
    CHECK(page_area.releases == 1);
    live_page->page_window_ = nullptr;
    QTest::keyClick(ui.window(), Qt::Key_B);
    CHECK(std::ranges::any_of(inputs, [](const auto& in) {
        return in.type == QEvent::KeyPress && in.key == Qt::Key_B;
    }));
    CHECK_FALSE(facade.live()->global_blank());

    // The pointer passing over the page, and leaving the view, it hears.
    inputs.clear();
    hover_over();
    QHoverEvent leave(QEvent::HoverLeave, QPointF(-1, -1), QPointF(-1, -1), over);
    QCoreApplication::sendEvent(live_web, &leave);
    CHECK(heard(QEvent::MouseMove));
    CHECK(heard(QEvent::Leave));

    // Escape alone is the page's (its pop-ups, a video's full screen).
    inputs.clear();
    QTest::keyClick(ui.window(), Qt::Key_Escape);
    CHECK(live_web->property("interacting").toBool());
    CHECK(std::ranges::any_of(inputs, [](const auto& in) {
        return in.type == QEvent::KeyPress && in.key == Qt::Key_Escape;
    }));

    // Shift+Escape gives the page back (it is not the page's), and the page
    // loses the keys; it hears nothing more until clicked into again.
    inputs.clear();
    QTest::keyClick(ui.window(), Qt::Key_Escape, Qt::ShiftModifier);
    CHECK_FALSE(live_web->property("interacting").toBool());
    CHECK(heard(QEvent::FocusOut));
    CHECK((!tab->isVisible() || visible_text_under(tab, using_page) == nullptr));
    // Given back, the pointer goes where it likes.
    CHECK_FALSE(holds.holds[0].held);
    CHECK_FALSE(std::ranges::any_of(inputs, [](const auto& in) {
        return in.type == QEvent::KeyPress && in.key == Qt::Key_Escape;
    }));
    inputs.clear();
    hover_over();
    CHECK(inputs.empty());

    // So does a click anywhere else. (The page is portrait: there is room
    // beside it in the frame.)
    click(ui.window(), centre_of(live_web));
    REQUIRE(live_web->property("interacting").toBool());
    const QPointF beside = preview->mapToScene(QPointF(4, preview->height() / 2));
    REQUIRE_FALSE(live_web->contains(live_web->mapFromScene(beside)));
    click(ui.window(), beside.toPoint());
    CHECK_FALSE(live_web->property("interacting").toBool());
    CHECK_FALSE(holds.holds.back().held);

    // The system letting the pointer go (the person broke the hold) gives
    // the page back too.
    click(ui.window(), centre_of(live_web));
    REQUIRE(live_web->property("interacting").toBool());
    REQUIRE(holds.live != nullptr);
    holds.live->system_lets_go();
    QTest::qWait(10);
    CHECK_FALSE(live_web->property("interacting").toBool());

    // So does the page going out of sight: the pointer is never held over
    // nothing.
    click(ui.window(), centre_of(live_web));
    REQUIRE(live_web->property("interacting").toBool());
    REQUIRE(holds.holds.back().held);
    live_web->setVisible(false);
    CHECK_FALSE(live_web->property("interacting").toBool());
    CHECK_FALSE(holds.holds.back().held);
    facade.set_pointer_confinement(nullptr);

    // Deleted with the toolbar.
    REQUIRE(QMetaObject::invokeMethod(browser, "selectItem", Q_ARG(QVariant, QVariant(id)),
                                      Q_ARG(QVariant, false)));
    delete_confirmed(panel);
    CHECK(facade.webLibrary()->page(id).isEmpty());
    CHECK(ui.warnings().empty());
}

TEST_CASE("The controller's address bar sends the live page where it is typed",
          "[ui][library-tools][web]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    (void)build(facade, dir.path);
    seventhslide::test::FakeWebRenderers renderers;
    facade.set_web_renderer_factory(renderers.factory());
    const auto save = [&facade](const QString& url) {
        const auto saved = facade.webLibrary()->savePage(-1, {{QStringLiteral("url"), url}});
        REQUIRE(saved.contains(QStringLiteral("id")));
        return saved.value(QStringLiteral("id")).toLongLong();
    };
    const auto countdown = save(QStringLiteral("example.com/countdown"));
    const auto welcome = save(QStringLiteral("example.com/welcome"));

    OperatorUi ui(facade);
    QTest::qWait(50);
    auto* w = ui.window();
    w->requestActivate();
    REQUIRE(QTest::qWaitForWindowActive(w));
    REQUIRE(facade.webLibrary()->playPage(countdown));
    QTest::qWait(20);
    auto* live_page = dynamic_cast<seventhslide::test::FakeWebRenderer*>(
        facade.web_sources()->renderer(facade.program()->webSource().toStdString()));
    REQUIRE(live_page != nullptr);
    auto* address = seventhslide::test::program_well(w)->findChild<QQuickItem*>(QStringLiteral("webAddress"));
    REQUIRE(address != nullptr);
    REQUIRE(address->isVisible());
    const auto text = [address] { return address->property("text").toString(); };
    CHECK(text() == QStringLiteral("https://example.com/countdown"));
    auto* problem = seventhslide::test::program_well(w)->findChild<QObject*>(QStringLiteral("webAddressProblem"));
    REQUIRE(problem != nullptr);
    // The transport's buttons are a Repeater's: found by walking the items.
    const auto transport = [w](const char* action) -> QQuickItem* {
        const QString name = QStringLiteral("transport_") + QLatin1String(action);
        std::vector<QQuickItem*> todo{w->contentItem()};
        while (!todo.empty()) {
            QQuickItem* item = todo.back();
            todo.pop_back();
            if (item->objectName() == name && item->isVisible()) {
                return item;
            }
            for (auto* child : item->childItems()) {
                todo.push_back(child);
            }
        }
        return nullptr;
    };
    auto* previous = transport("webBack");
    REQUIRE(previous != nullptr);
    // A capital typed with Shift, as the keyboard sends it.
    const auto type_text = [w](const QString& typed) {
        for (const QChar c : typed) {
            QTest::keyClick(w, c.toLatin1(),
                            c.isUpper() ? Qt::ShiftModifier : Qt::NoModifier);
        }
    };
    const auto stack = facade.stacks()->focusedStackId();
    const auto& session = facade.session();

    // The first click takes the whole address, so what is typed replaces it,
    // and the keys typed are the bar's, not the app's shortcuts: an output's
    // number, B and F, with Shift or not, type, and the slide steps' keys do
    // nothing.
    click(w, centre_of(address));
    REQUIRE(address->hasActiveFocus());
    CHECK(address->property("selectedText").toString()
          == QStringLiteral("https://example.com/countdown"));
    type_text(QStringLiteral("example.org/1bfBF"));
    CHECK(text() == QStringLiteral("example.org/1bfBF"));
    CHECK_FALSE(facade.live()->global_blank());
    CHECK_FALSE(facade.live()->global_freeze());
    CHECK_FALSE(session.stack_is_blank(stack.toStdString()));
    CHECK_FALSE(session.stack_is_frozen(stack.toStdString()));
    for (const auto key : {Qt::Key_Up, Qt::Key_Down, Qt::Key_PageUp, Qt::Key_PageDown}) {
        QTest::keyClick(w, key);
    }
    CHECK(address->hasActiveFocus());
    CHECK(text() == QStringLiteral("example.org/1bfBF"));

    // The page moving on meanwhile, or the stack blanked from elsewhere,
    // leaves what is typed.
    live_page->follow(QUrl(QStringLiteral("https://example.com/elsewhere")));
    REQUIRE(facade.stacks()->toggleStackBlank(stack));
    REQUIRE(facade.stacks()->toggleStackBlank(stack));
    QTest::qWait(10);
    CHECK(text() == QStringLiteral("example.org/1bfBF"));

    // So does going to another window (to copy an address from) and back.
    QWindow other;
    other.resize(200, 100);
    other.show();
    other.requestActivate();
    REQUIRE(QTest::qWaitForWindowActive(&other));
    CHECK_FALSE(address->hasActiveFocus());
    CHECK(text() == QStringLiteral("example.org/1bfBF"));
    other.hide();
    w->requestActivate();
    REQUIRE(QTest::qWaitForWindowActive(w));
    CHECK(address->hasActiveFocus());
    CHECK(text() == QStringLiteral("example.org/1bfBF"));

    // Enter sends the page there, as a link followed there would: the bar
    // gives up the keys and shows where the page is, and Previous comes back.
    QTest::keyClick(w, Qt::Key_Return);
    REQUIRE(live_page->navigations_.size() == 1);
    CHECK(live_page->navigations_.back() == QUrl(QStringLiteral("https://example.org/1bfBF")));
    CHECK_FALSE(address->hasActiveFocus());
    CHECK(text() == QStringLiteral("https://example.org/1bfBF"));
    click(w, centre_of(previous));
    CHECK(text() == QStringLiteral("https://example.com/elsewhere"));
    click(w, centre_of(previous));
    CHECK(text() == QStringLiteral("https://example.com/countdown"));
    CHECK_FALSE(previous->isEnabled());

    // Enter with nothing typed only leaves the bar: the page is not loaded
    // again on the outputs.
    click(w, centre_of(address));
    REQUIRE(address->hasActiveFocus());
    QTest::keyClick(w, Qt::Key_Return);
    CHECK_FALSE(address->hasActiveFocus());
    CHECK(live_page->navigations_.size() == 1);
    CHECK(live_page->reloads_ == 0);

    // The page moving on before anything is typed: the bar shows where it
    // is, still all taken, so a paste would replace it.
    click(w, centre_of(address));
    REQUIRE(address->hasActiveFocus());
    live_page->follow(QUrl(QStringLiteral("https://example.com/next")));
    QTest::qWait(10);
    CHECK(text() == QStringLiteral("https://example.com/next"));
    CHECK(address->property("selectedText").toString()
          == QStringLiteral("https://example.com/next"));
    QTest::keyClick(w, Qt::Key_Escape);
    live_page->go_back();
    QTest::qWait(10);

    // What is not an address is refused, kept to be put right, and says why;
    // Escape puts back where the page is.
    click(w, centre_of(address));
    REQUIRE(address->hasActiveFocus());
    type_text(QStringLiteral("two words"));
    QTest::keyClick(w, Qt::Key_Return);
    CHECK(live_page->navigations_.size() == 1);
    CHECK(address->hasActiveFocus());
    CHECK(text() == QStringLiteral("two words"));
    CHECK(problem->property("visible").toBool());
    CHECK(problem->property("text").toString().contains(QStringLiteral("web address")));
    QTest::keyClick(w, Qt::Key_Escape);
    CHECK_FALSE(address->hasActiveFocus());
    CHECK_FALSE(problem->property("visible").toBool());
    CHECK(text() == QStringLiteral("https://example.com/countdown"));

    // Working anything else (a transport button takes no keys) gives the
    // keys back to the app's shortcuts.
    click(w, centre_of(address));
    REQUIRE(address->hasActiveFocus());
    auto* reload = transport("webReload");
    REQUIRE(reload != nullptr);
    click(w, centre_of(reload));
    CHECK(live_page->reloads_ == 1);
    CHECK_FALSE(address->hasActiveFocus());
    QTest::keyClick(w, Qt::Key_B);
    CHECK(facade.live()->global_blank());
    QTest::keyClick(w, Qt::Key_B);
    CHECK_FALSE(facade.live()->global_blank());
    CHECK(text() == QStringLiteral("https://example.com/countdown"));

    // Another page going live drops what was typed for this one, so Enter
    // cannot send that page where this one was meant to go.
    click(w, centre_of(address));
    REQUIRE(address->hasActiveFocus());
    type_text(QStringLiteral("example.org"));
    REQUIRE(facade.webLibrary()->playPage(welcome));
    QTest::qWait(20);
    CHECK_FALSE(address->hasActiveFocus());
    CHECK(text() == QStringLiteral("https://example.com/welcome"));
    CHECK(ui.warnings().empty());
}

TEST_CASE("A page cued in Preview is worked in the Preview well, the live one left as it is",
          "[ui][library-tools][web][preview]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    (void)build(facade, dir.path);
    seventhslide::test::FakeWebRenderers renderers;
    facade.set_web_renderer_factory(renderers.factory());
    const auto save = [&facade](const QString& url) {
        const auto saved = facade.webLibrary()->savePage(-1, {{QStringLiteral("url"), url}});
        REQUIRE(saved.contains(QStringLiteral("id")));
        return saved.value(QStringLiteral("id")).toLongLong();
    };
    const auto live_id = save(QStringLiteral("example.com/live"));
    const auto cued_id = save(QStringLiteral("example.com/cued"));
    const auto stack = facade.stacks()->focusedStackId();
    REQUIRE(facade.stacks()->setStackPreview(stack, true).isEmpty());

    OperatorUi ui(facade);
    QTest::qWait(50);
    auto* w = ui.window();
    w->requestActivate();
    REQUIRE(QTest::qWaitForWindowActive(w));
    REQUIRE(facade.webLibrary()->playPage(live_id, /*live=*/true));
    REQUIRE(facade.webLibrary()->playPage(cued_id));
    QTest::qWait(20);
    const auto page_of = [&](const seventhslide::app::SideView* view) {
        return dynamic_cast<seventhslide::test::FakeWebRenderer*>(
            facade.web_sources()->renderer(view->webSource().toStdString()));
    };
    auto* live_page = page_of(facade.program());
    auto* cued_page = page_of(facade.preview());
    REQUIRE(live_page != nullptr);
    REQUIRE(cued_page != nullptr);
    REQUIRE(live_page != cued_page);

    // The Preview well shows the cued page and its address.
    auto* well = w->findChild<QQuickItem*>(QStringLiteral("previewWell"));
    REQUIRE(well != nullptr);
    auto* picture = well->findChild<QQuickItem*>(QStringLiteral("liveWebPreview"));
    REQUIRE(picture != nullptr);
    CHECK(picture->isVisible());
    auto* address = well->findChild<QQuickItem*>(QStringLiteral("webAddress"));
    REQUIRE(address != nullptr);
    REQUIRE(address->isVisible());
    CHECK(address->property("text").toString() == QStringLiteral("https://example.com/cued"));

    // An address typed there sends the cued page, and only it.
    click(w, centre_of(address));
    REQUIRE(address->hasActiveFocus());
    for (const QChar c : QStringLiteral("example.org")) {
        QTest::keyClick(w, c.toLatin1());
    }
    QTest::keyClick(w, Qt::Key_Return);
    REQUIRE(cued_page->navigations_.size() == 1);
    CHECK(cued_page->navigations_.back() == QUrl(QStringLiteral("https://example.org")));
    CHECK(live_page->navigations_.empty());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Library toolbar on Live: sources found are played and put in the service",
          "[ui][library-tools][live]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    (void)build(facade, dir.path);
    facade.liveLibrary()->set_finder([] {
        return std::vector<seventhslide::model::LiveSource>{{"ndi", "STAGE (Camera 1)"},
                                                            {"ndi", "STAGE (Camera 2)"}};
    });
    seventhslide::test::FakeLiveReceivers receivers;
    facade.set_live_receiver_factory(receivers.factory());

    OperatorUi ui(facade, QStringLiteral("live"));
    QTest::qWait(50);
    QObject* panel = ui.find_component("LibraryPanel");
    REQUIRE(panel != nullptr);
    QObject* tab = panel->property("activeTab").value<QObject*>();
    REQUIRE(tab != nullptr);
    CHECK(tab->property("title").toString() == QStringLiteral("Live"));
    // Showing, it looks for sources.
    CHECK(facade.liveLibrary()->searching());
    auto* list = qobject_cast<QQuickItem*>(tab->findChild<QObject*>(QStringLiteral("liveSourceList")));
    REQUIRE(list != nullptr);
    REQUIRE(list->property("count").toInt() == 2);

    // The search narrows the list.
    tab->setProperty("search", QStringLiteral("camera 2"));
    QTest::qWait(10);
    CHECK(list->property("count").toInt() == 1);
    tab->setProperty("search", QString());
    // The list knows its rows at once and shows them at its next layout: until
    // then the first row is still the one the search left (Camera 2).
    REQUIRE(QTest::qWaitFor([&] {
        QQuickItem* first = nullptr;
        QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, first), Q_ARG(int, 0));
        return list->property("count").toInt() == 2 && first != nullptr
               && visible_text_under(first, QStringLiteral("STAGE (Camera 1)")) != nullptr;
    }, 5000));

    // A click selects; Service adds it.
    auto* w = ui.window();
    click(w, centre_of(row_of(list, 0)));
    CHECK(tab->property("selectedName").toString() == QStringLiteral("STAGE (Camera 1)"));
    REQUIRE(panel->property("canAdd").toBool());
    const int before = facade.serviceItems()->rowCount();
    call(panel, "toolAdd");
    REQUIRE(rundown_titles(facade).size() == before + 1);
    CHECK(rundown_titles(facade).last() == QStringLiteral("STAGE (Camera 1)"));

    // Ctrl adds a row to the selection, as on every library tab; Service adds
    // them all, in the list's order.
    click(w, centre_of(row_of(list, 1)), Qt::ControlModifier);
    CHECK(tab->property("selectedKeys").toList().size() == 2);
    call(panel, "toolAdd");
    REQUIRE(rundown_titles(facade).size() == before + 3);
    CHECK(rundown_titles(facade).mid(before + 1)
          == QStringList{QStringLiteral("STAGE (Camera 1)"), QStringLiteral("STAGE (Camera 2)")});

    // Dragged onto the service, it is added there too.
    auto* service = qobject_cast<QQuickItem*>(ui.find_component("ServicePanel"));
    REQUIRE(service != nullptr);
    click(w, centre_of(row_of(list, 0)));
    seventhslide::test::drag(ui.window(), centre_of(row_of(list, 1)), centre_of(service));
    REQUIRE(rundown_titles(facade).size() == before + 4);
    CHECK(rundown_titles(facade).contains(QStringLiteral("STAGE (Camera 2)")));
    CHECK_FALSE(facade.program()->liveSourceMode());  // dragged, not played

    // A double-click shows it live; the Controller shows its picture. The
    // rows stay the rows they were (the list is not made again, which would
    // scroll it to its top).
    QQuickItem* first_row = row_of(list, 0);
    QTest::mouseDClick(w, Qt::LeftButton, {}, centre_of(row_of(list, 1)));
    QTest::qWait(20);
    REQUIRE(facade.program()->liveSourceMode());
    CHECK(facade.session().focused_stack().program.scene_extras().live.value().name == "STAGE (Camera 2)");
    // Each row's dot says its picture came; each has its thumbnail.
    QTest::qWait(10);
    auto* dot1 = row_of(list, 0)->findChild<QQuickItem*>(QStringLiteral("signalDot"));
    auto* dot2 = row_of(list, 1)->findChild<QQuickItem*>(QStringLiteral("signalDot"));
    REQUIRE(dot1 != nullptr);
    REQUIRE(dot2 != nullptr);
    CHECK(dot1->isVisible());
    CHECK(dot2->isVisible());
    CHECK(row_of(list, 0) == first_row);
    // Each a still, its preview stream closed once it gave one.
    CHECK(facade.liveLibrary()->previews() == 0);
    CHECK_FALSE(facade.liveLibrary()->thumbnail(QStringLiteral("ndi:STAGE (Camera 1)")).isNull());
    CHECK_FALSE(facade.liveLibrary()->thumbnail(QStringLiteral("ndi:STAGE (Camera 2)")).isNull());

    // + adds a stream by its address; it is listed, and edited and forgotten
    // from the toolbar (found sources are neither).
    CHECK_FALSE(panel->property("canDelete").toBool());
    call(tab, "createItem");
    QObject* dialog = qml_object(tab, "StreamDialog");
    REQUIRE(dialog != nullptr);
    REQUIRE(dialog->property("open").toBool());
    dialog->setProperty("url", QStringLiteral("not a stream"));
    CHECK_FALSE(dialog->property("acceptEnabled").toBool());
    dialog->setProperty("url", QStringLiteral("srt://stage.local:9000"));
    dialog->setProperty("name", QStringLiteral("Stage feed"));
    REQUIRE(dialog->property("acceptEnabled").toBool());
    call(dialog, "submit");
    QTest::qWait(10);
    REQUIRE(list->property("count").toInt() == 3);
    CHECK(tab->property("selectedName").toString() == QStringLiteral("Stage feed"));
    CHECK(tab->property("selectedUrl").toString() == QStringLiteral("srt://stage.local:9000"));
    REQUIRE(panel->property("canDelete").toBool());
    CHECK(panel->property("canEdit").toBool());
    delete_confirmed(panel);
    QTest::qWait(10);
    CHECK(list->property("count").toInt() == 2);
    // One receiver for the outputs (the tab's thumbnails have their own).
    REQUIRE(facade.live_sources()->count() == 1);
    auto* preview = seventhslide::test::program_well(w)->findChild<QQuickItem*>(QStringLiteral("liveSourcePreview"));
    REQUIRE(preview != nullptr);
    CHECK(preview->isVisible());
    // Its picture from the start (the receiver had one when it was made).
    CHECK(facade.live_sources()->frames().size() == 1);
    CHECK(facade.program()->liveSourceProblem().isEmpty());
    CHECK(ui.warnings().empty());
}


// An NDI source can leave the network while it is selected.
TEST_CASE("Library toolbar on Live: a source that is no longer found leaves the selection",
          "[ui][library-tools][live]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    (void)build(facade, dir.path);
    auto found = std::make_shared<std::vector<seventhslide::model::LiveSource>>(
        std::vector<seventhslide::model::LiveSource>{{"ndi", "STAGE (Camera 1)"},
                                                     {"ndi", "STAGE (Camera 2)"}});
    facade.liveLibrary()->set_finder([found] { return *found; });
    seventhslide::test::FakeLiveReceivers receivers;
    facade.set_live_receiver_factory(receivers.factory());

    OperatorUi ui(facade, QStringLiteral("live"));
    QTest::qWait(50);
    QObject* panel = ui.find_component("LibraryPanel");
    REQUIRE(panel != nullptr);
    QObject* tab = panel->property("activeTab").value<QObject*>();
    REQUIRE(tab != nullptr);
    auto* list = qobject_cast<QQuickItem*>(tab->findChild<QObject*>(QStringLiteral("liveSourceList")));
    REQUIRE(list != nullptr);
    REQUIRE(list->property("count").toInt() == 2);
    auto* w = ui.window();
    click(w, centre_of(row_of(list, 0)));
    click(w, centre_of(row_of(list, 1)), Qt::ControlModifier);
    REQUIRE(tab->property("selectedKeys").toList().size() == 2);

    // One the search hides is still found, and stays selected.
    tab->setProperty("search", QStringLiteral("camera 1"));
    QTest::qWait(10);
    CHECK(tab->property("selectedKeys").toList().size() == 2);
    tab->setProperty("search", QString());
    QTest::qWait(10);

    // Camera 2 goes: Service adds the one left.
    found->pop_back();
    facade.liveLibrary()->refresh();
    QTest::qWait(10);
    CHECK(tab->property("selectedKeys").toList()
          == QVariantList{QStringLiteral("ndi:STAGE (Camera 1)")});
    CHECK(tab->property("selectedName").toString() == QStringLiteral("STAGE (Camera 1)"));
    REQUIRE(panel->property("canAdd").toBool());
    const int before = facade.serviceItems()->rowCount();
    call(panel, "toolAdd");
    REQUIRE(rundown_titles(facade).size() == before + 1);
    CHECK(rundown_titles(facade).last() == QStringLiteral("STAGE (Camera 1)"));

    // Camera 1 goes too: there is nothing to add.
    found->clear();
    facade.liveLibrary()->refresh();
    QTest::qWait(10);
    CHECK(tab->property("selectedKeys").toList().isEmpty());
    CHECK_FALSE(panel->property("canAdd").toBool());
    CHECK(ui.warnings().empty());
}


TEST_CASE("Library toolbar: New folder is in the create menu, and each tab exports its own way",
          "[ui][library-tools][export]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    (void)build(facade, dir.path);
    facade.media()->setMediaViewMode(QStringLiteral("list"));
    OperatorUi ui(facade);
    QObject* panel = ui.find_component("LibraryPanel");
    REQUIRE(panel != nullptr);
    auto* w = ui.window();

    // Songs, Freeform, Bibles, Videos, Images, Audio, then Browser and Live.
    struct Expect {
        QStringList exports;
        bool folders;
    };
    const std::vector<Expect> tabs{
        {{"xml", "images"}, false}, {{"images", "file"}, true}, {{"images"}, false}, {{"files"}, true},
        {{"files"}, true},          {{"files"}, true},  {{}, true},          {{}, false},
    };
    for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
        INFO("tab " << i);
        REQUIRE(panel->setProperty("tabIndex", i));
        QTest::qWait(20);
        QObject* tab = active_tab(panel);
        QStringList exports;
        for (const auto& choice : tab->property("exportChoices").toList()) {
            exports.push_back(choice.toMap().value(QStringLiteral("id")).toString());
        }
        CHECK(exports == tabs[static_cast<std::size_t>(i)].exports);
        CHECK(tab->property("offersExport").toBool() == !exports.isEmpty());
        CHECK(tab->property("offersNewFolder").toBool() == tabs[static_cast<std::size_t>(i)].folders);
    }

    // Images: + opens a menu of its one way in, and New folder.
    REQUIRE(panel->setProperty("tabIndex", 4));
    QTest::qWait(20);
    QObject* create_menu = ui.window()->findChild<QObject*>(QStringLiteral("createMenu"));
    REQUIRE(create_menu != nullptr);
    auto* create_button = create_menu->property("anchorItem").value<QQuickItem*>();
    REQUIRE(create_button != nullptr);
    click(w, centre_of(create_button));
    CHECK(create_menu->property("visible").toBool());
    REQUIRE(QMetaObject::invokeMethod(create_menu, "close"));
    QTest::qWait(20);

    // Export waits for a selection; Songs' images ask which output first.
    REQUIRE(panel->setProperty("tabIndex", 0));
    QTest::qWait(20);
    QObject* songs = active_tab(panel);
    CHECK_FALSE(songs->property("canExport").toBool());
    QQuickItem* list = list_view_of(panel, facade.library()->property("songs").value<QObject*>());
    REQUIRE(list != nullptr);
    click(w, centre_of(row_of(list, 0)));
    REQUIRE(songs->property("canExport").toBool());
    REQUIRE(QMetaObject::invokeMethod(songs, "exportChosen", Q_ARG(QVariant, QStringLiteral("images"))));
    QTest::qWait(20);
    QObject* ask = qml_object(songs, "ExportImagesDialog");
    REQUIRE(ask != nullptr);
    CHECK(ask->property("open").toBool());
    CHECK(ask->property("fileTitle").toString() == QStringLiteral("Alpha"));
    REQUIRE(QMetaObject::invokeMethod(ask, "close"));
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}
