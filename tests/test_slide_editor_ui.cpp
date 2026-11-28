// The slide editor's controls (src/ui/editors/slide) drive session.slideEdit
// the way the ribbon and the sidebar do: these take each control by its
// objectName, work it as a click or a commit does, and check the edit
// reached the document.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "canvas_test_helpers.hpp"
#include "facade_all.hpp"
#include "font_controller.hpp"
#include "font_fixtures.hpp"
#include "font_store.hpp"
#include "freeform_library_controller.hpp"
#include "image_providers.hpp"
#include "slide_edit_session.hpp"
#include "test_qml.hpp"
#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"
#include "theme_controller.hpp"
#include "ui_test_helpers.hpp"

#include <seventhslide/layout/chords.hpp>

#include <QElapsedTimer>
#include <QImage>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>
#include <QVariant>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <utility>
#include <vector>

using Catch::Matchers::WithinAbs;

namespace {

struct EditorWindow {
    // First member: everything below needs the application to exist.
    bool app = (seventhslide::test::ensure_qt_application(), true);
    seventhslide::test::TempDir tmp;
    seventhslide::app::SessionFacade facade;
    seventhslide::test::QmlScene scene;
    QObject* window = nullptr;
    QObject* dialog = nullptr;
    /// The window's view-model (SlideEditState).
    QObject* state = nullptr;

    EditorWindow()
    {
        REQUIRE(facade.init_libraries(tmp.path / "data"));
        seventhslide::app::add_image_providers(scene.engine(), facade);
        window = scene.create(QByteArrayLiteral("import QtQuick\nimport SeventhSlide\n"
                                                "Window {\n id: win; width: 1400; height: 900; visible: true\n"
                                                " required property SessionFacade facade\n"
                                                " property alias dialog: dialog\n "
                                                "SlideEditorWindow { id: dialog; session: win.facade }\n}\n"),
                              {}, {{QStringLiteral("facade"), QVariant::fromValue(&facade)}});
        dialog = window->property("dialog").value<QObject*>();
        REQUIRE(dialog != nullptr);
        for (QObject* o : dialog->findChildren<QObject*>()) {
            if (QByteArray(o->metaObject()->className()).startsWith("SlideEditState_QMLTYPE_")) {
                state = o;
            }
        }
        REQUIRE(state != nullptr);
    }

    [[nodiscard]] seventhslide::app::SlideEditSession* edit() { return facade.slideEdit(); }
    [[nodiscard]] seventhslide::app::SlideObjectsSession* objects() { return edit()->objects(); }
    [[nodiscard]] QVariantMap selection() { return objects()->selection(); }

    [[nodiscard]] QObject* control(const char* name) const
    {
        QObject* found = dialog->findChild<QObject*>(QString::fromLatin1(name));
        if (found == nullptr) {
            // A ribbon tab is made the first time it shows: show each once.
            const auto was = state->property("ribbonTab");
            for (const char* tab : {"home", "insert", "design", "transitions", "animations", "cues"}) {
                state->setProperty("ribbonTab", QString::fromLatin1(tab));
            }
            state->setProperty("ribbonTab", was);
            found = dialog->findChild<QObject*>(QString::fromLatin1(name));
        }
        INFO("control " << name);
        REQUIRE(found != nullptr);
        return found;
    }
    /// A RibbonField's commit, as typing a number and Enter.
    void commit(const char* name, double n)
    {
        REQUIRE(QMetaObject::invokeMethod(control(name), "commit", Q_ARG(double, n)));
    }
    /// A RibbonDropdown's pick.
    void pick(const char* name, const QVariant& value)
    {
        REQUIRE(QMetaObject::invokeMethod(control(name), "picked", Q_ARG(QVariant, value)));
    }
    /// A RibbonStrip's button.
    void strip(const char* name, const char* id)
    {
        REQUIRE(QMetaObject::invokeMethod(control(name), "clicked", Q_ARG(QString, QString::fromLatin1(id))));
    }
    /// A RibbonSwatch's colour.
    void swatch(const char* name, const char* value)
    {
        REQUIRE(QMetaObject::invokeMethod(control(name), "edited", Q_ARG(QString, QString::fromLatin1(value))));
    }
    /// A ToggleSwitch's flip.
    void toggle(const char* name)
    {
        REQUIRE(QMetaObject::invokeMethod(control(name), "toggled"));
    }
    [[nodiscard]] QVariant shown(const char* name) const
    {
        return control(name)->property("value");
    }

    void open_freeform(const char* name)
    {
        const auto id = facade.freeformLibrary()->createFreeform(QString::fromLatin1(name));
        REQUIRE(id > 0);
        REQUIRE(edit()->openFreeform(id));
        REQUIRE(dialog->property("visible").toBool());
    }
    void open_theme(const char* kind)
    {
        const auto themes = facade.themes()->list(QStringLiteral("Main"), QString::fromLatin1(kind));
        REQUIRE(!themes.isEmpty());
        REQUIRE(edit()->openTheme(QStringLiteral("Main"), QString::fromLatin1(kind),
                                  themes.front().toMap().value(QStringLiteral("id")).toString()));
        REQUIRE(dialog->property("visible").toBool());
    }
};

}  // namespace

TEST_CASE("The ribbon and sidebar edit a Freeform text box", "[ui][slide-editor]")
{
    EditorWindow w;
    w.open_freeform("Card");
    REQUIRE(w.objects()->insert(QStringLiteral("text")).isEmpty());
    const auto id = w.selection().value(QStringLiteral("id")).toString();
    REQUIRE(w.objects()->setText(id, QStringLiteral("Welcome"), false, false, false).isEmpty());
    REQUIRE(w.state->property("hasSel").toBool());

    w.commit("sideX", 300);
    w.commit("sideWidth", 640);
    w.commit("ribbonFontSize", 60);
    w.strip("ribbonTypeStrip", "bold");
    w.strip("ribbonAlignStrip", "left");
    w.commit("sideLineGap", 7);
    w.commit("sideOpacity", 50);

    const QVariantMap sel = w.selection();
    CHECK(sel.value(QStringLiteral("x")).toDouble() == 300);
    CHECK(sel.value(QStringLiteral("width")).toDouble() == 640);
    CHECK(sel.value(QStringLiteral("font_size")).toInt() == 60);
    CHECK(sel.value(QStringLiteral("bold")).toBool());
    CHECK(sel.value(QStringLiteral("align")).toString() == QStringLiteral("left"));
    CHECK(sel.value(QStringLiteral("line_gap")).toInt() == 7);
    CHECK_THAT(sel.value(QStringLiteral("opacity")).toDouble(), WithinAbs(0.5, 1e-9));

    // The controls read the same values back.
    CHECK(w.shown("sideX").toDouble() == 300);
    CHECK(w.shown("ribbonFontSize").toInt() == 60);
    CHECK(w.shown("sideOpacity").toInt() == 50);
}

TEST_CASE("Text styles go to the typed range or the whole text or the object", "[ui][slide-editor]")
{
    EditorWindow w;
    w.open_freeform("Card");
    REQUIRE(w.objects()->insert(QStringLiteral("text")).isEmpty());
    const auto id = w.selection().value(QStringLiteral("id")).toString();
    REQUIRE(w.objects()->setText(id, QStringLiteral("Welcome"), false, false, false).isEmpty());

    // The stage's text editor, driven the way the stage drives it.
    const auto edit = [&w](int start, int end) {
        REQUIRE(QMetaObject::invokeMethod(w.state, "beginTextEdit",
                                          Q_ARG(QVariant, QVariant(w.selection()))));
        REQUIRE(w.state->property("textEditing").toBool());
        REQUIRE(w.state->setProperty("editSelStart", start));
        REQUIRE(w.state->setProperty("editSelEnd", end));
    };
    const auto end_edit = [&w](bool save) {
        REQUIRE(QMetaObject::invokeMethod(w.state, "endTextEdit", Q_ARG(QVariant, save)));
    };
    const auto style_at = [&w](int offset) {
        QVariant out;
        REQUIRE(QMetaObject::invokeMethod(w.state, "styleAtOffset", Q_RETURN_ARG(QVariant, out),
                                          Q_ARG(QVariant, offset)));
        return out.toMap();
    };
    const auto on = [&style_at](int offset, const char* key) {
        return style_at(offset).value(QString::fromLatin1(key)).toBool();
    };
    const auto flag_shown = [&w](const char* key) {
        QVariant out;
        REQUIRE(QMetaObject::invokeMethod(w.state, "flagOn", Q_RETURN_ARG(QVariant, out),
                                          Q_ARG(QVariant, QString::fromLatin1(key))));
        return out.toBool();
    };

    SECTION("nothing typed in: the object")
    {
        w.strip("ribbonTypeStrip", "italic");
        CHECK(w.selection().value(QStringLiteral("italic")).toBool());
        CHECK(flag_shown("italic"));
        w.strip("ribbonTypeStrip", "italic");  // a toggle
        CHECK_FALSE(w.selection().value(QStringLiteral("italic")).toBool());
        CHECK_FALSE(flag_shown("italic"));

        w.strip("ribbonTypeStrip", "super");
        edit(0, 0);
        CHECK(style_at(3).value(QStringLiteral("baselineRise")).toInt() > 0);
        end_edit(false);
    }

    SECTION("a caret: the whole text")
    {
        edit(2, 2);
        w.strip("ribbonTypeStrip", "bold");
        CHECK(flag_shown("bold"));
        CHECK(on(1, "bold"));
        CHECK(on(6, "bold"));
        w.swatch("ribbonFontColor", "#ff0000");
        CHECK(style_at(6).value(QStringLiteral("color")).toString() == QStringLiteral("#ff0000"));
        end_edit(true);
        edit(0, 0);
        CHECK(on(1, "bold"));
        CHECK(on(6, "bold"));
        CHECK(style_at(6).value(QStringLiteral("color")).toString() == QStringLiteral("#ff0000"));
        end_edit(false);
    }

    SECTION("a range: just that range")
    {
        edit(0, 3);
        w.strip("ribbonTypeStrip", "underline");
        CHECK(flag_shown("underline"));
        CHECK(on(1, "underline"));
        CHECK_FALSE(on(5, "underline"));
        w.pick("ribbonFontFamily", QStringLiteral("Liberation Serif"));
        CHECK(style_at(1).value(QStringLiteral("family")).toString()
              == QStringLiteral("Liberation Serif"));
        CHECK(style_at(5).value(QStringLiteral("family")).toString()
              != QStringLiteral("Liberation Serif"));
        w.strip("ribbonTypeStrip", "super");
        CHECK(style_at(5).value(QStringLiteral("baselineRise")).toInt() == 0);
        end_edit(true);

        // Saved as runs: the object as a whole is not underlined.
        CHECK_FALSE(w.selection().value(QStringLiteral("underline")).toBool());
        edit(0, 0);
        CHECK(on(1, "underline"));
        CHECK_FALSE(on(5, "underline"));
        end_edit(false);
    }
}

TEST_CASE("The sidebar edits a Freeform shape", "[ui][slide-editor]")
{
    EditorWindow w;
    w.open_freeform("Card");
    REQUIRE(w.objects()
                ->insert(QStringLiteral("shape"), {{QStringLiteral("shapeKind"), QStringLiteral("rounded_rect")}})
                .isEmpty());
    REQUIRE(w.state->property("selType").toString() == QStringLiteral("shape"));

    w.commit("sideShapeCorner", 0.3);
    w.commit("sideStrokeWidth", 2.5);
    w.swatch("sideFill", "#ff0000");

    const QVariantMap sel = w.selection();
    CHECK_THAT(sel.value(QStringLiteral("shape_corner")).toDouble(), WithinAbs(0.3, 1e-9));
    CHECK_THAT(sel.value(QStringLiteral("stroke_width")).toDouble(), WithinAbs(2.5, 1e-9));
    CHECK(sel.value(QStringLiteral("fill")).toString().toLower().contains(QStringLiteral("ff0000")));
}

TEST_CASE("The sidebar gives a text box a fill and a line", "[ui][slide-editor][fill][line]")
{
    EditorWindow w;
    w.open_freeform("Card");
    REQUIRE(w.objects()->insert(QStringLiteral("text")).isEmpty());
    REQUIRE(w.state->property("selType").toString() == QStringLiteral("text"));

    // A new text box has neither, and says so.
    CHECK(w.control("sideFillSection")->property("visible").toBool());
    CHECK(w.control("sideLineSection")->property("visible").toBool());
    CHECK(w.shown("sideFillKind").toString() == QStringLiteral("none"));
    CHECK(w.shown("sideLineDash").toString() == QStringLiteral("solid"));

    w.pick("sideFillKind", QStringLiteral("solid"));
    w.swatch("sideFill", "#1a2b3c");
    w.swatch("sideStroke", "#ffffff");
    w.commit("sideStrokeWidth", 3);
    w.pick("sideLineDash", QStringLiteral("dash"));

    const QVariantMap sel = w.selection();
    CHECK(sel.value(QStringLiteral("fill_kind")).toString() == QStringLiteral("solid"));
    CHECK(sel.value(QStringLiteral("fill")).toString() == QStringLiteral("#1a2b3c"));
    CHECK(sel.value(QStringLiteral("stroke")).toString() == QStringLiteral("#ffffff"));
    CHECK_THAT(sel.value(QStringLiteral("stroke_width")).toDouble(), WithinAbs(3.0, 1e-9));
    CHECK(sel.value(QStringLiteral("line_dash")).toString() == QStringLiteral("dash"));
    CHECK(w.shown("sideFillKind").toString() == QStringLiteral("solid"));
    CHECK(w.shown("sideLineDash").toString() == QStringLiteral("dash"));

    // Off again: the colour is kept for when it comes back.
    w.pick("sideFillKind", QStringLiteral("none"));
    CHECK(w.selection().value(QStringLiteral("fill_kind")).toString() == QStringLiteral("none"));
    CHECK(w.selection().value(QStringLiteral("fill")).toString() == QStringLiteral("#1a2b3c"));

    // The stage draws the frame the output paints: the text box's own shape
    // item, a delegate's child, so found through the visual tree.
    bool frame_drawn = false;
    for (QQuickItem* item : seventhslide::test::visible_items_of(
             qobject_cast<QQuickItem*>(w.dialog), "seventhslide::app::FreeformShapeItem")) {
        if (item->property("kind").toString() == QStringLiteral("rectangle")
            && item->property("stroke").toString() == QStringLiteral("#ffffff")
            && item->property("dash").toString() == QStringLiteral("dash")
            && item->property("fillKind").toString() == QStringLiteral("none")) {
            frame_drawn = true;
        }
    }
    CHECK(frame_drawn);
}

TEST_CASE("The sidebar gives a text box its effects, and the stage draws them as the output does",
          "[ui][slide-editor][effects]")
{
    EditorWindow w;
    w.open_freeform("Card");
    REQUIRE(w.objects()->insert(QStringLiteral("text")).isEmpty());
    const auto id = w.selection().value(QStringLiteral("id")).toString();
    REQUIRE(w.objects()->setText(id, QStringLiteral("Welcome"), false, false, false).isEmpty());
    CHECK(w.control("sideEffectsSection")->property("visible").toBool());

    // A frame to put the shadow and glow round.
    w.pick("sideFillKind", QStringLiteral("solid"));
    w.toggle("sideShadowOn");
    w.commit("sideShadowBlur", 12);
    w.commit("sideShadowAngle", 90);
    w.toggle("sideGlowOn");
    w.swatch("sideGlowColor", "#00ff00");
    w.commit("sideSoftEdge", 4);
    w.commit("sideTextOutlineWidth", 2);
    w.swatch("sideTextOutlineColor", "#000000");
    w.toggle("sideTextShadowOn");
    w.commit("sideTextShadowDistance", 5);

    const QVariantMap sel = w.selection();
    CHECK(sel.value(QStringLiteral("shadow_on")).toBool());
    CHECK(sel.value(QStringLiteral("shadow_blur")).toDouble() == 12);
    CHECK(sel.value(QStringLiteral("shadow_angle")).toDouble() == 90);
    CHECK(sel.value(QStringLiteral("glow_on")).toBool());
    CHECK(sel.value(QStringLiteral("glow_color")).toString() == QStringLiteral("#00ff00"));
    CHECK(sel.value(QStringLiteral("soft_edge")).toDouble() == 4);
    CHECK(sel.value(QStringLiteral("text_outline_width")).toDouble() == 2);
    CHECK(sel.value(QStringLiteral("text_shadow_on")).toBool());
    CHECK(sel.value(QStringLiteral("text_shadow_distance")).toDouble() == 5);
    CHECK(sel.value(QStringLiteral("effects_reach")).toDouble() > 0);
    CHECK(w.shown("sideShadowBlur").toDouble() == 12);

    // Words with effects are drawn by the output's painter on the stage, with
    // the effects handed over whole.
    bool words_as_output = false;
    for (QQuickItem* item : seventhslide::test::visible_items_of(
             qobject_cast<QQuickItem*>(w.dialog), "seventhslide::app::OverlayTextItem")) {
        const auto effects = item->property("effects").toMap();
        if (effects.value(QStringLiteral("text_shadow")).toMap().value(QStringLiteral("on")).toBool()
            && item->property("padding").toDouble() > 0) {
            words_as_output = true;
        }
    }
    CHECK(words_as_output);
    // And the shape item behind them takes the frame's effects.
    bool frame_with_effects = false;
    for (QQuickItem* item : seventhslide::test::visible_items_of(
             qobject_cast<QQuickItem*>(w.dialog), "seventhslide::app::FreeformShapeItem")) {
        const auto effects = item->property("effects").toMap();
        if (effects.value(QStringLiteral("glow")).toMap().value(QStringLiteral("on")).toBool()) {
            frame_with_effects = true;
        }
    }
    CHECK(frame_with_effects);
}

TEST_CASE("The ribbon and sidebar set a text box's paragraphs, drawn as the output draws them",
          "[ui][slide-editor][paragraph]")
{
    EditorWindow w;
    w.open_freeform("Card");
    REQUIRE(w.objects()->insert(QStringLiteral("text")).isEmpty());
    const auto id = w.selection().value(QStringLiteral("id")).toString();
    REQUIRE(w.objects()->setText(id, QStringLiteral("One\nTwo"), false, false, false).isEmpty());

    w.pick("ribbonBullets", QStringLiteral("number"));
    w.commit("sideSpaceBefore", 8);
    w.commit("sideSpaceAfter", 4);
    w.commit("sideIndent", 24);
    w.toggle("sideAutofit");
    w.commit("sideMarginLeft", 10);
    w.commit("sideMarginBottom", 6);

    const QVariantMap sel = w.selection();
    CHECK(sel.value(QStringLiteral("bullets")).toString() == QStringLiteral("number"));
    CHECK(sel.value(QStringLiteral("space_before")).toDouble() == 8);
    CHECK(sel.value(QStringLiteral("space_after")).toDouble() == 4);
    CHECK(sel.value(QStringLiteral("indent")).toDouble() == 24);
    CHECK(sel.value(QStringLiteral("autofit")).toString() == QStringLiteral("shrink"));
    CHECK(sel.value(QStringLiteral("margin_left")).toDouble() == 10);
    CHECK(sel.value(QStringLiteral("margin_bottom")).toDouble() == 6);
    CHECK(sel.value(QStringLiteral("paragraph_set")).toBool());

    // The strip's lit item, clicked again, takes the bullets off.
    w.pick("ribbonBullets", QStringLiteral("number"));
    CHECK(w.selection().value(QStringLiteral("bullets")).toString() == QStringLiteral("none"));

    bool words_as_output = false;
    for (QQuickItem* item : seventhslide::test::visible_items_of(
             qobject_cast<QQuickItem*>(w.dialog), "seventhslide::app::OverlayTextItem")) {
        const auto paragraph = item->property("paragraph").toMap();
        if (paragraph.value(QStringLiteral("indent")).toDouble() == 24) {
            words_as_output = true;
        }
    }
    CHECK(words_as_output);
}

TEST_CASE("The sidebar crops, recolours and fades a picture, and fills a shape with one",
          "[ui][slide-editor][picture]")
{
    EditorWindow w;
    w.open_freeform("Card");
    REQUIRE(w.objects()->insert(QStringLiteral("image"), {{QStringLiteral("filename"), QStringLiteral("sunrise.png")}}).isEmpty());
    REQUIRE(w.state->property("selType").toString() == QStringLiteral("image"));

    w.commit("sideCropLeft", 25);
    w.commit("sideCropBottom", 10);
    w.pick("sideRecolor", QStringLiteral("tint"));
    w.swatch("sideTint", "#ffcc00");
    w.commit("sidePictureTransparency", 40);
    QVariantMap sel = w.selection();
    CHECK_THAT(sel.value(QStringLiteral("crop_left")).toDouble(), WithinAbs(0.25, 1e-9));
    CHECK_THAT(sel.value(QStringLiteral("crop_bottom")).toDouble(), WithinAbs(0.10, 1e-9));
    CHECK(sel.value(QStringLiteral("recolor")).toString() == QStringLiteral("tint"));
    CHECK(sel.value(QStringLiteral("tint")).toString() == QStringLiteral("#ffcc00"));
    CHECK_THAT(sel.value(QStringLiteral("picture_transparency")).toDouble(), WithinAbs(0.4, 1e-9));
    CHECK(w.shown("sideCropLeft").toInt() == 25);
    CHECK(w.shown("sidePictureTransparency").toInt() == 40);
    // The stage's picture item carries the adjustments whole.
    bool picture_item = false;
    for (QQuickItem* item : seventhslide::test::visible_items_of(
             qobject_cast<QQuickItem*>(w.dialog), "seventhslide::app::FreeformPictureItem")) {
        if (item->property("picture").toMap().value(QStringLiteral("recolor")).toString()
            == QStringLiteral("tint")) {
            picture_item = true;
        }
    }
    CHECK(picture_item);

    // A shape filled with a picture: a file chosen for a fill, once in the
    // library, sets it.
    REQUIRE(w.objects()->insert(QStringLiteral("shape"), {{QStringLiteral("shapeKind"), QStringLiteral("ellipse")}}).isEmpty());
    const QVariantMap pending{{QStringLiteral("use"), QStringLiteral("fill")},
                              {QStringLiteral("objectId"), w.selection().value(QStringLiteral("id"))}};
    REQUIRE(QMetaObject::invokeMethod(w.state, "useImportedMedia", Q_ARG(QVariant, pending),
                                      Q_ARG(QVariant, QStringLiteral("ocean.png"))));
    w.pick("sideFillPictureFit", QStringLiteral("contain"));
    sel = w.selection();
    CHECK(sel.value(QStringLiteral("fill_kind")).toString() == QStringLiteral("picture"));
    CHECK(sel.value(QStringLiteral("fill_picture_file")).toString() == QStringLiteral("ocean.png"));
    CHECK(sel.value(QStringLiteral("fill_picture_fit")).toString() == QStringLiteral("contain"));
    CHECK(w.shown("sideFillKind").toString() == QStringLiteral("picture"));
}

// The sidebar's Change… and Choose… ask for a file from disk, not the
// library; once imported it lands on the object it was chosen for. A
// Freeform picture's Change… changes that picture, adding none.
TEST_CASE("The sidebar's file buttons ask for a file that lands on the selection",
          "[ui][slide-editor][import]")
{
    EditorWindow w;
    w.open_freeform("Files");
    QSignalSpy asked(w.state, SIGNAL(mediaFileAsked(QString)));
    const auto click = [&](const char* name) {
        REQUIRE(QMetaObject::invokeMethod(w.control(name), "clicked"));
        REQUIRE(QMetaObject::invokeMethod(w.control("mediaFileDialog"), "close"));
        REQUIRE_FALSE(asked.isEmpty());
        return asked.takeLast().at(0).toString();
    };
    const auto imported = [&](const char* use, const char* filename) {
        const QVariantMap pending{{QStringLiteral("use"), QString::fromLatin1(use)},
                                  {QStringLiteral("objectId"), w.selection().value(QStringLiteral("id"))}};
        REQUIRE(QMetaObject::invokeMethod(w.state, "useImportedMedia", Q_ARG(QVariant, pending),
                                          Q_ARG(QVariant, QString::fromLatin1(filename))));
    };
    const auto count = [&] { return w.objects()->canvasObjects().size(); };

    REQUIRE(w.objects()->insert(QStringLiteral("image"), {{QStringLiteral("filename"), QStringLiteral("a.png")}}).isEmpty());
    QTest::qWait(0);
    const auto pictures = count();
    CHECK(click("sidePictureChange") == QStringLiteral("element"));
    imported("element", "b.png");
    CHECK(w.selection().value(QStringLiteral("filename")).toString() == QStringLiteral("b.png"));
    CHECK(count() == pictures);

    REQUIRE(w.objects()->insert(QStringLiteral("video"), {{QStringLiteral("filename"), QStringLiteral("a.mp4")}}).isEmpty());
    QTest::qWait(0);
    CHECK(click("sideVideoChange") == QStringLiteral("video_file"));
    imported("video_file", "b.mp4");
    CHECK(w.selection().value(QStringLiteral("filename")).toString() == QStringLiteral("b.mp4"));
    CHECK(click("sideVideoPoster") == QStringLiteral("poster"));
    imported("poster", "still.png");
    CHECK(w.selection().value(QStringLiteral("video_poster")).toString() == QStringLiteral("still.png"));
    const auto video = w.selection().value(QStringLiteral("id")).toString();

    // A file landing while a box is typed in waits for the typing to end,
    // then goes onto its object; the box stays selected.
    const QVariantMap pending{{QStringLiteral("use"), QStringLiteral("poster")},
                              {QStringLiteral("objectId"), video},
                              {QStringLiteral("slideId"), w.edit()->slides()->property("currentId")}};
    REQUIRE(QMetaObject::invokeMethod(w.state, "insert", Q_ARG(QVariant, QStringLiteral("text")),
                                      Q_ARG(QVariant, QVariant())));
    REQUIRE(w.state->property("textEditing").toBool());
    const auto box = w.selection().value(QStringLiteral("id")).toString();
    REQUIRE(QMetaObject::invokeMethod(w.state, "useImportedMedia", Q_ARG(QVariant, pending),
                                      Q_ARG(QVariant, QStringLiteral("later.png"))));
    CHECK(w.selection().value(QStringLiteral("id")).toString() == box);
    REQUIRE(QMetaObject::invokeMethod(w.state, "endTextEdit", Q_ARG(QVariant, true)));
    CHECK(w.selection().value(QStringLiteral("id")).toString() == box);
    w.objects()->selectObject(video);
    CHECK(w.selection().value(QStringLiteral("video_poster")).toString() == QStringLiteral("later.png"));
}

TEST_CASE("A picture not here is held a place on the stage, saying how far it has come",
          "[ui][slide-editor][picture][sync]")
{
    EditorWindow w;
    w.open_freeform("Card");
    REQUIRE(w.objects()->insert(QStringLiteral("image"), {{QStringLiteral("filename"), QStringLiteral("late.png")}}).isEmpty());
    QTest::qWait(20);
    auto* stage = qobject_cast<QQuickItem*>(w.dialog);
    const auto holders = [&] { return seventhslide::test::visible_qml_items_of(stage, "PicturePlaceholder"); };
    REQUIRE(holders().size() == 1);
    CHECK(seventhslide::test::visible_text_under(holders().front(), QStringLiteral("Picture missing")) != nullptr);

    using seventhslide::app::FileArrival;
    w.facade.media()->set_arrivals({{"images/late.png", {FileArrival::State::Downloading, 1, 4}}});
    QTest::qWait(20);
    REQUIRE(holders().size() == 1);
    CHECK(seventhslide::test::visible_text_under(holders().front(), QStringLiteral("Downloading 25%")) != nullptr);
    CHECK(seventhslide::test::visible_text_under(holders().front(), QStringLiteral("Picture missing")) == nullptr);

    // Come: the picture in its place.
    const auto images = w.facade.media_root() / "images";
    std::filesystem::create_directories(images);
    QImage picture(16, 9, QImage::Format_RGB32);
    picture.fill(Qt::red);
    REQUIRE(picture.save(QString::fromStdString((images / "late.png").string())));
    w.facade.media_arrived({"images/late.png"});
    QTest::qWait(20);
    CHECK(holders().isEmpty());
}

TEST_CASE("The stage reads a slide's picture at the size it shows, off the window's thread",
          "[ui][slide-editor][picture]")
{
    EditorWindow w;
    w.open_freeform("Card");
    const auto images = w.facade.media_root() / "images";
    std::filesystem::create_directories(images);
    for (const auto& [file, colour] : {std::pair{"big.png", Qt::blue}, std::pair{"other.png", Qt::green}}) {
        QImage picture(4000, 3000, QImage::Format_RGB32);
        picture.fill(colour);
        REQUIRE(picture.save(QString::fromStdString((images / file).string())));
    }
    w.facade.media_arrived({"images/big.png", "images/other.png"});

    constexpr int k_ready = 1;  // Image.Ready
    constexpr int k_loading = 2;
    auto* picture = w.dialog->findChild<QQuickItem*>(QStringLiteral("stageBackgroundImage"));
    REQUIRE(picture != nullptr);
    const auto status = [&] { return picture->property("status").toInt(); };
    // Read, and if not, what it was given and how far it got (0 none,
    // 2 loading, 3 an error), for the check after to say.
    const auto ready = [&] {
        const bool read = QTest::qWaitFor([&] { return status() == k_ready; }, 10000);
        UNSCOPED_INFO("picture status " << status() << ", source '"
                      << picture->property("source").toUrl().toString().toStdString() << "', "
                      << picture->width() << "x" << picture->height());
        return read;
    };
    REQUIRE(w.edit()->slides()->setBackground(QStringLiteral("image"), QStringLiteral("big.png")).isEmpty());
    CHECK(picture->property("asynchronous").toBool());
    REQUIRE(ready());
    CHECK(picture->isVisible());
    // Read to cover the stage, not at its own 4000 x 3000.
    const qreal dpr = picture->window()->devicePixelRatio();
    CHECK(picture->implicitWidth() <= std::ceil(picture->width() * dpr) + 1);
    CHECK(picture->implicitHeight() < 3000);

    // Zoomed, it is read again at the new size, and shows meanwhile.
    w.state->setProperty("zoom", 2.0);
    CHECK(status() == k_loading);
    CHECK(picture->isVisible());
    REQUIRE(ready());
    CHECK(picture->implicitWidth() > std::ceil(picture->width() * dpr / 2) + 1);

    // Another slide's picture: none shows until it is read.
    REQUIRE(w.edit()->slides()->insert().isEmpty());
    REQUIRE(w.edit()->slides()->setBackground(QStringLiteral("image"), QStringLiteral("other.png")).isEmpty());
    REQUIRE(ready());
    w.state->setProperty("zoom", 1.5);
    REQUIRE(ready());
    w.edit()->slides()->select(0);
    CHECK(status() == k_loading);
    CHECK_FALSE(picture->isVisible());
    REQUIRE(ready());
    CHECK(picture->isVisible());
}

TEST_CASE("The sidebar's Notes tab writes the slide's speaker notes as they are typed",
          "[ui][slide-editor][notes]")
{
    EditorWindow w;
    w.open_freeform("Card");
    CHECK(w.control("sideTabs")->property("visible").toBool());
    w.state->setProperty("sidebarTab", QStringLiteral("notes"));
    CHECK(w.control("sideNotesPane")->property("visible").toBool());

    QObject* notes = w.control("sideNotes");
    REQUIRE(QMetaObject::invokeMethod(notes, "forceActiveFocus"));
    notes->setProperty("text", QStringLiteral("Pause here for the offering."));
    CHECK(w.edit()->slides()->property("notes").toString()
          == QStringLiteral("Pause here for the offering."));
    // Another slide shows its own (none); back again, the words are there.
    REQUIRE(w.edit()->slides()->insert().isEmpty());
    CHECK(notes->property("text").toString().isEmpty());
    w.edit()->slides()->select(0);
    CHECK(notes->property("text").toString() == QStringLiteral("Pause here for the offering."));

    // A theme has no notes, so no tabs.
    EditorWindow t;
    t.open_theme("song");
    CHECK_FALSE(t.control("sideTabs")->property("visible").toBool());
}

TEST_CASE("A theme's boxes take a fill too; its media areas do not", "[ui][slide-editor][fill]")
{
    EditorWindow w;
    w.open_theme("song");
    // The lyrics box is a text box: a band behind the words is a fill.
    w.objects()->selectObject(QStringLiteral("te_lyrics"));
    REQUIRE(w.state->property("selBinding").toString() == QStringLiteral("lyrics"));
    CHECK(w.control("sideFillSection")->property("visible").toBool());
    CHECK(w.control("sideLineSection")->property("visible").toBool());
    w.pick("sideFillKind", QStringLiteral("solid"));
    w.swatch("sideFill", "#80000000");
    CHECK(w.selection().value(QStringLiteral("fill")).toString() == QStringLiteral("#80000000"));
    CHECK(w.selection().value(QStringLiteral("fill_kind")).toString() == QStringLiteral("solid"));

    // A picture area shows a picture, not a frame. A media theme opens on it.
    w.open_theme("media");
    bool saw_area = false;
    for (const auto& obj : w.objects()->canvasObjects()) {
        if (obj.toMap().value(QStringLiteral("binding")).toString() == QStringLiteral("image_area")) {
            w.objects()->selectObject(obj.toMap().value(QStringLiteral("id")).toString());
            saw_area = true;
        }
    }
    REQUIRE(saw_area);
    CHECK_FALSE(w.control("sideFillSection")->property("visible").toBool());
}

TEST_CASE("A lyrics box's active line and other lines take types of their own", "[ui][slide-editor][lines]")
{
    EditorWindow w;
    w.open_theme("song");
    w.objects()->selectObject(QStringLiteral("te_lyrics"));
    REQUIRE(w.state->property("selBinding").toString() == QStringLiteral("lyrics"));
    const auto el = [&w](const char* name) { return w.selection().value(QString::fromLatin1(name)); };

    // The sections are there once lines follow.
    w.edit()->theme()->setSetting(QStringLiteral("follow_lines"), 0);
    CHECK_FALSE(w.control("sideActiveLineSection")->property("visible").toBool());
    w.edit()->theme()->setSetting(QStringLiteral("follow_lines"), 2);
    CHECK(w.control("sideActiveLineSection")->property("visible").toBool());
    CHECK(w.control("sideOtherLinesSection")->property("visible").toBool());

    // Until a line has a type of its own, the controls show the box's; the
    // other lines start dimmed.
    CHECK(w.shown("sideActiveSize").toInt() == el("font_size").toInt());
    CHECK(w.shown("sideActiveColor").toString() == el("font_color").toString());
    CHECK(w.shown("sideOthersFont").toString().isEmpty());
    CHECK(w.shown("sideOthersColor").toString() == QStringLiteral("#888888"));

    w.pick("sideActiveFont", QStringLiteral("Liberation Serif"));
    w.commit("sideActiveSize", 64);
    w.strip("sideActiveStyle", "bold");
    w.strip("sideOthersStyle", "italic");
    w.swatch("sideOthersColor", "#777777");
    CHECK(el("active_line_family").toString() == QStringLiteral("Liberation Serif"));
    CHECK(el("active_line_size").toInt() == 64);
    CHECK(el("active_line_bold").toBool());
    CHECK(el("active_line_italic").isNull());
    CHECK(el("other_lines_italic").toBool());
    CHECK(el("other_lines_color").toString() == QStringLiteral("#777777"));
    CHECK(el("other_lines_size").toInt() == 0);
    CHECK(w.shown("sideActiveSize").toInt() == 64);
    CHECK(w.shown("sideOthersSize").toInt() == el("font_size").toInt());

    // Same as box hands the family back; a second press turns the flag off.
    w.pick("sideActiveFont", QStringLiteral(""));
    CHECK(el("active_line_family").toString().isEmpty());
    w.strip("sideActiveStyle", "bold");
    CHECK(el("active_line_bold").toBool() == false);
    CHECK_FALSE(el("active_line_bold").isNull());
}

TEST_CASE("A text theme is designed per content type", "[ui][slide-editor][contexts]")
{
    EditorWindow w;
    w.open_theme("media");
    CHECK(w.edit()->contexts()
          == QStringList{QStringLiteral("image"), QStringLiteral("video"), QStringLiteral("web"),
                         QStringLiteral("live")});
    const auto transitions = [&w] {
        return w.edit()->theme()->settings().value(QStringLiteral("transitions")).toMap();
    };
    const auto type_of = [&](const char* ctx) {
        return transitions().value(QString::fromLatin1(ctx)).toMap().value(QStringLiteral("type")).toString();
    };

    // The slide change set in the Video context is the Video context's alone.
    w.edit()->setContext(QStringLiteral("video"));
    REQUIRE(w.state->setProperty("ribbonTab", QStringLiteral("transitions")));
    w.pick("ribbonSlideChange", QStringLiteral("fade_through_black"));
    w.commit("ribbonSlideChangeMs", 900);
    CHECK(type_of("video") == QStringLiteral("fade_through_black"));
    CHECK(transitions().value(QStringLiteral("video")).toMap().value(QStringLiteral("ms")).toInt() == 900);
    CHECK(type_of("image") == QStringLiteral("cut"));
    // A text theme has no background fade of its own: that is the background theme's.
    CHECK_FALSE(w.control("ribbonBgFade")->property("visible").toBool());
    // A moving one comes from a side.
    CHECK_FALSE(w.control("ribbonSlideChangeDirection")->property("enabled").toBool());
    w.pick("ribbonSlideChange", QStringLiteral("push"));
    CHECK(w.control("ribbonSlideChangeDirection")->property("enabled").toBool());
    w.pick("ribbonSlideChangeDirection", QStringLiteral("up"));
    CHECK(transitions().value(QStringLiteral("video")).toMap().value(QStringLiteral("direction")).toString()
          == QStringLiteral("up"));
    CHECK(type_of("video") == QStringLiteral("push"));
    w.pick("ribbonSlideChange", QStringLiteral("fade_through_black"));

    // A Media theme holds no audio settings: an audio item's fades are each
    // output's (its General page).
    CHECK_FALSE(w.edit()->theme()->settings().contains(QStringLiteral("audio")));
    // A picture area goes only where pictures play.
    bool image_area = false;
    for (const auto& kind : w.edit()->theme()->insertKinds()) {
        image_area = image_area || kind.toMap().value(QStringLiteral("id")).toString() == QStringLiteral("image_area");
    }
    CHECK_FALSE(image_area);

    // Saved and opened again, each context keeps its own.
    REQUIRE(w.edit()->save().isEmpty());
    w.open_theme("media");
    CHECK(type_of("video") == QStringLiteral("fade_through_black"));
    CHECK(type_of("image") == QStringLiteral("cut"));

    // A song theme's own contexts: a title slide changes as a song slide does.
    w.open_theme("song");
    CHECK(w.edit()->contexts() == QStringList{QStringLiteral("song"), QStringLiteral("titles")});
    w.edit()->setContext(QStringLiteral("titles"));
    CHECK_FALSE(w.control("ribbonSlideChange")->property("enabled").toBool());
}

TEST_CASE("The ribbon and sidebar edit a theme element", "[ui][slide-editor]")
{
    EditorWindow w;
    w.open_theme("song");
    w.objects()->selectObject(QStringLiteral("te_lyrics"));
    REQUIRE(w.state->property("hasSel").toBool());
    REQUIRE(w.state->property("selBinding").toString() == QStringLiteral("lyrics"));

    w.commit("sideX", 200);
    w.commit("ribbonFontSize", 60);
    w.strip("ribbonAlignStrip", "left");
    w.commit("sideOpacity", 50);

    const auto el = [&w](const char* name) { return w.selection().value(QString::fromLatin1(name)); };
    CHECK(el("x").toInt() == 200);
    CHECK(el("font_size").toInt() == 60);
    CHECK(el("align").toString() == QStringLiteral("left"));
    CHECK_THAT(el("opacity").toDouble(), WithinAbs(0.5, 1e-9));
    CHECK(w.shown("ribbonFontSize").toInt() == 60);

    // The element's own fields, by the names the sections write them with.
    w.pick("sideRule", QStringLiteral("first"));
    w.toggle("sideShowChords");
    CHECK(el("rule").toString() == QStringLiteral("first"));
    CHECK(el("show_chords").toBool());
    CHECK_FALSE(el("no_such_field").isValid());

    // A theme-wide setting, from the Lines section.
    w.commit("sideFollowLines", 2);
    CHECK(w.edit()->theme()->settings().value(QStringLiteral("follow_lines")).toInt() == 2);
    CHECK(w.edit()->property("dirty").toBool());
}

TEST_CASE("The sidebar sets a Freeform indicator's label gap", "[ui][slide-editor]")
{
    EditorWindow w;
    w.open_freeform("Card");
    auto* cues = w.edit()->cues();
    // Two labelled cues, so the indicator shows two labels with a gap between.
    REQUIRE(cues->setLineControl(true).isEmpty());
    cues->select(0);
    REQUIRE(cues->setVerse(QStringLiteral("Verse"), 1).isEmpty());
    REQUIRE(cues->insert(QStringLiteral("Chorus line")).isEmpty());
    cues->select(1);
    REQUIRE(cues->setVerse(QStringLiteral("Chorus"), 1).isEmpty());
    REQUIRE(w.objects()->insert(QStringLiteral("indicator")).isEmpty());
    REQUIRE(w.state->property("selType").toString() == QStringLiteral("indicator"));
    CHECK_THAT(w.shown("sideIndicatorGap").toDouble(), WithinAbs(1.0, 1e-9));

    w.commit("sideIndicatorGap", 0.6);

    CHECK_THAT(w.selection().value(QStringLiteral("indicator_gap")).toDouble(), WithinAbs(0.6, 1e-9));
    CHECK_THAT(w.shown("sideIndicatorGap").toDouble(), WithinAbs(0.6, 1e-9));
    // The stage draws the new gap: a 60% space between the labels.
    const auto objects = w.objects()->canvasObjects();
    REQUIRE(objects.size() == 1);
    const auto text = objects.front().toMap().value(QStringLiteral("text")).toString();
    CHECK(text.contains(QStringLiteral("C1")));
    CHECK(text.contains(QStringLiteral("<size=60> </size>")));
}

TEST_CASE("The sidebar sets a theme indicator's label gap", "[ui][slide-editor]")
{
    EditorWindow w;
    w.open_theme("song");
    // The stock themes have no indicator; insert one the way the Insert tab does.
    REQUIRE(QMetaObject::invokeMethod(w.state, "insert", Q_ARG(QVariant, QStringLiteral("indicator")),
                                      Q_ARG(QVariant, QString())));
    REQUIRE(w.state->property("selType").toString() == QStringLiteral("indicator"));
    CHECK_THAT(w.shown("sideIndicatorGap").toDouble(), WithinAbs(1.0, 1e-9));

    w.commit("sideIndicatorGap", 2.5);

    CHECK_THAT(w.selection().value(QStringLiteral("indicator_gap")).toDouble(), WithinAbs(2.5, 1e-9));
    CHECK_THAT(w.shown("sideIndicatorGap").toDouble(), WithinAbs(2.5, 1e-9));
}

TEST_CASE("The Fields window types a token at the caret and makes a field of a selection",
          "[ui][slide-editor][fields]")
{
    EditorWindow w;
    w.open_freeform("Card");
    REQUIRE(w.objects()->insert(QStringLiteral("text")).isEmpty());
    const auto id = w.selection().value(QStringLiteral("id")).toString();
    REQUIRE(w.objects()->setText(id, QStringLiteral("Welcome"), false, false, false).isEmpty());
    REQUIRE(w.edit()->fields()->add(QStringLiteral("speaker")).isEmpty());
    REQUIRE(w.edit()->fields()->set(QStringLiteral("speaker"), QStringLiteral("Dan")).isEmpty());

    // Typing into the box, the caret at its end; the window opens over it.
    REQUIRE(QMetaObject::invokeMethod(w.state, "beginTextEdit", Q_ARG(QVariant, QVariant(w.selection()))));
    REQUIRE(w.state->property("textEditing").toBool());
    QTest::qWait(50);  // the stage's edit loads the words and takes the caret
    QObject* text_edit = w.control("stageTextEdit");
    REQUIRE(text_edit->setProperty("cursorPosition", 7));
    REQUIRE(QMetaObject::invokeMethod(w.state, "fieldsAsked"));
    QObject* fields = nullptr;
    for (QObject* o : w.dialog->findChildren<QObject*>()) {
        if (QByteArray(o->metaObject()->className()).startsWith("FieldsWindow_QMLTYPE_")) {
            fields = o;
        }
    }
    REQUIRE(fields != nullptr);
    REQUIRE(fields->property("open").toBool());
    REQUIRE(w.state->property("textEditing").toBool());

    // The row's + puts the token at the caret; the box is still being edited.
    QObject* list = w.control("fieldsList");
    REQUIRE(list->property("count").toInt() == 1);
    QQuickItem* row = nullptr;
    REQUIRE(QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, row), Q_ARG(int, 0)));
    REQUIRE(row != nullptr);
    QObject* plus = row->findChild<QObject*>(QStringLiteral("fieldInsert"));
    REQUIRE(plus->property("enabled").toBool());
    REQUIRE(QMetaObject::invokeMethod(plus, "clicked"));
    CHECK(w.state->property("textEditDraft").toString() == QStringLiteral("Welcome{speaker}"));
    // The split arrow's other form hides the line when the field is empty.
    REQUIRE(QMetaObject::invokeMethod(fields, "insertToken", Q_ARG(QVariant, QStringLiteral("speaker")),
                                      Q_ARG(QVariant, true)));
    CHECK(w.state->property("textEditDraft").toString() == QStringLiteral("Welcome{speaker}{-speaker}"));

    // From selection: the words selected become a field, their token in their place.
    REQUIRE(QMetaObject::invokeMethod(text_edit, "select", Q_ARG(int, 0), Q_ARG(int, 7)));
    REQUIRE(w.control("fieldsFromSelection")->property("enabled").toBool());
    REQUIRE(QMetaObject::invokeMethod(w.control("fieldsFromSelection"), "clicked"));
    CHECK(w.edit()->fields()->names() == QStringList{QStringLiteral("field-1"), QStringLiteral("speaker")});
    CHECK(w.edit()->fields()->list()->rowCount() == 2);
    CHECK(w.state->property("textEditDraft").toString() == QStringLiteral("{field-1}{speaker}{-speaker}"));
    CHECK(fields->property("selectedName").toString() == QStringLiteral("field-1"));

    // Saved, the box keeps the tokens and the field has the words.
    REQUIRE(QMetaObject::invokeMethod(w.state, "endTextEdit", Q_ARG(QVariant, true)));
    CHECK(w.selection().value(QStringLiteral("text")).toString() == QStringLiteral("{field-1}{speaker}{-speaker}"));
    CHECK(w.edit()->fields()->usage(QStringLiteral("speaker")) == 2);
    CHECK(w.edit()->fields()->usage(QStringLiteral("field-1")) == 1);
}

TEST_CASE("A theme's Fields window lists the service's tokens, read-only", "[ui][slide-editor][fields]")
{
    EditorWindow w;
    w.open_theme("song");
    CHECK_FALSE(w.control("slideEditorFields")->property("visible").toBool());
    REQUIRE(QMetaObject::invokeMethod(w.state, "fieldsAsked"));
    QObject* fields = nullptr;
    for (QObject* o : w.dialog->findChildren<QObject*>()) {
        if (QByteArray(o->metaObject()->className()).startsWith("FieldsWindow_QMLTYPE_")) {
            fields = o;
        }
    }
    REQUIRE(fields != nullptr);
    REQUIRE(fields->property("open").toBool());
    REQUIRE(fields->property("tokensOnly").toBool());
    CHECK_FALSE(w.control("fieldsNew")->property("visible").toBool());

    QObject* list = w.control("fieldsList");
    REQUIRE(list->property("count").toInt() == 8);
    QQuickItem* row = nullptr;
    REQUIRE(QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, row), Q_ARG(int, 0)));
    REQUIRE(row != nullptr);
    CHECK(row->findChild<QObject*>(QStringLiteral("tokenName"))->property("text").toString() == QStringLiteral("song-title"));
    CHECK_FALSE(row->findChild<QObject*>(QStringLiteral("fieldName"))->property("visible").toBool());
    QObject* plus = row->findChild<QObject*>(QStringLiteral("fieldInsert"));
    CHECK_FALSE(plus->property("enabled").toBool());

    // Typing into a static box of the theme, the + puts the token at the caret.
    REQUIRE(QMetaObject::invokeMethod(w.state, "fieldsAsked"));  // closes nothing; the window stays
    REQUIRE(w.objects()->insert(QStringLiteral("text")).isEmpty());
    REQUIRE(QMetaObject::invokeMethod(w.state, "beginTextEdit", Q_ARG(QVariant, QVariant(w.selection()))));
    REQUIRE(w.state->property("textEditing").toBool());
    QTest::qWait(50);
    REQUIRE(plus->property("enabled").toBool());
    REQUIRE(QMetaObject::invokeMethod(plus, "clicked"));
    CHECK(w.state->property("textEditDraft").toString().contains(QStringLiteral("{song-title}")));
}

TEST_CASE("The Fields window adds, values and deletes fields", "[ui][slide-editor][fields]")
{
    EditorWindow w;
    w.open_freeform("Card");
    REQUIRE(QMetaObject::invokeMethod(w.state, "fieldsAsked"));
    QObject* fields = nullptr;
    for (QObject* o : w.dialog->findChildren<QObject*>()) {
        if (QByteArray(o->metaObject()->className()).startsWith("FieldsWindow_QMLTYPE_")) {
            fields = o;
        }
    }
    REQUIRE(fields != nullptr);
    REQUIRE(fields->property("open").toBool());

    REQUIRE(QMetaObject::invokeMethod(w.control("fieldsNew"), "clicked"));
    QTest::qWait(50);  // the list makes its row
    CHECK(w.edit()->fields()->names() == QStringList{QStringLiteral("field-1")});
    CHECK(fields->property("selectedName").toString() == QStringLiteral("field-1"));
    REQUIRE(w.control("slideEditorFields")->property("label").toString() == QStringLiteral("Fields · 1"));

    // The row: its name and value fields, and its + greyed while no box is
    // being typed into. (A list's rows are reached by index, not as children.)
    QObject* list = w.control("fieldsList");
    REQUIRE(list->property("count").toInt() == 1);
    QQuickItem* row = nullptr;
    REQUIRE(QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, row), Q_ARG(int, 0)));
    REQUIRE(row != nullptr);
    CHECK(row->findChild<QObject*>(QStringLiteral("fieldName"))->property("text").toString() == QStringLiteral("field-1"));
    CHECK_FALSE(row->findChild<QObject*>(QStringLiteral("fieldInsert"))->property("enabled").toBool());
    CHECK_FALSE(w.control("fieldsFromSelection")->property("enabled").toBool());

    REQUIRE(QMetaObject::invokeMethod(w.control("fieldsDelete"), "clicked"));
    CHECK(w.edit()->fields()->names().isEmpty());
    CHECK(fields->property("selectedName").toString().isEmpty());
}

TEST_CASE("A Freeform slide comes in by its gallery choice, from its side", "[ui][slide-editor][transition]")
{
    EditorWindow w;
    w.open_freeform("Card");
    REQUIRE(w.state->setProperty("ribbonTab", QStringLiteral("transitions")));
    const auto bg = [&w](const char* key) {
        return w.edit()->slides()->background().value(QString::fromLatin1(key)).toString();
    };
    CHECK(bg("slide_transition_type") == QStringLiteral("cut"));
    w.pick("ribbonSlideTransition", QStringLiteral("wipe"));
    w.pick("ribbonSlideDirection", QStringLiteral("left"));
    w.commit("ribbonSlideTransitionMs", 750);
    CHECK(bg("slide_transition_type") == QStringLiteral("wipe"));
    CHECK(bg("slide_transition_direction") == QStringLiteral("left"));
    CHECK(w.edit()->slides()->background().value(QStringLiteral("slide_transition_ms")).toInt() == 750);
    const auto* slide = w.edit()->freeform_editor().current_slide();
    REQUIRE(slide != nullptr);
    REQUIRE(slide->transition.has_value());
    CHECK(slide->transition->type == seventhslide::model::TransitionType::Wipe);
    CHECK(slide->transition->direction == seventhslide::model::TransitionDirection::Left);
}

TEST_CASE("The Animations tab and the Build pane make a slide's builds", "[ui][slide-editor][builds]")
{
    EditorWindow w;
    w.open_freeform("Card");
    REQUIRE(w.objects()->insert(QStringLiteral("text")).isEmpty());
    const auto first = w.selection().value(QStringLiteral("id")).toString();
    REQUIRE(w.objects()->setText(first, QStringLiteral("Welcome"), false, false, false).isEmpty());
    REQUIRE(w.objects()->insert(QStringLiteral("shape")).isEmpty());
    const auto second = w.selection().value(QStringLiteral("id")).toString();
    REQUIRE(w.state->setProperty("ribbonTab", QStringLiteral("animations")));
    auto* anims = w.edit()->animations();
    const auto list = [anims] { return anims->list(); };
    const auto entry = [&](int i, const char* key) {
        return list().at(i).toMap().value(QString::fromLatin1(key));
    };

    // The gallery's pick on an object with none adds one, on a click.
    w.pick("ribbonAnimationEffect", QStringLiteral("fly_in"));
    REQUIRE(list().size() == 1);
    CHECK(entry(0, "objectId").toString() == second);
    CHECK(entry(0, "effect").toString() == QStringLiteral("fly_in"));
    CHECK(entry(0, "step").toInt() == 1);
    CHECK(w.control("ribbonAnimationDirection")->property("enabled").toBool());
    // The tab edits it.
    w.pick("ribbonAnimationDirection", QStringLiteral("up"));
    w.commit("ribbonAnimationMs", 900);
    w.commit("ribbonAnimationDelay", 200);
    CHECK(entry(0, "direction").toString() == QStringLiteral("up"));
    CHECK(entry(0, "durationMs").toInt() == 900);
    CHECK(entry(0, "delayMs").toInt() == 200);
    // Another pick changes its effect.
    w.pick("ribbonAnimationEffect", QStringLiteral("zoom"));
    REQUIRE(list().size() == 1);
    CHECK(entry(0, "effect").toString() == QStringLiteral("zoom"));
    CHECK_FALSE(w.control("ribbonAnimationDirection")->property("enabled").toBool());

    // The gallery shows one kind at a time: the current one's, or the one
    // switched to.
    const auto shown = [&] {
        return w.control("ribbonAnimationEffect")->property("options").toList().size();
    };
    const auto kind = [&](const char* name) {
        auto* column = qobject_cast<QQuickItem*>(w.control("ribbonAnimationKind"));
        REQUIRE(column != nullptr);
        for (QQuickItem* row : column->childItems()) {
            if (row->objectName() == QString::fromLatin1(name)) {
                REQUIRE(QMetaObject::invokeMethod(row, "clicked"));
                return;
            }
        }
        FAIL("no " << name);
    };
    CHECK(shown() == 6);
    kind("animationKind_emphasis");
    CHECK(shown() == 2);
    kind("animationKind_exit");
    CHECK(shown() == 6);
    CHECK(w.control("ribbonAnimationEffect")->property("options").toList().front().toMap()
              .value(QStringLiteral("value")).toString() == QStringLiteral("disappear"));

    // The words come in after it, by the pane's order.
    w.objects()->selectObject(first);
    w.pick("ribbonAnimationEffect", QStringLiteral("fade"));
    REQUIRE(list().size() == 2);
    w.pick("ribbonAnimationStart", QStringLiteral("after_previous"));
    CHECK(entry(1, "trigger").toString() == QStringLiteral("after_previous"));
    CHECK(entry(1, "step").toInt() == 0);
    CHECK(entry(1, "summary").toString().contains(QStringLiteral("after previous")));

    // The Build tab lists them; picking one selects its object.
    REQUIRE(w.state->setProperty("sidebarTab", QStringLiteral("build")));
    CHECK(w.control("sideBuildPane")->property("visible").toBool());
    REQUIRE(QMetaObject::invokeMethod(w.state, "selectAnimation", Q_ARG(QVariant, list().at(0))));
    CHECK(w.selection().value(QStringLiteral("id")).toString() == second);
    // Reordered by the pane's drag: the words first, and the pick goes
    // with the one picked.
    auto* drag = w.state->property("buildDrag").value<QObject*>();
    REQUIRE(drag != nullptr);
    REQUIRE(QMetaObject::invokeMethod(drag, "moved", Q_ARG(int, 1), Q_ARG(int, 0)));
    CHECK(entry(0, "objectId").toString() == first);
    const auto current = w.state->property("currentAnimation").toMap();
    CHECK(current.value(QStringLiteral("objectId")).toString() == second);
    CHECK(current.value(QStringLiteral("order")).toInt() == 2);

    // Previewed over the stage, and stopped.
    anims->play();
    CHECK(anims->playing());
    QTest::qWait(50);
    CHECK(w.control("stageBuildPreview")->property("visible").toBool());
    anims->stop();
    CHECK_FALSE(anims->playing());

    // Removed.
    REQUIRE(QMetaObject::invokeMethod(w.state, "selectAnimation", Q_ARG(QVariant, list().at(0))));
    REQUIRE(QMetaObject::invokeMethod(w.control("ribbonAnimationRemove"), "clicked"));
    CHECK(list().size() == 1);

    // Saved with the slide: its step is a step on the output.
    REQUIRE(w.edit()->save().isEmpty());
}

TEST_CASE("Insert Video places a video object, set in its sidebar section", "[ui][slide-editor][video]")
{
    EditorWindow w;
    w.open_freeform("Clip");
    REQUIRE(w.state->setProperty("ribbonTab", QStringLiteral("insert")));
    REQUIRE(w.control("ribbonInsertVideo")->property("visible").toBool());
    // The ribbon's Video asks for a video file; once it is in the library it
    // is inserted.
    REQUIRE(QMetaObject::invokeMethod(w.state, "insertVideo", Q_ARG(QVariant, QStringLiteral("clip.mp4"))));
    const auto sel = w.selection();
    REQUIRE(sel.value(QStringLiteral("type")).toString() == QStringLiteral("video"));
    CHECK(sel.value(QStringLiteral("filename")).toString() == QStringLiteral("clip.mp4"));
    CHECK(sel.value(QStringLiteral("video_start")).toString() == QStringLiteral("on_show"));

    w.pick("sideVideoStart", QStringLiteral("on_click"));
    w.toggle("sideVideoLoop");
    w.toggle("sideVideoMute");
    w.pick("sideVideoFit", QStringLiteral("cover"));
    REQUIRE(w.objects()->setSelectedProperty(QStringLiteral("video_poster"), QStringLiteral("still.png"))
                .isEmpty());
    const auto set = w.selection();
    CHECK(set.value(QStringLiteral("video_start")).toString() == QStringLiteral("on_click"));
    CHECK(set.value(QStringLiteral("video_loop")).toBool());
    CHECK(set.value(QStringLiteral("video_mute")).toBool());
    CHECK(set.value(QStringLiteral("object_fit")).toString() == QStringLiteral("cover"));
    CHECK(set.value(QStringLiteral("video_poster")).toString() == QStringLiteral("still.png"));

    // On the slide as set, and saved with it.
    const auto& doc = w.edit()->freeform_editor().document();
    REQUIRE_FALSE(doc.decks.empty());
    REQUIRE_FALSE(doc.decks.front().slides.front().objects.empty());
    const auto& obj = doc.decks.front().slides.front().objects.back();
    CHECK(obj.type == seventhslide::canvas::ObjectType::Video);
    CHECK(obj.video.start == seventhslide::canvas::VideoStart::OnClick);
    REQUIRE(w.edit()->save().isEmpty());
}

TEST_CASE("Draw makes a line, or a shape point by point", "[ui][slide-editor][drawn]")
{
    EditorWindow w;
    w.open_freeform("Lines");
    REQUIRE(w.state->setProperty("ribbonTab", QStringLiteral("insert")));
    // The shape palette ends in the drawing tools (a Freeform's; a theme has none).
    const auto cells = w.control("ribbonInsertShape")->property("cells").toList();
    REQUIRE(std::ranges::any_of(cells, [](const QVariant& cell) {
        return cell.toMap().value(QStringLiteral("id")).toString() == QStringLiteral("draw:polyline");
    }));
    REQUIRE(QMetaObject::invokeMethod(w.state, "startDrawing", Q_ARG(QVariant, QStringLiteral("line"))));
    CHECK(w.control("stageDraw")->property("visible").toBool());
    // A line by its two ends: a box round them, at least 8 px tall.
    const QVariantList ends{QVariantList{100, 100}, QVariantList{500, 100}};
    REQUIRE(QMetaObject::invokeMethod(w.state, "finishDrawing", Q_ARG(QVariant, ends), Q_ARG(QVariant, false)));
    CHECK(w.state->property("drawTool").toString().isEmpty());
    auto sel = w.selection();
    REQUIRE(sel.value(QStringLiteral("shape_kind")).toString() == QStringLiteral("drawn"));
    CHECK_FALSE(sel.value(QStringLiteral("shape_closed")).toBool());
    CHECK(sel.value(QStringLiteral("x")).toDouble() == 100);
    CHECK(sel.value(QStringLiteral("width")).toDouble() == 400);
    CHECK(sel.value(QStringLiteral("height")).toDouble() == 8);
    CHECK_FALSE(w.control("sideFillSection")->property("visible").toBool());
    w.pick("sideLineEnd", QStringLiteral("arrow"));
    CHECK(w.selection().value(QStringLiteral("line_end")).toString() == QStringLiteral("arrow"));

    // A curve, joined up: it fills.
    REQUIRE(QMetaObject::invokeMethod(w.state, "startDrawing", Q_ARG(QVariant, QStringLiteral("curve"))));
    const QVariantList loop{QVariantList{600, 200}, QVariantList{800, 300}, QVariantList{700, 450}};
    REQUIRE(QMetaObject::invokeMethod(w.state, "finishDrawing", Q_ARG(QVariant, loop), Q_ARG(QVariant, true)));
    sel = w.selection();
    CHECK(sel.value(QStringLiteral("shape_closed")).toBool());
    const auto path = sel.value(QStringLiteral("shape_path")).toList();
    REQUIRE(path.size() == 3);
    CHECK(path.front().toList().at(2).toInt() == 1);
    CHECK(w.control("sideFillSection")->property("visible").toBool());
    // Unjoined again.
    w.toggle("sideShapeClosed");
    CHECK_FALSE(w.selection().value(QStringLiteral("shape_closed")).toBool());

    // A click or two in one place draws nothing.
    REQUIRE(QMetaObject::invokeMethod(w.state, "startDrawing", Q_ARG(QVariant, QStringLiteral("polyline"))));
    const QVariantList dot{QVariantList{50, 50}, QVariantList{50.5, 50.5}};
    REQUIRE(QMetaObject::invokeMethod(w.state, "finishDrawing", Q_ARG(QVariant, dot), Q_ARG(QVariant, false)));
    CHECK_FALSE(w.state->property("errorText").toString().isEmpty());
}

TEST_CASE("Insert Table places a table, its cells written in the sidebar", "[ui][slide-editor][table]")
{
    EditorWindow w;
    w.open_freeform("Rota");
    REQUIRE(w.state->setProperty("ribbonTab", QStringLiteral("insert")));
    REQUIRE(QMetaObject::invokeMethod(w.control("ribbonInsertTable"), "clicked"));
    auto sel = w.selection();
    REQUIRE(sel.value(QStringLiteral("type")).toString() == QStringLiteral("table"));
    CHECK(sel.value(QStringLiteral("table_rows")).toInt() == 3);
    CHECK(sel.value(QStringLiteral("table_cells")).toList().size() == 9);
    w.commit("sideTableColumns", 2);
    w.toggle("sideTableBanding");
    sel = w.selection();
    CHECK(sel.value(QStringLiteral("table_columns")).toInt() == 2);
    CHECK(sel.value(QStringLiteral("table_banding")).toBool());
    // A cell, as its field commits it.
    auto* grid = qobject_cast<QQuickItem*>(w.control("sideTableCells"));
    REQUIRE(grid != nullptr);
    QObject* first = nullptr;
    for (QQuickItem* item : grid->childItems()) {
        if (item->property("index").isValid() && item->property("index").toInt() == 0) {
            first = item;
        }
    }
    REQUIRE(first != nullptr);
    REQUIRE(QMetaObject::invokeMethod(first, "committed", Q_ARG(QString, QStringLiteral("Reader"))));
    CHECK(w.selection().value(QStringLiteral("table_cells")).toList().front().toString()
          == QStringLiteral("Reader"));
    // A commit keeps the fields: the one clicked next is still there.
    bool kept = false;
    for (QQuickItem* item : grid->childItems()) {
        kept = kept || item == first;
    }
    CHECK(kept);
}

TEST_CASE("A text box sets its words in columns", "[ui][slide-editor][columns]")
{
    EditorWindow w;
    w.open_freeform("Notes");
    REQUIRE(w.objects()->insert(QStringLiteral("text")).isEmpty());
    w.commit("sideColumns", 2);
    w.commit("sideColumnGap", 30);
    // Text options starts folded.
    REQUIRE(w.state->setProperty("sectionClosed", QVariantMap{}));
    const auto sel = w.selection();
    CHECK(sel.value(QStringLiteral("columns")).toInt() == 2);
    CHECK(sel.value(QStringLiteral("column_gap")).toDouble() == 30);
    CHECK(w.control("sideColumnGap")->property("visible").toBool());
}

TEST_CASE("A drawing in progress goes with its slide", "[ui][slide-editor][drawn]")
{
    EditorWindow w;
    w.open_freeform("Lines");
    REQUIRE(w.edit()->slides()->insert().isEmpty());
    REQUIRE(QMetaObject::invokeMethod(w.state, "startDrawing", Q_ARG(QVariant, QStringLiteral("polyline"))));
    CHECK(w.state->property("drawTool").toString() == QStringLiteral("polyline"));
    w.edit()->slides()->select(0);
    CHECK(w.state->property("drawTool").toString().isEmpty());
}

TEST_CASE("While text is open, alignment and spacing are the caret's paragraph's", "[ui][slide-editor][paragraph]")
{
    EditorWindow w;
    w.open_freeform("Words");
    REQUIRE(w.objects()->insert(QStringLiteral("text")).isEmpty());
    const auto id = w.selection().value(QStringLiteral("id")).toString();
    REQUIRE(w.objects()->setText(id, QStringLiteral("One\nTwo"), false, false, false).isEmpty());
    REQUIRE(w.objects()->setSelectedProperty(QStringLiteral("font_size"), 40).isEmpty());
    QVariant object = w.selection();
    REQUIRE(QMetaObject::invokeMethod(w.state, "beginTextEdit", Q_ARG(QVariant, object)));
    REQUIRE(w.state->property("textEditing").toBool());
    // The caret in the second line.
    REQUIRE(w.state->setProperty("editSelStart", 5));
    REQUIRE(w.state->setProperty("editSelEnd", 5));
    w.strip("ribbonAlignStrip", "right");
    w.commit("sideSpaceBefore", 20);
    QVariant shown;
    REQUIRE(QMetaObject::invokeMethod(w.state, "paragraphValue", Q_RETURN_ARG(QVariant, shown),
                                      Q_ARG(QVariant, QStringLiteral("align")), Q_ARG(QVariant, QStringLiteral("left"))));
    CHECK(shown.toString() == QStringLiteral("right"));
    REQUIRE(QMetaObject::invokeMethod(w.state, "endTextEdit", Q_ARG(QVariant, true)));
    CHECK(w.selection().value(QStringLiteral("text")).toString()
          == QStringLiteral("One\n<p=align:right,before:50>Two"));
    // Closed, the same control is the box's, and takes it off the paragraph.
    w.strip("ribbonAlignStrip", "center");
    const auto sel = w.selection();
    CHECK(sel.value(QStringLiteral("align")).toString() == QStringLiteral("center"));
    CHECK(sel.value(QStringLiteral("text")).toString() == QStringLiteral("One\n<p=before:50>Two"));
}

TEST_CASE("The editor keeps to its size, its sorter wider for cues", "[ui][slide-editor][layout]")
{
    EditorWindow w;
    w.open_freeform("Size");
    QTest::qWait(0);
    // A window larger than the editor's most: it stops there, as tall as a
    // 16:9 slide needs with the sidebar shut, and that is its size either way.
    auto* box = qobject_cast<QQuickItem*>(w.control("slideEditorBox"));
    REQUIRE(box != nullptr);
    CHECK(box->width() == 1320);
    CHECK(box->height() < 880);
    const double tall = box->height();
    auto* stage = qobject_cast<QQuickItem*>(w.control("slideStage"));
    REQUIRE(stage != nullptr);
    const double inset = stage->property("inset").toDouble();
    const double shown_scale = w.state->property("stageScale").toDouble();
    w.state->setProperty("sidebarOpen", false);
    QTest::qWait(0);
    CHECK(box->height() == tall);
    CHECK(w.state->property("stageScale").toDouble() > shown_scale);
    const double slide_h = w.state->property("stageScale").toDouble() * 1080;
    CHECK(stage->height() - slide_h <= 2 * inset + 2);
    w.state->setProperty("sidebarOpen", true);
    QTest::qWait(0);
    CHECK(box->height() == tall);
    auto* sorter = qobject_cast<QQuickItem*>(w.control("slideSorter"));
    REQUIRE(sorter != nullptr);
    CHECK(sorter->width() == 196);
    REQUIRE(w.edit()->cues()->setLineControl(true).isEmpty());
    // The row lays out on the window's next frame (its polish), not at once.
    (void)QTest::qWaitFor([&] { return sorter->width() == 280; }, 1000);
    CHECK(sorter->width() == 280);
}

TEST_CASE("The ribbon's Delete takes away what is selected", "[ui][slide-editor]")
{
    EditorWindow w;
    w.open_freeform("Delete");
    REQUIRE(w.edit()->objects()->insert(QStringLiteral("shape")).isEmpty());
    QTest::qWait(0);
    auto* button = w.control("ribbonDelete");
    CHECK(button->property("enabled").toBool());
    REQUIRE(QMetaObject::invokeMethod(button, "clicked"));
    QTest::qWait(0);
    CHECK_FALSE(w.state->property("hasSel").toBool());
    CHECK_FALSE(button->property("enabled").toBool());
}

namespace {

/// The menu a ribbon dropdown or button opens.
QObject* menu_of(QObject* control)
{
    for (QObject* child : control->findChildren<QObject*>()) {
        if (child->inherits("QQuickPopup")) {
            return child;
        }
    }
    FAIL("no menu");
    return nullptr;
}

}  // namespace

// Escape, or a press elsewhere, shuts a ribbon menu behind the dropdown's
// back: it has to hear it, or the next press on the field only "closes" it.
TEST_CASE("A ribbon menu shut from outside opens at the next press", "[ui][slide-editor]")
{
    EditorWindow w;
    w.open_freeform("Menu");
    QTest::qWait(0);
    auto* font = w.control("ribbonFontFamily");
    QObject* menu = menu_of(font);
    REQUIRE(font->setProperty("open", true));
    QTest::qWait(0);
    REQUIRE(menu->property("visible").toBool());
    REQUIRE(QMetaObject::invokeMethod(menu, "close"));
    QTest::qWait(0);
    CHECK_FALSE(font->property("open").toBool());
}

// Each menu keeps whether it is open (CODING.md: no open-menu flags in
// state), and one open when the window goes goes with it.
TEST_CASE("A ribbon menu is its own and closes with the window", "[ui][slide-editor]")
{
    EditorWindow w;
    const QMetaObject* meta = w.state->metaObject();
    for (int i = 0; i < meta->propertyCount(); ++i) {
        const QString name = QString::fromLatin1(meta->property(i).name());
        INFO(name.toStdString());
        CHECK_FALSE(name.contains(QLatin1String("menu"), Qt::CaseInsensitive));
        CHECK_FALSE(name.contains(QLatin1String("pickerOpen")));
    }
    for (int i = 0; i < meta->methodCount(); ++i) {
        const QString name = QString::fromLatin1(meta->method(i).name());
        INFO(name.toStdString());
        CHECK_FALSE(name.contains(QLatin1String("menu"), Qt::CaseInsensitive));
    }

    w.open_freeform("Menu");
    REQUIRE(w.objects()->insert(QStringLiteral("shape")).isEmpty());
    QTest::qWait(0);
    for (const char* name : {"ribbonFontFamily", "ribbonAlign", "slideEditorOutput"}) {
        INFO(name);
        auto* control = w.control(name);
        QObject* menu = menu_of(control);
        REQUIRE(control->setProperty("open", true));
        QTest::qWait(0);
        REQUIRE(menu->property("visible").toBool());
        w.edit()->cancel();
        QTest::qWait(0);
        CHECK_FALSE(w.dialog->property("visible").toBool());
        CHECK_FALSE(menu->property("visible").toBool());
        CHECK_FALSE(control->property("open").toBool());
        w.open_freeform("Again");
        REQUIRE(w.objects()->insert(QStringLiteral("shape")).isEmpty());
        QTest::qWait(0);
        CHECK_FALSE(menu->property("visible").toBool());
    }
}

// Each of Home's split buttons closes its own menu when an item is picked.
TEST_CASE("A Home menu closes when one of its items is picked", "[ui][slide-editor]")
{
    EditorWindow w;
    w.open_freeform("Arrange");
    REQUIRE(w.objects()->insert(QStringLiteral("shape")).isEmpty());
    QTest::qWait(0);
    for (const char* name : {"ribbonAlign", "ribbonOrder", "ribbonGroup", "ribbonFlip"}) {
        INFO(name);
        auto* control = w.control(name);
        QObject* menu = menu_of(control);
        REQUIRE(control->setProperty("open", true));
        QTest::qWait(0);
        REQUIRE(menu->property("visible").toBool());
        QObject* item = nullptr;
        for (QObject* child : control->findChildren<QObject*>()) {
            if (QString::fromLatin1(child->metaObject()->className()).startsWith(
                    QStringLiteral("MenuItemRow_QMLTYPE_"))) {
                item = child;
                break;
            }
        }
        REQUIRE(item != nullptr);
        REQUIRE(QMetaObject::invokeMethod(item, "triggered"));
        QTest::qWait(0);
        CHECK_FALSE(control->property("open").toBool());
        CHECK_FALSE(menu->property("visible").toBool());
    }
}

TEST_CASE("Clicking a slide in the sorter shows it on the stage", "[ui][slide-editor][sorter]")
{
    EditorWindow w;
    w.open_freeform("Slides");
    REQUIRE(w.objects()->insert(QStringLiteral("shape")).isEmpty());
    REQUIRE(w.edit()->slides()->insert().isEmpty());
    REQUIRE(w.edit()->slides()->property("index").toInt() == 1);
    QTest::qWait(0);
    const auto shown = [&] { return w.state->property("canvasObjects").toList().size(); };
    CHECK(shown() == 0);
    // A real click on the first thumbnail.
    auto* sorter = qobject_cast<QQuickItem*>(w.control("slideSorter"));
    REQUIRE(sorter != nullptr);
    auto* quick = qobject_cast<QQuickWindow*>(w.window);
    REQUIRE(quick != nullptr);
    REQUIRE(QTest::qWaitForWindowExposed(quick));
    const QPointF at = sorter->mapToScene(QPointF(sorter->width() / 2, 40));
    QTest::mouseClick(quick, Qt::LeftButton, Qt::NoModifier, at.toPoint());
    QTest::qWait(0);
    CHECK(w.edit()->slides()->property("index").toInt() == 0);
    CHECK(shown() == 1);
}

// The pointer finds the boxes the stage draws: a click on one picks it, in a
// deck just made and in one opened from the library.
TEST_CASE("A click on a box on the stage picks it", "[ui][slide-editor]")
{
    EditorWindow w;
    const auto id = w.facade.freeformLibrary()->createFreeform(QStringLiteral("Click"));
    REQUIRE(w.edit()->openFreeform(id));
    REQUIRE(w.objects()->insert(QStringLiteral("text")).isEmpty());
    const auto box = w.selection().value(QStringLiteral("id")).toString();
    REQUIRE(w.objects()->setGeometries(seventhslide::test::geometry(box, 400, 300, 800, 300)).isEmpty());
    auto* quick = qobject_cast<QQuickWindow*>(w.window);
    REQUIRE(quick != nullptr);
    REQUIRE(QTest::qWaitForWindowExposed(quick));
    const auto click_box = [&] {
        w.objects()->clearSelection();
        QTest::qWait(0);
        QQuickItem* canvas = nullptr;
        for (auto* item : w.dialog->findChildren<QQuickItem*>()) {
            if (QByteArray(item->metaObject()->className()).startsWith("CanvasStage_QMLTYPE_")) {
                canvas = item;
            }
        }
        REQUIRE(canvas != nullptr);
        const double sc = canvas->width() / 1920.0;
        const QPointF at = canvas->mapToScene(QPointF(800 * sc, 450 * sc));
        QTest::mouseClick(quick, Qt::LeftButton, Qt::NoModifier, at.toPoint());
        QTest::qWait(0);
        return w.selection().value(QStringLiteral("id")).toString();
    };
    CHECK(click_box() == box);
    REQUIRE(w.edit()->save().isEmpty());
    w.edit()->cancel();
    QTest::qWait(0);
    REQUIRE(w.edit()->openFreeform(id));
    QTest::qWait(0);
    CHECK(click_box() == box);
}

// The footer's Cancel asks before unsaved work goes, as Escape does.
TEST_CASE("Cancel asks before throwing away unsaved work", "[ui][slide-editor]")
{
    EditorWindow w;
    w.open_freeform("Cancel");
    REQUIRE(w.objects()->insert(QStringLiteral("text")).isEmpty());
    QObject* confirm = nullptr;
    for (QObject* o : w.dialog->findChildren<QObject*>()) {
        if (o->property("title").toString() == QStringLiteral("Discard your changes?")) {
            confirm = o;
        }
    }
    REQUIRE(confirm != nullptr);
    REQUIRE(QMetaObject::invokeMethod(w.control("slideEditorCancel"), "clicked"));
    QTest::qWait(0);
    CHECK(confirm->property("open").toBool());
    CHECK(w.edit()->property("active").toBool());
    REQUIRE(confirm->setProperty("open", false));

    // Nothing to lose: it closes at once.
    REQUIRE(w.edit()->save().isEmpty());
    w.open_freeform("Clean");
    REQUIRE(QMetaObject::invokeMethod(w.control("slideEditorCancel"), "clicked"));
    QTest::qWait(0);
    CHECK_FALSE(confirm->property("open").toBool());
    CHECK_FALSE(w.edit()->property("active").toBool());
}

// A colour picked for the words being typed gives the keys back to them.
TEST_CASE("Picking a text colour gives the keys back to the words", "[ui][slide-editor]")
{
    EditorWindow w;
    w.open_freeform("Colour");
    auto* quick = qobject_cast<QQuickWindow*>(w.window);
    REQUIRE(quick != nullptr);
    REQUIRE(QTest::qWaitForWindowExposed(quick));
    REQUIRE(w.objects()->insert(QStringLiteral("text")).isEmpty());
    REQUIRE(QMetaObject::invokeMethod(w.state, "beginTextEdit", Q_ARG(QVariant, QVariant(w.selection()))));
    QTest::qWait(50);
    auto* text_edit = qobject_cast<QQuickItem*>(w.control("stageTextEdit"));
    REQUIRE(text_edit != nullptr);
    REQUIRE(text_edit->hasActiveFocus());
    QObject* swatch = w.control("ribbonFontColor");
    REQUIRE(QMetaObject::invokeMethod(swatch, "openPicker"));
    QTest::qWait(50);
    QObject* sheet = nullptr;
    for (QObject* o : swatch->findChildren<QObject*>()) {
        if (o->property("hue").isValid() && o->property("open").toBool()) {
            sheet = o;
        }
    }
    REQUIRE(sheet != nullptr);
    CHECK_FALSE(text_edit->hasActiveFocus());
    REQUIRE(sheet->setProperty("open", false));
    QTest::qWait(0);
    CHECK(text_edit->hasActiveFocus());
}

// A cue just inserted is open for its words: typing goes into it.
TEST_CASE("A cue just inserted takes what is typed", "[ui][slide-editor][cues]")
{
    EditorWindow w;
    w.open_freeform("Cues");
    auto* quick = qobject_cast<QQuickWindow*>(w.window);
    REQUIRE(quick != nullptr);
    REQUIRE(QTest::qWaitForWindowExposed(quick));
    REQUIRE(w.edit()->cues()->setLineControl(true).isEmpty());
    // A line for the slide, with no words standing in for it.
    CHECK(w.edit()->cues()->current().value(QStringLiteral("text")).toString().isEmpty());
    REQUIRE(QMetaObject::invokeMethod(w.control("ribbonInsertCue"), "clicked"));
    QTest::qWait(50);
    REQUIRE(w.state->property("cueInlineRenameIndex").toInt() == 1);
    QTest::keyClick(quick, Qt::Key_H);
    QTest::keyClick(quick, Qt::Key_I);
    QTest::keyClick(quick, Qt::Key_Return);
    QTest::qWait(0);
    CHECK(w.edit()->cues()->current().value(QStringLiteral("text")).toString() == QStringLiteral("hi"));
}

// A background theme is a fill: no tabs for objects it cannot hold.
TEST_CASE("A background theme opens on its fill, without Home and Insert", "[ui][slide-editor][theme]")
{
    EditorWindow w;
    w.open_theme("bg");
    QStringList ids;
    for (const auto& tab : w.dialog->property("tabs").toList()) {
        ids.push_back(tab.toMap().value(QStringLiteral("id")).toString());
    }
    CHECK(ids == QStringList{QStringLiteral("design"), QStringLiteral("transitions")});
    CHECK(w.state->property("ribbonTab").toString() == QStringLiteral("design"));
}

// Words typed into a new box look as they will be kept: plain, not in the type
// of the box typed into before.
TEST_CASE("Typing into a new box does not borrow the last box's type", "[ui][slide-editor]")
{
    EditorWindow w;
    w.open_freeform("Typing");
    auto* quick = qobject_cast<QQuickWindow*>(w.window);
    REQUIRE(quick != nullptr);
    REQUIRE(QTest::qWaitForWindowExposed(quick));
    REQUIRE(w.objects()->insert(QStringLiteral("text")).isEmpty());
    const auto first = w.selection().value(QStringLiteral("id")).toString();
    REQUIRE(w.objects()->setText(first, QStringLiteral("Loud"), false, false, false).isEmpty());
    REQUIRE(QMetaObject::invokeMethod(w.state, "beginTextEdit", Q_ARG(QVariant, QVariant(w.selection()))));
    QTest::qWait(50);
    QObject* text_edit = w.control("stageTextEdit");
    // Coloured and italic from the ribbon, the words selected; then the caret after them.
    REQUIRE(QMetaObject::invokeMethod(text_edit, "select", Q_ARG(int, 0), Q_ARG(int, 4)));
    QTest::qWait(0);
    QVariantMap red;
    red.insert(QStringLiteral("color"), QStringLiteral("#ff0000"));
    red.insert(QStringLiteral("italic"), true);
    QVariant applied;
    REQUIRE(QMetaObject::invokeMethod(w.state, "applySelectionFormat", Q_RETURN_ARG(QVariant, applied),
                                      Q_ARG(QVariant, QVariant(red))));
    REQUIRE(applied.toBool());
    QTest::qWait(50);
    REQUIRE(text_edit->setProperty("cursorPosition", 4));
    REQUIRE(QMetaObject::invokeMethod(w.state, "endTextEdit", Q_ARG(QVariant, true)));
    QTest::qWait(0);

    REQUIRE(w.objects()->insert(QStringLiteral("text")).isEmpty());
    REQUIRE(QMetaObject::invokeMethod(w.state, "beginTextEdit", Q_ARG(QVariant, QVariant(w.selection()))));
    QTest::qWait(50);
    REQUIRE(w.state->property("textEditing").toBool());
    // Nothing the operator typed yet, so nothing for Ctrl+Z to bring back.
    CHECK_FALSE(text_edit->property("canUndo").toBool());
    QTest::keyClick(quick, Qt::Key_X);
    QTest::qWait(0);
    const QString draft = w.state->property("textEditDraft").toString();
    INFO(QByteArray(draft.toUtf8()).toHex().toStdString());
    CHECK(draft.endsWith(QStringLiteral("x")));
    QString html;
    REQUIRE(QMetaObject::invokeMethod(text_edit, "getFormattedText", Q_RETURN_ARG(QString, html), Q_ARG(int, 0),
                                      Q_ARG(int, 1)));
    INFO(html.toStdString());
    CHECK_FALSE(html.contains(QStringLiteral("#ff0000")));
    CHECK_FALSE(html.contains(QStringLiteral("italic")));
    const auto second = w.selection().value(QStringLiteral("id")).toString();
    REQUIRE(QMetaObject::invokeMethod(w.state, "endTextEdit", Q_ARG(QVariant, true)));
    QTest::qWait(0);
    for (const auto& o : w.objects()->canvasObjects()) {
        if (o.toMap().value(QStringLiteral("id")).toString() == second) {
            CHECK(o.toMap().value(QStringLiteral("text")).toString() == QStringLiteral("x"));
        }
    }
}

// Escape ends the typing and keeps it, in a Freeform item as in a theme, as
// a click elsewhere does.
TEST_CASE("Escape in a text box keeps what was typed", "[ui][slide-editor]")
{
    EditorWindow w;
    w.open_freeform("Escape");
    auto* quick = qobject_cast<QQuickWindow*>(w.window);
    REQUIRE(quick != nullptr);
    REQUIRE(QTest::qWaitForWindowExposed(quick));
    REQUIRE(w.objects()->insert(QStringLiteral("text")).isEmpty());
    const auto id = w.selection().value(QStringLiteral("id")).toString();
    REQUIRE(QMetaObject::invokeMethod(w.state, "beginTextEdit", Q_ARG(QVariant, QVariant(w.selection()))));
    QTest::qWait(50);
    REQUIRE(w.state->property("textEditing").toBool());
    QTest::keyClick(quick, Qt::Key_H);
    QTest::keyClick(quick, Qt::Key_I);
    QTest::qWait(0);
    QTest::keyClick(quick, Qt::Key_Escape);
    QTest::qWait(0);
    CHECK_FALSE(w.state->property("textEditing").toBool());
    // The editor is still open: Escape ended the typing, not the window.
    CHECK(w.dialog->property("visible").toBool());
    bool found = false;
    for (const auto& o : w.objects()->canvasObjects()) {
        if (o.toMap().value(QStringLiteral("id")).toString() == id) {
            found = true;
            CHECK(o.toMap().value(QStringLiteral("text")).toString() == QStringLiteral("hi"));
        }
    }
    CHECK(found);
}

// The stage puts a box's words where its vertical alignment says, as the
// output does: top, middle or bottom of a box taller than they are.
TEST_CASE("The stage puts a box's words at its vertical alignment", "[ui][slide-editor]")
{
    EditorWindow w;
    w.open_freeform("Valign");
    REQUIRE(w.objects()->insert(QStringLiteral("text")).isEmpty());
    const auto box = w.selection().value(QStringLiteral("id")).toString();
    REQUIRE(w.objects()->setText(box, QStringLiteral("One line"), false, false, false).isEmpty());
    REQUIRE(w.objects()->setGeometries(seventhslide::test::geometry(box, 200, 100, 1200, 800)).isEmpty());
    auto* quick = qobject_cast<QQuickWindow*>(w.window);
    REQUIRE(quick != nullptr);
    REQUIRE(QTest::qWaitForWindowExposed(quick));
    // Every item under ``root`` in the scene (delegates have no QObject parent).
    const auto items_under = [](QQuickItem* root) {
        QList<QQuickItem*> out;
        QList<QQuickItem*> todo{root};
        while (!todo.isEmpty()) {
            QQuickItem* item = todo.takeLast();
            out.push_back(item);
            todo.append(item->childItems());
        }
        return out;
    };
    // Where the drawn words start, as a share of the box's height: the first
    // row of the box with a light pixel in it (the slide is black).
    const auto first_line_at = [&] {
        QTest::qWait(50);
        for (auto* item : items_under(quick->contentItem())) {
            if (!QByteArray(item->metaObject()->className()).startsWith("StageBox_QMLTYPE_")
                || !item->isVisible()) {
                continue;
            }
            const QImage shot = quick->grabWindow();
            const qreal dpr = shot.devicePixelRatio();
            const QRectF r = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
            const int x0 = int(r.left() * dpr) + 3;
            const int x1 = int(r.right() * dpr) - 3;
            const int y0 = int(r.top() * dpr) + 3;
            const int y1 = int(r.bottom() * dpr) - 3;
            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    if (qGray(shot.pixel(x, y)) > 128) {
                        return (y / dpr - r.top()) / r.height();
                    }
                }
            }
            FAIL("no words drawn");
        }
        FAIL("no stage box");
        return -1.0;
    };
    REQUIRE(w.objects()->setSelectedProperty(QStringLiteral("valign"), QStringLiteral("top")).isEmpty());
    w.objects()->clearSelection();
    CHECK(first_line_at() < 0.1);
    w.objects()->selectObject(box, false);
    REQUIRE(w.objects()->setSelectedProperty(QStringLiteral("valign"), QStringLiteral("bottom")).isEmpty());
    w.objects()->clearSelection();
    CHECK(first_line_at() > 0.8);
    w.objects()->selectObject(box, false);
    REQUIRE(w.objects()->setSelectedProperty(QStringLiteral("valign"), QStringLiteral("middle")).isEmpty());
    w.objects()->clearSelection();
    const double middle = first_line_at();
    CHECK(middle > 0.35);
    CHECK(middle < 0.55);
}

// What the editor costs per thing done, QML and a frame included: each step
// is the edit, the events it posts, and a grab of the window (its sync and
// render). Run with "[.bench]".
TEST_CASE("bench: slide editor", "[.bench]")
{
    EditorWindow w;
    const auto id = w.facade.freeformLibrary()->createFreeform(QStringLiteral("Bench"));
    REQUIRE(w.edit()->openFreeform(id));
    auto* quick = qobject_cast<QQuickWindow*>(w.window);
    REQUIRE(quick != nullptr);
    REQUIRE(QTest::qWaitForWindowExposed(quick));
    const auto settle = [&] {
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        (void)quick->grabWindow();
    };
    const auto time = [&](const char* what, int n, const auto& step) {
        settle();
        QElapsedTimer t;
        t.start();
        for (int i = 0; i < n; ++i) {
            step(i);
            settle();
        }
        std::printf("  %-52s %8.2f ms\n", what, static_cast<double>(t.nsecsElapsed()) / 1e6 / n);
    };

    // A service deck: 40 slides, each four text boxes and two shapes.
    constexpr int k_slides = 40;
    auto* slides = w.edit()->slides();
    for (int s = 0; s < k_slides; ++s) {
        if (s > 0) {
            REQUIRE(slides->insert().isEmpty());
        }
        for (int b = 0; b < 4; ++b) {
            REQUIRE(w.objects()->insert(QStringLiteral("text")).isEmpty());
            REQUIRE(w.objects()
                        ->setText(w.selection().value(QStringLiteral("id")).toString(),
                                  QStringLiteral("**Point %1** of slide %2\nA _second_ line\nA third")
                                      .arg(b)
                                      .arg(s),
                                  false, false, false)
                        .isEmpty());
            REQUIRE(w.objects()->setGeometries(seventhslide::test::geometry(
                        w.selection().value(QStringLiteral("id")).toString(), 80, 60 + b * 240, 1200, 200))
                        .isEmpty());
        }
        for (int k = 0; k < 2; ++k) {
            REQUIRE(w.objects()->insert(QStringLiteral("shape")).isEmpty());
        }
    }
    REQUIRE(w.edit()->save().isEmpty());
    std::printf("\nslide editor, %d slides x 6 objects\n", k_slides);

    time("open the deck", 3, [&](int) { REQUIRE(w.edit()->openFreeform(id)); });

    slides->select(k_slides / 2);
    settle();
    const auto objects = w.objects()->canvasObjects();
    REQUIRE(objects.size() == 6);
    const auto box = objects.back().toMap().value(QStringLiteral("id")).toString();
    QString text_box;
    for (const auto& o : objects) {
        if (o.toMap().value(QStringLiteral("type")).toString() == QStringLiteral("text")) {
            text_box = o.toMap().value(QStringLiteral("id")).toString();
        }
    }
    w.objects()->selectObject(text_box);

    time("a frame with nothing changed", 20, [&](int) {});
    time("pick another slide", 20, [&](int i) { slides->select((k_slides / 2 + 1 + i) % k_slides); });
    slides->select(k_slides / 2);
    w.objects()->selectObject(text_box);
    time("select another object", 20,
         [&](int i) { w.objects()->selectObject(i % 2 ? text_box : box); });
    REQUIRE(w.state->setProperty("sidebarOpen", false));
    time("select another object, the sidebar shut", 20,
         [&](int i) { w.objects()->selectObject(i % 2 ? text_box : box); });
    REQUIRE(w.state->setProperty("sidebarOpen", true));
    w.objects()->selectObject(text_box);
    time("change the box's colour", 20, [&](int i) {
        REQUIRE(w.objects()
                    ->setSelectedProperty(QStringLiteral("font_color"),
                                          i % 2 ? QStringLiteral("#ff0000") : QStringLiteral("#00ff00"))
                    .isEmpty());
    });
    time("move a box (one geometry edit)", 40, [&](int i) {
        if (i == 0) {
            w.objects()->beginGesture();
        }
        REQUIRE(w.objects()
                    ->setGeometries(seventhslide::test::geometry(text_box, 80 + i, 300, 1200, 200))
                    .isEmpty());
    });
    time("  and the gesture's end", 1, [&](int) { w.objects()->endGesture(); });
    time("switch ribbon tab", 20, [&](int i) {
        static const char* tabs[] = {"insert", "design", "transitions", "animations", "home"};
        REQUIRE(w.state->setProperty("ribbonTab", QString::fromLatin1(tabs[i % 5])));
    });
    REQUIRE(QMetaObject::invokeMethod(w.state, "beginTextEdit", Q_ARG(QVariant, QVariant(w.selection()))));
    time("move the caret while typing", 40, [&](int i) {
        REQUIRE(w.state->setProperty("editSelStart", i % 20));
        REQUIRE(w.state->setProperty("editSelEnd", i % 20));
    });
    REQUIRE(QMetaObject::invokeMethod(w.state, "endTextEdit", Q_ARG(QVariant, false)));

    w.open_theme("song");
    settle();
    w.objects()->selectObject(
        w.objects()->canvasObjects().front().toMap().value(QStringLiteral("id")).toString());
    time("text theme: change the font size", 20, [&](int i) {
        REQUIRE(w.objects()->setSelectedProperty(QStringLiteral("font_size"), 60 + (i % 2) * 4).isEmpty());
    });
}

// A box that shows the slide's lines is drawn by the output's own painter,
// which finds a library font by the library's name for it. Qt may know the
// file by another ("EB Garamond" for "EB Garamond 08"): handed that, the
// painter found nothing and drew in the stand-in until the box was opened.
TEST_CASE("A box showing the slide's lines draws in its library font", "[ui][slide-editor][fonts]")
{
    EditorWindow w;
    w.facade.fonts()->set_may_reload([] { return true; });
    seventhslide::app::FontStore store(*w.facade.open_library().database(), w.facade.media_root());
    REQUIRE(store.import_file(seventhslide::test::fixture_font("SSFixtureSansSemibold-Regular.ttf"))
                .error.empty());
    w.facade.fonts()->library_fonts_changed();
    const auto family = QString::fromUtf8(seventhslide::test::k_fixture_semibold.data(),
                                          static_cast<qsizetype>(seventhslide::test::k_fixture_semibold.size()));
    // The case at hand: Qt names the file otherwise.
    REQUIRE(w.facade.fonts()->painted(family) != family);

    w.open_freeform("Linked");
    REQUIRE(w.edit()->cues()->setLineControl(true).isEmpty());
    w.edit()->cues()->select(0);
    REQUIRE(w.edit()->cues()->rename(QStringLiteral("A line of words")).isEmpty());
    REQUIRE(w.objects()->insert(QStringLiteral("text")).isEmpty());
    REQUIRE(w.edit()->cues()->setLinked(true).isEmpty());
    REQUIRE(w.objects()->setSelectedProperty(QStringLiteral("font_family"), family).isEmpty());
    auto* quick = qobject_cast<QQuickWindow*>(w.window);
    REQUIRE(quick != nullptr);
    REQUIRE(QTest::qWaitForWindowExposed(quick));
    QTest::qWait(50);

    QList<QQuickItem*> todo{quick->contentItem()};
    QStringList drawn;
    while (!todo.isEmpty()) {
        QQuickItem* item = todo.takeLast();
        todo.append(item->childItems());
        if (QByteArray(item->metaObject()->className()).endsWith("OverlayTextItem") && item->isVisible()) {
            drawn << item->property("fontFamily").toString();
        }
    }
    CHECK(drawn == QStringList{family});
}

// Open, a box that shows the slide's lines spaces them as the output does:
// room over each for chord names only when the box shows chords.
TEST_CASE("An open box showing the slide's lines keeps their spacing", "[ui][slide-editor]")
{
    EditorWindow w;
    w.open_freeform("Spacing");
    REQUIRE(w.edit()->cues()->setLineControl(true).isEmpty());
    w.edit()->cues()->select(0);
    REQUIRE(w.edit()->cues()->rename(QStringLiteral("First line")).isEmpty());
    REQUIRE(w.edit()->cues()->insert(QStringLiteral("Second line")).isEmpty());
    REQUIRE(w.objects()->insert(QStringLiteral("text")).isEmpty());
    REQUIRE(w.edit()->cues()->setLinked(true).isEmpty());
    auto* quick = qobject_cast<QQuickWindow*>(w.window);
    REQUIRE(quick != nullptr);
    REQUIRE(QTest::qWaitForWindowExposed(quick));
    QObject* text_edit = w.control("stageTextEdit");
    REQUIRE(text_edit != nullptr);

    // Top padding, and the step from the first line to the second.
    const auto open_box = [&] {
        REQUIRE(QMetaObject::invokeMethod(w.state, "beginTextEdit", Q_ARG(QVariant, QVariant(w.selection()))));
        QTest::qWait(50);
        REQUIRE(w.state->property("textEditing").toBool());
        REQUIRE(w.state->property("textEditDraft").toString().startsWith(QStringLiteral("First line\nSecond line")));
        QRectF first;
        QRectF second;
        REQUIRE(QMetaObject::invokeMethod(text_edit, "positionToRectangle", Q_RETURN_ARG(QRectF, first),
                                          Q_ARG(int, 0)));
        REQUIRE(QMetaObject::invokeMethod(text_edit, "positionToRectangle", Q_RETURN_ARG(QRectF, second),
                                          Q_ARG(int, 11)));
        const double top = text_edit->property("topPadding").toDouble();
        REQUIRE(QMetaObject::invokeMethod(w.state, "endTextEdit", Q_ARG(QVariant, false)));
        QTest::qWait(0);
        return std::pair{top, second.y() - first.y()};
    };

    const auto [plain_top, plain_step] = open_box();
    const auto box = w.selection();
    const int px = std::max(8, static_cast<int>(std::lround(box.value(QStringLiteral("font_size")).toDouble()
                                                            * w.state->property("stageScale").toDouble())));
    CHECK(plain_top == 0);
    CHECK_THAT(plain_step, WithinAbs(std::ceil(px * box.value(QStringLiteral("line_height")).toDouble()), 1.0));

    REQUIRE(w.objects()->setSelectedProperty(QStringLiteral("show_chords"), true).isEmpty());
    const auto [chord_top, chord_step] = open_box();
    CHECK(chord_top > 0);
    CHECK(chord_step > plain_step);
}

// What the selection takes and the room its chords need are core's rules,
// read from the session; QML keeps no copy of either.
TEST_CASE("The editor's controls and chord room come from core", "[ui][slide-editor]")
{
    EditorWindow w;
    const QMetaObject* meta = w.state->metaObject();
    for (int i = 0; i < meta->methodCount(); ++i) {
        const QByteArray name = meta->method(i).name();
        INFO(name.toStdString());
        CHECK(name != "typeSupports");
        CHECK(name != "chordRowFactor");
    }

    w.open_freeform("Rules");
    for (const char* kind : {"text", "shape", "clock", "indicator"}) {
        INFO(kind);
        REQUIRE(w.objects()->insert(QString::fromLatin1(kind)).isEmpty());
        QTest::qWait(0);
        const QVariantMap supports = w.objects()->property("supports").toMap();
        REQUIRE(supports.size() == 11);
        for (auto it = supports.cbegin(); it != supports.cend(); ++it) {
            INFO(it.key().toStdString());
            QVariant shown;
            REQUIRE(QMetaObject::invokeMethod(w.state, "selSupports", Q_RETURN_ARG(QVariant, shown),
                                              Q_ARG(QVariant, it.key())));
            CHECK(shown.toBool() == it.value().toBool());
        }
    }
    CHECK(w.objects()->property("supports").toMap().value(QStringLiteral("indicator")).toBool());
    w.objects()->clearSelection();
    for (const auto& on : w.objects()->property("supports").toMap()) {
        CHECK_FALSE(on.toBool());
    }

    // A linked box showing chords: room over its lines as the output makes it.
    REQUIRE(w.edit()->cues()->setLineControl(true).isEmpty());
    w.edit()->cues()->select(0);
    REQUIRE(w.edit()->cues()->rename(QStringLiteral("First line")).isEmpty());
    REQUIRE(w.objects()->insert(QStringLiteral("text")).isEmpty());
    REQUIRE(w.edit()->cues()->setLinked(true).isEmpty());
    REQUIRE(w.objects()->setSelectedProperty(QStringLiteral("show_chords"), true).isEmpty());
    REQUIRE(w.objects()->setSelectedProperty(QStringLiteral("chord_size_pct"), 120).isEmpty());
    const auto box = w.selection();
    seventhslide::layout::FontStyle lyric;
    lyric.family = box.value(QStringLiteral("font_family")).toString().toStdString();
    lyric.size_px = box.value(QStringLiteral("font_size")).toDouble();
    lyric.bold = box.value(QStringLiteral("bold")).toBool();
    lyric.italic = box.value(QStringLiteral("italic")).toBool();
    lyric.letter_spacing = box.value(QStringLiteral("letter_spacing")).toDouble();
    const seventhslide::layout::ChordStyle chords{.size_pct = 120,
                                                  .gap_pct = box.value(QStringLiteral("chord_gap_pct")).toInt()};
    const int room = seventhslide::layout::chord_row_px(w.facade.previews().measurer(), lyric, chords);
    CHECK(room > 0);
    CHECK(w.objects()->property("chordRoom").toInt() == room);

    auto* quick = qobject_cast<QQuickWindow*>(w.window);
    REQUIRE(quick != nullptr);
    REQUIRE(QTest::qWaitForWindowExposed(quick));
    REQUIRE(QMetaObject::invokeMethod(w.state, "beginTextEdit", Q_ARG(QVariant, QVariant(w.selection()))));
    QTest::qWait(50);
    REQUIRE(w.state->property("editChordRoom").toBool());
    const double scale = w.state->property("stageScale").toDouble();
    CHECK(w.control("stageTextEdit")->property("topPadding").toDouble() == std::round(room * scale));
}

// The slide editor's file choosers offer the files the library takes, from
// core's one list (db::media_extensions).
TEST_CASE("The slide editor's file choosers offer what the library takes", "[ui][slide-editor]")
{
    namespace MediaKindQml = seventhslide::app::MediaKindQml;
    EditorWindow w;
    const auto filter = [&](const char* label, MediaKindQml::Kind kind) {
        return QStringList{QStringLiteral("%1 (%2)").arg(QString::fromLatin1(label),
                                                         w.facade.media()->mediaFilePatterns(kind)),
                           QStringLiteral("All files (*)")};
    };
    const QStringList images = filter("Images", MediaKindQml::Image);
    const QStringList videos = filter("Videos", MediaKindQml::Video);
    CHECK(images.front().contains(QStringLiteral("*.svg")));
    CHECK(videos.front().contains(QStringLiteral("*.avi")));
    CHECK_FALSE(videos.front().contains(QStringLiteral("*.m4v")));

    const auto filters = [&](const char* name) {
        return w.control(name)->property("nameFilters").toStringList();
    };
    CHECK(filters("slideBgImageDialog") == images);
    CHECK(filters("slideBgVideoDialog") == videos);
    QObject* insert = w.control("mediaFileDialog");
    REQUIRE(insert->setProperty("forWhat", QStringLiteral("object")));
    CHECK(filters("mediaFileDialog") == images);
    for (const char* use : {"video", "video_file"}) {
        REQUIRE(insert->setProperty("forWhat", QString::fromLatin1(use)));
        CHECK(filters("mediaFileDialog") == videos);
    }
    for (const char* use : {"element", "fill", "poster"}) {
        REQUIRE(insert->setProperty("forWhat", QString::fromLatin1(use)));
        CHECK(filters("mediaFileDialog") == images);
    }
    QObject* asset = w.control("slideAssetDialog");
    REQUIRE(asset->setProperty("video", false));
    CHECK(filters("slideAssetDialog") == images);
    REQUIRE(asset->setProperty("video", true));
    CHECK(filters("slideAssetDialog") == videos);
}

// A text theme's contexts are named and drawn as ContentTypes has their
// kinds: one of each, as the window names what it designs.
TEST_CASE("The slide editor names a theme's contexts from the one table", "[ui][slide-editor]")
{
    EditorWindow w;
    // The media theme: its contexts are every kind of media.
    w.open_theme("media");
    auto* types = w.scene.engine().singletonInstance<QObject*>("SeventhSlide", "ContentTypes");
    REQUIRE(types != nullptr);
    const auto type_of = [&](const QString& key) {
        QVariant out;
        REQUIRE(QMetaObject::invokeMethod(types, "of", Q_RETURN_ARG(QVariant, out), Q_ARG(QVariant, QVariant(key))));
        return out.toMap();
    };
    QObject* picker = w.control("slideEditorContext");
    const QVariantList options = picker->property("options").toList();
    REQUIRE(options.size() == w.edit()->contexts().size());
    QStringList labels;
    for (const QVariant& option : options) {
        const QString key = option.toMap().value(QStringLiteral("value")).toString();
        const QString label = option.toMap().value(QStringLiteral("label")).toString();
        INFO("context " << key.toStdString());
        labels << label;
        w.pick("slideEditorContext", key);
        REQUIRE(w.edit()->context() == key);
        if (key == QStringLiteral("titles")) {
            CHECK(label == QStringLiteral("Title slides"));
            CHECK(picker->property("icon").toString() == QStringLiteral("titles"));
            continue;
        }
        const QVariantMap type = type_of(key);
        REQUIRE_FALSE(type.isEmpty());
        CHECK(label == type.value(QStringLiteral("singular")).toString());
        CHECK(picker->property("icon").toString() == type.value(QStringLiteral("icon")).toString());
    }
    CHECK(labels.contains(QStringLiteral("Image")));
    CHECK(labels.contains(QStringLiteral("Live source")));
}

// What the window asks of the document by a call (how many fields, where
// one is used) follows every change to it, not only the first.
TEST_CASE("The Fields count and a clock on the stage follow what changes", "[ui][slide-editor][fields]")
{
    EditorWindow w;
    w.open_freeform("Follow");
    QObject* button = w.control("slideEditorFields");
    CHECK(button->property("label").toString() == QStringLiteral("Fields · 0"));
    REQUIRE(w.edit()->fields()->add(QStringLiteral("speaker")).isEmpty());
    QTest::qWait(0);
    CHECK(button->property("label").toString() == QStringLiteral("Fields · 1"));
    REQUIRE(w.edit()->property("dirty").toBool());
    REQUIRE(w.edit()->fields()->add(QStringLiteral("title")).isEmpty());
    QTest::qWait(0);
    CHECK(button->property("label").toString() == QStringLiteral("Fields · 2"));
    REQUIRE(w.edit()->fields()->remove(QStringLiteral("title")).isEmpty());
    QTest::qWait(0);
    CHECK(button->property("label").toString() == QStringLiteral("Fields · 1"));

    // A clock with seconds shows another time a second on.
    REQUIRE(w.objects()->insert(QStringLiteral("clock")).isEmpty());
    REQUIRE(w.objects()->setSelectedProperty(QStringLiteral("clock_seconds"), true).isEmpty());
    auto* quick = qobject_cast<QQuickWindow*>(w.window);
    REQUIRE(quick != nullptr);
    REQUIRE(QTest::qWaitForWindowExposed(quick));
    // The stage's boxes are a Repeater's: items, not QObject children.
    const auto shown = [&] {
        QStringList out;
        QList<QQuickItem*> todo{quick->contentItem()};
        while (!todo.isEmpty()) {
            QQuickItem* item = todo.takeLast();
            todo.append(item->childItems());
            if (QByteArray(item->metaObject()->className()).startsWith("StageBox_QMLTYPE_")) {
                const QString html = item->property("contentHtml").toString();
                if (!html.isEmpty()) {
                    out << html;
                }
            }
        }
        return out;
    };
    QTest::qWait(0);
    const QStringList before = shown();
    REQUIRE(before.size() == 1);
    CHECK(QTest::qWaitFor([&] { return shown() != before; }, 3000));
}

// Ctrl and the wheel zoom the stage a tenth a notch, about the pointer: once
// the slide is larger than the stage, the part of it under the pointer stays
// under it (smaller, it stays centred). The wheel alone does not zoom.
TEST_CASE("Ctrl and the wheel zoom the stage about the pointer", "[ui][slide-editor]")
{
    EditorWindow w;
    w.open_freeform("Zoom");
    auto* quick = qobject_cast<QQuickWindow*>(w.window);
    REQUIRE(quick != nullptr);
    REQUIRE(QTest::qWaitForWindowExposed(quick));
    QTest::qWait(0);
    auto* slide = qobject_cast<QQuickItem*>(w.control("stageSlide"));
    REQUIRE(slide != nullptr);
    const auto scale = [&] { return w.state->property("stageScale").toDouble(); };
    const auto zoom = [&] { return w.state->property("zoom").toDouble(); };
    const QPointF at = slide->mapToScene(QPointF(slide->width() * 0.3, slide->height() * 0.3));
    // Where ``at`` falls on the slide, in stage pixels at the scale shown.
    const auto under = [&] { return slide->mapFromScene(at) / scale(); };
    const auto wheel = [&](int notches, Qt::KeyboardModifiers modifiers) {
        QWheelEvent event(at, quick->mapToGlobal(at), QPoint(), QPoint(0, 120 * notches), Qt::NoButton,
                          modifiers, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(quick, &event);
        QTest::qWait(0);
    };
    const double fit = scale();

    wheel(1, Qt::ControlModifier);
    CHECK_THAT(zoom(), WithinAbs(fit * 1.1, 1e-6));
    for (int i = 0; i < 6; ++i) {
        wheel(1, Qt::ControlModifier);
    }
    CHECK_THAT(zoom(), WithinAbs(fit * std::pow(1.1, 7), 1e-6));
    // Larger than the stage both ways now: zooming in and out keeps the spot.
    auto* view = slide->parentItem()->parentItem();
    REQUIRE(slide->width() > view->width());
    REQUIRE(slide->height() > view->height());
    const QPointF spot = under();
    wheel(1, Qt::ControlModifier);
    QPointF moved = (under() - spot) * scale();
    CHECK(std::hypot(moved.x(), moved.y()) <= 1.5);
    wheel(-1, Qt::ControlModifier);
    moved = (under() - spot) * scale();
    CHECK(std::hypot(moved.x(), moved.y()) <= 1.5);

    wheel(-1, Qt::ControlModifier);
    CHECK_THAT(zoom(), WithinAbs(fit * std::pow(1.1, 6), 1e-6));
    const double shown = zoom();
    wheel(1, Qt::NoModifier);
    CHECK(zoom() == shown);

    // Never past the footer's range.
    for (int i = 0; i < 60; ++i) {
        wheel(1, Qt::ControlModifier);
    }
    CHECK(zoom() == w.state->property("maxZoom").toDouble());
    for (int i = 0; i < 80; ++i) {
        wheel(-1, Qt::ControlModifier);
    }
    CHECK(zoom() == w.state->property("minZoom").toDouble());
}

namespace {

/// Whether ``o`` is an instance of the QML type ``type``, or of one built on it.
bool is_qml_type(const QObject* o, const char* type)
{
    const QString prefix = QString::fromLatin1(type) + QStringLiteral("_QMLTYPE_");
    for (const QMetaObject* m = o->metaObject(); m != nullptr; m = m->superClass()) {
        if (QString::fromLatin1(m->className()).startsWith(prefix)) {
            return true;
        }
    }
    return false;
}

/// Every field the sidebar shows reads a key C++ fills, and every number
/// one C++ gives a range: no field falls back to a value of its own.
void check_sidebar_fields(EditorWindow& w)
{
    // Geometry and angles take any number (an angle comes back in one turn).
    static const QStringList unbounded{QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("width"),
                                       QStringLiteral("height"), QStringLiteral("rotation"),
                                       QStringLiteral("gradient_angle"), QStringLiteral("shadow_angle"),
                                       QStringLiteral("text_shadow_angle")};
    const QVariantMap selection = w.selection();
    const QVariantMap ranges = w.objects()->ranges();
    const QVariantMap settings = w.state->property("settings").toMap();
    const QVariantMap setting_ranges = w.state->property("settingRanges").toMap();
    int fields = 0;
    for (QObject* o : w.dialog->findChildren<QObject*>()) {
        auto* field = qobject_cast<QQuickItem*>(o);
        if (field == nullptr || !field->isVisible()) {
            continue;
        }
        const bool number = is_qml_type(field, "SidebarNumber");
        if (!number && !is_qml_type(field, "SidebarToggle") && !is_qml_type(field, "SidebarColor")
            && !is_qml_type(field, "SidebarChoice")) {
            continue;
        }
        ++fields;
        const QString prop = field->property("prop").toString();
        const bool setting = field->property("from").toString() == QStringLiteral("setting");
        INFO("field " << field->objectName().toStdString() << " reads " << prop.toStdString());
        CHECK((setting ? settings : selection).contains(prop));
        if (number && !unbounded.contains(prop)) {
            CHECK((setting ? setting_ranges : ranges).contains(prop));
        }
    }
    CHECK(fields > 0);
}

/// Every section the sidebar shows, each with the rows it shows in order,
/// checked for one hairline between each two and none over the first.
void check_sidebar_rules(EditorWindow& w)
{
    // Every section open; the rows lay out on the next polish, which on a
    // busy computer can come well after a short wait.
    w.state->setProperty("sectionClosed", QVariantMap{});
    seventhslide::test::laid_out(qobject_cast<QQuickWindow*>(w.window));
    int sections = 0;
    for (QObject* o : w.dialog->findChildren<QObject*>()) {
        auto* section = qobject_cast<QQuickItem*>(o);
        if (section == nullptr || !is_qml_type(section, "SidebarSection") || !section->isVisible()) {
            continue;
        }
        ++sections;
        std::vector<QQuickItem*> rows;
        for (QObject* child : section->findChildren<QObject*>()) {
            auto* row = qobject_cast<QQuickItem*>(child);
            if (row != nullptr && is_qml_type(row, "SidebarRow") && row->isVisible()) {
                rows.push_back(row);
            }
        }
        std::sort(rows.begin(), rows.end(), [](QQuickItem* a, QQuickItem* b) { return a->y() < b->y(); });
        INFO("section " << section->property("title").toString().toStdString());
        REQUIRE_FALSE(rows.empty());
        for (std::size_t i = 0; i < rows.size(); ++i) {
            // The hairline is the row's first child item.
            const auto children = rows[i]->childItems();
            REQUIRE_FALSE(children.isEmpty());
            INFO("row " << i << ", " << rows[i]->property("label").toString().toStdString());
            CHECK(children.front()->isVisible() == (i > 0));
        }
    }
    CHECK(sections > 0);
    check_sidebar_fields(w);
}

}  // namespace

TEST_CASE("Each sidebar section rules off every row it shows but the first, and reads what C++ fills",
          "[ui][slide-editor]")
{
    SECTION("a Freeform slide's objects")
    {
        EditorWindow w;
        w.open_freeform("Card");
        const std::vector<std::pair<const char*, const char*>> kinds{
            {"text", ""},  {"shape", ""},     {"image", "sunrise.png"}, {"table", ""},
            {"video", "welcome.mp4"}, {"indicator", ""}, {"clock", ""}};
        for (const auto& [type, filename] : kinds) {
            INFO("a selected " << type);
            QVariantMap options;
            if (*filename != '\0') {
                options.insert(QStringLiteral("filename"), QString::fromLatin1(filename));
            }
            REQUIRE(w.objects()->insert(QString::fromLatin1(type), options).isEmpty());
            check_sidebar_rules(w);
        }
        // An open line: its Fill section goes, its Line section gains its ends.
        REQUIRE(w.objects()->draw({QVariantList{200, 900}, QVariantList{700, 760}}, false, false).isEmpty());
        check_sidebar_rules(w);
    }
    SECTION("a theme's lyrics box, its lines following or not, and its copyright")
    {
        EditorWindow w;
        w.open_theme("song");
        w.objects()->selectObject(QStringLiteral("te_lyrics"));
        w.edit()->theme()->setSetting(QStringLiteral("fluid_slides"), true);
        w.edit()->theme()->setSetting(QStringLiteral("balance_wrapped_lines"), true);
        check_sidebar_rules(w);
        w.edit()->theme()->setSetting(QStringLiteral("follow_lines"), 2);
        check_sidebar_rules(w);
        w.objects()->selectObject(QStringLiteral("te_copyright"));
        REQUIRE(w.objects()->setSelectedProperty(QStringLiteral("rule"), QStringLiteral("first")).isEmpty());
        check_sidebar_rules(w);
    }
    SECTION("a theme's passage box, its verses numbered")
    {
        EditorWindow w;
        w.open_theme("bible");
        w.objects()->selectObject(QStringLiteral("te_bible_text"));
        REQUIRE(w.objects()->setSelectedProperty(QStringLiteral("verse_numbers"), true).isEmpty());
        check_sidebar_rules(w);
    }
}
