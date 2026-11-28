// The output editor's Destinations tab: the pickers that open a target menu,
// and what each destination setter does with a refusal. Destinations
// of every kind are put straight into OutputDestinationsState, since only a
// display can be added on a machine without NDI, Desktop Video, or a virtual
// camera; the state is told it is not shown so the session does not reload
// them underneath the test.

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include "destinations_controller.hpp"
#include "display_list_model.hpp"
#include "operator_ui.hpp"
#include "facade_all.hpp"
#include "output_host_test_helpers.hpp"
#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"
#include "ui_test_helpers.hpp"

#include <seventhslide/render/ndi_runtime.hpp>
#include <seventhslide/render/output_window_host.hpp>
#include <seventhslide/stream/stream_settings.hpp>

#include <QGuiApplication>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRectF>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <sstream>
#include <string>

using seventhslide::app::SessionFacade;
using seventhslide::test::OperatorUi;
using seventhslide::test::centre_of;
using seventhslide::test::click;
using seventhslide::test::is_qml_type;
using seventhslide::test::qml_object;
using seventhslide::test::visible_qml_items_of;
using seventhslide::test::visible_text_under;

namespace {

/// Scroll the Flickable ``item`` is in, if any, so ``item`` shows: the
/// settings beside the list scroll when they are taller than the panel.
QQuickItem* into_view(QQuickItem* item)
{
    QQuickItem* flick = item->parentItem();
    while (flick != nullptr && !flick->inherits("QQuickFlickable")) {
        flick = flick->parentItem();
    }
    if (flick == nullptr) {
        return item;
    }
    auto* content = flick->property("contentItem").value<QQuickItem*>();
    REQUIRE(content != nullptr);
    const QRectF at = item->mapRectToItem(content, QRectF(0, 0, item->width(), item->height()));
    const qreal top = flick->property("contentY").toReal();
    if (at.top() < top || at.bottom() > top + flick->height()) {
        const qreal most = std::max(0.0, flick->property("contentHeight").toReal() - flick->height());
        REQUIRE(flick->setProperty("contentY", std::clamp(at.top() - 12, 0.0, most)));
        QTest::qWait(20);
    }
    return item;
}

/// The topmost visible item under ``at`` in ``window``'s scene: what a click
/// there reaches first.
QQuickItem* item_at(QQuickItem* item, QPointF at)
{
    const auto children = item->childItems();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        QQuickItem* child = *it;
        if (!child->isVisible() || child->opacity() <= 0.0) {
            continue;
        }
        if (QQuickItem* hit = item_at(child, at)) {
            return hit;
        }
    }
    return item->contains(item->mapFromScene(at)) ? item : nullptr;
}

/// Where a click on ``picker`` went, for a failure to say: the picker's
/// place, the window's, what was under the click, and whether the picker
/// thought its menu open already.
std::string click_on(QQuickItem* picker)
{
    QQuickWindow* window = picker->window();
    const QRectF at = picker->mapRectToScene(picker->boundingRect());
    QQuickItem* hit = window != nullptr ? item_at(window->contentItem(), at.center()) : nullptr;
    std::ostringstream out;
    out << "picker at " << at.x() << "," << at.y() << " " << at.width() << "x" << at.height();
    if (window != nullptr) {
        out << " in a " << window->width() << "x" << window->height() << " window"
            << (window->isExposed() ? "" : ", not exposed")
            << (QGuiApplication::focusWindow() == window ? "" : ", not the focus window");
    }
    out << "; under the click: " << (hit != nullptr ? hit->metaObject()->className() : "nothing");
    if (hit != nullptr && !hit->objectName().isEmpty()) {
        out << " '" << hit->objectName().toStdString() << "'";
    }
    out << "; picker open: " << picker->property("open").toBool();
    return out.str();
}

/// ``object.method(args...)``, as QML calls it.
template <class... Args>
QVariant call(QObject* object, const char* method, const Args&... args)
{
    QVariant out;
    REQUIRE(QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QVariant, out),
                                      Q_ARG(QVariant, QVariant(args))...));
    return out;
}

/// The output editor on its Destinations tab, with the destinations state
/// cut loose from the session so a test can give it its own list.
struct DestinationsTab {
    QObject* dialog = nullptr;
    QObject* dests = nullptr;
    QQuickItem* panel = nullptr;

    explicit DestinationsTab(OperatorUi& ui)
    {
        dialog = ui.find_component("OutputEditDialog");
        REQUIRE(dialog != nullptr);
        call(dialog, "openFor", QStringLiteral("Main"), QStringLiteral("destinations"));
        QObject* state = qml_object(dialog, "OutputEditState");
        REQUIRE(state != nullptr);
        dests = state->property("destState").value<QObject*>();
        REQUIRE(dests != nullptr);
        panel = qobject_cast<QQuickItem*>(qml_object(dialog, "OutputDestinationsPanel"));
        REQUIRE(panel != nullptr);
        REQUIRE(dests->setProperty("shown", false));
        QTest::qWait(20);
    }

    void show(const QVariantList& list, const QString& selected)
    {
        close_menus();
        REQUIRE(dests->setProperty("destinations", list));
        REQUIRE(dests->setProperty("selectedDestId", selected));
        QTest::qWait(20);
    }

    /// Every target picker on the page, shown or not.
    [[nodiscard]] QList<QQuickItem*> pickers() const
    {
        QList<QQuickItem*> out;
        for (auto* item : panel->findChildren<QQuickItem*>()) {
            if (is_qml_type(item, "DestinationPicker")) {
                out.push_back(item);
            }
        }
        return out;
    }
    /// The picker showing that opens a ``kind`` menu, scrolled into view.
    [[nodiscard]] QQuickItem* picker(const char* kind) const
    {
        for (auto* item : pickers()) {
            if (item->isVisible() && item->property("menuKind").toString() == QLatin1String(kind)) {
                return into_view(item);
            }
        }
        INFO("picker " << kind);
        FAIL("no such picker showing");
        return nullptr;
    }
    /// The menu ``picker`` owns.
    [[nodiscard]] static QObject* menu_of(QQuickItem* picker)
    {
        QObject* menu = qml_object(picker, "OutputDestinationMenu");
        REQUIRE(menu != nullptr);
        return menu;
    }
    /// A picker's menu is showing.
    [[nodiscard]] bool menu_open() const
    {
        const auto all = pickers();
        return std::any_of(all.begin(), all.end(), [](QQuickItem* p) {
            return menu_of(p)->property("visible").toBool();
        });
    }
    void close_menus() const
    {
        for (auto* p : pickers()) {
            REQUIRE(QMetaObject::invokeMethod(menu_of(p), "close"));
        }
        QTest::qWait(20);
    }
    /// The state's property ``what``, as text.
    [[nodiscard]] QString text(const char* what) const
    {
        return dests->property(what).toString();
    }
};

QVariantMap fake(const char* type)
{
    return {{QStringLiteral("id"), QStringLiteral("d1")},
            {QStringLiteral("type"), QString::fromLatin1(type)},
            {QStringLiteral("name"), QStringLiteral("Booth")},
            {QStringLiteral("enabled"), true},
            {QStringLiteral("screenKey"), QStringLiteral("scr")},
            {QStringLiteral("screenLabel"), QStringLiteral("Screen One")},
            {QStringLiteral("targetKey"), QStringLiteral("dev")},
            {QStringLiteral("modeKey"), QStringLiteral("mode")},
            {QStringLiteral("modeName"), QStringLiteral("1080p59.94")}};
}

}  // namespace

TEST_CASE("Each destination kind shows its own pickers and opens its own menu",
          "[ui][output-edit][destinations]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.init_libraries(dir.path / "data"));
    OperatorUi ui(facade);
    DestinationsTab tab(ui);
    auto* w = ui.window();

    // The picker a value is shown in.
    const auto picker_of = [](QQuickItem* item) -> QQuickItem* {
        while (item != nullptr && !is_qml_type(item, "DestinationPicker")) {
            item = item->parentItem();
        }
        return item;
    };
    // Clicking a picker's value opens that picker's menu, for its destination
    // and checking its current target; clicking again closes it. The open
    // picker says so.
    const auto open_and_close = [&](const QString& value, const char* kind, const char* key) {
        QQuickItem* field = visible_text_under(tab.panel, value);
        INFO("picker showing " << value.toStdString());
        REQUIRE(field != nullptr);
        QQuickItem* picker = picker_of(field);
        REQUIRE(picker != nullptr);
        CHECK_FALSE(picker->property("open").toBool());
        click(w, centre_of(field));
        QObject* menu = DestinationsTab::menu_of(picker);
        REQUIRE(menu->property("visible").toBool());
        CHECK(menu->property("kind").toString() == QLatin1String(kind));
        CHECK(menu->property("currentKey").toString() == QLatin1String(key));
        CHECK(menu->property("destId").toString() == QStringLiteral("d1"));
        CHECK(picker->property("open").toBool());
        click(w, centre_of(field));
        CHECK_FALSE(tab.menu_open());
        CHECK_FALSE(picker->property("open").toBool());
    };
    const auto shown = [&](const char* text) {
        return visible_text_under(tab.panel, QString::fromLatin1(text)) != nullptr;
    };

    SECTION("display")
    {
        tab.show({fake("display")}, QStringLiteral("d1"));
        CHECK(shown("Monitor"));
        CHECK_FALSE(shown("Video mode"));
        open_and_close(QStringLiteral("Screen One"), "display", "scr");
    }

    SECTION("virtual camera")
    {
        tab.show({fake("vcam")}, QStringLiteral("d1"));
        CHECK(shown("Device"));
        CHECK_FALSE(shown("Monitor"));
        open_and_close(QStringLiteral("Screen One"), "vcam", "scr");
    }

    SECTION("Blackmagic: a device and a video mode")
    {
        tab.show({fake("bmd")}, QStringLiteral("d1"));
        CHECK(shown("Device"));
        CHECK(shown("Video mode"));
        // No card here, so the device reads as not connected.
        open_and_close(QStringLiteral("Not connected"), "bmd", "dev");
        open_and_close(QStringLiteral("1080p59.94"), "bmdmode", "mode");
    }

    SECTION("NDI has no picker")
    {
        tab.show({fake("ndi")}, QStringLiteral("d1"));
        CHECK_FALSE(shown("Monitor"));
        CHECK_FALSE(shown("Device"));
        CHECK_FALSE(shown("Video mode"));
        CHECK(shown("Source name"));
    }

    SECTION("no row open: only the row shows")
    {
        tab.show({fake("display")}, QString());
        CHECK(shown("Booth"));
        CHECK_FALSE(shown("Monitor"));
    }

    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Each destination setter reports a refusal and reloads or says it was not taken",
          "[ui][output-edit][destinations]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.init_libraries(dir.path / "data"));
    // Adding a destination needs a host to put it on.
    seventhslide::render::OutputWindowHost host;
    facade.set_output_host(&host);
    OperatorUi ui(facade);
    DestinationsTab tab(ui);
    QSignalSpy reported(tab.dests, SIGNAL(reported(QString)));

    // Every call names a destination the session does not have, so each is
    // refused. A reload swaps the fake list for the session's own.
    struct Row {
        const char* method;
        QVariant second;
        bool reloads;
        bool picks;
    };
    const Row rows[] = {
        {"setDestinationName", QStringLiteral("X"), false, false},
        {"setDestinationScreen", QStringLiteral("k"), false, true},
        {"setDestinationEnabled", true, true, false},
        {"setDestinationNdiName", QStringLiteral("n"), true, false},
        {"setDestinationAlpha", true, true, false},
        {"setDestinationMatchesOutput", false, true, false},
        // A pick from a menu says it was not taken, so the menu stays open to
        // pick another.
        {"setDestinationBmdDevice", QStringLiteral("k"), false, true},
        {"setDestinationBmdMode", QStringLiteral("k"), false, true},
        {"setDestinationCamera", QStringLiteral("k"), false, true},
        {"setDestinationAudioDevice", QStringLiteral("k"), false, true},
    };
    const QVariantList fake_list{fake("display")};
    for (const auto& row : rows) {
        INFO(row.method);
        tab.show(fake_list, QStringLiteral("d1"));
        reported.clear();
        const QVariant taken = call(tab.dests, row.method, QStringLiteral("nope"), row.second);
        REQUIRE(reported.count() == 1);
        CHECK_FALSE(reported.front().front().toString().isEmpty());
        const bool reloaded = tab.dests->property("destinations").toList() != fake_list;
        CHECK(reloaded == row.reloads);
        if (row.picks) {
            CHECK(taken.typeId() == QMetaType::Bool);
            CHECK_FALSE(taken.toBool());
        }
    }

    SECTION("a refused remove changes nothing")
    {
        tab.show(fake_list, QStringLiteral("d1"));
        reported.clear();
        call(tab.dests, "removeDestination", QStringLiteral("nope"));
        REQUIRE(reported.count() == 1);
        CHECK_FALSE(reported.front().front().toString().isEmpty());
        CHECK(tab.dests->property("destinations").toList() == fake_list);
        CHECK(tab.text("selectedDestId") == QStringLiteral("d1"));
    }

    SECTION("accepted changes clear the error")
    {
        REQUIRE_FALSE(tab.dests->property("outputId").toString().isEmpty());
        reported.clear();
        call(tab.dests, "addDestination", QStringLiteral("display"));
        const QString id = tab.text("selectedDestId");
        INFO("add reported: " << (reported.isEmpty() ? std::string("nothing")
                                  : reported.front().front().toString().toStdString()));
        REQUIRE_FALSE(id.isEmpty());

        QTest::qWait(20);
        INFO(click_on(tab.picker("display")));
        click(ui.window(), centre_of(tab.picker("display")));
        REQUIRE(tab.menu_open());
        reported.clear();
        call(tab.dests, "setDestinationName", id, QStringLiteral("Stage"));
        REQUIRE(reported.count() == 1);
        CHECK(reported.front().front().toString().isEmpty());
        CHECK(tab.menu_open());  // a rename leaves the menu alone

        // Removing the destination the menu is open for closes it and drops
        // the selection.
        reported.clear();
        call(tab.dests, "removeDestination", id);
        REQUIRE(reported.count() == 1);
        CHECK(reported.front().front().toString().isEmpty());
        CHECK_FALSE(tab.menu_open());
        CHECK(tab.text("selectedDestId") != id);
    }

    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("A destination's menu lists its kind's targets and picks one",
          "[ui][output-edit][destinations]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.init_libraries(dir.path / "data"));
    seventhslide::render::OutputWindowHost host;
    facade.set_output_host(&host);
    OperatorUi ui(facade);
    DestinationsTab tab(ui);
    QSignalSpy reported(tab.dests, SIGNAL(reported(QString)));

    call(tab.dests, "addDestination", QStringLiteral("display"));
    const QString id = tab.text("selectedDestId");
    REQUIRE_FALSE(id.isEmpty());
    // The menu a click on the ``kind`` picker opens; its rows' area.
    QQuickItem* menu = nullptr;
    const auto open = [&](const char* kind) {
        QQuickItem* picker = tab.picker(kind);
        INFO(click_on(picker));
        click(ui.window(), centre_of(picker));
        REQUIRE(tab.menu_open());
        menu = DestinationsTab::menu_of(picker)->property("contentItem").value<QQuickItem*>();
        REQUIRE(menu != nullptr);
        QTest::qWait(20);
    };
    // The menu item titled ``title``.
    const auto row = [&](const QString& title) {
        QQuickItem* item = visible_text_under(menu, title);
        INFO("menu row " << title.toStdString());
        REQUIRE(item != nullptr);
        while (item != nullptr && !is_qml_type(item, "MenuItemRow")) {
            item = item->parentItem();
        }
        REQUIRE(item != nullptr);
        return item;
    };

    SECTION("displays: the current one is checked, and a pick closes the menu")
    {
        const auto* displays = facade.destinations()->displays();
        REQUIRE(displays->rowCount() >= 1);
        const auto at = [displays](const char* role) {
            const auto roles = displays->roleNames();
            return displays->data(displays->index(0), roles.key(role)).toString();
        };
        const QString key = at("key");
        const QString title = at("label");  // falls back to the name, then the key
        REQUIRE_FALSE(title.isEmpty());
        const QString assigned = at("assignedOutput");
        const QString shown = (assigned.isEmpty() || assigned == QStringLiteral("Main"))
                                  ? title
                                  : QStringLiteral("%1 (used by %2)").arg(title, assigned);

        // The session's destination, shown on no screen and then on this one.
        QVariantMap dest = fake("display");
        dest.insert(QStringLiteral("id"), id);
        dest.insert(QStringLiteral("screenKey"), QString());
        tab.show({dest}, id);
        open("display");
        CHECK_FALSE(row(shown)->property("checked").toBool());
        dest.insert(QStringLiteral("screenKey"), key);
        tab.show({dest}, id);
        open("display");
        CHECK(row(shown)->property("checked").toBool());

        // The menu's face is behind every row, not a sliver above them: it
        // was once sized from a ``count`` the display model does not have.
        const auto* face = DestinationsTab::menu_of(tab.picker("display"))
                               ->property("background").value<QQuickItem*>();
        REQUIRE(face != nullptr);
        const QRectF rowArea = row(shown)->mapRectToItem(face, row(shown)->boundingRect());
        CHECK(face->height() >= rowArea.bottom());
        CHECK(rowArea.top() >= 0);

        reported.clear();
        click(ui.window(), centre_of(row(shown)));
        CHECK_FALSE(tab.menu_open());
        REQUIRE(reported.count() == 1);
        CHECK(reported.front().front().toString().isEmpty());
    }

    SECTION("cameras: named with the output that uses one; a refused pick keeps the menu")
    {
        tab.show({fake("vcam")}, QStringLiteral("d1"));
        open("vcam");
        // The machine may have a camera of its own (OBS's virtual camera, say).
        REQUIRE(tab.dests->setProperty("cameras", QVariantList{}));
        QTest::qWait(20);
        CHECK(visible_text_under(menu, QStringLiteral("No device found")) != nullptr);
        REQUIRE(tab.dests->setProperty(
            "cameras", QVariantList{QVariantMap{{QStringLiteral("key"), QStringLiteral("/dev/video9")},
                                                {QStringLiteral("label"), QStringLiteral("Loop")},
                                                {QStringLiteral("assignedOutput"), QStringLiteral("Stage")}}}));
        QTest::qWait(20);
        reported.clear();
        click(ui.window(), centre_of(row(QStringLiteral("Loop (used by Stage)"))));
        REQUIRE(reported.count() == 1);
        CHECK_FALSE(reported.front().front().toString().isEmpty());  // not the session's
        CHECK(tab.menu_open());
    }

    SECTION("Blackmagic devices and modes")
    {
        tab.show({fake("bmd")}, QStringLiteral("d1"));
        open("bmd");
        CHECK(visible_text_under(menu, QStringLiteral("No Blackmagic device found")) != nullptr);
        REQUIRE(tab.dests->setProperty(
            "bmdDevices",
            QVariantList{QVariantMap{{QStringLiteral("key"), QStringLiteral("k1")},
                                     {QStringLiteral("label"), QStringLiteral("DeckLink Mini")},
                                     {QStringLiteral("assignedOutput"), QStringLiteral("Stage")}}}));
        QTest::qWait(20);
        CHECK(row(QStringLiteral("DeckLink Mini — Stage")) != nullptr);
        tab.close_menus();

        open("bmdmode");
        CHECK(visible_text_under(menu, QStringLiteral("Choose a device first")) != nullptr);
        REQUIRE(tab.dests->setProperty(
            "bmdModes", QVariantList{QVariantMap{{QStringLiteral("key"), QStringLiteral("m1")},
                                                 {QStringLiteral("name"), QStringLiteral("1080p25")}}}));
        QTest::qWait(20);
        reported.clear();
        click(ui.window(), centre_of(row(QStringLiteral("1080p25"))));
        REQUIRE(reported.count() == 1);
        CHECK_FALSE(reported.front().front().toString().isEmpty());  // not the session's
        CHECK(tab.menu_open());
    }

    // A pick taken reloads the lists, so the row it was made from is gone
    // before the pick returns; the menu still closes. (A virtual camera's
    // pick cannot be taken here: the host opens the device to see that it
    // is one.)
    SECTION("a Blackmagic device taken closes the menu, though its row is gone")
    {
        // The host's own card destination, off so nothing opens.
        const QString out = tab.dests->property("outputId").toString();
        REQUIRE_FALSE(out.isEmpty());
        host.replace_destinations(nlohmann::json::array({nlohmann::json{{"id", "b1"},
                                                                        {"output", out.toStdString()},
                                                                        {"type", "bmd"},
                                                                        {"name", "Card"},
                                                                        {"enabled", false}}}));
        call(tab.dests, "loadDestinations");
        REQUIRE(tab.dests->setProperty("selectedDestId", QStringLiteral("b1")));
        QTest::qWait(20);
        open("bmd");
        // No card here, so one is listed as if found.
        REQUIRE(tab.dests->setProperty(
            "bmdDevices", QVariantList{QVariantMap{{QStringLiteral("key"), QStringLiteral("k1")},
                                                   {QStringLiteral("label"), QStringLiteral("DeckLink Mini")}}}));
        QTest::qWait(20);
        const QPointer<QQuickItem> picked = row(QStringLiteral("DeckLink Mini"));
        reported.clear();
        click(ui.window(), centre_of(picked));
        REQUIRE(reported.count() == 1);
        CHECK(reported.front().front().toString().isEmpty());
        CHECK(host.registry().find(QStringLiteral("b1"))->target_key == QStringLiteral("k1"));
        CHECK(picked.isNull());
        CHECK_FALSE(tab.menu_open());
        CHECK_FALSE(tab.picker("bmd")->property("open").toBool());
    }

    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// A destination's menu closes however the operator moves on, and its field
// says so: a click on it afterwards opens it again at once.
TEST_CASE("A destination's menu closes with its page, Escape, another destination or a press elsewhere",
          "[ui][output-edit][destinations]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.init_libraries(dir.path / "data"));
    OperatorUi ui(facade);
    DestinationsTab tab(ui);
    auto* w = ui.window();
    QObject* state = qml_object(tab.dialog, "OutputEditState");
    REQUIRE(state != nullptr);

    QVariantMap second = fake("display");
    second.insert(QStringLiteral("id"), QStringLiteral("d2"));
    second.insert(QStringLiteral("name"), QStringLiteral("Stage"));
    tab.show({fake("display"), second}, QStringLiteral("d1"));

    // The Monitor field, and its menu, opened with a click.
    QQuickItem* picker = nullptr;
    QObject* menu = nullptr;
    const auto open = [&] {
        picker = tab.picker("display");
        menu = DestinationsTab::menu_of(picker);
        INFO(click_on(picker));
        click(w, centre_of(picker));
        REQUIRE(menu->property("visible").toBool());
        REQUIRE(picker->property("open").toBool());
    };
    const auto closed = [&] {
        QTest::qWait(20);
        CHECK_FALSE(menu->property("visible").toBool());
        CHECK_FALSE(picker->property("open").toBool());
    };

    SECTION("another page")
    {
        open();
        call(state, "switchTab", QStringLiteral("general"));
        closed();
    }

    SECTION("Escape, which leaves the editor open")
    {
        open();
        QTest::keyClick(w, Qt::Key_Escape);
        closed();
        CHECK(tab.dialog->property("open").toBool());
    }

    SECTION("another destination shown")
    {
        open();
        REQUIRE(tab.dests->setProperty("selectedDestId", QStringLiteral("d2")));
        closed();
    }

    SECTION("a press elsewhere, after which one click opens it again")
    {
        open();
        QQuickItem* label = visible_text_under(tab.panel, QStringLiteral("Monitor"));
        REQUIRE(label != nullptr);
        click(w, centre_of(label));
        closed();
        click(w, centre_of(picker));
        CHECK(menu->property("visible").toBool());
        CHECK(picker->property("open").toBool());
    }

    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// The tab is a split: the destinations on the left, the settings of the one
// selected on the right. One is always selected while there are any; a row's
// switch turns its destination on or off without moving the selection.
TEST_CASE("The destinations list selects one to show and switches each on or off",
          "[ui][output-edit][destinations]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.init_libraries(dir.path / "data"));
    seventhslide::render::OutputWindowHost host;
    facade.set_output_host(&host);
    OperatorUi ui(facade);
    DestinationsTab tab(ui);
    REQUIRE(tab.dests->setProperty("shown", true));

    // Two destinations, named so their rows can be told apart.
    QStringList ids;
    for (const char* name : {"Booth", "Stage"}) {
        call(tab.dests, "addDestination", QStringLiteral("display"));
        ids << tab.text("selectedDestId");
        call(tab.dests, "setDestinationName", ids.back(), QString::fromLatin1(name));
    }
    REQUIRE(ids.size() == 2);
    REQUIRE_FALSE(ids.contains(QString()));
    QTest::qWait(50);

    const auto row_of = [&](const QString& title) -> QQuickItem* {
        QStringList titles;
        for (auto* item : visible_qml_items_of(tab.panel, "PickRow")) {
            if (item->property("title").toString() == title) {
                return item;
            }
            titles << item->property("title").toString();
        }
        FAIL("no row " << title.toStdString() << " among " << titles.join(", ").toStdString());
        return nullptr;
    };
    const auto enabled = [&](const QString& id) {
        for (const auto& d : tab.dests->property("destinations").toList()) {
            if (d.toMap().value(QStringLiteral("id")).toString() == id) {
                return d.toMap().value(QStringLiteral("enabled")).toBool();
            }
        }
        FAIL("no destination " << id.toStdString());
        return false;
    };

    // A click on a row shows its settings.
    click(ui.window(), centre_of(row_of(QStringLiteral("Booth"))));
    CHECK(tab.text("selectedDestId") == ids[0]);
    click(ui.window(), centre_of(row_of(QStringLiteral("Stage"))));
    CHECK(tab.text("selectedDestId") == ids[1]);

    // The Booth row's switch: Booth changes, the selection stays on Stage.
    const bool was = enabled(ids[0]);
    QQuickItem* booth = row_of(QStringLiteral("Booth"));
    const auto toggles = visible_qml_items_of(booth, "ToggleSwitch");
    REQUIRE(toggles.size() == 1);
    QQuickItem* toggle = toggles.front();
    click(ui.window(), centre_of(toggle));
    CHECK(enabled(ids[0]) != was);
    CHECK(tab.text("selectedDestId") == ids[1]);

    // Removing the one selected shows the first that is left.
    call(tab.dests, "removeDestination", ids[1]);
    CHECK(tab.text("selectedDestId") == ids[0]);
    call(tab.dests, "removeDestination", ids[0]);
    CHECK(tab.text("selectedDestId").isEmpty());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("Up and Down over the destinations reorder the one selected",
          "[ui][output-edit][destinations]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.init_libraries(dir.path / "data"));
    seventhslide::render::OutputWindowHost host;
    facade.set_output_host(&host);
    OperatorUi ui(facade);
    DestinationsTab tab(ui);
    REQUIRE(tab.dests->setProperty("shown", true));

    QStringList ids;
    for (const char* name : {"Booth", "Stage"}) {
        call(tab.dests, "addDestination", QStringLiteral("display"));
        ids << tab.text("selectedDestId");
        call(tab.dests, "setDestinationName", ids.back(), QString::fromLatin1(name));
    }
    REQUIRE(ids.size() == 2);
    QTest::qWait(50);

    const auto order = [&] {
        QStringList out;
        for (const auto& d : tab.dests->property("destinations").toList()) {
            out << d.toMap().value(QStringLiteral("id")).toString();
        }
        return out;
    };
    const auto button = [&](const char* name) -> QQuickItem* {
        for (auto* item : visible_qml_items_of(tab.panel, "AppButton")) {
            if (item->objectName() == QString::fromLatin1(name)) {
                return item;
            }
        }
        FAIL("no button " << name);
        return nullptr;
    };

    // Stage, the last added, is selected: it can go up, not down.
    REQUIRE(tab.text("selectedDestId") == ids[1]);
    CHECK(button("destinationMoveUp")->isEnabled());
    CHECK_FALSE(button("destinationMoveDown")->isEnabled());

    click(ui.window(), centre_of(button("destinationMoveUp")));
    CHECK(order() == QStringList{ids[1], ids[0]});
    CHECK(tab.text("selectedDestId") == ids[1]);
    CHECK_FALSE(button("destinationMoveUp")->isEnabled());
    CHECK(button("destinationMoveDown")->isEnabled());

    click(ui.window(), centre_of(button("destinationMoveDown")));
    CHECK(order() == QStringList{ids[0], ids[1]});
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("A destination matches the output or is sent smaller or as sound alone",
          "[ui][output-edit][destinations]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.init_libraries(dir.path / "data"));
    seventhslide::render::OutputWindowHost host;
    facade.set_output_host(&host);
    OperatorUi ui(facade);
    DestinationsTab tab(ui);
    auto* w = ui.window();
    QObject* state = qml_object(tab.dialog, "OutputEditState");
    REQUIRE(state != nullptr);
    // The size General has, saved or not, is the one a destination matches.
    REQUIRE(state->setProperty("canvasW", QStringLiteral("1920")));
    REQUIRE(state->setProperty("canvasH", QStringLiteral("1080")));

    // The host's own destinations, off so nothing opens: an NDI source (no
    // runtime needed while it is off) with its sound off, a display and a
    // Blackmagic card.
    const QString out = tab.dests->property("outputId").toString();
    REQUIRE_FALSE(out.isEmpty());
    const auto record = [&out](const char* id, const char* type) {
        return nlohmann::json{{"id", id},
                              {"output", out.toStdString()},
                              {"type", type},
                              {"enabled", false},
                              {"key", "SeventhSlide Test"},
                              {"name", id},
                              {"audio", false}};
    };
    host.replace_destinations(
        nlohmann::json::array({record("n1", "ndi"), record("d1", "display"), record("b1", "bmd")}));
    const auto select = [&](const char* id) {
        call(tab.dests, "loadDestinations");
        REQUIRE(tab.dests->setProperty("selectedDestId", QString::fromLatin1(id)));
        QTest::qWait(20);
    };
    const auto stored = [&host](const char* id) {
        const auto* d = host.registry().find(QString::fromLatin1(id));
        REQUIRE(d != nullptr);
        return *d;
    };
    select("n1");

    // The dropdown on the Size row (an NDI source has Transport's too).
    const auto size_field = [&]() -> QQuickItem* {
        QQuickItem* row = visible_text_under(tab.panel, QStringLiteral("Size"));
        REQUIRE(row != nullptr);
        while (row != nullptr && visible_qml_items_of(row, "DropdownField").isEmpty()) {
            row = row->parentItem();
        }
        REQUIRE(row != nullptr);
        const auto fields = visible_qml_items_of(row, "DropdownField");
        REQUIRE(fields.size() == 1);
        return into_view(fields.front());
    };
    const auto shows = [&](const char* text) {
        return size_field()->property("text").toString() == QString::fromUtf8(text);
    };

    // Matching, the size is the output's and cannot be picked.
    CHECK(visible_text_under(tab.panel, QStringLiteral("Destination")) != nullptr);
    CHECK(visible_text_under(tab.panel, QStringLiteral("Format")) != nullptr);
    CHECK(visible_text_under(tab.panel, QStringLiteral("Audio only")) == nullptr);
    CHECK(shows("1920 × 1080"));
    CHECK_FALSE(size_field()->isEnabled());

    // The switch on the row labelled ``label``.
    const auto switch_of = [&](const char* label) -> QQuickItem* {
        QQuickItem* row = visible_text_under(tab.panel, QString::fromLatin1(label));
        REQUIRE(row != nullptr);
        while (row != nullptr && visible_qml_items_of(row, "ToggleSwitch").isEmpty()) {
            row = row->parentItem();
        }
        REQUIRE(row != nullptr);
        return into_view(visible_qml_items_of(row, "ToggleSwitch").front());
    };

    // Its switch lets it have its own, starting from the output's, and the
    // host keeps it.
    click(w, centre_of(switch_of("Match output")));
    CHECK(size_field()->isEnabled());
    CHECK(shows("1920 × 1080"));
    CHECK(stored("n1").max_height == 1080);

    // The sizes: the output's, then the usual heights under it, as wide as
    // the output's shape makes them, rounded to an even width.
    QStringList offered;
    for (const auto& o : call(tab.dests, "destSizeOptions").toList()) {
        const auto m = o.toMap();
        offered << QStringLiteral("%1x%2").arg(m.value(QStringLiteral("w")).toInt())
                       .arg(m.value(QStringLiteral("h")).toInt());
    }
    CHECK(offered == QStringList{QStringLiteral("1920x1080"), QStringLiteral("1280x720"),
                                 QStringLiteral("960x540"), QStringLiteral("854x480"),
                                 QStringLiteral("640x360")});

    // Picked from its menu.
    click(w, centre_of(size_field()));
    CHECK(size_field()->property("open").toBool());
    QQuickItem* item = visible_text_under(w->contentItem(), QStringLiteral("480p"));
    REQUIRE(item != nullptr);
    click(w, centre_of(item));
    CHECK_FALSE(size_field()->property("open").toBool());
    CHECK(shows("854 × 480"));
    CHECK(stored("n1").max_height == 480);

    // A smaller canvas keeps the pick while it fits; under it, the pick is
    // the canvas's own size.
    REQUIRE(state->setProperty("canvasW", QStringLiteral("1280")));
    REQUIRE(state->setProperty("canvasH", QStringLiteral("720")));
    CHECK(shows("854 × 480"));
    REQUIRE(state->setProperty("canvasW", QStringLiteral("800")));
    REQUIRE(state->setProperty("canvasH", QStringLiteral("450")));
    CHECK(shows("800 × 450"));

    // An NDI source not matching can carry the sound alone: no size then,
    // and its sound is on, and stays on while it is.
    CHECK(switch_of("Send audio")->isEnabled());
    click(w, centre_of(switch_of("Audio only")));
    CHECK_FALSE(size_field()->isEnabled());
    CHECK(shows("No video"));
    CHECK_FALSE(stored("n1").video);
    CHECK(stored("n1").audio);
    CHECK(switch_of("Send audio")->property("checked").toBool());
    CHECK_FALSE(switch_of("Send audio")->isEnabled());

    // Matching again, it follows the output, picture and all; audio only
    // goes with the rest of what did not match.
    click(w, centre_of(switch_of("Match output")));
    CHECK_FALSE(size_field()->isEnabled());
    CHECK(shows("800 × 450"));
    CHECK(visible_text_under(tab.panel, QStringLiteral("Audio only")) == nullptr);
    CHECK(stored("n1").max_height == 0);
    CHECK(stored("n1").video);
    click(w, centre_of(switch_of("Match output")));
    CHECK(shows("800 × 450"));
    CHECK_FALSE(switch_of("Audio only")->property("checked").toBool());
    CHECK(stored("n1").max_height == 450);

    // A display has a size of its own too (drawn smaller, filling its
    // screen), but only NDI carries sound alone.
    select("d1");
    CHECK_FALSE(size_field()->isEnabled());
    CHECK(visible_text_under(tab.panel, QStringLiteral("Audio only")) == nullptr);
    click(w, centre_of(switch_of("Match output")));
    CHECK(size_field()->isEnabled());
    CHECK(stored("d1").max_height == 450);
    CHECK(visible_text_under(tab.panel, QStringLiteral("Audio only")) == nullptr);

    // A Blackmagic card's video mode decides its size: no choice offered.
    select("b1");
    CHECK(visible_text_under(tab.panel, QStringLiteral("Format")) == nullptr);
    CHECK(visible_text_under(tab.panel, QStringLiteral("Match output")) == nullptr);

    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("An SRT destination's Stream settings go to its host",
          "[ui][output-edit][destinations][srt]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.init_libraries(dir.path / "data"));
    seventhslide::render::OutputWindowHost host;
    facade.set_output_host(&host);
    OperatorUi ui(facade);
    DestinationsTab tab(ui);
    auto* w = ui.window();
    QSignalSpy reported(tab.dests, SIGNAL(reported(QString)));

    // The host's own SRT destination, off: no stream starts.
    const QString out = tab.dests->property("outputId").toString();
    REQUIRE_FALSE(out.isEmpty());
    host.replace_destinations(nlohmann::json::array({nlohmann::json{{"id", "s1"},
                                                                    {"output", out.toStdString()},
                                                                    {"type", "srt"},
                                                                    {"name", "Feed"},
                                                                    {"enabled", false}}}));
    call(tab.dests, "loadDestinations");
    REQUIRE(tab.dests->setProperty("selectedDestId", QStringLiteral("s1")));
    QTest::qWait(20);
    const auto stored = [&host] { return host.registry().find(QStringLiteral("s1"))->stream; };

    // The control of type ``type`` on the row labelled ``label``.
    const auto control_of = [&](const char* label, const char* type) -> QQuickItem* {
        QQuickItem* row = visible_text_under(tab.panel, QString::fromLatin1(label));
        REQUIRE(row != nullptr);
        while (row != nullptr && visible_qml_items_of(row, type).isEmpty()) {
            row = row->parentItem();
        }
        REQUIRE(row != nullptr);
        return into_view(visible_qml_items_of(row, type).front());
    };
    // Pick ``option`` from the dropdown on the row labelled ``label``.
    const auto pick = [&](const char* label, const char* option) {
        click(w, centre_of(control_of(label, "OptionField")));
        QQuickItem* item = visible_text_under(w->contentItem(), QString::fromUtf8(option));
        INFO("option " << option);
        REQUIRE(item != nullptr);
        click(w, centre_of(item));
    };

    // Waiting for a receiver: a port, no address.
    CHECK(visible_text_under(tab.panel, QStringLiteral("Stream")) != nullptr);
    CHECK(visible_text_under(tab.panel, QStringLiteral("Port")) != nullptr);
    CHECK(visible_text_under(tab.panel, QStringLiteral("Address")) == nullptr);
    CHECK(visible_text_under(tab.panel, QStringLiteral("Passphrase")) == nullptr);
    // It is sized and carries sound, as the other network destinations do.
    CHECK(visible_text_under(tab.panel, QStringLiteral("Format")) != nullptr);
    CHECK(visible_text_under(tab.panel, QStringLiteral("Send audio")) != nullptr);

    // Sending to a receiver asks for its address.
    pick("Connection", "Send to a receiver");
    CHECK(stored().mode == seventhslide::stream::SrtMode::Caller);
    CHECK(visible_text_under(tab.panel, QStringLiteral("Address")) != nullptr);

    pick("Quality", "Reliable");
    CHECK(stored().preset == seventhslide::stream::StreamPreset::Reliable);
    pick("Bitrate", "8.0 Mb/s");
    CHECK(stored().bitrate_kbps == 8000);

    // A wrong port is refused, and said why.
    reported.clear();
    call(tab.dests, "setDestinationStream", QStringLiteral("s1"),
         QVariantMap{{QStringLiteral("port"), 0}});
    REQUIRE(reported.count() == 1);
    CHECK_FALSE(reported.front().front().toString().isEmpty());
    CHECK(stored().port == 9000);

    // Encryption asks for a passphrase, and is on only once one is given.
    click(w, centre_of(control_of("Encryption", "ToggleSwitch")));
    CHECK(visible_text_under(tab.panel, QStringLiteral("Passphrase")) != nullptr);
    CHECK(stored().passphrase.empty());
    call(tab.dests, "setDestinationStream", QStringLiteral("s1"),
         QVariantMap{{QStringLiteral("passphrase"), QStringLiteral("a long passphrase")}});
    CHECK(stored().passphrase == "a long passphrase");
    // Off again takes it away.
    click(w, centre_of(control_of("Encryption", "ToggleSwitch")));
    CHECK(stored().passphrase.empty());
    CHECK(visible_text_under(tab.panel, QStringLiteral("Passphrase")) == nullptr);

    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("An NDI source picks this computer's transport, Automatic or TCP",
          "[ui][output-edit][destinations][ndi]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.init_libraries(dir.path / "data"));
    OperatorUi ui(facade);
    DestinationsTab tab(ui);
    auto* w = ui.window();
    tab.show({fake("ndi")}, QStringLiteral("d1"));

    const auto transport_field = [&]() -> QQuickItem* {
        QQuickItem* row = visible_text_under(tab.panel, QStringLiteral("Transport"));
        REQUIRE(row != nullptr);
        while (row != nullptr && visible_qml_items_of(row, "DropdownField").isEmpty()) {
            row = row->parentItem();
        }
        REQUIRE(row != nullptr);
        return visible_qml_items_of(row, "DropdownField").front();
    };
    CHECK(facade.ndiTransport() == QStringLiteral("auto"));
    CHECK(transport_field()->property("text").toString() == QStringLiteral("Automatic"));

    // Picked from its menu: kept as this computer's, in machine.json.
    click(w, centre_of(transport_field()));
    for (const auto& label : {QStringLiteral("Reliable UDP"), QStringLiteral("UDP"),
                              QStringLiteral("Single TCP")}) {
        CHECK(visible_text_under(w->contentItem(), label) != nullptr);
    }
    QQuickItem* multi = visible_text_under(w->contentItem(), QStringLiteral("Multi-TCP"));
    REQUIRE(multi != nullptr);
    click(w, centre_of(multi));
    CHECK_FALSE(transport_field()->property("open").toBool());
    CHECK(facade.ndiTransport() == QStringLiteral("multi_tcp"));
    CHECK(transport_field()->property("text").toString() == QStringLiteral("Multi-TCP"));
    {
        SessionFacade again;
        REQUIRE(again.init_libraries(dir.path / "data"));
        CHECK(again.ndiTransport() == QStringLiteral("multi_tcp"));
    }

    // A runtime this process already loaded keeps what it loaded with, and
    // the panel says the change waits for a restart; with none loaded yet it
    // takes the change as it loads.
    const auto restart = QStringLiteral("Restart SeventhSlide to change the transport.");
    CHECK(facade.ndiTransportPending() == seventhslide::render::ndi_runtime_available());
    CHECK((visible_text_under(tab.panel, restart) != nullptr) == facade.ndiTransportPending());

    // Back to Automatic, as every other test here expects the runtime to load.
    call(tab.dests, "setNdiTransport", QStringLiteral("auto"));
    CHECK(facade.ndiTransport() == QStringLiteral("auto"));
    CHECK_FALSE(facade.ndiTransportPending());
    CHECK(visible_text_under(tab.panel, restart) == nullptr);

    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("The bar's Live sends the enabled destinations and asks before stopping them",
          "[ui][destinations][live]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.init_libraries(dir.path / "data"));
    // As the app starts.
    seventhslide::render::OutputWindowHost host;
    host.set_live(false);
    facade.set_output_host(&host);
    facade.set_audience_outputs_enabled(true);
    OperatorUi ui(facade);
    QTest::qWait(50);

    const QString out = QString::fromStdString(facade.session().primary_output()->id);
    const QString dest = host.add_destination(out, QStringLiteral("display"));
    REQUIRE(host.set_destination_screen(dest, seventhslide::test::first_display_key(host)));
    REQUIRE(host.set_destination_enabled(dest, true));
    CHECK_FALSE(host.is_open(out));

    auto* live = ui.window()->findChild<QQuickItem*>(QStringLiteral("liveButton"));
    auto* confirm = ui.window()->findChild<QQuickItem*>(QStringLiteral("liveOffConfirm"));
    REQUIRE(live != nullptr);
    REQUIRE(confirm != nullptr);
    const auto press = [&](QQuickItem* item) {
        click(ui.window(), centre_of(item));
        QTest::qWait(20);
    };
    const auto confirm_button = [&](const QString& label) {
        REQUIRE(confirm->property("open").toBool());
        QQuickItem* text = visible_text_under(confirm, label);
        REQUIRE(text != nullptr);
        return text;
    };

    // On: what is enabled opens.
    press(live);
    CHECK(facade.destinations()->liveOn());
    CHECK(host.is_open(out));

    // Off while it sends asks first; Cancel leaves it on.
    press(live);
    CHECK(host.live());
    press(confirm_button(QStringLiteral("Cancel")));
    CHECK_FALSE(confirm->property("open").toBool());
    CHECK(host.is_open(out));

    // Turn Off stops it, and it stays enabled.
    press(live);
    press(confirm_button(QStringLiteral("Turn Off")));
    CHECK_FALSE(facade.destinations()->liveOn());
    CHECK_FALSE(host.is_open(out));
    CHECK(host.registry().find(dest)->enabled);

    // With nothing sending, off does not ask.
    REQUIRE(host.set_destination_enabled(dest, false));
    press(live);
    CHECK(host.live());
    press(live);
    CHECK_FALSE(host.live());
    CHECK_FALSE(confirm->property("open").toBool());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("A destination's Ignore Live sends it while Live is off", "[ui][destinations][live]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.init_libraries(dir.path / "data"));
    seventhslide::render::OutputWindowHost host;
    host.set_live(false);
    facade.set_output_host(&host);
    OperatorUi ui(facade);
    DestinationsTab tab(ui);
    REQUIRE(tab.dests->setProperty("shown", true));

    call(tab.dests, "addDestination", QStringLiteral("display"));
    const QString id = tab.text("selectedDestId");
    REQUIRE_FALSE(id.isEmpty());
    QTest::qWait(50);
    const auto ignoring = [&] { return host.registry().find(id)->ignore_live; };

    QQuickItem* label = visible_text_under(tab.panel, QStringLiteral("Ignore Live"));
    REQUIRE(label != nullptr);
    // Its row's switch.
    QQuickItem* row = label->parentItem();
    while (row != nullptr
           && !QString::fromLatin1(row->metaObject()->className()).startsWith(QStringLiteral("SettingRow"))) {
        row = row->parentItem();
    }
    REQUIRE(row != nullptr);
    const auto toggles = visible_qml_items_of(row, "ToggleSwitch");
    REQUIRE(toggles.size() == 1);
    QQuickItem* toggle = into_view(toggles.front());

    click(ui.window(), centre_of(toggle));
    CHECK(ignoring());
    click(ui.window(), centre_of(toggle));
    CHECK_FALSE(ignoring());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}
