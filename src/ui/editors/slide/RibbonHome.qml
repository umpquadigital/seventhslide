pragma ComponentBehavior: Bound
import QtQuick
import SeventhSlide

// The Home tab: the slide, the type, the paragraph and arranging. What is
// done often and is not about the selection alone; the sidebar has the rest.
Row {
    id: tab
    required property SlideEditState dialog
    readonly property SessionFacade session: tab.dialog.session
    readonly property bool text: tab.dialog.selSupports("font")
    readonly property bool runs: tab.dialog.selSupports("runs")
    spacing: 0
    height: parent ? parent.height : implicitHeight

    RibbonGroup {
        first: true
        visible: tab.dialog.isFreeform
        height: parent.height
        row1: [
            RibbonButton {
                objectName: "ribbonNewSlide"
                icon: "newSlide"
                label: qsTr("New slide")
                onClicked: tab.dialog.report(tab.dialog.edit.slides.insert())
            }
        ]
        row2: [
            RibbonButton {
                objectName: "ribbonDuplicateSlide"
                icon: "copy"
                label: qsTr("Duplicate")
                onClicked: tab.dialog.report(tab.dialog.edit.slides.duplicate())
            },
            RibbonButton {
                objectName: "ribbonDeleteSlide"
                icon: "trash"
                hint: qsTr("Delete slide")
                enabled: tab.dialog.edit.slides.count > 1
                onClicked: tab.dialog.report(tab.dialog.edit.slides.remove())
            }
        ]
    }
    RibbonGroup {
        first: !tab.dialog.isFreeform
        height: parent.height
        enabled: tab.text
        row1: [
            RibbonDropdown {
                objectName: "ribbonFontFamily"
                fieldWidth: 150
                options: tab.session ? tab.session.fonts.families : []
                value: tab.dialog.fontFamilyShown()
                onPicked: function (v) { tab.dialog.commitFontFamily(v) }
            },
            NumberField {
                objectName: "ribbonFontSize"
                fieldWidth: 60
                minValue: 8
                maxValue: 600
                value: Number(tab.dialog.fontSizeShown())
                onCommit: function (n) { tab.dialog.commitFontSize(n) }
            }
        ]
        row2: [
            RibbonStrip {
                objectName: "ribbonTypeStrip"
                items: [
                    { id: "bold", label: "B", bold: true, on: tab.dialog.flagOn("bold") },
                    { id: "italic", label: "I", italic: true, on: tab.dialog.flagOn("italic") },
                    { id: "underline", label: "U", underline: true, on: tab.dialog.flagOn("underline") },
                    { id: "super", label: "x²", on: tab.dialog.superOn() }
                ]
                onClicked: function (id) {
                    if (id === "super")
                        tab.dialog.toggleSuper()
                    else
                        tab.dialog.toggleFlag(id)
                }
            },
            Item { width: 1; height: 1 },
            RibbonSwatch {
                objectName: "ribbonFontColor"
                letter: "A"
                value: String(tab.dialog.fontColorShown() || "")
                dialogTitle: qsTr("Text color")
                onEdited: function (v) { tab.dialog.commitFontColor(v) }
            },
            RibbonSwatch {
                objectName: "ribbonTextOutlineColor"
                icon: "lineColor"
                value: String(tab.dialog.sel.text_outline_color || "")
                dialogTitle: qsTr("Text outline color")
                onEdited: function (v) {
                    // A colour shows only on an outline with some width.
                    if (!(Number(tab.dialog.sel.text_outline_width) > 0))
                        tab.dialog.setProp("text_outline_width", 2)
                    tab.dialog.setProp("text_outline_color", v)
                }
            }
        ]
    }
    RibbonGroup {
        height: parent.height
        enabled: tab.text
        row1: [
            RibbonStrip {
                objectName: "ribbonAlignStrip"
                readonly property string align: String(tab.dialog.paragraphValue("align", tab.dialog.sel.align))
                items: [
                    { id: "left", icon: "textLeft", on: align === "left" },
                    { id: "center", icon: "textCenter", on: align === "center" },
                    { id: "right", icon: "textRight", on: align === "right" }
                ]
                onClicked: function (id) { tab.dialog.setParagraphProp("align", id) }
            },
            // Bullets: a mark before each line; the one that is on, picked, goes off.
            RibbonChoice {
                objectName: "ribbonBullets"
                icon: "bullets"
                enabled: tab.runs
                value: String(tab.dialog.sel.bullets)
                defaultValue: "disc"
                options: [
                    { value: "disc", label: qsTr("Bullets") },
                    { value: "number", label: qsTr("Numbering") }
                ]
                onPicked: function (v) { tab.dialog.setProp("bullets", tab.dialog.sel.bullets === v ? "none" : v) }
            }
        ]
        row2: [
            RibbonStrip {
                objectName: "ribbonValignStrip"
                items: [
                    { id: "top", icon: "textTop", on: tab.dialog.sel.valign === "top" },
                    { id: "middle", icon: "textMiddle", on: tab.dialog.sel.valign === "middle" },
                    { id: "bottom", icon: "textBottom", on: tab.dialog.sel.valign === "bottom" }
                ]
                onClicked: function (id) { tab.dialog.setProp("valign", id) }
            },
            RibbonChoice {
                objectName: "ribbonLineHeight"
                icon: "lineSpacing"
                enabled: tab.runs
                value: Number(tab.dialog.paragraphValue("line_height", tab.dialog.sel.line_height))
                options: [
                    { value: 1, label: qsTr("Single") },
                    { value: 1.15, label: "1.15" },
                    { value: 1.5, label: "1.5" },
                    { value: 2, label: qsTr("Double") }
                ]
                onPicked: function (v) { tab.dialog.setParagraphProp("line_height", v) }
            },
            // Columns: the sidebar's Text options set the gap.
            RibbonChoice {
                objectName: "ribbonColumns"
                icon: "columns"
                enabled: tab.dialog.selType === "text" && tab.dialog.selBinding === "static" && !tab.dialog.sel.cue_linked
                value: Number(tab.dialog.sel.columns)
                options: [
                    { value: 1, label: qsTr("One column") },
                    { value: 2, label: qsTr("Two columns") },
                    { value: 3, label: qsTr("Three columns") }
                ]
                onPicked: function (v) { tab.dialog.setProp("columns", v) }
            }
        ]
    }
    RibbonGroup {
        grow: true
        height: parent.height
        enabled: tab.dialog.hasSel
        row1: [
            RibbonMenuButton {
                id: alignButton
                objectName: "ribbonAlign"
                icon: "alignLeft"
                label: qsTr("Align")
                enabled: tab.dialog.hasSel
                onClicked: alignButton.toggleMenu()
                MenuItemRow { icon: "alignLeft"; label: qsTr("Left"); onTriggered: tab.arrange("align", CanvasAlign.Left) }
                MenuItemRow { icon: "alignCenter"; label: qsTr("Center"); onTriggered: tab.arrange("align", CanvasAlign.Center) }
                MenuItemRow { icon: "alignRight"; label: qsTr("Right"); onTriggered: tab.arrange("align", CanvasAlign.Right) }
                MenuRule {}
                MenuItemRow { icon: "alignTop"; label: qsTr("Top"); onTriggered: tab.arrange("align", CanvasAlign.Top) }
                MenuItemRow { icon: "alignMiddle"; label: qsTr("Middle"); onTriggered: tab.arrange("align", CanvasAlign.Middle) }
                MenuItemRow { icon: "alignBottom"; label: qsTr("Bottom"); onTriggered: tab.arrange("align", CanvasAlign.Bottom) }
                MenuRule {}
                MenuItemRow { icon: "distributeH"; label: qsTr("Distribute horizontally"); enabled: tab.dialog.selCount >= 3; onTriggered: tab.arrange("distribute", CanvasDistribute.Horizontal) }
                MenuItemRow { icon: "distributeV"; label: qsTr("Distribute vertically"); enabled: tab.dialog.selCount >= 3; onTriggered: tab.arrange("distribute", CanvasDistribute.Vertical) }
            },
            RibbonMenuButton {
                id: orderButton
                objectName: "ribbonOrder"
                icon: "toFront"
                label: qsTr("Order")
                enabled: tab.dialog.hasSel
                onClicked: orderButton.toggleMenu()
                MenuItemRow { icon: "toFront"; label: qsTr("Bring to front"); shortcut: Platform.shortcutText("Ctrl+Shift+]"); enabled: tab.dialog.canRaiseSel; onTriggered: tab.arrange("order", CanvasOrder.Front) }
                MenuItemRow { icon: "up"; label: qsTr("Bring forward"); shortcut: Platform.shortcutText("Ctrl+]"); enabled: tab.dialog.canRaiseSel; onTriggered: tab.arrange("order", CanvasOrder.Forward) }
                MenuItemRow { icon: "down"; label: qsTr("Send backward"); shortcut: Platform.shortcutText("Ctrl+["); enabled: tab.dialog.canLowerSel; onTriggered: tab.arrange("order", CanvasOrder.Backward) }
                MenuItemRow { icon: "toBack"; label: qsTr("Send to back"); shortcut: Platform.shortcutText("Ctrl+Shift+["); enabled: tab.dialog.canLowerSel; onTriggered: tab.arrange("order", CanvasOrder.Back) }
            }
        ]
        row2: [
            RibbonMenuButton {
                id: groupButton
                objectName: "ribbonGroup"
                icon: tab.dialog.canUngroupSel ? "ungroup" : "group"
                label: tab.dialog.canUngroupSel ? qsTr("Ungroup") : qsTr("Group")
                enabled: tab.dialog.canGroupSel || tab.dialog.canUngroupSel
                onClicked: tab.arrange(tab.dialog.canUngroupSel ? "ungroup" : "group")
                MenuItemRow { icon: "group"; label: qsTr("Group"); shortcut: Platform.shortcutText("Ctrl+G"); enabled: tab.dialog.canGroupSel; onTriggered: tab.arrange("group") }
                MenuItemRow { icon: "ungroup"; label: qsTr("Ungroup"); shortcut: Platform.shortcutText("Ctrl+Shift+G"); enabled: tab.dialog.canUngroupSel; onTriggered: tab.arrange("ungroup") }
            },
            RibbonMenuButton {
                id: flipButton
                objectName: "ribbonFlip"
                icon: "flipH"
                label: qsTr("Flip")
                enabled: tab.dialog.hasSel
                onClicked: tab.arrange("flip", CanvasFlip.Horizontal)
                MenuItemRow { icon: "flipH"; label: qsTr("Flip horizontally"); onTriggered: tab.arrange("flip", CanvasFlip.Horizontal) }
                MenuItemRow { icon: "flipV"; label: qsTr("Flip vertically"); onTriggered: tab.arrange("flip", CanvasFlip.Vertical) }
            },
            RibbonButton {
                objectName: "ribbonLock"
                icon: tab.dialog.selLocked ? "lock" : "unlock"
                hint: tab.dialog.selLocked ? qsTr("Unlock") : qsTr("Lock")
                on: tab.dialog.selLocked
                enabled: tab.dialog.hasSel
                onClicked: tab.arrange("lock", !tab.dialog.selLocked)
            },
            RibbonButton {
                objectName: "ribbonDelete"
                icon: "trash"
                hint: qsTr("Delete")
                enabled: tab.dialog.hasSel
                onClicked: tab.dialog.report(tab.dialog.edit.objects.deleteSelected())
            }
        ]
    }

    /// An Arrange choice: align, distribute, group, ungroup, order, flip, lock.
    function arrange(action, value) {
        const o = tab.dialog.edit.objects
        tab.dialog.endTextEdit(true)
        switch (action) {
        case "align": tab.dialog.report(o.align(value)); break
        case "distribute": tab.dialog.report(o.distribute(value)); break
        case "group": tab.dialog.report(o.group()); break
        case "ungroup": tab.dialog.report(o.ungroup()); break
        case "order": tab.dialog.report(o.reorderSelected(value)); break
        case "flip": tab.dialog.report(o.flip(value)); break
        case "lock": tab.dialog.report(o.setLocked(value)); break
        }
    }
}
