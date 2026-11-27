// Rundown edits change the rundown only. Adding, removing, reordering or
// editing service items must never send anything live, move the operator's
// place, or disturb another stack. Only the edited item itself, when it is the
// one on screen, is re-shown in place with its new content.

#include <catch2/catch_test_macros.hpp>

#include "facade_all.hpp"
#include "test_qt_app.hpp"

#include <seventhslide/model/content.hpp>

using seventhslide::app::ServiceItemModel;
using seventhslide::app::SessionFacade;

namespace {

constexpr const char* k_lyrics =
    "[V1]\nLine one\nLine two\nLine three\n[V2]\nLine four\nLine five\nLine six";

std::int64_t add_library_song(SessionFacade& facade, const char* title)
{
    seventhslide::model::Song song;
    song.title = title;
    song.lyrics = k_lyrics;
    song.verse_order = "v1 v2";
    return facade.song_repo()->insert(song);
}

qint64 item_id_at(SessionFacade& facade, int row)
{
    return facade.serviceItems()
        ->data(facade.serviceItems()->index(row), ServiceItemModel::ItemIdRole)
        .toLongLong();
}

/// A service with songs Alpha and Beta; Alpha is live at line 2.
struct LiveService {
    // Declared before facade: members are built in declaration order, and the
    // facade's text measurer needs the QGuiApplication to exist already.
    struct QtApp {
        QtApp() { seventhslide::test::ensure_qt_application(); }
    } qt_app;
    SessionFacade facade;
    qint64 alpha_id = -1;
    qint64 beta_id = -1;
    std::int64_t gamma_song = -1;

    LiveService()
    {
        REQUIRE(facade.open_memory_database());
        const auto alpha = add_library_song(facade, "Alpha");
        const auto beta = add_library_song(facade, "Beta");
        gamma_song = add_library_song(facade, "Gamma");
        REQUIRE(facade.library()->createService(QStringLiteral("Sunday")) > 0);
        REQUIRE(facade.library()->addSongToService(static_cast<qint64>(alpha)));
        REQUIRE(facade.library()->addSongToService(static_cast<qint64>(beta)));
        alpha_id = item_id_at(facade, 0);
        beta_id = item_id_at(facade, 1);

        REQUIRE(facade.live()->selectServiceItem(0));
        REQUIRE(facade.program()->jumpToLine(2));
        REQUIRE(facade.live()->current_title() == QStringLiteral("Alpha"));
        REQUIRE(facade.program()->lineCursor() == 2);
    }

    void require_alpha_still_live(int expected_row)
    {
        REQUIRE(facade.live()->current_title() == QStringLiteral("Alpha"));
        REQUIRE(facade.program()->lineCursor() == 2);
        REQUIRE(facade.live()->service_item_index() == expected_row);
        REQUIRE(item_id_at(facade, expected_row) == alpha_id);
    }
};

}  // namespace

TEST_CASE("Adding to a service with nothing live sends nothing live", "[app][rundown]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const auto song = add_library_song(facade, "Alpha");
    REQUIRE(facade.library()->createService(QStringLiteral("Sunday")) > 0);

    REQUIRE(facade.library()->addSongToService(static_cast<qint64>(song)));
    REQUIRE(facade.library()->addDividerToService(QStringLiteral("Word")));
    REQUIRE(facade.serviceItems()->rowCount() == 2);
    REQUIRE(facade.live()->service_item_index() == -1);
    REQUIRE(facade.live()->current_title().isEmpty());
    REQUIRE(facade.stacks()->occupiedStacks().isEmpty());
}

TEST_CASE("Rundown edits leave the live item where it is", "[app][rundown]")
{
    LiveService s;
    auto& facade = s.facade;

    SECTION("append")
    {
        REQUIRE(facade.library()->addSongToService(static_cast<qint64>(s.gamma_song)));
        REQUIRE(facade.serviceItems()->rowCount() == 3);
        s.require_alpha_still_live(0);
    }
    SECTION("insert above the live row shifts its index, not what is live")
    {
        REQUIRE(facade.library()->addSongToService(static_cast<qint64>(s.gamma_song), 0));
        s.require_alpha_still_live(1);
    }
    SECTION("divider, image and video adds")
    {
        REQUIRE(facade.library()->addDividerToService(QStringLiteral("Word"), 0));
        REQUIRE(facade.media()->addImageToService(QStringLiteral("slide.png")));
        REQUIRE(facade.media()->addVideoToService(QStringLiteral("clip.mp4")));
        REQUIRE_FALSE(facade.program()->videoMode());
        s.require_alpha_still_live(1);
    }
    SECTION("reorder moves the highlight with the item")
    {
        REQUIRE(facade.library()->moveServiceItemTo(0, 1));
        s.require_alpha_still_live(1);
        REQUIRE(facade.library()->reorderServiceItems({s.alpha_id, s.beta_id}));
        s.require_alpha_still_live(0);
    }
    SECTION("removing another item")
    {
        REQUIRE(facade.library()->remove_service_item(s.beta_id));
        s.require_alpha_still_live(0);
    }
    SECTION("removing the live item keeps it on screen and clears the highlight")
    {
        REQUIRE(facade.library()->remove_service_item(s.alpha_id));
        REQUIRE(facade.live()->current_title() == QStringLiteral("Alpha"));
        REQUIRE(facade.program()->lineCursor() == 2);
        REQUIRE(facade.live()->service_item_index() == -1);
    }
    SECTION("renaming a divider")
    {
        REQUIRE(facade.library()->addDividerToService(QStringLiteral("Word")));
        REQUIRE(facade.library()->renameServiceDivider(item_id_at(facade, 2), QStringLiteral("Sermon"))
                    .isEmpty());
        s.require_alpha_still_live(0);
    }
    SECTION("service theme overrides")
    {
        REQUIRE(facade.library()->setServiceThemeMap(facade.openServiceId(), QVariantMap{}).isEmpty());
        s.require_alpha_still_live(0);
    }
    SECTION("editing a different song item")
    {
        REQUIRE(facade.library()
                    ->updateServiceSongItem(s.beta_id, QStringLiteral("Beta 2"),
                                           QString::fromUtf8(k_lyrics),
                                           QStringLiteral("v1 v2"))
                    .isEmpty());
        s.require_alpha_still_live(0);
    }
}

TEST_CASE("Editing the live song re-shows it in place", "[app][rundown]")
{
    LiveService s;
    auto& facade = s.facade;

    REQUIRE(facade.library()
                ->updateServiceSongItem(s.alpha_id, QStringLiteral("Alpha (new)"),
                                       QString::fromUtf8(k_lyrics), QStringLiteral("v1 v2"))
                .isEmpty());
    REQUIRE(facade.live()->current_title() == QStringLiteral("Alpha (new)"));
    REQUIRE(facade.program()->lineCursor() == 2);
    REQUIRE(facade.live()->service_item_index() == 0);

    // Shorter lyrics: the cursor clamps to the new last line.
    REQUIRE(facade.library()
                ->updateServiceSongItem(s.alpha_id, QStringLiteral("Alpha (short)"),
                                       QStringLiteral("[V1]\nOnly line"), QStringLiteral("v1"))
                .isEmpty());
    REQUIRE(facade.live()->current_title() == QStringLiteral("Alpha (short)"));
    REQUIRE(facade.program()->lineCursor() == facade.program()->totalLines() - 1);
}

TEST_CASE("Rundown edits leave other stacks alone", "[app][rundown][stacks]")
{
    LiveService s;
    auto& facade = s.facade;

    REQUIRE(facade.stacks()->addStack(QStringLiteral("Lobby"), QStringLiteral("info")).isEmpty());
    const auto lobby = facade.stacks()->stacks().back().toMap().value(QStringLiteral("id")).toString();
    REQUIRE(facade.stacks()->setStackTypeDefault(QStringLiteral("bible"), lobby).isEmpty());
    const auto bible = facade.bible_repo()->import_bible(
        "KJV", "",
        {{.book = "John", .chapter = 3, .verse_num = 16, .text = "For God so loved"}});
    REQUIRE(facade.live()->play_bible_ref(bible, QStringLiteral("John"), 3, 16, 16));
    REQUIRE(facade.stacks()->occupiedStacks().size() == 2);

    facade.stacks()->setFocusedStackId(QStringLiteral("main"));
    REQUIRE(facade.library()->addSongToService(static_cast<qint64>(s.gamma_song)));
    REQUIRE(facade.library()->moveServiceItemTo(2, 0));
    REQUIRE(facade.library()->remove_service_item(s.beta_id));
    REQUIRE(facade.stacks()->occupiedStacks().size() == 2);
}

TEST_CASE("Settings and theme changes keep the live line", "[app][rundown]")
{
    LiveService s;
    auto& facade = s.facade;

    QVariantMap fields;
    fields.insert(QStringLiteral("ccliLicenceNumber"), QStringLiteral("12345"));
    REQUIRE(facade.updateAppSettings(fields).isEmpty());
    s.require_alpha_still_live(0);

    REQUIRE(facade.outputSettings()->createOutput(QStringLiteral("Stream")).contains(QStringLiteral("name")));
    s.require_alpha_still_live(0);
}

TEST_CASE("Rows removed or pinned together change all of them or none", "[app][rundown]")
{
    LiveService s;
    auto& library = *s.facade.library();
    REQUIRE(library.addSongToService(static_cast<qint64>(s.gamma_song)));
    const qint64 gamma_id = item_id_at(s.facade, 2);
    const auto stack_of = [&](int row) {
        return s.facade.serviceItems()
            ->data(s.facade.serviceItems()->index(row), ServiceItemModel::StackIdRole)
            .toString();
    };

    // An id not in the service: nothing changes, and it says why.
    CHECK_FALSE(library.setServiceItemsStack({s.beta_id, 9999}, QStringLiteral("main")).isEmpty());
    CHECK(stack_of(1).isEmpty());
    CHECK_FALSE(library.removeServiceItems({s.beta_id, 9999}).isEmpty());
    CHECK(s.facade.serviceItems()->rowCount() == 3);

    CHECK(library.setServiceItemsStack({s.beta_id, gamma_id}, QStringLiteral("main")).isEmpty());
    CHECK(stack_of(1) == QStringLiteral("main"));
    CHECK(stack_of(2) == QStringLiteral("main"));
    CHECK(library.setServiceItemsStack({s.beta_id, gamma_id}, QString()).isEmpty());
    CHECK(stack_of(1).isEmpty());

    CHECK(library.removeServiceItems({s.beta_id, gamma_id}).isEmpty());
    CHECK(s.facade.serviceItems()->rowCount() == 1);
    s.require_alpha_still_live(0);
}
