// ThemeGalleryView (src/ui/dialogs/ThemeGalleryView.qml): the gallery the output
// dialog and the theme picker share. The browsing rules have their own test
// (test_theme_gallery.cpp); this one covers the view's wiring.

#include <catch2/catch_test_macros.hpp>

#include "facade_all.hpp"
#include "image_providers.hpp"
#include "test_qml.hpp"
#include "test_qt_app.hpp"
#include "ui_test_helpers.hpp"

#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace {

constexpr const char* k_gallery_qml = R"QML(
import QtQuick
import SeventhSlide
import SeventhSlide as Ui

Window {
    id: win
    required property SessionFacade facade
    width: 640; height: 400; visible: true
    property alias view: view
    property int menusClosed: 0
    property var held: null
    property string toggled: ""

    Ui.ThemeGalleryView {
        id: view
        anchors.fill: parent
        session: win.facade
        themes: [
            { id: "a", name: "Beta", tags: ["live", "stage"] },
            { id: "b", name: "alpha", tags: ["live"] },
            { id: "c", name: "Gamma", tags: [] }
        ]
        defaults: ({ song: { theme: "c" } })
        onMenusClosed: win.menusClosed++
        onDefaultToggled: function (category, field, themeId) {
            win.toggled = category + ":" + field + ":" + themeId
        }
    }

    function ids(list) { return list.map(function (t) { return t.id }).join(",") }
    function hold() { win.held = view.items }
    function stillHeld() { return win.held === view.items }
}
)QML";

struct GalleryWindow {
    // First member: the facade below needs the application to exist.
    bool app = (seventhslide::test::ensure_qt_application(), true);
    seventhslide::app::SessionFacade facade;
    seventhslide::test::QmlScene scene;
    QObject* window = nullptr;
    QObject* view = nullptr;

    GalleryWindow()
    {
        window = scene.create(k_gallery_qml,
                              QUrl::fromLocalFile(QStringLiteral(SEVENTHSLIDE_UI_QML_DIR "/")),
                              {{QStringLiteral("facade"), QVariant::fromValue(&facade)}});
        view = window->property("view").value<QObject*>();
        REQUIRE(view != nullptr);
    }

    [[nodiscard]] QString ids(const char* list) const
    {
        QVariant out;
        REQUIRE(QMetaObject::invokeMethod(window, "ids", Q_RETURN_ARG(QVariant, out),
                                          Q_ARG(QVariant, view->property(list))));
        return out.toString();
    }
    [[nodiscard]] bool call_bool(const char* method) const
    {
        QVariant out;
        REQUIRE(QMetaObject::invokeMethod(window, method, Q_RETURN_ARG(QVariant, out)));
        return out.toBool();
    }
};

}  // namespace

TEST_CASE("Theme gallery view narrows and orders the owner's themes", "[ui][gallery]")
{
    GalleryWindow g;
    CHECK(g.ids("matches") == QStringLiteral("b,a,c"));
    CHECK(g.ids("items") == QStringLiteral("b,a,c"));

    g.view->setProperty("searchText", QStringLiteral("stage"));
    CHECK(g.ids("matches") == QStringLiteral("a"));
    CHECK(g.view->property("filtered").toBool());

    g.view->setProperty("searchText", QString());
    g.view->setProperty("defaultsOnly", true);
    CHECK(g.ids("matches") == QStringLiteral("c"));
}

TEST_CASE("Theme gallery view leads the unfiltered list with the Inherit tiles", "[ui][gallery]")
{
    GalleryWindow g;
    const auto tile = [](const QString& id) {
        return QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("caption"), id}};
    };
    g.view->setProperty("inheritTiles", QVariantList{tile(QString())});
    CHECK(g.ids("items") == QStringLiteral(",b,a,c"));
    // Each tile picks its own id, in the order given.
    g.view->setProperty("inheritTiles",
                        QVariantList{tile(QString()), tile(QStringLiteral("@above"))});
    CHECK(g.ids("items") == QStringLiteral(",@above,b,a,c"));

    // A search hides it: it is not a match for anything.
    g.view->setProperty("searchText", QStringLiteral("a"));
    CHECK(!g.ids("items").startsWith(QLatin1Char(',')));
}

TEST_CASE("Theme gallery view keeps its card list while only the selection moves", "[ui][gallery]")
{
    // The Repeater rebuilds every card when its model changes identity, so the
    // lists are properties that change only with what they are computed from.
    GalleryWindow g;
    REQUIRE(QMetaObject::invokeMethod(g.window, "hold"));
    g.view->setProperty("selectedId", QStringLiteral("a"));
    CHECK(g.call_bool("stillHeld"));

    g.view->setProperty("sortAsc", false);
    CHECK(!g.call_bool("stillHeld"));
}

TEST_CASE("Theme gallery view tells its owner when it closes its menus", "[ui][gallery]")
{
    GalleryWindow g;
    REQUIRE(QMetaObject::invokeMethod(g.view, "closeMenus"));
    CHECK(!g.view->property("menuOpen").toBool());
    CHECK(g.window->property("menusClosed").toInt() == 1);
}

TEST_CASE("Theme gallery view: a card's Default chip makes it the default", "[ui][gallery]")
{
    using seventhslide::test::centre_of;
    using seventhslide::test::visible_text_under;
    GalleryWindow g;
    g.view->setProperty("editable", true);
    auto* window = qobject_cast<QQuickWindow*>(g.window);
    REQUIRE(window != nullptr);
    QTest::qWait(50);

    // The card named ``name``: up from its name to the item with its theme id.
    const auto card_of = [&](const QString& name) -> QQuickItem* {
        QQuickItem* item = visible_text_under(window->contentItem(), name);
        while (item && !item->property("themeId").isValid()) {
            item = item->parentItem();
        }
        REQUIRE(item != nullptr);
        return item;
    };
    QQuickItem* alpha = card_of(QStringLiteral("alpha"));
    QQuickItem* gamma = card_of(QStringLiteral("Gamma"));

    // Gamma holds the song default and shows it; alpha shows nothing until
    // the pointer is on it, then the chip it could take: a song theme is only
    // ever the songs' default.
    CHECK(visible_text_under(gamma, QStringLiteral("DEFAULT")) != nullptr);
    CHECK(visible_text_under(alpha, QStringLiteral("DEFAULT")) == nullptr);
    QTest::mouseMove(window, centre_of(alpha));
    // Shown once the hover has reached the card and it is laid out again: a
    // frame or two, longer on a slow or busy machine.
    QQuickItem* song = nullptr;
    REQUIRE(QTest::qWaitFor([&] { return (song = visible_text_under(alpha, QStringLiteral("DEFAULT"))) != nullptr; },
                            2000));
    REQUIRE(visible_text_under(alpha, QStringLiteral("BIBLE")) == nullptr);
    REQUIRE(visible_text_under(alpha, QStringLiteral("MEDIA")) == nullptr);
    // The pointer moved onto a chip: it stays, blue, so it can be clicked -
    // and stays from frame to frame, not hidden as the chip takes the hover.
    // Blue once the hover has reached the chip, and from then on in every
    // frame. A chip only just shown takes no hover from the move that lands
    // on it in its first frame, and a move to where the pointer already is
    // is no move, so the pointer stirs on the chip until it is blue, as a
    // hand's would; longer on a slow or busy machine.
    const auto blue = [&] {
        QQuickItem* chip = visible_text_under(alpha, QStringLiteral("DEFAULT"));
        return chip != nullptr && chip->parentItem()->property("tone").toString() == QStringLiteral("accent");
    };
    int stir = 0;
    REQUIRE(QTest::qWaitFor(
        [&] {
            QTest::mouseMove(window, centre_of(song) + QPoint(++stir % 2, 0));
            return blue();
        },
        2000));
    for (int i = 0; i < 10; ++i) {
        QTest::qWait(20);
        QQuickItem* still = visible_text_under(alpha, QStringLiteral("DEFAULT"));
        REQUIRE(still != nullptr);
        REQUIRE(still->parentItem()->property("tone").toString() == QStringLiteral("accent"));
    }
    // Found again where it is now, and the click's effect waited for: on a
    // slow or busy machine either can lag the pointer.
    REQUIRE((song = visible_text_under(alpha, QStringLiteral("DEFAULT"))) != nullptr);
    seventhslide::test::click(window, centre_of(song));
    CHECK(QTest::qWaitFor(
        [&] { return g.window->property("toggled").toString() == QStringLiteral("song:theme:b"); },
        2000));

    // A chip the card already holds does nothing.
    g.window->setProperty("toggled", QString());
    const QPoint held = centre_of(visible_text_under(gamma, QStringLiteral("DEFAULT")));
    QTest::mouseMove(window, held);
    QTest::qWait(20);
    seventhslide::test::click(window, held);
    CHECK(g.window->property("toggled").toString().isEmpty());
}

TEST_CASE("Theme gallery view asks only for the pictures near the view", "[ui][gallery]")
{
    GalleryWindow g;
    QVariantList many;
    for (int i = 0; i < 200; ++i) {
        const QString id = QStringLiteral("t%1").arg(i, 3, 10, QLatin1Char('0'));
        many.push_back(QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("name"), id}});
    }
    seventhslide::app::add_image_providers(g.scene.engine(), g.facade);
    g.view->setProperty("outputName", QStringLiteral("Main"));
    g.view->setProperty("themes", many);
    auto* window = qobject_cast<QQuickWindow*>(g.window);
    REQUIRE(window != nullptr);
    QTest::qWait(50);

    // Whether the card for ``id`` has asked for its picture.
    const auto asked = [&](const QString& id) {
        const std::function<QQuickItem*(QQuickItem*)> thumb_of = [&](QQuickItem* item) {
            for (QQuickItem* child : item->childItems()) {
                if (child->objectName() == QStringLiteral("themeThumb")
                    && child->parentItem()->parentItem()->property("themeId").toString() == id) {
                    return child;
                }
                if (QQuickItem* found = thumb_of(child)) {
                    return found;
                }
            }
            return static_cast<QQuickItem*>(nullptr);
        };
        QQuickItem* thumb = thumb_of(window->contentItem());
        REQUIRE(thumb != nullptr);
        return !thumb->property("source").toUrl().isEmpty();
    };

    CHECK(asked(QStringLiteral("t000")));
    CHECK_FALSE(asked(QStringLiteral("t199")));

    // Brought into view it asks; what it left behind keeps its picture.
    REQUIRE(QMetaObject::invokeMethod(g.view, "reveal", Q_ARG(QVariant, QStringLiteral("t199"))));
    QTest::qWait(50);
    CHECK(asked(QStringLiteral("t199")));
    CHECK(asked(QStringLiteral("t000")));
}
