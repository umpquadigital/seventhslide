pragma ComponentBehavior: Bound
import QtQuick
import SeventhSlide

// A row's icon choice (a stack's, an output's): a button showing the current
// glyph and a caret, opening a grid of glyphs lined up with the button's right
// edge. ``icons`` are glyph ids (a stack's list, an output's).
AppButton {
    id: button
    property var icons: []
    property string current: ""
    signal picked(string id)

    function _url(id, ink) {
        return Icons.url(id, ink, 2)
    }

    size: "control"
    splitMenu: true
    menuOpen: grid.visible
    iconSource: button._url(button.current, button.ink)
    onClicked: grid.visible ? grid.close() : grid.openAt()
    onMenuClicked: grid.visible ? grid.close() : grid.openAt()

    AppMenu {
        id: grid
        anchorItem: button
        alignRight: true
        minWidth: 0

        MenuHeading { text: qsTr("Icon") }
        Grid {
            leftPadding: 4
            rightPadding: 4
            bottomPadding: 4
            columns: 6
            spacing: 3
            Repeater {
                model: button.icons
                delegate: AppButton {
                    id: choice
                    required property string modelData
                    size: "standard"
                    ghost: choice.modelData !== button.current
                    primary: choice.modelData === button.current
                    iconSource: button._url(choice.modelData, choice.ink)
                    onClicked: {
                        grid.close()
                        button.picked(choice.modelData)
                    }
                }
            }
        }
    }
}
