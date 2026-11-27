pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SeventhSlide

// The Service panel's toolbar, flat on the panel: the service picker, editing
// and removing items, the stack menu, the service settings and ⋯ (a divider,
// playing items together).
Rectangle {
    id: svcToolbar
    /// Untyped, since ServicePanel makes this type and Qt refuses the cycle;
    /// tools/panel_refs.py checks the names used on it.
    required property var panel // ServicePanel
    required property Popup stackMenu
    Layout.fillWidth: true
    Layout.preferredHeight: Style.controlH + 16
    color: Style.panelBg

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        anchors.topMargin: 8
        anchors.bottomMargin: 8
        spacing: 5

        // The open service: opens the Services window.
        DropdownField {
            Layout.fillWidth: true
            Layout.preferredHeight: Style.controlH
            Layout.maximumWidth: 10000
            text: svcToolbar.panel.session && svcToolbar.panel.session.library.openServiceName.length
                  ? svcToolbar.panel.session.library.openServiceName
                  : qsTr("Select service…")
            placeholder: !(svcToolbar.panel.session && svcToolbar.panel.session.openServiceId >= 0)
            open: svcToolbar.panel.pickerOpen
            onPressed: svcToolbar.panel.interacted()
            onClicked: svcToolbar.panel.servicePicker().show()
        }

        ServiceToolButton {
            panel: svcToolbar.panel
            icon: "pen"
            enabled: svcToolbar.panel.canEditItem
            onClicked: svcToolbar.panel.toolEditItem()
        }
        ServiceToolButton {
            panel: svcToolbar.panel
            icon: "trash"
            enabled: svcToolbar.panel.hasItemSelection
            onClicked: svcToolbar.panel.toolDeleteItem()
        }
        ServiceToolButton {
            id: svcStackBtn
            panel: svcToolbar.panel
            icon: "layers"
            visible: svcToolbar.panel.stacksActive
            enabled: svcToolbar.panel.canSetItemStack
            onClicked: {
                if (svcToolbar.stackMenu.opened)
                    svcToolbar.stackMenu.close()
                else
                    svcToolbar.panel.openStackMenu(svcStackBtn)
            }
        }
        ServiceToolButton {
            panel: svcToolbar.panel
            icon: "settings"
            enabled: svcToolbar.panel.serviceOpen
            onClicked: svcToolbar.panel.serviceSettings().openDialog()
        }
        ServiceToolButton {
            id: svcAddBtn
            panel: svcToolbar.panel
            icon: "more"
            enabled: svcToolbar.panel.serviceOpen
            onClicked: svcAddMenu.visible ? svcAddMenu.close() : svcAddMenu.openAt()

            AppMenu {
                id: svcAddMenu
                anchorItem: svcAddBtn
                alignRight: true

                MenuItemRow {
                    icon: "divider"
                    label: qsTr("Add Divider")
                    onTriggered: {
                        svcAddMenu.close()
                        svcToolbar.panel.openDividerDialog("add")
                    }
                }
                MenuItemRow {
                    icon: "export"
                    label: qsTr("Export service…")
                    onTriggered: {
                        svcAddMenu.close()
                        svcToolbar.panel.serviceSettings().openDialog("export")
                    }
                }
                MenuItemRow {
                    visible: svcToolbar.panel.stacksActive
                    enabled: svcToolbar.panel.canLinkItems
                    icon: "chain"
                    label: (svcToolbar.panel.selectionLinkableCount === 1
                            && svcToolbar.panel.selectionIsLinked)
                           ? qsTr("Unlink") : qsTr("Play Together")
                    onTriggered: {
                        svcAddMenu.close()
                        svcToolbar.panel.linkSelectedItems()
                    }
                }
            }
        }
    }
}
