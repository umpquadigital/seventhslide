// The output editor's General tab: the canvas picked by name from its menu,
// or typed as a custom size.

#include <catch2/catch_test_macros.hpp>

#include "operator_ui.hpp"
#include "facade_all.hpp"
#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"
#include "ui_test_helpers.hpp"

#include <QList>
#include <QQuickItem>
#include <QString>
#include <QTest>
#include <QVariant>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

using seventhslide::app::SessionFacade;
using seventhslide::test::OperatorUi;
using seventhslide::test::centre_of;
using seventhslide::test::click;
using seventhslide::test::qml_object;
using seventhslide::test::visible_button_under;
using seventhslide::test::visible_qml_items_of;
using seventhslide::test::visible_text_under;

namespace {

/// The output editor on its General tab.
struct GeneralTab {
    OperatorUi& ui;
    QObject* dialog = nullptr;
    QObject* state = nullptr;
    QQuickItem* panel = nullptr;

    explicit GeneralTab(OperatorUi& operator_ui)
        : ui(operator_ui)
    {
        dialog = ui.find_component("OutputEditDialog");
        REQUIRE(dialog != nullptr);
        state = qml_object(dialog, "OutputEditState");
        REQUIRE(state != nullptr);
        panel = qobject_cast<QQuickItem*>(qml_object(dialog, "OutputGeneralPanel"));
        REQUIRE(panel != nullptr);
        open();
    }

    void open() const
    {
        REQUIRE(QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, QStringLiteral("Main")),
                                          Q_ARG(QVariant, QStringLiteral("general"))));
        QTest::qWait(20);
    }

    /// The control on the form row labelled ``label``, of QML type ``type``.
    [[nodiscard]] QQuickItem* on_row(const char* label, const char* type) const
    {
        QQuickItem* row = visible_text_under(panel, QString::fromLatin1(label));
        while (row != nullptr && visible_qml_items_of(row, type).isEmpty()) {
            row = row->parentItem();
        }
        REQUIRE(row != nullptr);
        return visible_qml_items_of(row, type).front();
    }
    [[nodiscard]] QQuickItem* canvas_field() const { return on_row("Canvas", "DropdownField"); }
    /// The Size row's two fields.
    [[nodiscard]] QList<QQuickItem*> size_fields() const
    {
        const auto fields =
            visible_qml_items_of(on_row("Size", "DialogField")->parentItem(), "DialogField");
        REQUIRE(fields.size() == 2);
        return fields;
    }
    /// What the Size row's fields show, as "WxH".
    [[nodiscard]] QString fields_show() const
    {
        const auto fields = size_fields();
        return fields.at(0)->property("text").toString() + QLatin1Char('x')
             + fields.at(1)->property("text").toString();
    }
    [[nodiscard]] QString field(const char* name) const
    {
        return canvas_field()->property(name).toString();
    }
    [[nodiscard]] QString canvas() const
    {
        return state->property("canvasW").toString() + QLatin1Char('x')
             + state->property("canvasH").toString();
    }

    /// Open the Canvas menu and pick ``label`` in the section headed
    /// ``heading`` (empty for an item outside every section).
    void pick(const char* heading, const char* label) const
    {
        click(ui.window(), centre_of(canvas_field()));
        REQUIRE(canvas_field()->property("open").toBool());
        QQuickItem* under = ui.window()->contentItem();
        if (heading[0] != '\0') {
            under = nullptr;
            for (QQuickItem* s : visible_qml_items_of(ui.window()->contentItem(), "MenuSection")) {
                if (s->property("title").toString() == QString::fromUtf8(heading)) {
                    under = s;
                }
            }
            INFO(heading);
            REQUIRE(under != nullptr);
        }
        QQuickItem* item = visible_text_under(under, QString::fromUtf8(label));
        INFO(label);
        REQUIRE(item != nullptr);
        click(ui.window(), centre_of(item));
        CHECK_FALSE(canvas_field()->property("open").toBool());
    }

    /// Scroll the row labelled ``label`` into view and click its ToggleSwitch.
    void click_toggle(const char* label) const
    {
        QQuickItem* toggle = on_row(label, "ToggleSwitch");
        const QRectF at = toggle->mapRectToItem(panel->property("contentItem").value<QQuickItem*>(),
                                                toggle->boundingRect());
        const qreal most = std::max(0.0, panel->property("contentHeight").toReal()
                                             - panel->height());
        REQUIRE(panel->setProperty("contentY", std::clamp(at.top() - 12, 0.0, most)));
        QTest::qWait(10);
        click(ui.window(), centre_of(toggle));
    }

    /// Replace what the field on the row labelled ``label`` holds with
    /// ``text``, typed.
    void type_into(const char* label, const std::string& text) const
    {
        type_in(on_row(label, "DialogField"), text);
    }

    /// Replace what the field at ``index`` on the Size row holds with ``text``,
    /// typed.
    void type_size(int index, const std::string& text) const
    {
        type_in(size_fields().at(index), text);
    }

    void type_in(QQuickItem* field, const std::string& text) const
    {
        REQUIRE(QMetaObject::invokeMethod(field, "focusInput"));
        QTest::keyClick(ui.window(), Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(ui.window(), Qt::Key_Backspace);
        for (const char c : text) {
            QTest::keyClick(ui.window(), c);
        }
        QTest::qWait(10);
    }
};

}  // namespace

TEST_CASE("An output's General tab switches each content type on and off",
          "[ui][output-edit][general]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.init_libraries(dir.path / "data"));
    OperatorUi ui(facade);
    GeneralTab tab(ui);
    const auto disabled = [&facade]() {
        return facade.session().outputs().front().disabled_content_types;
    };

    // Everything is on to begin with, and nothing is kept until Save.
    QQuickItem* videos = tab.on_row("Videos", "ToggleSwitch");
    CHECK(videos->property("checked").toBool());
    CHECK(tab.on_row("Songs", "ToggleSwitch")->property("checked").toBool());
    tab.click_toggle("Videos");
    CHECK_FALSE(videos->property("checked").toBool());
    CHECK(tab.on_row("Images", "ToggleSwitch")->property("checked").toBool());
    CHECK(disabled().empty());

    // Saved, the output has it, and it opens again as it was left.
    REQUIRE(QMetaObject::invokeMethod(tab.state, "saveGeneral"));
    CHECK_FALSE(tab.state->property("open").toBool());
    CHECK(disabled() == std::vector<std::string>{"video"});
    tab.open();
    videos = tab.on_row("Videos", "ToggleSwitch");
    CHECK_FALSE(videos->property("checked").toBool());

    // Switched back on, nothing is left out.
    tab.click_toggle("Videos");
    CHECK(videos->property("checked").toBool());
    REQUIRE(QMetaObject::invokeMethod(tab.state, "saveGeneral"));
    CHECK(disabled().empty());

    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("An output's General tab sets how audio fades on it", "[ui][output-edit][general][audio]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.init_libraries(dir.path / "data"));
    OperatorUi ui(facade);
    GeneralTab tab(ui);
    const auto fades = [&facade]() { return facade.session().outputs().front().audio_fades; };

    // All cuts to begin with.
    CHECK(tab.on_row("Fade in", "DialogField")->property("text").toString() == QStringLiteral("0"));
    tab.type_into("Fade out", "1200");
    tab.type_into("Overlap", "500");
    CHECK_FALSE(tab.on_row("Fades apply to play/pause", "ToggleSwitch")->property("checked").toBool());
    tab.click_toggle("Fades apply to play/pause");
    CHECK(fades() == seventhslide::model::AudioFades{});

    // Saved, the output has them, and they open again as they were left.
    REQUIRE(QMetaObject::invokeMethod(tab.state, "saveGeneral"));
    CHECK(fades() == seventhslide::model::AudioFades{0, 1200, 500, true});
    tab.open();
    CHECK(tab.on_row("Fade out", "DialogField")->property("text").toString()
          == QStringLiteral("1200"));
    CHECK(tab.on_row("Overlap", "DialogField")->property("text").toString()
          == QStringLiteral("500"));
    CHECK(tab.on_row("Fades apply to play/pause", "ToggleSwitch")->property("checked").toBool());

    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("An output's canvas is picked by name, or typed as a custom size",
          "[ui][output-edit][general]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.init_libraries(dir.path / "data"));
    OperatorUi ui(facade);
    GeneralTab tab(ui);

    // A new output's canvas has a name, and its size is in the fields under it.
    CHECK(tab.field("text") == QStringLiteral("1080p 16:9"));
    CHECK(tab.field("detail").isEmpty());
    CHECK(tab.fields_show() == QStringLiteral("1920x1080"));

    // Picked from its menu, under its shape (1080p is under three); the menu
    // says names, the fields the size.
    CHECK(visible_text_under(ui.window()->contentItem(), QStringLiteral("1920 × 1080"))
          == nullptr);
    tab.pick("9:16 Vertical", "1080p");
    CHECK(tab.canvas() == QStringLiteral("1080x1920"));
    CHECK(tab.field("text") == QStringLiteral("1080p Vertical"));
    CHECK(tab.fields_show() == QStringLiteral("1080x1920"));

    // Custom… puts the keyboard in the width, the size kept until it is typed.
    tab.pick("", "Custom…");
    CHECK(tab.canvas() == QStringLiteral("1080x1920"));
    QQuickItem* typing = ui.window()->activeFocusItem();
    REQUIRE(typing != nullptr);
    CHECK(typing->inherits("QQuickTextInput"));
    CHECK(typing->property("text").toString() == QStringLiteral("1080"));

    // A size no preset has is Custom, its shape said beside it.
    tab.type_size(0, "2560");
    tab.type_size(1, "1080");
    CHECK(tab.canvas() == QStringLiteral("2560x1080"));
    CHECK(tab.field("text") == QStringLiteral("Custom"));
    CHECK(tab.field("detail") == QStringLiteral("2.37:1"));

    // Its button turns it on its side.
    QQuickItem* swap = visible_button_under(tab.panel, QStringLiteral("canvasSwapSides"));
    REQUIRE(swap != nullptr);
    click(ui.window(), centre_of(swap));
    CHECK(tab.canvas() == QStringLiteral("1080x2560"));
    CHECK(tab.fields_show() == QStringLiteral("1080x2560"));
    CHECK(tab.field("detail") == QStringLiteral("0.42:1"));

    // Typed to a preset's size, it is named again.
    tab.type_size(1, "1920");
    CHECK(tab.field("text") == QStringLiteral("1080p Vertical"));
    CHECK(tab.field("detail").isEmpty());

    // A size an output cannot have says why under the fields, and Save says
    // the same and keeps the window open.
    tab.type_size(0, "9000");
    const QString too_large = QStringLiteral("The canvas can be up to 7680 × 4320");
    CHECK(visible_text_under(tab.panel, too_large) != nullptr);
    REQUIRE(QMetaObject::invokeMethod(tab.state, "saveGeneral"));
    CHECK(tab.state->property("errorText").toString() == too_large);
    CHECK(tab.state->property("open").toBool());
    tab.type_size(0, "");
    CHECK(visible_text_under(tab.panel, QStringLiteral("The canvas needs a width and a height"))
          != nullptr);

    // Saved, the output has it, and opens again as Custom.
    tab.type_size(0, "1366");
    tab.type_size(1, "768");
    CHECK(visible_text_under(tab.panel, too_large) == nullptr);
    REQUIRE(QMetaObject::invokeMethod(tab.state, "saveGeneral"));
    CHECK_FALSE(tab.state->property("open").toBool());
    CHECK(facade.session().outputs().front().canvas_width == 1366);
    CHECK(facade.session().outputs().front().canvas_height == 768);
    // Its text theme was carried onto the new canvas, and the window says so.
    QTest::qWait(20);
    const QString fitted = QStringLiteral("3 themes were fitted to 1366 × 768");
    CHECK(visible_text_under(ui.window()->contentItem(), fitted) != nullptr);
    QTest::keyClick(ui.window(), Qt::Key_Escape);
    QTest::qWait(20);
    CHECK(visible_text_under(ui.window()->contentItem(), fitted) == nullptr);
    tab.open();
    CHECK(tab.field("text") == QStringLiteral("Custom"));
    CHECK(tab.field("detail") == QStringLiteral("1.78:1"));
    CHECK(tab.fields_show() == QStringLiteral("1366x768"));

    // A preset picked fills the fields.
    tab.pick("16:9 Widescreen", "720p");
    CHECK(tab.canvas() == QStringLiteral("1280x720"));
    CHECK(tab.fields_show() == QStringLiteral("1280x720"));
    CHECK(tab.field("text") == QStringLiteral("720p 16:9"));

    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Each theme library's page in the output window has its own content's icon",
          "[ui][output-edit][themes]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.init_libraries(dir.path / "data"));
    OperatorUi ui(facade);
    const GeneralTab tab(ui);
    QObject* shell = qml_object(tab.dialog, "WindowShell");
    REQUIRE(shell != nullptr);

    std::vector<std::pair<std::string, std::string>> icons;
    for (const auto& page : shell->property("pages").toList()) {
        const auto map = page.toMap();
        icons.emplace_back(map.value(QStringLiteral("id")).toString().toStdString(),
                           map.value(QStringLiteral("icon")).toString().toStdString());
    }
    // A text theme's library has the icon its content has in the library.
    CHECK(icons == std::vector<std::pair<std::string, std::string>>{
                       {"general", "settings"},
                       {"song", "song"},
                       {"bible", "bible"},
                       {"media", "video"},
                       {"bg", "image"},
                       {"destinations", "display"}});
}
