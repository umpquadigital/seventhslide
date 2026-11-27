// Roles on LineMapModel and ServiceItemModel that nothing else asserts.
//
// The facade tests reach most roles in passing, but four carry real derivation
// and were never checked: isVisible / isNext (which drive the operator's line
// list highlighting and are computed from the slide maps, not stored), and
// stackId / stackIcon (a three-step fallback that decides which tab icon a
// rundown row shows). These same models are what the web remote serialises, so
// a wrong role here is wrong in two places.

#include "test_qt_app.hpp"

#include <catch2/catch_test_macros.hpp>

#include "display_list_model.hpp"
#include "library_controller.hpp"
#include "library_models.hpp"
#include "line_map_model.hpp"
#include "service_list_model.hpp"
#include "service_item_model.hpp"
#include "facade_all.hpp"

#include <seventhslide/db/media_repository.hpp>
#include <seventhslide/model/content.hpp>
#include <seventhslide/model/output_config.hpp>
#include <seventhslide/session/session.hpp>
#include <seventhslide/sync/changed.hpp>

#include <nlohmann/json.hpp>

#include <QAbstractItemModel>
#include <QList>
#include <QSignalSpy>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <algorithm>
#include <string>
#include <vector>

using seventhslide::app::LineMapModel;
using seventhslide::app::ServiceItemModel;
using seventhslide::app::SessionFacade;
using seventhslide::model::OutputConfig;
using seventhslide::model::ServiceItem;
using seventhslide::model::Song;

namespace {

OutputConfig make_main()
{
    OutputConfig oc;
    oc.name = "Main";
    oc.canvas_width = 1920;
    oc.canvas_height = 1080;
    return oc;
}

Song demo_song()
{
    Song song;
    song.title = "Test Song";
    song.lyrics =
        "[V1]\nPraise to the Lord\nThe Almighty\n\n[V2]\nO my soul praise Him\nFor He is "
        "thy health";
    song.verse_order = "v1 v2";
    return song;
}

ServiceItem make_song_item(std::string title)
{
    ServiceItem item;
    item.item_type = "song";
    item.data = nlohmann::json{{"title", std::move(title)},
                               {"lyrics", "[V1]\nOne\nTwo"},
                               {"verse_order", "v1"},
                               {"theme_map", nlohmann::json::object()}};
    return item;
}

ServiceItem make_divider(std::string title)
{
    ServiceItem item;
    item.item_type = "divider";
    item.data = nlohmann::json{{"title", std::move(title)}};
    return item;
}

bool role_bool(const LineMapModel& model, int row, int role)
{
    return model.data(model.index(row), role).toBool();
}

} // namespace

// --- LineMapModel --------------------------------------------------------

TEST_CASE("LineMapModel: a slide step notifies only the rows whose flags moved", "[app][line_map]")
{
    // The remote and the operator's line list both react to dataChanged, so a
    // step used to re-push every row of a long song.
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    facade.set_outputs({make_main()});

    Song song;
    song.title = "Long";
    std::string lyrics = "[V1]\n";
    for (int i = 0; i < 60; ++i) {
        lyrics += "Line " + std::to_string(i) + "\n";
    }
    song.lyrics = lyrics;
    song.verse_order = "v1";
    facade.live()->play_song(std::move(song));

    auto* map = facade.program()->lines();
    const int rows = map->rowCount();
    REQUIRE(rows >= 20);

    int widest = 0;
    int notifications = 0;
    QObject::connect(map, &QAbstractItemModel::dataChanged,
                     [&](const QModelIndex& first, const QModelIndex& last, const QList<int>&) {
                         ++notifications;
                         widest = std::max(widest, last.row() - first.row() + 1);
                     });

    REQUIRE(facade.program()->nextSlide());
    REQUIRE(notifications >= 1);
    REQUIRE(widest > 0);
    REQUIRE(widest < rows);
}

TEST_CASE("LineMapModel: a reload of the same lines keeps the list where it is", "[app][line_map]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.set_outputs({make_main()});
    Song song;
    song.id = 1;
    song.title = "Long";
    song.lyrics = "[V1]\nOne\nTwo\nThree\n";
    song.verse_order = "v1";
    facade.live()->play_song(song);
    auto* map = facade.program()->lines();
    REQUIRE(map->rowCount() > 0);

    QSignalSpy resets(map, &QAbstractItemModel::modelReset);
    QSignalSpy changes(map, &QAbstractItemModel::dataChanged);
    // A first time from the lines last reset to, then as a sync elsewhere does.
    facade.reload_after_sync({});
    resets.clear();
    changes.clear();
    facade.reload_after_sync({});
    CHECK(resets.isEmpty());
    CHECK_FALSE(changes.isEmpty());

    song.lyrics = "[V1]\nOther\nWords\n";
    facade.live()->play_song(song);
    CHECK(resets.size() == 1);
}

TEST_CASE("LineMapModel: isVisible marks other lines on the live slide", "[app][line_map]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    facade.set_outputs({make_main()});
    facade.live()->play_song(demo_song());

    auto* map = facade.program()->lines();
    REQUIRE(map->rowCount() > 1);

    const auto& player = facade.session().focused_stack().program.player();

    // Rather than hard-code a layout, assert the definition: a row is visible
    // exactly when some output is currently showing the slide that
    // row belongs to - and never when it is the cursor itself, which is
    // "current", not "visible".
    for (int row = 0; row < map->rowCount(); ++row) {
        INFO("row " << row);
        bool on_a_live_slide = false;
        for (const auto& oc : player.outputs()) {
            if (oc.line_to_slide.empty()) {
                continue;
            }
            if (static_cast<std::size_t>(row) >= oc.line_to_slide.size()) {
                continue;
            }
            if (oc.line_to_slide[static_cast<std::size_t>(row)] == oc.index) {
                on_a_live_slide = true;
                break;
            }
        }
        const bool expected = on_a_live_slide && row != player.line_cursor();
        REQUIRE(role_bool(*map, row, LineMapModel::IsVisibleRole) == expected);
    }

    // The cursor row is current, and therefore never also visible.
    REQUIRE(role_bool(*map, player.line_cursor(), LineMapModel::IsCurrentRole));
    REQUIRE_FALSE(role_bool(*map, player.line_cursor(), LineMapModel::IsVisibleRole));
}

TEST_CASE("LineMapModel: isNext marks at most one row, never the cursor", "[app][line_map]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    facade.set_outputs({make_main()});
    facade.live()->play_song(demo_song());

    auto* map = facade.program()->lines();
    const auto& player = facade.session().focused_stack().program.player();

    const auto check = [&] {
        const auto next = player.find_next_change_line(player.line_cursor(), +1);
        int marked = 0;
        int marked_row = -1;
        for (int row = 0; row < map->rowCount(); ++row) {
            if (role_bool(*map, row, LineMapModel::IsNextRole)) {
                ++marked;
                marked_row = row;
            }
        }
        // Two rows both claiming to be "next" would light two entries in the
        // operator's list.
        REQUIRE(marked <= 1);
        if (next && *next != player.line_cursor()) {
            REQUIRE(marked == 1);
            REQUIRE(marked_row == *next);
        } else {
            REQUIRE(marked == 0);
        }
        REQUIRE_FALSE(role_bool(*map, player.line_cursor(), LineMapModel::IsNextRole));
    };

    check();
    while (facade.program()->nextSlide()) {
        check();
    }
    // At the end of the song there is no next change line left to mark.
    check();
}

TEST_CASE("LineMapModel: long labels are truncated for the list column", "[app][line_map]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    facade.set_outputs({make_main()});

    // Short verse codes pass through; a label over five characters is cut to
    // three plus an ellipsis so the label gutter cannot widen.
    facade.live()->play_song(demo_song());
    auto* map = facade.program()->lines();
    for (int row = 0; row < map->rowCount(); ++row) {
        const QString label =
            map->data(map->index(row), LineMapModel::LabelRole).toString();
        INFO("row " << row << " label " << label.toStdString());
        REQUIRE(label.size() <= 5);
        if (label.endsWith(QStringLiteral(".."))) {
            REQUIRE(label.size() == 5);
        }
    }
}

TEST_CASE("LineMapModel: rows show markup as text, never as raw tags or entities", "[app][line_map]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    facade.set_outputs({make_main()});

    Song song;
    song.title = "Rock & Roll";
    song.lyrics = "---[Verse:1]---\n<b>I</b> <3 you\nsay &lt;b> & a < b";
    song.verse_order = "";  // title slide first
    facade.live()->play_song(song);

    auto* map = facade.program()->lines();
    QStringList text;
    QStringList rich;
    for (int row = 0; row < map->rowCount(); ++row) {
        text << map->data(map->index(row), LineMapModel::TextRole).toString();
        rich << map->data(map->index(row), LineMapModel::RichTextRole).toString();
    }
    INFO(text.join(QStringLiteral(" | ")).toStdString());
    REQUIRE(text.contains(QStringLiteral("Rock & Roll")));
    REQUIRE(text.contains(QStringLiteral("I <3 you")));
    REQUIRE(text.contains(QStringLiteral("say <b> & a < b")));
    // StyledText gets escaped text with only its real formatting.
    REQUIRE(rich.contains(QStringLiteral("Rock &amp; Roll")));
    REQUIRE(rich.contains(QStringLiteral("<b>I</b> &lt;3 you")));
    REQUIRE(rich.contains(QStringLiteral("say &lt;b&gt; &amp; a &lt; b")));
}

TEST_CASE("LineMapModel: image mode numbers the playlist", "[app][line_map]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    using seventhslide::db::MediaKind;
    facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Image, "one.png", "One");
    facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Image, "two.png", "Two");
    facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Image, "three.png", "Three");
    const auto folder = facade.media_repo()->folders(MediaKind::Image).create("Pack");
    (void)facade.media_repo()->add_folder_item(MediaKind::Image, folder, "one.png");
    (void)facade.media_repo()->add_folder_item(MediaKind::Image, folder, "two.png");
    (void)facade.media_repo()->add_folder_item(MediaKind::Image, folder, "three.png");
    facade.refresh_library();

    REQUIRE(facade.media()->playImageFolder(static_cast<qint64>(folder), 1));
    REQUIRE(facade.program()->imageMode());

    auto* map = facade.program()->lines();
    REQUIRE(map->rowCount() == 3);

    // Text is the filename; the label is a 1-based position, not a verse code.
    REQUIRE(map->data(map->index(0), LineMapModel::TextRole).toString()
            == QStringLiteral("one.png"));
    REQUIRE(map->data(map->index(0), LineMapModel::LabelRole).toString()
            == QStringLiteral("1"));
    REQUIRE(map->data(map->index(2), LineMapModel::LabelRole).toString()
            == QStringLiteral("3"));

    // Current follows the playlist index, and next is simply the one after it.
    REQUIRE_FALSE(role_bool(*map, 0, LineMapModel::IsCurrentRole));
    REQUIRE(role_bool(*map, 1, LineMapModel::IsCurrentRole));
    REQUIRE(role_bool(*map, 2, LineMapModel::IsNextRole));
    REQUIRE_FALSE(role_bool(*map, 1, LineMapModel::IsNextRole));

    // Only one image is on screen at a time, so nothing is ever "also visible".
    for (int row = 0; row < map->rowCount(); ++row) {
        INFO("row " << row);
        REQUIRE_FALSE(role_bool(*map, row, LineMapModel::IsVisibleRole));
    }
}

TEST_CASE("LineMapModel: an out-of-range row yields nothing", "[app][line_map]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    facade.set_outputs({make_main()});
    facade.live()->play_song(demo_song());

    auto* map = facade.program()->lines();
    REQUIRE_FALSE(map->data(map->index(map->rowCount() + 5), LineMapModel::TextRole)
                      .isValid());
    REQUIRE_FALSE(map->data(QModelIndex(), LineMapModel::TextRole).isValid());
    // A child index has no rows beneath it - this is a flat list.
    REQUIRE(map->rowCount(map->index(0)) == 0);
}

// --- ServiceItemModel ----------------------------------------------------

TEST_CASE("ServiceItemModel: selecting a row does not reset the model", "[app][service_items]")
{
    // A reset rebuilds every QML delegate and can jump the operator's scroll
    // position, so it is reserved for a rundown whose rows actually changed.
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    facade.set_outputs({make_main()});

    std::vector<ServiceItem> items;
    items.push_back(make_song_item("First"));
    items.push_back(make_song_item("Second"));
    facade.library()->open_rundown(1, nlohmann::json::object(), std::move(items));

    auto* model = facade.serviceItems();
    REQUIRE(model->rowCount() == 2);
    REQUIRE(model->data(model->index(1), ServiceItemModel::DisplayNumberRole).toInt() == 2);

    int resets = 0;
    int changes = 0;
    QObject::connect(model, &QAbstractItemModel::modelReset, [&] { ++resets; });
    QObject::connect(model, &QAbstractItemModel::dataChanged, [&] { ++changes; });

    REQUIRE(facade.live()->selectServiceItem(1));
    REQUIRE(resets == 0);
    REQUIRE(changes >= 1);
    REQUIRE(model->data(model->index(1), ServiceItemModel::SelectedRole).toBool());

    // A different set of rows still resets.
    std::vector<ServiceItem> shorter;
    shorter.push_back(make_song_item("First"));
    facade.library()->open_rundown(2, nlohmann::json::object(), std::move(shorter));
    REQUIRE(resets >= 1);
    REQUIRE(model->rowCount() == 1);
}

TEST_CASE("ServiceItemModel: stackId is empty until a row is pinned", "[app][service_items]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    facade.set_outputs({make_main()});

    std::vector<ServiceItem> items;
    items.push_back(make_song_item("First"));
    facade.library()->open_rundown(1, nlohmann::json::object(), std::move(items));

    auto* model = facade.serviceItems();
    REQUIRE(model->rowCount() == 1);

    // No explicit pin: stackId is blank, and the icon falls through to the
    // type default's stack.
    REQUIRE(model->data(model->index(0), ServiceItemModel::StackIdRole).toString()
            == QString());
    REQUIRE(model->data(model->index(0), ServiceItemModel::StackIconRole).toString()
            == QStringLiteral("display"));
}

TEST_CASE("ServiceItemModel: stackIcon follows the type default", "[app][service_items]")
{
    // Second step of the fallback: no per-item pin, so the icon comes from
    // whichever stack this content type is routed to.
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    REQUIRE(facade.stacks()->addStack(QStringLiteral("Lobby"), QStringLiteral("info"))
                .isEmpty());
    const QString lobby =
        facade.stacks()->stacks().back().toMap().value(QStringLiteral("id")).toString();
    REQUIRE(lobby != QStringLiteral("main"));

    std::vector<ServiceItem> items;
    items.push_back(make_song_item("First"));
    facade.library()->open_rundown(1, nlohmann::json::object(), std::move(items));

    auto* model = facade.serviceItems();
    REQUIRE(model->data(model->index(0), ServiceItemModel::StackIconRole).toString()
            == QStringLiteral("display"));

    REQUIRE(facade.stacks()->setStackTypeDefault(QStringLiteral("song"), lobby).isEmpty());
    model->reload();
    REQUIRE(model->data(model->index(0), ServiceItemModel::StackIconRole).toString()
            == QStringLiteral("info"));

    // Still no per-item pin - the icon moved without the row being touched.
    REQUIRE(model->data(model->index(0), ServiceItemModel::StackIdRole).toString()
            == QString());
}

TEST_CASE("ServiceItemModel: a pinned stack wins over the type default", "[app][service_items]")
{
    // First step of the fallback, and the one an operator sets by hand.
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    facade.set_outputs({make_main()});

    REQUIRE(facade.stacks()->addStack(QStringLiteral("Lower Third"), QStringLiteral("titles"))
                .isEmpty());
    const QString lower =
        facade.stacks()->stacks().back().toMap().value(QStringLiteral("id")).toString();

    ServiceItem pinned = make_song_item("Pinned");
    pinned.data["stack_id"] = lower.toStdString();

    std::vector<ServiceItem> items;
    items.push_back(pinned);
    items.push_back(make_song_item("Unpinned"));
    facade.library()->open_rundown(1, nlohmann::json::object(), std::move(items));

    auto* model = facade.serviceItems();
    REQUIRE(model->data(model->index(0), ServiceItemModel::StackIdRole).toString()
            == lower);
    REQUIRE(model->data(model->index(0), ServiceItemModel::StackIconRole).toString()
            == QStringLiteral("titles"));

    // The neighbouring row is unaffected.
    REQUIRE(model->data(model->index(1), ServiceItemModel::StackIdRole).toString()
            == QString());
    REQUIRE(model->data(model->index(1), ServiceItemModel::StackIconRole).toString()
            == QStringLiteral("display"));
}

TEST_CASE("ServiceItemModel: a pin at a deleted stack falls back", "[app][service_items]")
{
    // Third step: the row names a stack that no longer exists. Returning its id
    // is fine, but the icon must resolve to something drawable rather than an
    // empty string that renders as a blank tab.
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    facade.set_outputs({make_main()});

    ServiceItem orphan = make_song_item("Orphan");
    orphan.data["stack_id"] = "deleted-stack-id";

    std::vector<ServiceItem> items;
    items.push_back(orphan);
    facade.library()->open_rundown(1, nlohmann::json::object(), std::move(items));

    auto* model = facade.serviceItems();
    REQUIRE(model->data(model->index(0), ServiceItemModel::StackIdRole).toString()
            == QStringLiteral("deleted-stack-id"));

    const QString icon =
        model->data(model->index(0), ServiceItemModel::StackIconRole).toString();
    REQUIRE_FALSE(icon.isEmpty());
    REQUIRE(seventhslide::model::is_known_stack_icon(icon.toStdString()));
}

TEST_CASE("ServiceItemModel: armed send-to does not change rundown icons", "[app][service_items]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    REQUIRE(facade.stacks()->addStack(QStringLiteral("Lobby"), QStringLiteral("info"))
                .isEmpty());
    const QString lobby =
        facade.stacks()->stacks().back().toMap().value(QStringLiteral("id")).toString();

    std::vector<ServiceItem> items;
    items.push_back(make_song_item("First"));
    facade.library()->open_rundown(1, nlohmann::json::object(), std::move(items));

    auto* model = facade.serviceItems();
    REQUIRE(model->data(model->index(0), ServiceItemModel::StackIconRole).toString()
            == QStringLiteral("display"));

    facade.stacks()->armSendTo(lobby);
    model->reload();
    REQUIRE(model->data(model->index(0), ServiceItemModel::StackIconRole).toString()
            == QStringLiteral("display"));
    REQUIRE(facade.session().resolve_target_stack("song", "") == lobby.toStdString());
    REQUIRE(facade.live()->resolvePlannedStack(QStringLiteral("song")) != lobby);
}

TEST_CASE("ServiceItemModel: alsoPlayIds and alsoPlayWith", "[app][service_items]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    facade.set_outputs({make_main()});

    ServiceItem parent = make_song_item("Hymn");
    parent.item_id = 1;
    seventhslide::model::set_service_item_also_play(parent, {2});

    ServiceItem child = make_song_item("Loop");
    child.item_id = 2;
    child.item_type = "image";
    child.data = nlohmann::json{{"filename", "loop.jpg"}};

    std::vector<ServiceItem> items;
    items.push_back(parent);
    items.push_back(child);
    facade.library()->open_rundown(1, nlohmann::json::object(), std::move(items));

    auto* model = facade.serviceItems();
    const QVariantList ids =
        model->data(model->index(0), ServiceItemModel::AlsoPlayIdsRole).toList();
    REQUIRE(ids.size() == 1);
    REQUIRE(ids.at(0).toLongLong() == 2);
    REQUIRE(model->data(model->index(0), ServiceItemModel::AlsoPlayWithRole).toString()
            .isEmpty());
    REQUIRE(model->data(model->index(1), ServiceItemModel::AlsoPlayWithRole).toString()
            == QStringLiteral("Hymn"));
}

TEST_CASE("ServiceItemModel: display numbers skip dividers", "[app][service_items]")
{
    // Dividers are section headers, so they take no number and must not
    // consume one - the rundown's visible numbering has to stay 1..n.
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    facade.set_outputs({make_main()});

    std::vector<ServiceItem> items;
    items.push_back(make_divider("Welcome"));
    items.push_back(make_song_item("One"));
    items.push_back(make_song_item("Two"));
    items.push_back(make_divider("Message"));
    items.push_back(make_song_item("Three"));
    facade.library()->open_rundown(1, nlohmann::json::object(), std::move(items));

    auto* model = facade.serviceItems();
    REQUIRE(model->rowCount() == 5);

    const auto number = [&](int row) {
        return model->data(model->index(row), ServiceItemModel::DisplayNumberRole).toInt();
    };
    REQUIRE(number(0) == 0);  // divider
    REQUIRE(number(1) == 1);
    REQUIRE(number(2) == 2);
    REQUIRE(number(3) == 0);  // divider
    REQUIRE(number(4) == 3);  // numbering continues across the divider
}

TEST_CASE("ServiceItemModel: folderImages is empty for non-folder rows", "[app][service_items]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    facade.set_outputs({make_main()});

    ServiceItem folder;
    folder.item_type = seventhslide::model::k_item_type_image_folder;
    folder.data = nlohmann::json{{"name", "Pack"},
                                 {"images", nlohmann::json::array({"a.png", "b.png"})}};

    std::vector<ServiceItem> items;
    items.push_back(make_song_item("A song"));
    items.push_back(folder);
    facade.library()->open_rundown(1, nlohmann::json::object(), std::move(items));

    auto* model = facade.serviceItems();
    REQUIRE(model->data(model->index(0), ServiceItemModel::FolderImagesRole)
                .toStringList()
                .isEmpty());

    const QStringList names =
        model->data(model->index(1), ServiceItemModel::FolderImagesRole).toStringList();
    REQUIRE(names.size() == 2);
    REQUIRE(names.at(0) == QStringLiteral("a.png"));
    REQUIRE(names.at(1) == QStringLiteral("b.png"));
}

TEST_CASE("Service list snapshots carry every role by its QML name", "[app][services]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const auto sid = facade.library()->createService(QStringLiteral("Sunday"));
    REQUIRE(sid >= 0);
    REQUIRE(facade.library()->setServiceTags(sid, {QStringLiteral("Advent")}).isEmpty());

    const auto services = facade.library()->services()->snapshot();
    REQUIRE(services.size() == 1);
    const auto s = services.front().toMap();
    CHECK(QSet<QString>(s.keyBegin(), s.keyEnd())
          == QSet<QString>{QStringLiteral("serviceId"), QStringLiteral("name"), QStringLiteral("isOpen"),
                           QStringLiteral("tags"), QStringLiteral("createdAt"),
                           QStringLiteral("serviceAt")});
    CHECK(s.value(QStringLiteral("name")).toString() == QStringLiteral("Sunday"));
    CHECK(s.value(QStringLiteral("tags")).toStringList() == QStringList{QStringLiteral("Advent")});
    CHECK(s.value(QStringLiteral("isOpen")).typeId() == QMetaType::Bool);
}

TEST_CASE("DisplayListModel names a display by label, then name, then key", "[app][displays]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::app::DisplayListModel model;
    const QVariantMap bounds{{QStringLiteral("x"), 0}, {QStringLiteral("w"), 800}};
    model.set_displays({
        QVariantMap{{QStringLiteral("key"), QStringLiteral("k1")}},
        QVariantMap{{QStringLiteral("key"), QStringLiteral("k2")}, {QStringLiteral("name"), QStringLiteral("Stage")}},
        QVariantMap{{QStringLiteral("key"), QStringLiteral("k3")}, {QStringLiteral("name"), QStringLiteral("Hall")},
                    {QStringLiteral("label"), QStringLiteral("Hall (left)")}, {QStringLiteral("primary"), true},
                    {QStringLiteral("assignedOutput"), QStringLiteral("Main")}, {QStringLiteral("bounds"), bounds}},
    });
    using M = seventhslide::app::DisplayListModel;
    const auto at = [&model](int row, int role) { return model.data(model.index(row), role); };
    REQUIRE(model.rowCount() == 3);
    CHECK(at(0, M::NameRole).toString() == QStringLiteral("k1"));
    CHECK(at(0, M::LabelRole).toString() == QStringLiteral("k1"));
    CHECK(at(1, M::NameRole).toString() == QStringLiteral("Stage"));
    CHECK(at(1, M::LabelRole).toString() == QStringLiteral("Stage"));
    CHECK(at(2, M::LabelRole).toString() == QStringLiteral("Hall (left)"));
    CHECK(at(2, M::KeyRole).toString() == QStringLiteral("k3"));
    // Every role is text, so a delegate's required string properties always
    // bind: a missing assigned output reads as "".
    CHECK(at(0, M::AssignedOutputRole).toString().isEmpty());
    for (const int role : {M::KeyRole, M::NameRole, M::LabelRole, M::AssignedOutputRole}) {
        CHECK(at(0, role).typeId() == QMetaType::QString);
    }
    CHECK(at(2, M::AssignedOutputRole).toString() == QStringLiteral("Main"));
    CHECK_FALSE(model.data(model.index(3), M::NameRole).isValid());
}

TEST_CASE("BibleListModel answers nameAt and idAt only for its rows", "[app][bibles]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::app::BibleListModel model;
    seventhslide::model::Bible kjv;
    kjv.id = 4;
    kjv.name = "KJV";
    seventhslide::model::Bible unsaved;
    unsaved.name = "Draft";
    model.set_bibles({kjv, unsaved});
    CHECK(model.nameAt(0) == QStringLiteral("KJV"));
    CHECK(model.idAt(0) == 4);
    CHECK(model.nameAt(1) == QStringLiteral("Draft"));
    CHECK(model.idAt(1) == -1);  // no id yet
    for (const int row : {-1, 2}) {
        CHECK(model.nameAt(row).isEmpty());
        CHECK(model.idAt(row) == -1);
    }
}
