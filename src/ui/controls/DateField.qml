pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import SeventhSlide

// A field for a day: it reads as the date written out ("Saturday, March 14,
// 2026", in the app's locale) and opens a month to pick from, with "No date"
// when ``allowNone``. ``date`` is ``yyyy-MM-dd`` ("" for none); ``picked(date)``
// asks the owner to change it.
DropdownField {
    id: field
    property string date: ""
    property bool allowNone: true
    property string noneText: qsTr("No date")
    readonly property bool menuOpen: menu.visible
    signal picked(string date)

    /// ``date`` as a Date at local midnight, or null.
    function _asDate(text) {
        const m = /^(\d{4})-(\d{2})-(\d{2})$/.exec(String(text || ""))
        return m ? new Date(+m[1], +m[2] - 1, +m[3]) : null
    }
    function _key(d) {
        const pad = function (n) { return (n < 10 ? "0" : "") + n }
        return d.getFullYear() + "-" + pad(d.getMonth() + 1) + "-" + pad(d.getDate())
    }

    // The month the menu shows.
    property int _year: 2026
    property int _month: 0

    readonly property var _day: field._asDate(field.date)
    text: field._day ? field._day.toLocaleDateString(Qt.locale(), Locale.LongFormat) : field.noneText
    placeholder: !field._day
    open: menu.visible
    onClicked: {
        if (menu.visible) {
            menu.close()
            return
        }
        const shown = field._day || new Date()
        field._year = shown.getFullYear()
        field._month = shown.getMonth()
        menu.openAt()
    }

    AppMenu {
        id: menu
        anchorItem: field
        minWidth: 252

        // The month, with arrows either side.
        Item {
            width: parent ? parent.width : 0
            implicitWidth: 236
            implicitHeight: 34
            AppButton {
                anchors.left: parent.left
                anchors.leftMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                size: "control"
                iconSource: Icons.url("chevronLeft", ink)
                onClicked: {
                    if (field._month === 0) {
                        field._month = 11
                        field._year -= 1
                    } else {
                        field._month -= 1
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                text: new Date(field._year, field._month, 1).toLocaleDateString(Qt.locale(), "MMMM yyyy")
                color: Style.textBright
                font.pixelSize: Style.fontBody
                font.weight: Font.DemiBold
            }
            AppButton {
                anchors.right: parent.right
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                size: "control"
                iconSource: Icons.url("chevronRight", ink)
                onClicked: {
                    if (field._month === 11) {
                        field._month = 0
                        field._year += 1
                    } else {
                        field._month += 1
                    }
                }
            }
        }
        DayOfWeekRow {
            width: parent ? parent.width : 0
            leftPadding: 4
            rightPadding: 4
            locale: Qt.locale()
            delegate: Text {
                required property string shortName
                text: shortName
                color: Style.textFaint
                font.pixelSize: Style.fontSmall
                horizontalAlignment: Text.AlignHCenter
            }
        }
        MonthGrid {
            id: grid
            width: parent ? parent.width : 0
            leftPadding: 4
            rightPadding: 4
            bottomPadding: 4
            year: field._year
            month: field._month
            locale: Qt.locale()
            delegate: Rectangle {
                id: day
                required property var model
                readonly property bool inMonth: day.model.month === grid.month
                readonly property bool chosen: field.date === field._key(day.model.date)
                implicitHeight: 26
                radius: Style.radiusSmall
                color: day.chosen ? Style.accent
                     : (dayArea.containsMouse ? Style.menuHover : "transparent")
                border.color: day.model.today && !day.chosen ? Style.onEdge : "transparent"
                Text {
                    anchors.centerIn: parent
                    text: day.model.day
                    color: day.chosen ? Style.textOnAccent
                         : (day.inMonth ? Style.text : Style.textFaint)
                    font.pixelSize: Style.fontBody
                }
                MouseArea {
                    id: dayArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        menu.close()
                        field.picked(field._key(day.model.date))
                    }
                }
            }
        }
        MenuRule {
            visible: field.allowNone
        }
        MenuItemRow {
            visible: field.allowNone
            checkable: true
            checked: field.date.length === 0
            label: field.noneText
            onTriggered: {
                menu.close()
                field.picked("")
            }
        }
    }
}
