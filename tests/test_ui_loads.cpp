#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "author_list_model.hpp"
#include "slide_edit_session.hpp"
#include "freeform_library_controller.hpp"
#include "library_controller.hpp"
#include "library_models.hpp"
#include "media_controller.hpp"
#include "operator_ui.hpp"
#include "output_settings_controller.hpp"
#include "remote_controller.hpp"
#include "facade_all.hpp"
#include "stack_controller.hpp"
#include "test_qml.hpp"
#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"
#include "theme_controller.hpp"
#include "ui_test_helpers.hpp"

#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/theme/song_theme_pack.hpp>
#include <seventhslide/db/media_repository.hpp>
#include <seventhslide/db/song_repository.hpp>
#include <seventhslide/model/content.hpp>
#include <seventhslide/model/transition.hpp>

#include <functional>
#include <optional>

#include <QSignalSpy>
#include <QVariant>
#include <QQmlEngine>
#include <QQmlError>
#include <QColor>
#include <QImage>
#include <QJSValue>
#include <QPoint>
#include <QSet>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStringList>
#include <QTest>
#include <QUrl>
#include <QtQml/qqmlextensionplugin.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace CanvasAlignQml = seventhslide::app::CanvasAlignQml;
namespace CanvasDistributeQml = seventhslide::app::CanvasDistributeQml;
namespace CanvasFlipQml = seventhslide::app::CanvasFlipQml;
namespace CanvasOrderQml = seventhslide::app::CanvasOrderQml;

Q_IMPORT_QML_PLUGIN(SeventhSlidePlugin)
Q_IMPORT_QML_PLUGIN(SeventhSlide_NativePlugin)

using seventhslide::app::SessionFacade;
using seventhslide::test::centre_of;
using seventhslide::test::click;
using seventhslide::test::drag;
using seventhslide::test::OperatorUi;
using seventhslide::test::qml_object;
using seventhslide::test::visible_text_under;
using seventhslide::test::visible_button_under;

// The operator UI is one QML module. A component that is not registered, or a
// type that does not resolve, stops the whole window from loading - and no
// other test instantiates the module.
TEST_CASE("The operator UI loads", "[ui][smoke]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    OperatorUi ui(facade);
    INFO(ui.warnings());
    REQUIRE(ui.window() != nullptr);
    REQUIRE(ui.warnings().empty());
}

// A library may have no outputs: its last deleted, or one joined from another
// computer before its outputs have come. Nothing is shown, and the window,
// the Settings pages and the song editor still work.
TEST_CASE("The window works with a library that has no outputs", "[ui][smoke][outputs]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    seventhslide::model::Song song;
    song.title = "Amazing Grace";
    song.lyrics = "[V1]\nAmazing grace\nHow sweet the sound";
    song.verse_order = "v1";
    const auto id = facade.song_repo()->insert(song);
    facade.refresh_library();

    OperatorUi ui(facade);
    REQUIRE(ui.window() != nullptr);
    REQUIRE(facade.outputSettings()->deleteOutput(QStringLiteral("Main")).isEmpty());
    REQUIRE(facade.session().outputs().empty());
    QTest::qWait(50);

    // Live, with nowhere to show it.
    facade.live()->play_song(song);
    QTest::qWait(50);

    QObject* settings = ui.find_component("AppSettingsDialog");
    REQUIRE(settings != nullptr);
    REQUIRE(QMetaObject::invokeMethod(settings, "openPage", Q_ARG(QVariant, QStringLiteral("outputs"))));
    QTest::qWait(50);
    REQUIRE(QMetaObject::invokeMethod(settings, "close"));

    QObject* editor = ui.find_component("SongEditDialog");
    REQUIRE(editor != nullptr);
    REQUIRE(QMetaObject::invokeMethod(editor, "openLibrary",
                                      Q_ARG(QVariant, QVariant::fromValue<qint64>(id))));
    QTest::qWait(50);
    REQUIRE(QMetaObject::invokeMethod(editor, "close"));

    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// The operator shortcuts are window shortcuts, which win over the focused
// item's keys. Behind a dialog or editor the keys belong to it (arrows nudge
// the selected object in the Freeform editor and theme designer), so they must
// never move the live output or blank it.
TEST_CASE("Operator shortcuts stand down while a modal is open", "[ui][shortcuts]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    REQUIRE(facade.library()->createService(QStringLiteral("Svc")) > 0);

    using seventhslide::db::MediaKind;
    facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Image, "a.png", "A");
    facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Image, "b.png", "B");
    facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Image, "c.png", "C");
    const auto folder_id = facade.media_repo()->folders(MediaKind::Image).create("Pack");
    for (const char* file : {"a.png", "b.png", "c.png"}) {
        (void)facade.media_repo()->add_folder_item(MediaKind::Image, folder_id, file);
    }
    facade.refresh_library();
    REQUIRE(facade.media()->addImageFolderToService(static_cast<qint64>(folder_id)));
    REQUIRE(facade.live()->selectServiceItem(0));
    REQUIRE(facade.program()->totalLines() == 3);

    OperatorUi ui(facade);
    QQuickWindow* window = ui.window();
    REQUIRE(window != nullptr);
    window->requestActivate();
    REQUIRE(QTest::qWaitForWindowActive(window));

    // No modal: the shortcuts drive the live output.
    REQUIRE(facade.program()->lineCursor() == 0);
    QTest::keyClick(window, Qt::Key_Right);
    REQUIRE(facade.program()->lineCursor() == 1);
    QTest::keyClick(window, Qt::Key_B);
    REQUIRE(facade.live()->global_blank());
    QTest::keyClick(window, Qt::Key_B);
    REQUIRE_FALSE(facade.live()->global_blank());

    QObject* dialog = ui.find_component("AppDialog");
    REQUIRE(dialog != nullptr);
    REQUIRE(dialog->setProperty("open", true));
    QCoreApplication::processEvents();

    for (const auto key : {Qt::Key_Right, Qt::Key_Down, Qt::Key_PageDown, Qt::Key_Left,
                           Qt::Key_Up, Qt::Key_PageUp, Qt::Key_B, Qt::Key_F}) {
        INFO("key " << key);
        QTest::keyClick(window, key);
        REQUIRE(facade.program()->lineCursor() == 1);
        REQUIRE_FALSE(facade.live()->global_blank());
        REQUIRE_FALSE(facade.live()->global_freeze());
    }
    QTest::keyClick(window, Qt::Key_B, Qt::ShiftModifier);
    REQUIRE_FALSE(facade.session().stack_is_blank(facade.stacks()->focusedStackId().toStdString()));

    // Closing the modal hands the keys back.
    REQUIRE(dialog->setProperty("open", false));
    QCoreApplication::processEvents();
    QTest::keyClick(window, Qt::Key_Right);
    REQUIRE(facade.program()->lineCursor() == 2);
}

// B and F blank and freeze every output; an output's number first, that
// output alone; with Shift, the stack the controller shows. Shift+Esc lifts
// them all.
TEST_CASE("Blank and Freeze keys reach every output, one output, or the stack", "[ui][shortcuts]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    REQUIRE(facade.outputSettings()->createOutput(QStringLiteral("Stage")).contains(QStringLiteral("name")));
    seventhslide::model::Song song;
    song.lyrics = "---[Verse:1]---\nOne\n[---][---]\nTwo\n";
    song.verse_order = "v1";
    facade.live()->play_song(song);

    const auto& outputs = facade.session().outputs_on();
    REQUIRE(outputs.size() == 2);
    const std::string first = outputs[0].id;
    const std::string second = outputs[1].id;
    const std::string stack = facade.stacks()->focusedStackId().toStdString();
    const auto& session = facade.session();

    OperatorUi ui(facade);
    QQuickWindow* window = ui.window();
    REQUIRE(window != nullptr);
    window->requestActivate();
    REQUIRE(QTest::qWaitForWindowActive(window));

    QTest::keyClick(window, Qt::Key_B);
    CHECK(session.output_is_blank(first));
    CHECK(session.output_is_blank(second));
    QTest::keyClick(window, Qt::Key_B);
    CHECK_FALSE(session.output_is_blank(first));
    CHECK_FALSE(session.output_is_blank(second));

    QTest::keyClick(window, Qt::Key_2);
    QTest::keyClick(window, Qt::Key_B);
    CHECK_FALSE(session.output_is_blank(first));
    CHECK(session.output_is_blank(second));
    QTest::keyClick(window, Qt::Key_2);
    QTest::keyClick(window, Qt::Key_F);
    CHECK_FALSE(session.output_is_frozen(first));
    CHECK(session.output_is_frozen(second));

    // No output 9: the number is ignored, and B blanks every output.
    QTest::keyClick(window, Qt::Key_9);
    QTest::keyClick(window, Qt::Key_B);
    CHECK(session.output_is_blank(first));
    CHECK(session.output_is_blank(second));

    // A number waits only a moment for its B or F.
    QTest::keyClick(window, Qt::Key_1);
    CHECK(window->property("armedOutput").toInt() == 0);
    REQUIRE(QTest::qWaitFor([&] { return window->property("armedOutput").toInt() == -1; }, 5000));
    QTest::keyClick(window, Qt::Key_B);
    CHECK_FALSE(session.output_is_blank(first));
    CHECK_FALSE(session.output_is_blank(second));

    QTest::keyClick(window, Qt::Key_B, Qt::ShiftModifier);
    QTest::keyClick(window, Qt::Key_F, Qt::ShiftModifier);
    CHECK(session.stack_is_blank(stack));
    CHECK(session.stack_is_frozen(stack));
    CHECK_FALSE(session.output_is_blank(first));

    QTest::keyClick(window, Qt::Key_Escape, Qt::ShiftModifier);
    CHECK_FALSE(session.stack_is_blank(stack));
    CHECK_FALSE(session.stack_is_frozen(stack));
    CHECK_FALSE(session.output_is_frozen(second));

    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

namespace {

QString png_url(const std::filesystem::path& path)
{
    QImage img(16, 16, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    const auto file = QString::fromStdString(path.string());
    REQUIRE(img.save(file));
    return QUrl::fromLocalFile(file).toString();
}

}  // namespace

namespace {

/// The slide editor's window and view-model, open on ``freeform``.
struct SlideEditor {
    QObject* window = nullptr;
    QObject* state = nullptr;
};

SlideEditor open_slide_editor(OperatorUi& ui)
{
    SlideEditor out;
    out.window = ui.find_component("SlideEditorWindow");
    REQUIRE(out.window != nullptr);
    out.state = qml_object(out.window, "SlideEditState");
    REQUIRE(out.state != nullptr);
    return out;
}

/// The RibbonHome row in the editor: the Arrange menus act through it.
QObject* ribbon_home(QObject* window)
{
    for (QObject* o : window->findChildren<QObject*>()) {
        if (QByteArray(o->metaObject()->className()).startsWith("RibbonHome_QMLTYPE_")) {
            return o;
        }
    }
    return nullptr;
}

}  // namespace

// The image is copied in the background, and the editor applies it when the
// copy reports back.
TEST_CASE("A slide background image is applied once its import finishes", "[ui][import]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    const auto freeform = facade.freeformLibrary()->createFreeform(QStringLiteral("Bg"));
    REQUIRE(freeform > 0);
    REQUIRE(facade.slideEdit()->openFreeform(freeform));

    OperatorUi ui(facade);
    const auto editor = open_slide_editor(ui);
    REQUIRE(QMetaObject::invokeMethod(editor.state, "importSlideBackground",
                                      Q_ARG(QVariant, png_url(dir.path / "bg.png")),
                                      Q_ARG(QVariant, false)));
    REQUIRE(facade.media()->importing());

    const auto bg_image = [&] {
        return facade.slideEdit()->slides()->background().value(QStringLiteral("image")).toString();
    };
    REQUIRE(QTest::qWaitFor([&] { return !bg_image().isEmpty(); }, 10000));
    REQUIRE(facade.media()->images()->rowCount() == 1);
    REQUIRE(bg_image()
            == facade.media()
                   ->images()
                   ->data(facade.media()->images()->index(0),
                          seventhslide::app::ImageListModel::FilenameRole)
                   .toString());
}

TEST_CASE("The slide editor's output picker lists the item's outputs that are on", "[ui][freeform]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.init_libraries(dir.path));
    REQUIRE(facade.outputSettings()->createOutput(QStringLiteral("Stage")).contains(QStringLiteral("name")));
    const auto freeform = facade.freeformLibrary()->createFreeform(QStringLiteral("Bg"));
    REQUIRE(freeform > 0);
    REQUIRE(facade.slideEdit()->openFreeform(freeform));

    OperatorUi ui(facade);
    const auto editor = open_slide_editor(ui);
    auto* picker = editor.window->findChild<QObject*>(QStringLiteral("slideEditorOutput"));
    REQUIRE(picker != nullptr);
    // In the outputs' own order, whichever is picked: the menu does not
    // reorder under the pointer.
    const auto listed = [&] {
        QStringList names;
        for (const auto& option : picker->property("options").toList()) {
            names << option.toMap().value(QStringLiteral("value")).toString();
        }
        return names;
    };
    const QStringList both{QStringLiteral("Main"), QStringLiteral("Stage")};
    REQUIRE(listed() == both);
    REQUIRE(picker->property("value").toString() == QStringLiteral("Main"));
    facade.slideEdit()->setOutput(QStringLiteral("Stage"));
    QTest::qWait(0);
    CHECK(picker->property("value").toString() == QStringLiteral("Stage"));
    CHECK(listed() == both);
    facade.slideEdit()->setOutput(QStringLiteral("Main"));
    QTest::qWait(0);

    REQUIRE(facade.outputSettings()->setOutputOn(QStringLiteral("Stage"), false).isEmpty());
}

TEST_CASE("Reopening the slide editor lays out its slide list without a binding loop", "[ui][scrollbar]")
{
    // Closing the editor collapses the slide list to nothing; reopening grows
    // it back. Its scrollbar used to reserve the gutter from inside its own
    // "overflowing" change, which resized the 16:9 rows, which changed the
    // content height "overflowing" reads: a binding loop.
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    const auto freeform = facade.freeformLibrary()->createFreeform(QStringLiteral("Card"));
    REQUIRE(freeform > 0);
    REQUIRE(facade.slideEdit()->openFreeform(freeform));

    OperatorUi ui(facade);
    REQUIRE(ui.window() != nullptr);
    // Close and reopen before the window has laid out once: the list grows
    // from nothing with the editor's rows already in it.
    facade.slideEdit()->cancel();
    REQUIRE(facade.slideEdit()->openFreeform(freeform));
    QTest::qWait(50);
    CHECK(ui.warnings().find("Binding loop") == std::string::npos);
}

// Closing the editor drops the pending background, so a copy that finishes
// later never lands on whatever is open by then.
TEST_CASE("A slide background import that outlives its editor is not applied", "[ui][import]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    const auto freeform = facade.freeformLibrary()->createFreeform(QStringLiteral("Bg"));
    REQUIRE(freeform > 0);
    REQUIRE(facade.slideEdit()->openFreeform(freeform));

    OperatorUi ui(facade);
    const auto editor = open_slide_editor(ui);
    REQUIRE(QMetaObject::invokeMethod(editor.state, "importSlideBackground",
                                      Q_ARG(QVariant, png_url(dir.path / "bg.png")),
                                      Q_ARG(QVariant, false)));
    facade.slideEdit()->cancel();
    REQUIRE(facade.slideEdit()->openFreeform(freeform));

    REQUIRE(QTest::qWaitFor([&] { return !facade.media()->importing(); }, 10000));
    QTest::qWait(20);
    // The image still reaches the library; it just is not anyone's background.
    REQUIRE(facade.media()->images()->rowCount() == 1);
    REQUIRE(facade.slideEdit()->slides()->background().value(QStringLiteral("image")).toString().isEmpty());
}

// A background theme is copied into the slide: its colour
// and fade at once, a picture it owns once the library has its own copy.
// The slide keeps them when the theme goes; nothing of the theme's shows on
// the stage but what the slide now has.
TEST_CASE("A background theme is copied into the slide, not linked", "[ui][freeform][bg]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    const auto freeform = facade.freeformLibrary()->createFreeform(QStringLiteral("Bg"));
    REQUIRE(freeform > 0);
    REQUIRE(facade.slideEdit()->openFreeform(freeform));
    auto* slides = facade.slideEdit()->slides();
    const auto bg = [slides](const char* key) {
        return slides->background().value(QString::fromLatin1(key)).toString();
    };

    auto* themes = facade.themes();
    const auto dusk = themes->create(QStringLiteral("Main"), QStringLiteral("bg"), QStringLiteral("Dusk"),
                                     {{QStringLiteral("background_type"), QStringLiteral("color")},
                                      {QStringLiteral("background_color"), QStringLiteral("#123456")},
                                      {QStringLiteral("bg_enable_fade"), true},
                                      {QStringLiteral("bg_fade_duration"), 700}});
    const auto dusk_id = dusk.value(QStringLiteral("id")).toString();
    REQUIRE_FALSE(dusk_id.isEmpty());
    REQUIRE(slides->copyBackgroundTheme(dusk_id).isEmpty());
    CHECK(bg("kind") == QStringLiteral("color"));
    CHECK(bg("color") == QStringLiteral("#123456"));
    CHECK(bg("transition_type") == QStringLiteral("fade"));
    CHECK(slides->background().value(QStringLiteral("transition_ms")).toInt() == 700);

    // A picture the theme owns: the slide gets the library's own copy.
    QImage picture(32, 18, QImage::Format_RGB32);
    picture.fill(QColor(QStringLiteral("#336699")));
    const auto png = dir.path / "dawn.png";
    REQUIRE(picture.save(QString::fromStdString(png.string())));
    const auto dawn = themes->create(QStringLiteral("Main"), QStringLiteral("bg"), QStringLiteral("Dawn"),
                                     {{QStringLiteral("background_type"), QStringLiteral("image")}});
    const auto dawn_id = dawn.value(QStringLiteral("id")).toString();
    QSignalSpy uploaded(themes, &seventhslide::app::ThemeController::assetUploadFinished);
    themes->upload_asset(QStringLiteral("Main"), dawn_id, QStringLiteral("bg"),
                        QString::fromStdString(png.string()));
    REQUIRE(uploaded.wait(10000));
    REQUIRE(uploaded.front().at(1).toString().isEmpty());

    REQUIRE(slides->copyBackgroundTheme(dawn_id).isEmpty());
    REQUIRE(QTest::qWaitFor([&] { return bg("kind") == QStringLiteral("image") && !bg("image").isEmpty(); },
                            10000));
    const auto copied = bg("image");
    CHECK(facade.media()->images()->rowCount() == 1);
    // A cut: the theme had no fade.
    CHECK(bg("transition_type") == QStringLiteral("cut"));

    // The theme goes; the slide keeps its picture, which is the library's.
    REQUIRE(themes->remove(QStringLiteral("Main"), QStringLiteral("bg"), dawn_id).isEmpty());
    CHECK(bg("image") == copied);
    CHECK(std::filesystem::exists(facade.open_library().media_root() / "images" / copied.toStdString()));

    // Nothing of a theme's shows on the stage.
    OperatorUi ui(facade);
    const auto editor = open_slide_editor(ui);
    auto* stage = editor.window->findChild<QQuickItem*>(QStringLiteral("slideStage"));
    REQUIRE(stage != nullptr);
    for (QQuickItem* item : stage->findChildren<QQuickItem*>()) {
        const QString src = item->property("source").toUrl().toString();
        CHECK_FALSE((src.startsWith(QStringLiteral("image://themepreview/theme/")) && item->isVisible()));
    }
}

// Choosing "Color" for the slide background makes it a colour and offers the
// colour picker for it.
TEST_CASE("A colour slide background opens its colour picker", "[ui][freeform]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    const auto freeform = facade.freeformLibrary()->createFreeform(QStringLiteral("Bg"));
    REQUIRE(freeform > 0);
    REQUIRE(facade.slideEdit()->openFreeform(freeform));

    OperatorUi ui(facade);
    const auto editor = open_slide_editor(ui);
    const auto open_pickers = [window = editor.window] {
        int n = 0;
        const QString prefix = QStringLiteral("ColorPicker_QMLTYPE_");
        for (QObject* o : window->findChildren<QObject*>()) {
            if (o->property("hue").isValid() && o->property("open").toBool()
                && o->parent() != nullptr
                && QString::fromLatin1(o->parent()->metaObject()->className()).startsWith(prefix)) {
                ++n;
            }
        }
        return n;
    };
    REQUIRE(open_pickers() == 0);
    // Picked on the Design tab, whose swatch the picker opens under.
    REQUIRE(editor.state->setProperty("ribbonTab", QStringLiteral("design")));
    QTest::qWait(0);
    REQUIRE(QMetaObject::invokeMethod(editor.state, "applySlideBgKind",
                                      Q_ARG(QVariant, QStringLiteral("color"))));
    QTest::qWait(20);
    CHECK(facade.slideEdit()->slides()->background().value(QStringLiteral("kind")).toString()
          == QStringLiteral("color"));
    CHECK(open_pickers() == 1);
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("The library panel imports media in the background", "[ui][import]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    const auto src = dir.path / "clip.mp4";
    {
        std::ofstream out(src, std::ios::binary);
        out << "mp4";
    }

    OperatorUi ui(facade);
    QObject* panel = ui.find_component("LibraryPanel");
    REQUIRE(panel != nullptr);
    // The Videos tab, as its Import button would.
    REQUIRE(QMetaObject::invokeMethod(panel, "selectTab", Q_ARG(QVariant, 3)));
    QObject* videos = panel->property("activeTab").value<QObject*>();
    REQUIRE(videos != nullptr);
    REQUIRE(videos->property("isVideo").toBool());
    REQUIRE(QMetaObject::invokeMethod(
        videos, "importFiles",
        Q_ARG(QVariant, QStringList{QUrl::fromLocalFile(QString::fromStdString(src.string()))
                                        .toString()}),
        Q_ARG(QVariant, -1)));
    REQUIRE(facade.media()->importing());
    REQUIRE(facade.media()->browser(seventhslide::db::MediaKind::Video)->rowCount() == 0);

    REQUIRE(QTest::qWaitFor([&] { return !facade.media()->importing(); }, 10000));
    QTest::qWait(20);
    REQUIRE(facade.media()->browser(seventhslide::db::MediaKind::Video)->rowCount() == 1);
    INFO(ui.warnings());
    REQUIRE(ui.warnings().empty());
}

namespace {

/// Open the output editor on Main's ``kind`` themes and design its first
/// theme: the slide editor opens over it.
SlideEditor open_theme_design(SessionFacade& facade, OperatorUi& ui, const char* kind)
{
    const auto themes = facade.themes()->list(QStringLiteral("Main"), QString::fromLatin1(kind));
    REQUIRE_FALSE(themes.isEmpty());
    const auto theme_id = themes.front().toMap().value(QStringLiteral("id")).toString();
    QObject* dialog = ui.find_component("OutputEditDialog");
    REQUIRE(dialog != nullptr);
    QObject* state = qml_object(dialog, "OutputEditState");
    REQUIRE(state != nullptr);
    REQUIRE(QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, QStringLiteral("Main")),
                                      Q_ARG(QVariant, QString::fromLatin1(kind))));
    REQUIRE(QMetaObject::invokeMethod(state, "enterDesign", Q_ARG(QVariant, theme_id)));
    REQUIRE(facade.slideEdit()->active());
    // What is open: a text theme of whichever kind, or a background.
    REQUIRE(facade.slideEdit()->property("mode").toString()
            == (QString::fromLatin1(kind) == QStringLiteral("bg") ? QStringLiteral("bg")
                                                                   : QStringLiteral("text")));
    return open_slide_editor(ui);
}

}  // namespace

// The stage is the draft raster under chrome: the raster paints the design,
// each element at its own place in the stack, and no box on the stage draws a
// design's words or stand-ins over it, which would put them over everything.
TEST_CASE("A theme's stage draws only chrome over its draft", "[ui][design][paint_order]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    OperatorUi ui(facade);

    // No box on the stage draws words or a stand-in: what is open has none
    // in QML, in every context it is designed in.
    const auto check_chrome_only = [&](const char* kind, std::initializer_list<const char*> contexts) {
        const auto editor = open_theme_design(facade, ui, kind);
        // The editor is a window of its own, or an item in the operator's.
        QList<QQuickItem*> roots;
        if (auto* window = qobject_cast<QQuickWindow*>(editor.window)) {
            roots << window->contentItem();
        } else if (auto* item = qobject_cast<QQuickItem*>(editor.window)) {
            roots << item;
        } else {
            roots = editor.window->findChildren<QQuickItem*>(Qt::FindDirectChildrenOnly);
        }
        REQUIRE_FALSE(roots.isEmpty());
        for (const char* context : contexts) {
            INFO(context);
            facade.slideEdit()->setContext(QString::fromLatin1(context));
            REQUIRE(facade.slideEdit()->property("context").toString()
                    == QString::fromLatin1(context));
            QTest::qWait(0);
            int boxes = 0;
            QList<QQuickItem*> todo = roots;
            while (!todo.isEmpty()) {
                QQuickItem* item = todo.takeLast();
                todo.append(item->childItems());
                if (!QByteArray(item->metaObject()->className()).startsWith("StageBox_QMLTYPE_")) {
                    continue;
                }
                ++boxes;
                CHECK_FALSE(item->property("showContent").toBool());
                CHECK(item->property("contentHtml").toString().isEmpty());
            }
            CHECK(boxes > 0);
        }
    };
    SECTION("the lyrics") { check_chrome_only("song", {"song"}); }
    SECTION("the passage") { check_chrome_only("bible", {"bible"}); }
    SECTION("the media areas") { check_chrome_only("media", {"image", "video", "web", "live"}); }
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// The theme designer's "Choose image" for an Image element.
TEST_CASE("The theme designer's image picker sets the element's image", "[ui][design]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    OperatorUi ui(facade);
    const auto editor = open_theme_design(facade, ui, "song");
    auto* objects = facade.slideEdit()->objects();
    REQUIRE(objects->insert(QStringLiteral("image"), {{QStringLiteral("binding"), QStringLiteral("static")}})
                .isEmpty());

    // Until it has a file, the new image stands in on the stage as a "Pick an
    // image" box, painted in the draft at its own place, tinted when selected.
    const QString picked_id = objects->selection().value(QStringLiteral("id")).toString();
    const auto stand_in_text = [&] {
        for (const auto& so : facade.themes()->draft_frame().objects) {
            if (QString::fromStdString(so.id) == picked_id && so.text && !so.text->lines.empty()) {
                return QString::fromStdString(seventhslide::model::plain_text(so.text->lines[0]));
            }
        }
        return QString();
    };
    CHECK(stand_in_text() == QStringLiteral("Pick an image"));
    QVariant tint;
    REQUIRE(QMetaObject::invokeMethod(editor.state, "stageStandInTint", Q_RETURN_ARG(QVariant, tint),
                                      Q_ARG(QVariant, QVariant(objects->selection()))));
    CHECK(tint.value<QColor>() == QColor(QStringLiteral("#2200c8c8")));

    // The file, once in the library, goes onto the element it was chosen for.
    const QVariantMap pending{{QStringLiteral("use"), QStringLiteral("element")},
                              {QStringLiteral("objectId"), objects->selection().value(QStringLiteral("id"))}};
    REQUIRE(QMetaObject::invokeMethod(editor.state, "useImportedMedia", Q_ARG(QVariant, pending),
                                      Q_ARG(QVariant, QStringLiteral("logo.png"))));

    CHECK(objects->selection().value(QStringLiteral("filename")).toString() == QStringLiteral("logo.png"));
    CHECK(facade.slideEdit()->property("dirty").toBool());
    QVariant none;
    REQUIRE(QMetaObject::invokeMethod(editor.state, "stageStandInTint", Q_RETURN_ARG(QVariant, none),
                                      Q_ARG(QVariant, QVariant(objects->selection()))));
    CHECK(none.value<QColor>().alpha() == 0);
    CHECK(stand_in_text().isEmpty());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

namespace {

/// The library's first picture, as objects name it.
QString first_library_image(SessionFacade& facade)
{
    auto* images = facade.media()->images();
    return images->rowCount() > 0
               ? images->data(images->index(0), seventhslide::app::ImageListModel::FilenameRole).toString()
               : QString();
}

}  // namespace

// The Insert ribbon's Picture and Video choose a file from disk, as the
// slide background's Picture does, not from the library.
TEST_CASE("Insert Picture and Video choose a file, imported then placed", "[ui][import]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    const auto freeform = facade.freeformLibrary()->createFreeform(QStringLiteral("Pic"));
    REQUIRE(freeform > 0);
    REQUIRE(facade.slideEdit()->openFreeform(freeform));

    OperatorUi ui(facade);
    const auto editor = open_slide_editor(ui);
    QObject* chooser = editor.window->findChild<QObject*>(QStringLiteral("mediaFileDialog"));
    REQUIRE(chooser != nullptr);
    const auto count = [&] { return facade.slideEdit()->objects()->property("canvasObjects").toList().size(); };
    const auto before = count();
    QSignalSpy asked(editor.state, SIGNAL(mediaFileAsked(QString)));
    for (const char* kind : {"video", "image"}) {
        REQUIRE(QMetaObject::invokeMethod(editor.state, "insert", Q_ARG(QVariant, QString::fromLatin1(kind)),
                                          Q_ARG(QVariant, QVariant())));
        REQUIRE(QMetaObject::invokeMethod(chooser, "close"));
    }
    REQUIRE(asked.size() == 2);
    CHECK(asked.at(0).at(0).toString() == QStringLiteral("video"));
    CHECK(asked.at(1).at(0).toString() == QStringLiteral("object"));
    // Nothing is placed until a file is chosen.
    CHECK(count() == before);

    REQUIRE(QMetaObject::invokeMethod(editor.state, "importMediaFile",
                                      Q_ARG(QVariant, png_url(dir.path / "pic.png")),
                                      Q_ARG(QVariant, QStringLiteral("object"))));
    const auto sel = [&] { return facade.slideEdit()->objects()->selection(); };
    REQUIRE(QTest::qWaitFor([&] { return sel().value(QStringLiteral("type")).toString() == QStringLiteral("image"); },
                            10000));
    REQUIRE(facade.media()->images()->rowCount() == 1);
    CHECK(sel().value(QStringLiteral("filename")).toString() == first_library_image(facade));
    CHECK(count() == before + 1);
}

// A text theme's Picture asks for a file first, so a chooser cancelled
// leaves no empty element; the element is placed once the file is in the
// library. A Change… with nothing selected is refused before any copy.
TEST_CASE("A theme's inserted picture takes a file chosen from disk", "[ui][import][design]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    OperatorUi ui(facade);
    const auto editor = open_theme_design(facade, ui, "song");
    QObject* chooser = editor.window->findChild<QObject*>(QStringLiteral("mediaFileDialog"));
    REQUIRE(chooser != nullptr);
    auto* objects = facade.slideEdit()->objects();
    const auto count = [&] { return editor.state->property("canvasObjects").toList().size(); };
    const auto before = count();
    QSignalSpy asked(editor.state, SIGNAL(mediaFileAsked(QString)));
    REQUIRE(QMetaObject::invokeMethod(editor.state, "insert", Q_ARG(QVariant, QStringLiteral("image")),
                                      Q_ARG(QVariant, QVariant())));
    REQUIRE(QMetaObject::invokeMethod(chooser, "close"));
    REQUIRE(asked.size() == 1);
    CHECK(asked.at(0).at(0).toString() == QStringLiteral("object"));
    CHECK(count() == before);

    objects->clearSelection();
    REQUIRE(QMetaObject::invokeMethod(editor.state, "importMediaFile",
                                      Q_ARG(QVariant, png_url(dir.path / "stray.png")),
                                      Q_ARG(QVariant, QStringLiteral("element"))));
    CHECK_FALSE(facade.media()->importing());
    CHECK_FALSE(editor.state->property("statusMessage").toString().isEmpty());

    REQUIRE(QMetaObject::invokeMethod(editor.state, "importMediaFile",
                                      Q_ARG(QVariant, png_url(dir.path / "logo.png")),
                                      Q_ARG(QVariant, QStringLiteral("object"))));
    REQUIRE(QTest::qWaitFor([&] { return count() == before + 1; }, 10000));
    REQUIRE(facade.media()->images()->rowCount() == 1);
    CHECK(objects->selection().value(QStringLiteral("type")).toString() == QStringLiteral("image"));
    CHECK(objects->selection().value(QStringLiteral("filename")).toString() == first_library_image(facade));
}

// Imports under way at once each land: a slide background and a picture
// chosen while it copies.
TEST_CASE("Two imports under way at once both land", "[ui][import]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    const auto freeform = facade.freeformLibrary()->createFreeform(QStringLiteral("Both"));
    REQUIRE(freeform > 0);
    REQUIRE(facade.slideEdit()->openFreeform(freeform));

    OperatorUi ui(facade);
    const auto editor = open_slide_editor(ui);
    const auto bg = png_url(dir.path / "bg.png");
    const auto pic = png_url(dir.path / "pic.png");
    REQUIRE(QMetaObject::invokeMethod(editor.state, "importSlideBackground", Q_ARG(QVariant, bg),
                                      Q_ARG(QVariant, false)));
    REQUIRE(QMetaObject::invokeMethod(editor.state, "importMediaFile", Q_ARG(QVariant, pic),
                                      Q_ARG(QVariant, QStringLiteral("object"))));
    const auto bg_image = [&] {
        return facade.slideEdit()->slides()->background().value(QStringLiteral("image")).toString();
    };
    const auto sel = [&] { return facade.slideEdit()->objects()->selection(); };
    REQUIRE(QTest::qWaitFor([&] {
        return !bg_image().isEmpty() && sel().value(QStringLiteral("type")).toString() == QStringLiteral("image");
    }, 10000));
    REQUIRE(facade.media()->images()->rowCount() == 2);
    CHECK(bg_image() != sel().value(QStringLiteral("filename")).toString());
}

// A refused edit says why in the window's footer.
TEST_CASE("The slide editor shows why an edit was refused", "[ui][design]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    OperatorUi ui(facade);
    const auto editor = open_theme_design(facade, ui, "song");
    auto* objects = facade.slideEdit()->objects();
    REQUIRE(objects->insert(QStringLiteral("text")).isEmpty());
    REQUIRE(objects->setLocked(true).isEmpty());
    QObject* home = ribbon_home(editor.window);
    REQUIRE(home != nullptr);

    REQUIRE(QMetaObject::invokeMethod(home, "arrange", Q_ARG(QVariant, QStringLiteral("align")),
                                      Q_ARG(QVariant, int(CanvasAlignQml::Left))));
    CHECK(editor.state->property("errorText").toString() == QStringLiteral("selection is locked"));

    // The next edit that goes through clears it.
    REQUIRE(QMetaObject::invokeMethod(home, "arrange", Q_ARG(QVariant, QStringLiteral("lock")),
                                      Q_ARG(QVariant, false)));
    CHECK(editor.state->property("errorText").toString().isEmpty());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// The window follows the session by signal: the selection it shows is the
// session's, and the draft raster repaints after any edit of the elements.
TEST_CASE("The slide editor follows the session's selection and edits", "[ui][design]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    OperatorUi ui(facade);
    const auto editor = open_theme_design(facade, ui, "song");
    auto* objects = facade.slideEdit()->objects();
    REQUIRE(objects->insert(QStringLiteral("shape")).isEmpty());
    const auto id = objects->selection().value(QStringLiteral("id")).toString();
    const auto shown = [&] { return editor.state->property("sel").toMap().value(QStringLiteral("id")).toString(); };
    CHECK(shown() == id);

    objects->clearSelection();
    CHECK(shown().isEmpty());
    CHECK_FALSE(editor.state->property("hasSel").toBool());
    objects->selectObject(id);
    CHECK(shown() == id);

    const int before = facade.themes()->draftRevision();
    REQUIRE(QMetaObject::invokeMethod(editor.state, "setProp", Q_ARG(QVariant, QStringLiteral("opacity")),
                                      Q_ARG(QVariant, 0.5)));
    CHECK(facade.themes()->draftRevision() > before);
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// The window's controls act on the selection as a whole: an edit keeps a
// selection of several, and Delete removes all of it.
TEST_CASE("The slide editor's buttons act on the whole selection", "[ui][design]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    OperatorUi ui(facade);
    const auto editor = open_theme_design(facade, ui, "song");
    auto* objects = facade.slideEdit()->objects();
    const auto before = objects->canvasObjects().size();
    REQUIRE(objects->insert(QStringLiteral("shape")).isEmpty());
    const auto first = objects->selection().value(QStringLiteral("id")).toString();
    REQUIRE(objects->insert(QStringLiteral("shape")).isEmpty());
    objects->selectObject(first, true);
    REQUIRE(objects->selectedIds().size() == 2);

    REQUIRE(QMetaObject::invokeMethod(editor.state, "setProp", Q_ARG(QVariant, QStringLiteral("opacity")),
                                      Q_ARG(QVariant, 0.5)));
    CHECK(objects->selectedIds().size() == 2);

    REQUIRE(QMetaObject::invokeMethod(editor.state, "deleteSelected"));
    CHECK(objects->canvasObjects().size() == before);
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// The editor takes the canvas shortcuts (CanvasEditorKeys.qml) on a theme
// and a Freeform item alike, driven here by real key presses.
TEST_CASE("The slide editor takes the canvas keyboard shortcuts", "[ui][design][keys]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));

    SECTION("a theme: clipboard, stacking, nudge and delete")
    {
        OperatorUi ui(facade);
        const auto editor = open_theme_design(facade, ui, "song");
        auto* objects = facade.slideEdit()->objects();
        const auto before = objects->canvasObjects().size();
        REQUIRE(objects->insert(QStringLiteral("shape")).isEmpty());
        const auto shape = objects->selection().value(QStringLiteral("id")).toString();
        REQUIRE(QMetaObject::invokeMethod(editor.window, "forceActiveFocus"));

        QTest::keyClick(ui.window(), Qt::Key_C, Qt::ControlModifier);
        QTest::keyClick(ui.window(), Qt::Key_V, Qt::ControlModifier);
        REQUIRE(objects->canvasObjects().size() == before + 2);
        const auto copy = objects->selection().value(QStringLiteral("id")).toString();
        CHECK(copy != shape);

        // The copy is on top; Ctrl+Shift+[ sends it to the back.
        QTest::keyClick(ui.window(), Qt::Key_BracketLeft,
                        Qt::ControlModifier | Qt::ShiftModifier);
        CHECK(objects->canvasObjects().front().toMap().value(QStringLiteral("id")).toString() == copy);

        const double x = objects->canvasObjects().front().toMap().value(QStringLiteral("x")).toDouble();
        QTest::keyClick(ui.window(), Qt::Key_Right, Qt::ShiftModifier);
        CHECK(objects->canvasObjects().front().toMap().value(QStringLiteral("x")).toDouble() == x + 10);

        QTest::keyClick(ui.window(), Qt::Key_Backspace);
        CHECK(objects->canvasObjects().size() == before + 1);
        CHECK(objects->selectedIds().isEmpty());
        INFO(ui.warnings());
        CHECK(ui.warnings().empty());
    }
    SECTION("a Freeform item: copy, paste, delete and undo")
    {
        const auto freeform = facade.freeformLibrary()->createFreeform(QStringLiteral("Keys"));
        REQUIRE(freeform > 0);
        REQUIRE(facade.slideEdit()->openFreeform(freeform));
        auto* objects = facade.slideEdit()->objects();
        OperatorUi ui(facade);
        const auto editor = open_slide_editor(ui);
        REQUIRE(objects->insert(QStringLiteral("text")).isEmpty());
        REQUIRE(QMetaObject::invokeMethod(editor.window, "forceActiveFocus"));

        QTest::keyClick(ui.window(), Qt::Key_C, Qt::ControlModifier);
        CHECK(editor.state->property("statusMessage").toString() == QStringLiteral("Copied to clipboard"));
        QTest::keyClick(ui.window(), Qt::Key_V, Qt::ControlModifier);
        CHECK(objects->canvasObjects().size() == 2);

        QTest::keyClick(ui.window(), Qt::Key_Backspace);
        CHECK(objects->canvasObjects().size() == 1);
        QTest::keyClick(ui.window(), Qt::Key_Z, Qt::ControlModifier);
        CHECK(objects->canvasObjects().size() == 2);
        INFO(ui.warnings());
        CHECK(ui.warnings().empty());
    }
}

// The Arrange menus hold CanvasAlign / CanvasFlip / CanvasOrder values;
// RibbonHome hands them to the editor as the numbers it takes.
TEST_CASE("The Arrange menus reach the editor as enum values", "[ui][design][arrange]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    const auto freeform = facade.freeformLibrary()->createFreeform(QStringLiteral("Arrange"));
    REQUIRE(freeform > 0);
    auto* edit = facade.slideEdit();
    REQUIRE(edit->openFreeform(freeform));
    OperatorUi ui(facade);
    const auto editor = open_slide_editor(ui);
    QObject* home = ribbon_home(editor.window);
    REQUIRE(home != nullptr);
    auto* objects = edit->objects();
    REQUIRE(objects->insert(QStringLiteral("text")).isEmpty());

    const auto arrange = [&](const char* action, const QVariant& value) {
        return QMetaObject::invokeMethod(home, "arrange", Q_ARG(QVariant, QString::fromLatin1(action)),
                                         Q_ARG(QVariant, value));
    };
    const auto object = [&] { return objects->canvasObjects().front().toMap(); };

    REQUIRE(arrange("align", int(CanvasAlignQml::Right)));
    CHECK(object().value(QStringLiteral("x")).toDouble() + object().value(QStringLiteral("width")).toDouble()
          == Catch::Approx(edit->canvasWidth()));
    REQUIRE(arrange("flip", int(CanvasFlipQml::Horizontal)));
    CHECK(object().value(QStringLiteral("flip_h")).toBool());
    REQUIRE(objects->insert(QStringLiteral("text")).isEmpty());
    const auto top = objects->selectedIds().front();
    REQUIRE(arrange("order", int(CanvasOrderQml::Back)));
    CHECK(object().value(QStringLiteral("id")).toString() == top);
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// A background theme's picture: the copy runs in the background and the
// draft picks the new file up when it lands.
TEST_CASE("The theme designer takes a background upload into the draft", "[ui][import]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    OperatorUi ui(facade);
    const auto editor = open_theme_design(facade, ui, "bg");
    auto* theme = facade.slideEdit()->theme();
    const auto draft_image = [&] {
        return theme->settings().value(QStringLiteral("background_image")).toString();
    };
    const auto before = draft_image();

    theme->uploadAsset(QStringLiteral("bg"), png_url(dir.path / "bg.png"));
    REQUIRE(facade.themes()->copyingAsset());
    REQUIRE(draft_image() == before);

    REQUIRE(QTest::qWaitFor([&] { return draft_image() != before; }, 10000));
    CHECK_FALSE(draft_image().isEmpty());
    CHECK(facade.slideEdit()->property("dirty").toBool());
    CHECK(editor.state->property("errorText").toString().isEmpty());
    INFO(ui.warnings());
    REQUIRE(ui.warnings().empty());

    // Only the draft has it: the saved theme is as it was until saved, and a
    // draft let go takes its copy with it.
    const auto theme_id = theme->themeId();
    const auto saved_image = [&] {
        return facade.themes()->theme_map(QStringLiteral("Main"), QStringLiteral("bg"), theme_id)
            .value(QStringLiteral("style")).toMap().value(QStringLiteral("background_image")).toString();
    };
    CHECK(saved_image() == before);
    const auto copy = facade.media_root()
                      / draft_image().mid(QStringLiteral("/static/").size()).toStdString();
    REQUIRE(std::filesystem::exists(copy));
    facade.slideEdit()->cancel();
    CHECK(saved_image() == before);
    CHECK_FALSE(std::filesystem::exists(copy));

    // Saved, it is the theme's.
    REQUIRE(facade.slideEdit()->openTheme(QStringLiteral("Main"), QStringLiteral("bg"), theme_id));
    theme->uploadAsset(QStringLiteral("bg"), png_url(dir.path / "bg.png"));
    REQUIRE(QTest::qWaitFor([&] { return draft_image() != before; }, 10000));
    const auto kept = draft_image();
    REQUIRE(facade.slideEdit()->save().isEmpty());
    CHECK(saved_image() == kept);
    const auto kept_file = facade.media_root() / kept.mid(QStringLiteral("/static/").size()).toStdString();
    CHECK(std::filesystem::exists(kept_file));

    // A copy still coming when the editor closes is not left behind.
    const auto files_here = [&] {
        return std::distance(std::filesystem::directory_iterator(kept_file.parent_path()),
                             std::filesystem::directory_iterator());
    };
    const auto files_before = files_here();
    REQUIRE(facade.slideEdit()->openTheme(QStringLiteral("Main"), QStringLiteral("bg"), theme_id));
    QSignalSpy finished(facade.themes(), &seventhslide::app::ThemeController::assetUploadFinished);
    theme->uploadAsset(QStringLiteral("bg"), png_url(dir.path / "bg.png"));
    facade.slideEdit()->cancel();
    REQUIRE(finished.wait(10000));
    CHECK(saved_image() == kept);
    CHECK(files_here() == files_before);
}


// Settings' theme priority and its stacks reorder by arrows over their
// tables. These drive them with the mouse, as the operator does, and read the
// result from the session.
TEST_CASE("Settings: the arrows move the picked theme priority up and down", "[ui][settings]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    OperatorUi ui(facade);
    QObject* dialog = ui.find_component("AppSettingsDialog");
    REQUIRE(dialog != nullptr);
    REQUIRE(QMetaObject::invokeMethod(dialog, "openDialog"));
    QTest::qWait(50);

    auto* sheet = qobject_cast<QQuickItem*>(dialog);
    QQuickItem* up = visible_button_under(sheet, QStringLiteral("priorityMoveUp"));
    QQuickItem* down = visible_button_under(sheet, QStringLiteral("priorityMoveDown"));
    REQUIRE(up != nullptr);
    REQUIRE(down != nullptr);
    // None picked: the first is shown picked, and can only go down.
    CHECK_FALSE(up->isEnabled());
    CHECK(down->isEnabled());

    // Send the first to the end, where it can go no further.
    click(ui.window(), centre_of(down));
    click(ui.window(), centre_of(down));
    CHECK_FALSE(down->isEnabled());
    CHECK(up->isEnabled());

    // Pick Global, now in the middle, and move it down one.
    QQuickItem* global = visible_text_under(sheet, QStringLiteral("Global"));
    REQUIRE(global != nullptr);
    click(ui.window(), centre_of(global));
    QObject* general = qml_object(dialog, "SettingsGeneralState");
    REQUIRE(general != nullptr);
    CHECK(general->property("priority").toStringList()
          == QStringList{QStringLiteral("service"), QStringLiteral("global"),
                         QStringLiteral("content")});
    click(ui.window(), centre_of(down));

    REQUIRE(QMetaObject::invokeMethod(dialog, "save"));
    const auto order = facade.getAppSettings().value(QStringLiteral("themePriority")).toStringList();
    CHECK(order == QStringList{QStringLiteral("service"), QStringLiteral("content"),
                               QStringLiteral("global")});
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Settings: the arrows move the picked stack up and down", "[ui][settings]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    const auto names = [&facade] {
        QStringList out;
        for (const QVariant& s : facade.stacks()->stacks()) {
            out.push_back(s.toMap().value(QStringLiteral("name")).toString());
        }
        return out;
    };
    REQUIRE(facade.stacks()->addStack(QStringLiteral("Lower thirds")).isEmpty());
    REQUIRE(facade.stacks()->addStack(QStringLiteral("Logos")).isEmpty());
    // Core lists stacks back to front; the panel lists them front to back.
    const QStringList before = names();
    REQUIRE(before.size() == 3);

    OperatorUi ui(facade);
    QObject* dialog = ui.find_component("AppSettingsDialog");
    REQUIRE(dialog != nullptr);
    REQUIRE(QMetaObject::invokeMethod(dialog, "openDialog"));
    REQUIRE(dialog->setProperty("tab", QStringLiteral("stacks")));
    QTest::qWait(50);

    auto* sheet = qobject_cast<QQuickItem*>(dialog);
    QQuickItem* up = visible_button_under(sheet, QStringLiteral("stackMoveUp"));
    QQuickItem* down = visible_button_under(sheet, QStringLiteral("stackMoveDown"));
    REQUIRE(up != nullptr);
    REQUIRE(down != nullptr);
    // None picked: the front one is shown picked, and can only go back.
    CHECK_FALSE(up->isEnabled());
    CHECK(down->isEnabled());

    // Pick the middle row (in the sheet: the main window shows stack names
    // too) and send it to the back, where it can go no further.
    QQuickItem* middle = visible_text_under(sheet, before.at(1));
    REQUIRE(middle != nullptr);
    click(ui.window(), centre_of(middle));
    click(ui.window(), centre_of(down));
    QStringList expected = before;
    expected.move(1, 0);
    CHECK(names() == expected);
    CHECK_FALSE(down->isEnabled());

    // Then all the way to the front.
    click(ui.window(), centre_of(up));
    click(ui.window(), centre_of(up));
    expected.move(0, 2);
    CHECK(names() == expected);
    CHECK_FALSE(up->isEnabled());
    CHECK(down->isEnabled());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Settings: a stack's Preview is switched in its row, and asks before dropping a cue",
          "[ui][settings][preview]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    const auto main = facade.stacks()->stacks().front().toMap().value(QStringLiteral("id"))
                          .toString().toStdString();
    OperatorUi ui(facade);
    QObject* dialog = ui.find_component("AppSettingsDialog");
    REQUIRE(dialog != nullptr);
    REQUIRE(QMetaObject::invokeMethod(dialog, "openDialog"));
    REQUIRE(dialog->setProperty("tab", QStringLiteral("stacks")));
    QTest::qWait(50);

    auto* sheet = qobject_cast<QQuickItem*>(dialog);
    const auto toggle = [sheet] {
        for (QQuickItem* item : seventhslide::test::visible_items_of(sheet, "QQuickItem")) {
            if (item->objectName() == QStringLiteral("stackPreview")) {
                return item;
            }
        }
        return static_cast<QQuickItem*>(nullptr);
    };
    REQUIRE(toggle() != nullptr);
    CHECK_FALSE(toggle()->property("checked").toBool());
    click(ui.window(), centre_of(toggle()));
    CHECK(facade.session().preview_stack_ids().contains(main));
    REQUIRE(toggle() != nullptr);
    CHECK(toggle()->property("checked").toBool());

    // Nothing cued: off at once.
    click(ui.window(), centre_of(toggle()));
    CHECK(facade.session().preview_stack_ids().empty());
    click(ui.window(), centre_of(toggle()));
    REQUIRE(facade.session().preview_stack_ids().contains(main));

    // Something cued: asked first, and nothing goes until the operator says
    // so.
    seventhslide::model::Song song;
    song.title = "Be Thou My Vision";
    song.lyrics = "[V1]\nBe thou my vision";
    song.verse_order = "v1";
    facade.session().play_song(song, main);
    REQUIRE(facade.stacks()->previewCued(QString::fromStdString(main)));
    QObject* confirm = nullptr;
    for (QObject* child : dialog->findChildren<QObject*>()) {
        if (seventhslide::test::is_qml_type(child, "AppDialog")
            && child->property("title").toString() == QStringLiteral("Turn Preview off")) {
            confirm = child;
        }
    }
    REQUIRE(confirm != nullptr);
    click(ui.window(), centre_of(toggle()));
    CHECK(confirm->property("open").toBool());
    CHECK(facade.session().preview_stack_ids().contains(main));
    REQUIRE(QMetaObject::invokeMethod(confirm, "submit"));
    CHECK(facade.session().preview_stack_ids().empty());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// The song editor's state is its view-model: edits made there, through the
// raw and guided views, are what Save writes.
TEST_CASE("The song editor saves edits made in both lyric views", "[ui][song-edit]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    seventhslide::model::Song song;
    song.title = "Amazing Grace";
    song.lyrics = "[V1]\nAmazing grace\nHow sweet the sound";
    song.verse_order = "v1";
    const auto id = facade.song_repo()->insert(song);

    OperatorUi ui(facade);
    QObject* dialog = ui.find_component("SongEditDialog");
    REQUIRE(dialog != nullptr);
    REQUIRE(QMetaObject::invokeMethod(dialog, "openLibrary",
                                      Q_ARG(QVariant, QVariant::fromValue<qint64>(id))));
    QObject* state = qml_object(dialog, "SongEditState");
    REQUIRE(state != nullptr);
    QTest::qWait(20);

    REQUIRE(state->setProperty("titleText", QStringLiteral("Amazing Grace (2)")));
    REQUIRE(QMetaObject::invokeMethod(state, "setLyricsMode", Q_ARG(QVariant, QStringLiteral("raw"))));
    REQUIRE(state->setProperty("rawLyrics", QStringLiteral(
        "---[Verse:1]---\nAmazing grace\nHow sweet the sound\n"
        "---[Chorus:1]---\nMy chains are gone")));
    REQUIRE(state->setProperty("rawOrder", QStringLiteral("v1 c1 v1")));
    REQUIRE(QMetaObject::invokeMethod(state, "setLyricsMode", Q_ARG(QVariant, QStringLiteral("guided"))));
    QTest::qWait(20);
    REQUIRE(state->property("verses").toList().size() == 2);
    REQUIRE(QMetaObject::invokeMethod(dialog, "save"));
    INFO("error: " << state->property("errorText").toString().toStdString());

    const auto saved = facade.library()->getSong(id);
    CHECK(saved.value(QStringLiteral("title")).toString() == QStringLiteral("Amazing Grace (2)"));
    CHECK(saved.value(QStringLiteral("lyrics")).toString().contains(QStringLiteral("My chains are gone")));
    CHECK(saved.value(QStringLiteral("verseOrder")).toString().toLower()
          == QStringLiteral("v1 c1 v1"));
    CHECK_FALSE(dialog->property("open").toBool());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// A click outside, Escape or Cancel closes a window at once when nothing is
// unsaved, and asks first when something is: in the song editor, Settings and
// Service Settings, as in the slide editor.
TEST_CASE("Closing an editor with unsaved changes asks first", "[ui][song-edit][settings]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    seventhslide::model::Song song;
    song.title = "Amazing Grace";
    song.lyrics = "---[Verse:1]---\nAmazing grace\nHow sweet the sound";
    song.verse_order = "v1";
    const auto song_id = facade.song_repo()->insert(song);
    const qint64 service_id = facade.library()->createService(QStringLiteral("Sunday"));
    REQUIRE(service_id > 0);
    OperatorUi ui(facade);

    const auto invoke = [](QObject* o, const char* method, const QVariant& arg = {}) {
        if (arg.isValid()) {
            REQUIRE(QMetaObject::invokeMethod(o, method, Q_ARG(QVariant, arg)));
        } else {
            REQUIRE(QMetaObject::invokeMethod(o, method));
        }
        QTest::qWait(20);
    };
    const auto is_open = [](QObject* o) { return o->property("open").toBool(); };
    /// ``window``'s own child of QML type ``type`` (its click-away scrim, its
    /// shell), not one of a dialog's inside it.
    const auto own = [](QObject* window, const char* type) -> QObject* {
        for (QObject* child : window->children()) {
            if (QByteArray(child->metaObject()->className()).startsWith(type)) {
                return child;
            }
        }
        return nullptr;
    };
    /// Open, cancel unchanged (closes); open, change, then each way of
    /// closing (Escape, a click outside, Cancel) asks; the confirm's Discard
    /// closes.
    const auto check = [&](QObject* window, const char* confirm_name,
                           const std::function<void()>& open, const std::function<void()>& change) {
        QObject* confirm = window->findChild<QObject*>(QString::fromLatin1(confirm_name));
        REQUIRE(confirm != nullptr);
        QObject* scrim = own(window, "ModalScrim_QMLTYPE_");
        QObject* shell = own(window, "WindowShell_QMLTYPE_");
        REQUIRE(scrim != nullptr);
        REQUIRE(shell != nullptr);
        open();
        REQUIRE(is_open(window));
        invoke(window, "requestCancel");
        CHECK_FALSE(is_open(window));
        CHECK_FALSE(is_open(confirm));

        open();
        change();
        const std::vector<std::pair<const char*, std::function<void()>>> ways{
            {"Escape", [&] { QTest::keyClick(ui.window(), Qt::Key_Escape); }},
            {"a click outside", [&] { REQUIRE(QMetaObject::invokeMethod(scrim, "clicked")); }},
            {"Cancel", [&] { REQUIRE(QMetaObject::invokeMethod(shell, "canceled")); }},
        };
        for (const auto& [way, close] : ways) {
            INFO(way);
            close();
            QTest::qWait(20);
            CHECK(is_open(window));
            CHECK(is_open(confirm));
            invoke(confirm, "close");
        }
        invoke(window, "requestCancel");
        REQUIRE(is_open(confirm));
        invoke(confirm, "submit");
        CHECK_FALSE(is_open(window));
    };

    SECTION("the song editor")
    {
        QObject* dialog = ui.find_component("SongEditDialog");
        REQUIRE(dialog != nullptr);
        QObject* state = qml_object(dialog, "SongEditState");
        REQUIRE(state != nullptr);
        check(
            dialog, "songDiscardConfirm",
            [&] { invoke(dialog, "openLibrary", QVariant::fromValue<qint64>(song_id)); },
            [&] { REQUIRE(state->setProperty("titleText", QStringLiteral("Amazing Grace (2)"))); });
        // Discarded: the song is as it was.
        CHECK(facade.library()->getSong(song_id).value(QStringLiteral("title")).toString()
              == QStringLiteral("Amazing Grace"));
    }

    SECTION("Settings")
    {
        QObject* dialog = ui.find_component("AppSettingsDialog");
        REQUIRE(dialog != nullptr);
        QObject* general = qml_object(dialog, "SettingsGeneralState");
        REQUIRE(general != nullptr);
        check(
            dialog, "settingsDiscardConfirm", [&] { invoke(dialog, "openDialog"); },
            [&] { REQUIRE(general->setProperty("organizationName", QStringLiteral("St Mark's"))); });
    }

    SECTION("Service Settings")
    {
        QObject* dialog = ui.find_component("ServiceSettingsDialog");
        REQUIRE(dialog != nullptr);
        QObject* general = qml_object(dialog, "ServiceGeneralState");
        REQUIRE(general != nullptr);
        check(
            dialog, "serviceDiscardConfirm",
            [&] {
                REQUIRE(QMetaObject::invokeMethod(dialog, "openFor",
                                                  Q_ARG(QVariant, QVariant::fromValue<qint64>(service_id)),
                                                  Q_ARG(QVariant, false)));
                QTest::qWait(20);
            },
            [&] { REQUIRE(general->setProperty("draftName", QStringLiteral("Sunday Evening"))); });
    }
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// The verse editor scrolls by its words, not by the pane it fills: a short
// verse shows no scrollbar, a long one does, and going back to the short one
// lets it go. (Scrolled by the editor's full height, the bar stayed for good.)
TEST_CASE("The song editor's verse pane shows its scrollbar only for a long verse",
          "[ui][song-edit]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    seventhslide::model::Song song;
    song.title = "Long and Short";
    song.lyrics = "---[Verse:1]---\nA short verse\n---[Verse:2]---\n";
    for (int line = 1; line <= 60; ++line) {
        song.lyrics += "Line " + std::to_string(line) + "\n";
    }
    song.verse_order = "v1 v2";
    const auto id = facade.song_repo()->insert(song);

    OperatorUi ui(facade);
    QObject* dialog = ui.find_component("SongEditDialog");
    REQUIRE(dialog != nullptr);
    REQUIRE(QMetaObject::invokeMethod(dialog, "openLibrary",
                                      Q_ARG(QVariant, QVariant::fromValue<qint64>(id))));
    QObject* state = qml_object(dialog, "SongEditState");
    REQUIRE(state != nullptr);
    QObject* pane = qml_object(dialog, "SongVerseEditor");
    REQUIRE(pane != nullptr);
    QObject* bar = qml_object(pane, "AppScrollBar");
    REQUIRE(bar != nullptr);
    QTest::qWait(50);

    const auto select = [&](int verse) {
        REQUIRE(QMetaObject::invokeMethod(state, "selectVerse", Q_ARG(QVariant, verse)));
        QTest::qWait(50);
    };
    select(0);
    CHECK_FALSE(bar->property("barShown").toBool());
    select(1);
    CHECK(bar->property("barShown").toBool());
    select(0);
    CHECK_FALSE(bar->property("barShown").toBool());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// Verse labels follow the song editor's rules (SongText): a new verse takes the
// next free number, a number already in use swaps the two verses, and a type
// change onto a used label moves to the next free number. The play order
// follows each move.
TEST_CASE("The song editor renumbers and retypes verses", "[ui][song-edit]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    seventhslide::model::Song song;
    song.title = "Numbers";
    song.lyrics = "---[Verse:1]---\nOne\n---[Verse:2]---\nTwo\n---[Chorus:1]---\nChorus";
    song.verse_order = "v1 v2 c1";
    const auto id = facade.song_repo()->insert(song);

    OperatorUi ui(facade);
    QObject* dialog = ui.find_component("SongEditDialog");
    REQUIRE(dialog != nullptr);
    REQUIRE(QMetaObject::invokeMethod(dialog, "openLibrary",
                                      Q_ARG(QVariant, QVariant::fromValue<qint64>(id))));
    QObject* state = qml_object(dialog, "SongEditState");
    REQUIRE(state != nullptr);
    QTest::qWait(20);
    const auto labels = [&] {
        QStringList out;
        for (const auto& v : state->property("verses").toList()) {
            out.push_back(v.toMap().value(QStringLiteral("label")).toString());
        }
        return out;
    };
    const auto order = [&] { return state->property("orderChips").toStringList().join(u' '); };
    const auto call = [&](const char* fn, const QVariant& arg) {
        return QMetaObject::invokeMethod(state, fn, Q_ARG(QVariant, arg));
    };
    REQUIRE(labels() == QStringList{QStringLiteral("Verse:1"), QStringLiteral("Verse:2"),
                                    QStringLiteral("Chorus:1")});

    REQUIRE(QMetaObject::invokeMethod(state, "addVerse"));
    CHECK(labels().last() == QStringLiteral("Verse:3"));

    REQUIRE(call("selectVerse", 0));
    REQUIRE(call("setSelectedVerseNumber", 2));
    CHECK(labels().mid(0, 2) == QStringList{QStringLiteral("Verse:2"), QStringLiteral("Verse:1")});
    CHECK(order() == QStringLiteral("V2 V1 C1"));

    REQUIRE(call("selectVerse", 2));
    REQUIRE(call("setSelectedVerseType", 0));
    CHECK(labels().at(2) == QStringLiteral("Verse:4"));
    CHECK(order() == QStringLiteral("V2 V1 V4"));
    CHECK(state->property("selectedTypeIndex").toInt() == 0);
    CHECK(state->property("selectedVerseNumber").toInt() == 4);

    // The verse picker's number field says which numbers the type has.
    QVariant used;
    REQUIRE(QMetaObject::invokeMethod(state, "verseNumbersInUse", Q_RETURN_ARG(QVariant, used),
                                      Q_ARG(QVariant, QStringLiteral("Verse"))));
    CHECK(used.toList() == QVariantList{1, 2, 3});

    // A duplicate follows its verse, as the next free number of its type.
    REQUIRE(call("selectVerse", 0));
    REQUIRE(QMetaObject::invokeMethod(state, "duplicateSelectedVerse"));
    CHECK(labels().mid(0, 2) == QStringList{QStringLiteral("Verse:2"), QStringLiteral("Verse:5")});
    CHECK(state->property("selectedVerse").toInt() == 1);
    const auto verses = state->property("verses").toList();
    CHECK(verses.at(1).toMap().value(QStringLiteral("content"))
          == verses.at(0).toMap().value(QStringLiteral("content")));
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("The song editor's chord field and menus take Escape first", "[ui][song-edit]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    seventhslide::model::Song song;
    song.title = "Chords";
    song.lyrics = "---[Verse:1]---\nAmazing grace";
    const auto id = facade.song_repo()->insert(song);

    OperatorUi ui(facade);
    QObject* dialog = ui.find_component("SongEditDialog");
    REQUIRE(QMetaObject::invokeMethod(dialog, "openLibrary",
                                      Q_ARG(QVariant, QVariant::fromValue<qint64>(id))));
    QObject* state = qml_object(dialog, "SongEditState");
    REQUIRE(state != nullptr);
    QTest::qWait(30);
    QObject* editor = qml_object(dialog, "SongVerseEditor");
    REQUIRE(editor != nullptr);
    auto* text = editor->property("textEdit").value<QObject*>();
    REQUIRE(text != nullptr);

    // The chord field is always there, beside the words: Enter sets the
    // chord of the word the cursor is in and gives the words the keys back;
    // Escape in it puts its chord back before it leaves the editor.
    QObject* tools = qml_object(dialog, "SongVerseTools");
    REQUIRE(tools != nullptr);
    auto* chord = tools->property("chordInput").value<QObject*>();
    REQUIRE(chord != nullptr);
    CHECK_FALSE(text->property("readOnly").toBool());
    REQUIRE(text->setProperty("cursorPosition", 2));
    REQUIRE(QMetaObject::invokeMethod(chord, "focusInput"));
    REQUIRE(chord->setProperty("text", QStringLiteral("G")));
    QTest::keyClick(ui.window(), Qt::Key_Return);
    QTest::qWait(30);
    CHECK(text->property("activeFocus").toBool());
    CHECK(chord->property("text").toString() == QStringLiteral("G"));
    REQUIRE(QMetaObject::invokeMethod(chord, "focusInput"));
    REQUIRE(chord->setProperty("text", QStringLiteral("Am")));
    QTest::keyClick(ui.window(), Qt::Key_Escape);
    QTest::qWait(30);
    CHECK(chord->property("text").toString() == QStringLiteral("G"));
    CHECK(text->property("activeFocus").toBool());
    CHECK(dialog->property("open").toBool());

    // An open menu takes Escape too.
    QObject* menu = nullptr;
    for (QObject* o : dialog->findChildren<QObject*>()) {
        if (QByteArray(o->metaObject()->className()).startsWith("AppMenu_QMLTYPE_")) {
            menu = o;
            break;
        }
    }
    REQUIRE(menu != nullptr);
    REQUIRE(QMetaObject::invokeMethod(menu, "openAt"));
    QTest::qWait(30);
    REQUIRE(menu->property("visible").toBool());
    QTest::keyClick(ui.window(), Qt::Key_Escape);
    QTest::qWait(30);
    CHECK_FALSE(menu->property("visible").toBool());
    CHECK(dialog->property("open").toBool());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// The default arrangement is stored as SongText.defaultArrangementName in every
// language; a song with only that arrangement and no theme chips stores no
// theme map, and renaming it to the name it shows changes nothing.
TEST_CASE("The song editor keeps the default arrangement's stored name", "[ui][song-edit]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    seventhslide::model::Song song;
    song.title = "Plain";
    song.lyrics = "---[Verse:1]---\nOne";
    song.verse_order = "v1";
    const auto id = facade.song_repo()->insert(song);

    OperatorUi ui(facade);
    QObject* dialog = ui.find_component("SongEditDialog");
    REQUIRE(dialog != nullptr);
    REQUIRE(QMetaObject::invokeMethod(dialog, "openLibrary",
                                      Q_ARG(QVariant, QVariant::fromValue<qint64>(id))));
    QObject* state = qml_object(dialog, "SongEditState");
    REQUIRE(state != nullptr);
    QTest::qWait(20);
    const auto arrangement = [&] { return state->property("arrangements").toList().front().toMap(); };
    CHECK(arrangement().value(QStringLiteral("name")).toString()
          == QString::fromUtf8(seventhslide::theme::k_default_arrangement_name));

    // The prompt starts from the name shown; taken as it is, nothing changes.
    QSignalSpy asked(state, SIGNAL(arrangementNameAsked(QString,QString)));
    REQUIRE(QMetaObject::invokeMethod(state, "beginRenameArrangement"));
    REQUIRE(asked.size() == 1);
    REQUIRE(QMetaObject::invokeMethod(state, "submitArrangementName",
                                      Q_ARG(QString, asked.front().at(0).toString()),
                                      Q_ARG(QString, asked.front().at(1).toString())));
    CHECK(arrangement().value(QStringLiteral("name")).toString()
          == QString::fromUtf8(seventhslide::theme::k_default_arrangement_name));

    REQUIRE(QMetaObject::invokeMethod(dialog, "save"));
    INFO("error: " << state->property("errorText").toString().toStdString());
    CHECK(facade.song_repo()->get(id)->theme_map == nlohmann::json::object());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// One prompt names everything, and its button says what it does: Create for
// something new in the library, Add for something put into the service,
// Rename for a rename.
TEST_CASE("Every name prompt is the one prompt, its button saying what it does", "[ui][song-edit]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    seventhslide::model::Song song;
    song.title = "Plain";
    song.lyrics = "---[Verse:1]---\nOne";
    song.verse_order = "v1";
    const auto id = facade.song_repo()->insert(song);
    REQUIRE(facade.library()->createService(QStringLiteral("Sunday")) > 0);

    OperatorUi ui(facade);
    // The prompt showing: its title and its button.
    const auto shown = [&]() -> std::pair<QString, QString> {
        const QString prefix = QStringLiteral("AppPromptDialog_QMLTYPE_");
        for (QObject* o : ui.window()->findChildren<QObject*>()) {
            if (QString::fromLatin1(o->metaObject()->className()).startsWith(prefix)
                && o->property("open").toBool()) {
                const std::pair<QString, QString> out{o->property("title").toString(),
                                                      o->property("acceptLabel").toString()};
                REQUIRE(QMetaObject::invokeMethod(o, "close"));
                return out;
            }
        }
        return {};
    };
    using Shown = std::pair<QString, QString>;

    // A folder in the Library.
    QObject* panel = ui.find_component("LibraryPanel");
    REQUIRE(panel != nullptr);
    QObject* images = nullptr;
    const QString browser = QStringLiteral("MediaBrowser_QMLTYPE_");
    for (QObject* o : panel->findChildren<QObject*>()) {
        if (QString::fromLatin1(o->metaObject()->className()).startsWith(browser)
            && o->property("tabId").toString() == QStringLiteral("images")) {
            images = o;
        }
    }
    REQUIRE(images != nullptr);
    REQUIRE(QMetaObject::invokeMethod(images, "newFolder"));
    CHECK(shown() == Shown{QStringLiteral("New folder"), QStringLiteral("Create")});

    // A song saved as a new Freeform item: a prompt of its own type.
    REQUIRE(QMetaObject::invokeMethod(panel, "requestSaveAsFreeform", Q_ARG(int, static_cast<int>(id)),
                                      Q_ARG(QString, QStringLiteral("Plain (edit)"))));
    QObject* save_as = ui.built_component("SaveAsFreeformDialog");
    REQUIRE(save_as != nullptr);
    CHECK(save_as->property("open").toBool());
    CHECK(save_as->property("title").toString() == QStringLiteral("Save as Freeform"));
    CHECK(save_as->property("acceptLabel").toString() == QStringLiteral("Create"));
    REQUIRE(QMetaObject::invokeMethod(save_as, "close"));

    // A divider in the service.
    QObject* service = ui.find_component("ServicePanel");
    REQUIRE(service != nullptr);
    REQUIRE(QMetaObject::invokeMethod(service, "openDividerDialog", Q_ARG(QVariant, QStringLiteral("add"))));
    CHECK(shown() == Shown{QStringLiteral("Add divider"), QStringLiteral("Add")});
    REQUIRE(QMetaObject::invokeMethod(service, "openDividerDialog", Q_ARG(QVariant, QStringLiteral("rename"))));
    CHECK(shown() == Shown{QStringLiteral("Rename divider"), QStringLiteral("Rename")});

    // An arrangement of a song.
    QObject* dialog = ui.find_component("SongEditDialog");
    REQUIRE(dialog != nullptr);
    REQUIRE(QMetaObject::invokeMethod(dialog, "openLibrary",
                                      Q_ARG(QVariant, QVariant::fromValue<qint64>(id))));
    QObject* state = qml_object(dialog, "SongEditState");
    REQUIRE(state != nullptr);
    QTest::qWait(20);
    REQUIRE(QMetaObject::invokeMethod(state, "beginNewArrangement"));
    CHECK(shown() == Shown{QStringLiteral("New arrangement"), QStringLiteral("Create")});
    REQUIRE(QMetaObject::invokeMethod(state, "beginRenameArrangement"));
    CHECK(shown() == Shown{QStringLiteral("Rename arrangement"), QStringLiteral("Rename")});
    REQUIRE(QMetaObject::invokeMethod(dialog, "close"));
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// Credits load from the song, toggle as a set from the role menu, take free
// text once, show on the author's row, and save as they read.
TEST_CASE("The song editor credits its authors", "[ui][song-edit]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    seventhslide::model::Song song;
    song.title = "Credits";
    song.lyrics = "---[Verse:1]---\nOne";
    song.authors = {seventhslide::model::Author("John Newton", {{"words", "", ""}}),
                    seventhslide::model::Author("Anon")};
    const auto id = facade.song_repo()->insert(song);

    OperatorUi ui(facade);
    QObject* dialog = ui.find_component("SongEditDialog");
    REQUIRE(dialog != nullptr);
    REQUIRE(QMetaObject::invokeMethod(dialog, "openLibrary",
                                      Q_ARG(QVariant, QVariant::fromValue<qint64>(id))));
    QObject* state = qml_object(dialog, "SongEditState");
    REQUIRE(state != nullptr);
    QTest::qWait(20);
    const auto call = [&](const char* fn, const QVariant& arg) {
        return QMetaObject::invokeMethod(state, fn, Q_ARG(QVariant, arg));
    };
    auto* authors = qobject_cast<seventhslide::app::AuthorListModel*>(
        state->property("authorModel").value<QObject*>());
    REQUIRE(authors != nullptr);
    const auto selected_types = [&] {
        QStringList out;
        for (const auto& r : authors->credits_at(state->property("selectedAuthorIndex").toInt())) {
            const auto m = r.toMap();
            out.push_back(m.value(QStringLiteral("type")).toString() + QLatin1Char(':')
                          + m.value(QStringLiteral("label")).toString());
        }
        return out;
    };

    REQUIRE(state->setProperty("tab", QStringLiteral("authors")));
    REQUIRE(call("selectAuthor", 0));
    CHECK(selected_types() == QStringList{QStringLiteral("words:")});
    REQUIRE(call("toggleSelectedAuthorRole", QStringLiteral("music")));
    REQUIRE(call("addSelectedAuthorCustomRole", QStringLiteral(" Descant ")));
    REQUIRE(call("addSelectedAuthorCustomRole", QStringLiteral("Descant")));
    CHECK(selected_types() == QStringList{QStringLiteral("words:"), QStringLiteral("music:"),
                                          QStringLiteral("other:Descant")});
    REQUIRE(call("toggleSelectedAuthorRole", QStringLiteral("words")));
    CHECK(selected_types() == QStringList{QStringLiteral("music:"), QStringLiteral("other:Descant")});
    QTest::qWait(20);
    auto* root = qobject_cast<QQuickItem*>(ui.window()->contentItem());
    REQUIRE(root != nullptr);
    // The row's Credit cell says them.
    CHECK(visible_text_under(root, QStringLiteral("Music, Descant")) != nullptr);

    REQUIRE(call("selectAuthor", 1));
    CHECK(selected_types().isEmpty());

    REQUIRE(QMetaObject::invokeMethod(dialog, "save"));
    INFO("error: " << state->property("errorText").toString().toStdString());
    const auto saved = facade.song_repo()->get(id);
    REQUIRE(saved);
    REQUIRE(saved->authors.size() == 2);
    CHECK(saved->authors[0].name == "John Newton");
    CHECK(saved->authors[0].roles
          == std::vector<seventhslide::model::AuthorRole>{{"music", "", ""}, {"other", "Descant", ""}});
    CHECK(saved->authors[1].roles.empty());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// A song author's name, driven by real keys: a new author takes them at once,
// Return keeps the name, Escape leaves the field with it.
TEST_CASE("Names are edited in place with Return and Escape", "[ui][rename]")
{
    seventhslide::test::ensure_qt_application();
    const auto type_text = [](QWindow* window, const QString& text) {
        for (const QChar c : text) {
            QTest::keyClick(window, c.toLatin1());
        }
    };
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));

    SECTION("a song author")
    {
        seventhslide::model::Song song;
        song.title = "Rename";
        song.lyrics = "---[Verse:1]---\nOne";
        const auto id = facade.song_repo()->insert(song);
        OperatorUi ui(facade);
        QObject* dialog = ui.find_component("SongEditDialog");
        REQUIRE(dialog != nullptr);
        REQUIRE(QMetaObject::invokeMethod(dialog, "openLibrary",
                                          Q_ARG(QVariant, QVariant::fromValue<qint64>(id))));
        QObject* state = qml_object(dialog, "SongEditState");
        REQUIRE(state != nullptr);
        REQUIRE(state->setProperty("tab", QStringLiteral("authors")));
        QTest::qWait(20);
        // A new author opens with the cursor in its name.
        REQUIRE(QMetaObject::invokeMethod(state, "addAuthor"));
        QTest::qWait(20);
        type_text(ui.window(), QStringLiteral("Ada"));
        QTest::keyClick(ui.window(), Qt::Key_Return);
        auto* model = state->property("authorModel").value<QAbstractItemModel*>();
        REQUIRE(model != nullptr);
        CHECK(model->rowCount() == 1);
        CHECK(model->index(0, 0).data(Qt::UserRole + 1).toString() == QStringLiteral("Ada"));
        CHECK(state->property("selectedAuthorIndex").toInt() == 0);

        // Escape leaves the field with what was typed, as every field here does.
        REQUIRE(QMetaObject::invokeMethod(state, "addAuthor"));
        QTest::qWait(20);
        type_text(ui.window(), QStringLiteral("Bob"));
        QTest::keyClick(ui.window(), Qt::Key_Escape);
        CHECK(model->index(1, 0).data(Qt::UserRole + 1).toString() == QStringLiteral("Bob"));
        CHECK(dialog->property("open").toBool());

        // One added and left without a name goes when its row closes.
        REQUIRE(QMetaObject::invokeMethod(state, "addAuthor"));
        REQUIRE(QMetaObject::invokeMethod(state, "closeAuthor"));
        CHECK(model->rowCount() == 2);
        INFO(ui.warnings());
        CHECK(ui.warnings().empty());
    }
}

TEST_CASE("The song editor saves topics typed as chips, and comments", "[ui][song-edit]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    seventhslide::model::Song song;
    song.title = "O Come";
    song.lyrics = "---[Verse:1]---\nOne";
    song.topics = {"Christmas"};
    const auto id = facade.song_repo()->insert(song);

    OperatorUi ui(facade);
    QObject* dialog = ui.find_component("SongEditDialog");
    REQUIRE(QMetaObject::invokeMethod(dialog, "openLibrary",
                                      Q_ARG(QVariant, QVariant::fromValue<qint64>(id))));
    QObject* state = qml_object(dialog, "SongEditState");
    REQUIRE(state != nullptr);
    REQUIRE(state->setProperty("tab", QStringLiteral("topics")));
    QTest::qWait(20);
    QObject* field = qml_object(dialog, "TagField");
    REQUIRE(field != nullptr);
    REQUIRE(QMetaObject::invokeMethod(field, "focusInput"));
    // A comma or Enter adds what is typed; a repeat in another case does not.
    for (const char c : std::string("Advent,christmas")) {
        QTest::keyClick(ui.window(), c);
    }
    QTest::keyClick(ui.window(), Qt::Key_Return);
    for (const char c : std::string("Hope")) {
        QTest::keyClick(ui.window(), c);
    }
    QTest::keyClick(ui.window(), Qt::Key_Return);
    CHECK(state->property("topics").toStringList()
          == QStringList({QStringLiteral("Christmas"), QStringLiteral("Advent"),
                          QStringLiteral("Hope")}));
    // Backspace in the empty slot takes the last one off.
    QTest::keyClick(ui.window(), Qt::Key_Backspace);
    CHECK(state->property("topics").toStringList().size() == 2);

    REQUIRE(state->setProperty("commentsText", QStringLiteral("Choir sings v2.")));
    REQUIRE(QMetaObject::invokeMethod(dialog, "save"));
    INFO("error: " << state->property("errorText").toString().toStdString());
    const auto saved = facade.song_repo()->get(id);
    REQUIRE(saved);
    CHECK(saved->topics == std::vector<std::string>{"Christmas", "Advent"});
    CHECK(saved->comments == "Choir sings v2.");
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// Save takes what is still being typed: a field commits as its focus leaves,
// and the window's Save moves it off first.
TEST_CASE("The song editor saves a name or topic still being typed", "[ui][song-edit]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    seventhslide::model::Song song;
    song.title = "Typing";
    song.lyrics = "---[Verse:1]---\nOne";
    const auto id = facade.song_repo()->insert(song);
    const auto type_text = [](QWindow* window, const std::string& text) {
        for (const char c : text) {
            QTest::keyClick(window, c);
        }
    };

    OperatorUi ui(facade);
    QObject* dialog = ui.find_component("SongEditDialog");
    REQUIRE(QMetaObject::invokeMethod(dialog, "openLibrary",
                                      Q_ARG(QVariant, QVariant::fromValue<qint64>(id))));
    QObject* state = qml_object(dialog, "SongEditState");
    REQUIRE(state != nullptr);
    QObject* shell = qml_object(dialog, "WindowShell");
    REQUIRE(shell != nullptr);

    SECTION("an author's name")
    {
        REQUIRE(state->setProperty("tab", QStringLiteral("authors")));
        QTest::qWait(20);
        REQUIRE(QMetaObject::invokeMethod(state, "addAuthor"));
        QTest::qWait(20);
        type_text(ui.window(), "Ada");
        REQUIRE(QMetaObject::invokeMethod(shell, "accept"));
        INFO("error: " << state->property("errorText").toString().toStdString());
        const auto saved = facade.song_repo()->get(id);
        REQUIRE(saved);
        REQUIRE(saved->authors.size() == 1);
        CHECK(saved->authors[0].name == "Ada");
    }
    SECTION("a topic")
    {
        REQUIRE(state->setProperty("tab", QStringLiteral("topics")));
        QTest::qWait(20);
        QObject* field = qml_object(dialog, "TagField");
        REQUIRE(field != nullptr);
        REQUIRE(QMetaObject::invokeMethod(field, "focusInput"));
        type_text(ui.window(), "Hope");
        REQUIRE(QMetaObject::invokeMethod(shell, "accept"));
        INFO("error: " << state->property("errorText").toString().toStdString());
        const auto saved = facade.song_repo()->get(id);
        REQUIRE(saved);
        CHECK(saved->topics == std::vector<std::string>{"Hope"});
    }
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("The song editor saves authors and theme chips", "[ui][song-edit]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    seventhslide::model::Song song;
    song.title = "Amazing Grace";
    song.lyrics = "[V1]\nAmazing grace\nHow sweet the sound";
    song.verse_order = "v1";
    const auto id = facade.song_repo()->insert(song);
    const QString output = QString::fromStdString(facade.session().outputs().front().id);
    const auto themes = facade.themes()->list(QStringLiteral("Main"), QStringLiteral("song"));
    REQUIRE_FALSE(themes.isEmpty());
    const QString theme = themes.front().toMap().value(QStringLiteral("id")).toString();

    OperatorUi ui(facade);
    QObject* dialog = ui.find_component("SongEditDialog");
    REQUIRE(dialog != nullptr);
    REQUIRE(QMetaObject::invokeMethod(dialog, "openLibrary",
                                      Q_ARG(QVariant, QVariant::fromValue<qint64>(id))));
    QObject* state = qml_object(dialog, "SongEditState");
    REQUIRE(state != nullptr);
    QTest::qWait(20);

    REQUIRE(QMetaObject::invokeMethod(state, "addAuthor"));
    REQUIRE(QMetaObject::invokeMethod(state, "renameAuthor", Q_ARG(QVariant, 0),
                                      Q_ARG(QVariant, QStringLiteral("John Newton"))));
    REQUIRE(QMetaObject::invokeMethod(state, "addThemeChip"));
    QTest::qWait(20);
    auto* editor = state->property("themeMapEditor").value<QObject*>();
    REQUIRE(editor != nullptr);
    // As a slot does when its picker closes: the new chip's song theme.
    REQUIRE(QMetaObject::invokeMethod(editor, "setThemeId", Q_ARG(QVariant, output),
                                      Q_ARG(QVariant, QStringLiteral("song")),
                                      Q_ARG(QVariant, QStringLiteral("theme")),
                                      Q_ARG(QVariant, theme)));
    REQUIRE(QMetaObject::invokeMethod(dialog, "save"));
    INFO("error: " << state->property("errorText").toString().toStdString());

    const auto saved = facade.library()->getSong(id);
    const auto authors = saved.value(QStringLiteral("authors")).toList();
    REQUIRE(authors.size() == 1);
    CHECK(authors.front().toMap().value(QStringLiteral("name")).toString()
          == QStringLiteral("John Newton"));
    const auto map = saved.value(QStringLiteral("themeMap")).toMap();
    const auto arrangements = map.value(QStringLiteral("arrangements")).toList();
    REQUIRE(arrangements.size() == 1);
    const auto arrangement = arrangements.front().toMap();
    CHECK(arrangement.value(QStringLiteral("order")).toString().toUpper()
          == QStringLiteral("V1 TH1"));
    CHECK(arrangement.value(QStringLiteral("chips")).toMap().value(QStringLiteral("TH1")).toMap()
              .value(output).toMap().value(QStringLiteral("song")).toMap()
              .value(QStringLiteral("theme")).toString()
          == theme);
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// The play order is built by dragging: a verse row onto the order strip adds
// it, a chip dropped on another's left half moves it in front.
TEST_CASE("The song editor's play order follows verse and chip drags", "[ui][song-edit][drag]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    seventhslide::model::Song song;
    song.title = "Amazing Grace";
    song.lyrics = "---[Verse:1]---\nAmazing grace\n---[Chorus:1]---\nMy chains are gone";
    song.verse_order = "v1 c1";
    const auto id = facade.song_repo()->insert(song);

    OperatorUi ui(facade);
    QObject* dialog = ui.find_component("SongEditDialog");
    REQUIRE(dialog != nullptr);
    REQUIRE(QMetaObject::invokeMethod(dialog, "openLibrary",
                                      Q_ARG(QVariant, QVariant::fromValue<qint64>(id))));
    QObject* state = qml_object(dialog, "SongEditState");
    REQUIRE(state != nullptr);
    QTest::qWait(50);
    const auto order = [state] { return state->property("orderChips").toStringList(); };
    REQUIRE(order() == QStringList{"V1", "C1"});

    // The order strip sits above the verse list, so the first "V1"/"C1" in
    // the sheet are its chips; the rows also show their codes.
    auto* sheet = qobject_cast<QQuickItem*>(dialog);
    QQuickItem* chorusRow = visible_text_under(sheet, QStringLiteral("Chorus 1"));
    QQuickItem* firstChip = visible_text_under(sheet, QStringLiteral("V1"));
    REQUIRE(chorusRow != nullptr);
    REQUIRE(firstChip != nullptr);
    REQUIRE(centre_of(firstChip).y() < centre_of(chorusRow).y());
    drag(ui.window(), centre_of(chorusRow), centre_of(firstChip));
    CHECK(order() == QStringList{"V1", "C1", "C1"});
    CHECK_FALSE(state->property("verseDragActive").toBool());

    // The chips were rebuilt for the new order.
    firstChip = visible_text_under(sheet, QStringLiteral("V1"));
    QQuickItem* secondChip = visible_text_under(sheet, QStringLiteral("C1"));
    REQUIRE(firstChip != nullptr);
    REQUIRE(secondChip != nullptr);
    REQUIRE(centre_of(secondChip).y() == centre_of(firstChip).y());
    drag(ui.window(), centre_of(secondChip), centre_of(firstChip) - QPoint(6, 0));
    CHECK(order() == QStringList{"C1", "V1", "C1"});
    CHECK_FALSE(state->property("chipDragActive").toBool());

    REQUIRE(QMetaObject::invokeMethod(dialog, "save"));
    CHECK(facade.library()->getSong(id).value(QStringLiteral("verseOrder")).toString().toLower()
          == QStringLiteral("c1 v1 c1"));
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// Dragging a slide in the sorter moves it: dropped on the lower half of the
// last slide, the first slide becomes the last.
TEST_CASE("Dragging a slide in the Freeform sorter reorders the slides", "[ui][freeform][drag]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    const auto freeform = facade.freeformLibrary()->createFreeform(QStringLiteral("Sorter"));
    REQUIRE(freeform > 0);
    auto* edit = facade.slideEdit()->slides();
    REQUIRE(facade.slideEdit()->openFreeform(freeform));
    REQUIRE(edit->insert().isEmpty());
    REQUIRE(edit->insert().isEmpty());
    const auto slide_ids = [edit] {
        QStringList out;
        for (int i = 0; i < 3; ++i) {
            edit->select(i);
            out.push_back(edit->currentId());
        }
        return out;
    };
    const QStringList before = slide_ids();
    REQUIRE(before.size() == 3);
    REQUIRE(QSet<QString>(before.begin(), before.end()).size() == 3);

    OperatorUi ui(facade);
    QObject* dialog = ui.find_component("SlideEditorWindow");
    REQUIRE(dialog != nullptr);
    QTest::qWait(50);
    QObject* slides = edit->list();
    REQUIRE(slides != nullptr);
    QQuickItem* sorter = nullptr;
    for (QQuickItem* item : dialog->findChildren<QQuickItem*>()) {
        if (item->inherits("QQuickListView") && item->property("model").value<QObject*>() == slides) {
            sorter = item;
        }
    }
    REQUIRE(sorter != nullptr);
    const auto row = [sorter](int i) {
        QQuickItem* item = nullptr;
        REQUIRE(QMetaObject::invokeMethod(sorter, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, item),
                                          Q_ARG(int, i)));
        REQUIRE(item != nullptr);
        return item;
    };
    QQuickItem* first = row(0);
    QQuickItem* last = row(2);
    const QPoint lower_last =
        last->mapToScene(QPointF(last->width() / 2, last->height() * 0.8)).toPoint();
    drag(ui.window(), centre_of(first), lower_last);

    CHECK(slide_ids() == QStringList{before[1], before[2], before[0]});
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// The output editor's state asks for confirms; the dialog shows them. Each
// request reaches the piece of view that answers it.
TEST_CASE("The output editor shows what its state asks for", "[ui][output-edit]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    const auto themes = facade.themes()->list(QStringLiteral("Main"), QStringLiteral("song"));
    REQUIRE_FALSE(themes.isEmpty());
    const QString theme = themes.front().toMap().value(QStringLiteral("id")).toString();

    OperatorUi ui(facade);
    QObject* dialog = ui.find_component("OutputEditDialog");
    REQUIRE(dialog != nullptr);
    QObject* state = qml_object(dialog, "OutputEditState");
    REQUIRE(state != nullptr);
    const auto titled = [dialog](const QString& title) -> QObject* {
        for (QObject* o : dialog->findChildren<QObject*>()) {
            if (o->property("title").toString() == title && o->property("open").isValid()) {
                return o;
            }
        }
        return nullptr;
    };
    QObject* theme_confirm = titled(QStringLiteral("Delete theme"));
    REQUIRE(theme_confirm != nullptr);

    REQUIRE(QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, QStringLiteral("Main")),
                                      Q_ARG(QVariant, QStringLiteral("song"))));
    REQUIRE(state->setProperty("selectedId", theme));
    REQUIRE(QMetaObject::invokeMethod(state, "deleteSelected"));
    CHECK(theme_confirm->property("open").toBool());
    REQUIRE(theme_confirm->setProperty("open", false));
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// The theme pages' bar: Edit Design, Duplicate and Delete act on the theme
// selected, and wait for one.
TEST_CASE("The output editor's theme bar acts on the selected theme", "[ui][output-edit]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    const auto count = [&facade] {
        return facade.themes()->list(QStringLiteral("Main"), QStringLiteral("song")).size();
    };
    const auto before = count();
    REQUIRE(before > 0);
    const QString theme = facade.themes()->list(QStringLiteral("Main"), QStringLiteral("song"))
                              .front().toMap().value(QStringLiteral("id")).toString();

    OperatorUi ui(facade);
    QObject* dialog = ui.find_component("OutputEditDialog");
    REQUIRE(dialog != nullptr);
    QObject* state = qml_object(dialog, "OutputEditState");
    REQUIRE(state != nullptr);
    REQUIRE(QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, QStringLiteral("Main")),
                                      Q_ARG(QVariant, QStringLiteral("song"))));
    QTest::qWait(50);
    QObject* gallery = qml_object(dialog, "OutputThemeGallery");
    REQUIRE(gallery != nullptr);
    const auto button = [gallery](const QString& name) -> QQuickItem* {
        for (auto* item : gallery->findChildren<QQuickItem*>(name)) {
            if (item->isVisible()) {
                return item;
            }
        }
        FAIL("no button " << name.toStdString());
        return nullptr;
    };
    QQuickItem* edit = button(QStringLiteral("themeEditDesign"));
    QQuickItem* duplicate = button(QStringLiteral("themeDuplicate"));
    QQuickItem* remove = button(QStringLiteral("themeDelete"));

    // Nothing selected: nothing to act on.
    REQUIRE(state->setProperty("selectedId", QString()));
    QTest::qWait(20);
    CHECK_FALSE(edit->isEnabled());
    CHECK_FALSE(duplicate->isEnabled());
    CHECK_FALSE(remove->isEnabled());

    REQUIRE(state->setProperty("selectedId", theme));
    QTest::qWait(20);
    click(ui.window(), centre_of(duplicate));
    CHECK(count() == before + 1);

    REQUIRE(state->setProperty("selectedId", theme));
    QTest::qWait(20);
    click(ui.window(), centre_of(remove));
    QObject* confirm = nullptr;
    for (QObject* o : dialog->findChildren<QObject*>()) {
        if (o->property("title").toString() == QStringLiteral("Delete theme")
            && o->property("open").isValid()) {
            confirm = o;
        }
    }
    REQUIRE(confirm != nullptr);
    CHECK(confirm->property("open").toBool());
    REQUIRE(confirm->setProperty("open", false));

    click(ui.window(), centre_of(edit));
    CHECK(facade.slideEdit()->active());
    CHECK(facade.slideEdit()->property("mode").toString() == QStringLiteral("text"));
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// The editor splits lyrics with the parser that builds the slides, so a verse
// it shows is a verse that plays. Sections 1a / 1b of one verse are one verse
// whatever the case of their labels.
TEST_CASE("The song editor shows the verses that play", "[ui][song-edit]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    seventhslide::model::Song song;
    song.title = "Sections";
    song.lyrics = "---[Verse:1a]---\nOne\n---[verse:1b]---\nTwo\n---[Chorus:1]---\nThree";
    song.verse_order = "v1 c1";
    const auto id = facade.song_repo()->insert(song);

    OperatorUi ui(facade);
    QObject* dialog = ui.find_component("SongEditDialog");
    REQUIRE(dialog != nullptr);
    REQUIRE(QMetaObject::invokeMethod(dialog, "openLibrary",
                                      Q_ARG(QVariant, QVariant::fromValue<qint64>(id))));
    QObject* state = qml_object(dialog, "SongEditState");
    REQUIRE(state != nullptr);
    const auto verses = state->property("verses").toList();
    REQUIRE(verses.size() == 2);
    CHECK(verses[0].toMap().value(QStringLiteral("label")).toString() == QStringLiteral("Verse:1"));
    CHECK(verses[0].toMap().value(QStringLiteral("content")).toString()
          == QStringLiteral("One\n[---][---]\nTwo"));
    CHECK(state->property("orderChips").toStringList() == QStringList{"V1", "C1"});
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// Output cards reorder by dragging their title bar.
TEST_CASE("Dragging an output card reorders the outputs", "[ui][drag]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    const auto created = facade.outputSettings()->createOutput(QStringLiteral("Stage"));
    REQUIRE(created.value(QStringLiteral("error")).toString().isEmpty());
    const auto names = [&facade] {
        QStringList out;
        for (const auto& o : facade.session().outputs()) {
            out.push_back(QString::fromStdString(o.name));
        }
        return out;
    };
    const QStringList before = names();
    REQUIRE(before.size() == 2);

    OperatorUi ui(facade);
    auto* column = qobject_cast<QQuickItem*>(ui.find_component("OutputsPanel"));
    REQUIRE(column != nullptr);
    QTest::qWait(50);
    QQuickItem* first = visible_text_under(column, before[0]);
    QQuickItem* second = visible_text_under(column, before[1]);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    // Down past the middle of the second card: the first goes after it.
    drag(ui.window(), centre_of(first), centre_of(second) + QPoint(0, 120));
    CHECK(names() == (QStringList{before[1], before[0]}));
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// A preview has the shape of what it shows: a portrait output's card and the
// slide thumbnails for it are not a narrow strip in a 16:9 box.
TEST_CASE("Previews take their output's shape", "[ui][outputs]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    const auto created = facade.outputSettings()->createOutput(QStringLiteral("Stage"));
    REQUIRE(created.value(QStringLiteral("error")).toString().isEmpty());
    auto outputs = facade.session().outputs();
    REQUIRE(outputs.size() == 2);
    // The first is the primary: the one the slide thumbnails are drawn for.
    outputs[0].canvas_width = 1080;
    outputs[0].canvas_height = 1920;
    outputs[1].canvas_width = 1920;
    outputs[1].canvas_height = 1080;
    facade.set_outputs(outputs);
    const auto aspect_of = [&facade](const QString& id) {
        for (const auto& oc : facade.session().outputs()) {
            if (QString::fromStdString(oc.id) == id) {
                return static_cast<double>(oc.canvas_width) / oc.canvas_height;
            }
        }
        return 0.0;
    };

    OperatorUi ui(facade);
    auto* column = qobject_cast<QQuickItem*>(ui.find_component("OutputsPanel"));
    REQUIRE(column != nullptr);
    QTest::qWait(50);
    const auto previews =
        seventhslide::test::visible_items_of(column, "seventhslide::app::OutputPreviewItem");
    REQUIRE(previews.size() == 2);
    for (QQuickItem* preview : previews) {
        const auto id = preview->property("outputId").toString();
        INFO("output " << id.toStdString() << ": " << preview->width() << " x " << preview->height());
        REQUIRE(preview->height() > 0);
        CHECK(std::abs(preview->width() / preview->height() - aspect_of(id)) < 0.02);
        // No taller than the card is wide.
        CHECK(preview->height() <= preview->parentItem()->width() + 0.5);
    }

    // The slide gallery's cells, in the primary output's shape.
    QObject* gallery = nullptr;
    for (QObject* o : ui.find_component("ControllerColumn")->findChildren<QObject*>()) {
        if (o->property("slideAspect").isValid() && o->property("thumbH").isValid()) {
            gallery = o;
        }
    }
    REQUIRE(gallery != nullptr);
    const double thumb_w = gallery->property("thumbW").toDouble();
    const double thumb_h = gallery->property("thumbH").toDouble();
    REQUIRE(thumb_h > 0);
    CHECK(std::abs(thumb_w / thumb_h - 1080.0 / 1920.0) < 0.05);
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// The toolbar's Send to menu (StackSendMenu): the type default, then every
// stack; picking one arms it, and picking it again lets it go.
TEST_CASE("The Send to menu lists every stack and arms the one picked", "[ui][stacks]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    REQUIRE(facade.stacks()->addStack(QStringLiteral("Lobby")).isEmpty());
    QString lobby_id;
    for (const auto& stack : facade.session().stack_defs()) {
        if (stack.name == "Lobby") {
            lobby_id = QString::fromStdString(stack.id);
        }
    }
    REQUIRE_FALSE(lobby_id.isEmpty());

    OperatorUi ui(facade);
    auto* w = ui.window();
    auto* target = qobject_cast<QQuickItem*>(ui.find_component("StackTargetButton"));
    REQUIRE(target != nullptr);
    REQUIRE(target->isVisible());
    // A row of the open menu (in the popup overlay), not the stack strip's.
    const auto menu_row = [w](const QString& text) -> QQuickItem* {
        for (QQuickItem* item : w->contentItem()->findChildren<QQuickItem*>()) {
            if (item->inherits("QQuickOverlay")) {
                return seventhslide::test::visible_text_under(item, text);
            }
        }
        return nullptr;
    };

    click(w, centre_of(target));
    QTest::qWait(20);
    REQUIRE(menu_row(QStringLiteral("Send to")) != nullptr);
    CHECK(menu_row(QStringLiteral("Type default")) != nullptr);
    QQuickItem* lobby = menu_row(QStringLiteral("Lobby"));
    REQUIRE(lobby != nullptr);
    click(w, centre_of(lobby));
    QTest::qWait(20);
    CHECK(facade.stacks()->armedSendToId() == lobby_id);
    CHECK(menu_row(QStringLiteral("Send to")) == nullptr);  // it closed

    click(w, centre_of(target));
    QTest::qWait(20);
    lobby = menu_row(QStringLiteral("Lobby"));
    REQUIRE(lobby != nullptr);
    click(w, centre_of(lobby));
    QTest::qWait(20);
    CHECK(facade.stacks()->armedSendToId().isEmpty());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// The rundown's Send to menu names the stack a selected row's type plays on,
// and follows it when the operator points the type elsewhere.
TEST_CASE("The rundown's type default names the stack the type plays on now", "[ui][stacks]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    REQUIRE(facade.stacks()->addStack(QStringLiteral("Lobby")).isEmpty());
    QString lobby_id;
    for (const auto& stack : facade.session().stack_defs()) {
        if (stack.name == "Lobby") {
            lobby_id = QString::fromStdString(stack.id);
        }
    }
    REQUIRE_FALSE(lobby_id.isEmpty());
    seventhslide::model::Song song;
    song.title = "Alpha";
    song.lyrics = "[V1]\nOne";
    const auto song_id = facade.song_repo()->insert(song);
    facade.library()->refresh_songs();
    REQUIRE(facade.library()->createService(QStringLiteral("Sunday")) > 0);
    REQUIRE(facade.library()->addSongToService(static_cast<qint64>(song_id)));
    const auto item_id = facade.serviceItems()
                             ->data(facade.serviceItems()->index(0), seventhslide::app::ServiceItemModel::ItemIdRole)
                             .toLongLong();

    OperatorUi ui(facade);
    QObject* panel = ui.find_component("ServicePanel");
    REQUIRE(panel != nullptr);
    QObject* menu = seventhslide::test::OperatorUi::component_under(panel, "StackSendMenu");
    REQUIRE(menu != nullptr);
    REQUIRE(panel->setProperty("stackMenuIds", QVariantList{item_id}));
    const QString first = menu->property("defaultLabel").toString();
    CHECK(first.startsWith(QStringLiteral("Type default (")));
    CHECK_FALSE(first.contains(QStringLiteral("Lobby")));
    REQUIRE(facade.stacks()->setStackTypeDefault(QStringLiteral("song"), lobby_id).isEmpty());
    CHECK(menu->property("defaultLabel").toString() == QStringLiteral("Type default (Lobby)"));
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// The controller's ⋯ menu (an AppMenu): it opens under its button, a toggle
// in it changes the focused stack's auto-advance and leaves it open, and
// Escape closes it.
TEST_CASE("The controller's options menu opens and its toggles apply", "[ui][controller]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    OperatorUi ui(facade);
    auto* w = ui.window();
    QObject* menu = ui.find_component("ControllerOptionsMenu");
    REQUIRE(menu != nullptr);
    auto* button = menu->property("button").value<QQuickItem*>();
    REQUIRE(button != nullptr);
    const auto stack = facade.stacks()->focusedStackId();
    const auto loop = [&] {
        return facade.stacks()->autoAdvanceFor(stack).value(QStringLiteral("loop")).toBool();
    };
    REQUIRE_FALSE(loop());

    click(w, centre_of(button));
    QTest::qWait(20);
    REQUIRE(menu->property("visible").toBool());
    QQuickItem* row = seventhslide::test::visible_text_under(w->contentItem(), QStringLiteral("Loop"));
    REQUIRE(row != nullptr);
    click(w, centre_of(row));
    QTest::qWait(20);
    CHECK(loop());
    CHECK(menu->property("visible").toBool());  // a toggle leaves it open

    QTest::keyClick(w, Qt::Key_Escape);
    QTest::qWait(20);
    CHECK_FALSE(menu->property("visible").toBool());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// A flipped shape is mirrored in place on the Freeform stage: about its own
// centre, not somewhere else on the slide.
TEST_CASE("A flipped shape stays on its box on the Freeform stage", "[ui][freeform]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    const auto freeform = facade.freeformLibrary()->createFreeform(QStringLiteral("Flip"));
    REQUIRE(freeform > 0);
    auto* edit = facade.slideEdit();

    OperatorUi ui(facade);
    QObject* dialog = ui.find_component("SlideEditorWindow");
    REQUIRE(dialog != nullptr);
    REQUIRE(edit->openFreeform(freeform));
    REQUIRE(edit->objects()
                ->insert(QStringLiteral("shape"), {{QStringLiteral("shapeKind"), QStringLiteral("triangle")}})
                .isEmpty());
    REQUIRE(edit->objects()->flip(CanvasFlipQml::Horizontal).isEmpty());
    QTest::qWait(50);
    // The stage's object: the one shape item sized to a box, not a menu icon.
    QQuickItem* shape = nullptr;
    for (QQuickItem* item : seventhslide::test::visible_items_of(
             qobject_cast<QQuickItem*>(dialog), "seventhslide::app::FreeformShapeItem")) {
        if (item->width() > 40) {
            shape = item;
        }
    }
    REQUIRE(shape != nullptr);
    QQuickItem* box = shape->parentItem();
    REQUIRE(box != nullptr);
    const qreal a = shape->mapToScene(QPointF(0, 0)).x();
    const qreal b = shape->mapToScene(QPointF(shape->width(), 0)).x();
    const qreal left = box->mapToScene(QPointF(0, 0)).x();
    const qreal right = box->mapToScene(QPointF(box->width(), 0)).x();
    INFO("shape " << std::min(a, b) << ".." << std::max(a, b) << " box " << left << ".." << right);
    // The paint surface overhangs the box by its border pad on each side.
    CHECK(std::abs((a + b) / 2 - (left + right) / 2) < 1.0);
    CHECK(a > b);  // mirrored
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// A linked box's room between lines is not in its text. The canvas draws such a
// box as the output does, with that room.
TEST_CASE("The canvas draws a linked box as the output does", "[ui][freeform]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    auto doc = seventhslide::freeform::make_document("Gaps", {facade.session().primary_output()->id});
    doc.line_control = true;
    doc.cues = {seventhslide::freeform::Cue{.id = "c1", .text = "Verse one"},
                seventhslide::freeform::Cue{.id = "c2", .text = "Verse two"}};
    auto& deck = doc.decks[0];
    deck.line_to_slide = {0, 0};
    deck.cue_flow = {{.id = "c2", .gap_above = 30}};
    seventhslide::freeform::Object text;
    text.id = "t1";
    text.type = seventhslide::freeform::ObjectType::Text;
    text.w = 1600;
    text.h = 900;
    text.cue_linked = true;
    deck.slides[0].objects.push_back(text);
    seventhslide::model::FreeformItem item;
    item.name = "Gaps";
    item.document = doc.to_json();
    const auto id = facade.freeform_repo()->create_item(item);

    OperatorUi ui(facade);
    auto* panel = qobject_cast<QQuickItem*>(
        qml_object(ui.find_component("SlideEditorWindow"), "SlideStage"));
    REQUIRE(panel != nullptr);
    REQUIRE(facade.slideEdit()->openFreeform(static_cast<qint64>(id)));
    QTest::qWait(20);
    // A repeater's items are the canvas's by item, not as child objects.
    QVariantList gaps;
    const std::function<void(QQuickItem*)> look = [&](QQuickItem* node) {
        if (QString::fromLatin1(node->metaObject()->className()).contains(QStringLiteral("OverlayTextItem"))
            && !node->property("markup").toString().isEmpty()) {
            gaps = node->property("gapsAbove").toList();
        }
        for (QQuickItem* child : node->childItems()) {
            look(child);
        }
    };
    look(panel);
    REQUIRE(gaps.size() == 2);
    CHECK(gaps[1].toInt() == 30);
}

// The text being edited holds the spaces between cues that run on; chords are
// counted within each cue.
TEST_CASE("Chords stay on their words where edited cues run on", "[ui][freeform][chords]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    OperatorUi ui(facade);
    QObject* editor = qml_object(ui.find_component("SlideEditorWindow"), "SlideEditState");
    REQUIRE(editor != nullptr);

    editor->setProperty("textEditDraft", QStringLiteral("Ab Cd"));
    editor->setProperty("editCues",
                        QVariantList{QVariantMap{{QStringLiteral("length"), 2}, {QStringLiteral("before"), 0}},
                                     QVariantMap{{QStringLiteral("length"), 2}, {QStringLiteral("before"), 1}}});
    const auto place = [&](int cue, int at) {
        QVariant out;
        REQUIRE(QMetaObject::invokeMethod(editor, "chordDraftPos", Q_RETURN_ARG(QVariant, out),
                                          Q_ARG(QVariant, cue), Q_ARG(QVariant, at)));
        return out.toInt();
    };
    CHECK(place(0, 0) == 0);
    CHECK(place(0, 2) == 2);
    CHECK(place(1, 0) == 3);
    CHECK(place(1, 1) == 4);

    const auto slot = [&](int pos) {
        QVariant out;
        REQUIRE(QMetaObject::invokeMethod(editor, "chordSlotAt", Q_RETURN_ARG(QVariant, out),
                                          Q_ARG(QVariant, pos), Q_ARG(QVariant, pos + 1)));
        const auto map = out.value<QJSValue>().toVariant().toMap();
        return std::pair{map.value(QStringLiteral("cue")).toInt(),
                         map.value(QStringLiteral("at")).toInt()};
    };
    CHECK(slot(0) == std::pair{0, 0});
    CHECK(slot(3) == std::pair{1, 0});
    CHECK(slot(4) == std::pair{1, 1});
    // The space between them goes with the end of the first.
    CHECK(slot(2) == std::pair{0, 2});
}

// A song saved as Freeform owns its background theme: each
// slide has the theme's colour and fade, and a picture the theme owns as the
// library's own copy, the item saved once that is in.
TEST_CASE("A song saved as Freeform owns its background theme", "[ui][freeform][bg]")
{
    using seventhslide::freeform::SlideBackgroundKind;
    using seventhslide::model::Transition;
    using seventhslide::model::TransitionType;
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    seventhslide::model::Song song;
    song.title = "Hymn";
    song.lyrics = "[V1]\nBe thou my vision\nO Lord of my heart";
    song.verse_order = "v1";
    const auto song_id = static_cast<qint64>(facade.song_repo()->insert(song));
    auto* themes = facade.themes();
    const auto main = QStringLiteral("Main");
    const auto bg = QStringLiteral("bg");
    const auto use = [&](const QString& theme_id) {
        REQUIRE(themes->setDefault(main, QStringLiteral("song"), bg, theme_id).isEmpty());
    };

    // A colour: in at once.
    const auto dusk = themes->create(main, bg, QStringLiteral("Dusk"),
                                     {{QStringLiteral("background_type"), QStringLiteral("color")},
                                      {QStringLiteral("background_color"), QStringLiteral("#654321")},
                                      {QStringLiteral("bg_enable_fade"), true},
                                      {QStringLiteral("bg_fade_duration"), 700}})
                          .value(QStringLiteral("id"))
                          .toString();
    REQUIRE_FALSE(dusk.isEmpty());
    use(dusk);
    const auto coloured = facade.saveSongAsFreeform(song_id, QStringLiteral("Dusk hymn"));
    REQUIRE(coloured.value(QStringLiteral("error")).toString().isEmpty());
    const auto coloured_id = coloured.value(QStringLiteral("id")).toLongLong();
    REQUIRE(coloured_id >= 0);
    const auto coloured_doc = facade.freeformLibrary()->load_freeform_document(coloured_id);
    REQUIRE(coloured_doc);
    REQUIRE_FALSE(coloured_doc->decks.empty());
    for (const auto& slide : coloured_doc->decks[0].slides) {
        CHECK(slide.background.kind == SlideBackgroundKind::Color);
        CHECK(slide.background.color == "#654321");
        CHECK(slide.background_transition == Transition{TransitionType::Fade, 700});
    }

    // A picture the theme owns: saved once the library has its own copy.
    QImage picture(32, 18, QImage::Format_RGB32);
    picture.fill(QColor(QStringLiteral("#336699")));
    const auto png = dir.path / "dawn.png";
    REQUIRE(picture.save(QString::fromStdString(png.string())));
    const auto dawn = themes->create(main, bg, QStringLiteral("Dawn"),
                                     {{QStringLiteral("background_type"), QStringLiteral("image")}})
                          .value(QStringLiteral("id"))
                          .toString();
    QSignalSpy uploaded(themes, &seventhslide::app::ThemeController::assetUploadFinished);
    themes->upload_asset(main, dawn, bg, QString::fromStdString(png.string()));
    REQUIRE(uploaded.wait(10000));
    REQUIRE(uploaded.front().at(1).toString().isEmpty());
    use(dawn);
    const auto saved_item = [&]() -> std::optional<qint64> {
        for (const auto& item : facade.freeform_repo()->list_items()) {
            if (item.name == "Dawn hymn" && item.id) {
                return static_cast<qint64>(*item.id);
            }
        }
        return std::nullopt;
    };
    const auto waiting = facade.saveSongAsFreeform(song_id, QStringLiteral("Dawn hymn"));
    CHECK(waiting.value(QStringLiteral("error")).toString().isEmpty());
    CHECK(waiting.value(QStringLiteral("id")).toLongLong() == -1);
    CHECK_FALSE(saved_item());
    REQUIRE(QTest::qWaitFor([&] { return saved_item().has_value(); }, 10000));
    const auto pictured = facade.freeformLibrary()->load_freeform_document(*saved_item());
    REQUIRE(pictured);
    REQUIRE_FALSE(pictured->decks.empty());
    const auto copied = pictured->decks[0].slides.front().background.image;
    CHECK_FALSE(copied.empty());
    CHECK_FALSE(copied.starts_with("/static/"));
    for (const auto& slide : pictured->decks[0].slides) {
        CHECK(slide.background.kind == SlideBackgroundKind::Image);
        CHECK(slide.background.image == copied);
        // A cut: the theme had no fade.
        REQUIRE(slide.background_transition.has_value());
        CHECK(slide.background_transition->type == TransitionType::Cut);
    }
    CHECK(facade.media()->images()->rowCount() == 1);

    // The theme goes; the item keeps its picture, which is the library's.
    REQUIRE(themes->remove(main, bg, dawn).isEmpty());
    CHECK(std::filesystem::exists(facade.open_library().media_root() / "images" / copied));
}

TEST_CASE("A stack with Preview on shows Preview and Program side by side, and T takes it",
          "[ui][preview]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    REQUIRE(facade.library()->createService(QStringLiteral("Svc")) > 0);
    using seventhslide::db::MediaKind;
    for (const char* file : {"a.png", "b.png", "c.png"}) {
        facade.media_repo()->upsert_file(MediaKind::Image, file, file);
    }
    const auto folder_id = facade.media_repo()->folders(MediaKind::Image).create("Pack");
    for (const char* file : {"a.png", "b.png", "c.png"}) {
        (void)facade.media_repo()->add_folder_item(MediaKind::Image, folder_id, file);
    }
    facade.refresh_library();
    REQUIRE(facade.media()->addImageFolderToService(static_cast<qint64>(folder_id)));
    const auto main = facade.stacks()->focusedStackId();

    OperatorUi ui(facade);
    QQuickWindow* window = ui.window();
    window->requestActivate();
    REQUIRE(QTest::qWaitForWindowActive(window));
    QObject* controller = ui.find_component("ControllerColumn");
    REQUIRE(controller != nullptr);
    auto* preview_well = window->findChild<QQuickItem*>(QStringLiteral("previewWell"));
    auto* program_well = seventhslide::test::program_well(window);
    REQUIRE(preview_well != nullptr);
    REQUIRE(program_well != nullptr);
    // The item named ``name`` shown under ``root`` (anywhere in the window,
    // popups and a list's rows included): found by walking the items.
    const auto shown_in = [](QQuickItem* root, const QString& name) -> QQuickItem* {
        std::vector<QQuickItem*> todo{root};
        while (!todo.empty()) {
            QQuickItem* item = todo.back();
            todo.pop_back();
            if (!item->isVisible()) {
                continue;
            }
            if (item->objectName() == name) {
                return item;
            }
            for (auto* child : item->childItems()) {
                todo.push_back(child);
            }
        }
        return nullptr;
    };
    const auto shown = [&](const QString& name) {
        return shown_in(window->contentItem(), name);
    };
    // How many of ``well``'s thumbs are edged as the next one.
    auto* style = qmlEngine(window)->singletonInstance<QObject*>("SeventhSlide", "Style");
    REQUIRE(style != nullptr);
    const auto next_edge = style->property("thumbNext").value<QColor>();
    REQUIRE(next_edge.isValid());
    const auto next_thumbs = [&next_edge](QQuickItem* well) {
        int count = 0;
        std::vector<QQuickItem*> todo{well};
        while (!todo.empty()) {
            QQuickItem* item = todo.back();
            todo.pop_back();
            if (!item->isVisible()) {
                continue;
            }
            if (item->objectName() == QStringLiteral("wellThumb")) {
                auto* border = item->property("border").value<QObject*>();
                REQUIRE(border != nullptr);
                count += border->property("color").value<QColor>() == next_edge;
            }
            for (auto* child : item->childItems()) {
                todo.push_back(child);
            }
        }
        return count;
    };

    // One well while the stack has no Preview.
    CHECK_FALSE(preview_well->isVisible());
    CHECK(shown(QStringLiteral("takeTransition")) == nullptr);
    const int narrow = controller->property("minimumWidth").toInt();

    REQUIRE(facade.stacks()->setStackPreview(main, true).isEmpty());
    QTest::qWait(20);
    CHECK(preview_well->isVisible());
    CHECK(program_well->isVisible());
    CHECK(controller->property("minimumWidth").toInt() > narrow);
    // Nothing cued: nothing can be taken.
    auto* take = shown(QStringLiteral("takeTransition"));
    REQUIRE(take != nullptr);
    CHECK_FALSE(take->isEnabled());

    // Cued: the Preview has it, the outputs not.
    REQUIRE(facade.live()->selectServiceItem(0));
    QTest::qWait(20);
    CHECK(facade.preview()->totalLines() == 3);
    CHECK_FALSE(facade.program()->occupied());
    CHECK(take->isEnabled());

    // The keys drive the Program's well until the Preview's is worked: it
    // alone marks what is next.
    CHECK(program_well->property("keyed").toBool());
    CHECK_FALSE(preview_well->property("keyed").toBool());
    CHECK(next_thumbs(preview_well) == 0);
    QTest::keyClick(window, Qt::Key_Right);
    CHECK(facade.preview()->lineCursor() == 0);
    // Its face's corner, clear of the thumbnails.
    auto* face = preview_well->findChild<QQuickItem*>(QStringLiteral("wellFace"));
    REQUIRE(face != nullptr);
    click(window, face->mapToScene(QPointF(3, 3)).toPoint());
    CHECK(preview_well->property("keyed").toBool());
    CHECK_FALSE(program_well->property("keyed").toBool());
    CHECK(next_thumbs(preview_well) == 1);
    QTest::keyClick(window, Qt::Key_Right);
    CHECK(facade.preview()->lineCursor() == 1);

    // T takes it where it was cued; the keys go back to the Program's well.
    QTest::keyClick(window, Qt::Key_T);
    QTest::qWait(20);
    CHECK(facade.program()->lineCursor() == 1);
    CHECK_FALSE(facade.preview()->occupied());
    CHECK_FALSE(take->isEnabled());
    CHECK(program_well->property("keyed").toBool());
    CHECK(next_thumbs(program_well) == 1);
    QTest::keyClick(window, Qt::Key_Right);
    CHECK(facade.program()->lineCursor() == 2);

    // Transition's ▾ (its right edge): a Fade's length, kept on this
    // computer, and a take by the Fade, what was live going back to the
    // Preview where it was.
    REQUIRE(facade.live()->selectServiceItem(0));
    QTest::qWait(20);
    const int was_live = facade.program()->lineCursor();
    REQUIRE(was_live > 0);
    REQUIRE(take->isEnabled());
    const auto open_take_menu = [&] {
        click(window, take->mapToScene(QPointF(take->width() - 6, take->height() / 2)).toPoint());
        QTest::qWait(20);
    };
    const auto content_transition = [&facade] {
        return facade.session().scene_frames().front().content_transition;
    };
    open_take_menu();
    // Its + (the top of the field's right edge) adds a tenth of a second.
    auto* length = shown(QStringLiteral("takeFadeLength"));
    REQUIRE(length != nullptr);
    click(window, length->mapToScene(QPointF(length->width() - 6, length->height() / 4)).toPoint());
    QTest::qWait(20);
    CHECK(facade.stacks()->takeFadeMs() == 600);
    auto* fade = shown(QStringLiteral("takeFade"));
    REQUIRE(fade != nullptr);
    click(window, centre_of(fade));
    QTest::qWait(20);
    CHECK(facade.program()->lineCursor() == 0);
    CHECK(facade.preview()->lineCursor() == was_live);
    CHECK(content_transition().type == seventhslide::model::TransitionType::Fade);
    CHECK(content_transition().duration_ms == 600);
    // The take closed it.
    CHECK(shown(QStringLiteral("takeFade")) == nullptr);

    // By a Cut, and back again by Transition, the theme's way.
    open_take_menu();
    auto* cut = shown(QStringLiteral("takeCut"));
    REQUIRE(cut != nullptr);
    click(window, centre_of(cut));
    QTest::qWait(20);
    CHECK(facade.program()->lineCursor() == was_live);
    CHECK(content_transition().type == seventhslide::model::TransitionType::Cut);
    click(window, centre_of(take));
    QTest::qWait(20);
    CHECK(facade.program()->lineCursor() == 0);
    CHECK(facade.preview()->lineCursor() == was_live);

    // Blanked in its own well, what is cued goes live blanked, and what was
    // live comes back as it was; taken back, each blank goes with its side.
    const std::string stack = main.toStdString();
    auto* cued_blank = shown_in(preview_well, QStringLiteral("transport_blank"));
    auto* live_blank = shown_in(program_well, QStringLiteral("transport_blank"));
    REQUIRE(cued_blank != nullptr);
    REQUIRE(live_blank != nullptr);
    REQUIRE(cued_blank->isEnabled());
    click(window, centre_of(cued_blank));
    QTest::qWait(20);
    CHECK(facade.session().preview_is_blank(stack));
    CHECK(cued_blank->property("toggled").toBool());
    CHECK_FALSE(live_blank->property("toggled").toBool());
    CHECK(shown(QStringLiteral("takeHeld")) == nullptr);
    QTest::keyClick(window, Qt::Key_T);
    QTest::qWait(20);
    CHECK(facade.program()->lineCursor() == was_live);
    CHECK(facade.session().stack_is_blank(stack));
    CHECK_FALSE(facade.session().preview_is_blank(stack));
    CHECK(live_blank->property("toggled").toBool());
    CHECK_FALSE(cued_blank->property("toggled").toBool());
    // Blanked, the take still shows: what is cued is not.
    CHECK(shown(QStringLiteral("takeHeld")) == nullptr);
    QTest::keyClick(window, Qt::Key_T);
    QTest::qWait(20);
    CHECK(facade.program()->lineCursor() == 0);
    CHECK_FALSE(facade.session().stack_is_blank(stack));
    CHECK(facade.session().preview_is_blank(stack));
    CHECK(cued_blank->property("toggled").toBool());

    // Cleared in its own well: what is live stays, the keys go back to it,
    // and T has nothing to take.
    click(window, face->mapToScene(QPointF(3, 3)).toPoint());
    auto* clear = shown(QStringLiteral("transport_clear"));
    REQUIRE(clear != nullptr);
    click(window, centre_of(clear));
    QTest::qWait(20);
    CHECK_FALSE(facade.preview()->occupied());
    CHECK_FALSE(facade.session().preview_is_blank(stack));
    CHECK_FALSE(cued_blank->isEnabled());
    QTest::keyClick(window, Qt::Key_Right);
    CHECK(facade.program()->lineCursor() == 1);
    QTest::keyClick(window, Qt::Key_T);
    QTest::qWait(20);
    CHECK(facade.program()->occupied());
    CHECK(facade.program()->lineCursor() == 1);

    // Preview off: one well again.
    REQUIRE(facade.stacks()->setStackPreview(main, false).isEmpty());
    QTest::qWait(20);
    CHECK_FALSE(preview_well->isVisible());
    CHECK(controller->property("minimumWidth").toInt() == narrow);
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// The operator window starts with what it shows. Each window, page and dialog
// is made the first time it is asked for, and kept.
TEST_CASE("The operator window builds no window until it is opened", "[ui]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    OperatorUi ui(facade);
    for (const char* name : {"AppSettingsDialog", "CollaborationWindow", "OutputEditDialog",
                             "SongEditDialog", "SaveAsFreeformDialog", "ServicePickerDialog",
                             "ServiceSettingsDialog", "SongImportDialog", "StreamDialog",
                             "ExportImagesDialog", "ConvertDialog", "DownloadDialog",
                             "WebPageDialog", "FreeformImportDialog", "SlideEditorWindow"}) {
        INFO("built " << name);
        CHECK(ui.built_component(name) == nullptr);
    }
    // Asking for what is inside a window not opened (a Settings page's state,
    // the Collaboration window's panel) makes nothing.
    CHECK(ui.find_component("SettingsSyncState") == nullptr);
    CHECK(ui.find_component("CollaborationPanel") == nullptr);
    CHECK(ui.built_component("AppSettingsDialog") == nullptr);
    CHECK(ui.built_component("CollaborationWindow") == nullptr);

    // The slide editor is made when a document opens, and shows it as it
    // always has; closed and opened again, it is the same window.
    REQUIRE(QTest::qWaitForWindowExposed(ui.window()));
    auto* edit = facade.slideEdit();
    const auto first = facade.freeformLibrary()->createFreeform(QStringLiteral("Welcome"));
    const auto second = facade.freeformLibrary()->createFreeform(QStringLiteral("Offering"));
    REQUIRE(edit->openFreeform(first));
    auto* editor = qobject_cast<QQuickItem*>(ui.built_component("SlideEditorWindow"));
    REQUIRE(editor != nullptr);
    QObject* editState = qml_object(editor, "SlideEditState");
    REQUIRE(editState != nullptr);
    CHECK(editor->isVisible());
    CHECK(editor->parentItem() == ui.window()->contentItem());
    CHECK(editor->width() == ui.window()->width());
    CHECK(editState->property("nameDraft").toString() == QStringLiteral("Welcome"));
    CHECK(editor->hasActiveFocus());
    REQUIRE(editState->setProperty("ribbonTab", QStringLiteral("insert")));
    edit->cancel();
    CHECK_FALSE(editor->isVisible());
    REQUIRE(edit->openFreeform(second));
    CHECK(ui.built_component("SlideEditorWindow") == editor);
    CHECK(editor->isVisible());
    CHECK(editState->property("nameDraft").toString() == QStringLiteral("Offering"));
    CHECK(editState->property("ribbonTab").toString() == QStringLiteral("home"));
    edit->cancel();

    // The bar's Settings makes Settings, with the page it opens on.
    QObject* bar = ui.find_component("AppBar");
    REQUIRE(bar != nullptr);
    REQUIRE(QMetaObject::invokeMethod(bar, "settingsRequested"));
    QObject* settings = ui.built_component("AppSettingsDialog");
    REQUIRE(settings != nullptr);
    CHECK(settings->property("open").toBool());
    CHECK(ui.built_component("SettingsGeneralPanel") != nullptr);
    CHECK(ui.built_component("SettingsSyncPanel") == nullptr);
    REQUIRE(settings->setProperty("tab", QStringLiteral("sync")));
    CHECK(ui.built_component("SettingsSyncPanel") != nullptr);
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}
