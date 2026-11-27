import QtQuick
import SeventhSlide

// A flag in the sidebar, by its name in the property table (or a theme
// setting's, ``from: "setting"``): a ToggleSwitch that shows it and turns it
// over.
ToggleSwitch {
    id: toggle
    required property SlideEditState dialog
    required property string prop
    /// selection | setting
    property string from: "selection"

    checked: !!(toggle.from === "setting" ? toggle.dialog.setting(toggle.prop)
                                          : toggle.dialog.sel[toggle.prop])
    onToggled: {
        if (toggle.from === "setting")
            toggle.dialog.setSetting(toggle.prop, !toggle.checked)
        else
            toggle.dialog.setProp(toggle.prop, !toggle.checked)
    }
}
