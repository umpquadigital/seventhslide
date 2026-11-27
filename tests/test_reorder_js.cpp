// Drag-to-reorder arithmetic (src/ui/js/Reorder.js): where a pointer drops a
// row, and where the row lands. Run through a real QML engine the way the
// lists load it.

#include <catch2/catch_test_macros.hpp>

#include "test_qml.hpp"

#include <QMetaObject>
#include <QString>
#include <QUrl>
#include <QVariant>
#include <QVariantMap>

namespace {

QUrl probe_url()
{
    return QUrl::fromLocalFile(QStringLiteral(SEVENTHSLIDE_UI_QML_DIR)
                               + QStringLiteral("/js/ReorderProbe.qml"));
}

QVariantMap run(QObject* probe)
{
    QVariant out;
    REQUIRE(QMetaObject::invokeMethod(probe, "run", Q_RETURN_ARG(QVariant, out)));
    return out.toMap();
}

}  // namespace

TEST_CASE("Reorder.js: where a dropped row lands", "[ui][reorder]")
{
    seventhslide::test::QmlScene scene;
    QObject* probe = scene.create(R"(
import QtQuick
import "Reorder.js" as Reorder
QtObject {
    function run() {
        return {
            // 3 rows 20 tall, 4 apart: tops 0, 24, 48.
            pitchTop: Reorder.slotByPitch(3, 20, 4, 0),
            pitchUpperHalf: Reorder.slotByPitch(3, 20, 4, 9),
            pitchLowerHalf: Reorder.slotByPitch(3, 20, 4, 11),
            pitchPastEnd: Reorder.slotByPitch(3, 20, 4, 500),
            downOne: Reorder.landing(0, 2, 3),
            upOne: Reorder.landing(2, 1, 3),
            toEnd: Reorder.landing(0, 3, 3),
            ownSlot: Reorder.landing(1, 1, 3),
            slotBelowSelf: Reorder.landing(1, 2, 3),
            pastEnd: Reorder.landing(0, 4, 3),
            moved: Reorder.moved(["a", "b", "c"], 0, 2).join(",")
        }
    }
}
)",
                                  probe_url());
    const QVariantMap r = run(probe);
    const auto num = [&r](const char* key) { return r.value(QLatin1String(key)).toInt(); };

    CHECK(num("pitchTop") == 0);
    CHECK(num("pitchUpperHalf") == 0);
    CHECK(num("pitchLowerHalf") == 1);
    CHECK(num("pitchPastEnd") == 3);
    CHECK(num("downOne") == 1);
    CHECK(num("upOne") == 1);
    CHECK(num("toEnd") == 2);
    CHECK(num("ownSlot") == -1);       // dropped where it is
    CHECK(num("slotBelowSelf") == -1); // just below itself is still where it is
    CHECK(num("pastEnd") == -1);
    CHECK(r.value(QStringLiteral("moved")).toString() == QStringLiteral("b,c,a"));
}

TEST_CASE("Reorder.js: the slot under the pointer in a spaced list", "[ui][reorder]")
{
    seventhslide::test::QmlScene scene;
    QObject* probe = scene.create(R"(
import QtQuick
import "Reorder.js" as Reorder
ListView {
    id: view
    width: 100
    height: 400
    // 4 rows 20 tall, 4 apart: tops 0, 24, 48, 72; the last ends at 92.
    model: 4
    spacing: 4
    delegate: Rectangle {
        required property int index
        width: ListView.view.width
        height: 20
    }
    function run() {
        view.forceLayout()
        return {
            count: view.count,
            upperHalf: Reorder.slotInView(view, 5),
            lowerHalf: Reorder.slotInView(view, 15),
            firstGap: Reorder.slotInView(view, 22),
            secondGap: Reorder.slotInView(view, 46),
            lastRow: Reorder.slotInView(view, 85),
            justBelowLast: Reorder.slotInView(view, 94),
            farBelow: Reorder.slotInView(view, 300)
        }
    }
}
)",
                                  probe_url());
    const QVariantMap r = run(probe);
    const auto num = [&r](const char* key) { return r.value(QLatin1String(key)).toInt(); };

    REQUIRE(num("count") == 4);
    CHECK(num("upperHalf") == 0);
    CHECK(num("lowerHalf") == 1);
    // The spacing between two rows drops between them, not after the last row.
    CHECK(num("firstGap") == 1);
    CHECK(num("secondGap") == 2);
    CHECK(num("lastRow") == 4);
    CHECK(num("justBelowLast") == 4);
    CHECK(num("farBelow") == 4);
}
