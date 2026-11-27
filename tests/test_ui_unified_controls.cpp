// The redesign's shared controls (src/ui/controls): each loads, lays out the
// way pages rely on, and answers clicks the way its callers expect.

#include "glyphs.hpp"
#include "test_qml.hpp"
#include "ui_test_helpers.hpp"

#include <seventhslide/model/output_config.hpp>

#include <catch2/catch_test_macros.hpp>

#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QUrl>
#include <QVariant>

#include <string>

namespace {

using seventhslide::test::centre_of;
using seventhslide::test::click;
using seventhslide::test::QmlScene;
using seventhslide::test::visible_text_under;

QQuickWindow* show(QmlScene& scene, const char* qml)
{
    auto* window = qobject_cast<QQuickWindow*>(scene.create(
        qml, QUrl::fromLocalFile(QStringLiteral(SEVENTHSLIDE_UI_QML_DIR "/"))));
    REQUIRE(window != nullptr);
    window->show();
    REQUIRE(QTest::qWaitForWindowExposed(window));
    return window;
}

QQuickItem* item(QQuickWindow* window, const char* name)
{
    auto* found = window->findChild<QQuickItem*>(QString::fromLatin1(name));
    INFO(name);
    REQUIRE(found != nullptr);
    return found;
}

// Every new control, as a page would declare it.
constexpr const char* k_all_qml = R"QML(
import QtQuick
import QtQuick.Layouts
import SeventhSlide as Ui

Window {
    width: 600; height: 700
    Column {
        width: 500
        spacing: 6
        Ui.PageHeader {
            width: parent.width
            title: "Stacks"
            text: "Stacks let you show more than one thing on screen at once."
            tools: Ui.AppButton { primary: true; size: "control"; label: "New" }
        }
        Ui.GroupList {
            width: parent.width
            Ui.GroupRow {
                interactive: true
                Ui.HandleTile { text: "1"; draggable: true }
                Text { Layout.fillWidth: true; text: "Song / Item" }
                Ui.StatusBadge { text: "Highest" }
            }
            Ui.GroupRow { selected: true; Text { text: "Lower Thirds" } }
            Ui.GroupOpenBody {
                Ui.FormRow {
                    objectName: "nameRow"
                    width: parent.width
                    label: "Name"
                    Ui.DialogField { objectName: "nameField"; implicitWidth: Ui.Style.fieldLongW; text: "Lower Thirds" }
                }
                foot: Ui.AppButton { danger: true; size: "control"; label: "Delete Stack…" }
            }
        }
        Ui.ListWell {
            width: parent.width; height: 120
            Ui.WellPane {
                Layout.fillWidth: true; Layout.fillHeight: true
                title: "Libraries"
                Ui.PickRow { Layout.fillWidth: true; title: "Grace Church"; subtitle: "~/SeventhSlide"; selected: true; checked: true; trailingArrow: true }
                Ui.PickRow { Layout.fillWidth: true; title: "Archive"; trail: true }
            }
            Ui.WellPane {
                Layout.fillWidth: true; Layout.fillHeight: true
                ruleLeft: true
                title: "Profiles"
            }
        }
        Row {
            spacing: 6
            Ui.TagChip { text: "Sabbath"; removable: true }
            Ui.StatusBadge { text: "In use"; tone: "blue" }
            Ui.SearchField { width: 180 }
            Ui.SegmentedControl { options: ["Guided", "XML"] }
            Ui.AppButton { objectName: "boldOn"; size: "control"; label: "B"; toggled: true; toggledKind: "on" }
        }
    }
}
)QML";

}  // namespace

TEST_CASE("The redesign's shared controls load together", "[ui][unified]")
{
    QmlScene scene;
    QQuickWindow* window = show(scene, k_all_qml);
    CHECK(visible_text_under(window->contentItem(), QStringLiteral("Grace Church")) != nullptr);
    CHECK(visible_text_under(window->contentItem(), QStringLiteral("Delete Stack…")) != nullptr);
}

TEST_CASE("Fields and the buttons beside them share one height", "[ui][unified]")
{
    QmlScene scene;
    QQuickWindow* window = show(scene, k_all_qml);
    CHECK(item(window, "nameField")->height() == 26);
    CHECK(item(window, "boldOn")->height() == 26);
}

TEST_CASE("A form row puts its control at the row's right edge", "[ui][unified]")
{
    QmlScene scene;
    QQuickWindow* window = show(scene, k_all_qml);
    QQuickItem* row = item(window, "nameRow");
    QQuickItem* field = item(window, "nameField");
    const QPointF right = field->mapToItem(row, QPointF(field->width(), 0));
    CHECK(right.x() == row->width());
}

TEST_CASE("A checklist field sums up what is ticked", "[ui][unified]")
{
    QmlScene scene;
    QQuickWindow* window = show(scene, R"QML(
import QtQuick
import SeventhSlide as Ui
Window {
    width: 400; height: 300
    property alias field: field
    Ui.ChecklistField {
        id: field
        manyText: "%1 item types"
        model: [{ id: "a", label: "Songs" }, { id: "b", label: "Scripture" },
                { id: "c", label: "Images" }, { id: "d", label: "Videos" }]
    }
}
)QML");
    QObject* field = window->property("field").value<QObject*>();
    REQUIRE(field != nullptr);
    auto summary = [&] {
        QVariant out;
        REQUIRE(QMetaObject::invokeMethod(field, "summary", Q_RETURN_ARG(QVariant, out)));
        return out.toString();
    };
    CHECK(summary().isEmpty());
    field->setProperty("checkedIds", QStringList{QStringLiteral("b")});
    CHECK(summary() == QStringLiteral("Scripture"));
    field->setProperty("checkedIds", QStringList{QStringLiteral("c"), QStringLiteral("d")});
    CHECK(summary() == QStringLiteral("Images, Videos"));
    field->setProperty("checkedIds", QStringList{QStringLiteral("a"), QStringLiteral("b"),
                                                 QStringLiteral("c"), QStringLiteral("d")});
    CHECK(summary() == QStringLiteral("4 item types"));
}

TEST_CASE("A checklist stays open while boxes are ticked", "[ui][unified]")
{
    QmlScene scene;
    QQuickWindow* window = show(scene, R"QML(
import QtQuick
import SeventhSlide as Ui
Window {
    id: win
    width: 400; height: 300
    property alias field: field
    property var ticked: []
    Ui.ChecklistField {
        id: field
        x: 20; y: 20
        model: [{ id: "songs", label: "Songs" }, { id: "bible", label: "Scripture" }]
        checkedIds: win.ticked
        onToggled: function (id) {
            const t = win.ticked.slice()
            const i = t.indexOf(id)
            if (i >= 0) t.splice(i, 1); else t.push(id)
            win.ticked = t
        }
    }
}
)QML");
    QObject* field = window->property("field").value<QObject*>();
    REQUIRE(field != nullptr);
    click(window, centre_of(qobject_cast<QQuickItem*>(field)));
    REQUIRE(field->property("menuOpen").toBool());

    // Popups sit in the window's overlay, under its content item.
    QQuickItem* overlay = window->contentItem();
    QQuickItem* songs = visible_text_under(overlay, QStringLiteral("Songs"));
    REQUIRE(songs != nullptr);
    click(window, centre_of(songs));
    CHECK(window->property("ticked").toStringList() == QStringList{QStringLiteral("songs")});
    CHECK(field->property("menuOpen").toBool());

    QQuickItem* bible = visible_text_under(overlay, QStringLiteral("Scripture"));
    REQUIRE(bible != nullptr);
    click(window, centre_of(bible));
    CHECK(window->property("ticked").toStringList()
          == (QStringList{QStringLiteral("songs"), QStringLiteral("bible")}));
    CHECK(field->property("menuOpen").toBool());

    // A click elsewhere closes it.
    click(window, QPoint(390, 290));
    CHECK_FALSE(field->property("menuOpen").toBool());
}

TEST_CASE("An icon picker hands back the glyph chosen and closes", "[ui][unified]")
{
    QmlScene scene;
    QQuickWindow* window = show(scene, R"QML(
import QtQuick
import SeventhSlide as Ui
Window {
    id: win
    width: 500; height: 400
    property alias picker: picker
    property string chosen: ""
    Ui.IconPickerButton {
        id: picker
        x: 400; y: 20
        icons: ["display", "person", "video"]
        current: "display"
        onPicked: function (id) { win.chosen = id }
    }
}
)QML");
    auto* picker = window->property("picker").value<QQuickItem*>();
    REQUIRE(picker != nullptr);
    click(window, centre_of(picker));
    REQUIRE(picker->property("menuOpen").toBool());

    // The grid's second glyph is "person".
    QList<QQuickItem*> choices;
    for (QQuickItem* b : seventhslide::test::visible_items_of(window->contentItem(),
                                                             "QQuickRectangle")) {
        if (b->property("modelData").toString() == QStringLiteral("person")) {
            choices.push_back(b);
        }
    }
    REQUIRE(choices.size() == 1);
    // The grid lines up with the button's right edge.
    const QPointF buttonRight = picker->mapToScene(QPointF(picker->width(), 0));
    CHECK(choices.front()->mapToScene(QPointF(0, 0)).x() < buttonRight.x());

    click(window, centre_of(choices.front()));
    CHECK(window->property("chosen").toString() == QStringLiteral("person"));
    CHECK_FALSE(picker->property("menuOpen").toBool());
}

TEST_CASE("A menu is as wide as its widest item", "[ui][unified]")
{
    QmlScene scene;
    QQuickWindow* window = show(scene, R"QML(
import QtQuick
import SeventhSlide as Ui
Window {
    width: 700; height: 300
    property alias menu: menu
    Ui.AppButton { id: opener; x: 20; y: 20; label: "Add" }
    Ui.AppMenu {
        id: menu
        anchorItem: opener
        Ui.MenuItemRow { icon: "display"; label: "Display" }
        Ui.MenuItemRow {
            objectName: "long"
            icon: "video"
            label: "Virtual Camera"
            detail: "Needs v4l2loopback. Load it, then reopen this window."
            enabled: false
        }
    }
}
)QML");
    QObject* menu = window->property("menu").value<QObject*>();
    REQUIRE(menu != nullptr);
    REQUIRE(QMetaObject::invokeMethod(menu, "openAt"));
    REQUIRE(QTest::qWaitFor([&] { return menu->property("opened").toBool(); }, 2000));

    auto* longest = window->findChild<QQuickItem*>(QStringLiteral("long"));
    REQUIRE(longest != nullptr);
    CHECK(menu->property("width").toReal() > 196);
    // The item gets all the width it asks for, so its reason is not cut off.
    CHECK(longest->width() >= longest->implicitWidth());
}

TEST_CASE("A menu too tall for either side scrolls beside its field, not over it",
          "[ui][unified]")
{
    QmlScene scene;
    QQuickWindow* window = show(scene, R"QML(
import QtQuick
import SeventhSlide as Ui
Window {
    width: 700; height: 400
    property alias menu: menu
    property alias opener: opener
    Ui.AppButton { id: opener; x: 20; y: 260; label: "Font" }
    Ui.AppMenu {
        id: menu
        anchorItem: opener
        Repeater {
            model: 40
            Ui.MenuItemRow { required property int index; label: "Font " + index }
        }
    }
}
)QML");
    QObject* menu = window->property("menu").value<QObject*>();
    auto* opener = window->property("opener").value<QQuickItem*>();
    REQUIRE(menu != nullptr);
    REQUIRE(opener != nullptr);
    REQUIRE(QMetaObject::invokeMethod(menu, "openAt"));
    REQUIRE(QTest::qWaitFor([&] { return menu->property("opened").toBool(); }, 2000));
    QTest::qWait(0);
    // More room above the field than below: it opens there, and stops short
    // of the field.
    const double top = menu->property("y").toReal();
    const double bottom = top + menu->property("height").toReal();
    CHECK(top >= 4);
    CHECK(bottom <= opener->y());
    CHECK(bottom > opener->y() - 12);
}

TEST_CASE("A menu that fits neither side covers its anchor unless kept clear of it",
          "[ui][unified]")
{
    QmlScene scene;
    QQuickWindow* window = show(scene, R"QML(
import QtQuick
import SeventhSlide as Ui
Window {
    width: 700; height: 400
    property alias menu: menu
    property alias opener: opener
    Ui.AppButton { id: opener; x: 20; y: 170; label: "Filter" }
    Ui.AppMenu {
        id: menu
        anchorItem: opener
        Repeater {
            model: 9
            Ui.MenuItemRow { required property int index; label: "Tag " + index }
        }
    }
}
)QML");
    QObject* menu = window->property("menu").value<QObject*>();
    auto* opener = window->property("opener").value<QQuickItem*>();
    REQUIRE(menu != nullptr);
    REQUIRE(opener != nullptr);
    const auto open = [&] {
        REQUIRE(QMetaObject::invokeMethod(menu, "openAt"));
        REQUIRE(QTest::qWaitFor([&] { return menu->property("opened").toBool(); }, 2000));
        QTest::qWait(0);
    };
    const auto top = [&] { return menu->property("y").toReal(); };
    const auto bottom = [&] { return top() + menu->property("height").toReal(); };
    const double anchorTop = opener->y();
    const double anchorBottom = opener->y() + opener->height();

    // Taller than the room on either side, though it fits the window: all of
    // it in sight, over its anchor.
    open();
    CHECK(top() >= 4);
    CHECK(bottom() > anchorTop);
    CHECK(top() < anchorBottom);
    REQUIRE(QMetaObject::invokeMethod(menu, "close"));
    REQUIRE(QTest::qWaitFor([&] { return !menu->property("visible").toBool(); }, 2000));

    // Kept clear: on the roomier side (below), scrolling there.
    menu->setProperty("keepAnchorClear", true);
    open();
    CHECK(top() >= anchorBottom);
    CHECK(bottom() <= window->height() - 4);
}

TEST_CASE("A menu gives the keyboard back to what had it as it closes", "[ui][unified]")
{
    QmlScene scene;
    QQuickWindow* window = show(scene, R"QML(
import QtQuick
import SeventhSlide as Ui
Window {
    id: win
    width: 500; height: 400
    property alias menu: menu
    property alias owner: owner
    property alias other: other
    property int escapes: 0
    // In a focus scope, as a dialog's page is: Qt's own handing back focuses
    // the item within its scope, and the keyboard stays with the window.
    FocusScope {
        width: 100; height: 30
        Item {
            id: owner
            anchors.fill: parent
            Keys.onEscapePressed: win.escapes++
        }
    }
    Ui.AppButton { id: opener; x: 20; y: 40; label: "Sort" }
    TextInput { id: other; y: 200; width: 100; height: 20 }
    Ui.AppMenu {
        id: menu
        anchorItem: opener
        Ui.MenuItemRow { label: "Name" }
    }
}
)QML");
    QObject* menu = window->property("menu").value<QObject*>();
    auto* owner = window->property("owner").value<QQuickItem*>();
    auto* other = window->property("other").value<QQuickItem*>();
    REQUIRE(menu != nullptr);
    REQUIRE(owner != nullptr);
    REQUIRE(other != nullptr);
    const auto open = [&] {
        REQUIRE(QMetaObject::invokeMethod(menu, "openAt"));
        REQUIRE(QTest::qWaitFor([&] { return menu->property("opened").toBool(); }, 2000));
    };
    const auto closed = [&] {
        REQUIRE(QTest::qWaitFor([&] { return !menu->property("visible").toBool(); }, 2000));
    };

    // Escape closes the menu, and the next one reaches what had the keyboard.
    owner->forceActiveFocus();
    REQUIRE(owner->hasActiveFocus());
    open();
    CHECK_FALSE(owner->hasActiveFocus());
    QTest::keyClick(window, Qt::Key_Escape);
    closed();
    CHECK(owner->hasActiveFocus());
    QTest::keyClick(window, Qt::Key_Escape);
    CHECK(window->property("escapes").toInt() == 1);

    // A click elsewhere closes it and lands: what was clicked has the keyboard.
    owner->forceActiveFocus();
    open();
    click(window, centre_of(other));
    closed();
    CHECK(other->hasActiveFocus());

    // Focus that moved on while it was open (a row put it in a field) stays.
    owner->forceActiveFocus();
    open();
    other->forceActiveFocus();
    REQUIRE(QMetaObject::invokeMethod(menu, "close"));
    closed();
    CHECK(other->hasActiveFocus());
}

TEST_CASE("A window shell's rail asks for a page and its footer for Save", "[ui][unified]")
{
    QmlScene scene;
    QQuickWindow* window = show(scene, R"QML(
import QtQuick
import SeventhSlide as Ui
Window {
    id: win
    width: 760; height: 600
    property alias shell: shell
    property string page: "general"
    property int saves: 0
    property int cancels: 0
    Ui.WindowShell {
        id: shell
        anchors.fill: parent
        title: "Settings"
        pages: [{ id: "general", label: "General", icon: "settings" },
                { id: "stacks", label: "Stacks", icon: "layers" }]
        page: win.page
        onPageActivated: function (id) { win.page = id }
        onAccepted: win.saves++
        onCanceled: win.cancels++
        Text { objectName: "pageText"; text: "Page " + win.page }
    }
}
)QML");
    QQuickItem* root = window->contentItem();
    QQuickItem* stacks = visible_text_under(root, QStringLiteral("Stacks"));
    REQUIRE(stacks != nullptr);
    click(window, centre_of(stacks));
    CHECK(window->property("page").toString() == QStringLiteral("stacks"));

    // The page sits inside the padding, right of the rail and under the bar.
    QQuickItem* text = item(window, "pageText");
    const QPointF at = text->mapToScene(QPointF(0, 0));
    CHECK(at.x() >= 184 + 28);
    CHECK(at.y() >= 44 + 18);

    QQuickItem* save = visible_text_under(root, QStringLiteral("Save"));
    REQUIRE(save != nullptr);
    click(window, centre_of(save));
    CHECK(window->property("saves").toInt() == 1);

    // A page whose changes apply at once shows Close alone, which cancels.
    window->property("shell").value<QObject*>()->setProperty("closeOnly", true);
    CHECK(visible_text_under(root, QStringLiteral("Save")) == nullptr);
    QQuickItem* close = visible_text_under(root, QStringLiteral("Close"));
    REQUIRE(close != nullptr);
    click(window, centre_of(close));
    CHECK(window->property("cancels").toInt() == 1);
}

TEST_CASE("Every output icon the core stores has a glyph", "[ui][unified]")
{
    for (const auto icon : seventhslide::model::k_output_icons) {
        INFO(icon);
        CHECK(seventhslide::app::glyphs::find(icon) != nullptr);
    }
}

TEST_CASE("A search field shows the owner's value after typing, when the owner clears it",
          "[ui][unified]")
{
    QmlScene scene;
    QQuickWindow* window = show(scene, R"QML(
import QtQuick
import SeventhSlide as Ui
Window {
    id: win
    width: 400; height: 200
    property string query: ""
    property alias filterField: filterField
    property alias plainField: plainField
    Ui.SearchFilterField {
        id: filterField
        y: 10
        text: win.query
        onEdited: function (v) { win.query = v }
    }
    Ui.SearchField {
        id: plainField
        y: 60
        width: 200
        text: win.query
        onTextChanged: win.query = text
    }
}
)QML");
    auto* filterField = window->property("filterField").value<QQuickItem*>();
    REQUIRE(filterField != nullptr);
    click(window, centre_of(filterField) - QPoint(60, 0));
    for (const char c : std::string("grace"))
        QTest::keyClick(window, c);
    CHECK(window->property("query").toString() == QStringLiteral("grace"));

    // The owner clears its search (a reset): both fields show it.
    window->setProperty("query", QString());
    QTest::qWait(10);
    const auto shown = [](QQuickItem* root) {
        for (QObject* o : root->findChildren<QObject*>()) {
            if (o->inherits("QQuickTextInput"))
                return o->property("text").toString();
        }
        return QStringLiteral("<none>");
    };
    CHECK(shown(filterField).isEmpty());
    CHECK(shown(window->property("plainField").value<QQuickItem*>()).isEmpty());
}
