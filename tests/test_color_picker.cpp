// The colour picker opens on the colour it shows and, where it offers
// opacity, keeps a translucent colour translucent.

#include <catch2/catch_test_macros.hpp>

#include "test_qml.hpp"

#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>
#include <QVariant>

#include <string>

namespace {

constexpr const char* k_picker_qml = R"QML(
import QtQuick
import SeventhSlide as Ui

Window {
    width: 400; height: 400; visible: true
    property alias picker: picker
    Ui.ColorPicker { id: picker }
}
)QML";

/// Open ``picker`` on ``value`` and press Select without touching anything:
/// what it hands back is the colour it opened on.
std::string reselect(QObject* picker, const QString& value, bool alpha)
{
    REQUIRE(picker->setProperty("value", value));
    if (alpha || picker->property("alpha").isValid()) {
        REQUIRE(picker->setProperty("alpha", alpha));
    }
    QSignalSpy edited(picker, SIGNAL(edited(QString)));
    REQUIRE(QMetaObject::invokeMethod(picker, "openDialog"));
    QTest::qWait(10);
    QObject* select = nullptr;
    for (QObject* o : picker->findChildren<QObject*>()) {
        if (QByteArray(o->metaObject()->className()).startsWith("AppButton_QMLTYPE_")
            && o->property("label").toString() == QStringLiteral("Select")) {
            select = o;
        }
    }
    REQUIRE(select != nullptr);
    REQUIRE(QMetaObject::invokeMethod(select, "clicked"));
    REQUIRE(edited.size() == 1);
    return edited.front().front().toString().toStdString();
}

}  // namespace

TEST_CASE("The colour picker opens on the colour it shows", "[ui][color-picker]")
{
    seventhslide::test::QmlScene scene;
    auto* window = qobject_cast<QQuickWindow*>(scene.create(k_picker_qml));
    REQUIRE(window != nullptr);
    QObject* picker = window->property("picker").value<QObject*>();
    REQUIRE(picker != nullptr);

    // It used to open on white whatever the colour was.
    CHECK(reselect(picker, QStringLiteral("#ff0000"), false) == "#ff0000");
    CHECK(reselect(picker, QStringLiteral("#336699"), false) == "#336699");

    SECTION("with opacity, a translucent colour stays translucent")
    {
        CHECK(reselect(picker, QStringLiteral("#14ffffff"), true) == "#14ffffff");
        CHECK(reselect(picker, QStringLiteral("#80336699"), true) == "#80336699");
        // An opaque one is still written as #rrggbb.
        CHECK(reselect(picker, QStringLiteral("#336699"), true) == "#336699");
    }
    SECTION("without opacity, the pick is opaque")
    {
        CHECK(reselect(picker, QStringLiteral("#80336699"), false) == "#336699");
    }
}
