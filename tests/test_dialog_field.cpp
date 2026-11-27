#include <catch2/catch_test_macros.hpp>

#include "test_qml.hpp"
#include "test_wait.hpp"

#include <QQuickWindow>
#include <QTest>
#include <QUrl>

namespace {

// A DialogField bound the way every dialog binds one: ``text`` reads the
// caller's value and ``edited`` writes it back, here upper-cased so the
// caller's answer differs from what was typed.
constexpr const char* k_field_qml = R"QML(
import QtQuick
import SeventhSlide as Ui

Window {
    id: win
    width: 200; height: 60; visible: true
    property string value: ""
    property int edits: 0
    property alias field: field

    Ui.DialogField {
        id: field
        width: 180
        text: win.value
        onEdited: function (v) {
            win.edits++
            win.value = v.toUpperCase()
        }
    }
}
)QML";

// A DialogField that saves when editing ends, as the Sync page's do, and refuses
// what is not a number: ``value`` then stays what it was.
constexpr const char* k_saved_field_qml = R"QML(
import QtQuick
import SeventhSlide as Ui

Window {
    id: win
    width: 200; height: 60; visible: true
    property string value: "8766"
    property string refused: ""
    property alias field: field

    Ui.DialogField {
        id: field
        width: 180
        text: win.value
        onCommitted: function (v) {
            if (/^[0-9]*$/.test(v))
                win.value = v
            else
                win.refused = v
        }
    }
}
)QML";

// A password prompt, as the Sync page asks for the library's admin password.
constexpr const char* k_password_prompt_qml = R"QML(
import QtQuick
import SeventhSlide as Ui

Window {
    id: win
    width: 480; height: 320; visible: true
    property alias prompt: prompt

    Ui.AppPromptDialog {
        id: prompt
        title: "Password"
        password: true
    }
}
)QML";

void type(QQuickWindow& window, const char* text)
{
    for (const char* c = text; *c != '\0'; ++c) {
        QTest::keyClick(&window, *c);
    }
}

QObject* text_input(QObject& window)
{
    for (QObject* o : window.findChildren<QObject*>()) {
        if (o->inherits("QQuickTextInput")) {
            return o;
        }
    }
    FAIL("no TextInput in the window");
    return nullptr;
}

QString typed_text(QObject& window)
{
    return text_input(window)->property("text").toString();
}

struct FieldWindow {
    seventhslide::test::QmlScene scene;
    QQuickWindow* window = nullptr;
    QObject* field = nullptr;

    explicit FieldWindow(const char* qml = k_field_qml)
    {
        window = qobject_cast<QQuickWindow*>(scene.create(
            qml, QUrl::fromLocalFile(QStringLiteral(SEVENTHSLIDE_UI_QML_DIR "/"))));
        REQUIRE(window != nullptr);
        field = window->property("field").value<QObject*>();
        REQUIRE(field != nullptr);
        window->requestActivate();
        REQUIRE(QTest::qWaitForWindowActive(window));
    }

    void focus() { REQUIRE(QMetaObject::invokeMethod(field, "focusInput")); }
    void end_edit() { REQUIRE(QMetaObject::invokeMethod(field, "clearFocus")); }
};

}  // namespace

TEST_CASE("A dialog field keeps what is typed while the caller rewrites it", "[ui][dialog-field]")
{
    FieldWindow w;
    w.focus();
    type(*w.window, "ab");
    CHECK(w.window->property("value").toString() == QStringLiteral("AB"));
    // The caller's answer to each keystroke does not replace the typing.
    CHECK(typed_text(*w.window) == QStringLiteral("ab"));

    w.end_edit();
    CHECK(typed_text(*w.window) == QStringLiteral("AB"));
}

TEST_CASE("A dialog field shows a change made elsewhere, even mid-edit", "[ui][dialog-field]")
{
    FieldWindow w;
    w.window->setProperty("value", QStringLiteral("FIRST"));
    CHECK(typed_text(*w.window) == QStringLiteral("FIRST"));

    w.focus();
    type(*w.window, "x");
    // An arrangement chip, a reload: not an answer to the typing.
    w.window->setProperty("value", QStringLiteral("CHIP"));
    CHECK(typed_text(*w.window) == QStringLiteral("CHIP"));

    // Ending the edit does not write the stale text back over it.
    const int edits = w.window->property("edits").toInt();
    w.end_edit();
    CHECK(w.window->property("value").toString() == QStringLiteral("CHIP"));
    CHECK(w.window->property("edits").toInt() == edits);
}

TEST_CASE("A dialog field does not echo the caller's value back as an edit", "[ui][dialog-field]")
{
    FieldWindow w;
    w.window->setProperty("value", QStringLiteral("LOADED"));
    w.focus();
    w.end_edit();
    CHECK(w.window->property("edits").toInt() == 0);
}

TEST_CASE("A dialog field shows what was saved once editing ends, not what was refused", "[ui][dialog-field]")
{
    FieldWindow w(k_saved_field_qml);
    w.focus();
    type(*w.window, "x");
    SECTION("leaving the field")
    {
        w.end_edit();
    }
    SECTION("Enter, then leaving the field")
    {
        QTest::keyClick(w.window, Qt::Key_Return);
        // Still there to correct, while it has the focus.
        CHECK(typed_text(*w.window) == QStringLiteral("8766x"));
        w.end_edit();
    }
    CHECK(w.window->property("refused").toString() == QStringLiteral("8766x"));
    CHECK(w.window->property("value").toString() == QStringLiteral("8766"));
    CHECK(typed_text(*w.window) == QStringLiteral("8766"));
}

TEST_CASE("A dialog field saves what was typed when editing ends", "[ui][dialog-field]")
{
    FieldWindow w(k_saved_field_qml);
    w.focus();
    type(*w.window, "1");
    w.end_edit();
    CHECK(w.window->property("value").toString() == QStringLiteral("87661"));
    CHECK(typed_text(*w.window) == QStringLiteral("87661"));
}

// A prompt has one field, so the caret stays in it while the password is
// typed: the dots must show then, not only once editing ends.
TEST_CASE("A password prompt hides the password while it is typed", "[ui][dialog-field]")
{
    seventhslide::test::QmlScene scene;
    auto* window = qobject_cast<QQuickWindow*>(scene.create(
        k_password_prompt_qml, QUrl::fromLocalFile(QStringLiteral(SEVENTHSLIDE_UI_QML_DIR "/"))));
    REQUIRE(window != nullptr);
    window->requestActivate();
    REQUIRE(QTest::qWaitForWindowActive(window));
    QObject* prompt = window->property("prompt").value<QObject*>();
    REQUIRE(prompt != nullptr);
    REQUIRE(QMetaObject::invokeMethod(prompt, "ask", Q_ARG(QVariant, QString())));
    QObject* input = text_input(*window);
    REQUIRE(seventhslide::test::wait_until([&] { return input->property("activeFocus").toBool(); }));

    type(*window, "secret");
    CHECK(prompt->property("value").toString() == QStringLiteral("secret"));
    CHECK(input->property("activeFocus").toBool());
    CHECK(input->property("displayText").toString() != QStringLiteral("secret"));
}
