#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dock_layout.hpp"
#include "test_qt_app.hpp"

#include <QSettings>
#include <QSignalSpy>
#include <QStringList>

#include <algorithm>
#include <string>
#include <vector>

using seventhslide::app::DockLayout;
using seventhslide::app::DockState;
using Edge = DockState::Edge;
using Rect = DockState::Rect;
using Catch::Matchers::WithinAbs;

namespace {

constexpr const char* k_default =
    "row(column(service,library),controller,column(outputs,mixer))";

/// Start each test from no saved layout.
void forget_layout()
{
    seventhslide::test::ensure_qt_application();
    QSettings().remove(QString::fromLatin1(DockLayout::k_settings_key.data()));
}

Rect place(const DockState::Geometry& g, std::string_view id)
{
    const auto* p = g.find(id);
    REQUIRE(p != nullptr);
    return p->rect;
}

}  // namespace

TEST_CASE("The dock starts with Service and Library left, Outputs and the Audio Mixer right",
          "[app][dock]")
{
    const auto s = DockState::defaults();
    CHECK(s.describe() == k_default);
    // The previews keep the larger share; the Library more than the Service.
    CHECK(s.find("outputs")->weight > s.find("mixer")->weight);
    CHECK(s.find("library")->weight > s.find("service")->weight);
    CHECK(s.root.children[0].extent == 360);
    CHECK(s.root.children[2].extent == 290);

    // Drawn: the columns at their widths, the Controller between, a line
    // either side of it.
    const auto g = s.layout(1400, 800, {});
    CHECK(place(g, "service") == Rect{0, 0, 360, 355});
    CHECK(place(g, "library") == Rect{0, 356, 360, 444});
    CHECK(place(g, "controller") == Rect{361, 0, 1400 - 360 - 290 - 2, 800});
    CHECK(place(g, "outputs").x == 1400 - 290);
    CHECK(g.lines.size() == 4);
}

TEST_CASE("A panel moves to any side of another panel, the Controller, or the window",
          "[app][dock]")
{
    auto s = DockState::defaults();

    SECTION("above a panel in another column")
    {
        REQUIRE(s.move("outputs", "library", Edge::Top));
        CHECK(s.describe() == "row(column(service,outputs,library),controller,mixer)");
    }
    SECTION("beside a panel: the two share its place")
    {
        REQUIRE(s.move("outputs", "library", Edge::Right));
        CHECK(s.describe()
              == "row(column(service,row(library,outputs)),controller,mixer)");
        const auto g = s.layout(1400, 800, {});
        CHECK(place(g, "library").y == place(g, "outputs").y);
        CHECK(place(g, "library").width + place(g, "outputs").width + 1
              == place(g, "service").width);
    }
    SECTION("over the Controller, and under it")
    {
        REQUIRE(s.move("mixer", "controller", Edge::Top));
        REQUIRE(s.move("service", "controller", Edge::Bottom));
        CHECK(s.describe()
              == "row(library,column(mixer,controller,service),outputs)");
        // Beside the Controller each keeps its default height; the
        // Controller takes the rest.
        const auto g = s.layout(1400, 800, {});
        CHECK(place(g, "mixer").height == 260);
        CHECK(place(g, "service").height == 260);
        CHECK(place(g, "controller").height == 800 - 2 * 260 - 2);
        CHECK(place(g, "mixer").width == place(g, "controller").width);
    }
    SECTION("along a whole edge of the window")
    {
        REQUIRE(s.move("mixer", "", Edge::Bottom));
        CHECK(s.describe()
              == "column(row(column(service,library),controller,outputs),mixer)");
        const auto g = s.layout(1400, 800, {});
        CHECK(place(g, "mixer") == Rect{0, 800 - 260, 1400, 260});
        REQUIRE(s.move("library", "", Edge::Left));
        CHECK(s.describe()
              == "row(library,column(row(service,controller,outputs),mixer))");
    }
    SECTION("within its own column")
    {
        REQUIRE(s.move("library", "service", Edge::Top));
        CHECK(s.describe() == "row(column(library,service),controller,column(outputs,mixer))");
    }
    SECTION("to where it is already")
    {
        const auto before = s;
        CHECK_FALSE(s.move("mixer", "outputs", Edge::Bottom));
        CHECK_FALSE(s.move("service", "library", Edge::Top));
        CHECK(s == before);
    }
    SECTION("a hidden panel moved is shown")
    {
        REQUIRE(s.set_shown("library", false));
        REQUIRE(s.move("library", "outputs", Edge::Top));
        CHECK(s.describe() == "row(service,controller,column(library,outputs,mixer))");
    }
    CHECK_FALSE(s.move("nothing", "", Edge::Left));
    CHECK_FALSE(s.move("controller", "service", Edge::Left));
    CHECK_FALSE(s.move("service", "service", Edge::Left));
    CHECK_FALSE(s.move("service", "teleprompter", Edge::Left));
}

TEST_CASE("A split that goes into its parent leaves its panels the length it had",
          "[app][dock]")
{
    auto s = DockState::defaults();
    REQUIRE(s.move("outputs", "library", Edge::Right));
    REQUIRE(s.move("service", "controller", Edge::Left));
    // Library and Outputs shared the left column, 360 wide, half each; with
    // the Service gone they stand beside the Controller on their own.
    CHECK(s.describe() == "row(library,outputs,service,controller,mixer)");
    CHECK(s.root.children[0].extent == 180);
    CHECK(s.root.children[1].extent == 180);
    const auto g = s.layout(1400, 800, {});
    CHECK(place(g, "library").width == 180);
    CHECK(place(g, "outputs").width == 180);
}

TEST_CASE("Hidden panels keep their place and take no room", "[app][dock]")
{
    auto s = DockState::defaults();
    REQUIRE(s.set_shown("service", false));
    CHECK_FALSE(s.set_shown("service", false));
    CHECK_FALSE(s.set_shown("controller", false));
    auto g = s.layout(1400, 800, {});
    CHECK(g.find("service") == nullptr);
    CHECK(place(g, "library") == Rect{0, 0, 360, 800});
    REQUIRE(s.set_shown("service", true));
    CHECK(s.describe() == k_default);

    // A column with nothing shown takes no width.
    REQUIRE(s.set_shown("outputs", false));
    REQUIRE(s.set_shown("mixer", false));
    g = s.layout(1400, 800, {});
    CHECK(place(g, "controller") == Rect{361, 0, 1400 - 361, 800});
    CHECK(g.lines.size() == 2);
}

TEST_CASE("The lines resize by share among panels, by length beside the Controller",
          "[app][dock]")
{
    auto s = DockState::defaults();

    // The line right of the left column: the column is 420 wide now.
    REQUIRE(s.resize_line("", 0, 1, 420, 1000));
    CHECK(s.root.children[0].extent == 420);
    // The one left of the right column: that column takes what is after it.
    REQUIRE(s.resize_line("", 1, 2, 700, 1000));
    CHECK(s.root.children[2].extent == 300);

    // The line under Service: it takes 70% of what it and Library share.
    const double total = s.find("service")->weight + s.find("library")->weight;
    REQUIRE(s.resize_line("0", 0, 1, 70, 100));
    CHECK_THAT(s.find("service")->weight, WithinAbs(total * 0.7, 1e-9));
    CHECK_THAT(s.find("library")->weight, WithinAbs(total * 0.3, 1e-9));

    CHECK_FALSE(s.resize_line("0", 1, 2, 10, 100));
    CHECK_FALSE(s.resize_line("7", 0, 1, 10, 100));
    CHECK_FALSE(s.resize_line("0.1", 0, 1, 10, 100));

    // Two panels beside the Controller both keep a length.
    REQUIRE(s.move("mixer", "controller", Edge::Left));
    CHECK(s.describe() == "row(column(service,library),mixer,controller,outputs)");
    REQUIRE(s.resize_line("", 0, 1, 250, 550));
    CHECK(s.root.children[0].extent == 250);
    CHECK(s.root.children[1].extent == 300);

    // Short of room, the panels give way alike and the Controller keeps its
    // least.
    const auto g = s.layout(900, 600, {});
    const int least = DockState::Metrics{}.controller_min_width;
    CHECK(place(g, "controller").width >= least);
    CHECK(place(g, "controller").width < least + 4);
    CHECK(place(g, "mixer").width < 300);

    // Two wells ask for more, and the panels give way further.
    DockState::Metrics wide;
    wide.controller_min_width = 760;
    const auto g2 = s.layout(1200, 600, wide);
    CHECK(place(g2, "controller").width >= 760);
    CHECK(place(g2, "mixer").width < place(g, "mixer").width);
    // With room to spare, the panels keep their lengths.
    const auto roomy = s.layout(1800, 600, wide);
    CHECK(place(roomy, "mixer").width == 300);
}

TEST_CASE("A collapsed panel is its title bar: a bar in a column, a strip in a row",
          "[app][dock]")
{
    auto s = DockState::defaults();
    REQUIRE(s.set_collapsed("service", true));
    CHECK_FALSE(s.set_collapsed("service", true));
    CHECK_FALSE(s.set_collapsed("controller", true));
    CHECK(s.describe()
          == "row(column(service:collapsed,library),controller,column(outputs,mixer))");
    auto g = s.layout(1400, 800, {});
    CHECK(place(g, "service") == Rect{0, 0, 360, 36});
    CHECK_FALSE(g.find("service")->strip);
    CHECK(place(g, "library") == Rect{0, 37, 360, 800 - 37});
    // The line between a collapsed panel and the next one resizes nothing.
    const auto under = std::ranges::find(g.lines, std::string("0"),
                                         &DockState::Geometry::Line::path);
    REQUIRE(under != g.lines.end());
    CHECK_FALSE(under->resizable);
    CHECK_FALSE(s.resize_line("0", 0, 1, 70, 100));

    // Beside the Controller it stands on end.
    REQUIRE(s.move("service", "controller", Edge::Left));
    CHECK(s.find("service")->collapsed);
    g = s.layout(1400, 800, {});
    CHECK(place(g, "service") == Rect{361, 0, 36, 800});
    CHECK(g.find("service")->strip);
    REQUIRE(s.set_collapsed("service", false));
    g = s.layout(1400, 800, {});
    CHECK(place(g, "service").width == 300);
}

TEST_CASE("A saved layout reads back, and a damaged one as far as it makes sense",
          "[app][dock]")
{
    auto s = DockState::defaults();
    REQUIRE(s.move("outputs", "controller", Edge::Top));
    REQUIRE(s.set_shown("library", false));
    REQUIRE(s.set_collapsed("service", true));
    REQUIRE(s.resize_line("", 0, 1, 400, 1000));
    CHECK(DockState::from_json(s.to_json()) == s);

    // Unknown and repeated panels go, a missing one runs along the right
    // edge, a bad weight is 1, and a split left with one child is it.
    const auto j = nlohmann::json::parse(R"({"root": {
        "axis": "row", "children": [
            {"axis": "column", "extent": 400, "children": [
                {"id": "outputs", "weight": -2},
                {"id": "teleprompter"}]},
            {"id": "controller", "shown": false},
            {"axis": "column", "children": [
                {"id": "outputs"},
                {"id": "service", "shown": false},
                {"id": "library", "collapsed": true}]}]}})");
    const auto read = DockState::from_json(j);
    CHECK(read.describe()
          == "row(outputs,controller,column(service:hidden,library:collapsed),mixer)");
    CHECK_THAT(read.find("outputs")->weight, WithinAbs(1.0, 1e-9));
    CHECK(read.find("outputs")->extent == 400);
    CHECK(read.find("controller")->shown);

    CHECK(DockState::from_json(nlohmann::json("nonsense")) == DockState::defaults());
    // Without the Controller it is no layout.
    CHECK(DockState::from_json(nlohmann::json::parse(R"({"root": {"id": "service"}})"))
          == DockState::defaults());
}

TEST_CASE("The dock layout is kept on this computer as it changes", "[app][dock]")
{
    forget_layout();
    {
        DockLayout layout;
        CHECK(layout.state().describe() == k_default);
        QSignalSpy places(&layout, &DockLayout::placesChanged);
        QSignalSpy sizes(&layout, &DockLayout::sizesChanged);
        QSignalSpy changed(&layout, &DockLayout::changed);
        layout.movePanel(QStringLiteral("outputs"), QStringLiteral("controller"),
                         QStringLiteral("top"));
        layout.setShown(QStringLiteral("service"), false);
        CHECK(places.size() == 2);
        // Collapsing changes what is drawn where, not the sizes kept.
        layout.setCollapsed(QStringLiteral("library"), true);
        CHECK(places.size() == 3);
        CHECK(sizes.size() == 0);
        // A resize says so apart from where the panels are.
        layout.resizeLine(QStringLiteral(""), 0, 1, 300, 1000);
        CHECK(places.size() == 3);
        CHECK(sizes.size() == 1);
        CHECK(changed.size() == 4);
        CHECK(layout.revision() == 4);
        layout.setShown(QStringLiteral("service"), false);  // no change, no signal
        CHECK(places.size() == 3);
        // Edit Mode is not part of the layout.
        layout.setEditMode(true);
        CHECK(changed.size() == 4);

        // What QML is handed: the places, and the lines by key.
        const auto g = layout.geometry(1400, 800, 36, 0);
        const auto placed = g.value(QStringLiteral("places")).toMap();
        CHECK(placed.contains(QStringLiteral("controller")));
        CHECK(placed.contains(QStringLiteral("outputs")));
        CHECK_FALSE(placed.contains(QStringLiteral("service")));
        const auto lines = g.value(QStringLiteral("lines")).toMap();
        CHECK(lines.size() == layout.lines().size());
        for (const auto& line : layout.lines()) {
            CHECK(lines.contains(line.toMap().value(QStringLiteral("key")).toString()));
        }
    }
    {
        DockLayout again;
        CHECK(again.state().describe()
              == "row(column(service:hidden,library:collapsed),column(outputs,controller),"
                 "mixer)");
        CHECK(again.hidden() == QStringList{"service"});
        CHECK(again.collapsed() == QStringList{"library"});
        CHECK(again.state().root.children[0].extent == 300);
        CHECK_FALSE(again.editMode());
        again.reset();
        CHECK(again.state().describe() == k_default);
    }
    {
        DockLayout after_reset;
        CHECK(after_reset.state() == DockState::defaults());
    }
    forget_layout();
}
