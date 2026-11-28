// Playing, outputs, destinations and stacks through the facade (playback, output and stack controllers).

#include "session_facade_fixture.hpp"
#include "freeform_picture_item.hpp"
#include "output_preview_item.hpp"
#include "overlay_text_item.hpp"
#include "wav_fixture.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <seventhslide/audio/audio_engine.hpp>
#include <seventhslide/model/output_library.hpp>
#include <seventhslide/render/output_window.hpp>
#include <seventhslide/render/presenter_view.hpp>
#include <seventhslide/render/scene_presenter.hpp>
#include <seventhslide/render/text_painter.hpp>

#include <QQuickWindow>
#include <QTest>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <thread>

namespace MediaKindQml = seventhslide::app::MediaKindQml;

using namespace seventhslide::test::facade;

TEST_CASE("SessionFacade video and preview notifications are their own signals", "[app][signals]")
{
    // Playback pulls a frame 30 times a second. That must not look like a
    // session change (every QML binding, every remote section), but the video
    // properties still have to follow ordinary mutations.
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.set_outputs({make_main()});

    int sessions = 0;
    int video_states = 0;
    int previews = 0;
    QObject::connect(&facade, &SessionFacade::sessionChanged, [&] { ++sessions; });
    QObject::connect(&facade, &SessionFacade::mediaStateChanged, [&] { ++video_states; });
    QObject::connect(&facade, &SessionFacade::previewRevisionChanged, [&] { ++previews; });

    facade.live()->play_song(demo_song());
    REQUIRE(sessions >= 1);
    // A mutation still refreshes the video properties and the preview URLs.
    REQUIRE(video_states >= sessions);
    REQUIRE(previews >= 1);
}

TEST_CASE("SessionFacade play_song populates output model", "[app]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    facade.set_outputs({make_main()});

    REQUIRE(facade.ready());
    REQUIRE(facade.outputs()->rowCount() == 1);
    REQUIRE(facade.outputs()->data(facade.outputs()->index(0),
                                    seventhslide::app::OutputListModel::NameRole)
                .toString()
            == QStringLiteral("Main"));

    facade.live()->play_song(demo_song());
    REQUIRE(facade.live()->mode() == QStringLiteral("song"));
    REQUIRE(facade.has_build());
    REQUIRE(facade.live()->current_title() == QStringLiteral("Test Song"));
    REQUIRE(facade.program()->totalLines() > 0);
    REQUIRE(facade.program()->lineCursor() == 0);
}

TEST_CASE("SessionFacade service select updates selected role", "[app]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    facade.set_outputs({make_main()});

    std::vector<ServiceItem> items;
    items.push_back(make_song_item("First", "[V1]\nOne\nTwo"));
    items.push_back(make_song_item("Second", "[V1]\nThree\nFour"));

    facade.library()->open_rundown(1, nlohmann::json::object(), std::move(items));
    REQUIRE(facade.serviceItems()->rowCount() == 2);
    REQUIRE(facade.live()->service_item_index() == -1);

    REQUIRE(facade.live()->selectServiceItem(0));
    REQUIRE(facade.live()->service_item_index() == 0);
    REQUIRE(facade.serviceItems()
                ->data(facade.serviceItems()->index(0),
                       seventhslide::app::ServiceItemModel::SelectedRole)
                .toBool());
    REQUIRE_FALSE(facade.serviceItems()
                       ->data(facade.serviceItems()->index(1),
                              seventhslide::app::ServiceItemModel::SelectedRole)
                       .toBool());
    REQUIRE(facade.live()->current_title() == QStringLiteral("First"));

    REQUIRE(facade.live()->selectServiceItem(1));
    REQUIRE(facade.live()->service_item_index() == 1);
    REQUIRE(facade.serviceItems()
                ->data(facade.serviceItems()->index(1),
                       seventhslide::app::ServiceItemModel::SelectedRole)
                .toBool());
    REQUIRE(facade.live()->current_title() == QStringLiteral("Second"));
}

TEST_CASE("SessionFacade live row follows the focused stack", "[app][stacks][alsoplay]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    REQUIRE(facade.stacks()->addStack(QStringLiteral("Lobby"), QStringLiteral("info")).isEmpty());
    const auto all = facade.stacks()->stacks();
    const auto main = all.front().toMap().value(QStringLiteral("id")).toString();
    const auto lobby = all.back().toMap().value(QStringLiteral("id")).toString();

    ServiceItem song = make_song_item("Hymn", "[V1]\nOne");
    song.item_id = 1;
    song.data["stack_id"] = main.toStdString();
    song.data["also_play"] = nlohmann::json::array({2});
    ServiceItem notice = make_song_item("Notice", "[V1]\nWelcome");
    notice.item_id = 2;
    notice.data["stack_id"] = lobby.toStdString();
    ServiceItem other = make_song_item("Other", "[V1]\nThree");
    other.item_id = 3;

    std::vector<ServiceItem> items{song, notice, other};
    facade.library()->open_rundown(1, nlohmann::json::object(), std::move(items));
    auto* model = facade.serviceItems();
    const auto selected = [&](int row) {
        return model->data(model->index(row), seventhslide::app::ServiceItemModel::SelectedRole)
            .toBool();
    };
    const auto link_set = [&](int row) {
        return model->data(model->index(row), seventhslide::app::ServiceItemModel::LinkSetRole)
            .toInt();
    };
    CHECK(link_set(0) == 0);
    CHECK(link_set(1) == 0);
    CHECK(link_set(2) == -1);

    // Going live on the companion takes the item that holds the link too,
    // but only the row on the focused stack shows as live.
    REQUIRE(facade.live()->selectServiceItem(1));
    REQUIRE(facade.stacks()->focusedStackId() == lobby);
    CHECK(selected(1));
    CHECK_FALSE(selected(0));

    facade.stacks()->setFocusedStackId(main);
    CHECK(selected(0));
    CHECK_FALSE(selected(1));
}

TEST_CASE("SessionFacade blank toggles refresh output roles", "[app]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    facade.set_outputs({make_main()});
    facade.live()->play_song(demo_song());

    const auto idx = facade.outputs()->index(0);
    REQUIRE_FALSE(
        facade.outputs()
            ->data(idx, seventhslide::app::OutputListModel::IsBlankRole)
            .toBool());

    facade.live()->toggleGlobalBlank();
    REQUIRE(facade.live()->global_blank());
    REQUIRE(facade.outputs()
                ->data(idx, seventhslide::app::OutputListModel::IsBlankRole)
                .toBool());

    REQUIRE(facade.live()->toggleOutputFreeze(QStringLiteral("out-main")));
    REQUIRE(facade.outputs()
                ->data(idx, seventhslide::app::OutputListModel::IsFrozenRole)
                .toBool());
}

// The output cards and the remote's Blank and Freeze show the outputs' state,
// not a copy held by whichever stack tab has focus.
TEST_CASE("SessionFacade shows output blank and freeze whichever stack has focus", "[app][outputs][stacks]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    REQUIRE(facade.stacks()->addStack(QStringLiteral("Lobby"), QStringLiteral("info")).isEmpty());
    const auto lobby = facade.stacks()->stacks().back().toMap().value(QStringLiteral("id")).toString();
    facade.live()->play_song(demo_song());

    facade.live()->toggleGlobalBlank();
    facade.live()->toggleGlobalFreeze();
    facade.stacks()->setFocusedStackId(lobby);
    REQUIRE(facade.stacks()->focusedStackId() == lobby);

    const auto idx = facade.outputs()->index(0);
    CHECK(facade.outputs()->data(idx, seventhslide::app::OutputListModel::IsBlankRole).toBool());
    CHECK(facade.outputs()->data(idx, seventhslide::app::OutputListModel::IsFrozenRole).toBool());
    CHECK(facade.live()->global_blank());
    CHECK(facade.live()->global_freeze());
}

TEST_CASE("SessionFacade open_memory_database seeds Main output", "[app]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    REQUIRE(facade.ready());
    REQUIRE(facade.outputs()->rowCount() >= 1);
    REQUIRE(facade.outputs()->data(facade.outputs()->index(0),
                                    seventhslide::app::OutputListModel::NameRole)
                .toString()
            == QStringLiteral("Main"));
}

TEST_CASE("SessionFacade line map + jumpToLine + shortcuts", "[app]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    facade.set_outputs({make_main()});
    facade.live()->play_song(demo_song());

    auto* map = facade.program()->lines();
    REQUIRE(map->rowCount() == facade.program()->totalLines());
    REQUIRE(map->rowCount() > 1);

    REQUIRE(map->data(map->index(0), seventhslide::app::LineMapModel::IsCurrentRole)
                .toBool());
    REQUIRE_FALSE(
        map->data(map->index(1), seventhslide::app::LineMapModel::IsCurrentRole).toBool());

    const auto text0 = map->data(map->index(0), seventhslide::app::LineMapModel::TextRole)
                           .toString();
    REQUIRE_FALSE(text0.isEmpty());
    REQUIRE(map->data(map->index(0), seventhslide::app::LineMapModel::LabelRole)
                .isValid());

    REQUIRE(facade.program()->jumpToLine(1));
    REQUIRE(facade.program()->lineCursor() == 1);
    REQUIRE(map->data(map->index(1), seventhslide::app::LineMapModel::IsCurrentRole)
                .toBool());

    REQUIRE_FALSE(facade.live()->global_blank());
    facade.live()->toggleGlobalBlank();
    REQUIRE(facade.live()->global_blank());

    REQUIRE_FALSE(facade.live()->global_freeze());
    facade.live()->toggleGlobalFreeze();
    REQUIRE(facade.live()->global_freeze());
}

// Each control has one target, however many stacks are live: global blank
// and freeze are the outputs', a stack's are that stack's, and one never
// hides the other's state.
TEST_CASE("SessionFacade Blank and Freeze keep their target as stacks go live",
          "[app][stacks]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    REQUIRE(facade.stacks()->addStack(QStringLiteral("Lobby"), QStringLiteral("info")).isEmpty());
    const auto stacks = facade.stacks()->stacks();
    const auto main = stacks.front().toMap().value(QStringLiteral("id")).toString();
    const auto lobby = stacks.back().toMap().value(QStringLiteral("id")).toString();

    const auto pinned_song = [](std::int64_t id, const char* title, const QString& stack) {
        return seventhslide::test::song_item_on_stack(id, title, stack.toStdString());
    };
    facade.library()->open_rundown(1, nlohmann::json::object(),
                                   {pinned_song(1, "Hymn", main), pinned_song(2, "Notice", lobby)});
    REQUIRE(facade.live()->selectServiceItem(0));
    const auto out = first_output_id(facade).toStdString();

    // Global on with one stack live stays on, and comes off, once a second
    // stack is live.
    facade.live()->toggleGlobalBlank();
    facade.live()->toggleGlobalFreeze();
    REQUIRE(facade.live()->selectServiceItem(1));
    REQUIRE(facade.stacks()->showStackTabs());
    CHECK(facade.session().output_is_blank(out));
    CHECK(facade.session().output_is_frozen(out));
    facade.live()->toggleGlobalBlank();
    facade.live()->toggleGlobalFreeze();
    CHECK_FALSE(facade.session().output_is_blank(out));
    CHECK_FALSE(facade.session().output_is_frozen(out));

    // A stack's toggles touch that stack alone, and are seen in the stacks
    // list the tabs and the transport read.
    int notified = 0;
    QObject::connect(facade.stacks(), &seventhslide::app::StackController::occupiedStacksChanged,
                     [&] { ++notified; });
    REQUIRE(facade.stacks()->toggleStackBlank(lobby));
    REQUIRE(facade.stacks()->toggleStackFreeze(lobby));
    CHECK(notified > 0);
    CHECK(facade.session().stack_is_blank(lobby.toStdString()));
    CHECK(facade.session().stack_is_frozen(lobby.toStdString()));
    CHECK_FALSE(facade.session().stack_is_blank(main.toStdString()));
    CHECK_FALSE(facade.session().output_is_blank(out));
    const auto shown = [&](const QString& id) {
        for (const auto& entry : facade.stacks()->occupiedStacks()) {
            const auto stack = entry.toMap();
            if (stack.value(QStringLiteral("id")).toString() == id) {
                return stack;
            }
        }
        return QVariantMap{};
    };
    CHECK(shown(lobby).value(QStringLiteral("blank")).toBool());
    CHECK(shown(lobby).value(QStringLiteral("frozen")).toBool());
    CHECK_FALSE(shown(main).value(QStringLiteral("blank")).toBool());

    // Shift+Esc's command lifts both kinds.
    facade.live()->toggleGlobalBlank();
    facade.live()->clearBlankFreeze();
    CHECK_FALSE(facade.session().output_is_blank(out));
    CHECK_FALSE(facade.session().stack_is_blank(lobby.toStdString()));
    CHECK_FALSE(facade.session().stack_is_frozen(lobby.toStdString()));
}

// Blank hides the picture: a video under it plays on, whichever kind of
// blank it is, so lifting it shows the video where it has got to.
TEST_CASE("SessionFacade Blank never pauses media", "[app][stacks]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    REQUIRE(facade.library()->createService(QStringLiteral("Vid svc")) > 0);
    REQUIRE(facade.media()->addVideoToService(QStringLiteral("clip.mp4")));
    REQUIRE(facade.live()->selectServiceItem(0));
    REQUIRE(facade.program()->mediaPlay());
    REQUIRE(facade.program()->mediaPlaying());

    facade.live()->toggleGlobalBlank();
    CHECK(facade.live()->global_blank());
    CHECK(facade.program()->mediaPlaying());
    facade.live()->toggleGlobalBlank();

    REQUIRE(facade.live()->toggleOutputBlank(first_output_id(facade)));
    CHECK(facade.program()->mediaPlaying());
    REQUIRE(facade.live()->toggleOutputBlank(first_output_id(facade)));

    REQUIRE(facade.stacks()->toggleStackBlank(facade.stacks()->focusedStackId()));
    CHECK(facade.program()->mediaPlaying());
}

TEST_CASE("SessionFacade video play/pause via mpv backend", "[app]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    REQUIRE(facade.library()->createService(QStringLiteral("Vid svc")) > 0);
    REQUIRE(facade.media()->addVideoToService(QStringLiteral("clip.mp4")));
    REQUIRE(facade.live()->selectServiceItem(0));
    REQUIRE(facade.program()->videoMode());
    // Missing on-disk file: play updates session flags without libmpv init.
    REQUIRE(facade.program()->mediaPositionSec() == Catch::Approx(0.0));
    REQUIRE(facade.program()->mediaDurationSec() == Catch::Approx(0.0));
    REQUIRE(facade.program()->mediaPlay());
    REQUIRE(facade.program()->mediaPlaying());
    REQUIRE(facade.program()->mediaPause());
    REQUIRE_FALSE(facade.program()->mediaPlaying());
    // Seek is accepted; with no file loaded, the side reads where it was
    // told it stands, as it reads whether it plays.
    REQUIRE(facade.program()->mediaSeek(12.5));
    REQUIRE(facade.program()->mediaPositionSec() == Catch::Approx(12.5));
    REQUIRE(facade.program()->mediaDurationSec() == Catch::Approx(0.0));
    REQUIRE(facade.program()->mediaRestart());
    REQUIRE(facade.program()->mediaPlaying());

    const auto frames = facade.session().scene_frames();
    REQUIRE_FALSE(frames.empty());
    REQUIRE(frames[0].video.has_value());
    REQUIRE(frames[0].video->url == "/static/videos/clip.mp4");
}

TEST_CASE("SessionFacade playBible standalone", "[app]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    using seventhslide::model::BibleVerse;
    std::vector<BibleVerse> verses{
        {.book = "John", .chapter = 3, .verse_num = 16, .text = "For God so loved"},
        {.book = "John", .chapter = 3, .verse_num = 17, .text = "For God sent not"},
    };
    const auto bible_id = facade.bible_repo()->import_bible("KJV", "", verses);
    facade.refresh_library();
    REQUIRE(facade.library()->selectBible(static_cast<qint64>(bible_id)));
    REQUIRE(facade.library()->resolveBibleRef(QStringLiteral("John 3")).value(QStringLiteral("success")).toBool());
    REQUIRE(facade.live()->playBible(16, 17));
    REQUIRE(facade.session().focused_stack().program.bible_mode_active());
    REQUIRE(facade.live()->mode() == QStringLiteral("bible"));
    REQUIRE(facade.live()->current_title().contains(QStringLiteral("John")));
    REQUIRE(facade.program()->totalLines() >= 1);

    // Line controller: verse ref lives in the label, not duplicated in text.
    auto* map = facade.program()->lines();
    REQUIRE(map->rowCount() >= 1);
    const auto label0 =
        map->data(map->index(0), seventhslide::app::LineMapModel::LabelRole).toString();
    const auto text0 =
        map->data(map->index(0), seventhslide::app::LineMapModel::TextRole).toString();
    REQUIRE(label0 == QStringLiteral("3:16"));
    REQUIRE_FALSE(text0.startsWith(QStringLiteral("3:16")));
    REQUIRE(text0.contains(QStringLiteral("For God so loved")));

    const auto frames = facade.session().scene_frames();
    REQUIRE_FALSE(frames.empty());
    REQUIRE(frames[0].mode == seventhslide::scene::SceneMode::Bible);
    REQUIRE(element_text(frames[0], "te_bible_ref").find("John") != std::string::npos);
    REQUIRE(element_text(frames[0], "te_bible_ref").find("KJV") != std::string::npos);
    REQUIRE(frames[0].text.has_value());
}

TEST_CASE("An output card's preview draws what the output's destinations are shown",
          "[app][present][preview]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;

    OutputConfig stage;
    stage.id = "out-stage";
    stage.name = "Stage";
    stage.canvas_width = 1280;
    stage.canvas_height = 720;
    facade.set_outputs({make_main(), stage});
    seventhslide::render::OutputWindowHost host;
    facade.set_output_host(&host);
    REQUIRE(facade.outputPreviews()->output_host() == &host);

    const QString main = QStringLiteral("out-main");
    const QString stage_id = QStringLiteral("out-stage");
    // No destination is enabled: the previews' holds are what make each
    // output present at all.
    seventhslide::render::ScenePresenter* main_presenter = host.hold_presenter(main);
    seventhslide::render::ScenePresenter* stage_presenter = host.hold_presenter(stage_id);
    // Two previews of one output share its one presentation.
    REQUIRE(host.hold_presenter(main) == main_presenter);
    REQUIRE(stage_presenter != main_presenter);

    facade.live()->play_song(demo_song());
    QCoreApplication::processEvents();  // the present pass play_song queued

    const auto expected_main = facade.session().scene_frame(main.toStdString());
    const auto expected_stage = facade.session().scene_frame(stage_id.toStdString());
    REQUIRE(expected_main.has_value());
    REQUIRE(expected_stage.has_value());
    REQUIRE(main_presenter->current_frame() != nullptr);
    REQUIRE(stage_presenter->current_frame() != nullptr);
    REQUIRE(*main_presenter->current_frame() == *expected_main);
    REQUIRE(*stage_presenter->current_frame() == *expected_stage);

    SECTION("an output card shows its output's presenter")
    {
        seventhslide::app::OutputPreviewItem card;
        card.setSession(&facade);
        card.setOutputId(main);
        const auto* view = card.findChild<seventhslide::render::PresenterView*>();
        REQUIRE(view != nullptr);
        REQUIRE(view->presenter() == main_presenter);

        card.setOutputId(stage_id);
        REQUIRE(view->presenter() == stage_presenter);

        facade.set_output_host(nullptr);  // a test without a host shows nothing
        REQUIRE(view->presenter() == nullptr);
    }

    host.release_presenter(main);
    host.release_presenter(main);
    host.release_presenter(stage_id);
}

TEST_CASE("An output card's preview lets go of a host that goes first", "[app][preview]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    facade.set_outputs({make_main()});
    seventhslide::app::OutputPreviewItem card;
    card.setSession(&facade);
    card.setOutputId(QStringLiteral("out-main"));
    const auto* view = card.findChild<seventhslide::render::PresenterView*>();
    REQUIRE(view != nullptr);
    {
        seventhslide::render::OutputWindowHost host;
        facade.set_output_host(&host);
        facade.live()->play_song(demo_song());
        QCoreApplication::processEvents();
        REQUIRE(view->presenter() != nullptr);
        REQUIRE(view->presenter()->has_content());
    }
    // The host deleted its presenters; the card no longer points at one.
    REQUIRE(view->presenter() == nullptr);
}

TEST_CASE("An item drawn from the session takes only a session, and follows only the one it holds",
          "[app][preview]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade first;
    first.set_outputs({make_main()});
    SessionFacade second;
    second.set_outputs({make_main()});
    seventhslide::render::OutputWindowHost first_host;
    seventhslide::render::OutputWindowHost second_host;
    seventhslide::app::OutputPreviewItem card;
    card.setOutputId(QStringLiteral("out-main"));
    const auto* view = card.findChild<seventhslide::render::PresenterView*>();
    REQUIRE(view != nullptr);
    QSignalSpy changed(&card, &seventhslide::app::SessionBoundItem::sessionChanged);

    // The property is the session's own type: any other object is refused,
    // not taken as no session.
    const QMetaObject* meta = card.metaObject();
    CHECK(meta->property(meta->indexOfProperty("session")).metaType() == QMetaType::fromType<SessionFacade*>());
    QObject other;
    CHECK_FALSE(card.setProperty("session", QVariant::fromValue(&other)));
    CHECK(card.session() == nullptr);
    CHECK(changed.isEmpty());
    // So is a painted item's.
    seventhslide::app::OverlayTextItem text;
    QSignalSpy text_changed(&text, &seventhslide::app::SessionBoundPaintedItem::sessionChanged);
    CHECK_FALSE(text.setProperty("session", QVariant::fromValue(&other)));
    CHECK(text.setProperty("session", QVariant::fromValue(&first)));
    CHECK(text.session() == &first);
    CHECK(text_changed.size() == 1);

    REQUIRE(card.setProperty("session", QVariant::fromValue(&first)));
    CHECK(card.session() == &first);
    card.setSession(&first);
    CHECK(changed.size() == 1);
    // What it follows of the session it holds: a host arriving.
    first.set_output_host(&first_host);
    CHECK(view->presenter() != nullptr);

    // Another session: the first's host is let go, and its changes no longer reach the card.
    card.setSession(&second);
    CHECK(changed.size() == 2);
    CHECK(view->presenter() == nullptr);
    first.set_output_host(nullptr);
    first.set_output_host(&first_host);
    CHECK(view->presenter() == nullptr);
    second.set_output_host(&second_host);
    CHECK(view->presenter() != nullptr);

    // None: nothing is drawn, and nothing of the session is followed.
    card.setSession(nullptr);
    CHECK(changed.size() == 3);
    CHECK(view->presenter() == nullptr);
    second.set_output_host(nullptr);
    second.set_output_host(&second_host);
    CHECK(view->presenter() == nullptr);
}

namespace {

/// The stage's picture item, counting the times it is painted.
class CountedPicture : public seventhslide::app::FreeformPictureItem {
public:
    using FreeformPictureItem::FreeformPictureItem;

    void paint(QPainter* painter) override
    {
        ++paints_;
        FreeformPictureItem::paint(painter);
    }
    [[nodiscard]] int paints() const noexcept { return paints_; }

private:
    int paints_ = 0;
};

/// The library picture late.png, all ``colour``, in ``facade``'s media folder.
void put_late_picture(const SessionFacade& facade, const QColor& colour)
{
    const auto images = facade.media_root() / "images";
    std::filesystem::create_directories(images);
    QImage picture(64, 36, QImage::Format_RGB32);
    picture.fill(colour);
    REQUIRE(picture.save(QString::fromStdString((images / "late.png").string())));
}

}  // namespace

TEST_CASE("A painted item paints again as a file arrives in the session it holds, and in no other",
          "[app][preview]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::test::TempDir first_dir;
    seventhslide::test::TempDir second_dir;
    auto first = std::make_unique<SessionFacade>();
    REQUIRE(first->open_database(first_dir.path / "library.db"));
    SessionFacade second;
    REQUIRE(second.open_database(second_dir.path / "library.db"));

    QQuickWindow window;
    window.resize(64, 36);
    window.setColor(Qt::black);
    auto* picture = new CountedPicture(window.contentItem());
    picture->setSize(QSizeF(64, 36));
    picture->setFit(QStringLiteral("fill"));
    picture->setFilename(QStringLiteral("late.png"));
    window.show();
    const auto shown = [&] {
        const QImage image = window.grabWindow();
        return image.pixelColor(image.width() / 2, image.height() / 2);
    };

    // The picture is not in the library yet: nothing is painted.
    picture->setSession(first.get());
    CHECK(shown() == QColor(Qt::black));
    // On the disk, but not said to have arrived: not painted again.
    put_late_picture(*first, Qt::red);
    int paints = picture->paints();
    CHECK(shown() == QColor(Qt::black));
    CHECK(picture->paints() == paints);
    // Arrived: painted again, from the session's image cache.
    first->media_arrived({"images/late.png"});
    CHECK(shown() == QColor(Qt::red));
    CHECK(picture->paints() > paints);

    // Another session: painted from its library.
    put_late_picture(second, Qt::blue);
    picture->setSession(&second);
    CHECK(shown() == QColor(Qt::blue));
    // A file arriving in the first no longer paints it; one in the second does.
    paints = picture->paints();
    first->media_arrived({"images/late.png"});
    CHECK(shown() == QColor(Qt::blue));
    CHECK(picture->paints() == paints);
    second.media_arrived({"images/late.png"});
    CHECK(shown() == QColor(Qt::blue));
    CHECK(picture->paints() > paints);

    // The session going while the item holds it: it lets go of it as it
    // goes, as if set to none, and paints again with nothing from it.
    picture->setSession(first.get());
    CHECK(shown() == QColor(Qt::red));
    QSignalSpy let_go(picture, &seventhslide::app::SessionBoundPaintedItem::sessionChanged);
    paints = picture->paints();
    first.reset();
    CHECK(picture->session() == nullptr);
    CHECK(let_go.size() == 1);
    CHECK(shown() == QColor(Qt::black));
    CHECK(picture->paints() > paints);
    // And it takes another.
    picture->setSession(&second);
    CHECK(picture->session() == &second);
    CHECK(shown() == QColor(Qt::blue));
}

TEST_CASE("A host that goes before the session is let go everywhere", "[app][preview]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    OutputConfig stage;
    stage.id = "out-stage";
    stage.name = "Stage";
    facade.set_outputs({make_main(), stage});
    {
        seventhslide::render::OutputWindowHost host;
        facade.set_output_host(&host);
        REQUIRE(facade.outputPreviews()->output_host() == &host);
        REQUIRE(facade.destinations()->displays()->rowCount() >= 1);
    }
    CHECK(facade.outputPreviews()->output_host() == nullptr);
    CHECK(facade.destinations()->displays()->rowCount() == 0);

    // Each controller that painted to it goes on without it (under ASan, any
    // that still reached for it would fail here).
    facade.destinations()->refreshDisplays();
    CHECK(facade.destinations()->displays()->rowCount() == 0);
    facade.live()->play_song(demo_song());
    QCoreApplication::processEvents();
    CHECK(facade.has_build());
    REQUIRE(facade.outputSettings()->deleteOutput(QStringLiteral("Stage")).isEmpty());
}

TEST_CASE("SessionFacade playSong standalone from library", "[app]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    Song lib;
    lib.title = "Live From Library";
    lib.lyrics = "[V1]\nPraise Him\nAll ye saints\n\n[V2]\nLift your voice";
    lib.verse_order = "v1 v2";
    lib.authors = {"Ada Habershon"};
    lib.songbook_name = "Hymnal";
    lib.songbook_entry = "42";
    const auto song_id = facade.song_repo()->insert(lib);
    facade.library()->refresh_songs();

    REQUIRE(facade.library()->songs()->rowCount() == 1);
    REQUIRE(facade.library()->songs()
                ->data(facade.library()->songs()->index(0), seventhslide::app::SongListModel::AuthorsRole)
                .toString()
            == QStringLiteral("Ada Habershon"));

    REQUIRE(facade.live()->playSong(static_cast<qint64>(song_id)));
    REQUIRE(facade.live()->mode() == QStringLiteral("song"));
    REQUIRE(facade.live()->current_title() == QStringLiteral("Live From Library"));
    REQUIRE(facade.has_build());
    REQUIRE(facade.program()->totalLines() > 0);
    REQUIRE(facade.openServiceId() < 0);
}

TEST_CASE("SessionFacade playImage standalone", "[app]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    REQUIRE(facade.live()->playImage(QStringLiteral("welcome.png")));
    REQUIRE(facade.program()->imageMode());
    REQUIRE(facade.live()->mode() == QStringLiteral("image"));
}

// A live clock repaints the outputs every second. The preview revision only
// reloads the operator's live video frame, which has no clock in it, so a tick
// leaves it alone (as a repeat video frame does).
TEST_CASE("A clock tick repaints the outputs without reloading previews", "[app][clock]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    auto oc = make_main();
    auto design = seventhslide::theme::default_elements();
    design.song.push_back(seventhslide::test::clock_element("te_clock"));
    seventhslide::test::use_design(oc, design);
    facade.set_outputs({oc});
    facade.live()->play_song(demo_song());
    QCoreApplication::processEvents();  // the present queued by play_song

    QSignalSpy frames(&facade, &SessionFacade::framesChanged);
    QSignalSpy revisions(&facade, &SessionFacade::previewRevisionChanged);
    REQUIRE(frames.wait(2500));  // the next tick
    CHECK(revisions.isEmpty());
}

// A clock that only a mid-song theme (a theme chip) shows still ticks.
TEST_CASE("A clock in a theme chip's theme ticks", "[app][clock]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    auto oc = make_main();
    seventhslide::test::use_design(oc, seventhslide::theme::default_elements());
    auto clocked = seventhslide::theme::default_elements();
    clocked.song.push_back(seventhslide::test::clock_element("te_clock"));
    seventhslide::model::ThemeEntry chip_theme;
    chip_theme.id = "clocked";
    chip_theme.name = "Clocked";
    chip_theme.style = {{std::string(seventhslide::theme::k_elements_key), clocked.to_json()}};
    oc.song_themes.push_back(std::move(chip_theme));
    facade.set_outputs({oc});
    auto song = demo_song();
    song.verse_order = "V1 TH1 V2";
    song.theme_map = song_theme_json_with_chip({{"out-main", {{"song", {{"theme", "clocked"}}}}}});
    song.theme_map["arrangements"][0]["order"] = "V1 TH1 V2";
    facade.live()->play_song(song);
    QCoreApplication::processEvents();  // the present queued by play_song
    // The clock is in the chip's theme only: the first verse shows none.
    const auto first = facade.session().scene_frame("out-main");
    REQUIRE(first);
    REQUIRE_FALSE(seventhslide::render::scene_frame_has_clock(*first));
    const int slides =
        facade.session().focused_stack().program.last_build().outputs.front().slide_count();
    bool chip_clock = false;
    for (int i = 1; i < slides; ++i) {
        const auto f = facade.session().scene_frame_at({facade.session().focused_stack_id()}, "out-main", i);
        chip_clock = chip_clock || (f && seventhslide::render::scene_frame_has_clock(*f));
    }
    REQUIRE(chip_clock);

    // Move on to the chip's slides; from then on the clock ticks.
    const auto shows_clock = [&facade] {
        const auto f = facade.session().scene_frame("out-main");
        return f && seventhslide::render::scene_frame_has_clock(*f);
    };
    while (!shows_clock()) {
        REQUIRE(facade.program()->nextSlide());
    }
    QCoreApplication::processEvents();  // the present queued by nextSlide

    QSignalSpy frames(&facade, &SessionFacade::framesChanged);
    REQUIRE(frames.wait(2500));  // the next tick
}

// The tick recomposes only the outputs showing a clock; the others keep the
// frame they have.
TEST_CASE("A clock tick recomposes only the outputs showing a clock", "[app][clock]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    auto main_oc = make_main();
    auto design = seventhslide::theme::default_elements();
    design.song.push_back(seventhslide::test::clock_element("te_clock"));
    seventhslide::test::use_design(main_oc, design);
    auto stage = make_main();
    stage.id = "out-stage";
    stage.name = "Stage";
    seventhslide::test::use_design(stage, seventhslide::theme::default_elements());
    facade.set_outputs({main_oc, stage});

    // Seen as the destinations see it: through each output's presenter.
    seventhslide::render::OutputWindowHost host;
    facade.set_output_host(&host);
    seventhslide::render::ScenePresenter* main_presenter =
        host.hold_presenter(QStringLiteral("out-main"));
    seventhslide::render::ScenePresenter* stage_presenter =
        host.hold_presenter(QStringLiteral("out-stage"));
    std::vector<std::uint64_t> main_seen;
    std::vector<std::uint64_t> stage_seen;
    const auto conn = QObject::connect(&facade, &SessionFacade::framesChanged, &facade, [&]() {
        main_seen.push_back(main_presenter->revision());
        stage_seen.push_back(stage_presenter->revision());
    });
    facade.live()->play_song(demo_song());
    QCoreApplication::processEvents();  // the full pass play_song queued
    REQUIRE(main_seen.size() == 1);
    REQUIRE(main_presenter->has_content());
    REQUIRE(stage_presenter->has_content());

    QSignalSpy frames(&facade, &SessionFacade::framesChanged);
    REQUIRE(frames.wait(2500));  // the next tick
    QObject::disconnect(conn);
    REQUIRE(main_seen.size() == 2);
    CHECK(main_seen[1] > main_seen[0]);  // presented again
    CHECK(stage_seen[1] == stage_seen[0]);  // left as it was
    host.release_presenter(QStringLiteral("out-main"));
    host.release_presenter(QStringLiteral("out-stage"));
}

TEST_CASE("SessionFacade output settings create/update/delete", "[app]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    const auto created = facade.outputSettings()->createOutput(QStringLiteral("Foldback"));
    REQUIRE_FALSE(created.contains(QStringLiteral("error")));
    REQUIRE(created.value(QStringLiteral("name")).toString() == QStringLiteral("Foldback"));
    REQUIRE(facade.outputs()->rowCount() == 2);

    auto settings = facade.outputSettings()->getOutputSettings(QStringLiteral("Foldback"));
    REQUIRE(settings.value(QStringLiteral("canvasWidth")).toInt() == 1920);

    REQUIRE(facade.outputSettings()
                ->updateOutputSettings(QStringLiteral("Foldback"),
                                       {{QStringLiteral("name"), QStringLiteral("Stage")},
                                        {QStringLiteral("canvasWidth"), 1280},
                                        {QStringLiteral("canvasHeight"), 720},
                                        {QStringLiteral("exemptFromGlobalBlank"), true},
                                        {QStringLiteral("icon"), QStringLiteral("projector")}})
                .value(QStringLiteral("name"))
                .toString()
            == QStringLiteral("Stage"));
    settings = facade.outputSettings()->getOutputSettings(QStringLiteral("Stage"));
    REQUIRE(settings.value(QStringLiteral("name")).toString() == QStringLiteral("Stage"));
    REQUIRE(settings.value(QStringLiteral("canvasWidth")).toInt() == 1280);
    REQUIRE(settings.value(QStringLiteral("exemptFromGlobalBlank")).toBool());
    REQUIRE(settings.value(QStringLiteral("icon")).toString() == QStringLiteral("projector"));
    // The list shows it too.
    CHECK(facade.outputs()
              ->data(facade.outputs()->index(1), seventhslide::app::OutputListModel::IconRole)
              .toString()
          == QStringLiteral("projector"));

    // The stacks it shows are kept as chosen, in the order given.
    REQUIRE(facade.stacks()->addStack(QStringLiteral("Lobby"), QStringLiteral("info")).isEmpty());
    const QVariantList all_stacks = facade.stacks()->stacks();
    REQUIRE(all_stacks.size() >= 2);
    const QVariantList chosen{all_stacks.back().toMap().value(QStringLiteral("id")),
                              all_stacks.front().toMap().value(QStringLiteral("id"))};
    REQUIRE_FALSE(facade.outputSettings()
                      ->updateOutputSettings(QStringLiteral("Stage"),
                                             {{QStringLiteral("enabledStackIds"), chosen}})
                      .contains(QStringLiteral("error")));
    CHECK(facade.outputSettings()
              ->getOutputSettings(QStringLiteral("Stage"))
              .value(QStringLiteral("enabledStackIds"))
              .toList()
          == chosen);
    // So are the content types it leaves out.
    const QVariantList left_out{QStringLiteral("video"), QStringLiteral("image")};
    REQUIRE_FALSE(facade.outputSettings()
                      ->updateOutputSettings(QStringLiteral("Stage"),
                                             {{QStringLiteral("disabledContentTypes"), left_out}})
                      .contains(QStringLiteral("error")));
    CHECK(facade.outputSettings()
              ->getOutputSettings(QStringLiteral("Stage"))
              .value(QStringLiteral("disabledContentTypes"))
              .toList()
          == left_out);
    // Fields left out keep their values.
    CHECK(facade.outputSettings()
              ->getOutputSettings(QStringLiteral("Stage"))
              .value(QStringLiteral("canvasWidth"))
              .toInt()
          == 1280);

    REQUIRE(facade.outputSettings()->reorderOutputs({QStringLiteral("Stage"), QStringLiteral("Main")}).isEmpty());
    REQUIRE(facade.session().outputs().front().name == "Stage");

    // A stale order list is refused and loses nothing: both outputs are still
    // live, and the next edit persists both.
    REQUIRE_FALSE(facade.outputSettings()
                      ->reorderOutputs({QStringLiteral("Main"), QStringLiteral("Gone")})
                      .isEmpty());
    REQUIRE(facade.session().outputs().size() == 2);
    REQUIRE(facade.session().outputs().front().name == "Stage");
    REQUIRE(facade.outputs()->rowCount() == 2);

    REQUIRE(facade.outputSettings()->deleteOutput(QStringLiteral("Stage")).isEmpty());
    REQUIRE(facade.outputs()->rowCount() == 1);
    // The last one too: a library may have none, and then nothing is shown.
    REQUIRE(facade.outputSettings()->deleteOutput(QStringLiteral("Main")).isEmpty());
    CHECK(facade.outputs()->rowCount() == 0);
    CHECK(facade.session().outputs().empty());
    CHECK(facade.session().primary_output() == nullptr);
}

TEST_CASE("SessionFacade output strip send/stop + preview", "[app]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    facade.set_outputs({make_main()});
    facade.live()->play_song(demo_song());

    const auto preview = facade.outputPreviews()->render_output_preview(QStringLiteral("out-main"), QSize(160, 90));
    REQUIRE_FALSE(preview.isNull());
    REQUIRE(preview.width() == 160);
    REQUIRE(preview.height() == 90);
    REQUIRE(facade.preview_revision() > 0);

    seventhslide::render::OutputWindowHost host;
    facade.set_output_host(&host);
    REQUIRE(facade.destinations()->displays()->rowCount() >= 1);

    REQUIRE_FALSE(facade.destinations()->displays()
                      ->data(facade.destinations()->displays()->index(0),
                             seventhslide::app::DisplayListModel::KeyRole)
                      .toString()
                      .isEmpty());
}

// Destinations follow their output by id: renaming the output retitles its
// open window and leaves it showing the live content.
TEST_CASE("Renaming an output keeps its open window on air", "[app][outputs][destinations]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    facade.set_outputs({make_main()});
    seventhslide::render::OutputWindowHost host;
    facade.set_output_host(&host);
    facade.live()->play_song(demo_song());

    const auto dest = facade.destinations()
                          ->add(QStringLiteral("out-main"), QStringLiteral("display"))
                          .value(QStringLiteral("id"))
                          .toString();
    REQUIRE(facade.destinations()->setEnabled(dest, true).isEmpty());
    QCoreApplication::processEvents();
    auto* win = host.window(QStringLiteral("out-main"));
    REQUIRE(win != nullptr);

    REQUIRE(facade.outputSettings()
                ->updateOutputSettings(QStringLiteral("Main"),
                                       {{QStringLiteral("name"), QStringLiteral("Stage")}})
                .value(QStringLiteral("error"))
                .toString()
                .isEmpty());
    QCoreApplication::processEvents();
    REQUIRE(host.window(QStringLiteral("out-main")) == win);
    CHECK(win->title().contains(QStringLiteral("Stage")));
    CHECK(facade.outputs()
              ->data(facade.outputs()->index(0), seventhslide::app::OutputListModel::IsOpenRole)
              .toBool());
    CHECK(facade.destinations()->list(QStringLiteral("out-main")).size() == 1);

    REQUIRE(facade.program()->nextSlide());
    QCoreApplication::processEvents();
    const auto* shown = win->presenter()->current_frame();
    REQUIRE(shown != nullptr);
    CHECK(shown->output_name == "Stage");
    CHECK(shown->slide_index == facade.session().scene_frame("out-main")->slide_index);
}

TEST_CASE("SessionFacade output destinations add enable steal", "[app][destinations]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    facade.set_outputs({make_main()});
    facade.live()->play_song(demo_song());

    seventhslide::render::OutputWindowHost host;
    facade.set_output_host(&host);

    auto added = facade.destinations()->add(QStringLiteral("out-main"), QStringLiteral("display"));
    REQUIRE_FALSE(added.contains(QStringLiteral("error")));
    const auto id = added.value(QStringLiteral("id")).toString();
    REQUIRE_FALSE(id.isEmpty());
    REQUIRE(facade.destinations()->list(QStringLiteral("out-main")).size() == 1);

    REQUIRE(facade.destinations()->setEnabled(id, true).isEmpty());
    REQUIRE(host.is_open(QStringLiteral("out-main")));
    REQUIRE(facade.outputs()
                ->data(facade.outputs()->index(0), seventhslide::app::OutputListModel::IsOpenRole)
                .toBool());

    const auto created = facade.outputSettings()->createOutput(QStringLiteral("Foldback"));
    REQUIRE_FALSE(created.contains(QStringLiteral("error")));
    const auto foldback_id = facade.outputSettings()
                                 ->getOutputSettings(QStringLiteral("Foldback"))
                                 .value(QStringLiteral("id"))
                                 .toString();
    REQUIRE_FALSE(foldback_id.isEmpty());
    auto fold = facade.destinations()->add(foldback_id, QStringLiteral("display"));
    const auto fold_id = fold.value(QStringLiteral("id")).toString();
    const auto key = facade.destinations()->displays()
                         ->data(facade.destinations()->displays()->index(0),
                                seventhslide::app::DisplayListModel::KeyRole)
                         .toString();
    REQUIRE(facade.destinations()->setScreen(id, key).isEmpty());
    REQUIRE(facade.destinations()->setScreen(fold_id, key).isEmpty());
    REQUIRE(facade.destinations()->setEnabled(fold_id, true).isEmpty());
    const auto foldback = QString::fromStdString(facade.session().outputs().back().id);
    REQUIRE(host.is_open(foldback));
    REQUIRE_FALSE(host.is_open(QStringLiteral("out-main")));

    REQUIRE(facade.destinations()->remove(fold_id).isEmpty());
    REQUIRE_FALSE(host.is_open(foldback));

    REQUIRE(facade.destinations()->setName(id, QStringLiteral("House Left")).isEmpty());
    const auto named = facade.destinations()->list(QStringLiteral("out-main")).front().toMap();
    REQUIRE(named.value(QStringLiteral("name")).toString() == QStringLiteral("House Left"));

    const auto catalog = facade.destinations()->types();
    REQUIRE(catalog.size() == 6);
    const auto ndi_info = facade.destinations()->ndiRuntimeInfo();
    REQUIRE(ndi_info.contains(QStringLiteral("available")));
    const bool ndi_ready = ndi_info.value(QStringLiteral("available")).toBool();
    REQUIRE(ndi_ready == seventhslide::render::ndi_runtime_available());
    if (ndi_ready) {
        // The runtime is installed on this machine, so the type is offered.
        REQUIRE_FALSE(ndi_info.value(QStringLiteral("version")).toString().isEmpty());
        REQUIRE(ndi_info.value(QStringLiteral("message")).toString().isEmpty());
        auto ndi_added = facade.destinations()->add(QStringLiteral("out-main"), QStringLiteral("ndi"));
        REQUIRE_FALSE(ndi_added.contains(QStringLiteral("error")));
        const QString ndi_id = ndi_added.value(QStringLiteral("id")).toString();
        REQUIRE_FALSE(ndi_id.isEmpty());
        REQUIRE(facade.destinations()->setNdiName(ndi_id, QStringLiteral("Foyer")).isEmpty());
        REQUIRE_FALSE(facade.destinations()->setNdiName(ndi_id, QStringLiteral("  ")).isEmpty());
        REQUIRE(facade.destinations()->setAlpha(ndi_id, true).isEmpty());
        REQUIRE(facade.destinations()->remove(ndi_id).isEmpty());
    } else {
        // Greyed out: adding must fail rather than create a dead destination.
        REQUIRE_FALSE(ndi_info.value(QStringLiteral("message")).toString().isEmpty());
        REQUIRE(facade.destinations()->add(QStringLiteral("out-main"), QStringLiteral("ndi"))
                    .contains(QStringLiteral("error")));
    }
    if (seventhslide::render::virtual_camera_implemented()) {
        auto vcam = facade.destinations()->add(QStringLiteral("out-main"), QStringLiteral("vcam"));
        REQUIRE_FALSE(vcam.contains(QStringLiteral("error")));
        REQUIRE_FALSE(vcam.value(QStringLiteral("id")).toString().isEmpty());
        if (seventhslide::render::list_virtual_camera_devices().empty()) {
            const QString err =
                facade.destinations()->setEnabled(vcam.value(QStringLiteral("id")).toString(), true);
            REQUIRE_FALSE(err.isEmpty());
        }
    } else {
        REQUIRE(facade.destinations()->add(QStringLiteral("out-main"), QStringLiteral("vcam"))
                    .contains(QStringLiteral("error")));
    }
}

TEST_CASE("SessionFacade stacks persist and stay hidden until occupied", "[app][stacks]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    REQUIRE(facade.stacks()->stacks().size() == 1);
    REQUIRE(facade.stacks()->stacks().front().toMap().value(QStringLiteral("id")).toString()
            == QStringLiteral("main"));
    REQUIRE_FALSE(facade.stacks()->showStackTabs());
    REQUIRE(facade.stacks()->occupiedStacks().isEmpty());

    REQUIRE(facade.stacks()->addStack(QStringLiteral("Lobby"), QStringLiteral("info")).isEmpty());
    REQUIRE(facade.stacks()->stacks().size() == 2);
    REQUIRE_FALSE(facade.stacks()->showStackTabs());

    REQUIRE(facade.stacks()->stackTypeDefaults().value(QStringLiteral("song")).toString()
            == QStringLiteral("main"));

    const auto lobby = facade.stacks()->stacks().back().toMap().value(QStringLiteral("id")).toString();
    REQUIRE(facade.stacks()->setStackTypeDefault(QStringLiteral("bible"), lobby).isEmpty());
    REQUIRE(facade.stacks()->setStackTypeDefault(QStringLiteral("image"), lobby).isEmpty());

    // Stacks are edited through the stack controller only: the settings map
    // neither shows them nor takes them.
    REQUIRE_FALSE(facade.getAppSettings().contains(QStringLiteral("stacks")));
    QVariantMap fields;
    fields.insert(QStringLiteral("stackTypeDefaults"),
                  QVariantMap{{QStringLiteral("image"), QStringLiteral("main")}});
    fields.insert(QStringLiteral("stacks"), QVariantList{});
    REQUIRE(facade.updateAppSettings(fields).isEmpty());
    REQUIRE(facade.stacks()->stacks().size() == 2);
    REQUIRE(facade.stacks()->stackTypeDefaults().value(QStringLiteral("image")).toString() == lobby);
    REQUIRE(facade.stacks()->stackTypeDefaults().value(QStringLiteral("bible")).toString() == lobby);
    REQUIRE(facade.stacks()->stackTypeDefaults().value(QStringLiteral("song")).toString()
            == QStringLiteral("main"));
}

TEST_CASE("SessionFacade refuses a stale stack order without losing stacks", "[app][stacks]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    REQUIRE(facade.stacks()->addStack(QStringLiteral("Lobby"), QStringLiteral("info")).isEmpty());
    const auto lobby = facade.stacks()->stacks().back().toMap().value(QStringLiteral("id")).toString();

    REQUIRE_FALSE(facade.stacks()->reorderStacks({lobby, QStringLiteral("gone")}).isEmpty());
    REQUIRE(facade.stacks()->stacks().size() == 2);

    // The next edit persists both stacks, in their original order.
    REQUIRE(facade.stacks()->addStack(QStringLiteral("Lower thirds"), QStringLiteral("text")).isEmpty());
    const auto stacks = facade.stacks()->stacks();
    REQUIRE(stacks.size() == 3);
    REQUIRE(stacks.front().toMap().value(QStringLiteral("id")).toString() == QStringLiteral("main"));
    REQUIRE(stacks.at(1).toMap().value(QStringLiteral("id")).toString() == lobby);
}

TEST_CASE("SessionFacade setStackTypeDefault persists one type", "[app][stacks]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    REQUIRE(facade.stacks()->addStack(QStringLiteral("Lobby"), QStringLiteral("info")).isEmpty());
    const auto lobby = facade.stacks()->stacks().back().toMap().value(QStringLiteral("id")).toString();
    REQUIRE(lobby != QStringLiteral("main"));

    REQUIRE(facade.stacks()->setStackTypeDefault(QStringLiteral("bible"), lobby).isEmpty());
    REQUIRE(facade.stacks()->stackTypeDefaults().value(QStringLiteral("bible")).toString() == lobby);
    REQUIRE(facade.stacks()->stackTypeDefaults().value(QStringLiteral("song")).toString()
            == QStringLiteral("main"));

    REQUIRE_FALSE(facade.stacks()->setStackTypeDefault(QStringLiteral("bogus"), lobby).isEmpty());
    REQUIRE_FALSE(facade.stacks()->setStackTypeDefault(QStringLiteral("bible"),
                                             QStringLiteral("nope")).isEmpty());

    // Releasing a type sends it back to the fallback; Main cannot release one.
    REQUIRE(facade.stacks()->fallbackStackId() == QStringLiteral("main"));
    REQUIRE(facade.stacks()->setStackTypeDefault(QStringLiteral("video"), lobby).isEmpty());
    REQUIRE(facade.stacks()->releaseStackTypeDefault(QStringLiteral("video"), lobby).isEmpty());
    REQUIRE(facade.stacks()->stackTypeDefaults().value(QStringLiteral("video")).toString()
            == QStringLiteral("main"));
    REQUIRE_FALSE(facade.stacks()->releaseStackTypeDefault(QStringLiteral("song"),
                                                           QStringLiteral("main")).isEmpty());

    // Deleting the stack remaps the default that pointed at it.
    REQUIRE(facade.stacks()->removeStack(lobby).isEmpty());
    REQUIRE(facade.stacks()->stackTypeDefaults().value(QStringLiteral("bible")).toString()
            == QStringLiteral("main"));
}

TEST_CASE("SessionFacade typeDefaults notifies when a type is repointed", "[app][stacks][signals]")
{
    // The Settings dialog's checkmarks bind to typeDefaults. Repointing a type
    // leaves the stack list as it was, so stacksChanged alone cannot carry it.
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    REQUIRE(facade.stacks()->addStack(QStringLiteral("Lobby"), QStringLiteral("info")).isEmpty());
    const auto lobby = facade.stacks()->stacks().back().toMap().value(QStringLiteral("id")).toString();

    int defaults_changed = 0;
    int stacks_changed = 0;
    QObject::connect(facade.stacks(), &seventhslide::app::StackController::typeDefaultsChanged,
                     [&] { ++defaults_changed; });
    QObject::connect(facade.stacks(), &seventhslide::app::StackController::stacksChanged,
                     [&] { ++stacks_changed; });

    REQUIRE(facade.stacks()->setStackTypeDefault(QStringLiteral("song"), lobby).isEmpty());
    REQUIRE(defaults_changed == 1);
    REQUIRE(stacks_changed == 0);
    REQUIRE(facade.stacks()->property("typeDefaults").toMap().value(QStringLiteral("song")).toString()
            == lobby);

    // Pointing a type where it already points changes nothing.
    REQUIRE(facade.stacks()->setStackTypeDefault(QStringLiteral("song"), lobby).isEmpty());
    REQUIRE(defaults_changed == 1);

    // A rename persists every stack setting but leaves the defaults alone.
    REQUIRE(facade.stacks()->renameStack(lobby, QStringLiteral("Foyer")).isEmpty());
    REQUIRE(defaults_changed == 1);

    // Deleting the stack remaps its types, and that is a change too.
    REQUIRE(facade.stacks()->removeStack(lobby).isEmpty());
    REQUIRE(defaults_changed == 2);
    REQUIRE(facade.stacks()->property("typeDefaults").toMap().value(QStringLiteral("song")).toString()
            == QStringLiteral("main"));
}

TEST_CASE("Deleting the live stack clears what the outputs show", "[app][stacks][present]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.set_outputs({make_main()});
    seventhslide::render::OutputWindowHost host;
    facade.set_output_host(&host);
    seventhslide::render::ScenePresenter* presenter =
        host.hold_presenter(QStringLiteral("out-main"));

    REQUIRE(facade.stacks()->addStack(QStringLiteral("Lobby"), QStringLiteral("info")).isEmpty());
    const auto lobby = facade.stacks()->stacks().back().toMap().value(QStringLiteral("id")).toString();
    REQUIRE(facade.stacks()->setStackTypeDefault(QStringLiteral("song"), lobby).isEmpty());
    facade.live()->play_song(demo_song());
    QCoreApplication::processEvents();
    REQUIRE(presenter->current_frame() != nullptr);
    REQUIRE(presenter->current_frame()->mode == seventhslide::scene::SceneMode::Song);
    REQUIRE(facade.has_build());

    // The only stack with anything on it goes: the outputs show nothing, as
    // if it had been cleared, rather than keeping its last slide.
    REQUIRE(facade.stacks()->removeStack(lobby).isEmpty());
    QCoreApplication::processEvents();
    REQUIRE_FALSE(facade.has_build());
    REQUIRE(presenter->current_frame() != nullptr);
    CHECK(presenter->current_frame()->mode == seventhslide::scene::SceneMode::Empty);
    CHECK_FALSE(presenter->current_frame()->text.has_value());

    host.release_presenter(QStringLiteral("out-main"));
}

TEST_CASE("SessionFacade playVideo loads mpv frames", "[app][mpv]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::test::TempDir tmp;
    const auto videos = tmp.path / "videos";
    std::filesystem::create_directories(videos);
    // A 64x36 clip of solid #3366cc.
    std::filesystem::copy_file(
        std::filesystem::path{SEVENTHSLIDE_FIXTURES_DIR} / "video" / "solid_3366cc_64x36.mp4",
        videos / "clip.mp4");

    SessionFacade facade;
    seventhslide::render::OutputWindowHost host;
    host.set_static_root(tmp.path);
    facade.set_output_host(&host);
    facade.set_media_root(tmp.path);
    facade.set_outputs({make_main()});

    REQUIRE(facade.live()->playVideo(QStringLiteral("clip.mp4")));
    REQUIRE(facade.program()->videoMode());
    REQUIRE(facade.program()->mediaPlaying());

    // The centre of the contain-fitted clip turns from the black plate to the
    // clip's blue once mpv has decoded a frame.
    const auto centre_is_clip = [&] {
        const QImage preview = facade.outputPreviews()->render_output_preview(
            QStringLiteral("out-main"), QSize(160, 90));
        if (preview.isNull()) {
            return false;
        }
        const QColor px = preview.pixelColor(80, 45);
        return px.blue() > 150 && px.red() < 100;
    };
    CHECK(QTest::qWaitFor(centre_is_clip, 2000));
}

// A playing clip moves its clock every tick (about 30 a second). Only the
// position follows that; whether it plays and how long it is change rarely,
// and every binding on them (and the remote's session section) must not
// re-run 30 times a second.
TEST_CASE("Playback ticks move the position and leave the rest of the media state",
          "[app][mpv][signals]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::test::TempDir tmp;
    const auto videos = tmp.path / "videos";
    std::filesystem::create_directories(videos);
    std::filesystem::copy_file(
        std::filesystem::path{SEVENTHSLIDE_FIXTURES_DIR} / "video" / "solid_3366cc_64x36.mp4",
        videos / "clip.mp4");

    SessionFacade facade;
    facade.set_media_root(tmp.path);
    facade.set_outputs({make_main()});
    REQUIRE(facade.live()->playVideo(QStringLiteral("clip.mp4"), /*loop=*/true));
    // Let it load: the length arrives once mpv has opened the file.
    REQUIRE(QTest::qWaitFor([&] { return facade.program()->mediaDurationSec() > 0.0; }, 5000));

    int states = 0;
    int positions = 0;
    QObject::connect(facade.program(), &seventhslide::app::SideView::mediaStateChanged,
                     [&] { ++states; });
    QObject::connect(facade.program(), &seventhslide::app::SideView::mediaPositionChanged,
                     [&] { ++positions; });
    // Forty ticks, over a second: the state is told once or twice in them,
    // so one told every few ticks shows.
    CHECK(QTest::qWaitFor([&] { return positions >= 40; }, 10000));
    INFO("states=" << states << " positions=" << positions);
    CHECK(states <= 3);
}

TEST_CASE("SessionFacade focus switch refreshes only the controller", "[app][signals][stacks]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.set_outputs({make_main()});
    REQUIRE(facade.stacks()->addStack(QStringLiteral("Lobby"), QStringLiteral("info")).isEmpty());
    const QString lobby = facade.stacks()->stacks().back().toMap().value(QStringLiteral("id")).toString();
    const QString main_id = QStringLiteral("main");

    facade.live()->play_song(demo_song());
    facade.stacks()->armSendTo(lobby);
    // Other lines on it: the list starts over for them.
    auto other = demo_song();
    other.lyrics = "[V1]\nOther words\n";
    facade.live()->play_song(std::move(other));
    REQUIRE(facade.stacks()->occupiedStacks().size() == 2);
    REQUIRE(facade.stacks()->focusedStackId() == lobby);
    QCoreApplication::processEvents();

    int sessions = 0;
    int focus_changes = 0;
    int frames = 0;
    int previews = 0;
    int occupied = 0;
    int output_resets = 0;
    int line_resets = 0;
    int service_updates = 0;
    QObject::connect(&facade, &SessionFacade::sessionChanged, [&] { ++sessions; });
    QObject::connect(&facade, &SessionFacade::focusedStackChanged, [&] { ++focus_changes; });
    QObject::connect(&facade, &SessionFacade::framesChanged, [&] { ++frames; });
    QObject::connect(&facade, &SessionFacade::previewRevisionChanged, [&] { ++previews; });
    QObject::connect(facade.stacks(), &seventhslide::app::StackController::occupiedStacksChanged, [&] { ++occupied; });
    QObject::connect(facade.outputs(), &QAbstractItemModel::modelReset, [&] { ++output_resets; });
    QObject::connect(facade.program()->lines(), &QAbstractItemModel::modelReset, [&] { ++line_resets; });
    QObject::connect(facade.serviceItems(), &QAbstractItemModel::dataChanged,
                     [&](const QModelIndex&, const QModelIndex&, const QList<int>&) {
                         ++service_updates;
                     });

    facade.stacks()->setFocusedStackId(main_id);
    QCoreApplication::processEvents();
    REQUIRE(facade.stacks()->focusedStackId() == main_id);
    REQUIRE(focus_changes == 1);
    REQUIRE(sessions == 0);
    REQUIRE(line_resets == 1);
    REQUIRE(frames == 0);
    REQUIRE(previews == 0);
    REQUIRE(occupied == 0);
    REQUIRE(output_resets == 0);
    REQUIRE(service_updates == 0);

    // Clicking the tab that is already focused does nothing.
    facade.stacks()->setFocusedStackId(main_id);
    facade.stacks()->setFocusedStackId(QStringLiteral("no-such-stack"));
    REQUIRE(focus_changes == 1);
    REQUIRE(line_resets == 1);
}

TEST_CASE("SessionFacade stack list and armed target notify only on change", "[app][signals][stacks]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.set_outputs({make_main()});
    REQUIRE(facade.stacks()->addStack(QStringLiteral("Lobby"), QStringLiteral("info")).isEmpty());
    const QString lobby = facade.stacks()->stacks().back().toMap().value(QStringLiteral("id")).toString();

    int occupied = 0;
    int armed = 0;
    int sessions = 0;
    QObject::connect(facade.stacks(), &seventhslide::app::StackController::occupiedStacksChanged, [&] { ++occupied; });
    QObject::connect(facade.stacks(), &seventhslide::app::StackController::armedSendToChanged, [&] { ++armed; });
    QObject::connect(&facade, &SessionFacade::sessionChanged, [&] { ++sessions; });

    facade.live()->play_song(demo_song());
    REQUIRE(occupied == 1);
    REQUIRE(facade.program()->nextSlide());
    REQUIRE(occupied == 1);

    const int sessions_before_arm = sessions;
    facade.stacks()->armSendTo(lobby);
    facade.stacks()->armSendTo(lobby);
    REQUIRE(armed == 1);
    REQUIRE(sessions == sessions_before_arm);

    REQUIRE(facade.stacks()->toggleStackBlank(QStringLiteral("main")));
    REQUIRE(occupied == 2);
}

TEST_CASE("Blanking or freezing a stack keeps the line map's rows", "[app][signals][stacks]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.set_outputs({make_main()});
    facade.live()->play_song(demo_song());
    REQUIRE(facade.program()->lines()->rowCount() > 0);

    // A reset would scroll the controller's line list back to the top.
    int line_resets = 0;
    QObject::connect(facade.program()->lines(), &QAbstractItemModel::modelReset, [&] { ++line_resets; });

    REQUIRE(facade.stacks()->toggleStackBlank(QStringLiteral("main")));
    REQUIRE(facade.session().stack_is_blank("main"));
    REQUIRE(facade.stacks()->toggleStackFreeze(QStringLiteral("main")));
    REQUIRE(facade.session().stack_is_frozen("main"));
    REQUIRE(facade.stacks()->toggleStackBlank(QStringLiteral("main")));
    REQUIRE(facade.stacks()->toggleStackFreeze(QStringLiteral("main")));
    REQUIRE(line_resets == 0);
}

TEST_CASE("SessionFacade resets the outputs model only when output rows change", "[app][signals]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.set_outputs({make_main()});

    int resets = 0;
    int row_updates = 0;
    QObject::connect(facade.outputs(), &QAbstractItemModel::modelReset, [&] { ++resets; });
    QObject::connect(facade.outputs(), &QAbstractItemModel::dataChanged,
                     [&](const QModelIndex&, const QModelIndex&, const QList<int>&) {
                         ++row_updates;
                     });

    facade.live()->play_song(demo_song());
    facade.live()->play_song(demo_song());
    REQUIRE(resets == 0);
    REQUIRE(row_updates >= 2);

    REQUIRE(facade.outputSettings()->createOutput(QStringLiteral("Stream")).contains(QStringLiteral("name")));
    REQUIRE(resets == 1);
    REQUIRE(facade.outputs()->rowCount() == 2);
}

TEST_CASE("Renaming an output keeps its Freeform slides and theme overrides", "[app][outputs][freeform]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const std::string output_id = facade.session().primary_output()->id;
    REQUIRE_FALSE(output_id.empty());

    // A per-song text theme for this output, stored before the rename.
    const auto theme = facade.themes()->create(QStringLiteral("Main"), QStringLiteral("song"),
                                          QStringLiteral("Wide gaps"),
                                          QVariantMap{{QStringLiteral("verse_gap"), 37}});
    const auto theme_id = theme.value(QStringLiteral("id")).toString().toStdString();
    REQUIRE_FALSE(theme_id.empty());
    seventhslide::model::Song song;
    song.title = "Hymn";
    song.lyrics = "---[Verse:1]---\nPraise\n";
    song.verse_order = "TH1 V1";
    song.theme_map = song_theme_json_with_chip({{output_id, {{"song", {{"theme", theme_id}}}}}});
    const auto song_id = facade.song_repo()->insert(song);

    const auto freeform_id = add_library_freeform(facade, "Promo", 1);

    const auto renamed = facade.outputSettings()->updateOutputSettings(QStringLiteral("Main"), {{QStringLiteral("name"), QStringLiteral("Stage")},
                                          {QStringLiteral("canvasWidth"), 1920},
                                          {QStringLiteral("canvasHeight"), 1080}});
    REQUIRE(renamed.value(QStringLiteral("name")).toString() == QStringLiteral("Stage"));
    REQUIRE(facade.session().primary_output()->id == output_id);

    SECTION("the Freeform still paints on the renamed output")
    {
        REQUIRE(facade.live()->playFreeform(freeform_id));
        const auto frame = facade.session().scene_frame(output_id);
        REQUIRE(frame.has_value());
        REQUIRE(frame->output_name == "Stage");
        REQUIRE_FALSE(frame->objects.empty());
    }
    SECTION("the song's theme override still applies")
    {
        REQUIRE(facade.live()->playSong(static_cast<qint64>(song_id)));
        REQUIRE(facade.session().focused_stack().program.last_build().outputs.front().theme_for_slide(0).behavior.verse_gap
                == 37);
    }
}

TEST_CASE("Output ids survive reopening the library", "[app][outputs]")
{
    seventhslide::test::ensure_qt_application();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto path = std::filesystem::path(dir.path().toStdString()) / "library.db";

    std::string first_id;
    {
        SessionFacade facade;
        REQUIRE(facade.open_database(path));
        first_id = facade.session().primary_output()->id;
        REQUIRE_FALSE(first_id.empty());
    }
    SessionFacade reopened;
    REQUIRE(reopened.open_database(path));
    REQUIRE(reopened.session().primary_output()->id == first_id);
}

TEST_CASE("Playing a library song keeps the open service's name", "[app][service]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    const auto service_id = facade.service_repo()->create_service("Sunday AM");
    REQUIRE(facade.library()->openService(static_cast<qint64>(service_id)));
    REQUIRE(facade.library()->openServiceName() == QStringLiteral("Sunday AM"));

    seventhslide::model::Song song;
    song.title = "Hymn";
    song.lyrics = "---[Verse:1]---\nPraise\n";
    const auto song_id = facade.song_repo()->insert(song);
    REQUIRE(facade.live()->playSong(static_cast<qint64>(song_id)));

    REQUIRE(facade.openServiceId() == static_cast<qint64>(service_id));
    REQUIRE(facade.library()->openServiceName() == QStringLiteral("Sunday AM"));
}

TEST_CASE("Renaming a blanked or frozen output keeps it blanked or frozen", "[app][outputs]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.live()->play_song(demo_song());
    REQUIRE(facade.live()->toggleOutputBlank(first_output_id(facade)));
    REQUIRE(facade.live()->toggleOutputFreeze(first_output_id(facade)));
    const auto output_id = facade.session().outputs().front().id;
    REQUIRE(facade.session().scene_frame(output_id)->is_blank);

    const auto renamed = facade.outputSettings()->updateOutputSettings(QStringLiteral("Main"), {{QStringLiteral("name"), QStringLiteral("Stage")},
                                          {QStringLiteral("canvasWidth"), 1920},
                                          {QStringLiteral("canvasHeight"), 1080}});
    REQUIRE(renamed.value(QStringLiteral("name")).toString() == QStringLiteral("Stage"));

    const auto frame = facade.session().scene_frame(output_id);
    REQUIRE(frame.has_value());
    REQUIRE(frame->output_name == "Stage");
    REQUIRE(frame->is_blank);
    REQUIRE(frame->is_frozen);
}

namespace {

/// A facade on a fresh library whose media folder holds real audio files,
/// with a second stack, "Audio", that audio plays on by default.
struct AudioFacade {
    seventhslide::test::QtBootstrap qt;
    seventhslide::test::TempDir dir;
    SessionFacade facade;
    QString audio_stack;

    AudioFacade()
    {
        REQUIRE(facade.open_database(dir.path / "library.db"));
        const auto audio = facade.media_root() / "audio";
        std::filesystem::create_directories(audio);
        // Registered under the names they have.
        for (const char* name : {"bed.wav", "lobby.wav", "prelude.wav"}) {
            seventhslide::test::write_tone_wav(audio / name, 1.0);
            facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Audio, name, name);
        }
        facade.refresh_library();
        REQUIRE(facade.stacks()->addStack(QStringLiteral("Audio"), QStringLiteral("layers")).isEmpty());
        audio_stack =
            facade.stacks()->stacks().back().toMap().value(QStringLiteral("id")).toString();
        REQUIRE(facade.stacks()->setStackTypeDefault(QStringLiteral("audio"), audio_stack).isEmpty());
    }

    [[nodiscard]] const seventhslide::session::StackRuntime& stack(const QString& id)
    {
        const auto* st = facade.session().runtime({id.toStdString()});
        REQUIRE(st != nullptr);
        return *st;
    }
};

/// Output ``output``'s own audio fades (its General page's).
void set_output_fades(SessionFacade& facade,
                      const QString& output,
                      int in_ms,
                      int out_ms,
                      int overlap_ms = 0)
{
    const QVariantMap fades{{QStringLiteral("fade_in_ms"), in_ms},
                            {QStringLiteral("fade_out_ms"), out_ms},
                            {QStringLiteral("overlap_ms"), overlap_ms}};
    REQUIRE(facade.outputSettings()
                ->updateOutputSettings(output, {{QStringLiteral("audioFades"), fades}})
                .value(QStringLiteral("error"))
                .toString()
                .isEmpty());
}

/// A second output, named ``name``, showing every stack.
QString add_output(SessionFacade& facade, const QString& name)
{
    const auto made = facade.outputSettings()->createOutput(name);
    REQUIRE(made.value(QStringLiteral("error")).toString().isEmpty());
    return made.value(QStringLiteral("name")).toString();
}

}  // namespace

namespace {

/// Output ``output_id``'s mix, as loud as it has been since ``reset``, for as
/// long as this lives.
class OutputListener : public seventhslide::audio::OutputAudioSink {
public:
    OutputListener(seventhslide::audio::Mixer& mixer, const std::string& output_id)
        : mixer_(mixer)
    {
        mixer_.add_sink(output_id, this);
    }
    ~OutputListener() override { mixer_.remove_sink(this); }

    // On the mixer's thread. Only a louder block raises the peak, and in one
    // step: a reset made while a silent block is read stays a reset.
    void push(const float* frames, std::size_t count) override
    {
        float loudest = 0.0f;
        for (std::size_t i = 0; i < count * seventhslide::audio::k_channels; ++i) {
            loudest = std::max(loudest, std::abs(frames[i]));
        }
        float peak = peak_.load();
        while (loudest > peak && !peak_.compare_exchange_weak(peak, loudest)) {
        }
    }
    [[nodiscard]] bool heard() const { return peak_.load() > 0.0f; }
    void reset() { peak_.store(0.0f); }

private:
    seventhslide::audio::Mixer& mixer_;
    std::atomic<float> peak_{0.0f};
};

}  // namespace

TEST_CASE("An output leaving audio or video out hears neither from that source",
          "[app][audio][mpv]")
{
    AudioFacade t;
    auto& facade = t.facade;
    const auto media = facade.media_root();
    seventhslide::test::write_tone_wav(media / "audio" / "long.wav", 8.0);
    facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Audio, "long.wav", "long.wav");
    std::filesystem::create_directories(media / "videos");
    std::filesystem::copy_file(std::filesystem::path(SEVENTHSLIDE_FIXTURES_DIR) / "video"
                                   / "tone_3366cc_64x36_8s.mp4",
                               media / "videos" / "clip.mp4");
    facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Video, "clip.mp4", "clip.mp4");
    facade.refresh_library();

    const auto output = facade.session().outputs().front();
    OutputListener listener(facade.mixer()->engine().mixer(), output.id);
    const auto leave_out = [&](const QString& type) {
        const QVariantList types = type.isEmpty() ? QVariantList{} : QVariantList{type};
        REQUIRE(facade.outputSettings()
                    ->updateOutputSettings(QString::fromStdString(output.name),
                                           {{QStringLiteral("disabledContentTypes"), types}})
                    .value(QStringLiteral("error"))
                    .toString()
                    .isEmpty());
        // What was already in the block being mixed is past.
        QTest::qWait(50);
        listener.reset();
    };
    const auto stack_loud = [&](const QString& stack) {
        return QTest::qWaitFor(
            [&] {
                return facade.mixer()->meter(seventhslide::audio::StripKind::Stack,
                                             stack.toStdString())
                           .level_db[0]
                       > -60.0f;
            },
            5000);
    };
    const auto now_heard = [&] { return QTest::qWaitFor([&] { return listener.heard(); }, 5000); };

    // Audio left out: its stack plays it, the output has none of it, and it
    // comes in while playing once it is not left out.
    leave_out(QStringLiteral("audio"));
    REQUIRE(facade.live()->playAudio(QStringLiteral("long.wav")));
    REQUIRE(stack_loud(t.audio_stack));
    QTest::qWait(150);
    CHECK_FALSE(listener.heard());
    leave_out({});
    CHECK(now_heard());
    facade.stacks()->clearAllStacks();

    // The same for a video's sound, and leaving out the other type is no matter.
    leave_out(QStringLiteral("video"));
    REQUIRE(facade.live()->playVideo(QStringLiteral("clip.mp4")));
    REQUIRE(stack_loud(QStringLiteral("main")));
    QTest::qWait(150);
    CHECK_FALSE(listener.heard());
    leave_out(QStringLiteral("audio"));
    CHECK(now_heard());
}

TEST_CASE("Audio plays on its own stack and player beside a video", "[app][audio][mpv]")
{
    AudioFacade t;
    auto& facade = t.facade;

    REQUIRE(facade.stacks()->stackTypeDefaults().value(QStringLiteral("audio")).toString()
            == t.audio_stack);
    REQUIRE(facade.live()->playAudio(QStringLiteral("bed.wav")));
    REQUIRE(t.stack(t.audio_stack).audio_mode_active());
    REQUIRE(facade.playback()->audio_player_count() == 1);
    // Played from the library, it still names itself as what is live.
    REQUIRE(facade.live()->current_title() == QStringLiteral("bed.wav"));

    // A video on Main plays alongside: the audio keeps its stack and player.
    REQUIRE(facade.live()->playVideo(QStringLiteral("clip.mp4")));
    REQUIRE(t.stack(QStringLiteral("main")).video_mode_active());
    REQUIRE(t.stack(t.audio_stack).audio_mode_active());
    REQUIRE(facade.playback()->audio_player_count() == 1);

    // The focused stack's transport: audio has one, but no picture.
    facade.stacks()->setFocusedStackId(t.audio_stack);
    REQUIRE(facade.program()->mediaTransport());
    REQUIRE_FALSE(facade.program()->videoMode());
    // The decoder opens the real file: its length reaches the controller.
    REQUIRE(QTest::qWaitFor([&] { return facade.program()->mediaDurationSec() > 0.5; }, 10000));
    REQUIRE(facade.program()->mediaDurationSec() == Catch::Approx(1.0).margin(0.1));
    REQUIRE(facade.program()->mediaPause());
    REQUIRE(QTest::qWaitFor([&] { return !facade.program()->mediaPlaying(); }, 5000));
    REQUIRE(facade.program()->mediaPlay());
    REQUIRE(QTest::qWaitFor([&] { return facade.program()->mediaPlaying(); }, 5000));

    // A second audio stack gets a second player.
    REQUIRE(facade.stacks()->addStack(QStringLiteral("Lobby"), QStringLiteral("info")).isEmpty());
    const auto lobby =
        facade.stacks()->stacks().back().toMap().value(QStringLiteral("id")).toString();
    facade.stacks()->armSendTo(lobby);
    REQUIRE(facade.live()->playAudio(QStringLiteral("lobby.wav")));
    REQUIRE(t.stack(lobby).audio_mode_active());
    REQUIRE(facade.playback()->audio_player_count() == 2);

    // Clearing a stack releases its player; the other keeps playing.
    REQUIRE(facade.stacks()->clearStack(lobby).isEmpty());
    REQUIRE(facade.playback()->audio_player_count() == 1);
    REQUIRE(t.stack(t.audio_stack).audio_mode_active());

    // A song sent to the audio stack replaces the audio and its player goes.
    facade.stacks()->armSendTo(t.audio_stack);
    REQUIRE(facade.library()->createService(QStringLiteral("Svc")) > 0);
    auto song = demo_song();
    REQUIRE(facade.live()->playSong(facade.song_repo()->insert(song)));
    REQUIRE(t.stack(t.audio_stack).content_kind() == seventhslide::model::ContentKind::Song);
    REQUIRE(facade.playback()->audio_player_count() == 0);
}

TEST_CASE("Audio items play through the Audio Mixer on their stack's strip", "[app][audio]")
{
    namespace audio = seventhslide::audio;
    AudioFacade t;
    auto& facade = t.facade;
    auto& mixer = facade.mixer()->engine().mixer();
    const auto stack_id = t.audio_stack.toStdString();
    const auto meter = [&](audio::StripKind kind, const std::string& id) {
        return mixer.meter(kind, id).level_db[0];
    };

    // A new library's first output is on Listen, so this computer hears what
    // Main carries; the stacks are not.
    const auto output_id = facade.session().outputs().front().id;
    REQUIRE(mixer.strip(audio::StripKind::Output, output_id).listen);
    REQUIRE_FALSE(mixer.strip(audio::StripKind::Stack, stack_id).listen);
    REQUIRE_FALSE(mixer.strip(audio::StripKind::Stack, "main").listen);

    // The mixer's clock (its timer, in tests) plays the file: the stack's
    // meter moves, and so does the transport's clock.
    REQUIRE(facade.live()->playAudio(QStringLiteral("bed.wav")));
    facade.stacks()->setFocusedStackId(t.audio_stack);
    REQUIRE(QTest::qWaitFor([&] { return meter(audio::StripKind::Stack, stack_id) > -40.0f; },
                            5000));
    REQUIRE(QTest::qWaitFor([&] { return facade.program()->mediaPositionSec() > 0.05; }, 5000));
    // Every output shows every stack by default, so each carries the sound.
    REQUIRE(meter(audio::StripKind::Output, output_id) > -40.0f);

    // Cleared, the stack's player leaves the mixer and its meter falls away.
    REQUIRE(facade.stacks()->clearStack(t.audio_stack).isEmpty());
    REQUIRE(facade.playback()->audio_player_count() == 0);
    REQUIRE(QTest::qWaitFor([&] { return meter(audio::StripKind::Stack, stack_id) < -60.0f; },
                            5000));

    // A stack added later gets a strip of its own.
    REQUIRE(facade.stacks()->addStack(QStringLiteral("Lobby"), QStringLiteral("info")).isEmpty());
    const auto lobby =
        facade.stacks()->stacks().back().toMap().value(QStringLiteral("id")).toString();
    facade.stacks()->armSendTo(lobby);
    REQUIRE(facade.live()->playAudio(QStringLiteral("lobby.wav")));
    REQUIRE(mixer.strip(audio::StripKind::Stack, lobby.toStdString()) ==
            seventhslide::library::ChannelMix{});
    REQUIRE(QTest::qWaitFor(
        [&] { return meter(audio::StripKind::Stack, lobby.toStdString()) > -40.0f; }, 5000));
}

TEST_CASE("Audio paused before its length is known still announces it", "[app][audio][signals]")
{
    AudioFacade t;
    auto& facade = t.facade;
    facade.stacks()->setFocusedStackId(t.audio_stack);
    // What the seek bar reads each time it hears the media state change.
    double announced = 0.0;
    QObject::connect(facade.program(), &seventhslide::app::SideView::mediaStateChanged,
                     [&] { announced = facade.program()->mediaDurationSec(); });

    REQUIRE(facade.live()->playAudio(QStringLiteral("bed.wav")));
    REQUIRE(facade.program()->mediaPause());
    REQUIRE(QTest::qWaitFor([&] { return announced > 0.5; }, 5000));
    REQUIRE(announced == Catch::Approx(1.0).margin(0.1));
}

TEST_CASE("Audio played again announces its length once its file opens", "[app][audio][signals]")
{
    AudioFacade t;
    auto& facade = t.facade;
    // Long enough that it does not end while the test waits: its end
    // announces the media state as well, and would hide a length not
    // announced.
    seventhslide::test::write_tone_wav(facade.media_root() / "audio" / "hymn.wav", 10.0);
    facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Audio, "hymn.wav", "hymn.wav");
    facade.refresh_library();
    facade.stacks()->setFocusedStackId(t.audio_stack);
    // What the seek bar reads each time it hears the media state change.
    double announced = 0.0;
    QObject::connect(facade.program(), &seventhslide::app::SideView::mediaStateChanged,
                     [&] { announced = facade.program()->mediaDurationSec(); });

    REQUIRE(facade.live()->playAudio(QStringLiteral("hymn.wav")));
    REQUIRE(QTest::qWaitFor([&] { return announced > 5.0; }, 5000));

    // Played again and again, quickly: each time a new player, whose length
    // is not known as the change is announced. Its file opens before the
    // next tick (none runs while this thread sleeps), and the length, the
    // same as the last play's, still reaches the seek bar.
    for (int i = 0; i < 3; ++i) {
        REQUIRE(facade.live()->playAudio(QStringLiteral("hymn.wav")));
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        REQUIRE(facade.program()->mediaDurationSec() == Catch::Approx(10.0).margin(0.1));
        REQUIRE(QTest::qWaitFor([&] { return announced > 5.0; }, 3000));
        REQUIRE(announced == Catch::Approx(10.0).margin(0.1));
        REQUIRE(facade.program()->mediaPlaying());
    }
}

TEST_CASE("Seeking audio moves only the clock", "[app][audio][signals]")
{
    AudioFacade t;
    auto& facade = t.facade;
    REQUIRE(facade.live()->playAudio(QStringLiteral("bed.wav")));
    facade.stacks()->setFocusedStackId(t.audio_stack);
    REQUIRE(QTest::qWaitFor([&] { return facade.program()->mediaDurationSec() > 0.5; }, 5000));

    // Audio has no picture: a scrub repaints nothing and reloads no preview.
    int previews = 0;
    int repaints = 0;
    int positions = 0;
    const auto a = QObject::connect(facade.playback(),
                                    &seventhslide::app::PlaybackController::previewChanged,
                                    [&] { ++previews; });
    const auto b = QObject::connect(facade.playback(),
                                    &seventhslide::app::PlaybackController::repaintRequested,
                                    [&] { ++repaints; });
    const auto c = QObject::connect(facade.program(),
                                    &seventhslide::app::SideView::mediaPositionChanged,
                                    [&] { ++positions; });
    REQUIRE(facade.program()->mediaSeek(0.5));
    QObject::disconnect(a);
    QObject::disconnect(b);
    QObject::disconnect(c);
    CHECK(previews == 0);
    CHECK(repaints == 0);
    CHECK(positions >= 1);
    CHECK(facade.program()->mediaPositionSec() == Catch::Approx(0.5).margin(0.05));
}

TEST_CASE("Audio sought back from its end stays paused, and plays on from there",
          "[app][audio]")
{
    AudioFacade t;
    auto& facade = t.facade;
    REQUIRE(facade.live()->playAudio(QStringLiteral("bed.wav")));
    facade.stacks()->setFocusedStackId(t.audio_stack);
    REQUIRE(QTest::qWaitFor([&] { return facade.program()->mediaPositionSec() > 0.9; }, 5000));
    REQUIRE(QTest::qWaitFor([&] { return !facade.program()->mediaPlaying(); }, 5000));

    // The transport shows Play at the end; a seek leaves it showing the truth.
    REQUIRE(facade.program()->mediaSeek(0.3));
    REQUIRE_FALSE(facade.program()->mediaPlaying());
    QTest::qWait(300);
    REQUIRE(facade.program()->mediaPositionSec() == Catch::Approx(0.3).margin(0.02));

    // Played, the clock the seek bar hears moves on from there.
    double heard = 0.0;
    QObject::connect(facade.program(), &seventhslide::app::SideView::mediaPositionChanged,
                     [&] { heard = facade.program()->mediaPositionSec(); });
    REQUIRE(facade.program()->mediaPlay());
    REQUIRE(QTest::qWaitFor([&] { return heard > 0.4; }, 5000));
}

TEST_CASE("Play after audio reached its end starts it over", "[app][audio]")
{
    AudioFacade t;
    auto& facade = t.facade;
    REQUIRE(facade.live()->playAudio(QStringLiteral("bed.wav")));
    facade.stacks()->setFocusedStackId(t.audio_stack);
    REQUIRE(QTest::qWaitFor([&] { return facade.program()->mediaPositionSec() > 0.9; }, 5000));
    REQUIRE(QTest::qWaitFor([&] { return !facade.program()->mediaPlaying(); }, 5000));

    // The button shows Play at the end, and pressing it plays from the start.
    REQUIRE(facade.program()->mediaPlay());
    REQUIRE(facade.program()->mediaPlaying());
    REQUIRE(facade.program()->mediaPositionSec() < 0.5);
    REQUIRE(QTest::qWaitFor([&] { return facade.program()->mediaPositionSec() > 0.1; }, 5000));
    REQUIRE(facade.program()->mediaPlaying());
}

TEST_CASE("A video's sound plays through the Audio Mixer on its stack's strip",
          "[app][audio][mpv]")
{
    namespace audio = seventhslide::audio;
    AudioFacade t;
    auto& facade = t.facade;
    auto& mixer = facade.mixer()->engine().mixer();
    const auto main_meter = [&] { return mixer.meter(audio::StripKind::Stack, "main").level_db[0]; };
    const auto videos = facade.media_root() / "videos";
    std::filesystem::create_directories(videos);
    // A clip with a 440 Hz sound track, at -18 dBFS.
    std::filesystem::copy_file(
        std::filesystem::path{SEVENTHSLIDE_FIXTURES_DIR} / "video" / "tone_3366cc_64x36_8s.mp4",
        videos / "talk.mp4");

    REQUIRE(facade.live()->playVideo(QStringLiteral("talk.mp4")));
    REQUIRE(t.stack(QStringLiteral("main")).video_mode_active());
    // mpv draws the picture; the sound reaches Main's strip and the outputs.
    REQUIRE(QTest::qWaitFor([&] { return main_meter() > -30.0f; }, 5000));
    const auto output_id = facade.session().outputs().front().id;
    REQUIRE(mixer.meter(audio::StripKind::Output, output_id).level_db[0] > -30.0f);

    // Paused, the sound stops with the picture.
    facade.stacks()->setFocusedStackId(QStringLiteral("main"));
    REQUIRE(facade.program()->mediaPause());
    REQUIRE(QTest::qWaitFor([&] { return main_meter() < -60.0f; }, 5000));

    // Played again and then cleared: the sound leaves the mixer with it.
    REQUIRE(facade.program()->mediaPlay());
    REQUIRE(QTest::qWaitFor([&] { return main_meter() > -30.0f; }, 5000));
    REQUIRE(facade.stacks()->clearStack(QStringLiteral("main")).isEmpty());
    REQUIRE(QTest::qWaitFor([&] { return main_meter() < -60.0f; }, 5000));
}

TEST_CASE("Audio service items keep playing their files through renames", "[app][audio]")
{
    AudioFacade t;
    auto& facade = t.facade;
    auto& media = *facade.media();
    REQUIRE(facade.library()->createService(QStringLiteral("Sunday")) > 0);

    REQUIRE(media.addAudioToService(QStringLiteral("bed.wav")));
    const auto folder = media.library(MediaKindQml::Audio)->createFolder(QStringLiteral("Preludes"));
    REQUIRE(folder >= 0);
    REQUIRE(media.library(MediaKindQml::Audio)->moveItems({QStringLiteral("lobby.wav"), QStringLiteral("prelude.wav")}, folder)
                .isEmpty());
    // A folder adds one item per file, in the folder's order.
    REQUIRE(media.addAudioFolderToService(folder));
    const auto& items = facade.session().service_items();
    REQUIRE(items.size() == 3);
    for (const auto& item : items) {
        REQUIRE(item.kind() == seventhslide::model::ContentKind::Audio);
    }
    REQUIRE(items[1].data.value("filename", "") == "lobby.wav");
    REQUIRE(items[2].data.value("filename", "") == "prelude.wav");

    // Renaming a file the service plays retitles it in the library; the file
    // and the item playing it are untouched.
    const auto renamed = media.renameMediaFile(MediaKindQml::Audio, QStringLiteral("bed.wav"),
                                               QStringLiteral("Gathering"));
    REQUIRE(renamed.value(QStringLiteral("filename")).toString() == QStringLiteral("bed.wav"));
    REQUIRE(facade.session().service_items()[0].data.value("filename", "") == "bed.wav");
    // An item added now is titled by the new title.
    REQUIRE(media.addAudioToService(QStringLiteral("bed.wav")));
    REQUIRE(facade.session().service_items().back().title() == "Gathering.wav");

    // A file or folder in use cannot be deleted, and nothing goes.
    const auto refused = media.library(MediaKindQml::Audio)->deleteItem(QStringLiteral("bed.wav"));
    REQUIRE(refused.contains(QStringLiteral("audio file")));
    REQUIRE(std::filesystem::exists(facade.media_root() / "audio" / "bed.wav"));
    REQUIRE_FALSE(media.library(MediaKindQml::Audio)->deleteFolder(folder).isEmpty());
    REQUIRE(std::filesystem::exists(facade.media_root() / "audio" / "prelude.wav"));

    // Selecting the item plays it on the audio stack.
    REQUIRE(facade.live()->selectServiceItem(0));
    const auto& stack = t.stack(t.audio_stack);
    REQUIRE(stack.audio_mode_active());
    REQUIRE(stack.playback().filename == "bed.wav");
}

TEST_CASE("A theme's background video plays from the frames the outputs are given", "[app][video]")
{
    // Found in the one composition a change makes for the outputs (none is
    // composed only to look for it), and let go when the stage is cleared.
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    const auto loops = facade.media_root() / "theme_backgrounds";
    std::filesystem::create_directories(loops);
    std::filesystem::copy_file(
        std::filesystem::path(SEVENTHSLIDE_FIXTURES_DIR) / "video" / "solid_3366cc_64x36_20s.mp4",
        loops / "loop.mp4");
    auto main = make_main();
    seventhslide::test::set_look(main, {{"background_type", "video"},
                                        {"background_video", "/static/theme_backgrounds/loop.mp4"}});
    facade.set_outputs({main});

    const auto playing = [&] {
        const auto* player = facade.playback()->background_player();
        return player != nullptr && player->has_media();
    };
    facade.live()->play_song(demo_song());
    REQUIRE(QTest::qWaitFor(playing, 5000));
    facade.stacks()->clearAllStacks();
    REQUIRE(QTest::qWaitFor([&] { return !playing(); }, 5000));
}

TEST_CASE("A background video plays beside a content video an output leaves out",
          "[app][video][mpv]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    const auto loops = facade.media_root() / "theme_backgrounds";
    const auto videos = facade.media_root() / "videos";
    std::filesystem::create_directories(loops);
    std::filesystem::create_directories(videos);
    const auto fixture = std::filesystem::path(SEVENTHSLIDE_FIXTURES_DIR) / "video"
                       / "solid_3366cc_64x36_20s.mp4";
    std::filesystem::copy_file(fixture, loops / "loop.mp4");
    std::filesystem::copy_file(fixture, videos / "clip.mp4");

    auto main = make_main();
    auto thirds = make_main();
    thirds.id = "out-thirds";
    thirds.name = "Lower Thirds";
    thirds.disabled_content_types = {"video"};
    seventhslide::test::set_look(thirds,
                                 {{"background_type", "video"},
                                  {"background_video", "/static/theme_backgrounds/loop.mp4"}});
    facade.set_outputs({main, thirds});

    const auto background = [&] {
        const auto* player = facade.playback()->background_player();
        return player != nullptr && player->has_media();
    };
    facade.live()->play_song(demo_song());
    REQUIRE(QTest::qWaitFor(background, 5000));

    // Main shows the video; the other output leaves it out, so the background
    // behind its song plays on.
    REQUIRE(facade.stacks()->addStack(QStringLiteral("Videos"), QStringLiteral("video")).isEmpty());
    const auto stack_id =
        facade.stacks()->stacks().back().toMap().value(QStringLiteral("id")).toString();
    facade.stacks()->setFocusedStackId(stack_id);
    REQUIRE(facade.stacks()->setStackTypeDefault(QStringLiteral("video"), stack_id).isEmpty());
    REQUIRE(facade.live()->playVideo(QStringLiteral("clip.mp4")));
    REQUIRE(facade.program()->videoMode());
    QTest::qWait(200);
    REQUIRE(background());
    REQUIRE(QTest::qWaitFor(
        [&] { return !facade.playback()->background_frame().isNull(); }, 5000));

    // Pausing the video leaves the background moving.
    REQUIRE(facade.program()->mediaPause());
    REQUIRE(QTest::qWaitFor([&] { return !facade.program()->mediaPlaying(); }, 5000));
    const auto frame_before = facade.playback()->background_frame().cacheKey();
    CHECK(QTest::qWaitFor(
        [&] { return facade.playback()->background_frame().cacheKey() != frame_before; }, 5000));

    // Once that output shows the video too, it has the stage everywhere.
    REQUIRE(facade.outputSettings()
                ->updateOutputSettings(QStringLiteral("Lower Thirds"),
                                       {{QStringLiteral("disabledContentTypes"), QVariantList{}}})
                .value(QStringLiteral("error"))
                .toString()
                .isEmpty());
    CHECK(QTest::qWaitFor([&] { return !background(); }, 5000));
}

TEST_CASE("An audio item fades in, overlaps the next and fades out as its output says",
          "[app][audio][transition]")
{
    AudioFacade t;
    auto& facade = t.facade;
    set_output_fades(facade, QStringLiteral("Main"), 300, 600, 400);
    auto* playback = facade.playback();
    const auto stack = t.audio_stack.toStdString();

    // In from nothing.
    REQUIRE(facade.live()->playAudio(QStringLiteral("bed.wav")));
    CHECK(playback->audio_level(stack) < 1.0f);
    REQUIRE(QTest::qWaitFor([&] { return playback->audio_level(stack) == 1.0f; }, 3000));
    CHECK(playback->leaving_count() == 0);

    // The next item on the same stack: the two play together, one going.
    REQUIRE(facade.live()->playAudio(QStringLiteral("lobby.wav")));
    CHECK(playback->leaving_count() == 1);
    CHECK(playback->audio_player_count() == 1);
    REQUIRE(QTest::qWaitFor([&] { return playback->leaving_count() == 0; }, 3000));
    REQUIRE(QTest::qWaitFor([&] { return playback->audio_level(stack) == 1.0f; }, 3000));

    // Cleared: it fades out rather than stopping dead.
    REQUIRE(facade.stacks()->clearStack(t.audio_stack).isEmpty());
    CHECK(playback->audio_player_count() == 0);
    CHECK(playback->leaving_count() == 1);
    REQUIRE(QTest::qWaitFor([&] { return playback->leaving_count() == 0; }, 3000));
}

TEST_CASE("An audio item plays on through a rebuild of what is live", "[app][audio]")
{
    AudioFacade t;
    auto& facade = t.facade;
    auto* playback = facade.playback();
    REQUIRE(facade.live()->playAudio(QStringLiteral("bed.wav")));
    facade.stacks()->setFocusedStackId(t.audio_stack);
    REQUIRE(QTest::qWaitFor([&] { return facade.program()->mediaPositionSec() > 0.2; }, 5000));
    const auto instance = t.stack(t.audio_stack).media_instance();
    REQUIRE(instance != 0);
    const double before = facade.program()->mediaPositionSec();
    const auto build = t.stack(t.audio_stack).build_id();

    // A theme edited under it builds what is live again. The item stays the
    // same one, on the same player: nothing is handed off, nothing reloads.
    for (const auto& entry : facade.themes()->list(QStringLiteral("Main"), QStringLiteral("media"))) {
        const auto theme = entry.toMap();
        const auto id = theme.value(QStringLiteral("id")).toString();
        auto style = facade.themes()
                         ->theme_map(QStringLiteral("Main"), QStringLiteral("media"), id)
                         .value(QStringLiteral("style"))
                         .toMap();
        style.insert(QStringLiteral("transitions"),
                     QVariantMap{{QStringLiteral("video"),
                                  QVariantMap{{QStringLiteral("type"), QStringLiteral("fade")},
                                              {QStringLiteral("duration_ms"), 200}}}});
        REQUIRE(facade.themes()
                    ->update_theme(QStringLiteral("Main"), QStringLiteral("media"), id,
                                   theme.value(QStringLiteral("name")).toString(),
                                   seventhslide::app::to_json(QVariant(style)))
                    .isEmpty());
    }
    REQUIRE(t.stack(t.audio_stack).build_id() != build);
    CHECK(t.stack(t.audio_stack).media_instance() == instance);
    CHECK(playback->audio_player_count() == 1);
    CHECK(playback->leaving_count() == 0);
    CHECK(facade.program()->mediaPositionSec() >= before);
    CHECK(facade.program()->mediaPlaying());
}

TEST_CASE("Two stacks' videos play at once, each heard on its own stack", "[app][mpv]")
{
    namespace audio = seventhslide::audio;
    AudioFacade t;
    auto& facade = t.facade;
    const auto media = facade.media_root();
    std::filesystem::create_directories(media / "videos");
    for (const char* name : {"back.mp4", "front.mp4"}) {
        std::filesystem::copy_file(std::filesystem::path(SEVENTHSLIDE_FIXTURES_DIR) / "video"
                                       / "tone_3366cc_64x36_8s.mp4",
                                   media / "videos" / name);
        facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Video, name, name);
    }
    facade.refresh_library();
    REQUIRE(facade.stacks()->addStack(QStringLiteral("Front"), QStringLiteral("info")).isEmpty());
    const auto front =
        facade.stacks()->stacks().back().toMap().value(QStringLiteral("id")).toString();
    const auto main = QStringLiteral("main");
    auto& mixer = facade.mixer()->engine().mixer();
    const auto meter = [&](const QString& stack) {
        return mixer.meter(audio::StripKind::Stack, stack.toStdString()).level_db[0];
    };
    const auto picture_of = [&](const QString& stack) {
        const auto key = seventhslide::scene::content_video_key(t.stack(stack).media_instance());
        return seventhslide::render::source_image(&facade.playback()->content_frames(), key);
    };

    facade.stacks()->armSendTo(main);
    REQUIRE(facade.live()->playVideo(QStringLiteral("back.mp4")));
    facade.stacks()->armSendTo(front);
    REQUIRE(facade.live()->playVideo(QStringLiteral("front.mp4")));

    // Each has a player of its own: both play, both pictures go to the
    // outputs, and each is heard on its own stack's strip.
    REQUIRE(t.stack(main).media_player() != nullptr);
    REQUIRE(t.stack(front).media_player() != nullptr);
    REQUIRE(t.stack(main).media_player() != t.stack(front).media_player());
    REQUIRE(QTest::qWaitFor(
        [&] { return picture_of(main) != nullptr && picture_of(front) != nullptr; }, 10000));
    REQUIRE(QTest::qWaitFor([&] { return meter(main) > -40.0f && meter(front) > -40.0f; },
                            10000));

    // The front one cleared, the one behind plays on.
    const double before = t.stack(main).playback().position_sec;
    REQUIRE(facade.stacks()->clearStack(front).isEmpty());
    REQUIRE(t.stack(main).media_player() != nullptr);
    REQUIRE(QTest::qWaitFor([&] { return meter(front) < -60.0f; }, 5000));
    REQUIRE(QTest::qWaitFor(
        [&] { return t.stack(main).playback().position_sec > before + 0.3; }, 10000));
    CHECK(meter(main) > -40.0f);
}

TEST_CASE("A video plays on through the transition that replaces it", "[app][mpv][transition]")
{
    AudioFacade t;
    auto& facade = t.facade;
    const auto media = facade.media_root();
    std::filesystem::create_directories(media / "videos");
    for (const char* name : {"one.mp4", "two.mp4"}) {
        std::filesystem::copy_file(std::filesystem::path(SEVENTHSLIDE_FIXTURES_DIR) / "video"
                                       / "tone_3366cc_64x36_8s.mp4",
                                   media / "videos" / name);
        facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Video, name, name);
    }
    facade.refresh_library();
    // The video's own change, and the song's that replaces it, each on its
    // kind's themes.
    const auto set_video_transition = [&](const char* type) {
        const QVariantMap fade{{QStringLiteral("type"), QString::fromLatin1(type)},
                               {QStringLiteral("duration_ms"), 600}};
        for (const auto& [kind, context] : {std::pair{"media", "video"}, std::pair{"song", "song"}}) {
            const QString k = QString::fromLatin1(kind);
            for (const auto& entry : facade.themes()->list(QStringLiteral("Main"), k)) {
                const auto theme = entry.toMap();
                const auto id = theme.value(QStringLiteral("id")).toString();
                auto style = facade.themes()->theme_map(QStringLiteral("Main"), k, id)
                                 .value(QStringLiteral("style"))
                                 .toMap();
                style.insert(QStringLiteral("transitions"),
                             QVariantMap{{QString::fromLatin1(context), fade}});
                REQUIRE(facade.themes()
                            ->update_theme(QStringLiteral("Main"), k, id,
                                           theme.value(QStringLiteral("name")).toString(),
                                           seventhslide::app::to_json(QVariant(style)))
                            .isEmpty());
            }
        }
    };
    auto* playback = facade.playback();
    const auto playing = [&] {
        return QTest::qWaitFor([&] { return facade.program()->mediaPlaying(); }, 5000);
    };

    set_video_transition("fade");
    REQUIRE(facade.live()->playVideo(QStringLiteral("one.mp4")));
    REQUIRE(playing());
    // The next video on the stack: the first plays on, fading, then goes.
    REQUIRE(facade.live()->playVideo(QStringLiteral("two.mp4")));
    CHECK(playback->leaving_video_count() == 1);
    REQUIRE(playing());
    REQUIRE(QTest::qWaitFor([&] { return playback->leaving_video_count() == 0; }, 3000));

    // A song after it: the video goes as the song comes in.
    auto song = demo_song();
    REQUIRE(facade.live()->playSong(facade.song_repo()->insert(song)));
    CHECK(playback->leaving_video_count() == 1);
    REQUIRE(QTest::qWaitFor([&] { return playback->leaving_video_count() == 0; }, 3000));

    // A theme that cuts: the video stops at once.
    set_video_transition("cut");
    REQUIRE(facade.live()->playVideo(QStringLiteral("one.mp4")));
    REQUIRE(playing());
    set_video_transition("cut");
    REQUIRE(facade.live()->playVideo(QStringLiteral("two.mp4")));
    QCoreApplication::processEvents();
    CHECK(playback->leaving_video_count() == 0);
}

// 4.7's 6: a video object on a Freeform slide plays its clip in its box,
// with a player of its own; one that starts on a click waits for it, its
// click a step of the slide.
TEST_CASE("A video object plays in its box, on show or on a click", "[app][mpv][builds]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::test::TempDir tmp;
    const auto videos = tmp.path / "videos";
    std::filesystem::create_directories(videos);
    // A 64x36 clip of solid #3366cc.
    std::filesystem::copy_file(
        std::filesystem::path{SEVENTHSLIDE_FIXTURES_DIR} / "video" / "solid_3366cc_64x36.mp4",
        videos / "clip.mp4");

    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.set_media_root(tmp.path);
    facade.set_outputs({make_main()});
    const auto id = add_library_freeform(facade, "Clip", 1);
    auto doc = facade.freeformLibrary()->load_freeform_document(id);
    REQUIRE(doc);
    seventhslide::freeform::Object video;
    video.id = "vid";
    video.type = seventhslide::freeform::ObjectType::Video;
    video.filename = "clip.mp4";
    video.object_fit = "fill";
    // The right half of the canvas.
    video.x = 960;
    video.y = 0;
    video.w = 960;
    video.h = 1080;
    const bool on_click = GENERATE(false, true);
    video.video.start = on_click ? seventhslide::canvas::VideoStart::OnClick
                                 : seventhslide::canvas::VideoStart::OnShow;
    doc->decks[0].slides[0].objects.push_back(video);
    REQUIRE(facade.freeformLibrary()->commit_freeform_document(id, *doc).isEmpty());

    REQUIRE(facade.live()->playFreeform(id));
    auto* playback = facade.playback();
    REQUIRE(playback->object_player_count() == 1);
    // Saved, the item plays its own copy of the library's clip.
    const auto saved = facade.freeformLibrary()->load_freeform_document(id);
    REQUIRE(saved);
    const auto& played = saved->decks[0].slides[0].objects.back().filename;
    REQUIRE(played.starts_with("/static/freeform_media/"));
    const auto key = seventhslide::scene::video_object_key(played);
    CHECK(facade.program()->step_count() == (on_click ? 2 : 1));
    if (on_click) {
        // Held on its first picture until the click.
        CHECK_FALSE(playback->object_playing(key));
        REQUIRE(facade.program()->nextSlide());
        CHECK(facade.program()->currentStep() == 1);
    }
    CHECK(playback->object_playing(key));

    // Its picture comes into its box: the right half blue, the left not.
    const auto shows_clip = [&] {
        QCoreApplication::processEvents();
        const QImage preview = facade.outputPreviews()->render_output_preview(
            QStringLiteral("out-main"), QSize(160, 90));
        if (preview.isNull()) {
            return false;
        }
        const QColor right = preview.pixelColor(120, 45);
        const QColor left = preview.pixelColor(20, 45);
        return right.blue() > 150 && right.red() < 100 && left.blue() < 100;
    };
    CHECK(QTest::qWaitFor(shows_clip, 5000));

    // Off the slide, its player goes.
    facade.stacks()->clearAllStacks();
    CHECK(playback->object_player_count() == 0);
}

namespace {

/// An AudioFacade whose media folder also holds 8 s videos with a sound
/// track, by ``names``, and whose stack ``stack`` has a Preview.
void add_videos(AudioFacade& t, std::initializer_list<const char*> names)
{
    const auto videos = t.facade.media_root() / "videos";
    std::filesystem::create_directories(videos);
    for (const char* name : names) {
        std::filesystem::copy_file(std::filesystem::path(SEVENTHSLIDE_FIXTURES_DIR) / "video"
                                       / "tone_3366cc_64x36_8s.mp4",
                                   videos / name);
        t.facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Video, name, name);
    }
    t.facade.refresh_library();
}

[[nodiscard]] const seventhslide::session::StackRuntime& preview_of(AudioFacade& t,
                                                                    const QString& id)
{
    const auto* st = t.facade.session().runtime(
        {id.toStdString(), seventhslide::session::StackSide::Preview});
    REQUIRE(st != nullptr);
    return *st;
}

}  // namespace

TEST_CASE("A video played in Preview is heard only on its cue strip, and carries on at the take",
          "[app][mpv][preview]")
{
    namespace audio = seventhslide::audio;
    AudioFacade t;
    auto& facade = t.facade;
    add_videos(t, {"one.mp4", "two.mp4"});
    const auto main = QStringLiteral("main");
    REQUIRE(facade.stacks()->setStackPreview(main, true).isEmpty());
    auto* playback = facade.playback();
    auto& mixer = facade.mixer()->engine().mixer();
    const auto meter = [&](const std::string& strip) {
        return mixer.meter(audio::StripKind::Stack, strip).level_db[0];
    };
    const auto cue = audio::cue_strip_id("main");
    const auto output_id = facade.session().outputs().front().id;
    const auto output_meter = [&] {
        return mixer.meter(audio::StripKind::Output, output_id).level_db[0];
    };
    const auto key_of = [](const seventhslide::session::StackRuntime& st) {
        return seventhslide::scene::content_video_key(st.media_instance());
    };

    // Cued: on a player of its own, paused, its picture for the Preview well
    // alone.
    REQUIRE(facade.live()->playVideo(QStringLiteral("one.mp4")));
    const auto one = key_of(preview_of(t, main));
    REQUIRE(preview_of(t, main).media_player() != nullptr);
    CHECK(playback->cued_count() == 1);
    CHECK_FALSE(facade.preview()->mediaPlaying());
    REQUIRE(QTest::qWaitFor(
        [&] { return seventhslide::render::source_image(&playback->cued_frames(), one) != nullptr; },
        10000));
    CHECK(seventhslide::render::source_image(&playback->content_frames(), one) == nullptr);
    CHECK_FALSE(facade.outputPreviews()->video_frame(facade.preview()).isNull());

    // Played there: heard on the cue strip, by no stack's strip or output.
    REQUIRE(facade.preview()->mediaPlay());
    REQUIRE(QTest::qWaitFor([&] { return meter(cue) > -40.0f; }, 10000));
    REQUIRE(QTest::qWaitFor([&] { return facade.preview()->mediaPositionSec() > 0.3; }, 10000));
    CHECK(meter("main") < -60.0f);
    CHECK(output_meter() < -60.0f);

    // Taken: the same player plays on from where it was, its sound now the
    // stack's and the outputs', its picture theirs.
    auto* const player = preview_of(t, main).media_player();
    const double before = facade.preview()->mediaPositionSec();
    REQUIRE(facade.stacks()->transition(main).isEmpty());
    CHECK(t.stack(main).media_player() == player);
    CHECK(facade.program()->mediaPlaying());
    CHECK(facade.program()->mediaPositionSec() >= before);
    CHECK(playback->cued_count() == 0);
    REQUIRE(QTest::qWaitFor([&] { return meter("main") > -40.0f && output_meter() > -40.0f; },
                            10000));
    REQUIRE(QTest::qWaitFor([&] { return meter(cue) < -60.0f; }, 5000));
    CHECK(seventhslide::render::source_image(&playback->content_frames(), one) != nullptr);
    CHECK(seventhslide::render::source_image(&playback->cued_frames(), one) == nullptr);

    // Another cued, paused, and taken with a fade of its own: it plays from
    // its play head, and the one going plays on over the fade, then waits in
    // Preview where it was taken off, paused.
    REQUIRE(facade.live()->playVideo(QStringLiteral("two.mp4")));
    // Loaded before it is moved, and moved before it is taken: mpv opens a
    // file and seeks in its own time.
    REQUIRE(QTest::qWaitFor([&] { return facade.preview()->mediaDurationSec() > 7.0; }, 10000));
    REQUIRE(facade.preview()->mediaSeek(2.0));
    REQUIRE(QTest::qWaitFor(
        [&] { return std::abs(facade.preview()->mediaPositionSec() - 2.0) < 0.3; }, 10000));
    REQUIRE(QTest::qWaitFor([&] { return t.stack(main).playback().position_sec > 1.0; }, 10000));
    auto* const going = t.stack(main).media_player();
    REQUIRE(facade.stacks()
                ->transitionWith(main, QStringLiteral("fade"), 600, QString())
                .isEmpty());
    const double taken_off = preview_of(t, main).playback().position_sec;
    REQUIRE(taken_off < 5.0);
    CHECK(key_of(preview_of(t, main)) == one);
    CHECK(playback->leaving_video_count() == 1);
    CHECK(facade.program()->mediaPlaying());
    CHECK(facade.program()->mediaPositionSec() == Catch::Approx(2.0).margin(0.5));
    // While it plays out on the outputs, the Preview is given no player: its
    // well reads where it was taken off, and moving it there moves only the
    // Preview, never the picture on the outputs.
    CHECK(preview_of(t, main).media_player() == nullptr);
    CHECK_FALSE(facade.preview()->mediaPlaying());
    CHECK(facade.preview()->mediaPositionSec() == Catch::Approx(taken_off));
    CHECK(facade.preview()->mediaDurationSec() > 7.0);
    REQUIRE(facade.preview()->mediaSeek(7.0));
    CHECK(facade.preview()->mediaPositionSec() == Catch::Approx(7.0));
    CHECK(going->position_sec() < 6.0);
    CHECK(going->is_playing());
    REQUIRE(QTest::qWaitFor(
        [&] { return playback->leaving_video_count() == 0 && playback->cued_count() == 1; },
        5000));
    CHECK(preview_of(t, main).media_player() == going);
    REQUIRE(QTest::qWaitFor([&] { return !facade.preview()->mediaPlaying(); }, 5000));
    REQUIRE(QTest::qWaitFor(
        [&] { return std::abs(facade.preview()->mediaPositionSec() - 7.0) < 0.3; }, 5000));
    REQUIRE(QTest::qWaitFor([&] { return meter(cue) < -60.0f; }, 5000));

    // A cut takes it back at once, nothing left going.
    REQUIRE(facade.stacks()
                ->transitionWith(main, QStringLiteral("cut"), 0, QString())
                .isEmpty());
    CHECK(playback->leaving_video_count() == 0);
    CHECK(playback->cued_count() == 1);
    CHECK(key_of(t.stack(main)) == one);

    // Cleared, the cued one stops and lets go.
    REQUIRE(facade.stacks()->clearPreview(main).isEmpty());
    CHECK(playback->cued_count() == 0);
}

TEST_CASE("Audio played in Preview is heard only on its cue strip, and taken onto its stack",
          "[app][audio][preview]")
{
    namespace audio = seventhslide::audio;
    AudioFacade t;
    auto& facade = t.facade;
    const auto audio_dir = facade.media_root() / "audio";
    seventhslide::test::write_tone_wav(audio_dir / "long.wav", 6.0);
    facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Audio, "long.wav", "long.wav");
    facade.refresh_library();
    REQUIRE(facade.stacks()->setStackPreview(t.audio_stack, true).isEmpty());
    auto* playback = facade.playback();
    auto& mixer = facade.mixer()->engine().mixer();
    const auto stack = t.audio_stack.toStdString();
    const auto cue = audio::cue_strip_id(stack);
    const auto meter = [&](const std::string& strip) {
        return mixer.meter(audio::StripKind::Stack, strip).level_db[0];
    };
    const auto output_id = facade.session().outputs().front().id;

    // Cued: nothing plays; played there, only the cue strip hears it.
    REQUIRE(facade.live()->playAudio(QStringLiteral("long.wav")));
    REQUIRE(facade.stacks()->focusedStackId() == t.audio_stack);
    CHECK(playback->cued_count() == 1);
    CHECK(playback->audio_player_count() == 0);
    CHECK_FALSE(facade.preview()->mediaPlaying());
    REQUIRE(facade.preview()->mediaPlay());
    REQUIRE(QTest::qWaitFor([&] { return meter(cue) > -40.0f; }, 5000));
    REQUIRE(QTest::qWaitFor([&] { return facade.preview()->mediaPositionSec() > 0.2; }, 5000));
    CHECK(meter(stack) < -60.0f);
    CHECK(mixer.meter(audio::StripKind::Output, output_id).level_db[0] < -60.0f);

    // Taken: the same player on the stack's strip and the outputs.
    auto* const player = preview_of(t, t.audio_stack).media_player();
    const double before = facade.preview()->mediaPositionSec();
    REQUIRE(facade.stacks()->transition(t.audio_stack).isEmpty());
    CHECK(t.stack(t.audio_stack).media_player() == player);
    CHECK(playback->audio_player_count() == 1);
    CHECK(facade.program()->mediaPositionSec() >= before);
    REQUIRE(QTest::qWaitFor(
        [&] {
            return meter(stack) > -40.0f
                   && mixer.meter(audio::StripKind::Output, output_id).level_db[0] > -40.0f;
        },
        5000));
    REQUIRE(QTest::qWaitFor([&] { return meter(cue) < -60.0f; }, 5000));

    // Taken back by a cut: paused in Preview where it was taken off.
    REQUIRE(facade.live()->playAudio(QStringLiteral("bed.wav")));
    REQUIRE(facade.stacks()
                ->transitionWith(t.audio_stack, QStringLiteral("cut"), 0, QString())
                .isEmpty());
    CHECK(playback->cued_count() == 1);
    REQUIRE(QTest::qWaitFor([&] { return !facade.preview()->mediaPlaying(); }, 5000));
    CHECK(preview_of(t, t.audio_stack).media_player() == player);
}

TEST_CASE("A take onto a stack under another stack's live video leaves both playing",
          "[app][mpv][preview]")
{
    namespace audio = seventhslide::audio;
    AudioFacade t;
    auto& facade = t.facade;
    add_videos(t, {"back.mp4", "front.mp4"});
    REQUIRE(facade.stacks()->addStack(QStringLiteral("Front"), QStringLiteral("info")).isEmpty());
    const auto front =
        facade.stacks()->stacks().back().toMap().value(QStringLiteral("id")).toString();
    const auto main = QStringLiteral("main");
    REQUIRE(facade.stacks()->setStackPreview(main, true).isEmpty());
    auto& mixer = facade.mixer()->engine().mixer();
    const auto meter = [&](const QString& strip) {
        return mixer.meter(audio::StripKind::Stack, strip.toStdString()).level_db[0];
    };

    facade.stacks()->armSendTo(front);
    REQUIRE(facade.live()->playVideo(QStringLiteral("front.mp4")));
    facade.stacks()->armSendTo(main);
    REQUIRE(facade.live()->playVideo(QStringLiteral("back.mp4")));
    REQUIRE(facade.preview()->mediaPlay());
    REQUIRE(facade.stacks()->transition(main).isEmpty());

    REQUIRE(t.stack(main).media_player() != nullptr);
    REQUIRE(t.stack(front).media_player() != nullptr);
    CHECK(facade.playback()->cued_count() == 0);
    REQUIRE(QTest::qWaitFor([&] { return meter(main) > -40.0f && meter(front) > -40.0f; },
                            10000));
    const auto picture_of = [&](const QString& stack) {
        const auto key = seventhslide::scene::content_video_key(t.stack(stack).media_instance());
        return seventhslide::render::source_image(&facade.playback()->content_frames(), key);
    };
    REQUIRE(QTest::qWaitFor(
        [&] { return picture_of(main) != nullptr && picture_of(front) != nullptr; }, 10000));
}

TEST_CASE("An audio take comes in and goes back as its output's fades say",
          "[app][audio][preview]")
{
    AudioFacade t;
    auto& facade = t.facade;
    seventhslide::test::write_tone_wav(facade.media_root() / "audio" / "long.wav", 6.0);
    facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Audio, "long.wav", "long.wav");
    facade.refresh_library();
    set_output_fades(facade, QStringLiteral("Main"), 300, 600);
    REQUIRE(facade.stacks()->setStackPreview(t.audio_stack, true).isEmpty());
    auto* playback = facade.playback();
    const auto stack = t.audio_stack.toStdString();
    const auto cut = [&] {
        return facade.stacks()
            ->transitionWith(t.audio_stack, QStringLiteral("cut"), 0, QString())
            .isEmpty();
    };

    // Taken by a cut, it still comes up over its fade in, as it would sent
    // there.
    REQUIRE(facade.live()->playAudio(QStringLiteral("long.wav")));
    REQUIRE(facade.preview()->mediaPlay());
    REQUIRE(cut());
    CHECK(playback->audio_level(stack) < 1.0f);
    REQUIRE(QTest::qWaitFor([&] { return playback->audio_level(stack) == 1.0f; }, 3000));

    // Sent back by a cut, it fades out first, then waits in Preview.
    REQUIRE(facade.live()->playAudio(QStringLiteral("lobby.wav")));
    REQUIRE(cut());
    CHECK(playback->leaving_count() == 1);
    REQUIRE(QTest::qWaitFor(
        [&] { return playback->leaving_count() == 0 && playback->cued_count() == 1; }, 3000));
    REQUIRE(QTest::qWaitFor([&] { return !facade.preview()->mediaPlaying(); }, 3000));
}

namespace {

/// Output ``name``'s id.
std::string output_id(SessionFacade& facade, const QString& name)
{
    const auto* oc = seventhslide::model::find_output_by_name(facade.session().outputs(),
                                                              name.toStdString());
    REQUIRE(oc != nullptr);
    return oc->id;
}

}  // namespace

TEST_CASE("Each output fades an audio item by its own fades", "[app][audio][transition]")
{
    AudioFacade t;
    auto& facade = t.facade;
    seventhslide::test::write_tone_wav(facade.media_root() / "audio" / "long.wav", 6.0);
    facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Audio, "long.wav", "long.wav");
    facade.refresh_library();
    const auto stage = add_output(facade, QStringLiteral("Stage"));
    // The room cuts; the stage brings it up over 400 ms and down over 800 ms.
    set_output_fades(facade, QStringLiteral("Main"), 0, 0);
    set_output_fades(facade, stage, 400, 800);
    const auto main_id = output_id(facade, QStringLiteral("Main"));
    const auto stage_id = output_id(facade, stage);
    auto& mixer = facade.mixer()->engine().mixer();
    OutputListener main_ear(mixer, main_id);
    OutputListener stage_ear(mixer, stage_id);
    auto* playback = facade.playback();
    const auto stack = t.audio_stack.toStdString();

    // In: at once on Main, from nothing on Stage. The stack's strip hears it
    // at the louder.
    REQUIRE(facade.live()->playAudio(QStringLiteral("long.wav")));
    CHECK(playback->audio_level(stack, main_id) == 1.0f);
    CHECK(playback->audio_level(stack, stage_id) < 1.0f);
    CHECK(playback->audio_level(stack) == 1.0f);
    REQUIRE(QTest::qWaitFor([&] { return playback->audio_level(stack, stage_id) == 1.0f; }, 3000));
    REQUIRE(QTest::qWaitFor([&] { return main_ear.heard() && stage_ear.heard(); }, 5000));

    // Cleared: gone from Main at once, fading on Stage, and let go once
    // Stage's fade is over.
    REQUIRE(facade.stacks()->clearStack(t.audio_stack).isEmpty());
    CHECK(playback->leaving_count() == 1);
    QTest::qWait(50);
    main_ear.reset();
    stage_ear.reset();
    QTest::qWait(150);
    CHECK_FALSE(main_ear.heard());
    CHECK(stage_ear.heard());
    REQUIRE(QTest::qWaitFor([&] { return playback->leaving_count() == 0; }, 3000));
}

TEST_CASE("A video's sound goes with its picture on each output, over that output's change",
          "[app][mpv][transition]")
{
    AudioFacade t;
    auto& facade = t.facade;
    const auto media = facade.media_root();
    std::filesystem::create_directories(media / "videos");
    std::filesystem::copy_file(std::filesystem::path(SEVENTHSLIDE_FIXTURES_DIR) / "video"
                                   / "tone_3366cc_64x36_8s.mp4",
                               media / "videos" / "clip.mp4");
    facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Video, "clip.mp4", "clip.mp4");
    facade.refresh_library();
    const auto stage = add_output(facade, QStringLiteral("Stage"));
    // What replaces the video: a song, which comes in over a fade on Main and
    // cuts on Stage, each by its own output's Song theme.
    const auto set_song_transition = [&](const QString& output, const char* type, int ms) {
        const QVariantMap change{{QStringLiteral("type"), QString::fromLatin1(type)},
                                 {QStringLiteral("duration_ms"), ms}};
        for (const auto& entry : facade.themes()->list(output, QStringLiteral("song"))) {
            const auto theme = entry.toMap();
            const auto id = theme.value(QStringLiteral("id")).toString();
            auto style = facade.themes()->theme_map(output, QStringLiteral("song"), id)
                             .value(QStringLiteral("style"))
                             .toMap();
            style.insert(QStringLiteral("transitions"),
                         QVariantMap{{QStringLiteral("song"), change}});
            REQUIRE(facade.themes()
                        ->update_theme(output, QStringLiteral("song"), id,
                                       theme.value(QStringLiteral("name")).toString(),
                                       seventhslide::app::to_json(QVariant(style)))
                        .isEmpty());
        }
    };
    set_song_transition(QStringLiteral("Main"), "fade", 800);
    set_song_transition(stage, "cut", 0);
    auto& mixer = facade.mixer()->engine().mixer();
    OutputListener main_ear(mixer, output_id(facade, QStringLiteral("Main")));
    OutputListener stage_ear(mixer, output_id(facade, stage));
    auto* playback = facade.playback();

    REQUIRE(facade.live()->playVideo(QStringLiteral("clip.mp4")));
    REQUIRE(QTest::qWaitFor([&] { return main_ear.heard() && stage_ear.heard(); }, 10000));

    // The song comes in: the video's sound goes out with its picture on
    // Main, and at once on Stage, where the song cuts in.
    auto song = demo_song();
    REQUIRE(facade.live()->playSong(facade.song_repo()->insert(song)));
    CHECK(playback->leaving_video_count() == 1);
    QTest::qWait(50);
    main_ear.reset();
    stage_ear.reset();
    QTest::qWait(150);
    CHECK(main_ear.heard());
    CHECK_FALSE(stage_ear.heard());
    REQUIRE(QTest::qWaitFor([&] { return playback->leaving_video_count() == 0; }, 3000));
}

TEST_CASE("A service's fades hold over each output's, and an audio file's over both",
          "[app][audio][service]")
{
    AudioFacade t;
    auto& facade = t.facade;
    seventhslide::test::write_tone_wav(facade.media_root() / "audio" / "long.wav", 6.0);
    facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Audio, "long.wav", "long.wav");
    facade.refresh_library();
    const auto stage = add_output(facade, QStringLiteral("Stage"));
    set_output_fades(facade, QStringLiteral("Main"), 0, 0);
    set_output_fades(facade, stage, 0, 900);
    const auto main_id = output_id(facade, QStringLiteral("Main"));
    const auto stage_id = output_id(facade, stage);
    const auto stack = t.audio_stack.toStdString();
    const auto on = [&] { return facade.session().audio_fades_on(stack); };
    using seventhslide::model::AudioFades;
    auto* library = facade.library();
    auto* media = facade.media();
    const auto service = library->createService(QStringLiteral("Sunday"));
    REQUIRE(service > 0);
    REQUIRE(media->addAudioToService(QStringLiteral("long.wav")));
    REQUIRE(facade.live()->selectServiceItem(0));
    CHECK(on().at(main_id) == AudioFades{});
    CHECK(on().at(stage_id) == AudioFades{0, 900, 0});

    // The open service's fade in, on every output; the fade out still each
    // output's. Left empty, a field is the outputs'.
    REQUIRE(library->setServiceAudioFades(service, {{QStringLiteral("fade_in_ms"), 500}})
                .isEmpty());
    CHECK(library->serviceDetails(service).value(QStringLiteral("audioFades")).toMap()
          == QVariantMap{{QStringLiteral("fade_in_ms"), 500}});
    CHECK(on().at(main_id) == AudioFades{500, 0, 0});
    CHECK(on().at(stage_id) == AudioFades{500, 900, 0});

    // The file's own over the service's, wherever it plays.
    REQUIRE(media->setAudioFileFades(QStringLiteral("long.wav"),
                                     {{QStringLiteral("fade_in_ms"), 0},
                                      {QStringLiteral("fade_out_ms"), 2000}})
                .isEmpty());
    CHECK(media->audioFileFades(QStringLiteral("long.wav"))
          == QVariantMap{{QStringLiteral("fade_in_ms"), 0}, {QStringLiteral("fade_out_ms"), 2000}});
    CHECK(on().at(main_id) == AudioFades{0, 2000, 0});
    CHECK(on().at(stage_id) == AudioFades{0, 2000, 0});
    // It comes in as they say.
    auto* playback = facade.playback();
    REQUIRE(QTest::qWaitFor([&] { return playback->audio_level(stack, stage_id) == 1.0f; }, 3000));

    // Each given back: the outputs' again.
    REQUIRE(media->setAudioFileFades(QStringLiteral("long.wav"), {}).isEmpty());
    REQUIRE(library->setServiceAudioFades(service, {}).isEmpty());
    CHECK(media->audioFileFades(QStringLiteral("long.wav")).isEmpty());
    CHECK(on().at(stage_id) == AudioFades{0, 900, 0});

    // Another service open has none of this one's.
    REQUIRE(library->setServiceAudioFades(service, {{QStringLiteral("overlap_ms"), 700}})
                .isEmpty());
    CHECK(on().at(main_id).overlap_ms == 700);
    REQUIRE(library->createService(QStringLiteral("Evening")) > 0);
    CHECK(on().at(main_id).overlap_ms == 0);
    CHECK_FALSE(media->setAudioFileFades(QStringLiteral("nothing.wav"), {}).isEmpty());
}

TEST_CASE("An audio file's own fades bring it in and out through a Preview's take",
          "[app][audio][preview][file-fades]")
{
    AudioFacade t;
    auto& facade = t.facade;
    seventhslide::test::write_tone_wav(facade.media_root() / "audio" / "long.wav", 8.0);
    facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Audio, "long.wav", "long.wav");
    facade.refresh_library();
    // The output cuts; only the file says fade.
    set_output_fades(facade, QStringLiteral("Main"), 0, 0);
    REQUIRE(facade.media()
                ->setAudioFileFades(QStringLiteral("long.wav"),
                                    {{QStringLiteral("fade_in_ms"), 1500},
                                     {QStringLiteral("fade_out_ms"), 1500}})
                .isEmpty());
    REQUIRE(facade.stacks()->setStackPreview(t.audio_stack, true).isEmpty());
    const auto main_id = output_id(facade, QStringLiteral("Main"));
    const auto stack = t.audio_stack.toStdString();
    auto* playback = facade.playback();

    // Cued from the Library, played there, then taken.
    REQUIRE(facade.live()->playAudio(QStringLiteral("long.wav")));
    REQUIRE(facade.preview()->mediaPlay());
    REQUIRE(facade.stacks()
                ->transitionWith(t.audio_stack, QStringLiteral("cut"), 0, QString())
                .isEmpty());
    const auto on = facade.session().audio_fades_on(stack);
    INFO("fade in " << on.at(main_id).fade_in_ms);
    CHECK(on.at(main_id).fade_in_ms == 1500);
    QTest::qWait(150);
    const float early = playback->audio_level(stack, main_id);
    INFO("level 150 ms in " << early);
    CHECK(early < 0.5f);
    REQUIRE(QTest::qWaitFor([&] { return playback->audio_level(stack, main_id) == 1.0f; }, 4000));

    // Cleared: it goes over the file's fade out, not at once.
    REQUIRE(facade.stacks()->clearStack(t.audio_stack).isEmpty());
    CHECK(playback->leaving_count() == 1);
    QTest::qWait(150);
    CHECK(playback->leaving_count() == 1);
    REQUIRE(QTest::qWaitFor([&] { return playback->leaving_count() == 0; }, 4000));

    // Played straight to the Program (Also play), the same.
    REQUIRE(facade.live()->playAudio(QStringLiteral("long.wav"), false, true));
    QTest::qWait(150);
    CHECK(playback->audio_level(stack, main_id) < 0.5f);
}

TEST_CASE("Play and Pause fade an audio item when its fades say so", "[app][audio][transport]")
{
    AudioFacade t;
    auto& facade = t.facade;
    seventhslide::test::write_tone_wav(facade.media_root() / "audio" / "long.wav", 8.0);
    facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Audio, "long.wav", "long.wav");
    facade.refresh_library();
    const auto main_id = output_id(facade, QStringLiteral("Main"));
    const auto stack = t.audio_stack.toStdString();
    auto* playback = facade.playback();
    auto& mixer = facade.mixer()->engine().mixer();
    OutputListener ear(mixer, main_id);
    const auto set_fades = [&](bool on_play_pause) {
        REQUIRE(facade.outputSettings()
                    ->updateOutputSettings(
                        QStringLiteral("Main"),
                        {{QStringLiteral("audioFades"),
                          QVariantMap{{QStringLiteral("fade_in_ms"), 400},
                                      {QStringLiteral("fade_out_ms"), 600},
                                      {QStringLiteral("on_play_pause"), on_play_pause}}}})
                    .value(QStringLiteral("error"))
                    .toString()
                    .isEmpty());
    };
    set_fades(true);
    REQUIRE(facade.live()->playAudio(QStringLiteral("long.wav")));
    facade.stacks()->setFocusedStackId(t.audio_stack);
    REQUIRE(QTest::qWaitFor([&] { return playback->audio_level(stack, main_id) == 1.0f; }, 3000));

    // Pause: the transport shows it paused at once, and it goes down over
    // the fade out, still heard, then stops.
    REQUIRE(facade.program()->mediaPause());
    CHECK_FALSE(facade.program()->mediaPlaying());
    QTest::qWait(100);
    ear.reset();
    QTest::qWait(100);
    CHECK(ear.heard());
    CHECK(playback->audio_level(stack, main_id) < 1.0f);
    CHECK(playback->audio_level(stack, main_id) > 0.0f);
    CHECK_FALSE(facade.program()->mediaPlaying());
    REQUIRE(QTest::qWaitFor([&] { return playback->audio_level(stack, main_id) == 0.0f; }, 3000));
    QTest::qWait(100);
    const double held = facade.program()->mediaPositionSec();
    QTest::qWait(150);
    CHECK(facade.program()->mediaPositionSec() == held);

    // Play: up from nothing over the fade in, where it was paused.
    REQUIRE(facade.program()->mediaPlay());
    CHECK(facade.program()->mediaPlaying());
    QTest::qWait(100);
    CHECK(playback->audio_level(stack, main_id) < 1.0f);
    REQUIRE(QTest::qWaitFor([&] { return playback->audio_level(stack, main_id) == 1.0f; }, 3000));
    CHECK(facade.program()->mediaPositionSec() >= held);

    // Play while a Pause is fading out: back up from where it got to.
    REQUIRE(facade.program()->mediaPause());
    QTest::qWait(150);
    REQUIRE(facade.program()->mediaPlay());
    CHECK(facade.program()->mediaPlaying());
    REQUIRE(QTest::qWaitFor([&] { return playback->audio_level(stack, main_id) == 1.0f; }, 3000));
    // The Pause it was fading to is gone: it plays on past when that would
    // have stopped it.
    const double resumed = facade.program()->mediaPositionSec();
    REQUIRE(QTest::qWaitFor([&] { return facade.program()->mediaPositionSec() > resumed + 0.8; },
                            3000));
    CHECK(facade.program()->mediaPlaying());

    // Switched off: Pause is at once.
    set_fades(false);
    REQUIRE(facade.program()->mediaPause());
    QTest::qWait(60);
    ear.reset();
    QTest::qWait(100);
    CHECK_FALSE(ear.heard());
    CHECK(playback->audio_level(stack, main_id) == 1.0f);
}
