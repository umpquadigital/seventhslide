// A stack's Preview through the facade: what the Controller's wells read, the
// take and Clear (StackController), this computer's Fade length, the
// cued service row, and a send straight past the Preview.

#include "session_facade_fixture.hpp"

#include <seventhslide/library/machine_store.hpp>

#include <QSignalSpy>

using namespace seventhslide::test::facade;
using seventhslide::app::ServiceItemModel;
using seventhslide::app::StackController;
using seventhslide::session::Land;

namespace {

/// The stack's entry in ``stacks.occupied``; empty while nothing is live or
/// cued on it.
[[nodiscard]] QVariantMap entry_of(SessionFacade& facade, const QString& id)
{
    for (const auto& row : facade.stacks()->occupiedStacks()) {
        if (row.toMap().value(QStringLiteral("id")).toString() == id) {
            return row.toMap();
        }
    }
    return {};
}

[[nodiscard]] QString main_id(SessionFacade& facade)
{
    return facade.stacks()->stacks().front().toMap().value(QStringLiteral("id")).toString();
}

}  // namespace

TEST_CASE("The Controller cues on a stack with Preview on and takes it live",
          "[app][preview]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    auto* stacks = facade.stacks();
    const auto main = main_id(facade);
    REQUIRE(stacks->setStackPreview(main, true).isEmpty());
    QSignalSpy live(stacks, &StackController::liveChanged);

    // Cued: the Preview's side has it, the Program's not; the stack is listed
    // as cued, and one stack is no reason for tabs.
    facade.live()->play_song(demo_song());
    CHECK(facade.preview()->preview());
    CHECK(facade.preview()->occupied());
    CHECK(facade.preview()->totalLines() > 0);
    CHECK_FALSE(facade.program()->occupied());
    auto entry = entry_of(facade, main);
    CHECK(entry.value(QStringLiteral("cued")).toBool());
    CHECK_FALSE(entry.value(QStringLiteral("live")).toBool());
    CHECK(entry.value(QStringLiteral("preview")).toBool());
    CHECK_FALSE(stacks->showStackTabs());

    // Taken by the theme's transition.
    REQUIRE(stacks->transition(main).isEmpty());
    CHECK(live.count() == 1);
    CHECK(facade.program()->occupied());
    CHECK_FALSE(facade.preview()->occupied());
    entry = entry_of(facade, main);
    CHECK(entry.value(QStringLiteral("live")).toBool());
    CHECK_FALSE(entry.value(QStringLiteral("cued")).toBool());
    // Nothing cued now: refused, and nothing moves.
    CHECK_FALSE(stacks->transition(main).isEmpty());
    CHECK(live.count() == 1);

    // Taken by one of its own.
    facade.live()->play_song(demo_song());
    REQUIRE(stacks->transitionWith(main, QStringLiteral("fade_through_black"), 1234,
                                   QStringLiteral("right"))
                .isEmpty());
    CHECK(facade.session().transition_ms_on(main.toStdString()) == 1234);
    // Its easing as named; a name the model does not know is refused, not
    // taken as a cut.
    facade.live()->play_song(demo_song());
    REQUIRE(stacks->transitionWith(main, QStringLiteral("fade"), 400, QString(),
                                   QStringLiteral("linear"))
                .isEmpty());
    CHECK(facade.session().scene_frames().front().content_transition.easing
          == seventhslide::model::TransitionEasing::Linear);
    facade.live()->play_song(demo_song());
    CHECK_FALSE(stacks->transitionWith(main, QStringLiteral("swirl"), 400, QString()).isEmpty());
    CHECK_FALSE(stacks->transitionWith(main, QStringLiteral("push"), 400, QStringLiteral("inward"))
                    .isEmpty());
    CHECK(facade.preview()->occupied());

    // Cleared, a Preview drops its cue and leaves what is live.
    facade.live()->play_song(demo_song());
    REQUIRE(facade.preview()->occupied());
    REQUIRE(stacks->clearPreview(main).isEmpty());
    CHECK_FALSE(facade.preview()->occupied());
    CHECK(facade.program()->occupied());

    // Straight live, past the Preview.
    facade.live()->play_song(demo_song(), Land::Live);
    CHECK_FALSE(facade.preview()->occupied());

    // A stack without a Preview has none to clear.
    REQUIRE(stacks->setStackPreview(main, false).isEmpty());
    CHECK_FALSE(stacks->clearPreview(main).isEmpty());
    CHECK_FALSE(facade.preview()->occupied());
}

TEST_CASE("The take's Fade length is this computer's, kept as it is changed",
          "[app][preview][machine]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir tmp;
    {
        SessionFacade facade;
        REQUIRE(facade.init_libraries(tmp.path));
        auto* stacks = facade.stacks();
        CHECK(stacks->takeFadeMs() == 500);
        QSignalSpy changed(stacks, &StackController::takeFadeMsChanged);

        REQUIRE(stacks->setTakeFadeMs(1200).isEmpty());
        CHECK(stacks->takeFadeMs() == 1200);
        CHECK(changed.count() == 1);
        // The same again changes nothing.
        REQUIRE(stacks->setTakeFadeMs(1200).isEmpty());
        CHECK(changed.count() == 1);
        // Out of range, the nearest end of it.
        REQUIRE(stacks->setTakeFadeMs(0).isEmpty());
        CHECK(stacks->takeFadeMs() == seventhslide::model::k_min_take_fade_ms);
        REQUIRE(stacks->setTakeFadeMs(750).isEmpty());
    }
    SessionFacade restarted;
    REQUIRE(restarted.init_libraries(tmp.path));
    CHECK(restarted.stacks()->takeFadeMs() == 750);
}

TEST_CASE("The service list marks the row cued in the focused stack's Preview",
          "[app][preview][service]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    REQUIRE(facade.library()->createService(QStringLiteral("Svc")) > 0);
    using seventhslide::db::MediaKind;
    for (const char* file : {"a.png", "b.png", "c.png"}) {
        facade.media_repo()->upsert_file(MediaKind::Image, file, file);
    }
    const auto folder_id = facade.media_repo()->folders(MediaKind::Image).create("Pack");
    for (const char* file : {"a.png", "b.png", "c.png"}) {
        (void)facade.media_repo()->add_folder_item(MediaKind::Image, folder_id, file);
    }
    facade.refresh_library();
    REQUIRE(facade.media()->addImageFolderToService(static_cast<qint64>(folder_id)));
    REQUIRE(facade.stacks()->setStackPreview(facade.stacks()->focusedStackId(), true).isEmpty());

    auto* rows = facade.serviceItems();
    const auto role = [rows](int r) { return rows->data(rows->index(0), r).toBool(); };

    // Cued at its third picture: in the Preview, and marked so.
    REQUIRE(facade.live()->selectServiceItemAtImage(0, 2));
    CHECK(facade.preview()->lineCursor() == 2);
    CHECK_FALSE(facade.program()->occupied());
    CHECK(role(ServiceItemModel::CuedRole));
    CHECK_FALSE(role(ServiceItemModel::SelectedRole));

    // Taken: live, where it was cued.
    REQUIRE(facade.stacks()->transition(facade.stacks()->focusedStackId()).isEmpty());
    CHECK(facade.program()->lineCursor() == 2);
    CHECK(role(ServiceItemModel::SelectedRole));
    CHECK_FALSE(role(ServiceItemModel::CuedRole));
    CHECK(facade.live()->service_item_index() == 0);

    // Shift+double-click: straight live, past the Preview.
    REQUIRE(facade.live()->selectServiceItemAtImage(0, 1, /*live=*/true));
    CHECK(facade.program()->lineCursor() == 1);
    CHECK_FALSE(facade.preview()->occupied());

    // Cued again, then sent straight live at another picture: each side where
    // it was sent, the cued one left alone.
    REQUIRE(facade.live()->selectServiceItemAtImage(0, 1));
    REQUIRE(facade.live()->selectServiceItemAtImage(0, 2, /*live=*/true));
    CHECK(facade.program()->lineCursor() == 2);
    CHECK(facade.preview()->lineCursor() == 1);
}
