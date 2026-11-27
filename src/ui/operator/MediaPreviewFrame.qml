pragma ComponentBehavior: Bound

import QtQuick
import SeventhSlide

// An image, video, web page or live source in a well of the Controller, fitted
// by aspect and rimmed in the accent colour.
Item {
    id: frame
    property url source: ""
    /// Set for a live picture, which is drawn from the session's frames
    /// rather than loaded from ``source``.
    property SessionFacade liveSession: null
    /// The side of the stack whose picture it is.
    property SideView liveView: null
    /// With ``liveSession``, what is drawn: the side's ``video``, web page
    /// (``web``) or live source (``source``).
    property string liveKind: "video"
    readonly property bool live: liveSession !== null
    readonly property var liveItem: liveKind === "web" ? liveWeb
                                  : liveKind === "source" ? liveSource : liveVideo
    /// Accent rim thickness; the ring is drawn just *outside* the pixels so
    /// it encompasses the frame without covering its edge rows.
    readonly property int rimWidth: 2
    property color rimColor: Style.accent
    /// The live web page has been clicked into: it has the pointer and keys
    /// until Shift+Escape.
    readonly property bool webInteracting: live && liveKind === "web" && liveWeb.interacting

    // Natural size of the picture shown. Independent of our own
    // geometry, so the fit box below cannot feed back into it.
    readonly property real naturalW: live ? liveItem.implicitWidth : mediaImg.implicitWidth
    readonly property real naturalH: live ? liveItem.implicitHeight : mediaImg.implicitHeight
    readonly property real fitScale: (naturalW > 0 && naturalH > 0
                                      && width > 0 && height > 0)
                                     ? Math.min(width / naturalW, height / naturalH)
                                     : 0
    readonly property int fitW: Math.max(0, Math.round(naturalW * fitScale))
    readonly property int fitH: Math.max(0, Math.round(naturalH * fitScale))

    // The fit box is snapped to whole pixels here instead of read from
    // Image.paintedWidth, whose integer centring drifts the accent rim a pixel
    // off the picture.
    Item {
        id: fitBox
        x: Math.round((frame.width - frame.fitW) / 2)
        y: Math.round((frame.height - frame.fitH) / 2)
        width: frame.fitW
        height: frame.fitH

        // Stretch: the box is already the fitted rect.
        Image {
            id: mediaImg
            anchors.fill: parent
            visible: !frame.live
            fillMode: Image.Stretch
            source: frame.live ? "" : frame.source
            asynchronous: true
            // Sized from the frame: this Image's size derives from the
            // decoded size, so using it here would be a binding loop.
            sourceSize: Qt.size(
                Math.max(1, Math.round(Math.min(frame.width, 1280) * 2)),
                Math.max(1, Math.round(Math.min(frame.height, 1280) * 2)))
        }
        LiveVideoItem {
            id: liveVideo
            anchors.fill: parent
            visible: frame.live && frame.liveKind === "video"
            session: frame.liveKind === "video" ? frame.liveSession : null
            view: frame.liveKind === "video" ? frame.liveView : null
        }
        LiveWebItem {
            id: liveWeb
            anchors.fill: parent
            visible: frame.live && frame.liveKind === "web"
            session: frame.liveKind === "web" ? frame.liveSession : null
            view: frame.liveKind === "web" ? frame.liveView : null
        }
        HoverHandler {
            id: webHover
            enabled: frame.live && frame.liveKind === "web"
        }
        LiveSourceItem {
            id: liveSource
            anchors.fill: parent
            visible: frame.live && frame.liveKind === "source"
            session: frame.liveKind === "source" ? frame.liveSession : null
            view: frame.liveKind === "source" ? frame.liveView : null
        }
    }

    Rectangle {
        anchors.fill: fitBox
        anchors.margins: -frame.rimWidth
        visible: frame.fitW > 1 && frame.fitH > 1
        color: "transparent"
        border.width: frame.rimWidth
        border.color: frame.webInteracting ? Style.focusEdge : frame.rimColor
        radius: Style.radiusSmall
        z: 2
    }

    // A key named in the web page's tab.
    component KeyCap: Rectangle {
        property alias label: keyText.text
        implicitWidth: keyText.implicitWidth + 8
        implicitHeight: keyText.implicitHeight + 3
        radius: Style.radiusSmall
        color: Style.keyCap
        border.color: Style.keyCapEdge
        Text {
            id: keyText
            anchors.centerIn: parent
            color: Style.textBright
            font.pixelSize: Style.fontCaption
        }
    }

    // A tab hung from the rim's top edge: how to take the live web page while
    // the pointer is over it, and, in the rim's blue while it is taken, how to
    // give it back. It takes no input of its own.
    Rectangle {
        objectName: "webPageTab"
        anchors.horizontalCenter: fitBox.horizontalCenter
        anchors.top: fitBox.top
        width: webTabRow.implicitWidth + 2 * Style.pad
        height: 22
        radius: Style.radius
        color: frame.webInteracting ? Style.focusEdge : Style.edgeIdle
        visible: frame.live && frame.liveKind === "web" && fitBox.width > width + 16
                 && (frame.webInteracting || webHover.hovered)
        z: 3

        // Square where it meets the rim; only its foot is rounded.
        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: parent.radius
            color: parent.color
        }
        Row {
            id: webTabRow
            anchors.centerIn: parent
            spacing: 4
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: frame.webInteracting ? qsTr("Using the page ·") : qsTr("Click to use the page")
                color: Style.textStrong
                font.pixelSize: Style.fontSmall
            }
            KeyCap {
                anchors.verticalCenter: parent.verticalCenter
                visible: frame.webInteracting
                label: qsTr("Shift")
            }
            KeyCap {
                anchors.verticalCenter: parent.verticalCenter
                visible: frame.webInteracting
                label: qsTr("Esc")
            }
        }
    }
}
