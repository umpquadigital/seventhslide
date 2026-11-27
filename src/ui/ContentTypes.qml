pragma Singleton

import QtQuick

// The kinds of content: what a stack receives, what an output shows, what a
// Library tab holds, what a rundown row is. Every view names and draws a kind
// from this one table. The keys are core's k_stack_type_keys, in that order;
// ``tab`` is the Library tab that holds the kind (LibraryTabs.js's id);
// ``label`` names the kind as a tab or a list does, ``singular`` one of it.
QtObject {
    id: types

    /// [{key, tab, label, singular, icon}], in the order they are listed.
    readonly property var items: [
        { key: "song", tab: "songs", label: qsTr("Songs"), singular: qsTr("Song"), icon: "song" },
        { key: "bible", tab: "bibles", label: qsTr("Bibles"), singular: qsTr("Bible"), icon: "bible" },
        { key: "freeform", tab: "freeform", label: qsTr("Freeform"), singular: qsTr("Freeform"), icon: "freeform" },
        { key: "image", tab: "images", label: qsTr("Images"), singular: qsTr("Image"), icon: "image" },
        { key: "video", tab: "videos", label: qsTr("Videos"), singular: qsTr("Video"), icon: "video" },
        { key: "audio", tab: "audio", label: qsTr("Audio"), singular: qsTr("Audio"), icon: "audio" },
        { key: "web", tab: "browser", label: qsTr("Browser"), singular: qsTr("Web page"), icon: "globe" },
        { key: "live", tab: "live", label: qsTr("Live"), singular: qsTr("Live source"), icon: "cast" }
    ]

    /// The kind ``key``, or null when it is not one.
    function of(key) {
        return types.items.find(item => item.key === key) ?? null
    }

    /// The kind Library tab ``tab`` holds, or null.
    function ofTab(tab) {
        return types.items.find(item => item.tab === tab) ?? null
    }
}
