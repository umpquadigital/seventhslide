pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SeventhSlide

// The Audio Mixer panel, after OBS's vertical audio mixer: a Stacks tab with a
// strip per stack and an Outputs tab with a strip per output, side by side
// and scrolling sideways. Listen plays on this computer's default sound
// device. Every change is kept on this computer for the open library as it is
// made (see mixer_controller.hpp); there is nothing to save. The panel's
// frame and title are DockPanel's.
Item {
    id: root
    required property SessionFacade session
    /// A change the mixer refused.
    signal failed(string message)

    readonly property MixerController mixer: root.session ? root.session.mixer : null
    readonly property bool outputsTab: !!root.mixer && root.mixer.shownTab === "outputs"

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PanelTabStrip {
            Layout.fillWidth: true
            titles: [qsTr("Stacks"), qsTr("Outputs")]
            currentIndex: root.outputsTab ? 1 : 0
            onTabClicked: index => {
                if (root.mixer)
                    root.mixer.shownTab = index === 1 ? "outputs" : "stacks"
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Style.border
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Style.wellBg
            clip: true

            ListView {
                id: strips
                anchors.fill: parent
                orientation: ListView.Horizontal
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                model: !root.mixer ? null : (root.outputsTab ? root.mixer.outputs : root.mixer.stacks)
                ScrollBar.horizontal: AppScrollBar {}
                // The strip's other properties are the model's roles.
                delegate: MixerStrip {
                    height: strips.height - Style.scrollGutterH(strips)
                    mixer: root.mixer
                    kind: root.outputsTab ? "output" : "stack"
                    onFailed: message => root.failed(message)
                }
            }
        }
    }
}
