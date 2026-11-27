// The theme gallery's browsing rules (src/ui/js/ThemeGallery.js), run through a
// real QML engine the way both dialogs load them.

#include <catch2/catch_test_macros.hpp>

#include "test_qml.hpp"

#include <QMetaObject>
#include <QString>
#include <QUrl>
#include <QVariant>
#include <QVariantMap>

TEST_CASE("theme gallery filters, sorts, pages and fits tag chips", "[ui][gallery]")
{
    seventhslide::test::QmlScene scene;
    QObject* probe = scene.create(R"(
import QtQuick
import "ThemeGallery.js" as G
QtObject {
    readonly property var themes: [
        { id: "a", name: "Beta", createdAt: "2026-01-04", tags: ["live", "stage"] },
        { id: "b", name: "alpha", createdAt: "2026-01-03", tags: ["live"] },
        { id: "c", name: "Gamma", createdAt: "2026-01-01", tags: [] }
    ]
    function tagsOf(t) { return t.tags }
    function ids(list) { return list.map(function (t) { return t.id }).join(",") }
    function run() {
        const base = { query: "", tagFilter: "", defaultsOnly: false, defaultIds: [],
                       sortKey: "name", sortAsc: true, tagsOf: tagsOf }
        function with_(o) { return Object.assign({}, base, o) }
        return {
            byName: ids(G.filterAndSort(themes, base)),
            byAddedDesc: ids(G.filterAndSort(themes, with_({ sortKey: "added", sortAsc: false }))),
            tagLive: ids(G.filterAndSort(themes, with_({ tagFilter: "live" }))),
            searchTag: ids(G.filterAndSort(themes, with_({ query: " STAG " }))),
            defaults: ids(G.filterAndSort(themes, with_({ defaultsOnly: true, defaultIds: ["c", ""] }))),
            catalog: G.tagCatalog(themes, tagsOf).join(","),
            menu: G.tagMenuItems(["live", "stage"], "st", "all").map(function (r) { return r.label }).join(",")
        }
    }
}
)",
                                  QUrl::fromLocalFile(QStringLiteral(SEVENTHSLIDE_UI_QML_DIR)
                                                      + QStringLiteral("/js/GalleryProbe.qml")));

    QVariant out;
    REQUIRE(QMetaObject::invokeMethod(probe, "run", Q_RETURN_ARG(QVariant, out)));
    const QVariantMap r = out.toMap();
    const auto str = [&r](const char* key) { return r.value(QLatin1String(key)).toString(); };

    CHECK(str("byName") == QStringLiteral("b,a,c"));       // case-insensitive name order
    CHECK(str("byAddedDesc") == QStringLiteral("a,b,c"));  // newest first
    CHECK(str("tagLive") == QStringLiteral("b,a"));
    CHECK(str("searchTag") == QStringLiteral("a"));        // query matches a tag
    CHECK(str("defaults") == QStringLiteral("c"));
    CHECK(str("catalog") == QStringLiteral("live,stage"));
    CHECK(str("menu") == QStringLiteral("stage"));         // "all" does not match "st"
}
