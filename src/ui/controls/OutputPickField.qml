pragma ComponentBehavior: Bound

import QtQuick
import SeventhSlide

// A dropdown of the outputs in ``outputs`` (an OutputListModel), led by All
// Outputs: ``value`` is the id of the output shown, empty for all of them.
// As with any OptionField, ``picked`` asks for another and the caller sets
// ``value``; should the output shown go, it asks for all.
OptionField {
    id: field

    property OutputListModel outputs: null
    property string allLabel: qsTr("All outputs")

    value: ""
    options: field._options

    property var _options: []

    function _rebuild() {
        const options = [{ value: "", label: field.allLabel }]
        const model = field.outputs
        for (let i = 0; model && i < model.rowCount(); ++i) {
            const ix = model.index(i, 0)
            options.push({ value: String(model.data(ix, OutputListModel.IdRole)),
                           label: String(model.data(ix, OutputListModel.NameRole)) })
        }
        field._options = options
        if (!options.some(o => o.value === field.value))
            field.picked("")
    }

    Component.onCompleted: field._rebuild()
    onOutputsChanged: field._rebuild()
    onAllLabelChanged: field._rebuild()

    Connections {
        target: field.outputs
        function onModelReset() { field._rebuild() }
        function onRowsInserted() { field._rebuild() }
        function onRowsRemoved() { field._rebuild() }
        function onRowsMoved() { field._rebuild() }
        function onDataChanged() { field._rebuild() }
    }
}
