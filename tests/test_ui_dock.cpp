// The operator window's dock and bottom bar, driven as the operator drives
// them: Layout ▾ shows and hides panels and turns Edit Mode on; the lines
// resize and a title bar collapses its panel at any time; in Edit Mode a
// panel moves by its title bar to any side of another panel or the
// Controller; the bar's buttons open Settings.

#include <catch2/catch_test_macros.hpp>

#include "dock_layout.hpp"
#include "operator_ui.hpp"
#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"
#include "ui_library_fixture.hpp"
#include "ui_test_helpers.hpp"

#include <QJSValue>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QString>
#include <QTest>

using seventhslide::app::DockLayout;
using seventhslide::app::DockState;
using seventhslide::app::SessionFacade;
using seventhslide::test::OperatorUi;
using seventhslide::test::centre_of;
using seventhslide::test::click;
using seventhslide::test::drag;
using seventhslide::test::visible_text_under;

namespace {

/// The DockPanel holding panel ``id``.
QQuickItem* dock_panel(const OperatorUi& ui, const QString& id)
{
    for (QQuickItem* item : ui.window()->findChildren<QQuickItem*>()) {
        if (QString::fromLatin1(item->metaObject()->className())
                    .startsWith(QStringLiteral("DockPanel_QMLTYPE_"))
            && item->property("dockId").toString() == id) {
            return item;
        }
    }
    INFO("panel " << id.toStdString());
    FAIL("no such panel");
    return nullptr;
}

/// Panel ``panel``'s title bar.
QQuickItem* title_bar(QQuickItem* panel)
{
    for (QQuickItem* item : panel->findChildren<QQuickItem*>()) {
        if (QString::fromLatin1(item->metaObject()->className())
                .startsWith(QStringLiteral("DockTitleBar_QMLTYPE_"))) {
            return item;
        }
    }
    FAIL("no title bar");
    return nullptr;
}

/// Where panel ``id``'s title is, to drag it by.
QPoint title_of(const OperatorUi& ui, const QString& id, const QString& title)
{
    QQuickItem* text = visible_text_under(dock_panel(ui, id), title);
    REQUIRE(text != nullptr);
    return centre_of(text);
}

/// Item ``label`` of the open Layout menu.
QPoint layout_menu_item(const OperatorUi& ui, const QString& label)
{
    QObject* menu = ui.window()->findChild<QObject*>(QStringLiteral("layoutMenu"));
    REQUIRE(menu != nullptr);
    REQUIRE(menu->property("visible").toBool());
    auto* items = menu->property("contentItem").value<QQuickItem*>();
    REQUIRE(items != nullptr);
    QQuickItem* row = visible_text_under(items, label);
    INFO("menu item " << label.toStdString());
    REQUIRE(row != nullptr);
    return centre_of(row);
}

void open_layout_menu(const OperatorUi& ui)
{
    auto* button = ui.window()->findChild<QQuickItem*>(QStringLiteral("layoutButton"));
    REQUIRE(button != nullptr);
    click(ui.window(), centre_of(button));
}

}  // namespace

TEST_CASE("Layout shows and hides panels, and resets them", "[ui][dock]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    OperatorUi ui(facade);
    QTest::qWait(50);
    DockLayout* layout = ui.dock_layout();
    REQUIRE(layout != nullptr);

    QQuickItem* library = dock_panel(ui, QStringLiteral("library"));
    QQuickItem* service = dock_panel(ui, QStringLiteral("service"));
    REQUIRE(library->isVisible());
    const qreal shared = library->height() + service->height();

    open_layout_menu(ui);
    click(ui.window(), layout_menu_item(ui, QStringLiteral("Library")));
    CHECK(layout->hidden() == QStringList{QStringLiteral("library")});
    CHECK_FALSE(library->isVisible());
    // The Service takes the whole column.
    CHECK(service->height() > shared);

    // The menu stays open for the next panel; Reset Layout puts all back.
    click(ui.window(), layout_menu_item(ui, QStringLiteral("Reset Layout")));
    CHECK(layout->hidden().isEmpty());
    CHECK(library->isVisible());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("The lines resize and a title bar collapses its panel, without Edit Mode",
          "[ui][dock]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    OperatorUi ui(facade);
    QTest::qWait(50);
    DockLayout* layout = ui.dock_layout();
    REQUIRE(layout != nullptr);
    REQUIRE_FALSE(layout->editMode());
    QQuickItem* service = dock_panel(ui, QStringLiteral("service"));
    QQuickItem* library = dock_panel(ui, QStringLiteral("library"));

    // The line right of the left column, dragged 60 px right, widens it.
    const int before = static_cast<int>(service->width());
    const QPoint line(before, ui.window()->height() / 2);
    drag(ui.window(), line, line + QPoint(60, 0));
    CHECK(layout->state().root.children[0].extent == before + 60);
    CHECK(service->width() == before + 60);

    // The line under the Service, dragged down, gives it more height.
    const qreal service_h = service->height();
    const QPoint under = service->mapToScene(QPointF(service->width() / 2,
                                                     service->height())).toPoint();
    drag(ui.window(), under, under + QPoint(0, 40));
    CHECK(service->height() > service_h + 30);

    // A click on the Service's title bar collapses it to the bar; the Library
    // takes the rest. Another opens it at the height it had.
    const qreal opened_h = service->height();
    click(ui.window(), title_of(ui, QStringLiteral("service"), QStringLiteral("Service")));
    CHECK(layout->collapsed() == QStringList{QStringLiteral("service")});
    CHECK(service->height() == title_bar(service)->height());
    CHECK(library->y() == service->height() + 1);
    CHECK(library->y() + library->height() == service->parentItem()->height());
    click(ui.window(), title_of(ui, QStringLiteral("service"), QStringLiteral("Service")));
    CHECK(layout->collapsed().isEmpty());
    CHECK(service->height() == opened_h);
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("A drag from the Library opens a collapsed Service panel", "[ui][dock]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    seventhslide::test::ui::build(facade, dir.path);
    OperatorUi ui(facade);
    QTest::qWait(50);
    DockLayout* layout = ui.dock_layout();
    REQUIRE(layout != nullptr);
    layout->setCollapsed(QStringLiteral("service"), true);
    QTest::qWait(20);

    // A song dragged toward the Service's bar: the panel opens to take it.
    QQuickItem* song = visible_text_under(dock_panel(ui, QStringLiteral("library")),
                                          QStringLiteral("Alpha"));
    REQUIRE(song != nullptr);
    drag(ui.window(), centre_of(song),
         title_of(ui, QStringLiteral("service"), QStringLiteral("Service")));
    CHECK(layout->collapsed().isEmpty());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("In Edit Mode, and only there, a panel moves by its title bar", "[ui][dock]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    OperatorUi ui(facade);
    QTest::qWait(50);
    DockLayout* layout = ui.dock_layout();
    REQUIRE(layout != nullptr);
    QQuickItem* outputs = dock_panel(ui, QStringLiteral("outputs"));
    QQuickItem* library = dock_panel(ui, QStringLiteral("library"));
    const QString title = QStringLiteral("Outputs");

    // Out of Edit Mode, a drag from the title bar moves nothing, and is not a
    // click: nothing collapses.
    drag(ui.window(), title_of(ui, QStringLiteral("outputs"), title),
         centre_of(library));
    CHECK(layout->state() == DockState::defaults());
    CHECK(layout->collapsed().isEmpty());

    open_layout_menu(ui);
    click(ui.window(), layout_menu_item(ui, QStringLiteral("Edit Mode")));
    REQUIRE(layout->editMode());

    // Dropped on the upper half of the Library: above it, in the left column.
    const QPoint upper_library =
        library->mapToScene(QPointF(library->width() / 2, library->height() / 4)).toPoint();
    drag(ui.window(), title_of(ui, QStringLiteral("outputs"), title), upper_library);
    CHECK(layout->state().describe() == "row(column(service,outputs,library),controller,mixer)");
    CHECK(layout->collapsed().isEmpty());
    CHECK(outputs->x() == 0);
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("In Edit Mode a panel goes under the Controller, or beside another panel",
          "[ui][dock]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    OperatorUi ui(facade);
    QTest::qWait(50);
    DockLayout* layout = ui.dock_layout();
    REQUIRE(layout != nullptr);
    layout->setEditMode(true);
    QTest::qWait(20);
    QQuickItem* mixer = dock_panel(ui, QStringLiteral("mixer"));
    QQuickItem* library = dock_panel(ui, QStringLiteral("library"));
    QQuickItem* service = dock_panel(ui, QStringLiteral("service"));
    auto* controller = qobject_cast<QQuickItem*>(ui.find_component("ControllerColumn"));
    REQUIRE(controller != nullptr);

    // Dropped low over the Controller: under it, as wide as it.
    const QPoint low = controller->mapToScene(QPointF(controller->width() / 2,
                                                      controller->height() * 0.9))
                           .toPoint();
    drag(ui.window(), title_of(ui, QStringLiteral("mixer"), QStringLiteral("Audio Mixer")), low);
    CHECK(layout->state().describe()
          == "row(column(service,library),column(controller,mixer),outputs)");
    CHECK(mixer->x() == controller->x());
    CHECK(mixer->width() == controller->width());
    CHECK(mixer->y() == controller->height() + 1);

    // Dropped at the Library's right: the two side by side under the Service.
    const QPoint right_of_library =
        library->mapToScene(QPointF(library->width() * 0.9, library->height() / 2)).toPoint();
    drag(ui.window(), title_of(ui, QStringLiteral("mixer"), QStringLiteral("Audio Mixer")),
         right_of_library);
    CHECK(layout->state().describe()
          == "row(column(service,row(library,mixer)),controller,outputs)");
    CHECK(mixer->y() == library->y());
    CHECK(mixer->x() == library->x() + library->width() + 1);
    CHECK(mixer->x() + mixer->width() == service->width());

    // Collapsed beside the Library, it stands on end; the Library widens.
    const qreal library_w = library->width();
    click(ui.window(), title_of(ui, QStringLiteral("mixer"), QStringLiteral("Audio Mixer")));
    CHECK(layout->collapsed() == QStringList{QStringLiteral("mixer")});
    CHECK(mixer->width() == title_bar(mixer)->height());
    CHECK(mixer->height() == library->height());
    CHECK(library->width() > library_w);
    // Its turned title still opens it.
    click(ui.window(), title_of(ui, QStringLiteral("mixer"), QStringLiteral("Audio Mixer")));
    CHECK(layout->collapsed().isEmpty());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("A panel being moved shows a line along the edge it would land on", "[ui][dock]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    OperatorUi ui(facade);
    QTest::qWait(50);
    DockLayout* layout = ui.dock_layout();
    REQUIRE(layout != nullptr);
    layout->setEditMode(true);
    QTest::qWait(20);
    auto* area = qobject_cast<QQuickItem*>(ui.find_component("DockArea"));
    REQUIRE(area != nullptr);
    QQuickItem* mixer = dock_panel(ui, QStringLiteral("mixer"));
    QQuickItem* library = dock_panel(ui, QStringLiteral("library"));
    auto* controller = qobject_cast<QQuickItem*>(ui.find_component("ControllerColumn"));
    REQUIRE(controller != nullptr);

    QPoint at = title_of(ui, QStringLiteral("mixer"), QStringLiteral("Audio Mixer"));
    QTest::mousePress(ui.window(), Qt::LeftButton, {}, at);
    const auto move_to = [&](QPoint to) {
        for (int i = 1; i <= 4; ++i) {
            QTest::mouseMove(ui.window(), at + (to - at) * i / 4);
            QTest::qWait(10);
        }
        at = to;
        return area->property("dropTarget").value<QJSValue>();
    };

    // High over the Library: a thin line over its top, as wide as it, less
    // a little at each end.
    QJSValue drop = move_to(library->mapToScene(QPointF(library->width() / 2,
                                                        library->height() / 5)).toPoint());
    CHECK(drop.property("target").toString() == QStringLiteral("library"));
    CHECK(drop.property("edge").toString() == QStringLiteral("top"));
    QJSValue line = drop.property("line");
    CHECK(line.property("height").toInt() == 3);
    CHECK(line.property("width").toInt() == static_cast<int>(library->width()) - 12);
    CHECK(line.property("y").toInt() == static_cast<int>(library->y()) - 2);

    // Near the Controller's left: a thin line down its left side.
    drop = move_to(controller->mapToScene(QPointF(controller->width() / 10,
                                                  controller->height() / 2)).toPoint());
    CHECK(drop.property("target").toString() == QStringLiteral("controller"));
    CHECK(drop.property("edge").toString() == QStringLiteral("left"));
    line = drop.property("line");
    CHECK(line.property("width").toInt() == 3);
    CHECK(line.property("height").toInt() == static_cast<int>(controller->height()) - 12);
    CHECK(line.property("x").toInt() == static_cast<int>(controller->x()) - 2);

    // At the bottom of the whole area: a line along all of it.
    drop = move_to(area->mapToScene(QPointF(area->width() / 2, area->height() - 5)).toPoint());
    CHECK(drop.property("target").toString().isEmpty());
    CHECK(drop.property("edge").toString() == QStringLiteral("bottom"));
    line = drop.property("line");
    CHECK(line.property("height").toInt() == 3);
    CHECK(line.property("width").toInt() == static_cast<int>(area->width()) - 12);

    // Over the panel moving, nothing; let go there and it stays, and open.
    move_to(centre_of(mixer));
    // Read as it is: null does not come through QJSValue as null.
    CHECK(area->property("dropTarget").isNull());
    QTest::mouseRelease(ui.window(), Qt::LeftButton, {}, at);
    QTest::qWait(20);
    CHECK(layout->state() == DockState::defaults());
    CHECK(layout->collapsed().isEmpty());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("The bottom bar opens Libraries & Profiles and Settings", "[ui][dock]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.init_libraries(dir.path));
    OperatorUi ui(facade);
    QTest::qWait(50);
    QObject* settings = ui.find_component("AppSettingsDialog");
    REQUIRE(settings != nullptr);
    auto* bar = qobject_cast<QQuickItem*>(ui.find_component("AppBar"));
    REQUIRE(bar != nullptr);

    // The library button names the library and its profile.
    const QString library = facade.libraries()->activeLibraryName();
    REQUIRE_FALSE(library.isEmpty());
    QQuickItem* name = visible_text_under(bar, library);
    REQUIRE(name != nullptr);
    click(ui.window(), centre_of(name));
    CHECK(settings->property("open").toBool());
    CHECK(settings->property("tab").toString() == QStringLiteral("library"));
    REQUIRE(QMetaObject::invokeMethod(settings, "close"));

    QQuickItem* gear = visible_text_under(bar, QStringLiteral("Settings"));
    REQUIRE(gear != nullptr);
    click(ui.window(), centre_of(gear));
    CHECK(settings->property("open").toBool());
    CHECK(settings->property("tab").toString() == QStringLiteral("general"));
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}
