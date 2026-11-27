#include <catch2/catch_test_macros.hpp>

#include "test_qml.hpp"

#include <QCoreApplication>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QUrl>
#include <QWheelEvent>

namespace {

void move_to(QQuickWindow& window, QPointF pos)
{
    QMouseEvent move(QEvent::MouseMove, pos, window.mapToGlobal(pos), Qt::NoButton,
                     Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &move);
}

void wheel_at(QQuickWindow& window, QPointF pos)
{
    QWheelEvent wheel(pos, window.mapToGlobal(pos), QPoint(), QPoint(0, 120), Qt::NoButton,
                      Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&window, &wheel);
}

void click_at(QQuickWindow& window, QPointF pos, Qt::MouseButton button)
{
    QMouseEvent press(QEvent::MouseButtonPress, pos, window.mapToGlobal(pos), button, button,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, pos, window.mapToGlobal(pos), button,
                        Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &release);
}

}  // namespace

TEST_CASE("A modal scrim keeps hover, wheel and clicks off what is behind it", "[ui][modal]")
{
    seventhslide::test::QmlScene scene;
    auto* window = qobject_cast<QQuickWindow*>(scene.create(R"QML(
import QtQuick
import SeventhSlide as Ui

Window {
    id: win
    width: 200; height: 200; visible: true
    property int hovers: 0
    property int wheels: 0
    property int presses: 0
    property int scrimClicks: 0
    property alias scrimVisible: scrim.visible

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.AllButtons
        onPositionChanged: win.hovers++
        onWheel: function (wheel) { win.wheels++ }
        onPressed: win.presses++
    }
    Ui.ModalScrim {
        id: scrim
        anchors.fill: parent
        onClicked: win.scrimClicks++
    }
}
)QML",
        QUrl::fromLocalFile(QStringLiteral(SEVENTHSLIDE_UI_QML_DIR "/"))));
    REQUIRE(window != nullptr);
    QCoreApplication::processEvents();

    const QPointF centre(100, 100);
    move_to(*window, centre);
    move_to(*window, centre + QPointF(5, 5));
    wheel_at(*window, centre);
    click_at(*window, centre, Qt::RightButton);
    REQUIRE(window->property("hovers").toInt() == 0);
    REQUIRE(window->property("wheels").toInt() == 0);
    REQUIRE(window->property("presses").toInt() == 0);
    // A right click is swallowed, not treated as "close the dialog".
    REQUIRE(window->property("scrimClicks").toInt() == 0);

    click_at(*window, centre, Qt::LeftButton);
    REQUIRE(window->property("scrimClicks").toInt() == 1);
    REQUIRE(window->property("presses").toInt() == 0);

    // Without the scrim the same events do reach the area: the harness is live.
    window->setProperty("scrimVisible", false);
    move_to(*window, centre);
    move_to(*window, centre + QPointF(-5, -5));
    wheel_at(*window, centre);
    click_at(*window, centre, Qt::LeftButton);
    REQUIRE(window->property("hovers").toInt() > 0);
    REQUIRE(window->property("wheels").toInt() == 1);
    REQUIRE(window->property("presses").toInt() == 1);
}

TEST_CASE("Stacked modal scrims dim the window once", "[ui][modal]")
{
    seventhslide::test::QmlScene scene;
    auto* window = qobject_cast<QQuickWindow*>(scene.create(R"QML(
import QtQuick
import SeventhSlide as Ui

Window {
    width: 200; height: 200; visible: true
    property alias pickerVisible: picker.visible
    property alias sheetVisible: sheet.visible
    property alias confirmVisible: confirm.visible
    readonly property bool open: Ui.Modals.open
    readonly property string dimmer: Ui.Modals.dimmer ? Ui.Modals.dimmer.objectName : ""

    // A colour picker's scrim takes input but has no tint.
    Ui.ModalScrim { id: picker; objectName: "picker"; anchors.fill: parent; visible: false }
    Ui.ModalScrim {
        id: sheet; objectName: "sheet"; anchors.fill: parent; visible: false
        tint: Ui.Style.sheetScrim
    }
    Ui.ModalScrim {
        id: confirm; objectName: "confirm"; anchors.fill: parent; visible: false
        tint: Ui.Style.modalScrim
    }
}
)QML",
        QUrl::fromLocalFile(QStringLiteral(SEVENTHSLIDE_UI_QML_DIR "/"))));
    REQUIRE(window != nullptr);
    QCoreApplication::processEvents();
    const auto dimmer = [&] { return window->property("dimmer").toString(); };

    REQUIRE_FALSE(window->property("open").toBool());
    REQUIRE(dimmer().isEmpty());

    // An untinted scrim is a modal, but dims nothing.
    window->setProperty("pickerVisible", true);
    REQUIRE(window->property("open").toBool());
    REQUIRE(dimmer().isEmpty());

    // The sheet dims; a confirm over it does not dim again.
    window->setProperty("sheetVisible", true);
    window->setProperty("confirmVisible", true);
    REQUIRE(dimmer() == QStringLiteral("sheet"));

    // The confirm takes over the dim only once the sheet has gone.
    window->setProperty("sheetVisible", false);
    REQUIRE(dimmer() == QStringLiteral("confirm"));

    // Shown again, the sheet is now over the confirm, which keeps the dim.
    window->setProperty("sheetVisible", true);
    REQUIRE(dimmer() == QStringLiteral("confirm"));

    window->setProperty("confirmVisible", false);
    window->setProperty("sheetVisible", false);
    window->setProperty("pickerVisible", false);
    REQUIRE_FALSE(window->property("open").toBool());
    REQUIRE(dimmer().isEmpty());
}
