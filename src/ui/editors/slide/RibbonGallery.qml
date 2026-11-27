pragma ComponentBehavior: Bound
import QtQuick
import SeventhSlide

// A gallery: a row of cards as tall as the ribbon's two rows, each
// a little picture of what it does over its name, and, when there are more
// than fit, chevrons at its right that page through the rest. The card in use
// is the selected grey. ``options`` are [{value, label}]; ``picked(value)``
// asks for a change, as RibbonDropdown does.
Row {
    id: gallery
    property var options: []
    property var value
    /// What the cards sketch: a transition's two pictures, or an animation's
    /// one object (``effect``).
    property string sketch: "transition"
    property int cardWidth: 70
    /// How many cards a page shows.
    property int pageSize: 6
    signal picked(var value)

    readonly property int pageCount: Math.max(1, Math.ceil(gallery.options.length / gallery.pageSize))
    property int page: 0
    // The page the card in use is on, when a pick or a selection names it.
    onValueChanged: gallery.showValue()
    onOptionsChanged: gallery.showValue()
    function showValue() {
        for (let i = 0; i < gallery.options.length; ++i) {
            if (String(gallery.options[i].value) === String(gallery.value)) {
                gallery.page = Math.floor(i / gallery.pageSize)
                return
            }
        }
        gallery.page = Math.min(gallery.page, gallery.pageCount - 1)
    }

    spacing: 4
    height: Style.ribbonTallH
    opacity: enabled ? 1 : Style.offOpacity

    Repeater {
        model: gallery.options.slice(gallery.page * gallery.pageSize,
                                     (gallery.page + 1) * gallery.pageSize)
        delegate: Rectangle {
            id: card
            required property var modelData
            readonly property string type: String(card.modelData.value)
            readonly property bool on: String(gallery.value) === card.type
            objectName: gallery.objectName + "_" + card.type
            width: gallery.cardWidth
            height: gallery.height
            radius: Style.radiusSmall
            color: card.on ? Style.panelRowSel : (area.containsMouse ? Style.btnGhostHover : "transparent")
            border.color: card.on ? Style.selectedBar : "transparent"

            Rectangle {
                id: pic
                anchors.top: parent.top
                anchors.topMargin: 4
                anchors.horizontalCenter: parent.horizontalCenter
                width: 54
                height: 30
                radius: 2
                color: Style.fieldFace
                clip: true

                // An effect: the object as the effect leaves it part way.
                Rectangle {
                    visible: gallery.sketch === "effect"
                    readonly property bool exit: card.type === "disappear" || card.type.endsWith("_out")
                    anchors.centerIn: parent
                    anchors.horizontalCenterOffset: card.type === "fly_in" ? -11 : card.type === "fly_out" ? 11 : 0
                    anchors.verticalCenterOffset: card.type === "float" ? 3 : card.type === "float_out" ? -3 : 0
                    width: card.type === "wipe" || card.type === "wipe_out" ? 10 : 20
                    height: 11
                    radius: 1
                    color: Style.galleryObject
                    scale: card.type === "pulse" || card.type === "grow" ? 1.35
                           : card.type === "zoom" || card.type === "zoom_out" ? 0.7 : 1
                    opacity: card.type === "fade" ? 0.45 : exit ? 0.2 : 1
                }

                // A transition: what shows before, and what comes as the type
                // moves it.
                Rectangle {
                    visible: gallery.sketch === "transition"
                    anchors.fill: parent
                    radius: 2
                    color: Style.galleryBase
                    Rectangle {
                        visible: card.type !== "cut"
                        y: card.type === "zoom" ? 7 : 0
                        height: card.type === "zoom" ? parent.height - 14 : parent.height
                        x: card.type === "push" ? parent.width / 2
                           : card.type === "zoom" ? 14
                           : card.type === "cover" ? parent.width * 0.3
                           : card.type === "split" ? parent.width * 0.3 : 0
                        width: card.type === "push" ? parent.width / 2
                               : card.type === "wipe" ? parent.width * 0.55
                               : card.type === "zoom" ? parent.width - 28
                               : card.type === "split" ? parent.width * 0.4
                               : card.type === "cover" ? parent.width * 0.7
                               : card.type === "reveal" ? parent.width * 0.7 : parent.width
                        color: card.type === "fade_through_black" ? "black" : Style.galleryShade
                        opacity: card.type === "fade" ? 0.5
                                 : card.type === "fade_through_black" ? 0.7 : 1
                    }
                }
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 3
                width: parent.width - 4
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: String(card.modelData.label)
                color: card.on ? Style.textStrong : Style.text
                font.pixelSize: Style.fontCaption
            }
            MouseArea {
                id: area
                anchors.fill: parent
                hoverEnabled: true
                onClicked: gallery.picked(card.type)
            }
        }
    }
    Column {
        visible: gallery.pageCount > 1
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        Repeater {
            model: [-1, 1]
            delegate: AppButton {
                id: pager
                required property int modelData
                size: "compact"
                width: 18
                height: 18
                minWidth: 18
                enabled: pager.modelData < 0 ? gallery.page > 0 : gallery.page < gallery.pageCount - 1
                iconSource: Icons.url("chevronDown", pager.ink, 2.5)
                rotation: pager.modelData < 0 ? 180 : 0
                onClicked: gallery.page += pager.modelData
            }
        }
    }
}
