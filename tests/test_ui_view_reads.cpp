// What the operator window's views read and say: model rows by role, the
// destination type names, the confirms that name a row, and next / previous.
// Each goes through the QML function the view itself calls.

#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "font_controller.hpp"
#include "glyphs.hpp"
#include "library_controller.hpp"
#include "library_models.hpp"
#include "media_controller.hpp"
#include "operator_ui.hpp"
#include "sample_library.hpp"
#include "output_list_model.hpp"
#include "profile_controller.hpp"
#include "service_item_model.hpp"
#include "service_list_model.hpp"
#include "service_thumbs.hpp"
#include "stack_controller.hpp"
#include "facade_all.hpp"
#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"
#include "ui_library_fixture.hpp"
#include "ui_test_helpers.hpp"
#include "wav_fixture.hpp"

#include <seventhslide/db/bible_repository.hpp>
#include <seventhslide/db/font_repository.hpp>
#include <seventhslide/db/media_repository.hpp>
#include <seventhslide/db/song_repository.hpp>
#include <seventhslide/model/content.hpp>
#include <seventhslide/model/stack.hpp>

#include <QElapsedTimer>
#include <QLocale>
#include <QMetaObject>
#include <QQmlEngine>
#include <QQuickItem>
#include <QSet>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QTcpServer>
#include <QTime>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <filesystem>

namespace MediaKindQml = seventhslide::app::MediaKindQml;

using seventhslide::app::SessionFacade;
using seventhslide::test::OperatorUi;
using seventhslide::test::centre_of;
using seventhslide::test::click;
using seventhslide::test::list_view_of;
using seventhslide::test::qml_object;
using seventhslide::test::row_of;
using seventhslide::test::visible_items_of;
using seventhslide::test::visible_qml_items_of;
using seventhslide::test::visible_button_under;
using seventhslide::test::visible_text_under;
using namespace seventhslide::test::ui;

namespace {

/// ``object.method(args...)``, as QML calls it.
template <class... Args>
QVariant call(QObject* object, const char* method, const Args&... args)
{
    QVariant out;
    REQUIRE(QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QVariant, out),
                                      Q_ARG(QVariant, QVariant(args))...));
    return out;
}

/// ``row``'s ``role`` straight from the C++ model.
QVariant model_value(const QAbstractItemModel* model, int row, int role)
{
    return model->data(model->index(row, 0), role);
}

}  // namespace

TEST_CASE("Operator views read their models' rows by role", "[ui][roles]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    const Library lib = build(facade, dir.path);
    const auto& it = lib.items;
    using seventhslide::app::ServiceItemModel;
    const auto* items = facade.serviceItems();
    // A companion has to play on another stack.
    REQUIRE(facade.stacks()->addStack(QStringLiteral("Lower"), QStringLiteral("layers")).isEmpty());
    QString lower;
    for (const auto& row : facade.stacks()->stacks()) {
        const auto m = row.toMap();
        if (m.value(QStringLiteral("name")).toString() == QLatin1String("Lower")) {
            lower = m.value(QStringLiteral("id")).toString();
        }
    }
    REQUIRE_FALSE(lower.isEmpty());
    REQUIRE(facade.library()->set_service_item_stack(it[1], lower).isEmpty());
    const QString linked = facade.library()->set_service_item_also_play(it[0], QVariantList{it[1]});
    INFO(linked.toStdString());
    REQUIRE(linked.isEmpty());

    OperatorUi ui(facade);
    QTest::qWait(50);

    SECTION("the service rundown")
    {
        QObject* column = ui.find_component("ServicePanel");
        REQUIRE(column != nullptr);
        CHECK(call(column, "serviceItemIdAtRow", 2).toLongLong() == it[2]);
        CHECK(call(column, "serviceItemTypeAtRow", 2).toString() == QStringLiteral("song"));
        // it[1] was moved to the Lower stack above.
        CHECK(call(column, "stackIdForItemId", it[1]).toString() == lower);
        CHECK(call(column, "stackIdForItemId", it[2]).toString()
              == model_value(items, 2, ServiceItemModel::StackIdRole).toString());
        // Both rows of the linked pair are in the same set.
        CHECK(model_value(items, 0, ServiceItemModel::LinkSetRole).toInt() == 0);
        CHECK(model_value(items, 1, ServiceItemModel::LinkSetRole).toInt() == 0);

        QQuickItem* list = list_view_of(column, items);
        REQUIRE(list != nullptr);
        click(ui.window(), centre_of(row_of(list, 0)));
        CHECK(column->property("selectedItemId").toLongLong() == it[0]);
        CHECK(call(column, "selectedItemTitle").toString() == QStringLiteral("Alpha"));
        CHECK(column->property("selectionIsLinked").toBool());
        click(ui.window(), centre_of(row_of(list, 3)));
        CHECK_FALSE(column->property("selectionIsLinked").toBool());

        // Undoing the link leaves every row where it was; the menu follows.
        click(ui.window(), centre_of(row_of(list, 0)));
        REQUIRE(column->property("selectionIsLinked").toBool());
        REQUIRE(QMetaObject::invokeMethod(column, "linkSelectedItems"));
        QTest::qWait(20);
        CHECK(model_value(items, 0, ServiceItemModel::LinkSetRole).toInt() == -1);
        CHECK_FALSE(column->property("selectionIsLinked").toBool());
        // And linking it again shows it.
        REQUIRE(facade.library()->set_service_item_also_play(it[0], QVariantList{it[1]}).isEmpty());
        QTest::qWait(20);
        CHECK(column->property("selectionIsLinked").toBool());
    }

    SECTION("the output strip")
    {
        QObject* column = ui.find_component("OutputsPanel");
        REQUIRE(column != nullptr);
        const auto* outputs = facade.outputsOn();
        REQUIRE(outputs->rowCount() > 0);
        CHECK(call(column, "outputNameAt", 0).toString()
              == model_value(outputs, 0, seventhslide::app::OutputListModel::NameRole).toString());
        CHECK(call(column, "outputNameAt", outputs->rowCount()).toString().isEmpty());
    }

    SECTION("the media browser")
    {
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
        using seventhslide::app::MediaBrowserModel;
        const auto* model = facade.media()->browser(seventhslide::db::MediaKind::Image);
        REQUIRE(model != nullptr);
        REQUIRE(model->rowCount() == 4);  // a.png .. d.png

        // At the root a file is listed loose: no folder item, so no id.
        QVariantMap entry = call(browser, "browserEntry", 1).toMap();
        CHECK(entry.value(QStringLiteral("kind")).toString() == QStringLiteral("file"));
        CHECK(entry.value(QStringLiteral("filename")).toString() == QStringLiteral("b.png"));
        CHECK(entry.value(QStringLiteral("entryId")).toLongLong()
              == model_value(model, 1, MediaBrowserModel::IdRole).toLongLong());
        CHECK(call(browser, "browserEntry", 4).isNull());
        CHECK(call(browser, "fileIdsForFilenames", QVariantList{QStringLiteral("b.png")})
                  .toList()
                  .isEmpty());

        // Inside a folder the file is a folder item, and its row carries that id.
        using seventhslide::db::MediaKind;
        const auto folder = facade.media_repo()->folders(MediaKind::Image).create("Set");
        const auto item = facade.media_repo()->add_folder_item(MediaKind::Image, folder, "b.png");
        REQUIRE(item >= 0);
        facade.media()->library(MediaKindQml::Image)->setFolder(folder);
        QTest::qWait(20);
        REQUIRE(model->rowCount() == 2);  // the way up, then b.png
        CHECK(call(browser, "browserEntry", 0).toMap().value(QStringLiteral("kind")).toString()
              == QStringLiteral("parent"));
        entry = call(browser, "browserEntry", 1).toMap();
        CHECK(entry.value(QStringLiteral("kind")).toString() == QStringLiteral("file"));
        CHECK(entry.value(QStringLiteral("entryId")).toLongLong() == item);
        const QVariantList ids =
            call(browser, "fileIdsForFilenames",
                 QVariantList{QStringLiteral("b.png"), QStringLiteral("c.png")})
                .toList();
        REQUIRE(ids.size() == 1);
        CHECK(ids.front().toLongLong() == item);
    }

    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("A media file on its way says so where it is listed", "[ui][media][sync]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    (void)build(facade, dir.path);
    REQUIRE(facade.media()->addImageToService(QStringLiteral("a.png")));

    OperatorUi ui(facade);
    QObject* panel = ui.find_component("LibraryPanel");
    REQUIRE(panel != nullptr);
    REQUIRE(panel->setProperty("tabIndex", 4));
    QTest::qWait(50);
    const auto marks = [&] {
        QStringList out;
        for (QQuickItem* mark : visible_qml_items_of(ui.window()->contentItem(), "ArrivalMark")) {
            out << mark->property("arrival").toMap().value(QStringLiteral("text")).toString();
        }
        return out;
    };
    REQUIRE(marks().isEmpty());

    using seventhslide::app::FileArrival;
    facade.media()->set_arrivals({
        {"images/a.png", {FileArrival::State::Downloading, 500, 1000}},
        {"images/c.png", {FileArrival::State::Nowhere, 0, 0}},
    });
    QTest::qWait(50);
    // In the image browser, and in the service it is in.
    CHECK(marks().count(QStringLiteral("Downloading 50%")) == 2);
    CHECK(marks().count(QStringLiteral("Missing on every computer connected")) == 1);
    // What they say may hold another computer's name: never taken for markup.
    for (QQuickItem* mark : visible_qml_items_of(ui.window()->contentItem(), "ArrivalMark")) {
        for (QQuickItem* words : seventhslide::test::visible_items_of(mark, "QQuickText")) {
            CHECK(words->property("textFormat").toInt() == Qt::PlainText);
        }
    }

    facade.media()->set_arrivals({});
    QTest::qWait(50);
    CHECK(marks().isEmpty());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("The controller says what live needs that is still on its way", "[ui][media][sync]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    (void)build(facade, dir.path);
    REQUIRE(facade.media()->addImageToService(QStringLiteral("a.png")));
    OperatorUi ui(facade);
    QTest::qWait(50);
    const auto notes = [&] { return visible_qml_items_of(ui.window()->contentItem(), "ControllerArrivalNote"); };
    REQUIRE(notes().empty());

    using seventhslide::app::FileArrival;
    facade.media()->set_arrivals({{"images/a.png", {FileArrival::State::Downloading, 250, 1000}}});
    const int row = facade.serviceItems()->rowCount() - 1;
    REQUIRE(facade.live()->selectServiceItem(row));
    REQUIRE(QTest::qWaitFor([&] { return notes().size() == 1; }, 2000));
    auto* note = notes().front();
    // Its lines have their width once the note is laid out, a frame after it
    // shows; until then none of them counts as visible.
    CHECK(QTest::qWaitFor(
        [&] {
            return visible_text_under(note, QStringLiteral("Live uses a file that is not on this computer. "
                                                           "It shows once it has come."))
                   != nullptr;
        },
        2000));
    CHECK(visible_text_under(note, QStringLiteral("a.png")) != nullptr);
    CHECK(visible_text_under(note, QStringLiteral("Downloading 25%")) != nullptr);

    // Paused for what is live: it says so, and how to let it come.
    facade.media()->set_sync_paused(true);
    QTest::qWait(50);
    CHECK(visible_text_under(note, QStringLiteral("Paused at 25%")) != nullptr);
    bool says_paused = false;
    for (QQuickItem* text : seventhslide::test::visible_items_of(note, "QQuickText")) {
        says_paused = says_paused || text->property("text").toString().contains(QStringLiteral("Pause during services"));
    }
    CHECK(says_paused);

    // On no computer connected: it says so, and promises nothing.
    facade.media()->set_arrivals({{"images/a.png", {FileArrival::State::Nowhere, 0, 0}}});
    QTest::qWait(50);
    CHECK(visible_text_under(note, QStringLiteral("Live uses a file that is not on this computer.")) != nullptr);
    CHECK(visible_text_under(note, QStringLiteral("Missing on every computer connected")) != nullptr);
    bool still_paused = false;
    for (QQuickItem* text : seventhslide::test::visible_items_of(note, "QQuickText")) {
        still_paused = still_paused || text->property("text").toString().contains(QStringLiteral("Pause during services"));
    }
    CHECK_FALSE(still_paused);

    facade.media()->set_arrivals({});
    QTest::qWait(50);
    CHECK(notes().empty());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("The output editor names each destination type", "[ui][output-edit]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    OperatorUi ui(facade);
    QObject* dialog = ui.find_component("OutputEditDialog");
    REQUIRE(dialog != nullptr);
    QObject* state = qml_object(dialog, "OutputEditState");
    REQUIRE(state != nullptr);
    QObject* dests = state->property("destState").value<QObject*>();
    REQUIRE(dests != nullptr);

    const auto label = [dests](const char* t) {
        return call(dests, "destTypeLabel", QString::fromLatin1(t)).toString();
    };
    const auto icon = [dests](const char* t) {
        return call(dests, "destTypeIcon", QString::fromLatin1(t)).toString();
    };
    CHECK(label("display") == QStringLiteral("Display"));
    CHECK(label("ndi") == QStringLiteral("NDI"));
    CHECK(label("bmd") == QStringLiteral("Blackmagic"));
    CHECK(label("vcam") == QStringLiteral("Virtual Camera"));
    CHECK(icon("display") == QStringLiteral("display"));
    CHECK(icon("ndi") == QStringLiteral("ndi"));
    CHECK(icon("bmd") == QStringLiteral("bmd"));
    CHECK(icon("vcam") == QStringLiteral("video"));
    // An unknown type reads as a display.
    CHECK(label("smoke-signal") == QStringLiteral("Display"));
    CHECK(icon("smoke-signal") == QStringLiteral("display"));
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Settings: the Fonts page says what is missing and draws each face", "[ui][settings][fonts]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.init_libraries(dir.path / "data"));
    auto* db = facade.open_library().database();
    db->exec(R"(INSERT INTO freeform_items (name, document)
                VALUES ('one', '{"objects":[{"font_family":"Gotham"}]}'))");
    facade.fonts()->refresh();
    OperatorUi ui(facade);
    REQUIRE(QTest::qWaitFor([&] { return seventhslide::db::FontRepository(*db).bundled_taken(); }, 20000));
    QTest::qWait(50);

    // The bar says so, and goes to where it is put right.
    QObject* dialog = ui.find_component("AppSettingsDialog");
    REQUIRE(dialog != nullptr);
    auto* bar = qobject_cast<QQuickItem*>(ui.find_component("AppBar"));
    REQUIRE(bar != nullptr);
    QQuickItem* warning = visible_text_under(bar, QStringLiteral("1 font missing"));
    REQUIRE(warning != nullptr);
    click(ui.window(), centre_of(warning));
    QTest::qWait(50);
    CHECK(dialog->property("open").toBool());
    CHECK(dialog->property("tab").toString() == QStringLiteral("fonts"));

    auto* sheet = qobject_cast<QQuickItem*>(dialog);
    CHECK(visible_text_under(sheet, QStringLiteral("Missing")) != nullptr);
    // The missing under their heading, then the library's under theirs.
    auto* families = sheet->findChild<QQuickItem*>(QStringLiteral("fontFamilies"));
    REQUIRE(families != nullptr);
    const auto texts = visible_items_of(families, "QQuickText");
    const auto heading = [&](const QString& title) {
        const auto found = std::ranges::find_if(
            texts, [&](const QQuickItem* text) { return text->property("text").toString() == title; });
        const auto count = std::ranges::count_if(
            texts, [&](const QQuickItem* text) { return text->property("text").toString() == title; });
        CHECK(count == 1);
        return found == texts.end() ? nullptr : *found;
    };
    const auto* missing_heading = heading(QStringLiteral("Missing"));
    const auto* library_heading = heading(QStringLiteral("In this library"));
    auto* gotham = visible_text_under(families, QStringLiteral("Gotham"));
    REQUIRE(missing_heading != nullptr);
    REQUIRE(library_heading != nullptr);
    REQUIRE(gotham != nullptr);
    const auto top = [&](const QQuickItem* item) { return item->mapToItem(families, QPointF()).y(); };
    CHECK(top(missing_heading) < top(gotham));
    CHECK(top(gotham) < top(library_heading));
    CHECK(top(library_heading) >= top(gotham) + gotham->height());

    // With none missing there is nothing to tell apart, so no heading at all,
    // and so again once one is missing once more.
    const auto headings = [&] {
        QStringList out;
        for (const auto* text : visible_items_of(families, "QQuickText")) {
            const auto said = text->property("text").toString();
            if (said == QStringLiteral("Missing") || said == QStringLiteral("In this library")) {
                out.push_back(said);
            }
        }
        out.sort();  // in the view's child order, not on screen's
        return out;
    };
    db->exec("DELETE FROM freeform_items WHERE name = 'one'");
    facade.fonts()->refresh();
    QTest::qWait(50);
    CHECK(headings().isEmpty());
    db->exec(R"(INSERT INTO freeform_items (name, document)
                VALUES ('one', '{"objects":[{"font_family":"Gotham"}]}'))");
    facade.fonts()->refresh();
    QTest::qWait(50);
    CHECK(headings() == QStringList{QStringLiteral("In this library"), QStringLiteral("Missing")});
    CHECK(visible_text_under(sheet, QStringLiteral("Gotham")) != nullptr);
    CHECK(visible_text_under(sheet, QStringLiteral("Showing in Liberation Sans")) != nullptr);
    CHECK(visible_text_under(sheet, QStringLiteral("No font for Gotham")) != nullptr);
    // And says where it is used.
    auto* uses = sheet->findChild<QQuickItem*>(QStringLiteral("fontUses"));
    REQUIRE(uses != nullptr);
    CHECK(uses->isVisible());
    CHECK(visible_text_under(uses, QStringLiteral("Used in 1 place")) != nullptr);
    CHECK(visible_text_under(uses, QStringLiteral("one")) != nullptr);
    // Its glyph says what it is: no line under it saying so again.
    CHECK(visible_text_under(uses, QStringLiteral("Freeform")) == nullptr);
    // A search that leaves it showing does not read the library again.
    QObject* fonts_state = qml_object(dialog, "SettingsFontsState");
    REQUIRE(fonts_state != nullptr);
    QSignalSpy reread(fonts_state, SIGNAL(shownUsesChanged()));
    fonts_state->setProperty("searchText", QStringLiteral("Got"));
    QTest::qWait(20);
    fonts_state->setProperty("searchText", QString());
    QTest::qWait(20);
    CHECK(reread.isEmpty());

    // A double click opens it, over Settings; closing it goes back there, and
    // the page reads again. (The editor opens what it can load: a new item's
    // empty document, not this test's stand-in for one naming the font.)
    db->exec("UPDATE freeform_items SET document = '{}' WHERE name = 'one'");
    QQuickItem* one = visible_text_under(uses, QStringLiteral("one"));
    REQUIRE(one != nullptr);
    QTest::mouseDClick(ui.window(), Qt::LeftButton, Qt::NoModifier, centre_of(one));
    QTest::qWait(50);
    REQUIRE(facade.slideEdit()->active());
    CHECK(dialog->property("open").toBool());
    db->exec(R"(UPDATE freeform_items SET document = '{"objects":[{"font_family":"Gotham"}]}'
                WHERE name = 'one')");
    facade.slideEdit()->cancel();
    QTest::qWait(50);
    CHECK_FALSE(facade.slideEdit()->active());
    CHECK(dialog->property("open").toBool());
    CHECK_FALSE(reread.isEmpty());

    // What ships is the library's like anything else.
    CHECK(visible_text_under(sheet, QStringLiteral("Comes with SeventhSlide")) == nullptr);
    QObject* state = qml_object(dialog, "SettingsFontsState");
    REQUIRE(state != nullptr);
    call(state, "selectFamily", QStringLiteral("Carlito"));
    QTest::qWait(50);
    CHECK(visible_text_under(sheet, QStringLiteral("Bold Italic")) != nullptr);
    CHECK(visible_text_under(sheet, QStringLiteral("Rejoice always.")) != nullptr);
    CHECK(visible_text_under(sheet, QStringLiteral("Add to Library")) == nullptr);
    // A family the library has says nothing of where it is used.
    CHECK_FALSE(uses->isVisible());

    // What is selected can be removed, and the page says what follows.
    const auto carlito = [&] { return seventhslide::db::FontRepository(*db).list_family("Carlito").size(); };
    CHECK(carlito() == 4);
    CHECK(state->property("canRemove").toBool());
    QSignalSpy remove(state, SIGNAL(removeAsked(QString)));
    auto* trash_button = sheet->findChild<QQuickItem*>(QStringLiteral("removeFonts"));
    REQUIRE(trash_button != nullptr);
    const auto trash = [&] {
        click(ui.window(), centre_of(trash_button));
        QTest::qWait(20);
        REQUIRE(!remove.isEmpty());
        return remove.takeFirst().front().toString();
    };
    const auto say_yes = [&] {
        auto* yes = visible_text_under(ui.window()->contentItem(), QStringLiteral("Remove"));
        REQUIRE(yes != nullptr);
        click(ui.window(), centre_of(yes));
        QTest::qWait(50);
    };
    const auto press_style = [&](const QString& style, Qt::KeyboardModifiers modifiers) {
        auto* label = visible_text_under(sheet, style);
        REQUIRE(label != nullptr);
        QTest::mouseClick(ui.window(), Qt::LeftButton, modifiers, centre_of(label));
        QTest::qWait(20);
    };
    const auto picked = [&] { return state->property("selectedFaces").toList().size(); };

    // One style, picked by a click on its row. Its file is on its way here
    // (its row came first), so the computers that have it lose it too.
    std::string bold_italic;
    for (const auto& face : seventhslide::db::FontRepository(*db).list_family("Carlito")) {
        if (face.style == "Bold Italic") {
            bold_italic = face.filename;
        }
    }
    REQUIRE_FALSE(bold_italic.empty());
    std::filesystem::remove(facade.media_root() / "fonts" / bold_italic);
    using seventhslide::app::FileArrival;
    facade.media()->set_arrivals({{"fonts/" + bold_italic, {FileArrival::State::Downloading, 1, 2}}});
    press_style(QStringLiteral("Bold Italic"), Qt::NoModifier);
    CHECK(picked() == 1);
    auto asked = trash();
    CHECK(asked.contains(QStringLiteral("Carlito Bold Italic")));
    CHECK(asked.contains(QStringLiteral("that style")));
    CHECK(asked.contains(QStringLiteral("lose them too")));
    say_yes();
    facade.media()->set_arrivals({});
    CHECK(carlito() == 3);
    CHECK(picked() == 0);

    // Two with Ctrl, and a click on the family takes them back.
    press_style(QStringLiteral("Regular"), Qt::NoModifier);
    press_style(QStringLiteral("Italic"), Qt::ControlModifier);
    CHECK(picked() == 2);
    call(state, "selectFamily", QStringLiteral("Carlito"));
    CHECK(picked() == 0);
    press_style(QStringLiteral("Regular"), Qt::NoModifier);
    press_style(QStringLiteral("Italic"), Qt::ControlModifier);
    asked = trash();
    CHECK(asked.contains(QStringLiteral("those styles")));
    CHECK_FALSE(asked.contains(QStringLiteral("lose them too")));
    say_yes();
    CHECK(carlito() == 1);

    // All that is left goes as the family; so does the family with none picked.
    press_style(QStringLiteral("Bold"), Qt::NoModifier);
    asked = trash();
    CHECK(asked.contains(QStringLiteral("Nothing in this library names it now.")));
    say_yes();
    CHECK(carlito() == 0);
    CHECK_FALSE(state->property("canRemove").toBool());
    const auto lora = [&] { return seventhslide::db::FontRepository(*db).list_family("Lora").size(); };
    CHECK(lora() == 4);
    call(state, "selectFamily", QStringLiteral("Lora"));
    QTest::qWait(50);
    asked = trash();
    CHECK(asked.contains(QStringLiteral("Lora")));
    say_yes();
    CHECK(lora() == 0);

    // Closed, nothing is held in Qt, and the bar still says.
    REQUIRE(QMetaObject::invokeMethod(dialog, "close"));
    QTest::qWait(50);
    CHECK(visible_text_under(bar, QStringLiteral("1 font missing")) != nullptr);
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Settings confirms name the library or profile they remove", "[ui][settings]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.init_libraries(dir.path / "data"));
    OperatorUi ui(facade);
    QObject* dialog = ui.find_component("AppSettingsDialog");
    REQUIRE(dialog != nullptr);
    QObject* state = qml_object(dialog, "SettingsLibraryState");
    REQUIRE(state != nullptr);

    // The library in use is not offered for removal.
    QSignalSpy remove(state, SIGNAL(removeLibraryAsked(QString)));
    call(state, "chooseLibrary", facade.libraries()->activeLibraryId());
    REQUIRE(QMetaObject::invokeMethod(state, "beginRemoveLibrary"));
    REQUIRE(remove.isEmpty());

    REQUIRE(facade.libraries()->createLibrary(QStringLiteral("Kids")).isEmpty());
    call(state, "chooseLibrary", QStringLiteral("kids"));
    REQUIRE(QMetaObject::invokeMethod(state, "beginRemoveLibrary"));
    REQUIRE(remove.count() == 1);
    CHECK(remove.front().front().toString()
          == QStringLiteral("Remove “Kids” from the list? Its files stay on disk."));

    // Nor is a library's last profile offered for deleting.
    QSignalSpy del(state, SIGNAL(deleteProfileAsked(QString)));
    call(state, "chooseProfile", facade.profiles()->activeProfileId());
    REQUIRE(QMetaObject::invokeMethod(state, "beginDeleteProfile"));
    REQUIRE(del.isEmpty());

    REQUIRE(facade.profiles()->createProfile(QStringLiteral("Youth Vespers")).isEmpty());
    const QVariantMap profile = facade.profiles()->styleProfiles().value(1).toMap();
    REQUIRE(profile.value(QStringLiteral("name")).toString() == QStringLiteral("Youth Vespers"));
    call(state, "chooseProfile", profile.value(QStringLiteral("id")));
    REQUIRE(QMetaObject::invokeMethod(state, "beginDeleteProfile"));
    REQUIRE(del.count() == 1);
    CHECK(del.front().front().toString()
          == QStringLiteral("Permanently delete the profile “Youth Vespers”?"));
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Settings deletes a library's folder only once its name is typed", "[ui][settings]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    const auto data = dir.path / "data";
    REQUIRE(facade.init_libraries(data));
    OperatorUi ui(facade);
    QObject* dialog = ui.find_component("AppSettingsDialog");
    REQUIRE(dialog != nullptr);
    QObject* state = qml_object(dialog, "SettingsLibraryState");
    REQUIRE(state != nullptr);

    REQUIRE(facade.libraries()->createLibrary(QStringLiteral("Kids")).isEmpty());
    const auto kids = data / "libraries" / "kids";
    call(state, "chooseLibrary", QStringLiteral("kids"));
    REQUIRE(QMetaObject::invokeMethod(state, "beginRemoveLibrary"));
    CHECK(state->property("canDeleteFolder").toBool());
    // Removing asks for nothing typed, and is what it starts as.
    CHECK_FALSE(state->property("deleteFolder").toBool());
    CHECK(state->property("canConfirmRemove").toBool());

    REQUIRE(state->setProperty("deleteFolder", true));
    CHECK_FALSE(state->property("canConfirmRemove").toBool());
    REQUIRE(state->setProperty("deleteTyped", QStringLiteral("kids")));
    CHECK_FALSE(state->property("canConfirmRemove").toBool());
    REQUIRE(state->setProperty("deleteTyped", QStringLiteral("Kids")));
    CHECK(state->property("canConfirmRemove").toBool());
    CHECK_THAT(state->property("deleteFolderMessage").toString().toStdString(),
               Catch::Matchers::ContainsSubstring(kids.string()));

    // Asked again, it starts over: the choice is made each time.
    REQUIRE(QMetaObject::invokeMethod(state, "beginRemoveLibrary"));
    CHECK_FALSE(state->property("deleteFolder").toBool());
    CHECK(state->property("deleteTyped").toString().isEmpty());

    // Without the choice, the files stay.
    call(state, "performRemoveLibrary", QStringLiteral("kids"));
    CHECK(std::filesystem::is_directory(kids));
    CHECK(state->property("selectedLibraryId").toString() == facade.libraries()->activeLibraryId());

    // With it, they go; and the library in use is picked.
    REQUIRE(facade.libraries()->createLibrary(QStringLiteral("Youth")).isEmpty());
    call(state, "chooseLibrary", QStringLiteral("youth"));
    REQUIRE(QMetaObject::invokeMethod(state, "beginRemoveLibrary"));
    REQUIRE(state->setProperty("deleteFolder", true));
    REQUIRE(state->setProperty("deleteTyped", QStringLiteral("Youth")));
    call(state, "performRemoveLibrary", QStringLiteral("youth"));
    CHECK_FALSE(std::filesystem::exists(data / "libraries" / "youth"));
    CHECK(state->property("selectedLibraryId").toString() == facade.libraries()->activeLibraryId());
    CHECK_FALSE(state->property("deleteFolder").toBool());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Settings asks before adding a library from a cloud folder", "[ui][settings]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.init_libraries(dir.path / "data"));
    OperatorUi ui(facade);
    QObject* dialog = ui.find_component("AppSettingsDialog");
    REQUIRE(dialog != nullptr);
    QObject* state = qml_object(dialog, "SettingsLibraryState");
    REQUIRE(state != nullptr);
    QSignalSpy asked(state, SIGNAL(cloudFolderAsked(QString)));

    // A folder nothing syncs is added straight away.
    const auto local = dir.path / "Local Library";
    std::filesystem::create_directories(local);
    call(state, "attachLibrary", QUrl::fromLocalFile(QString::fromStdString(local.string())).toString());
    CHECK(asked.isEmpty());
    REQUIRE(facade.libraries()->libraries().size() == 2);

    // One in Dropbox waits for the operator's word, naming the service.
    const auto synced = dir.path / "Dropbox" / "Church";
    std::filesystem::create_directories(synced);
    const QString url = QUrl::fromLocalFile(QString::fromStdString(synced.string())).toString();
    call(state, "attachLibrary", url);
    REQUIRE(asked.count() == 1);
    CHECK(asked.front().front().toString().startsWith(
        QStringLiteral("This folder is synced by Dropbox. Dropbox can damage")));
    CHECK(facade.libraries()->libraries().size() == 2);
    CHECK(state->property("pendingFolder").toString() == url);

    call(state, "performAttachLibrary", url);
    CHECK(state->property("pendingFolder").toString().isEmpty());
    const auto rows = facade.libraries()->libraries();
    REQUIRE(rows.size() == 3);
    CHECK(rows.at(1).toMap().value(QStringLiteral("cloudService")).toString().isEmpty());
    CHECK(rows.at(2).toMap().value(QStringLiteral("cloudService")).toString()
          == QStringLiteral("Dropbox"));
    CHECK(state->property("selectedLibraryId").toString()
          == rows.at(2).toMap().value(QStringLiteral("id")).toString());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Next and previous step the live item and stop at its ends", "[ui][controller]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    seventhslide::model::Song song;
    song.title = "Two Verses";
    song.lyrics = "[V1]\nOne\n\n[V2]\nTwo";
    song.verse_order = "v1 v2";
    const auto id = facade.song_repo()->insert(song);
    facade.refresh_library();

    OperatorUi ui(facade);
    QObject* controller = ui.find_component("ControllerColumn");
    REQUIRE(controller != nullptr);
    QSignalSpy interacted(controller, SIGNAL(interacted()));

    // Nothing live: no step.
    CHECK_FALSE(call(controller, "navigateNext").toBool());

    REQUIRE(facade.live()->playSong(id));
    CHECK(call(controller, "navigateNext").toBool());
    CHECK_FALSE(call(controller, "navigateNext").toBool());
    CHECK(call(controller, "navigatePrev").toBool());
    CHECK_FALSE(call(controller, "navigatePrev").toBool());
    // Every press counts as the operator's, refused or not.
    CHECK(interacted.count() == 5);
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("The controller shows audio as its transport alone", "[ui][controller][audio]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    (void)build(facade, dir.path);  // a service of songs, Alpha first
    OperatorUi ui(facade);
    QObject* well = seventhslide::test::program_well(ui.window());
    REQUIRE(well != nullptr);
    auto* content = ui.window()->contentItem();

    // A song: the line list and the slide transport.
    REQUIRE(facade.live()->selectServiceItem(0));
    QTest::qWait(20);
    CHECK_FALSE(well->property("mediaControls").toBool());
    CHECK(visible_text_under(content, QStringLiteral("Next")) != nullptr);
    CHECK(visible_text_under(content, QStringLiteral("Line")) != nullptr);

    // Audio replaces it on the same stack: the seek strip and the
    // Play/Pause - Blank - Freeze - Restart transport, and the mark, not words,
    // in the centre.
    REQUIRE(facade.live()->playAudio(QStringLiteral("hymn.mp3")));
    QTest::qWait(20);
    CHECK(facade.program()->mediaTransport());
    CHECK_FALSE(facade.program()->videoMode());
    CHECK(well->property("mediaControls").toBool());
    CHECK(visible_text_under(content, QStringLiteral("Restart")) != nullptr);
    CHECK(visible_text_under(content, QStringLiteral("Next")) == nullptr);
    CHECK(visible_text_under(content, QStringLiteral("Line")) == nullptr);
    CHECK(visible_text_under(content, QStringLiteral("No live items.")) == nullptr);
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("The seek bar follows audio, and a drag seeks to where it lets go",
          "[ui][controller][audio]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    const auto audio = facade.media_root() / "audio";
    std::filesystem::create_directories(audio);
    seventhslide::test::write_tone_wav(audio / "bed.wav", 4.0);
    facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Audio, "bed.wav", "bed.wav");
    facade.refresh_library();
    OperatorUi ui(facade);
    QQuickItem* bar = nullptr;
    for (auto* item : seventhslide::test::program_well(ui.window())->findChildren<QQuickItem*>()) {
        if (seventhslide::test::is_qml_type(item, "ControllerSeekBar")) {
            bar = item;
        }
    }
    REQUIRE(bar != nullptr);
    QQuickItem* slider = nullptr;
    for (auto* item : bar->findChildren<QQuickItem*>()) {
        if (item->inherits("QQuickSlider")) {
            slider = item;
        }
    }
    REQUIRE(slider != nullptr);
    const auto value = [&] { return slider->property("value").toDouble(); };

    // Paused at once, before the file has opened: its length still arrives.
    REQUIRE(facade.live()->playAudio(QStringLiteral("bed.wav")));
    REQUIRE(facade.program()->mediaPause());
    REQUIRE(QTest::qWaitFor([&] { return slider->property("to").toDouble() > 3.5; }, 5000));
    REQUIRE(bar->isVisible());
    REQUIRE(slider->isEnabled());
    // The bar has only just appeared, and a short file can open before the
    // next frame: until that frame lays it out it is its implicit size, at the
    // top of the window.
    REQUIRE(QTest::qWaitFor([&] { return slider->width() > slider->implicitWidth(); }, 5000));

    // Dragged by the handle three quarters along: the audio is where it let
    // go, and the bar stays there.
    const auto at = [&](double fraction) {
        const double left = slider->property("leftPadding").toDouble();
        const double travel = slider->property("availableWidth").toDouble() - 16;
        return slider->mapToScene(QPointF(left + 8 + fraction * travel, slider->height() / 2))
            .toPoint();
    };
    seventhslide::test::drag(ui.window(), at(0.0), at(0.75));
    const double landed = facade.program()->mediaPositionSec();
    CHECK(landed == Catch::Approx(3.0).margin(0.3));
    QTest::qWait(100);
    CHECK(value() == Catch::Approx(landed).margin(0.05));
    CHECK(facade.program()->mediaPositionSec() == Catch::Approx(landed).margin(0.01));

    // A slow drag back seeks in steps, not on every move: paused, each seek
    // announces the clock at most twice (at once, and the one tick after),
    // so 30 moves seeking on each would announce it 30 times. The steps are
    // 100 ms apart, so a slower machine's longer drag has more of them.
    int announced = 0;
    const auto counting = QObject::connect(
        facade.program(), &seventhslide::app::SideView::mediaPositionChanged,
        [&] { ++announced; });
    const QPoint from = at(landed / 4.0);
    const QPoint to = at(0.25);
    QElapsedTimer dragging;
    dragging.start();
    QTest::mousePress(ui.window(), Qt::LeftButton, {}, from);
    constexpr int moves = 30;
    for (int i = 1; i <= moves; ++i) {
        QTest::mouseMove(ui.window(), from + (to - from) * i / moves);
        QTest::qWait(10);
    }
    QTest::mouseRelease(ui.window(), Qt::LeftButton, {}, to);
    const qint64 steps = dragging.elapsed() / 100;
    QTest::qWait(100);
    QObject::disconnect(counting);
    INFO("announced " << announced << " over " << steps << " steps");
    CHECK(announced >= 1);
    // The steps, one at the start and one on letting go.
    CHECK(announced <= 2 * (steps + 2));
    CHECK(facade.program()->mediaPositionSec() == Catch::Approx(1.0).margin(0.3));
    const double back = facade.program()->mediaPositionSec();

    // Played, the bar follows the clock.
    REQUIRE(facade.program()->mediaPlay());
    REQUIRE(QTest::qWaitFor([&] { return value() > back + 0.2; }, 5000));
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Stack settings list every content type a stack can default for", "[ui][settings][audio]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    OperatorUi ui(facade);
    QObject* dialog = ui.find_component("AppSettingsDialog");
    REQUIRE(dialog != nullptr);
    QObject* stacks = qml_object(dialog, "SettingsStacksState");
    REQUIRE(stacks != nullptr);

    // The rows the panel draws are exactly the types core keeps a default for.
    QStringList shown;
    for (const auto& row : stacks->property("stackTypeItems").toList()) {
        shown.push_back(row.toMap().value(QStringLiteral("key")).toString());
    }
    QStringList kept = facade.stacks()->stackTypeDefaults().keys();
    shown.sort();
    kept.sort();
    CHECK(shown == kept);
    CHECK(shown.contains(QStringLiteral("audio")));
}

TEST_CASE("Services: sorted by a column, narrowed by tag, and Open or a double-click loads one",
          "[ui][services]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    const auto lib = seventhslide::test::fill_sample_library(facade);
    OperatorUi ui(facade);
    QObject* dialog = ui.find_component("ServicePickerDialog");
    REQUIRE(dialog != nullptr);
    QObject* state = qml_object(dialog, "ServicesState");
    REQUIRE(state != nullptr);
    call(dialog, "show");
    QTest::qWait(30);
    auto* sheet = qobject_cast<QQuickItem*>(dialog);

    // The open service is selected; both show, under their dates.
    CHECK(state->property("selectedId").toLongLong() == lib.service);
    CHECK(visible_text_under(sheet, QStringLiteral("Good Friday")) != nullptr);
    CHECK(visible_text_under(sheet, QStringLiteral("Sunday Service")) != nullptr);

    // A column's name sorts by it, and again turns it round: newest first,
    // then by name A to Z and Z to A.
    const auto first = [&] {
        return state->property("browser").value<QObject*>()->property("shownIds")
            .toList().front().toLongLong();
    };
    CHECK(first() == lib.other_service);
    QQuickItem* nameHead = visible_text_under(sheet, QStringLiteral("Name"));
    REQUIRE(nameHead != nullptr);
    click(ui.window(), centre_of(nameHead));
    QTest::qWait(30);
    CHECK(state->property("sortKey").toString() == QStringLiteral("name"));
    CHECK(state->property("sortAscending").toBool());
    CHECK(first() == lib.other_service);
    click(ui.window(), centre_of(nameHead));
    QTest::qWait(30);
    CHECK_FALSE(state->property("sortAscending").toBool());
    CHECK(first() == lib.service);
    QQuickItem* dateHead = visible_text_under(sheet, QStringLiteral("Date"));
    REQUIRE(dateHead != nullptr);
    click(ui.window(), centre_of(dateHead));
    QTest::qWait(30);
    CHECK(state->property("sortKey").toString() == QStringLiteral("date"));
    CHECK_FALSE(state->property("sortAscending").toBool());
    CHECK(first() == lib.other_service);

    // A service's row shows its first slide; one with nothing to show, none.
    auto* thumbs = facade.serviceThumbs();
    CHECK(thumbs->thumbUrl(lib.service).startsWith(QStringLiteral("image://servicethumb/")));
    CHECK(thumbs->thumbUrl(lib.other_service).isEmpty());
    // And under its name, how many items it holds.
    const int items = facade.library()->serviceItemCount(lib.service);
    CHECK(items > 1);
    CHECK(visible_text_under(sheet, QStringLiteral("%1 items").arg(items)) != nullptr);

    // The search field's tag button opens the tags, the caret in their
    // search; it narrows them, keeping the keys as they narrow.
    auto* field = qobject_cast<QQuickItem*>(qml_object(dialog, "SearchFilterField"));
    REQUIRE(field != nullptr);
    auto* tagButton = field->property("filterButton").value<QQuickItem*>();
    REQUIRE(tagButton != nullptr);
    QObject* menu = dialog->findChild<QObject*>(QStringLiteral("serviceTagMenu"));
    REQUIRE(menu != nullptr);
    auto* content = ui.window()->contentItem();
    click(ui.window(), centre_of(tagButton));
    QTest::qWait(30);
    REQUIRE(menu->property("visible").toBool());
    QObject* tagSearch = menu->findChild<QObject*>(QStringLiteral("serviceTagSearch"));
    REQUIRE(tagSearch != nullptr);
    CHECK(tagSearch->property("inputFocused").toBool());
    QTest::keyClick(ui.window(), Qt::Key_Z);
    QTest::keyClick(ui.window(), Qt::Key_Z);
    QTest::qWait(30);
    CHECK(dialog->property("tagSearch").toString() == QStringLiteral("zz"));
    CHECK(visible_text_under(content, QStringLiteral("Easter")) == nullptr);
    CHECK(visible_text_under(content, QStringLiteral("No tags match")) != nullptr);
    // Escape closes the menu, and its search goes with it; the window stays.
    QTest::keyClick(ui.window(), Qt::Key_Escape);
    QTest::qWait(30);
    CHECK_FALSE(menu->property("visible").toBool());
    CHECK(dialog->property("tagSearch").toString().isEmpty());
    CHECK(dialog->property("open").toBool());

    // A tag picked narrows the page and leaves the list for a chip under
    // the search, the menu staying open for another; the service picked
    // hidden, the first shown is picked in its place.
    auto* chips = qobject_cast<QQuickItem*>(menu->findChild<QObject*>(QStringLiteral("serviceTagChips")));
    REQUIRE(chips != nullptr);
    auto* tagList = qobject_cast<QQuickItem*>(menu->findChild<QObject*>(QStringLiteral("serviceTagList")));
    REQUIRE(tagList != nullptr);
    click(ui.window(), centre_of(tagButton));
    QTest::qWait(30);
    REQUIRE(menu->property("visible").toBool());
    CHECK_FALSE(chips->isVisible());
    QQuickItem* easter = visible_text_under(tagList, QStringLiteral("Easter"));
    REQUIRE(easter != nullptr);
    click(ui.window(), centre_of(easter));
    QTest::qWait(30);
    CHECK(state->property("filterTags").toStringList() == QStringList{QStringLiteral("Easter")});
    CHECK(menu->property("visible").toBool());
    CHECK(chips->isVisible());
    CHECK(visible_text_under(chips, QStringLiteral("Easter")) != nullptr);
    CHECK(visible_text_under(tagList, QStringLiteral("Easter")) == nullptr);
    CHECK(visible_text_under(content, QStringLiteral("Every tag is picked")) != nullptr);
    // The chips are under the search, and over the list.
    auto* searchItem = qobject_cast<QQuickItem*>(tagSearch);
    CHECK(chips->mapToScene(QPointF(0, 0)).y() > searchItem->mapToScene(QPointF(0, 0)).y());
    // Only the menu shows them: the window has no chips of its own.
    CHECK(visible_text_under(sheet, QStringLiteral("Clear")) == nullptr);
    CHECK(visible_text_under(sheet, QStringLiteral("Sunday Service")) == nullptr);
    CHECK(state->property("selectedId").toLongLong() == lib.other_service);
    CHECK(state->property("canOpen").toBool());

    // A chip's × drops its tag, back into the list, the menu staying open.
    QQuickItem* chipText = visible_text_under(chips, QStringLiteral("Easter"));
    REQUIRE(chipText != nullptr);
    // The chip, by its own ``text``, or the label in it.
    QQuickItem* chip = chipText->property("removable").isValid() ? chipText : chipText->parentItem();
    click(ui.window(), chip->mapToScene(QPointF(chip->width() - 9, chip->height() / 2)).toPoint());
    QTest::qWait(30);
    CHECK(state->property("filterTags").toStringList().isEmpty());
    CHECK(menu->property("visible").toBool());
    CHECK_FALSE(chips->isVisible());
    CHECK(visible_text_under(tagList, QStringLiteral("Easter")) != nullptr);
    CHECK(visible_text_under(sheet, QStringLiteral("Sunday Service")) != nullptr);

    // From the search, Enter picks the first tag it leaves and clears it for
    // the next; Down steps onto the list, Enter picking that one.
    for (const auto key : {Qt::Key_E, Qt::Key_A, Qt::Key_S}) {
        QTest::keyClick(ui.window(), key);
    }
    QTest::qWait(30);
    CHECK(dialog->property("tagCursor").toInt() == 0);
    QTest::keyClick(ui.window(), Qt::Key_Return);
    QTest::qWait(30);
    CHECK(state->property("filterTags").toStringList() == QStringList{QStringLiteral("Easter")});
    CHECK(dialog->property("tagSearch").toString().isEmpty());
    CHECK(menu->property("visible").toBool());
    CHECK(dialog->property("open").toBool());
    // Nothing left to step onto; Clear drops every chip.
    QTest::keyClick(ui.window(), Qt::Key_Down);
    CHECK(dialog->property("tagCursor").toInt() == -1);
    QQuickItem* clear = visible_text_under(chips, QStringLiteral("Clear"));
    REQUIRE(clear != nullptr);
    click(ui.window(), centre_of(clear));
    QTest::qWait(30);
    CHECK(state->property("filterTags").toStringList().isEmpty());
    CHECK(menu->property("visible").toBool());
    QTest::keyClick(ui.window(), Qt::Key_Down);
    CHECK(dialog->property("tagCursor").toInt() == 0);
    QTest::keyClick(ui.window(), Qt::Key_Return);
    QTest::qWait(30);
    CHECK(state->property("filterTags").toStringList() == QStringList{QStringLiteral("Easter")});
    CHECK(dialog->property("tagCursor").toInt() == -1);
    call(state, "clearTags");
    QTest::keyClick(ui.window(), Qt::Key_Escape);
    QTest::qWait(30);
    CHECK_FALSE(menu->property("visible").toBool());
    // A tag no service has is not narrowed to; one in use takes its case.
    call(state, "toggleTag", QStringLiteral("Nowhere"));
    CHECK(state->property("filterTags").toStringList().isEmpty());
    call(state, "toggleTag", QStringLiteral("easter"));
    CHECK(state->property("filterTags").toStringList() == QStringList{QStringLiteral("Easter")});
    call(state, "clearTags");
    call(state, "toggleTag", QStringLiteral("Easter"));
    QTest::qWait(30);

    // A double-click loads one and closes the window.
    QQuickItem* friday = visible_text_under(sheet, QStringLiteral("Good Friday"));
    REQUIRE(friday != nullptr);
    QTest::mouseDClick(ui.window(), Qt::LeftButton, {}, centre_of(friday));
    QTest::qWait(30);
    CHECK(facade.openServiceId() == lib.other_service);
    CHECK_FALSE(dialog->property("open").toBool());

    // Enter loads the one selected.
    call(dialog, "show");
    call(state, "select", QVariant::fromValue<qint64>(lib.service));
    QTest::qWait(30);
    QTest::keyClick(ui.window(), Qt::Key_Return);
    CHECK(facade.openServiceId() == lib.service);
    CHECK_FALSE(dialog->property("open").toBool());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Service Settings: General keeps a service's title, date, time and tags on Save",
          "[ui][services]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    const auto lib = seventhslide::test::fill_sample_library(facade);
    OperatorUi ui(facade);
    QObject* settings = ui.find_component("ServiceSettingsDialog");
    REQUIRE(settings != nullptr);
    QObject* general = qml_object(settings, "ServiceGeneralState");
    REQUIRE(general != nullptr);
    const auto id = QVariant::fromValue<qint64>(lib.other_service);
    const auto stored = [&] { return *facade.service_repo()->get_service(lib.other_service); };

    // Times as people type them.
    CHECK(call(general, "parseTime", QStringLiteral("7:30 pm")).toString() == QStringLiteral("19:30"));
    CHECK(call(general, "parseTime", QStringLiteral("930")).toString() == QStringLiteral("09:30"));
    CHECK(call(general, "parseTime", QStringLiteral("12am")).toString() == QStringLiteral("00:00"));
    CHECK(call(general, "parseTime", QStringLiteral("19:05")).toString() == QStringLiteral("19:05"));
    CHECK(call(general, "parseTime", QStringLiteral("19:05:00")).toString() == QStringLiteral("19:05"));
    CHECK(call(general, "parseTime", QStringLiteral("")).toString().isEmpty());
    CHECK(call(general, "parseTime", QStringLiteral("25:00")).isNull());
    CHECK(call(general, "parseTime", QStringLiteral("13pm")).isNull());

    // Opened on a service that is not the open one, on General.
    call(settings, "openFor", id, false);
    QTest::qWait(30);
    CHECK(settings->property("open").toBool());
    CHECK(settings->property("tab").toString() == QStringLiteral("general"));
    CHECK(general->property("draftName").toString() == QStringLiteral("Good Friday"));
    CHECK(general->property("draftDate").toString() == QStringLiteral("2026-04-03"));

    // Nothing is kept until Save.
    REQUIRE(general->setProperty("draftName", QStringLiteral("Good Friday Vespers")));
    REQUIRE(general->setProperty("draftDate", QStringLiteral("2026-04-02")));
    REQUIRE(general->setProperty("draftTimeText", QStringLiteral("6:15 PM")));
    // A tag typed into the field (found through the items, not as QObject
    // children).
    QObject* tags = nullptr;
    for (QQuickItem* item : seventhslide::test::visible_items_of(
             qobject_cast<QQuickItem*>(settings), "QQuickItem")) {
        if (QByteArray(item->metaObject()->className()).startsWith("TagField_QMLTYPE_"))
            tags = item;
    }
    REQUIRE(tags != nullptr);
    REQUIRE(QMetaObject::invokeMethod(tags, "focusInput"));
    for (const char c : std::string("Holy Week")) {
        QTest::keyClick(ui.window(), c);
    }
    QTest::keyClick(ui.window(), Qt::Key_Return);
    CHECK(stored().name == "Good Friday");
    call(settings, "save");
    CHECK_FALSE(settings->property("open").toBool());
    CHECK(stored().name == "Good Friday Vespers");
    CHECK(stored().service_at == "2026-04-02T18:15:00");
    CHECK(stored().tags == std::vector<std::string>{"Easter", "Holy Week"});

    // A time that is not one is refused, and nothing is kept.
    call(settings, "openFor", id, false);
    REQUIRE(general->setProperty("draftName", QStringLiteral("Renamed")));
    REQUIRE(general->setProperty("draftTimeText", QStringLiteral("soon")));
    call(settings, "save");
    CHECK(settings->property("open").toBool());
    CHECK(settings->property("errorText").toString().contains(QStringLiteral("not a time")));
    CHECK(stored().name == "Good Friday Vespers");

    // Cancel drops the drafts; no date clears when it is held.
    call(settings, "close");
    call(settings, "openFor", id, false);
    CHECK(general->property("draftName").toString() == QStringLiteral("Good Friday Vespers"));
    call(general, "removeTag", 0);
    REQUIRE(general->setProperty("draftDate", QString()));
    call(settings, "save");
    CHECK(stored().service_at.empty());
    CHECK(stored().tags == std::vector<std::string>{"Holy Week"});
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Service Settings: General keeps the time it opened with, however the locale writes it",
          "[ui][services]")
{
    // The field opens on the locale's own way of writing the time, which the
    // field's reading of what is typed need not take: Korean puts the
    // afternoon before the hour ("오후 7:00"), the C locale adds seconds.
    const auto language = GENERATE(QLocale::Korean, QLocale::C);
    CAPTURE(QLocale::languageToString(language).toStdString());
    struct DefaultLocale {
        QLocale was;
        ~DefaultLocale() { QLocale::setDefault(was); }
    } const restore{QLocale()};
    QLocale::setDefault(QLocale(language));

    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    const auto lib = seventhslide::test::fill_sample_library(facade);
    OperatorUi ui(facade);
    QObject* settings = ui.find_component("ServiceSettingsDialog");
    REQUIRE(settings != nullptr);
    QObject* general = qml_object(settings, "ServiceGeneralState");
    REQUIRE(general != nullptr);
    const auto stored = [&] { return *facade.service_repo()->get_service(lib.other_service); };

    call(settings, "openFor", QVariant::fromValue<qint64>(lib.other_service), false);
    QTest::qWait(30);
    CAPTURE(general->property("draftTimeText").toString().toStdString());
    CHECK(general->property("draftTimeText").toString()
          == QLocale().toString(QTime(19, 0), QLocale::ShortFormat));
    // Only the title changes; the time, as it opened, is the one stored.
    REQUIRE(general->setProperty("draftName", QStringLiteral("Good Friday Vespers")));
    call(settings, "save");
    CHECK(settings->property("errorText").toString().isEmpty());
    CHECK_FALSE(settings->property("open").toBool());
    CHECK(stored().name == "Good Friday Vespers");
    CHECK(stored().service_at == "2026-04-03T19:00:00");
}

namespace {

/// The visible item under ``root`` named ``name``; null when there is none.
QQuickItem* visible_named(QQuickItem* root, const QString& name)
{
    for (QQuickItem* item : seventhslide::test::visible_items_of(root, "QQuickItem")) {
        if (item->objectName() == name) {
            return item;
        }
    }
    return nullptr;
}

/// Replace what ``field`` (a DialogField) holds with ``text``, typed.
void type_into(QQuickWindow* window, QQuickItem* field, const std::string& text)
{
    REQUIRE(field != nullptr);
    REQUIRE(QMetaObject::invokeMethod(field, "focusInput"));
    QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(window, Qt::Key_Backspace);
    for (const char c : text) {
        QTest::keyClick(window, c);
    }
    QTest::qWait(10);
}

}  // namespace

TEST_CASE("Service Settings: General sets the service's audio fades, each empty for the outputs'",
          "[ui][services][audio]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    const auto lib = seventhslide::test::fill_sample_library(facade);
    OperatorUi ui(facade);
    QObject* settings = ui.find_component("ServiceSettingsDialog");
    REQUIRE(settings != nullptr);
    auto* root = qobject_cast<QQuickItem*>(settings);
    const auto stored = [&] { return facade.service_repo()->get_service(lib.service)->audio_fades; };
    const auto main_fades = [&] {
        return facade.session().audio_fades_on("main").at(facade.session().outputs().front().id);
    };

    // The open service, none set: each field empty, the outputs' showing.
    call(settings, "openFor", QVariant::fromValue<qint64>(lib.service), false);
    QTest::qWait(30);
    QQuickItem* fade_out = visible_named(root, QStringLiteral("serviceAudio_fade_out_ms"));
    REQUIRE(fade_out != nullptr);
    CHECK(fade_out->property("text").toString().isEmpty());
    type_into(ui.window(), fade_out, "1500");
    // Play/pause follows the outputs' until it is picked.
    REQUIRE(visible_named(root, QStringLiteral("serviceAudio_on_play_pause")) != nullptr);
    QObject* general = qml_object(settings, "ServiceGeneralState");
    call(general, "setFade", QStringLiteral("on_play_pause"), QStringLiteral("on"));
    CHECK(stored().empty());

    // Saved: the service has it, and so does every audio item while it is open.
    call(settings, "save");
    CHECK_FALSE(settings->property("open").toBool());
    CHECK(stored() == nlohmann::json{{"fade_out_ms", 1500}, {"on_play_pause", true}});
    CHECK(main_fades().fade_out_ms == 1500);
    CHECK(main_fades().on_play_pause);

    // Emptied again, it is the outputs' once more.
    call(settings, "openFor", QVariant::fromValue<qint64>(lib.service), false);
    QTest::qWait(30);
    fade_out = visible_named(root, QStringLiteral("serviceAudio_fade_out_ms"));
    CHECK(fade_out->property("text").toString() == QStringLiteral("1500"));
    type_into(ui.window(), fade_out, "");
    call(general, "setFade", QStringLiteral("on_play_pause"), QString());
    call(settings, "save");
    CHECK(stored().empty());
    CHECK(main_fades().fade_out_ms == 0);
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("The Library's Edit on an audio file sets its name and its own fades",
          "[ui][media][audio]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    (void)seventhslide::test::fill_sample_library(facade);
    OperatorUi ui(facade);
    const auto files = facade.media_repo()->list_files(seventhslide::db::MediaKind::Audio, true);
    REQUIRE_FALSE(files.empty());
    const auto filename = files.front().filename;
    const auto extension = std::filesystem::path(files.front().title()).extension().string();
    REQUIRE(ui.find_component("LibraryPanel")->setProperty("tabIndex", 5));
    QTest::qWait(30);
    QObject* browser = nullptr;
    for (QObject* o : ui.window()->findChildren<QObject*>()) {
        if (seventhslide::test::is_qml_type(o, "MediaBrowser") && o->property("isAudio").toBool()) {
            browser = o;
        }
    }
    REQUIRE(browser != nullptr);
    call(browser, "selectFile", QString::fromStdString(filename), false);
    call(browser, "promptRename");
    QTest::qWait(30);
    // Made the first time it is asked for, by the audio browser alone.
    QObject* dialog = nullptr;
    for (QObject* o : ui.window()->findChildren<QObject*>()) {
        if (seventhslide::test::is_qml_type(o, "AudioFileDialog")) {
            dialog = o;
        }
    }
    REQUIRE(dialog != nullptr);
    CHECK(dialog->property("open").toBool());
    auto* card = qobject_cast<QQuickItem*>(dialog);
    REQUIRE(card != nullptr);
    CHECK(visible_named(card, QStringLiteral("audioFile_fade_in_ms"))->property("text").toString()
              .isEmpty());

    // A new name and a fade in of its own; the rest left to the service and
    // the outputs.
    type_into(ui.window(), visible_named(card, QStringLiteral("audioFileName")), "Walk in");
    type_into(ui.window(), visible_named(card, QStringLiteral("audioFile_fade_in_ms")), "800");
    REQUIRE(visible_named(card, QStringLiteral("audioFile_on_play_pause")) != nullptr);
    call(dialog, "setFade", QStringLiteral("on_play_pause"), QStringLiteral("off"));
    REQUIRE(QMetaObject::invokeMethod(dialog, "submit"));
    QTest::qWait(30);
    CHECK_FALSE(dialog->property("open").toBool());
    CHECK(facade.media_repo()->get_file(seventhslide::db::MediaKind::Audio, filename)->title()
          == "Walk in" + extension);
    CHECK(facade.media_repo()->audio_fades(filename)
          == nlohmann::json{{"fade_in_ms", 800}, {"on_play_pause", false}});
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Services: Settings opens Service Settings on the service picked; Delete asks first",
          "[ui][services]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    const auto lib = seventhslide::test::fill_sample_library(facade);
    OperatorUi ui(facade);
    QObject* dialog = ui.find_component("ServicePickerDialog");
    QObject* state = qml_object(dialog, "ServicesState");
    REQUIRE(state != nullptr);
    call(dialog, "show");
    const auto id = QVariant::fromValue<qint64>(lib.other_service);
    call(state, "select", id);
    QTest::qWait(30);
    // Service Settings is made when it is asked for, not with this window.
    CHECK(ui.built_component("ServiceSettingsDialog") == nullptr);

    QQuickItem* gear = visible_button_under(qobject_cast<QQuickItem*>(dialog),
                                            QStringLiteral("serviceSettingsButton"));
    REQUIRE(gear != nullptr);
    click(ui.window(), centre_of(gear));
    QTest::qWait(30);
    QObject* settings = ui.built_component("ServiceSettingsDialog");
    REQUIRE(settings != nullptr);
    CHECK(settings->property("open").toBool());
    CHECK(settings->property("serviceId").toLongLong() == lib.other_service);
    // Over the Services window, which stays open beneath it.
    CHECK(dialog->property("open").toBool());
    call(settings, "close");
    // Back from it, the keys are the Services window's.
    CHECK(qobject_cast<QQuickItem*>(dialog)->hasActiveFocus());

    // Delete asks first; the one left is picked in its place.
    QSignalSpy asked(state, SIGNAL(deleteAsked(QVariant,QString)));
    call(state, "beginDelete", id, QStringLiteral("Good Friday"));
    CHECK(asked.count() == 1);
    call(state, "performDelete", id);
    CHECK_FALSE(facade.service_repo()->get_service(lib.other_service));
    CHECK(state->property("selectedId").toLongLong() == lib.service);
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Service panel: ⋯ Close Service closes the open service", "[ui][services]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    (void)seventhslide::test::fill_sample_library(facade);
    OperatorUi ui(facade);
    REQUIRE(facade.live()->selectServiceItem(0));
    QTest::qWait(30);

    auto* toolbar = qobject_cast<QQuickItem*>(ui.find_component("ServiceToolbar"));
    REQUIRE(toolbar != nullptr);
    QQuickItem* more = nullptr;
    for (QQuickItem* item : toolbar->findChildren<QQuickItem*>()) {
        if (seventhslide::test::is_qml_type(item, "ServiceToolButton")
            && item->property("icon").toString() == QStringLiteral("more")) {
            more = item;
        }
    }
    REQUIRE(more != nullptr);
    REQUIRE(more->isEnabled());
    click(ui.window(), centre_of(more));
    QTest::qWait(30);

    QQuickItem* overlay = ui.window()->contentItem();
    QQuickItem* label = visible_text_under(overlay, QStringLiteral("Close Service"));
    REQUIRE(label != nullptr);
    click(ui.window(), centre_of(label));
    QTest::qWait(30);
    CHECK(facade.openServiceId() < 0);
    CHECK(facade.session().occupied_stacks().empty());
    // The menu closed, and ⋯ is off with no service open.
    CHECK(visible_text_under(overlay, QStringLiteral("Close Service")) == nullptr);
    CHECK_FALSE(more->isEnabled());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Services: New makes a service and opens its settings, the cursor in its title",
          "[ui][services]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    OperatorUi ui(facade);
    QObject* dialog = ui.find_component("ServicePickerDialog");
    QObject* state = qml_object(dialog, "ServicesState");
    REQUIRE(state != nullptr);
    call(dialog, "show");
    QTest::qWait(30);
    auto* sheet = qobject_cast<QQuickItem*>(dialog);
    CHECK(visible_text_under(sheet, QStringLiteral("No services yet")) != nullptr);

    call(state, "createService");
    QTest::qWait(50);
    const auto id = state->property("selectedId").toLongLong();
    REQUIRE(id >= 0);
    CHECK(state->property("canOpen").toBool());
    QObject* settings = ui.built_component("ServiceSettingsDialog");
    REQUIRE(settings != nullptr);
    CHECK(settings->property("open").toBool());
    CHECK(settings->property("serviceId").toLongLong() == id);
    // The cursor is in the title: what is typed names it, on Save.
    QTest::keySequence(ui.window(), QKeySequence::SelectAll);
    for (const char c : std::string("Vespers")) {
        QTest::keyClick(ui.window(), c);
    }
    call(settings, "save");
    CHECK(facade.service_repo()->get_service(id)->name == "Vespers");
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Settings > Stacks keeps a name being typed when the list changes under it", "[ui][settings]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    REQUIRE(facade.stacks()->addStack(QStringLiteral("Lower thirds")).isEmpty());
    const auto stacks = facade.stacks()->stacks();
    const QString lower = stacks.back().toMap().value(QStringLiteral("id")).toString();
    const QString other = stacks.front().toMap().value(QStringLiteral("id")).toString();

    OperatorUi ui(facade);
    QObject* dialog = ui.find_component("AppSettingsDialog");
    REQUIRE(dialog != nullptr);
    REQUIRE(QMetaObject::invokeMethod(dialog, "openDialog"));
    REQUIRE(dialog->setProperty("tab", QStringLiteral("stacks")));
    QTest::qWait(50);
    auto* sheet = qobject_cast<QQuickItem*>(dialog);
    QObject* panel = qml_object(dialog, "SettingsStacksPanel");
    REQUIRE(panel != nullptr);

    const auto open_field = [sheet]() -> QQuickItem* {
        for (QQuickItem* cell : visible_qml_items_of(sheet, "TableTextCell")) {
            if (cell->property("open").toBool()) {
                const auto inputs = visible_items_of(cell, "QQuickTextInput");
                return inputs.isEmpty() ? nullptr : inputs.front();
            }
        }
        return nullptr;
    };
    REQUIRE(panel->setProperty("editingId", lower));
    QTest::qWait(20);
    QQuickItem* field = open_field();
    REQUIRE(field != nullptr);
    CHECK(field->hasActiveFocus());
    for (const char c : {'K', 'i', 'd', 's'}) {
        QTest::keyClick(ui.window(), c);
    }

    // Another stack changes (the remote, say) and the rows are made again:
    // the name typed so far is kept, as clicking away keeps it, and nothing
    // is left open.
    REQUIRE(facade.stacks()->setStackIcon(other, QStringLiteral("star")).isEmpty());
    QTest::qWait(20);
    CHECK(open_field() == nullptr);
    CHECK(panel->property("editingId").toString().isEmpty());
    QString name;
    for (const QVariant& v : facade.stacks()->stacks()) {
        if (v.toMap().value(QStringLiteral("id")).toString() == lower) {
            name = v.toMap().value(QStringLiteral("name")).toString();
        }
    }
    CHECK(name == QStringLiteral("Kids"));
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Settings > Stacks ticks and unticks what a stack receives", "[ui][settings]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    REQUIRE(facade.stacks()->addStack(QStringLiteral("Lower thirds")).isEmpty());
    const QString lower = facade.stacks()->stacks().back().toMap().value(QStringLiteral("id")).toString();
    OperatorUi ui(facade);
    QObject* state = qml_object(ui.find_component("AppSettingsDialog"), "SettingsStacksState");
    REQUIRE(state != nullptr);

    const auto on = [&](const char* type) {
        return facade.stacks()->stackTypeDefaults().value(QString::fromLatin1(type)).toString();
    };
    const auto item = [&](const QString& stack, const char* type) {
        for (const QVariant& v : call(state, "receivesModel", stack).toList()) {
            if (v.toMap().value(QStringLiteral("id")).toString() == QLatin1String(type))
                return v.toMap();
        }
        FAIL("no checklist item " << type);
        return QVariantMap{};
    };

    // Ticking Scripture on Lower thirds takes it from Main.
    call(state, "toggleReceives", lower, QStringLiteral("bible"));
    CHECK(on("bible") == lower);
    CHECK_FALSE(item(lower, "bible").value(QStringLiteral("locked")).toBool());
    // Unticking sends it back to Main, where what Main has cannot be unticked.
    call(state, "toggleReceives", lower, QStringLiteral("bible"));
    CHECK(on("bible") == QStringLiteral("main"));
    CHECK(item(QStringLiteral("main"), "bible").value(QStringLiteral("locked")).toBool());
    // Locked is all it says: the row carries no text under its label.
    CHECK_FALSE(item(QStringLiteral("main"), "bible").contains(QStringLiteral("detail")));
    call(state, "toggleReceives", QStringLiteral("main"), QStringLiteral("song"));
    CHECK(on("song") == QStringLiteral("main"));
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Settings > Outputs opens an output's settings over Settings", "[ui][settings]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    OperatorUi ui(facade);
    QObject* settings = ui.find_component("AppSettingsDialog");
    QObject* output = ui.find_component("OutputEditDialog");
    REQUIRE(settings != nullptr);
    REQUIRE(output != nullptr);
    REQUIRE(QMetaObject::invokeMethod(settings, "openDialog"));
    REQUIRE(settings->setProperty("tab", QStringLiteral("outputs")));
    QTest::qWait(50);

    auto* sheet = qobject_cast<QQuickItem*>(settings);
    const QString name = facade.primary_output_name();
    // The main window shows the output's name too; this is the page's row. A
    // click picks it; a double click opens it.
    QQuickItem* row = visible_text_under(sheet, name);
    REQUIRE(row != nullptr);
    click(ui.window(), centre_of(row));
    CHECK_FALSE(output->property("open").toBool());
    QTest::mouseDClick(ui.window(), Qt::LeftButton, {}, centre_of(row));
    QTest::qWait(30);
    REQUIRE(output->property("open").toBool());
    CHECK(settings->property("open").toBool());
    // It draws over Settings: both sit on the window's content item.
    auto* outputSheet = qobject_cast<QQuickItem*>(output);
    REQUIRE(sheet->parentItem() == outputSheet->parentItem());
    CHECK(outputSheet->z() > sheet->z());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Settings > Outputs: the arrows move the picked output", "[ui][settings]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    const QString main = facade.primary_output_name();
    REQUIRE(facade.outputSettings()->createOutput(QStringLiteral("Stage")).contains(QStringLiteral("name")));
    const auto first = [&facade] { return QString::fromStdString(facade.session().outputs().front().name); };
    REQUIRE(first() == main);

    OperatorUi ui(facade);
    QObject* settings = ui.find_component("AppSettingsDialog");
    REQUIRE(settings != nullptr);
    REQUIRE(QMetaObject::invokeMethod(settings, "openDialog"));
    REQUIRE(settings->setProperty("tab", QStringLiteral("outputs")));
    QTest::qWait(50);

    auto* sheet = qobject_cast<QQuickItem*>(settings);
    QQuickItem* up = visible_button_under(sheet, QStringLiteral("outputMoveUp"));
    QQuickItem* down = visible_button_under(sheet, QStringLiteral("outputMoveDown"));
    REQUIRE(up != nullptr);
    REQUIRE(down != nullptr);
    // None picked: the first is shown picked, and can only go down.
    CHECK_FALSE(up->isEnabled());
    CHECK(down->isEnabled());

    // Pick Stage, the last, and move it to the top.
    QQuickItem* stage = visible_text_under(sheet, QStringLiteral("Stage"));
    REQUIRE(stage != nullptr);
    click(ui.window(), centre_of(stage));
    CHECK_FALSE(down->isEnabled());
    click(ui.window(), centre_of(up));
    QTest::qWait(20);
    CHECK(first() == QStringLiteral("Stage"));
    CHECK_FALSE(up->isEnabled());
    CHECK(down->isEnabled());

    // Renamed in its Output window, it stays the one picked.
    QObject* panel = qml_object(settings, "SettingsOutputsState");
    REQUIRE(panel != nullptr);
    REQUIRE(facade.outputSettings()
                ->updateOutputSettings(QStringLiteral("Stage"),
                                       {{QStringLiteral("name"), QStringLiteral("Stage 2")}})
                .contains(QStringLiteral("name")));
    QTest::qWait(20);
    CHECK(panel->property("shownName").toString() == QStringLiteral("Stage 2"));
    CHECK_FALSE(up->isEnabled());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Settings > Outputs switches an output off, and the main page drops its card", "[ui][settings]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    const QString main = facade.primary_output_name();
    REQUIRE(facade.outputSettings()->createOutput(QStringLiteral("Stage")).contains(QStringLiteral("name")));

    OperatorUi ui(facade);
    QObject* settings = ui.find_component("AppSettingsDialog");
    REQUIRE(settings != nullptr);
    QObject* strip = ui.find_component("OutputsPanel");
    REQUIRE(strip != nullptr);
    QQuickItem* cards = list_view_of(strip, facade.outputsOn());
    REQUIRE(cards != nullptr);
    CHECK(cards->property("count").toInt() == 2);
    REQUIRE(QMetaObject::invokeMethod(settings, "openDialog"));
    REQUIRE(settings->setProperty("tab", QStringLiteral("outputs")));
    QTest::qWait(50);

    auto* sheet = qobject_cast<QQuickItem*>(settings);
    const auto switches = visible_qml_items_of(sheet, "ToggleSwitch");
    REQUIRE(switches.size() == 2);  // Main, then Stage
    CHECK(switches[0]->property("checked").toBool());
    CHECK(switches[1]->property("checked").toBool());

    click(ui.window(), centre_of(switches[1]));
    QTest::qWait(20);
    CHECK_FALSE(facade.session().output_is_on(
        facade.session().outputs().back().id));
    CHECK_FALSE(switches[1]->property("checked").toBool());
    // Still listed here, to switch back on; gone from the main page.
    CHECK(visible_text_under(sheet, QStringLiteral("Stage")) != nullptr);
    CHECK(cards->property("count").toInt() == 1);
    CHECK(settings->property("errorText").toString().isEmpty());

    // The last one on stays on, and says why.
    click(ui.window(), centre_of(switches[0]));
    QTest::qWait(20);
    CHECK(facade.session().output_is_on(facade.session().outputs().front().id));
    CHECK(switches[0]->property("checked").toBool());
    CHECK_FALSE(settings->property("errorText").toString().isEmpty());

    click(ui.window(), centre_of(switches[1]));
    QTest::qWait(20);
    CHECK(switches[1]->property("checked").toBool());
    CHECK(cards->property("count").toInt() == 2);
    CHECK(facade.primary_output_name() == main);
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Settings > Bibles follows the translations as they come and go", "[ui][settings]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    const auto lib = seventhslide::test::fill_sample_library(facade);
    OperatorUi ui(facade);
    QObject* settings = ui.find_component("AppSettingsDialog");
    REQUIRE(settings != nullptr);
    REQUIRE(QMetaObject::invokeMethod(settings, "openDialog"));
    REQUIRE(settings->setProperty("tab", QStringLiteral("bibles")));
    QTest::qWait(30);
    auto* sheet = qobject_cast<QQuickItem*>(settings);
    CHECK(visible_text_under(sheet, QStringLiteral("Sample Bible")) != nullptr);
    CHECK(visible_text_under(sheet, QStringLiteral("No Bibles imported")) == nullptr);

    // Deleting the only one leaves the empty page, not an empty list.
    QObject* state = qml_object(settings, "SettingsBiblesState");
    REQUIRE(state != nullptr);
    call(state, "performDeleteBible", QVariant::fromValue(static_cast<qint64>(lib.bible)));
    QTest::qWait(30);
    CHECK(visible_text_under(sheet, QStringLiteral("Sample Bible")) == nullptr);
    CHECK(visible_text_under(sheet, QStringLiteral("No Bibles imported")) != nullptr);
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Settings > Bibles: abbreviation, search and switching one off", "[ui][settings]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    const auto lib = seventhslide::test::fill_sample_library(facade);
    const auto id = QVariant::fromValue(static_cast<qint64>(lib.bible));
    OperatorUi ui(facade);
    QObject* settings = ui.find_component("AppSettingsDialog");
    REQUIRE(QMetaObject::invokeMethod(settings, "openDialog"));
    REQUIRE(settings->setProperty("tab", QStringLiteral("bibles")));
    QObject* state = qml_object(settings, "SettingsBiblesState");
    REQUIRE(state != nullptr);
    auto* sheet = qobject_cast<QQuickItem*>(settings);

    call(state, "setAbbreviation", id, QStringLiteral("SB"));
    REQUIRE(state->setProperty("bibleSearchText", QStringLiteral("sb")));
    QTest::qWait(30);
    CHECK(visible_text_under(sheet, QStringLiteral("Sample Bible")) != nullptr);
    CHECK(visible_text_under(sheet, QStringLiteral("SB")) != nullptr);

    // Renaming it out of the search shows "No matches", not an empty list.
    REQUIRE(state->setProperty("bibleSearchText", QStringLiteral("sample")));
    call(state, "renameBible", id, QStringLiteral("Other Bible"));
    QTest::qWait(30);
    CHECK(visible_text_under(sheet, QStringLiteral("No matches")) != nullptr);

    REQUIRE(state->setProperty("bibleSearchText", QString()));
    call(state, "setEnabled", id, false);
    QTest::qWait(30);
    const auto rows = visible_qml_items_of(sheet, "BibleTableRow");
    REQUIRE(rows.size() == 1);
    CHECK_FALSE(rows.front()->property("on").toBool());
    CHECK(facade.library()->enabledBibles()->rowCount() == 0);
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Settings > Bibles: a table to sort and edit in place", "[ui][settings]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    const auto lib = seventhslide::test::fill_sample_library(facade);
    REQUIRE(facade.library()->setBibleAbbreviation(lib.bible, QStringLiteral("SB")).isEmpty());
    seventhslide::model::BibleVerse verse;
    verse.book = "John";
    verse.chapter = 3;
    verse.verse_num = 16;
    verse.text = "For God so loved the world";
    const auto second = facade.bible_repo()->import_bible("Another Bible", "", {verse});
    REQUIRE(facade.library()->setBibleAbbreviation(second, QStringLiteral("XB")).isEmpty());
    facade.library()->refresh_bibles();
    OperatorUi ui(facade);
    auto* w = ui.window();
    QObject* settings = ui.find_component("AppSettingsDialog");
    REQUIRE(QMetaObject::invokeMethod(settings, "openDialog"));
    REQUIRE(settings->setProperty("tab", QStringLiteral("bibles")));
    QTest::qWait(30);
    QObject* state = qml_object(settings, "SettingsBiblesState");
    REQUIRE(state != nullptr);
    auto* sheet = qobject_cast<QQuickItem*>(settings);

    const auto names = [&]() {
        QStringList out;
        for (auto* item : visible_qml_items_of(sheet, "BibleTableRow")) {
            out << item->property("name").toString();
        }
        return out;
    };
    const auto row = [&](const QString& name) -> QQuickItem* {
        for (auto* item : visible_qml_items_of(sheet, "BibleTableRow")) {
            if (item->property("name").toString() == name) {
                return item;
            }
        }
        FAIL("no row " << name.toStdString());
        return nullptr;
    };
    const auto shown = [&]() {
        return state->property("shownBible").toMap().value(QStringLiteral("name")).toString();
    };
    const auto type = [&](const char* text) {
        for (const char* c = text; *c; ++c) {
            QTest::keyClick(w, *c);
        }
    };

    // By name, A to Z; a click on Abbreviation sorts by it, again the other way.
    CHECK(names() == QStringList{QStringLiteral("Another Bible"), QStringLiteral("Sample Bible")});
    click(w, centre_of(visible_text_under(sheet, QStringLiteral("Abbreviation"))));
    CHECK(state->property("bibleSortKey").toString() == QStringLiteral("abbreviation"));
    CHECK(names() == QStringList{QStringLiteral("Sample Bible"), QStringLiteral("Another Bible")});
    click(w, centre_of(visible_text_under(sheet, QStringLiteral("Abbreviation"))));
    CHECK(names() == QStringList{QStringLiteral("Another Bible"), QStringLiteral("Sample Bible")});

    // The first is picked until another is.
    CHECK(shown() == QStringLiteral("Another Bible"));
    click(w, centre_of(row(QStringLiteral("Sample Bible"))));
    CHECK(shown() == QStringLiteral("Sample Bible"));
    CHECK(row(QStringLiteral("Sample Bible"))->property("selected").toBool());

    // A double click on the name edits it in place; Enter keeps it.
    QTest::mouseDClick(w, Qt::LeftButton, {},
                       centre_of(visible_text_under(sheet, QStringLiteral("Sample Bible"))));
    QTest::qWait(30);
    CHECK(row(QStringLiteral("Sample Bible"))->property("editing").toString() == QStringLiteral("name"));
    type("Renamed Bible");
    QTest::keyClick(w, Qt::Key_Return);
    QTest::qWait(30);
    CHECK(shown() == QStringLiteral("Renamed Bible"));
    CHECK(row(QStringLiteral("Renamed Bible"))->property("editing").toString().isEmpty());

    // On the abbreviation, Escape drops the edit.
    QTest::mouseDClick(w, Qt::LeftButton, {}, centre_of(visible_text_under(sheet, QStringLiteral("SB"))));
    QTest::qWait(30);
    CHECK(row(QStringLiteral("Renamed Bible"))->property("editing").toString()
          == QStringLiteral("abbreviation"));
    type("NO");
    QTest::keyClick(w, Qt::Key_Escape);
    QTest::qWait(30);
    CHECK(row(QStringLiteral("Renamed Bible"))->property("abbreviation").toString() == QStringLiteral("SB"));
    CHECK(settings->property("open").toBool());

    // A row's switch turns it off; the pick stays.
    const auto toggles = visible_qml_items_of(row(QStringLiteral("Another Bible")), "ToggleSwitch");
    REQUIRE(toggles.size() == 1);
    click(w, centre_of(toggles.front()));
    CHECK(facade.library()->enabledBibles()->rowCount() == 1);
    CHECK_FALSE(row(QStringLiteral("Another Bible"))->property("on").toBool());
    CHECK(shown() == QStringLiteral("Renamed Bible"));

    // Deleting the one showing shows the first that is left.
    call(state, "performDeleteBible", QVariant::fromValue(static_cast<qint64>(lib.bible)));
    QTest::qWait(30);
    CHECK(shown() == QStringLiteral("Another Bible"));

    // A search that leaves none.
    REQUIRE(state->setProperty("bibleSearchText", QStringLiteral("zzz")));
    QTest::qWait(30);
    CHECK(state->property("shownBible").isNull());
    CHECK(visible_text_under(sheet, QStringLiteral("No matches")) != nullptr);
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Settings > Web Remote shows the address and its QR code once running", "[ui][settings]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    OperatorUi ui(facade);

    // A port nothing else holds.
    QTcpServer probe;
    REQUIRE(probe.listen(QHostAddress::LocalHost, 0));
    const int port = probe.serverPort() < 1024 ? 18765 : probe.serverPort();
    probe.close();
    REQUIRE(facade.updateAppSettings({{QStringLiteral("remoteEnabled"), true},
                                      {QStringLiteral("remotePort"), port}})
                .isEmpty());

    QObject* settings = ui.find_component("AppSettingsDialog");
    REQUIRE(QMetaObject::invokeMethod(settings, "openDialog"));
    REQUIRE(settings->setProperty("tab", QStringLiteral("remote")));
    QObject* state = qml_object(settings, "SettingsRemoteState");
    REQUIRE(state != nullptr);
    REQUIRE(QTest::qWaitFor([&] { return !state->property("address").toString().isEmpty(); }, 5000));
    const QString address = state->property("address").toString();
    CHECK(address.contains(QStringLiteral(":%1").arg(port)));

    auto* sheet = qobject_cast<QQuickItem*>(settings);
    CHECK(visible_text_under(sheet, address) != nullptr);
    // The code is drawn whole pixels per module, for that address.
    bool drawn = false;
    for (QQuickItem* item : seventhslide::test::visible_items_of(sheet, "QQuickImage")) {
        if (item->property("source").toString().startsWith(QStringLiteral("image://ssicon/"))
            && item->width() >= 64 && qRound(item->width()) == qRound(item->height())) {
            drawn = true;
        }
    }
    CHECK(drawn);
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Every content type is named and drawn from the one table", "[ui]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    OperatorUi ui(facade);
    auto* types = qmlEngine(ui.window())->singletonInstance<QObject*>("SeventhSlide", "ContentTypes");
    REQUIRE(types != nullptr);
    const auto of_tab = [&](const QString& tab) {
        QVariant out;
        REQUIRE(QMetaObject::invokeMethod(types, "ofTab", Q_RETURN_ARG(QVariant, out),
                                          Q_ARG(QVariant, QVariant(tab))));
        return out.toMap();
    };

    // One row a stack type, in core's order, each with a glyph and names of
    // its own.
    const QVariantList items = types->property("items").toList();
    REQUIRE(items.size() == static_cast<qsizetype>(seventhslide::model::k_stack_type_keys.size()));
    QSet<QString> labels;
    QSet<QString> singulars;
    for (qsizetype i = 0; i < items.size(); ++i) {
        const QVariantMap item = items[i].toMap();
        INFO("type " << item.value(QStringLiteral("key")).toString().toStdString());
        CHECK(item.value(QStringLiteral("key")).toString()
              == QString::fromUtf8(seventhslide::model::k_stack_type_keys[static_cast<std::size_t>(i)]));
        CHECK(seventhslide::app::glyphs::find(
                  item.value(QStringLiteral("icon")).toString().toStdString()) != nullptr);
        CHECK_FALSE(item.value(QStringLiteral("label")).toString().isEmpty());
        CHECK_FALSE(item.value(QStringLiteral("singular")).toString().isEmpty());
        labels.insert(item.value(QStringLiteral("label")).toString());
        singulars.insert(item.value(QStringLiteral("singular")).toString());
    }
    CHECK(labels.size() == items.size());
    CHECK(singulars.size() == items.size());

    // The Library's tabs are those kinds, named as the table names them.
    QObject* panel = ui.find_component("LibraryPanel");
    REQUIRE(panel != nullptr);
    auto* strip = qml_object(panel, "PanelTabStrip");
    REQUIRE(strip != nullptr);
    const QStringList ids = panel->property("shownTabIds").toStringList();
    const QStringList titles = strip->property("titles").toStringList();
    REQUIRE(ids.size() == items.size());
    REQUIRE(titles.size() == ids.size());
    for (qsizetype i = 0; i < ids.size(); ++i) {
        INFO("tab " << ids[i].toStdString());
        CHECK(titles[i] == of_tab(ids[i]).value(QStringLiteral("label")).toString());
    }

    // The rundown draws each kind with the table's glyph.
    QObject* service = ui.find_component("ServicePanel");
    REQUIRE(service != nullptr);
    const auto icon_url = [&](const QString& kind) {
        QVariant out;
        REQUIRE(QMetaObject::invokeMethod(service, "iconUrl", Q_RETURN_ARG(QVariant, out),
                                          Q_ARG(QVariant, kind), Q_ARG(QVariant, QStringLiteral("#000000"))));
        return out.toString();
    };
    for (const QVariant& v : items) {
        const QVariantMap item = v.toMap();
        CHECK(icon_url(item.value(QStringLiteral("key")).toString())
              == icon_url(item.value(QStringLiteral("icon")).toString()));
    }
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// Deleting rows, or pinning them to a stack, is one command for all of them:
// the rundown is read again once, not once a row.
TEST_CASE("The rundown's selection is deleted and pinned in one command", "[ui]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    REQUIRE(facade.library()->createService(QStringLiteral("Sunday")) > 0);
    for (const char* title : {"One", "Two", "Three", "Four"}) {
        REQUIRE(facade.library()->addDividerToService(QString::fromLatin1(title)));
    }
    const auto ids = [&] {
        QVariantList out;
        for (int r = 0; r < facade.serviceItems()->rowCount(); ++r) {
            out.push_back(facade.serviceItems()->data(facade.serviceItems()->index(r),
                                                      seventhslide::app::ServiceItemModel::ItemIdRole));
        }
        return out;
    };
    OperatorUi ui(facade);
    QObject* panel = ui.find_component("ServicePanel");
    REQUIRE(panel != nullptr);
    QSignalSpy reloads(facade.library(), &seventhslide::app::LibraryController::liveChanged);

    const QVariantList three = ids().mid(0, 3);
    REQUIRE(panel->setProperty("stackMenuIds", three));
    REQUIRE(QMetaObject::invokeMethod(panel, "applyStackToMenuIds", Q_ARG(QVariant, QStringLiteral("main"))));
    CHECK(reloads.size() == 1);
    for (int r = 0; r < 3; ++r) {
        CHECK(facade.serviceItems()->data(facade.serviceItems()->index(r), seventhslide::app::ServiceItemModel::StackIdRole)
                  .toString() == QStringLiteral("main"));
    }

    reloads.clear();
    auto* selection = panel->property("itemSelection").value<QObject*>();
    REQUIRE(selection != nullptr);
    REQUIRE(QMetaObject::invokeMethod(selection, "set", Q_ARG(QVariant, three),
                                      Q_ARG(QVariant, three.front()), Q_ARG(QVariant, three.front())));
    REQUIRE(QMetaObject::invokeMethod(panel, "toolDeleteItem"));
    CHECK(reloads.size() == 1);
    CHECK(facade.serviceItems()->rowCount() == 1);
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}
