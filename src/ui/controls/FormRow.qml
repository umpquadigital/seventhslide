import QtQuick
import SeventhSlide

// One row of a form: the label on the left, an optional hint under it, and
// the control (whatever is declared inside) at the row's right edge. Give the
// control one of Style's field widths (fieldShortW / fieldMediumW /
// fieldLongW) so right edges line up down the form.
//
// It adds no padding of its own: a GroupRow's insets make a one-line row
// Style.groupRowH high, as every grouped row is, and a GroupOpenBody spaces
// its rows to the same pitch. Only a hint or a tall control (a checklist)
// makes a row taller.
//
//   FormRow { label: qsTr("Name"); DialogField { implicitWidth: Style.fieldLongW } }
Item {
    id: row

    required property string label
    /// A quiet line under the label; none when empty.
    property string hint: ""
    /// Hint ink: the default, or Style.textWarning for what needs fixing.
    property color hintInk: Style.textHint
    /// Label ink: the default, or Style.textSoft in a row opened in place.
    property color labelInk: Style.text
    default property alias control: controlSlot.data

    implicitHeight: Math.max(Style.groupRowH - 12, labelCol.implicitHeight,
                             controlSlot.implicitHeight)
    implicitWidth: labelCol.implicitWidth + 12 + controlSlot.implicitWidth

    Column {
        id: labelCol
        anchors.left: parent.left
        anchors.right: controlSlot.left
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 1

        Text {
            width: parent.width
            text: row.label
            color: row.labelInk
            font.pixelSize: Style.fontBody
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            visible: row.hint.length > 0
            text: row.hint
            color: row.hintInk
            font.pixelSize: Style.fontSmall
            wrapMode: Text.WordWrap
        }
    }
    Item {
        id: controlSlot
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        implicitWidth: childrenRect.width
        implicitHeight: childrenRect.height
        width: implicitWidth
        height: implicitHeight
    }
}
