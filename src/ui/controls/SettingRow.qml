import QtQuick
import QtQuick.Layouts
import SeventhSlide

// A GroupRow that is one FormRow: a setting's label, and its control (declared
// inside) at the right edge. For a settings island in a narrow pane (the
// Output window's Destinations, Settings > Bibles), where each row is just
// a label and its control.
//
//   GroupList { SettingRow { label: qsTr("Name"); DialogField { width: ... } } }
GroupRow {
    id: setting

    required property string label
    default property alias control: form.control

    FormRow {
        id: form
        Layout.fillWidth: true
        label: setting.label
    }
}
