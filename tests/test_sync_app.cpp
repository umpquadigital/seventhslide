// Sync in the app (sync_controller.hpp): two computers, each a session with
// its own data folder and its controller, on this computer's addresses.
// Putting a library online, joining it by an invite (through a restart, with
// the inviting computer off at first), an edit reaching the other window, the
// pause while something is live, a viewer's read-only library, stopping, and
// the Sync page's password cards.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "facade_all.hpp"
#include "font_controller.hpp"
#include "font_fixtures.hpp"
#include "operator_ui.hpp"
#include "song_list_model.hpp"
#include "sync_controller.hpp"
#include "sync_test_helpers.hpp"
#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"
#include "ui_test_helpers.hpp"
#include "test_wait.hpp"

#include <seventhslide/db/config_repository.hpp>
#include <seventhslide/db/database.hpp>
#include <seventhslide/db/font_repository.hpp>
#include <seventhslide/db/sync_outbox.hpp>
#include <seventhslide/library/library.hpp>
#include <seventhslide/sync/member_list.hpp>

#include <QElapsedTimer>
#include <QImage>
#include <QQuickItem>
#include <QScopeGuard>
#include <QQuickWindow>
#include <QSGRendererInterface>

#include <iostream>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTcpServer>
#include <QUrl>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <thread>

using Catch::Matchers::ContainsSubstring;
using seventhslide::app::SessionFacade;
using seventhslide::app::SyncController;
using seventhslide::test::run_events_for;
using seventhslide::test::wait_until;

namespace {

constexpr const char* k_password = "lilies of the field";

/// A computer: its data folder, its window's session, its sync.
struct AppComputer {
    seventhslide::test::QtBootstrap qt;
    seventhslide::test::TempDir dir;
    SessionFacade facade;
    std::unique_ptr<SyncController> sync;
    std::vector<std::pair<QString, QVariantMap>> results;
    /// The moment syncing waits after the last live item (none, unless a test
    /// is about it).
    int live_grace_ms = 0;
    /// How long Sync Now looks for computers not connected.
    int sync_now_reach_ms = 300;

    /// Started as SeventhSlide starts (main.cpp): the libraries listed, then
    /// sync, then the active library opened.
    explicit AppComputer(const QString& name)
    {
        facade.load_libraries(dir.path);
        start();
        REQUIRE(facade.open_active_library());
        // These count what goes between computers: the shipped fonts' files
        // would be most of it.
        seventhslide::db::FontRepository(*facade.open_library().database()).note_bundled_taken();
        REQUIRE(sync->setComputerName(name).isEmpty());
    }

    /// How SeventhSlide's sync is started here: taking connections at
    /// ``port`` (any free one when 0).
    [[nodiscard]] SyncController::Options options(quint16 port = 0) const
    {
        SyncController::Options o;
        o.data_dir = dir.path;
        o.announce = false;  // this computer's network hears nothing of the tests
        o.port = port;
        o.box_params = seventhslide::crypto::k_box_params_quick;
        o.leave_wait_ms = 3000;
        o.live_grace_ms = live_grace_ms;
        o.sync_now_quiet_ms = 300;
        o.sync_now_reach_ms = sync_now_reach_ms;
        return o;
    }

    /// Its sync started, or started again as SeventhSlide would be
    /// (``options``).
    void start(quint16 port = 0)
    {
        sync.reset();
        sync = std::make_unique<SyncController>(facade, options(port));
        QObject::connect(sync.get(), &SyncController::finished,
                         [this](const QString& action, const QVariantMap& result) {
                             results.emplace_back(action, result);
                         });
    }

    seventhslide::db::Database& db() { return *facade.open_library().database(); }
    std::string text(const std::string& sql)
    {
        QSqlQuery q(db().handle());
        REQUIRE(q.exec(QString::fromStdString(sql)));
        REQUIRE(q.next());
        return q.value(0).toString().toStdString();
    }
    /// The result of the next ``action`` to finish.
    QVariantMap await(const QString& action)
    {
        const auto first = results.size();
        std::optional<QVariantMap> got;
        wait_until([&] {
            for (auto i = first; i < results.size(); ++i) {
                if (results[i].first == action) {
                    got = results[i].second;
                    return true;
                }
            }
            return false;
        });
        REQUIRE(got.has_value());
        return *got;
    }
    QVariantMap state() { return sync->libraryStates().value(facade.libraries()->activeLibraryId()).toMap(); }
    QString state_of(const QString& id) const
    {
        return sync->libraryStates().value(id).toMap().value(QStringLiteral("state")).toString();
    }
    std::string library_uid() { return text("SELECT value FROM library_meta WHERE key = 'library_uid'"); }
};

/// Whether anything of ``c``'s is on its way, or a sync of its open
/// library due or running.
bool busy(AppComputer& c)
{
    const auto* open = c.sync->open_sync();
    return c.sync->link().busy() || (open != nullptr && open->busy());
}

/// Settle both: nothing on its way, no sync due, for a moment.
void settle(AppComputer& a, AppComputer& b)
{
    REQUIRE(seventhslide::test::settle_while([&] { return busy(a) || busy(b); }));
}

/// How many changes ``c``'s open library has put in change files to send.
std::int64_t changes_sent(AppComputer& c)
{
    return c.sync->open_sync()->report().changes_sent;
}

/// Songs and a service, as an operator would have made them.
void fill(seventhslide::db::Database& db)
{
    db.exec("INSERT INTO songs (title, lyrics, key) VALUES ('Amazing Grace', '[V1]\nline', 'G')");
    db.exec("INSERT INTO songs (title, lyrics) VALUES ('It Is Well', '[V1]\nline')");
    db.exec("INSERT INTO services (name) VALUES ('Sabbath')");
    db.exec("INSERT INTO service_items (service_id, song_id, order_num, item_type) "
            "SELECT s.id, g.id, 0, 'song' FROM services s, songs g WHERE g.title = 'Amazing Grace'");
}

/// The church's library online, with something in it.
void put_online(AppComputer& church)
{
    fill(church.db());
    church.facade.refresh_library();
    REQUIRE(church.sync->putOnline(k_password).isEmpty());
    const auto online = church.await(QStringLiteral("putOnline"));
    REQUIRE(online.value(QStringLiteral("ok")).toBool());
    CHECK(online.value(QStringLiteral("recoveryCode")).toString().size() > 40);
}

QString q_machine(AppComputer& computer)
{
    return QString::fromStdString(computer.sync->node().identity().machine);
}

/// ``joiner`` joins the church's open library as ``role``, and opens it.
void join(AppComputer& church, AppComputer& joiner, const QString& role)
{
    REQUIRE(church.sync->admin()->invite(role, 7, k_password).isEmpty());
    const auto invite = church.await(QStringLiteral("invite"));
    REQUIRE(invite.value(QStringLiteral("ok")).toBool());
    const auto code = invite.value(QStringLiteral("code")).toString();
    CHECK_THAT(joiner.sync->readInvite(code).value(QStringLiteral("text")).toString().toStdString(),
               ContainsSubstring(role == QStringLiteral("viewer") ? "as a viewer" : "as an editor"));
    const auto before = joiner.facade.libraries()->activeLibraryId();
    REQUIRE(joiner.sync->joinLibrary(code).isEmpty());
    REQUIRE(joiner.await(QStringLiteral("join")).value(QStringLiteral("ok")).toBool());
    // It opens once it is all here.
    REQUIRE(wait_until([&] { return joiner.facade.libraries()->activeLibraryId() != before; }, 30000));
}

}  // namespace

TEST_CASE("A library put online here is joined on another computer, and edits reach its window",
          "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    AppComputer laptop(QStringLiteral("Laptop"));
    CHECK_FALSE(church.sync->openLibrary().value(QStringLiteral("synced")).toBool());
    put_online(church);
    const auto open = church.sync->openLibrary();
    CHECK(open.value(QStringLiteral("synced")).toBool());
    CHECK(open.value(QStringLiteral("role")).toString() == QStringLiteral("editor"));

    join(church, laptop, QStringLiteral("editor"));
    seventhslide::test::check_same_records(laptop.db(), church.db());
    CHECK(laptop.facade.library()->songs()->rowCount() == church.facade.library()->songs()->rowCount());
    CHECK_FALSE(laptop.facade.readOnly());
    CHECK(laptop.facade.libraries()->activeLibraryName() == church.facade.libraries()->activeLibraryName());

    // A song added in the church's window shows in the laptop's list.
    const auto songs = laptop.facade.library()->songs()->rowCount();
    church.db().exec("INSERT INTO songs (title, lyrics) VALUES ('How Great Thou Art', '[V1]\nline')");
    REQUIRE(wait_until([&] { return laptop.facade.library()->songs()->rowCount() == songs + 1; }));
    CHECK(laptop.state().value(QStringLiteral("state")).toString() == QStringLiteral("uptodate"));

    // Both see each other on the list.
    const auto computers = church.sync->openLibrary().value(QStringLiteral("computers")).toList();
    REQUIRE(computers.size() == 2);
    bool laptop_connected = false;
    for (const auto& c : computers) {
        const auto m = c.toMap();
        if (m.value(QStringLiteral("name")).toString() == QStringLiteral("Laptop")) {
            laptop_connected = m.value(QStringLiteral("connected")).toBool();
        }
    }
    CHECK(laptop_connected);
}

TEST_CASE("A library joined here opens only once all of it is here, and adds nothing of its own",
          "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    AppComputer laptop(QStringLiteral("Laptop"));
    fill(church.db());
    church.facade.refresh_library();
    // A large file: the change file it goes in (with the library's outputs
    // and settings) waits on the laptop until the file itself has come.
    const auto source = church.dir.path / "Prelude in G.mp3";
    std::ofstream(source, std::ios::binary) << std::string(24 * 1000 * 1000, 'x');
    QSignalSpy imported(church.facade.media(), &seventhslide::app::MediaController::importFinished);
    church.facade.media()->import_files(seventhslide::db::MediaKind::Audio,
                                       {QString::fromStdString(source.string())}, -1);
    REQUIRE(wait_until([&] { return imported.size() == 1; }));
    REQUIRE(church.sync->putOnline(k_password).isEmpty());
    REQUIRE(church.await(QStringLiteral("putOnline")).value(QStringLiteral("ok")).toBool());
    REQUIRE(church.facade.session().outputs().size() == 1);
    const auto main_id = church.facade.session().outputs().front().id;

    REQUIRE(church.sync->admin()->invite(QStringLiteral("editor"), 7, k_password).isEmpty());
    const auto code = church.await(QStringLiteral("invite")).value(QStringLiteral("code")).toString();
    // The change file is not held for the file for long; joining waits all
    // the same. The file comes no further for now (the church's thread for
    // reading files through, held).
    laptop.sync->link().set_hold_for_media(200);
    std::atomic<bool> let_go = false;
    church.sync->link().shared().checks.start([&] {
        while (!let_go) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
    const auto release = qScopeGuard([&] { let_go = true; });
    REQUIRE(laptop.sync->joinLibrary(code).isEmpty());
    const auto joined = laptop.await(QStringLiteral("join"));
    REQUIRE(joined.value(QStringLiteral("ok")).toBool());
    const auto id = joined.value(QStringLiteral("id")).toString();
    const auto* entry = laptop.facade.open_library().registry()->find(id.toStdString());
    REQUIRE(entry != nullptr);
    const auto mark = seventhslide::library::sync_root(entry->root) / "arriving";
    CHECK(std::filesystem::exists(mark));
    // Well past the hold: still not opened, with its file still to come.
    run_events_for(1500);
    CHECK_FALSE(laptop.facade.libraries()->switchLibrary(id).isEmpty());
    let_go = true;

    // The operator opens it at the first chance: by then all of it is here,
    // the church's output among it.
    REQUIRE(wait_until([&] { return laptop.facade.libraries()->switchLibrary(id).isEmpty(); }, 60000));
    REQUIRE(laptop.facade.session().outputs().size() == 1);
    CHECK(laptop.facade.session().outputs().front().id == main_id);
    const auto media_path = church.text("SELECT path FROM sync_media");
    CHECK(std::filesystem::file_size(seventhslide::library::media_root(entry->root) / media_path)
          == 24 * 1000 * 1000);
    CHECK_FALSE(std::filesystem::exists(mark));

    // Nothing of the laptop's own went to the church.
    laptop.sync->syncNow();
    church.sync->syncNow();
    run_events_for(2000);
    CHECK(church.text("SELECT COUNT(*) FROM outputs") == "1");
    CHECK(laptop.text("SELECT COUNT(*) FROM outputs") == "1");
}

TEST_CASE("The bar shows how the open library syncs, and opens the Sync page", "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    AppComputer laptop(QStringLiteral("Laptop"));
    seventhslide::test::OperatorUi ui(church.facade, QStringLiteral("songs"), church.sync.get());
    REQUIRE(ui.window() != nullptr);
    QObject* bar = ui.find_component("AppBar");
    REQUIRE(bar != nullptr);
    auto* button = ui.window()->findChild<QQuickItem*>(QStringLiteral("syncButton"));
    REQUIRE(button != nullptr);
    const auto label = [&] { return bar->property("syncLabel").toString(); };

    // A library of this computer's own: no sync to show.
    CHECK_FALSE(button->isVisible());

    // Online, with no other computer yet.
    put_online(church);
    REQUIRE(wait_until([&] { return button->isVisible(); }));
    REQUIRE(wait_until([&] { return label() == QStringLiteral("Not connected"); }));

    // Live sits to its right.
    auto* live = ui.window()->findChild<QQuickItem*>(QStringLiteral("liveButton"));
    REQUIRE(live != nullptr);
    REQUIRE(wait_until([&] { return live->x() >= button->x() + button->width(); }));

    // Joined, and all through.
    join(church, laptop, QStringLiteral("editor"));
    REQUIRE(wait_until([&] { return label() == QStringLiteral("Up to date"); }, 30000));
    CHECK_FALSE(bar->property("syncing").toBool());

    // A click opens Settings on its Sync page.
    REQUIRE(QMetaObject::invokeMethod(button, "clicked"));
    QObject* dialog = ui.find_component("AppSettingsDialog");
    REQUIRE(dialog != nullptr);
    CHECK(dialog->property("open").toBool());
    CHECK(dialog->property("tab").toString() == QStringLiteral("sync"));
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

// This computer's picture is read as the library's pictures are, so its
// chooser offers the files the library takes, and an SVG is one.
TEST_CASE("This computer's picture can be any picture the library takes", "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    seventhslide::test::OperatorUi ui(church.facade, QStringLiteral("songs"), church.sync.get());
    QObject* dialog = ui.find_component("AppSettingsDialog");
    REQUIRE(dialog != nullptr);
    QObject* chooser = dialog->findChild<QObject*>(QStringLiteral("syncPictureDialog"));
    REQUIRE(chooser != nullptr);
    const QString images = church.facade.media()->mediaFilePatterns(seventhslide::app::MediaKindQml::Image);
    CHECK(chooser->property("nameFilters").toStringList()
          == QStringList{QStringLiteral("Images (%1)").arg(images), QStringLiteral("All files (*)")});
    CHECK(images.contains(QStringLiteral("*.svg")));

    const auto svg = church.dir.path / "face.svg";
    std::ofstream(svg) << "<svg xmlns='http://www.w3.org/2000/svg' width='64' height='64'>"
                          "<circle cx='32' cy='32' r='30' fill='#2fb3a0'/></svg>";
    REQUIRE(church.sync->setComputerPictureFile(QUrl::fromLocalFile(QString::fromStdString(svg.string())).toString())
                .isEmpty());
    CHECK_FALSE(church.sync->computerPicture().value(QStringLiteral("image")).toString().isEmpty());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("The bar opens the Collaboration window while the open library syncs", "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    AppComputer laptop(QStringLiteral("Laptop"));
    seventhslide::test::OperatorUi ui(church.facade, QStringLiteral("songs"), church.sync.get());
    REQUIRE(ui.window() != nullptr);
    auto* button = ui.window()->findChild<QQuickItem*>(QStringLiteral("collaborationButton"));
    REQUIRE(button != nullptr);
    QObject* window = ui.find_component("CollaborationWindow");
    REQUIRE(window != nullptr);

    // A library of this computer's own: no one to collaborate with.
    CHECK_FALSE(button->isVisible());

    put_online(church);
    join(church, laptop, QStringLiteral("editor"));
    REQUIRE(wait_until([&] { return button->isVisible(); }));
    CHECK_FALSE(window->property("open").toBool());
    REQUIRE(QMetaObject::invokeMethod(button, "clicked"));
    CHECK(window->property("open").toBool());

    // It goes when the library stops syncing, and so does the window.
    REQUIRE(church.sync->stopSyncing(church.facade.libraries()->activeLibraryId()).isEmpty());
    REQUIRE(church.await(QStringLiteral("stopSyncing")).value(QStringLiteral("ok")).toBool());
    REQUIRE(wait_until([&] { return !button->isVisible(); }));
    CHECK_FALSE(window->property("open").toBool());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("A library's fonts reach a computer joining it, and one removed stays removed", "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    AppComputer laptop(QStringLiteral("Laptop"));
    using seventhslide::db::FontRepository;
    // Taken as a new library takes them, which AppComputer leaves out.
    seventhslide::test::forget_bundled_taken(church.db());
    church.facade.fonts()->library_opened();
    REQUIRE(wait_until([&] { return FontRepository(church.db()).bundled_taken(); }));
    REQUIRE(church.facade.fonts()->removeFamily(QStringLiteral("Bitter")).isEmpty());

    put_online(church);
    join(church, laptop, QStringLiteral("editor"));
    REQUIRE(wait_until([&] { return FontRepository(laptop.db()).list_family("Carlito").size() == 4; },
                       30000));

    // Opened there, it does not take them again.
    laptop.facade.fonts()->library_opened();
    QTest::qWait(100);
    CHECK(FontRepository(laptop.db()).list_family("Bitter").empty());
    CHECK(FontRepository(laptop.db()).bundled_taken());
}

TEST_CASE("A computer joining a library has the fonts the others have: it copies in none of the "
          "shipped ones",
          "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    AppComputer laptop(QStringLiteral("Laptop"));
    using seventhslide::db::FontRepository;
    // Made where a SeventhSlide that shipped no fonts runs: no shipped fonts,
    // and not even the note that they were taken.
    church.facade.fonts()->set_may_take_shipped([] { return false; });
    seventhslide::test::forget_bundled_taken(church.db());
    put_online(church);
    join(church, laptop, QStringLiteral("editor"));
    settle(church, laptop);
    REQUIRE(FontRepository(church.db()).list_all().empty());

    // Opened there, as on every start, it takes none.
    laptop.facade.fonts()->library_opened();
    QTest::qWait(300);
    settle(church, laptop);
    CHECK(FontRepository(laptop.db()).list_all().empty());
    CHECK_FALSE(FontRepository(laptop.db()).bundled_taken());
    CHECK(FontRepository(church.db()).list_all().empty());
}

TEST_CASE("A file on its way says so at both ends: nothing is up to date yet", "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    AppComputer laptop(QStringLiteral("Laptop"));
    put_online(church);
    join(church, laptop, QStringLiteral("viewer"));
    settle(church, laptop);
    REQUIRE(wait_until([&] { return laptop.state().value(QStringLiteral("state")).toString() == QStringLiteral("uptodate"); }));

    // The church's thread for reading files through, held: the file stays on its way.
    std::atomic<bool> let_go = false;
    std::atomic<bool> holding = false;
    church.sync->link().shared().checks.start([&] {
        holding = true;
        while (!let_go) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
    const auto release = qScopeGuard([&] { let_go = true; });
    REQUIRE(wait_until([&] { return holding.load(); }));

    const auto source = church.dir.path / "Prelude in G.mp3";
    std::ofstream(source, std::ios::binary) << std::string(1000 * 1000, 'x');
    QSignalSpy imported(church.facade.media(), &seventhslide::app::MediaController::importFinished);
    church.facade.media()->import_files(seventhslide::db::MediaKind::Audio,
                                       {QString::fromStdString(source.string())}, -1);
    REQUIRE(wait_until([&] { return imported.size() == 1; }));

    // Named by a change file that waits here for it: on no list of what is missing.
    REQUIRE(wait_until([&] { return laptop.state().value(QStringLiteral("state")).toString() == QStringLiteral("downloading"); }));
    // Its rows say it is on its way: begun on, none of it here yet.
    REQUIRE(wait_until([&] { return !church.text("SELECT path FROM sync_media").empty(); }));
    const auto name = QString::fromStdString(church.text("SELECT path FROM sync_media").substr(6));
    const auto arrival = [&] {
        return laptop.facade.media()->arrival(seventhslide::app::MediaKindQml::Audio, name)
            .value(QStringLiteral("state")).toString();
    };
    REQUIRE(wait_until([&] { return arrival() == QStringLiteral("downloading"); }));
    CHECK(laptop.facade.media()->arrival(seventhslide::app::MediaKindQml::Audio, name)
              .value(QStringLiteral("text")).toString()
          == QStringLiteral("Downloading 0%"));
    CHECK(laptop.state().value(QStringLiteral("text")).toString() == QStringLiteral("Downloading 1 file(s)"));
    CHECK(laptop.state().value(QStringLiteral("active")).toBool());
    REQUIRE(wait_until([&] { return church.state().value(QStringLiteral("state")).toString() == QStringLiteral("sending"); }));
    CHECK(church.state().value(QStringLiteral("text")).toString() == QStringLiteral("Sending 1 file(s)"));

    // The words follow by themselves, once it has come.
    let_go = true;
    const bool followed = wait_until([&] {
        return laptop.state().value(QStringLiteral("state")).toString() == QStringLiteral("uptodate")
               && church.state().value(QStringLiteral("state")).toString() == QStringLiteral("uptodate");
    }, 30000);
    INFO("laptop: " << laptop.state().value(QStringLiteral("text")).toString().toStdString()
                    << ", " << laptop.text("SELECT COUNT(*) FROM sync_media") << " sync_media row(s)");
    INFO("church: " << church.state().value(QStringLiteral("text")).toString().toStdString()
                    << ", " << church.text("SELECT COUNT(*) FROM sync_media") << " sync_media row(s)");
    REQUIRE(followed);
    settle(church, laptop);
    CHECK(laptop.text("SELECT COUNT(*) FROM sync_media") == "1");
    CHECK(church.text("SELECT COUNT(*) FROM sync_media") == "1");
    CHECK(arrival().isEmpty());
}

TEST_CASE("An edit made after a big file is taken in before the file has come", "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    AppComputer laptop(QStringLiteral("Laptop"));
    put_online(church);
    join(church, laptop, QStringLiteral("editor"));
    settle(church, laptop);
    // A minute in the app; a moment here.
    laptop.sync->link().set_hold_for_media(300);

    // The church's thread for reading files through, held: the file stays on its way.
    std::atomic<bool> let_go = false;
    std::atomic<bool> holding = false;
    church.sync->link().shared().checks.start([&] {
        holding = true;
        while (!let_go) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
    const auto release = qScopeGuard([&] { let_go = true; });
    REQUIRE(wait_until([&] { return holding.load(); }));

    const auto source = church.dir.path / "Prelude in G.mp3";
    std::ofstream(source, std::ios::binary) << std::string(1000 * 1000, 'x');
    QSignalSpy imported(church.facade.media(), &seventhslide::app::MediaController::importFinished);
    church.facade.media()->import_files(seventhslide::db::MediaKind::Audio,
                                       {QString::fromStdString(source.string())}, -1);
    REQUIRE(wait_until([&] { return imported.size() == 1; }));
    church.db().exec("UPDATE services SET name = 'Evensong'");

    // The edit reaches the laptop's window though the file has not come, and
    // the file says it is on its way.
    REQUIRE(wait_until([&] { return laptop.text("SELECT name FROM services") == "Evensong"; }, 15000));
    const auto path = church.text("SELECT path FROM sync_media");
    const auto name = QString::fromStdString(path.substr(6));
    const auto arrival = [&] {
        return laptop.facade.media()->arrival(seventhslide::app::MediaKindQml::Audio, name)
            .value(QStringLiteral("state")).toString();
    };
    // As the window is told, once a second.
    REQUIRE(wait_until([&] { return !arrival().isEmpty(); }));
    CHECK(arrival() != QStringLiteral("missing"));
    const auto* entry = laptop.facade.open_library().registry()->active();
    REQUIRE(entry != nullptr);
    CHECK_FALSE(std::filesystem::exists(entry->media_root() / path));
    CHECK(laptop.state().value(QStringLiteral("state")).toString() == QStringLiteral("downloading"));
    // Both ends say so.
    REQUIRE(wait_until([&] { return church.state().value(QStringLiteral("state")).toString() == QStringLiteral("sending"); }));

    // Once it comes it is here, said of nowhere as coming, without reopening.
    const auto files = laptop.facade.media()->filesRevision();
    let_go = true;
    REQUIRE(wait_until([&] { return std::filesystem::exists(entry->media_root() / path); }, 30000));
    REQUIRE(wait_until([&] { return arrival().isEmpty(); }));
    REQUIRE(wait_until([&] { return laptop.facade.media()->filesRevision() != files; }));
    REQUIRE(wait_until([&] { return laptop.state().value(QStringLiteral("state")).toString() == QStringLiteral("uptodate"); }, 30000));
}

TEST_CASE("A file lost on every computer is missing, not downloading for ever", "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    AppComputer laptop(QStringLiteral("Laptop"));
    fill(church.db());
    church.facade.refresh_library();
    const auto source = church.dir.path / "Prelude in G.mp3";
    std::ofstream(source, std::ios::binary) << std::string(5000, 'x');
    QSignalSpy imported(church.facade.media(), &seventhslide::app::MediaController::importFinished);
    church.facade.media()->import_files(seventhslide::db::MediaKind::Audio,
                                       {QString::fromStdString(source.string())}, -1);
    REQUIRE(wait_until([&] { return imported.size() == 1; }));
    REQUIRE(church.sync->putOnline(k_password).isEmpty());
    REQUIRE(church.await(QStringLiteral("putOnline")).value(QStringLiteral("ok")).toBool());
    join(church, laptop, QStringLiteral("editor"));
    REQUIRE(wait_until([&] { return church.state().value(QStringLiteral("state")).toString() == QStringLiteral("uptodate"); }));

    // The file goes from both computers, outside SeventhSlide.
    const auto path = church.text("SELECT path FROM sync_media");
    for (auto* c : {&church, &laptop}) {
        const auto* entry = c->facade.open_library().registry()->active();
        REQUIRE(entry != nullptr);
        REQUIRE(std::filesystem::remove(entry->media_root() / path));
    }
    church.sync->syncNow();
    laptop.sync->syncNow();

    // Each asks the other, hears it has not got it either, and says so.
    for (auto* c : {&church, &laptop}) {
        REQUIRE(wait_until([&] { return c->state().value(QStringLiteral("state")).toString() == QStringLiteral("missing"); }));
        const auto state = c->state();
        CHECK(state.value(QStringLiteral("missing")).toInt() == 1);
        CHECK(state.value(QStringLiteral("trouble")).toBool());
        CHECK_FALSE(state.value(QStringLiteral("active")).toBool());
        CHECK_THAT(state.value(QStringLiteral("text")).toString().toStdString(), ContainsSubstring("missing"));
        // And so do its rows.
        REQUIRE(wait_until([&] {
            return c->facade.media()
                       ->arrival(seventhslide::app::MediaKindQml::Audio, QString::fromStdString(path.substr(6)))
                       .value(QStringLiteral("state"))
                       .toString()
                   == QStringLiteral("missing");
        }));
    }
}

TEST_CASE("A file the other computer is itself getting is not called missing", "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    AppComputer laptop(QStringLiteral("Laptop"));
    fill(church.db());
    church.facade.refresh_library();
    const auto source = church.dir.path / "Prelude in G.mp3";
    std::ofstream(source, std::ios::binary) << std::string(5000, 'x');
    QSignalSpy imported(church.facade.media(), &seventhslide::app::MediaController::importFinished);
    church.facade.media()->import_files(seventhslide::db::MediaKind::Audio,
                                       {QString::fromStdString(source.string())}, -1);
    REQUIRE(wait_until([&] { return imported.size() == 1; }));
    REQUIRE(church.sync->putOnline(k_password).isEmpty());
    REQUIRE(church.await(QStringLiteral("putOnline")).value(QStringLiteral("ok")).toBool());
    join(church, laptop, QStringLiteral("editor"));
    REQUIRE(wait_until([&] { return church.state().value(QStringLiteral("state")).toString() == QStringLiteral("uptodate"); }));

    // Told by hand here, not from what is really on its way.
    church.sync->link().set_tell_coming_every(3600 * 1000);
    const auto path = church.text("SELECT path FROM sync_media");
    for (auto* c : {&church, &laptop}) {
        const auto* entry = c->facade.open_library().registry()->active();
        REQUIRE(entry != nullptr);
        REQUIRE(std::filesystem::remove(entry->media_root() / path));
    }
    church.sync->syncNow();
    laptop.sync->syncNow();
    const auto name = QString::fromStdString(path.substr(6));
    const auto arrival = [&] {
        return laptop.facade.media()->arrival(seventhslide::app::MediaKindQml::Audio, name);
    };
    REQUIRE(wait_until([&] { return arrival().value(QStringLiteral("state")).toString() == QStringLiteral("missing"); }));

    // The window open throughout: what it shows follows by itself.
    seventhslide::test::OperatorUi ui(laptop.facade, QStringLiteral("songs"), laptop.sync.get());
    QObject* bar = ui.find_component("AppBar");
    REQUIRE(bar != nullptr);
    const auto label = [&] { return bar->property("syncLabel").toString(); };
    REQUIRE(wait_until([&] { return label() == QStringLiteral("1 file missing"); }));

    const auto* peer_list = &church.sync->link().peers();
    REQUIRE(peer_list->size() == 1);
    const auto library = *peer_list->front()->libraries().begin();
    peer_list->front()->tell_coming(library, {path});
    REQUIRE(wait_until([&] { return arrival().value(QStringLiteral("state")).toString() == QStringLiteral("elsewhere"); }));
    CHECK(arrival().value(QStringLiteral("text")).toString() == QStringLiteral("On its way to Church PC"));
    // The bar does not call that downloading, nor missing, nor trouble.
    REQUIRE(wait_until([&] { return laptop.state().value(QStringLiteral("state")).toString() == QStringLiteral("elsewhere"); }));
    CHECK(laptop.state().value(QStringLiteral("text")).toString()
          == QStringLiteral("Waiting for 1 file(s) still on the way to another computer"));
    CHECK(laptop.state().value(QStringLiteral("elsewhere")).toInt() == 1);
    CHECK_FALSE(laptop.state().value(QStringLiteral("trouble")).toBool());

    REQUIRE(wait_until([&] { return label() == QStringLiteral("Waiting for 1 file"); }));
    CHECK_FALSE(bar->property("syncUnderWay").toBool());

    // No longer on its way to it: the bar says so again by itself.
    peer_list->front()->tell_coming(library, {});
    REQUIRE(wait_until([&] { return arrival().value(QStringLiteral("state")).toString() == QStringLiteral("missing"); }));
    REQUIRE(wait_until([&] { return label() == QStringLiteral("1 file missing"); }));
    CHECK(laptop.state().value(QStringLiteral("state")).toString() == QStringLiteral("missing"));
}

TEST_CASE("A file that comes after what names it shows in the window without reopening", "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    AppComputer laptop(QStringLiteral("Laptop"));
    put_online(church);
    join(church, laptop, QStringLiteral("viewer"));
    settle(church, laptop);

    const auto source = church.dir.path / "Sunrise.png";
    QImage picture(64, 36, QImage::Format_RGB32);
    picture.fill(Qt::red);
    REQUIRE(picture.save(QString::fromStdString(source.string())));
    QSignalSpy imported(church.facade.media(), &seventhslide::app::MediaController::importFinished);
    church.facade.media()->import_files(seventhslide::db::MediaKind::Image,
                                       {QString::fromStdString(source.string())}, -1);
    REQUIRE(wait_until([&] { return imported.size() == 1; }));
    REQUIRE(wait_until([&] { return laptop.text("SELECT COUNT(*) FROM image_files") == "1"; }));
    settle(church, laptop);

    auto* browser = laptop.facade.media()->browser(seventhslide::db::MediaKind::Image);
    const auto thumb = [&] {
        for (int row = 0; row < browser->rowCount(); ++row) {
            const auto idx = browser->index(row);
            if (!browser->data(idx, seventhslide::app::MediaBrowserModel::FilenameRole).toString().isEmpty()) {
                return browser->data(idx, seventhslide::app::MediaBrowserModel::ThumbUrlRole).toString();
            }
        }
        return QString();
    };
    REQUIRE_FALSE(thumb().isEmpty());

    // The file goes from the laptop, outside SeventhSlide: its row and its
    // previews show it missing.
    const auto path = laptop.text("SELECT path FROM sync_media");
    const auto* entry = laptop.facade.open_library().registry()->active();
    REQUIRE(entry != nullptr);
    REQUIRE(std::filesystem::remove(entry->media_root() / path));
    laptop.facade.media()->refresh(seventhslide::db::MediaKind::Image);
    REQUIRE(thumb().isEmpty());
    auto* pictures = laptop.facade.previews().image_cache();
    REQUIRE(pictures != nullptr);
    REQUIRE(pictures->get("/static/" + path).isNull());

    // Fetched again from the church, it shows, with nothing else done.
    const auto revision = laptop.facade.media()->filesRevision();
    laptop.sync->syncNow();
    REQUIRE(wait_until([&] { return !thumb().isEmpty(); }, 30000));
    CHECK(laptop.facade.media()->filesRevision() != revision);
    CHECK_FALSE(pictures->get("/static/" + path).isNull());
}

TEST_CASE("A computer renamed goes by its new name on every computer's list", "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    AppComputer laptop(QStringLiteral("fedora"));
    put_online(church);
    join(church, laptop, QStringLiteral("editor"));
    const auto named = [](AppComputer& on, const QString& name) {
        for (const auto& c : on.sync->openLibrary().value(QStringLiteral("computers")).toList()) {
            if (c.toMap().value(QStringLiteral("name")).toString() == name) {
                return true;
            }
        }
        return false;
    };
    REQUIRE(wait_until([&] { return named(church, QStringLiteral("fedora")); }));

    // Renamed on the Sync page: here, and on the church's list.
    REQUIRE(laptop.sync->setComputerName(QStringLiteral("Fedora")).isEmpty());
    REQUIRE(wait_until([&] { return named(laptop, QStringLiteral("Fedora")); }));
    REQUIRE(wait_until([&] { return named(church, QStringLiteral("Fedora")); }));
    CHECK_FALSE(named(church, QStringLiteral("fedora")));

    // Renamed in its settings file while its sync was not running, so no new
    // name went on the list: the list follows once it is.
    auto settings = seventhslide::link::SyncSettings::load(laptop.dir.path);
    CHECK(settings.computer_name == "Fedora");
    settings.computer_name = "Pastor's Laptop";
    REQUIRE(settings.save(laptop.dir.path));
    laptop.start();
    REQUIRE(wait_until([&] { return named(church, QStringLiteral("Pastor's Laptop")); }, 30000));
    CHECK(named(laptop, QStringLiteral("Pastor's Laptop")));
}

TEST_CASE("Not syncing automatically, a computer syncs at Sync Now, then waits again", "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    AppComputer laptop(QStringLiteral("Laptop"));
    put_online(church);
    join(church, laptop, QStringLiteral("editor"));
    REQUIRE(laptop.sync->syncAutomatically());
    REQUIRE(laptop.sync->setSyncAutomatically(false).isEmpty());
    CHECK_FALSE(laptop.sync->syncAutomatically());
    CHECK(seventhslide::link::SyncSettings::load(laptop.dir.path).sync_automatically == false);

    // Paused as it is during a service: the church hears, nothing comes.
    REQUIRE(wait_until([&] { return laptop.sync->paused_now(); }, 3000));
    CHECK(laptop.state().value(QStringLiteral("state")).toString() == QStringLiteral("paused"));
    CHECK(laptop.state().value(QStringLiteral("text")).toString() == QStringLiteral("Waiting for Sync Now"));
    REQUIRE(wait_until([&] {
        const auto& peers = church.sync->link().peers();
        return !peers.empty() && peers.front()->paused_there();
    }));
    const auto sent = changes_sent(church);
    church.db().exec("UPDATE services SET name = 'Divine Service'");
    // Sent at the church, and not come here.
    REQUIRE(wait_until([&] { return changes_sent(church) > sent; }));
    settle(church, laptop);
    CHECK(laptop.text("SELECT name FROM services") != "Divine Service");

    // Sync Now: it comes, and then the computer waits again.
    laptop.sync->syncNow();
    REQUIRE(wait_until([&] { return laptop.text("SELECT name FROM services") == "Divine Service"; }));
    REQUIRE(wait_until([&] { return laptop.sync->paused_now(); }));
    CHECK(laptop.sync->node().waiting_for_sync_now());

    // Its own edit waits there too, and goes at the next Sync Now.
    laptop.db().exec("UPDATE services SET name = 'Evensong'");
    // Nothing says an edit was seen while paused: past the look for edits and
    // the moment one settles.
    run_events_for(3000);
    settle(church, laptop);
    CHECK(church.text("SELECT name FROM services") == "Divine Service");
    laptop.sync->syncNow();
    REQUIRE(wait_until([&] { return church.text("SELECT name FROM services") == "Evensong"; }));

    // Kept across a restart, and on again at once when turned on.
    REQUIRE(wait_until([&] { return laptop.sync->paused_now(); }));
    laptop.start();
    CHECK_FALSE(laptop.sync->syncAutomatically());
    CHECK(laptop.sync->paused_now());
    REQUIRE(laptop.sync->setSyncAutomatically(true).isEmpty());
    CHECK_FALSE(laptop.sync->paused_now());
}

TEST_CASE("Sync Now looks for a while for a computer not connected, then waits again", "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    AppComputer laptop(QStringLiteral("Laptop"));
    put_online(church);
    join(church, laptop, QStringLiteral("editor"));
    laptop.sync_now_reach_ms = 1500;
    laptop.start();
    REQUIRE(laptop.sync->setSyncAutomatically(false).isEmpty());
    REQUIRE(wait_until([&] { return laptop.sync->paused_now(); }, 3000));
    church.sync.reset();

    laptop.sync->syncNow();
    run_events_for(800);  // past when nothing moving would have ended it
    CHECK_FALSE(laptop.sync->paused_now());
    REQUIRE(wait_until([&] { return laptop.sync->paused_now(); }, 5000));
}

TEST_CASE("Not syncing automatically, stopping still tells the others", "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    AppComputer stage(QStringLiteral("Stage display"));
    put_online(church);
    join(church, stage, QStringLiteral("viewer"));
    REQUIRE(stage.sync->setSyncAutomatically(false).isEmpty());
    REQUIRE(wait_until([&] { return stage.sync->paused_now(); }, 3000));

    REQUIRE(stage.sync->stopSyncing(stage.facade.libraries()->activeLibraryId()).isEmpty());
    const auto stopped = stage.await(QStringLiteral("stopSyncing"));
    REQUIRE(stopped.value(QStringLiteral("ok")).toBool());
    CHECK(stopped.value(QStringLiteral("heard")).toBool());
}

TEST_CASE("Addresses typed go on the list, and clearing them takes them off", "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    AppComputer laptop(QStringLiteral("Laptop"));
    put_online(church);
    join(church, laptop, QStringLiteral("editor"));
    const auto machine = laptop.sync->node().identity().machine;
    const auto on_the_list = [&] {
        auto* open = church.sync->open_sync();
        return seventhslide::sync::read_members(open->store(), open->uid()).find(machine)->addresses;
    };
    // None typed: none said, not even those on its networks.
    CHECK(on_the_list().empty());
    laptop.sync->node().set_addresses({"slides.example.org:9001"});
    REQUIRE(wait_until([&] { return on_the_list() == std::vector<std::string>{"slides.example.org:9001"}; }));
    laptop.sync->node().set_addresses({});
    REQUIRE(wait_until([&] { return on_the_list().empty(); }));
}

TEST_CASE("Where this computer is reached is set on the Sync page, and kept", "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    AppComputer laptop(QStringLiteral("Laptop"));
    put_online(church);
    join(church, laptop, QStringLiteral("editor"));
    const auto machine = laptop.sync->node().identity().machine;
    const auto on_the_list = [&] {
        auto* open = church.sync->open_sync();
        return seventhslide::sync::read_members(open->store(), open->uid()).find(machine)->addresses;
    };

    // Not an address: said, and nothing changes.
    CHECK_FALSE(laptop.sync->setReachAddresses(QStringLiteral("home.example.org, not:an:address")).isEmpty());
    CHECK(laptop.sync->reachAddresses().isEmpty());

    REQUIRE(laptop.sync->setReachAddresses(QStringLiteral("home.example.org:9001, [2001:db8::1]:9001")).isEmpty());
    CHECK(laptop.sync->reachAddresses()
          == QStringList{QStringLiteral("home.example.org:9001"), QStringLiteral("[2001:db8::1]:9001")});
    REQUIRE(wait_until([&] { return on_the_list().size() == 2; }));

    // Kept, and cleared with an empty field.
    laptop.start();
    CHECK(laptop.sync->reachAddresses().size() == 2);
    REQUIRE(laptop.sync->setReachAddresses(QStringLiteral("  ")).isEmpty());
    REQUIRE(wait_until([&] { return on_the_list().empty(); }));
}

TEST_CASE("A port chosen takes effect at the next start, and one taken is said", "[syncapp]")
{
    AppComputer computer(QStringLiteral("Booth PC"));
    CHECK(computer.sync->chosenPort() == 0);
    CHECK_FALSE(computer.sync->setChosenPort(QStringLiteral("not a port")).isEmpty());
    CHECK_FALSE(computer.sync->setChosenPort(QStringLiteral("70000")).isEmpty());
    CHECK(computer.sync->portProblem().isEmpty());

    // A port free now, and one being used.
    quint16 free_port = 0;
    {
        QTcpServer probe;
        REQUIRE(probe.listen(QHostAddress::Any));
        free_port = probe.serverPort();
    }
    REQUIRE(computer.sync->setChosenPort(QString::number(free_port)).isEmpty());
    CHECK(computer.sync->chosenPort() == free_port);
    CHECK(computer.sync->portPending());
    computer.start();
    CHECK_FALSE(computer.sync->portPending());
    CHECK(computer.sync->port() == free_port);
    CHECK(computer.sync->portProblem().isEmpty());

    QTcpServer taken;
    REQUIRE(taken.listen(QHostAddress::Any));
    REQUIRE(computer.sync->setChosenPort(QString::number(taken.serverPort())).isEmpty());
    computer.start();
    // It takes another, so the church network still finds it, and says so.
    CHECK(computer.sync->node().listening());
    CHECK(computer.sync->port() != taken.serverPort());
    CHECK_THAT(computer.sync->portProblem().toStdString(), ContainsSubstring("is taken"));
    // An address with no port means the port chosen, where the router forwards.
    REQUIRE(computer.sync->setReachAddresses(QStringLiteral("home.example.org")).isEmpty());
    CHECK(computer.sync->node().addresses()
          == std::vector<std::string>{"home.example.org:" + std::to_string(taken.serverPort())});

    // Unset: the usual way again.
    REQUIRE(computer.sync->setChosenPort(QString()).isEmpty());
    CHECK(computer.sync->chosenPort() == 0);
}

TEST_CASE("An upload speed cap holds at once, and is kept", "[syncapp]")
{
    AppComputer computer(QStringLiteral("Booth PC"));
    CHECK(computer.sync->uploadCap().isEmpty());
    CHECK(computer.sync->link().upload_cap() == 0);
    CHECK_FALSE(computer.sync->setUploadCap(QStringLiteral("fast")).isEmpty());
    CHECK_FALSE(computer.sync->setUploadCap(QStringLiteral("0.01")).isEmpty());
    REQUIRE(computer.sync->setUploadCap(QStringLiteral("2.5")).isEmpty());
    CHECK(computer.sync->uploadCap() == QStringLiteral("2.5"));
    CHECK(computer.sync->link().upload_cap() == 312'500);
    computer.start();
    CHECK(computer.sync->uploadCap() == QStringLiteral("2.5"));
    CHECK(computer.sync->link().upload_cap() == 312'500);
    REQUIRE(computer.sync->setUploadCap(QString()).isEmpty());
    CHECK(computer.sync->link().upload_cap() == 0);
}

TEST_CASE("Sharing this computer's address is on until turned off, and kept", "[syncapp]")
{
    AppComputer computer(QStringLiteral("Booth PC"));
    CHECK(computer.sync->shareAddress());
    CHECK(computer.sync->link().share_address());
    REQUIRE(computer.sync->setShareAddress(false).isEmpty());
    CHECK_FALSE(computer.sync->shareAddress());
    CHECK_FALSE(computer.sync->link().share_address());
    computer.start();
    CHECK_FALSE(computer.sync->link().share_address());
    REQUIRE(computer.sync->setShareAddress(true).isEmpty());
    CHECK(computer.sync->link().share_address());
}

TEST_CASE("Where to reach another computer can be typed here, needing no password", "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    AppComputer laptop(QStringLiteral("Laptop"));
    put_online(church);
    join(church, laptop, QStringLiteral("editor"));
    // Said once the laptop is in: an invite names only the addresses typed.
    REQUIRE(church.sync->setReachAddresses(QStringLiteral("church.example.org")).isEmpty());
    const auto machine = QString::fromStdString(church.sync->node().identity().machine);
    const auto row = [&] {
        for (const auto& c : laptop.sync->openLibrary().value(QStringLiteral("computers")).toList()) {
            if (c.toMap().value(QStringLiteral("machine")).toString() == machine) {
                return c.toMap();
            }
        }
        return QVariantMap{};
    };
    const auto typed = [&] { return row().value(QStringLiteral("typedAddresses")).toStringList(); };
    // With the port it is set to, as no port was typed.
    REQUIRE(wait_until([&] {
        const auto own = row().value(QStringLiteral("addresses")).toStringList();
        return own.size() == 1 && own.front().startsWith(QStringLiteral("church.example.org:"));
    }));
    const auto own = row().value(QStringLiteral("addresses")).toStringList().front();
    REQUIRE(laptop.sync->setComputerAddresses(machine, {QStringLiteral("church.lan"), QStringLiteral("203.0.113.9:9000"),
                                                        QStringLiteral("church.lan:8766")})
                .isEmpty());
    REQUIRE(wait_until([&] {
        return typed() == QStringList{QStringLiteral("church.lan:8766"), QStringLiteral("203.0.113.9:9000")};
    }));
    // One it gave itself is reached anyway; what is no address, or no other computer, will not do.
    CHECK_THAT(laptop.sync->setComputerAddresses(machine, {own}).toStdString(), ContainsSubstring("gave itself"));
    CHECK_FALSE(laptop.sync->setComputerAddresses(machine, {QStringLiteral("not:an:address")}).isEmpty());
    CHECK_FALSE(laptop.sync->setComputerAddresses(QStringLiteral("nobody"), {QStringLiteral("x.example.org")}).isEmpty());
    CHECK_FALSE(laptop.sync->setComputerAddresses(q_machine(laptop), {QStringLiteral("x.example.org")}).isEmpty());
    CHECK(typed().size() == 2);
    // As the Edit card reads one typed.
    CHECK(laptop.sync->readAddress(QStringLiteral(" stage.lan "), {}, {}).value(QStringLiteral("address")).toString()
          == QStringLiteral("stage.lan:8766"));
    CHECK(laptop.sync->readAddress(QStringLiteral("church.example.org"), {QStringLiteral("church.example.org:8766")}, {})
              .contains(QStringLiteral("error")));
    CHECK(laptop.sync->readAddress(own, {own}, {}).contains(QStringLiteral("error")));
    // One added already, or one more than a computer is given.
    CHECK_THAT(laptop.sync->readAddress(QStringLiteral("stage.lan"), {}, {QStringLiteral("stage.lan:8766")})
                   .value(QStringLiteral("error"))
                   .toString()
                   .toStdString(),
               ContainsSubstring("added already"));
    QStringList full;
    for (std::size_t i = 0; i < seventhslide::sync::k_most_addresses; ++i) {
        full.push_back(QStringLiteral("host%1.lan:8766").arg(i));
    }
    CHECK_THAT(laptop.sync->readAddress(QStringLiteral("one.more.lan"), {}, full)
                   .value(QStringLiteral("error"))
                   .toString()
                   .toStdString(),
               ContainsSubstring("at most"));
    REQUIRE(laptop.sync->setComputerAddresses(machine, {}).isEmpty());
    REQUIRE(wait_until([&] { return typed().isEmpty(); }));
}

TEST_CASE("An editor made a viewer loses no edit: it counts if made an editor again", "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    AppComputer laptop(QStringLiteral("Laptop"));
    put_online(church);
    join(church, laptop, QStringLiteral("editor"));
    const auto machine = q_machine(laptop);
    REQUIRE(church.sync->admin()->setRole(machine, QStringLiteral("viewer"), false, k_password).isEmpty());
    REQUIRE(church.await(QStringLiteral("role")).value(QStringLiteral("ok")).toBool());
    REQUIRE(wait_until([&] { return laptop.facade.readOnly(); }));

    // An edit it had not sent: on a connection of its own, as the window's is
    // read-only now.
    auto* open = laptop.sync->open_sync();
    {
        auto db = seventhslide::db::Database::open_file(seventhslide::library::database_file(open->root()));
        db.exec("UPDATE services SET name = 'Made while away'");
    }
    laptop.sync->syncNow();
    // In a change file of its own, and nothing left to send; no other computer
    // takes it, and it writes no snapshot.
    REQUIRE(wait_until([&] { return !open->store().sequences(seventhslide::sync::Numbered::Changes, q_machine(laptop).toStdString()).empty(); }));
    REQUIRE(wait_until([&] { return laptop.state().value(QStringLiteral("state")).toString() != QStringLiteral("sending"); }));
    // The church says nothing of what it refuses.
    run_events_for(2000);
    settle(church, laptop);
    CHECK(church.text("SELECT name FROM services") != "Made while away");
    CHECK(laptop.state().value(QStringLiteral("state")).toString() != QStringLiteral("sending"));
    CHECK(std::ranges::none_of(open->store().snapshots(), [&](const seventhslide::sync::SnapshotName& name) {
        return name.machine == q_machine(laptop).toStdString();
    }));

    // An editor again: it comes through.
    REQUIRE(church.sync->admin()->setRole(machine, QStringLiteral("editor"), false, k_password).isEmpty());
    REQUIRE(church.await(QStringLiteral("role")).value(QStringLiteral("ok")).toBool());
    REQUIRE(wait_until([&] { return church.text("SELECT name FROM services") == "Made while away"; }));
}

TEST_CASE("An invite waits among the computers until it is used or withdrawn", "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    AppComputer laptop(QStringLiteral("Laptop"));
    put_online(church);
    const auto invites = [&] {
        QVariantList out;
        for (const auto& c : church.sync->openLibrary().value(QStringLiteral("computers")).toList()) {
            if (c.toMap().contains(QStringLiteral("invite"))) {
                out.push_back(c);
            }
        }
        return out;
    };
    REQUIRE(church.sync->admin()->invite(QStringLiteral("viewer"), 7, k_password).isEmpty());
    const auto made = church.await(QStringLiteral("invite"));
    REQUIRE(made.value(QStringLiteral("ok")).toBool());
    const auto id = QString::fromStdString(
        seventhslide::sync::read_invite(made.value(QStringLiteral("code")).toString().toStdString()).invite.id);
    REQUIRE(invites().size() == 1);
    const auto row = invites().front().toMap();
    CHECK(row.value(QStringLiteral("invite")).toString() == id);
    CHECK(row.value(QStringLiteral("key")).toString() == QStringLiteral("invite:") + id);
    CHECK(row.value(QStringLiteral("role")).toString() == QStringLiteral("viewer"));
    CHECK(row.value(QStringLiteral("madeBy")).toString() == QStringLiteral("Church PC"));

    // Where its computer is, the usual port unless told; here alone.
    const auto uid = church.sync->open_sync()->uid();
    REQUIRE(church.sync->setInviteAddresses(id, {QStringLiteral("far.example.org"), QStringLiteral("far.example.org:9000")})
                .isEmpty());
    CHECK(church.sync->node().reach().invite_addresses(uid, id.toStdString())
          == std::vector<std::string>{"far.example.org:8766", "far.example.org:9000"});
    CHECK(invites().front().toMap().value(QStringLiteral("typedAddresses")).toStringList()
          == QStringList{QStringLiteral("far.example.org:8766"), QStringLiteral("far.example.org:9000")});
    CHECK_FALSE(church.sync->setInviteAddresses(QStringLiteral("feedfacefeedface"), {QStringLiteral("x.example.org")}).isEmpty());

    // Withdrawn: gone, and nobody joins by it.
    REQUIRE(church.sync->admin()->withdrawInvite(id, k_password).isEmpty());
    REQUIRE(church.await(QStringLiteral("withdraw")).value(QStringLiteral("ok")).toBool());
    CHECK(invites().isEmpty());
    CHECK_FALSE(church.sync->setInviteAddresses(id, {QStringLiteral("far.example.org")}).isEmpty());

    // Used: gone too.
    join(church, laptop, QStringLiteral("editor"));
    REQUIRE(wait_until([&] { return invites().isEmpty(); }));
}

TEST_CASE("Syncing waits a moment after the last live item closes", "[syncapp]")
{
    AppComputer laptop(QStringLiteral("Laptop"));
    put_online(laptop);
    laptop.live_grace_ms = 600;
    laptop.start();
    // Opened first: the library opens off the window's thread, and until it
    // has, the state says "Starting…" whatever is live. The grace is timed
    // from here.
    REQUIRE(wait_until([&] { return laptop.state().value(QStringLiteral("state")).toString()
                                    != QStringLiteral("starting"); },
                       20000));
    REQUIRE(laptop.sync->pauseDuringServices());
    const auto song = std::stoll(laptop.text("SELECT id FROM songs WHERE title = 'Amazing Grace'"));
    const auto go_live = [&] {
        REQUIRE(laptop.facade.live()->playSong(song));
        REQUIRE(wait_until([&] { return laptop.sync->paused_now(); }, 3000));
    };
    const auto clear_live = [&] { laptop.facade.stacks()->clearAllStacks(); };
    CHECK_FALSE(laptop.facade.media()->syncPaused());
    go_live();
    // What is on its way says it is paused too.
    CHECK(laptop.facade.media()->syncPaused());

    SECTION("not at once, and not if the next item comes first")
    {
        clear_live();
        run_events_for(200);
        CHECK(laptop.sync->paused_now());
        CHECK(laptop.state().value(QStringLiteral("text")).toString() == QStringLiteral("Syncing goes on in a moment"));
        go_live();
        run_events_for(800);  // past when the first would have ended
        CHECK(laptop.sync->paused_now());
        clear_live();
        REQUIRE(wait_until([&] { return !laptop.sync->paused_now(); }, 3000));
        CHECK_FALSE(laptop.facade.media()->syncPaused());
    }
    SECTION("Sync Now need not wait it out, though it waits for what is live")
    {
        laptop.sync->syncNow();
        CHECK(laptop.sync->paused_now());
        clear_live();
        run_events_for(100);
        CHECK(laptop.sync->paused_now());
        laptop.sync->syncNow();
        CHECK_FALSE(laptop.sync->paused_now());
    }
    SECTION("turning the pause off needs no wait")
    {
        clear_live();
        REQUIRE(laptop.sync->setPauseDuringServices(false).isEmpty());
        CHECK_FALSE(laptop.sync->paused_now());
    }
}

TEST_CASE("Syncing waits while something is live, when the operator wants that", "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    AppComputer laptop(QStringLiteral("Laptop"));
    put_online(church);
    join(church, laptop, QStringLiteral("editor"));
    REQUIRE(laptop.sync->pauseDuringServices());

    // Something live on the laptop: the church's change waits.
    const auto song = std::stoll(laptop.text("SELECT id FROM songs WHERE title = 'Amazing Grace'"));
    REQUIRE(laptop.facade.live()->playSong(song));
    REQUIRE(wait_until([&] { return laptop.sync->paused_now(); }, 3000));
    // Nothing goes over the network either: the church hears the laptop paused.
    CHECK(laptop.sync->link().paused());
    REQUIRE(wait_until([&] {
        const auto& peers = church.sync->link().peers();
        return !peers.empty() && peers.front()->paused_there();
    }, 3000));
    const auto sent = changes_sent(church);
    church.db().exec("UPDATE songs SET key = 'Bb' WHERE title = 'Amazing Grace'");
    REQUIRE(wait_until([&] { return changes_sent(church) > sent; }));
    settle(church, laptop);
    CHECK(laptop.text("SELECT key FROM songs WHERE title = 'Amazing Grace'") == "G");
    CHECK(laptop.state().value(QStringLiteral("state")).toString() == QStringLiteral("paused"));

    // Cleared: it comes in.
    laptop.facade.stacks()->clearAllStacks();
    REQUIRE(wait_until([&] { return laptop.text("SELECT key FROM songs WHERE title = 'Amazing Grace'") == "Bb"; }));
    CHECK_FALSE(laptop.sync->paused_now());
    CHECK_FALSE(laptop.sync->link().paused());
}

TEST_CASE("The bar's Collaboration button has a dot for a message not read, even after a restart", "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    AppComputer laptop(QStringLiteral("Laptop"));
    put_online(church);
    join(church, laptop, QStringLiteral("editor"));
    REQUIRE(wait_until([&] { return church.sync->log()->canPost() && laptop.sync->log()->canPost(); }));
    REQUIRE(wait_until([&] {
        return church.sync->log()->rowCount() > 0
               && laptop.sync->log()->rowCount() == church.sync->log()->rowCount();
    }));
    const auto say = [&](const QString& text) {
        REQUIRE(laptop.sync->log()->send(text).isEmpty());
    };
    const auto rows = [&](int n) { REQUIRE(wait_until([&] { return church.sync->log()->rowCount() == n; })); };
    int had = church.sync->log()->rowCount();
    {
        seventhslide::test::OperatorUi ui(church.facade, QStringLiteral("songs"), church.sync.get());
        REQUIRE(ui.window() != nullptr);
        auto* button = ui.window()->findChild<QQuickItem*>(QStringLiteral("collaborationButton"));
        REQUIRE(button != nullptr);
        QObject* window = ui.find_component("CollaborationWindow");
        REQUIRE(window != nullptr);
        const auto dot = [&] { return button->property("dot").toBool(); };

        // Notes of changes are not messages.
        CHECK_FALSE(dot());

        say(QStringLiteral("Could you check the second verse?"));
        rows(++had);
        REQUIRE(wait_until(dot));

        // Writing back is not reading it.
        REQUIRE(church.sync->log()->send(QStringLiteral("One moment")).isEmpty());
        rows(++had);
        CHECK(dot());

        // Opened, it is read; and what comes while it is open.
        REQUIRE(QMetaObject::invokeMethod(button, "clicked"));
        CHECK_FALSE(dot());
        say(QStringLiteral("Thanks"));
        rows(++had);
        CHECK_FALSE(dot());

        // Closed, the next is not read.
        REQUIRE(QMetaObject::invokeMethod(window, "close"));
        say(QStringLiteral("One more thing"));
        rows(++had);
        REQUIRE(wait_until(dot));
        INFO(ui.warnings());
        CHECK(ui.warnings().empty());
    }

    // Not read yet: it still is not after a restart.
    const auto port = church.sync->port();
    church.start(port);
    REQUIRE(wait_until([&] { return church.sync->log()->rowCount() == had; }));
    REQUIRE(wait_until([&] { return church.sync->log()->unread(); }));
    church.sync->log()->setShowing(true);
    CHECK_FALSE(church.sync->log()->unread());
    church.sync->log()->setShowing(false);

    // Read: it stays read after one. The file is written off the window's
    // thread; the log's going waits for that.
    church.start(port);
    REQUIRE(wait_until([&] { return church.sync->log()->rowCount() == had; }));
    CHECK_FALSE(church.sync->log()->unread());
}

TEST_CASE("A message that comes late is unread, though a later one was read", "[syncapp]")
{
    using seventhslide::app::CollaborationLog;
    using seventhslide::sync::FolderStore;
    using seventhslide::sync::Numbered;
    seventhslide::test::QtBootstrap qt;
    seventhslide::test::TempDir dir;
    const std::string uid = "0123456789abcdef0123456789abcdef";

    // What the laptop wrote, and what of it has reached this computer.
    FolderStore written{dir.path / "written"};
    const seventhslide::sync::Identity laptop{"laptop", "laptop", seventhslide::crypto::SigningKey::generate()};
    seventhslide::sync::LogBook book(written, laptop, uid);
    book.post_message("The first");
    book.post_message("The second");
    auto here = std::make_shared<FolderStore>(dir.path / "here");
    const auto arrive = [&](std::int64_t n) {
        here->write(Numbered::Log, "laptop", n, written.read(Numbered::Log, "laptop", n));
    };
    const auto opened = [&](int rows) {
        auto log = std::make_unique<CollaborationLog>(
            [](std::string, std::string, std::function<void(std::string)>) {}, dir.path);
        log->follow(uid, here, "church");
        REQUIRE(wait_until([&] { return log->rowCount() == rows; }));
        return log;
    };

    // The second came first, and was read.
    arrive(2);
    auto log = opened(1);
    CHECK(log->unread());
    log->setShowing(true);
    CHECK_FALSE(log->unread());
    log->setShowing(false);

    arrive(1);
    log->refresh();
    REQUIRE(wait_until([&] { return log->rowCount() == 2; }));
    CHECK(log->unread());

    // And so after a restart; read, it stays read after the next.
    log.reset();
    log = opened(2);
    CHECK(log->unread());
    log->setShowing(true);
    log->setShowing(false);
    log.reset();
    log = opened(2);
    CHECK_FALSE(log->unread());
}

TEST_CASE("One computer's notes with nothing between them on a day are one row", "[syncapp]")
{
    using seventhslide::app::CollaborationLog;
    using seventhslide::sync::FolderStore;
    seventhslide::test::QtBootstrap qt;
    seventhslide::test::TempDir dir;
    const std::string uid = "0123456789abcdef0123456789abcdef";

    auto here = std::make_shared<FolderStore>(dir.path / "here");
    const auto book_of = [&](const std::string& machine) {
        return seventhslide::sync::LogBook(
            *here, {machine, machine, seventhslide::crypto::SigningKey::generate()}, uid);
    };
    auto laptop = book_of("laptop");
    auto church = book_of("church");
    // The first names one of the three songs it changed.
    laptop.post_changes({{"song", "a", "Amazing Grace", "changed", {"lyrics"}}}, {{"song", {{"changed", 3}}}});
    laptop.post_changes({{"song", "a", "Amazing Grace", "changed", {"verse_order"}},
                         {"song", "b", "It Is Well", "added", {}}},
                        {{"song", {{"changed", 1}, {"added", 1}}}});
    laptop.post_message("Done");
    laptop.post_changes({{"song", "b", "It Is Well", "changed", {"key"}}}, {{"song", {{"changed", 1}}}});

    CollaborationLog log([](std::string, std::string, std::function<void(std::string)>) {}, dir.path);
    log.follow(uid, here, "church");
    REQUIRE(wait_until([&] { return log.rowCount() == 3; }));
    const auto role = [&](int row, CollaborationLog::Roles r) { return log.data(log.index(row), r); };
    const auto details = [&](int row) {
        QStringList out;
        for (const auto& item : role(row, CollaborationLog::ItemsRole).toList()) {
            const auto map = item.toMap();
            out << map.value(QStringLiteral("title")).toString() + ": " + map.value(QStringLiteral("detail")).toString();
        }
        return out;
    };

    // The two before the message, as one: each song once, as it ended up.
    CHECK(role(0, CollaborationLog::SummaryRole).toString() == QStringLiteral("Another computer changed 4 songs"));
    CHECK(details(0) == QStringList{QStringLiteral("Amazing Grace: Lyrics, verse order"),
                                    QStringLiteral("It Is Well: Added")});
    CHECK(role(1, CollaborationLog::KindRole).toString() == QStringLiteral("message"));
    CHECK(details(2) == QStringList{QStringLiteral("It Is Well: Key")});

    // Opened, it stays open as more of its notes come; another computer's
    // is a row of its own.
    log.setExpanded(role(2, CollaborationLog::EntryIdRole).toString(), true);
    laptop.post_changes({{"song", "b", "It Is Well", "removed", {}}}, {{"song", {{"removed", 1}}}});
    log.refresh();
    REQUIRE(wait_until([&] { return details(2).size() == 1 && details(2).front().endsWith(QStringLiteral("Removed")); }));
    CHECK(log.rowCount() == 3);
    CHECK(role(2, CollaborationLog::ExpandedRole).toBool());
    CHECK(role(2, CollaborationLog::SummaryRole).toString() == QStringLiteral("Another computer removed 1 song"));
    church.post_changes({{"song", "a", "Amazing Grace", "changed", {"key"}}}, {{"song", {{"changed", 1}}}});
    log.refresh();
    REQUIRE(wait_until([&] { return log.rowCount() == 4; }));

    // Closed by its first note's id, all of it closes.
    log.setExpanded(role(2, CollaborationLog::EntryIdRole).toString(), false);
    CHECK_FALSE(role(2, CollaborationLog::ExpandedRole).toBool());
}

TEST_CASE("The Collaboration window keeps to the newest until the operator scrolls back", "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    put_online(church);
    auto* log = church.sync->log();
    REQUIRE(wait_until([&] { return log->canPost() && log->rowCount() > 0; }));
    seventhslide::test::OperatorUi ui(church.facade, QStringLiteral("songs"), church.sync.get());
    REQUIRE(ui.window() != nullptr);
    auto* button = ui.window()->findChild<QQuickItem*>(QStringLiteral("collaborationButton"));
    REQUIRE(button != nullptr);
    QObject* window = ui.find_component("CollaborationWindow");
    REQUIRE(window != nullptr);
    auto* list = ui.window()->findChild<QQuickItem*>(QStringLiteral("collaborationEntries"));
    REQUIRE(list != nullptr);

    const auto say = [&] {
        const int had = log->rowCount();
        REQUIRE(log->send(QStringLiteral("Could you check the second verse?")).isEmpty());
        REQUIRE(wait_until([&] { return log->rowCount() == had + 1; }));
    };
    for (int i = 0; i < 60; ++i) {
        say();
    }
    const auto at_end = [&] { return list->property("atNewest").toBool(); };
    const auto following = [&] { return list->property("stickToEnd").toBool(); };
    const auto reopen = [&] {
        REQUIRE(QMetaObject::invokeMethod(button, "clicked"));
        REQUIRE(wait_until([&] { return list->property("count").toInt() == log->rowCount() && list->height() > 0; }));
    };
    const auto bar = [&](qreal y) { return list->mapToScene(QPointF(list->width() - 5, y)).toPoint(); };
    const auto scroll_up = [&] {
        seventhslide::test::drag(ui.window(), bar(list->height() - 8), bar(8));
    };

    reopen();
    REQUIRE(list->property("contentHeight").toReal() > list->height());
    REQUIRE(wait_until(at_end));
    CHECK(following());

    // Dragged back by its bar, it stays there as more comes.
    scroll_up();
    CHECK_FALSE(at_end());
    CHECK_FALSE(following());
    say();
    QTest::qWait(50);
    CHECK_FALSE(at_end());

    // Dragged to the foot, it follows again.
    seventhslide::test::drag(ui.window(), bar(8), bar(list->height() - 1));
    CHECK(following());
    say();
    CHECK(wait_until(at_end));

    // Opened again, it is at the newest, wherever it was left.
    scroll_up();
    REQUIRE_FALSE(following());
    REQUIRE(QMetaObject::invokeMethod(window, "close"));
    say();
    reopen();
    CHECK(wait_until(at_end));
    CHECK(following());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("What is written in the Collaboration window is not lost, or sent to the wrong library", "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    put_online(church);
    auto* log = church.sync->log();
    REQUIRE(wait_until([&] { return log->canPost(); }));
    seventhslide::test::OperatorUi ui(church.facade, QStringLiteral("songs"), church.sync.get());
    REQUIRE(ui.window() != nullptr);
    QObject* window = ui.find_component("CollaborationWindow");
    REQUIRE(window != nullptr);
    REQUIRE(QMetaObject::invokeMethod(window, "show"));
    QObject* panel = ui.find_component("CollaborationPanel");
    REQUIRE(panel != nullptr);
    QSignalSpy failed(panel, SIGNAL(failed(QString)));
    const auto draft = [&] { return panel->property("draft").toString(); };

    // Enter with nothing written does nothing, as the button would.
    panel->setProperty("draft", QStringLiteral("   "));
    REQUIRE(QMetaObject::invokeMethod(panel, "send"));
    CHECK(failed.isEmpty());

    // One that turns out not to have gone comes back to try again.
    const auto text = QStringLiteral("Could you check the key?");
    panel->setProperty("draft", text);
    REQUIRE(QMetaObject::invokeMethod(panel, "send"));
    CHECK(draft().isEmpty());
    emit log->failed(QStringLiteral("The message could not be saved on this computer."));
    CHECK(failed.size() == 1);
    CHECK(draft() == text);

    // Another library opens: it was for this one's computers.
    log->follow("another", std::make_shared<seventhslide::sync::FolderStore>(church.dir.path / "another"), "me");
    CHECK(draft().isEmpty());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("The Collaboration window's log: messages both ways, notes of edits, while live too",
          "[syncapp]")
{
    using seventhslide::app::CollaborationLog;
    AppComputer church(QStringLiteral("Church PC"));
    AppComputer laptop(QStringLiteral("Laptop"));
    CHECK_FALSE(church.sync->log()->available());
    put_online(church);
    join(church, laptop, QStringLiteral("editor"));
    auto* at_church = church.sync->log();
    auto* at_laptop = laptop.sync->log();
    REQUIRE(wait_until([&] { return at_church->canPost() && at_laptop->canPost(); }));
    const auto role = [](CollaborationLog* log, int row, CollaborationLog::Roles r) {
        return log->data(log->index(row), r);
    };
    // Putting the library online was noted ("Church PC added 5 items"): once
    // both have that, count from there.
    REQUIRE(wait_until([&] { return at_church->rowCount() > 0 && at_laptop->rowCount() == at_church->rowCount(); }));
    CHECK(role(at_church, 0, CollaborationLog::KindRole).toString() == QStringLiteral("changes"));
    const int church_had = at_church->rowCount();
    const int laptop_had = at_laptop->rowCount();

    // Something live on the laptop: syncing waits there, the log does not.
    const auto song = std::stoll(laptop.text("SELECT id FROM songs WHERE title = 'Amazing Grace'"));
    REQUIRE(laptop.facade.live()->playSong(song));
    REQUIRE(wait_until([&] { return laptop.sync->paused_now(); }, 3000));

    CHECK_FALSE(church.sync->log()->send(QStringLiteral("   ")).isEmpty());
    REQUIRE(at_church->send(QStringLiteral("Could you fix the typos in Amazing Grace?")).isEmpty());
    REQUIRE(wait_until([&] { return at_laptop->rowCount() == laptop_had + 1; }));
    CHECK(role(at_laptop, laptop_had, CollaborationLog::TextRole).toString()
          == QStringLiteral("Could you fix the typos in Amazing Grace?"));
    CHECK(role(at_laptop, laptop_had, CollaborationLog::NameRole).toString() == QStringLiteral("Church PC"));
    CHECK_FALSE(role(at_laptop, laptop_had, CollaborationLog::MineRole).toBool());

    REQUIRE(at_laptop->send(QStringLiteral("Sure, I'll go do that!")).isEmpty());
    REQUIRE(wait_until([&] { return at_church->rowCount() == church_had + 2; }));
    CHECK(role(at_church, church_had + 1, CollaborationLog::NameRole).toString() == QStringLiteral("Laptop"));
    CHECK(role(at_church, church_had + 1, CollaborationLog::KindRole).toString() == QStringLiteral("message"));
    CHECK(role(at_church, church_had, CollaborationLog::MineRole).toBool());

    // An edit at the church is noted, and the note reaches the laptop while
    // the edit itself waits there.
    church.db().exec("UPDATE songs SET lyrics = '[V1]\nfixed line' WHERE title = 'Amazing Grace'");
    REQUIRE(wait_until([&] { return at_laptop->rowCount() == laptop_had + 3; }));
    CHECK(role(at_laptop, laptop_had + 2, CollaborationLog::KindRole).toString() == QStringLiteral("changes"));
    CHECK(role(at_laptop, laptop_had + 2, CollaborationLog::SummaryRole).toString()
          == QStringLiteral("Church PC updated 1 song"));
    const auto items = role(at_laptop, laptop_had + 2, CollaborationLog::ItemsRole).toList();
    REQUIRE(items.size() == 1);
    CHECK(items.front().toMap().value(QStringLiteral("title")).toString() == QStringLiteral("Amazing Grace"));
    CHECK(items.front().toMap().value(QStringLiteral("detail")).toString() == QStringLiteral("Lyrics"));
    CHECK(laptop.text("SELECT lyrics FROM songs WHERE title = 'Amazing Grace'") == "[V1]\nline");
    CHECK(laptop.sync->paused_now());

    // A note opens, by the operator.
    const auto id = role(at_laptop, laptop_had + 2, CollaborationLog::EntryIdRole).toString();
    at_laptop->setExpanded(id, true);
    CHECK(role(at_laptop, laptop_had + 2, CollaborationLog::ExpandedRole).toBool());

    // A picture chosen on the laptop's Sync page shows at the church, once
    // nothing is live there: it goes on the member list, which waits.
    laptop.facade.stacks()->clearAllStacks();
    REQUIRE(laptop.sync->setComputerPicture(QStringLiteral("headphones"), QStringLiteral("#e0a43a")).isEmpty());
    CHECK_FALSE(laptop.sync->setComputerPicture(QStringLiteral("headphones"), QStringLiteral("orange")).isEmpty());
    CHECK(laptop.sync->computerPicture().value(QStringLiteral("glyph")).toString() == QStringLiteral("headphones"));
    REQUIRE(wait_until([&] {
        return role(at_church, church_had + 1, CollaborationLog::PictureRole).toMap().value(QStringLiteral("glyph"))
               == QStringLiteral("headphones");
    }, 30000));
    CHECK(role(at_church, church_had + 1, CollaborationLog::PictureRole).toMap().value(QStringLiteral("colour"))
          == QStringLiteral("#e0a43a"));
}

TEST_CASE("A viewer's library opens read-only, and a computer that stops syncing leaves the list",
          "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    AppComputer stage(QStringLiteral("Stage display"));
    put_online(church);
    join(church, stage, QStringLiteral("viewer"));
    CHECK(stage.facade.readOnly());
    CHECK(stage.state().value(QStringLiteral("viewOnly")).toBool());
    CHECK_THROWS(stage.db().exec("UPDATE songs SET key = 'C'"));
    // It writes nothing of its own, whatever the merge rules would have it do.
    CHECK(stage.state().value(QStringLiteral("viewOnly")).toBool());
    // An edit made in the window says why it went nowhere.
    QSignalSpy refused(stage.facade.library(), &seventhslide::app::LibraryController::errorOccurred);
    CHECK(stage.facade.library()->createService(QStringLiteral("Vespers")) < 0);
    REQUIRE(refused.size() == 1);
    CHECK_THAT(refused.front().front().toString().toStdString(), ContainsSubstring("view only"));
    // The church's changes still arrive.
    church.db().exec("UPDATE services SET name = 'Divine Service'");
    REQUIRE(wait_until([&] { return stage.text("SELECT name FROM services") == "Divine Service"; }));
    seventhslide::sync::FolderStore stage_store(seventhslide::library::sync_root(
        stage.facade.open_library().registry()->active()->root));
    const auto stage_machine = stage.sync->node().identity().machine;
    CHECK(stage_store.sequences(seventhslide::sync::Numbered::Changes, stage_machine).empty());
    CHECK(std::ranges::none_of(stage_store.snapshots(), [&](const seventhslide::sync::SnapshotName& name) {
        return name.machine == stage_machine;
    }));

    // The stage stops syncing: a plain library again, off the church's list.
    const auto id = stage.facade.libraries()->activeLibraryId();
    REQUIRE(stage.sync->stopSyncing(id).isEmpty());
    const auto stopped = stage.await(QStringLiteral("stopSyncing"));
    REQUIRE(stopped.value(QStringLiteral("ok")).toBool());
    CHECK(stopped.value(QStringLiteral("heard")).toBool());  // the church took the leave
    CHECK_FALSE(stage.facade.readOnly());
    CHECK_FALSE(stage.sync->openLibrary().value(QStringLiteral("synced")).toBool());
    // A library of its own now, under a uid of its own.
    CHECK(stage.library_uid() != church.library_uid());
    stage.db().exec("UPDATE songs SET key = 'C' WHERE title = 'Amazing Grace'");  // its own now
    REQUIRE(wait_until([&] {
        for (const auto& c : church.sync->openLibrary().value(QStringLiteral("computers")).toList()) {
            const auto m = c.toMap();
            if (m.value(QStringLiteral("name")).toString() == QStringLiteral("Stage display")) {
                return m.value(QStringLiteral("removed")).toBool();
            }
        }
        return false;
    }));

    // Put online again, it is a new synced library, not a second founding
    // of the church's.
    REQUIRE(stage.sync->putOnline(k_password).isEmpty());
    REQUIRE(stage.await(QStringLiteral("putOnline")).value(QStringLiteral("ok")).toBool());
    CHECK(stage.sync->openLibrary().value(QStringLiteral("synced")).toBool());
    CHECK(stage.library_uid() != church.library_uid());
}

TEST_CASE("A viewer's library opens with nothing written to it, though its outputs want putting right",
          "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    AppComputer stage(QStringLiteral("Stage display"));
    // The church's output named ``name``, with no song themes: what opening a
    // library puts right.
    const auto without_themes = [&](const std::string& name) {
        seventhslide::db::ConfigRepository config(church.db());
        auto outputs = config.load_outputs();
        REQUIRE(outputs.size() == 1);
        outputs.front().name = name;
        outputs.front().song_themes.clear();
        config.save_outputs(outputs);
    };
    const auto stored = [&] { return seventhslide::db::ConfigRepository(stage.db()).load_outputs(); };
    put_online(church);
    without_themes("Main");
    REQUIRE(wait_until([&] { return seventhslide::db::marked_records(church.db()).empty(); }));
    join(church, stage, QStringLiteral("viewer"));
    CHECK(stage.facade.readOnly());
    CHECK(stage.facade.open_library().last_error().isEmpty());
    // Put right for the window, and kept as the church wrote it.
    REQUIRE(stage.facade.session().outputs().size() == 1);
    CHECK_FALSE(stage.facade.session().outputs().front().song_themes.empty());
    REQUIRE(stored().size() == 1);
    CHECK(stored().front().song_themes.empty());

    // The outputs changed by the church are read again, and put right again
    // without a write.
    without_themes("Main screen");
    REQUIRE(wait_until([&] { return stage.facade.session().outputs().front().name == "Main screen"; }));
    CHECK_FALSE(stage.facade.session().outputs().front().song_themes.empty());
    CHECK(stored().front().song_themes.empty());
    CHECK(stage.facade.open_library().last_error().isEmpty());
}

TEST_CASE("A viewer started again with the library it views open writes nothing to it", "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    AppComputer stage(QStringLiteral("Stage display"));
    // The church's output with no song themes: what opening a library puts
    // right.
    {
        seventhslide::db::ConfigRepository config(church.db());
        auto outputs = config.load_outputs();
        REQUIRE(outputs.size() == 1);
        outputs.front().song_themes.clear();
        config.save_outputs(outputs);
    }
    put_online(church);
    join(church, stage, QStringLiteral("viewer"));
    REQUIRE(stage.facade.readOnly());
    settle(church, stage);
    const auto file =
        seventhslide::library::database_file(stage.facade.libraries()->active_library_path().toStdString());
    const auto stored = [&] {
        auto db = seventhslide::db::Database::open_file(file);
        return seventhslide::db::ConfigRepository(db).load_outputs();
    };
    const auto unsent = [&] {
        auto db = seventhslide::db::Database::open_file(file);
        return seventhslide::db::marked_records(db).size();
    };
    REQUIRE(stored().front().song_themes.empty());
    REQUIRE(unsent() == 0);

    // The stage display quits as SeventhSlide does, and starts again.
    stage.facade.profiles()->capture_active_profile();
    stage.sync.reset();
    static_cast<seventhslide::app::ILibrarySwitcher&>(stage.facade).close_library();
    SessionFacade again;
    QSignalSpy errors(&again, &SessionFacade::errorOccurred);
    again.load_libraries(stage.dir.path);
    SyncController sync(again, stage.options());
    REQUIRE(again.open_active_library());
    CHECK(again.readOnly());
    CHECK(again.open_library().last_error().isEmpty());
    CHECK_FALSE(again.session().outputs().front().song_themes.empty());
    run_events_for(1500);
    CHECK(stored().front().song_themes.empty());
    CHECK(unsent() == 0);
    CHECK(errors.isEmpty());
    CHECK(again.open_library().last_error().isEmpty());
}

TEST_CASE("A synced library deleted here does not come back, and can be joined again", "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    AppComputer laptop(QStringLiteral("Laptop"));
    put_online(church);
    const auto first = laptop.facade.libraries()->activeLibraryId();
    join(church, laptop, QStringLiteral("editor"));
    settle(church, laptop);
    const auto id = laptop.facade.libraries()->activeLibraryId();
    const auto name = laptop.facade.libraries()->activeLibraryName();
    const auto root = laptop.dir.path / "libraries" / id.toStdString();
    REQUIRE(std::filesystem::exists(root / "seventhslide.db"));

    // The computer keeps writing what it had begun, and the folder waits for it.
    REQUIRE(laptop.facade.libraries()->switchLibrary(first).isEmpty());
    REQUIRE(laptop.sync->libraryStates().contains(id));
    std::atomic<bool> writing = false;
    laptop.sync->link().shared().pieces.start([&, media = seventhslide::library::media_root(root)] {
        writing = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        std::filesystem::create_directories(media / "late");
    });
    REQUIRE(wait_until([&] { return writing.load(); }));
    REQUIRE(laptop.facade.libraries()->deleteLibrary(id, name).isEmpty());
    CHECK_FALSE(laptop.sync->libraryStates().contains(id));
    CHECK_FALSE(std::filesystem::exists(root));

    // The church goes on syncing, and nothing brings it back.
    const auto sent = changes_sent(church);
    church.db().exec("UPDATE services SET name = 'Divine Service'");
    REQUIRE(wait_until([&] { return changes_sent(church) > sent; }));
    settle(church, laptop);
    CHECK_FALSE(std::filesystem::exists(root));
    CHECK(church.text("SELECT name FROM services") == "Divine Service");

    // Invited again, it comes whole.
    join(church, laptop, QStringLiteral("editor"));
    settle(church, laptop);
    seventhslide::test::check_same_records(laptop.db(), church.db());
    CHECK(laptop.text("SELECT name FROM services") == "Divine Service");
}

TEST_CASE("A sync leaves the lists where they are scrolled", "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    AppComputer laptop(QStringLiteral("Laptop"));
    for (int i = 0; i < 200; ++i) {
        church.db().exec(("INSERT INTO songs (title, lyrics) VALUES ('Song " + std::to_string(1000 + i)
                          + "', '[V1]\nline')"));
    }
    put_online(church);
    join(church, laptop, QStringLiteral("editor"));
    settle(church, laptop);

    seventhslide::test::OperatorUi ui(laptop.facade, QStringLiteral("songs"), laptop.sync.get());
    REQUIRE(ui.window() != nullptr);
    ui.window()->resize(1400, 900);
    ui.window()->show();
    run_events_for(500);

    auto* songs = laptop.facade.library()->songs();
    QQuickItem* view = nullptr;
    for (auto* item : ui.window()->findChildren<QQuickItem*>()) {
        if (item->inherits("QQuickListView") && item->property("model").value<QObject*>() == songs) {
            view = item;
        }
    }
    REQUIRE(view != nullptr);
    const qreal scrolled = view->property("contentHeight").toReal() / 2;
    REQUIRE(scrolled > 1000);
    REQUIRE(view->setProperty("contentY", scrolled));
    run_events_for(200);

    /// The title of the song at the top of the list as it shows now.
    const auto top_song = [&] {
        int row = -1;
        REQUIRE(QMetaObject::invokeMethod(view, "indexAt", Q_RETURN_ARG(int, row), Q_ARG(qreal, 20.0),
                                          Q_ARG(qreal, view->property("contentY").toReal() + 4)));
        REQUIRE(row >= 0);
        return songs->data(songs->index(row), seventhslide::app::SongListModel::TitleRole).toString();
    };
    const auto top = top_song();
    QSignalSpy resets(songs, &QAbstractItemModel::modelReset);

    // Songs added above and below it, one gone above, one edited.
    const auto rows = songs->rowCount();
    church.db().exec("INSERT INTO songs (title, lyrics) VALUES ('A Mighty Fortress', '[V1]\nline')");
    church.db().exec("INSERT INTO songs (title, lyrics) VALUES ('Zion', '[V1]\nline')");
    church.db().exec("DELETE FROM songs WHERE title = 'Amazing Grace'");
    church.db().exec("UPDATE songs SET key = 'D' WHERE title = 'It Is Well'");
    REQUIRE(wait_until([&] { return songs->rowCount() == rows + 1; }));
    run_events_for(300);
    CHECK(resets.isEmpty());
    CHECK(top_song() == top);
    CHECK(view->property("contentY").toReal() > 1000);

    // The same for the rundown open.
    const auto service = std::stoll(laptop.text("SELECT id FROM services"));
    REQUIRE(laptop.facade.library()->openService(service));
    auto* items = laptop.facade.serviceItems();
    const auto item_rows = items->rowCount();
    QSignalSpy item_resets(items, &QAbstractItemModel::modelReset);
    church.db().exec("INSERT INTO service_items (service_id, order_num, item_type) "
                     "SELECT id, 5, 'divider' FROM services");
    REQUIRE(wait_until([&] { return items->rowCount() == item_rows + 1; }));
    CHECK(item_resets.isEmpty());
    INFO(ui.warnings());
    CHECK(ui.warnings().empty());
}

TEST_CASE("A library synced by an earlier build is put online afresh, and every computer gets it whole",
          "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    AppComputer laptop(QStringLiteral("Laptop"));
    fill(church.db());
    church.facade.refresh_library();
    const auto* entry = church.facade.open_library().registry()->active();
    const auto root = entry->root;

    // A picture, and all an earlier build's sync left of it: its files, which
    // this one cannot read, and what the database kept, the picture declared
    // by a whole-file hash.
    const auto picture = entry->media_root() / "images/ab12.png";
    std::filesystem::create_directories(picture.parent_path());
    std::ofstream(picture, std::ios::binary) << "lilies";
    church.db().exec("INSERT INTO image_files (filename, display_name) VALUES ('ab12.png', 'Lilies')");
    const auto old_entry = root / "sync/members/94c81bf2d25e604917e951d4f6227ee3/0000000001.json";
    const auto old_change = root / "sync/changes/94c81bf2d25e604917e951d4f6227ee3/0000000001.json";
    std::filesystem::create_directories(old_entry.parent_path());
    std::filesystem::create_directories(old_change.parent_path());
    std::ofstream(old_entry) << R"({"format":1,"body":{"kind":"found"},"signatures":[]})";
    std::ofstream(old_change) << R"({"format":1,"body":{},"signatures":[]})";
    const std::string old_hash(64, 'a');
    church.db().exec("INSERT INTO sync_media (path, size, sha256, counter, machine) VALUES ('images/ab12.png', 6, '"
                     + old_hash + "', 1, 'old')");
    church.db().exec("INSERT INTO sync_applied (machine_id, sequence) VALUES ('old', 5)");
    const auto old_uid = church.library_uid();

    // Started again on this build, it is a plain library.
    church.start();
    CHECK_FALSE(church.sync->openLibrary().value(QStringLiteral("synced")).toBool());
    REQUIRE(church.sync->putOnline(k_password).isEmpty());
    REQUIRE(church.await(QStringLiteral("putOnline")).value(QStringLiteral("ok")).toBool());
    CHECK(church.sync->openLibrary().value(QStringLiteral("synced")).toBool());

    // A new library, what was left kept beside it and not used.
    CHECK(church.library_uid() != old_uid);
    bool kept_aside = false;
    for (const auto& item : std::filesystem::directory_iterator(root)) {
        kept_aside = kept_aside
                     || (item.path().filename().string().starts_with("sync-left-")
                         && std::filesystem::exists(item.path() / "members/94c81bf2d25e604917e951d4f6227ee3"));
    }
    CHECK(kept_aside);
    CHECK_FALSE(std::filesystem::exists(old_change));
    CHECK(church.text("SELECT COUNT(*) FROM sync_applied WHERE machine_id = 'old'") == "0");

    // Sent as it is now: the computer that joins takes all of it.
    REQUIRE(wait_until([&] {
        return church.text("SELECT COUNT(*) FROM sync_media WHERE path = 'images/ab12.png' AND sha256 <> '" + old_hash
                           + "'")
               == "1";
    }));
    QStringList refused;
    QObject::connect(&laptop.sync->link(), &seventhslide::link::Link::refused,
                     [&](const QString& why) { refused.push_back(why); });
    join(church, laptop, QStringLiteral("editor"));
    CHECK(laptop.text("SELECT COUNT(*) FROM songs") == "2");
    const auto* laptop_entry = laptop.facade.open_library().registry()->active();
    REQUIRE(wait_until([&] { return std::filesystem::exists(laptop_entry->media_root() / "images/ab12.png"); }));
    CHECK(refused.isEmpty());
}

TEST_CASE("A join goes on until the computer the invite came from is reached, through a restart",
          "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    AppComputer laptop(QStringLiteral("Laptop"));
    put_online(church);
    REQUIRE(church.sync->admin()->invite(QStringLiteral("editor"), 7, k_password).isEmpty());
    const auto code = church.await(QStringLiteral("invite")).value(QStringLiteral("code")).toString();
    const auto port = church.sync->port();
    church.sync.reset();  // the church PC is switched off

    REQUIRE(laptop.sync->joinLibrary(code).isEmpty());
    const auto joined = laptop.await(QStringLiteral("join"));
    REQUIRE(joined.value(QStringLiteral("ok")).toBool());
    const auto id = joined.value(QStringLiteral("id")).toString();
    run_events_for(1500);
    CHECK(laptop.state_of(id) == QStringLiteral("joining"));
    // Not here yet: it cannot be opened, so nothing of the laptop's own goes
    // into it.
    CHECK_THAT(laptop.facade.libraries()->switchLibrary(id).toStdString(), ContainsSubstring("on its way"));

    // The laptop restarts, then the church PC comes back where it was.
    laptop.start();
    church.start(port);
    REQUIRE(wait_until([&] { return laptop.state_of(id) == QStringLiteral("uptodate"); }, 60000));
    REQUIRE(laptop.facade.libraries()->switchLibrary(id).isEmpty());
    settle(church, laptop);
    seventhslide::test::check_same_records(laptop.db(), church.db());
}

TEST_CASE("Stopping says when it could not be done, and when no other computer heard", "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    AppComputer laptop(QStringLiteral("Laptop"));
    put_online(church);
    join(church, laptop, QStringLiteral("editor"));
    const auto id = laptop.facade.libraries()->activeLibraryId();
    const std::filesystem::path root = laptop.facade.libraries()->active_library_path().toStdString();
    church.sync.reset();  // no one to hear the laptop leave

#ifndef _WIN32
    // The library's folder cannot be written to: its sync folder stays.
    std::filesystem::permissions(root, std::filesystem::perms::owner_write, std::filesystem::perm_options::remove);
    REQUIRE(laptop.sync->stopSyncing(id).isEmpty());
    const auto refused = laptop.await(QStringLiteral("stopSyncing"));
    std::filesystem::permissions(root, std::filesystem::perms::owner_write, std::filesystem::perm_options::add);
    CHECK_FALSE(refused.value(QStringLiteral("ok")).toBool());
    CHECK_THAT(refused.value(QStringLiteral("error")).toString().toStdString(),
               ContainsSubstring("could not be moved aside"));
    // Still synced here, with this computer off the list: it can be tried again.
    REQUIRE(wait_until([&] { return laptop.state_of(id) == QStringLiteral("removed"); }, 5000));
#endif

    REQUIRE(laptop.sync->stopSyncing(id).isEmpty());
    const auto stopped = laptop.await(QStringLiteral("stopSyncing"));
    REQUIRE(stopped.value(QStringLiteral("ok")).toBool());
    CHECK_FALSE(stopped.value(QStringLiteral("heard")).toBool());
    CHECK_FALSE(laptop.sync->openLibrary().value(QStringLiteral("synced")).toBool());
    CHECK(laptop.library_uid() != church.library_uid());
}

TEST_CASE("The Sync page's password cards hand on what was typed", "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    fill(church.db());
    seventhslide::test::OperatorUi ui(church.facade, QStringLiteral("songs"), church.sync.get());
    REQUIRE(ui.window() != nullptr);
    QObject* dialog = ui.find_component("AppSettingsDialog");
    REQUIRE(dialog != nullptr);
    REQUIRE(QMetaObject::invokeMethod(dialog, "openPage", Q_ARG(QVariant, QStringLiteral("sync"))));
    QObject* page = ui.find_component("SettingsSyncState");
    REQUIRE(page != nullptr);
    const auto cards = [&](const char* type) {
        std::vector<QObject*> out;
        const QString prefix = QString::fromLatin1(type) + QStringLiteral("_QMLTYPE_");
        for (QObject* o : ui.window()->findChildren<QObject*>()) {
            if (QString::fromLatin1(o->metaObject()->className()).startsWith(prefix)) {
                out.push_back(o);
            }
        }
        return out;
    };

    // Put online: the new password, twice.
    REQUIRE(QMetaObject::invokeMethod(page, "putOnline"));
    QObject* card = nullptr;
    for (QObject* o : cards("AppDialog")) {
        if (o->property("open").toBool()) {
            card = o;
        }
    }
    REQUIRE(card != nullptr);
    // Typed into each field's input, as a person does, then the field left:
    // what was typed must stay there (the field puts its own text back when
    // it loses the focus).
    int typed = 0;
    for (QObject* field : card->findChildren<QObject*>()) {
        if (QString::fromLatin1(field->metaObject()->className()).startsWith(QStringLiteral("DialogField_QMLTYPE_"))
            && field->property("password").toBool()) {
            QQuickItem* input = nullptr;
            for (QQuickItem* child : qobject_cast<QQuickItem*>(field)->childItems()) {
                if (child->inherits("QQuickTextInput")) {
                    input = child;
                }
            }
            REQUIRE(input != nullptr);
            input->forceActiveFocus();
            input->setProperty("text", QString::fromLatin1(k_password));
            input->setFocus(false);
            CHECK(input->property("text").toString() == QString::fromLatin1(k_password));
            ++typed;
        }
    }
    REQUIRE(typed == 2);
    REQUIRE(QMetaObject::invokeMethod(card, "submit"));
    REQUIRE(church.await(QStringLiteral("putOnline")).value(QStringLiteral("ok")).toBool());
    run_events_for(100);
    for (QObject* field : card->findChildren<QObject*>()) {
        if (field->property("password").toBool()) {
            CHECK(field->property("text").toString().isEmpty());  // not kept once closed
        }
    }

    // An invite: the admin password.
    REQUIRE(QMetaObject::invokeMethod(page, "invite", Q_ARG(QVariant, QStringLiteral("viewer"))));
    QObject* prompt = nullptr;
    for (QObject* o : cards("AppPromptDialog")) {
        if (o->property("open").toBool()) {
            prompt = o;
        }
    }
    REQUIRE(prompt != nullptr);
    CHECK(prompt->property("password").toBool());
    prompt->setProperty("value", QString::fromLatin1(k_password));
    REQUIRE(QMetaObject::invokeMethod(prompt, "submit"));
    const auto invite = church.await(QStringLiteral("invite"));
    CHECK(invite.value(QStringLiteral("ok")).toBool());
    CHECK(invite.value(QStringLiteral("error")).toString().isEmpty());
    run_events_for(100);
    CHECK(prompt->property("value").toString().isEmpty());
    CHECK(ui.warnings().empty());
}

TEST_CASE("The Sync page's address controls and Edit card do what they say", "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    AppComputer laptop(QStringLiteral("Laptop"));
    put_online(church);
    join(church, laptop, QStringLiteral("viewer"));
    seventhslide::test::OperatorUi ui(church.facade, QStringLiteral("songs"), church.sync.get());
    REQUIRE(ui.window() != nullptr);
    QObject* dialog = ui.find_component("AppSettingsDialog");
    REQUIRE(dialog != nullptr);
    REQUIRE(QMetaObject::invokeMethod(dialog, "openPage", Q_ARG(QVariant, QStringLiteral("sync"))));
    QObject* page = ui.find_component("SettingsSyncState");
    REQUIRE(page != nullptr);
    const auto open_prompt = [&]() -> QObject* {
        const QString prefix = QStringLiteral("AppPromptDialog_QMLTYPE_");
        for (QObject* o : ui.window()->findChildren<QObject*>()) {
            if (QString::fromLatin1(o->metaObject()->className()).startsWith(prefix) && o->property("open").toBool()) {
                return o;
            }
        }
        return nullptr;
    };
    const auto answer = [&](const QString& value) {
        QObject* prompt = open_prompt();
        REQUIRE(prompt != nullptr);
        prompt->setProperty("value", value);
        REQUIRE(QMetaObject::invokeMethod(prompt, "submit"));
    };
    const auto computer = [&](const QString& machine) {
        for (const auto& c : church.sync->openLibrary().value(QStringLiteral("computers")).toList()) {
            if (c.toMap().value(QStringLiteral("machine")).toString() == machine) {
                return c.toMap();
            }
        }
        return QVariantMap{};
    };
    const auto machine = q_machine(laptop);

    // Its own address and port; what will not do is said.
    QSignalSpy reported(page, SIGNAL(reported(QString)));
    REQUIRE(QMetaObject::invokeMethod(page, "setReachAddresses", Q_ARG(QVariant, QStringLiteral("slides.example.org"))));
    CHECK(church.sync->reachAddresses() == QStringList{QStringLiteral("slides.example.org")});
    REQUIRE(QMetaObject::invokeMethod(page, "setChosenPort", Q_ARG(QVariant, QStringLiteral("not a port"))));
    CHECK(reported.size() == 1);
    REQUIRE(QMetaObject::invokeMethod(page, "setChosenPort", Q_ARG(QVariant, QStringLiteral("9001"))));
    CHECK(church.sync->chosenPort() == 9001);
    CHECK(church.sync->portPending());
    CHECK(reported.size() == 1);

    // The Edit card: open for another computer, not for this one.
    const auto open_card = [&]() -> QObject* {
        const QString prefix = QStringLiteral("ComputerEditDialog_QMLTYPE_");
        for (QObject* o : ui.window()->findChildren<QObject*>()) {
            if (QString::fromLatin1(o->metaObject()->className()).startsWith(prefix) && o->property("open").toBool()) {
                return o;
            }
        }
        return nullptr;
    };
    const auto strings = [](const QVariant& v) { return v.toStringList(); };
    page->setProperty("selectedKey", q_machine(church));
    CHECK_FALSE(page->property("canEditSelected").toBool());
    page->setProperty("selectedKey", machine);
    REQUIRE(wait_until([&] { return page->property("canEditSelected").toBool(); }));
    REQUIRE(QMetaObject::invokeMethod(page, "editSelected"));
    QObject* card = open_card();
    REQUIRE(card != nullptr);
    CHECK(card->property("role").toString() == QStringLiteral("viewer"));
    CHECK(strings(card->property("typed")).isEmpty());

    // An address added: set on Save, with no password, as the role is the same.
    card->setProperty("typed", QStringList{QStringLiteral("stage.example.org")});
    REQUIRE(QMetaObject::invokeMethod(card, "submit"));
    CHECK(open_prompt() == nullptr);
    REQUIRE(wait_until([&] {
        return strings(computer(machine).value(QStringLiteral("typedAddresses")))
               == QStringList{QStringLiteral("stage.example.org:8766")};
    }));

    // Opened again, once the page has it, it shows what was added; the admin
    // key for the viewer, and the address taken away, on one Save: the
    // password for the key.
    REQUIRE(wait_until([&] {
        return strings(page->property("selectedComputer").toMap().value(QStringLiteral("typedAddresses")))
               == QStringList{QStringLiteral("stage.example.org:8766")};
    }));
    REQUIRE(QMetaObject::invokeMethod(page, "editSelected"));
    card = open_card();
    REQUIRE(card != nullptr);
    CHECK(strings(card->property("typed")) == QStringList{QStringLiteral("stage.example.org:8766")});
    CHECK_FALSE(card->property("holds").toBool());
    card->setProperty("holds", true);
    card->setProperty("typed", QStringList{});
    REQUIRE(QMetaObject::invokeMethod(card, "submit"));
    REQUIRE(open_prompt() != nullptr);
    CHECK(open_prompt()->property("password").toBool());
    answer(QString::fromLatin1(k_password));
    REQUIRE(church.await(QStringLiteral("role")).value(QStringLiteral("ok")).toBool());
    REQUIRE(wait_until([&] { return computer(machine).value(QStringLiteral("holdsAdminKey")).toBool(); }));
    CHECK(strings(computer(machine).value(QStringLiteral("typedAddresses"))).isEmpty());

    // Made an editor, then a viewer again: a viewer without the admin key, as
    // the card shows it, unless it is asked for.
    const auto selected_role = [&] {
        return page->property("selectedComputer").toMap().value(QStringLiteral("role")).toString();
    };
    // Edit waits, as its button does, while the page is busy.
    const auto edit = [&] {
        REQUIRE(wait_until([&] { return !page->property("working").toBool(); }));
        REQUIRE(QMetaObject::invokeMethod(page, "editSelected"));
    };
    const auto make = [&](const QString& role) {
        edit();
        card = open_card();
        REQUIRE(card != nullptr);
        card->setProperty("role", role);
        REQUIRE(QMetaObject::invokeMethod(card, "submit"));
        answer(QString::fromLatin1(k_password));
        REQUIRE(church.await(QStringLiteral("role")).value(QStringLiteral("ok")).toBool());
        REQUIRE(wait_until([&] { return selected_role() == role; }));
    };
    make(QStringLiteral("editor"));
    edit();
    card = open_card();
    REQUIRE(card != nullptr);
    CHECK_FALSE(card->property("holds").toBool());
    card->setProperty("open", false);
    make(QStringLiteral("viewer"));
    CHECK_FALSE(computer(machine).value(QStringLiteral("holdsAdminKey")).toBool());

    // An invite: a row of its own, where Edit says where its computer is, no
    // password asked; then withdrawn, with the password.
    REQUIRE(QMetaObject::invokeMethod(page, "invite", Q_ARG(QVariant, QStringLiteral("viewer"))));
    answer(QString::fromLatin1(k_password));
    const auto made = church.await(QStringLiteral("invite"));
    REQUIRE(made.value(QStringLiteral("ok")).toBool());
    const auto id = QString::fromStdString(
        seventhslide::sync::read_invite(made.value(QStringLiteral("code")).toString().toStdString()).invite.id);
    wait_until([&] { return open_prompt() == nullptr; }, 1000);
    page->setProperty("selectedKey", QStringLiteral("invite:") + id);
    REQUIRE(wait_until([&] { return page->property("canEditSelected").toBool(); }));
    REQUIRE(QMetaObject::invokeMethod(page, "editSelected"));
    card = open_card();
    REQUIRE(card != nullptr);
    CHECK(card->property("invite").toBool());
    card->setProperty("typed", QStringList{QStringLiteral("far.example.org:9000")});
    REQUIRE(QMetaObject::invokeMethod(card, "submit"));
    CHECK(open_prompt() == nullptr);
    CHECK(church.sync->node().reach().invite_addresses(church.sync->open_sync()->uid(), id.toStdString())
          == std::vector<std::string>{"far.example.org:9000"});

    REQUIRE(page->property("canRemoveSelected").toBool());
    REQUIRE(QMetaObject::invokeMethod(page, "removeSelected"));
    QObject* confirm = nullptr;
    for (QObject* o : ui.window()->findChildren<QObject*>()) {
        if (o->property("action").toString() == QStringLiteral("withdraw") && o->property("open").toBool()) {
            confirm = o;
        }
    }
    REQUIRE(confirm != nullptr);
    REQUIRE(QMetaObject::invokeMethod(confirm, "submit"));
    REQUIRE(open_prompt() != nullptr);
    answer(QString::fromLatin1(k_password));
    REQUIRE(church.await(QStringLiteral("withdraw")).value(QStringLiteral("ok")).toBool());
    REQUIRE(wait_until([&] { return !page->property("canEditSelected").toBool(); }));
    CHECK(ui.warnings().empty());
}

TEST_CASE("Media over the size limit: the import says so, and the library's state", "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    put_online(church);
    REQUIRE(church.sync->admin()->setSizeLimit(0.000001, k_password).isEmpty());  // 1000 bytes
    REQUIRE(church.await(QStringLiteral("limit")).value(QStringLiteral("ok")).toBool());
    REQUIRE(wait_until([&] { return church.sync->openLibrary().value(QStringLiteral("sizeLimit")).toDouble() == 1000; }));
    church.sync->syncNow();
    run_events_for(1000);

    const auto source = church.dir.path / "Prelude in G.mp3";
    std::ofstream(source, std::ios::binary) << std::string(5000, 'x');
    QSignalSpy told(church.facade.media(), &seventhslide::app::MediaController::errorOccurred);
    QSignalSpy finished(church.facade.media(), &seventhslide::app::MediaController::importFinished);
    church.facade.media()->import_files(seventhslide::db::MediaKind::Audio,
                                       {QString::fromStdString(source.string())}, -1);
    REQUIRE(wait_until([&] { return finished.size() == 1; }));
    REQUIRE(told.size() == 1);
    CHECK_THAT(told.front().front().toString().toStdString(), ContainsSubstring("“Prelude in G.mp3” is imported"));
    CHECK_THAT(told.front().front().toString().toStdString(), ContainsSubstring("won't sync"));

    // Once synced, the library says what does not.
    REQUIRE(wait_until([&] { return church.state().value(QStringLiteral("state")).toString() == QStringLiteral("full"); }));
    CHECK_THAT(church.state().value(QStringLiteral("text")).toString().toStdString(),
               ContainsSubstring("“Prelude in G.mp3” isn't syncing"));
    CHECK(church.state().value(QStringLiteral("trouble")).toBool());

    // Room made: it goes.
    REQUIRE(church.sync->admin()->setSizeLimit(1, k_password).isEmpty());
    REQUIRE(church.await(QStringLiteral("limit")).value(QStringLiteral("ok")).toBool());
    REQUIRE(wait_until([&] { return church.state().value(QStringLiteral("state")).toString() != QStringLiteral("full"); }));
    CHECK(church.sync->openLibrary().value(QStringLiteral("sizeUsed")).toDouble() >= 5000);
}

TEST_CASE("A record too large to sync: the library's state says what stays here, and what to do", "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    put_online(church);
    // Some 13 MB of lyrics, and a service item playing it.
    church.db().exec("INSERT INTO songs (title, lyrics) VALUES ('Psalm 119', "
                     "'[V1]\n' || substr(hex(randomblob(6600000)), 1, 13000000))");
    church.db().exec("INSERT INTO service_items (service_id, song_id, order_num, item_type) "
                     "SELECT (SELECT id FROM services), id, 5, 'song' FROM songs WHERE title = 'Psalm 119'");
    church.sync->syncNow();
    REQUIRE(wait_until([&] { return church.state().value(QStringLiteral("state")).toString() == QStringLiteral("problem"); },
                       30000));
    const auto said = QStringLiteral("“Psalm 119” is too large to sync, so it and 1 record(s) using it stay on this "
                                     "computer. Shorten it or delete it.");
    CHECK(church.state().value(QStringLiteral("text")).toString() == said);
    CHECK(church.state().value(QStringLiteral("tooLarge")).toString() == said);
    CHECK(church.state().value(QStringLiteral("trouble")).toBool());

    // Made smaller: it goes, with the item.
    church.db().exec("UPDATE songs SET lyrics = '[V1]\nGreat peace' WHERE title = 'Psalm 119'");
    church.sync->syncNow();
    REQUIRE(wait_until([&] { return church.state().value(QStringLiteral("state")).toString() != QStringLiteral("problem"); },
                       30000));
    CHECK(church.state().value(QStringLiteral("tooLarge")).toString().isEmpty());
}

TEST_CASE("A snapshot that cannot be written: the library's state and the status say why, and that sync goes on",
          "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    put_online(church);
    auto* open = church.sync->open_sync();
    const auto me = q_machine(church).toStdString();
    REQUIRE(wait_until([&] {
        return !open->store().sequences(seventhslide::sync::Numbered::Changes, me).empty() && !open->busy();
    }));
    // Its first change file let go of with no snapshot here: one is due at
    // once. Where parts go is a file, so none can be written.
    open->store().drop_changes_through(me, 1);
    std::ofstream(seventhslide::library::sync_root(open->root()) / "snapshot_parts") << "not a folder";
    church.sync->syncNow();
    REQUIRE(wait_until([&] { return church.state().value(QStringLiteral("state")).toString() == QStringLiteral("problem"); },
                       30000));
    const auto said = church.state().value(QStringLiteral("text")).toString();
    CHECK(said.startsWith(QStringLiteral("A snapshot's part could not be written: ")));
    CHECK(said.endsWith(QStringLiteral(".json. Old change files are kept until a snapshot can be written. Sync goes on.")));
    CHECK(church.state().value(QStringLiteral("snapshotProblem")).toString() == said);
    CHECK(church.state().value(QStringLiteral("tooLarge")).toString().isEmpty());
    CHECK(church.state().value(QStringLiteral("trouble")).toBool());

    // In the status a server gives too.
    const auto status = church.sync->node().status();
    REQUIRE(status["libraries"].size() == 1);
    CHECK_THAT(status["libraries"][0]["snapshot_problem"].get<std::string>(),
               ContainsSubstring("A snapshot's part could not be written: "));

    // A record too large to sync goes before it; it is still said.
    church.db().exec("INSERT INTO songs (title, lyrics) VALUES ('Psalm 119', "
                     "'[V1]\n' || substr(hex(randomblob(6600000)), 1, 13000000))");
    church.sync->syncNow();
    REQUIRE(wait_until([&] { return !church.state().value(QStringLiteral("tooLarge")).toString().isEmpty(); }, 30000));
    CHECK_THAT(church.state().value(QStringLiteral("text")).toString().toStdString(), ContainsSubstring("“Psalm 119”"));
    CHECK(church.state().value(QStringLiteral("snapshotProblem")).toString() == said);
}

TEST_CASE("Saving Settings keeps a change another computer made meanwhile", "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    seventhslide::test::OperatorUi ui(church.facade, QStringLiteral("songs"), church.sync.get());
    REQUIRE(ui.window() != nullptr);
    QObject* dialog = ui.find_component("AppSettingsDialog");
    QObject* general = ui.find_component("SettingsGeneralState");
    REQUIRE(dialog != nullptr);
    REQUIRE(general != nullptr);
    REQUIRE(QMetaObject::invokeMethod(dialog, "openDialog"));

    // While the dialog is open, the licence number changes underneath (as a
    // sync would bring it), and the operator changes only the name.
    REQUIRE(church.facade.updateAppSettings({{QStringLiteral("ccliLicenceNumber"), QStringLiteral("998877")}})
                .isEmpty());
    general->setProperty("organizationName", QStringLiteral("Grace Church"));
    REQUIRE(QMetaObject::invokeMethod(dialog, "save"));
    const auto saved = church.facade.getAppSettings();
    CHECK(saved.value(QStringLiteral("organizationName")).toString() == QStringLiteral("Grace Church"));
    CHECK(saved.value(QStringLiteral("ccliLicenceNumber")).toString() == QStringLiteral("998877"));
}

TEST_CASE("The Sync page's admin work, with the password", "[syncapp]")
{
    AppComputer church(QStringLiteral("Church PC"));
    CHECK_FALSE(church.sync->passphraseProblem(QStringLiteral("short")).isEmpty());
    CHECK(church.sync->putOnline(QStringLiteral("short")).size() > 0);
    put_online(church);
    CHECK_FALSE(church.sync->putOnline(k_password).isEmpty());  // online already

    REQUIRE(church.sync->admin()->invite(QStringLiteral("editor"), 7, QStringLiteral("not the password")).isEmpty());
    const auto wrong = church.await(QStringLiteral("invite"));
    CHECK_FALSE(wrong.value(QStringLiteral("ok")).toBool());
    CHECK(wrong.value(QStringLiteral("error")).toString() == QStringLiteral("That is not the library's admin password."));

    CHECK_FALSE(church.sync->admin()->setSizeLimit(std::numeric_limits<double>::infinity(), k_password).isEmpty());
    CHECK_FALSE(church.sync->admin()->setSizeLimit(-1, k_password).isEmpty());
    REQUIRE(church.sync->admin()->setSizeLimit(50, k_password).isEmpty());
    REQUIRE(church.await(QStringLiteral("limit")).value(QStringLiteral("ok")).toBool());
    CHECK(church.sync->openLibrary().value(QStringLiteral("sizeLimit")).toDouble() == 50e9);

    REQUIRE(church.sync->admin()->changePassword(k_password, QStringLiteral("consider the ravens now"), false).isEmpty());
    const auto changed = church.await(QStringLiteral("password"));
    REQUIRE(changed.value(QStringLiteral("ok")).toBool());
    CHECK_FALSE(changed.contains(QStringLiteral("recoveryCode")));
    REQUIRE(church.sync->admin()->newRecoveryCode(QStringLiteral("consider the ravens now")).isEmpty());
    const auto code = church.await(QStringLiteral("recovery")).value(QStringLiteral("recoveryCode")).toString();
    REQUIRE(code.size() > 40);
    REQUIRE(church.sync->admin()->changePassword(code, QStringLiteral("the sparrows are fed"), true).isEmpty());
    const auto recovered = church.await(QStringLiteral("password"));
    REQUIRE(recovered.value(QStringLiteral("ok")).toBool());
    CHECK(recovered.value(QStringLiteral("recoveryCode")).toString() != code);

    CHECK_FALSE(church.sync->joinLibrary(QStringLiteral("not an invite")).isEmpty());
    CHECK_FALSE(church.sync->setInviteAddresses(QStringLiteral("feedfacefeedface"), {QStringLiteral("a:b:c d")}).isEmpty());
    CHECK(church.sync->setComputerName(QStringLiteral("  ")).size() > 0);
    REQUIRE(church.sync->setComputerName(QStringLiteral("Booth PC")).isEmpty());
    CHECK(church.sync->computerName() == QStringLiteral("Booth PC"));
}

// Hidden: the Sync page, Libraries & Profiles and the bottom bar of a synced
// library, drawn to SEVENTHSLIDE_SHOTS_DIR to look at.
TEST_CASE("Sync shots", "[.syncshots]")
{
    const char* out = std::getenv("SEVENTHSLIDE_SHOTS_DIR");
    REQUIRE(out != nullptr);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    AppComputer church(QStringLiteral("Church PC"));
    AppComputer laptop(QStringLiteral("Laptop"));
    const auto shoot = [&](AppComputer& c, const char* tab, const std::string& name,
                           const std::function<void(seventhslide::test::OperatorUi&)>& prepare = {}) {
        seventhslide::test::OperatorUi ui(c.facade, QStringLiteral("songs"), c.sync.get());
        REQUIRE(ui.window() != nullptr);
        ui.window()->resize(1400, tab != nullptr ? 900 : 1100);
        ui.window()->show();
        if (tab != nullptr) {
            QObject* dialog = ui.find_component("AppSettingsDialog");
            REQUIRE(dialog != nullptr);
            QMetaObject::invokeMethod(dialog, "openPage", Q_ARG(QVariant, QString::fromLatin1(tab)));
        }
        if (prepare) {
            prepare(ui);
        }
        run_events_for(800);
        REQUIRE(ui.window()->grabWindow().save(QString::fromStdString(std::string(out) + "/" + name + ".png")));
        std::cout << ui.warnings();
    };
    shoot(church, "sync", "sync_offline");
    put_online(church);
    join(church, laptop, QStringLiteral("viewer"));
    settle(church, laptop);
    shoot(church, "sync", "sync_online");
    REQUIRE(church.sync->setReachAddresses(QStringLiteral("slides.example.org")).isEmpty());
    shoot(church, "sync", "sync_reached_at");
    shoot(church, "sync", "sync_computers", [&](seventhslide::test::OperatorUi& ui) {
        ui.find_component("SettingsSyncState")->setProperty("selectedKey", q_machine(laptop));
        ui.find_component("SettingsSyncPanel")->setProperty("contentY", 535);
    });
    REQUIRE(church.sync->setComputerAddresses(q_machine(laptop), {QStringLiteral("laptop.example.org")}).isEmpty());
    shoot(church, "sync", "sync_edit_computer", [&](seventhslide::test::OperatorUi& ui) {
        QObject* state = ui.find_component("SettingsSyncState");
        state->setProperty("selectedKey", q_machine(laptop));
        REQUIRE(QMetaObject::invokeMethod(state, "editSelected"));
    });
    REQUIRE(church.sync->setComputerAddresses(q_machine(laptop), {}).isEmpty());
    REQUIRE(church.sync->admin()->invite(QStringLiteral("viewer"), 7, k_password).isEmpty());
    const auto waiting = church.await(QStringLiteral("invite"));
    const auto waiting_id = QString::fromStdString(
        seventhslide::sync::read_invite(waiting.value(QStringLiteral("code")).toString().toStdString()).invite.id);
    REQUIRE(church.sync->setInviteAddresses(waiting_id, {QStringLiteral("cloud.example.org")}).isEmpty());
    shoot(church, "sync", "sync_invite_row", [&](seventhslide::test::OperatorUi& ui) {
        ui.find_component("SettingsSyncState")->setProperty("selectedKey", QStringLiteral("invite:") + waiting_id);
        ui.find_component("SettingsSyncPanel")->setProperty("contentY", 535);
    });
    shoot(church, "sync", "sync_edit_invite", [&](seventhslide::test::OperatorUi& ui) {
        QObject* state = ui.find_component("SettingsSyncState");
        state->setProperty("selectedKey", QStringLiteral("invite:") + waiting_id);
        REQUIRE(QMetaObject::invokeMethod(state, "editSelected"));
    });
    REQUIRE(church.sync->setReachAddresses(QString()).isEmpty());
    shoot(church, "library", "sync_libraries");
    shoot(laptop, nullptr, "sync_viewer_window");
    shoot(laptop, "sync", "sync_viewer_page");

    // Removing the synced library, kept and then deleted for its typed name.
    const auto joined = laptop.facade.libraries()->activeLibraryId();
    REQUIRE(laptop.facade.libraries()->switchLibrary(QStringLiteral("default")).isEmpty());
    const auto remove = [&](bool delete_folder, const QString& typed) {
        return [=](seventhslide::test::OperatorUi& ui) {
            QObject* state = ui.find_component("SettingsLibraryState");
            REQUIRE(state != nullptr);
            REQUIRE(QMetaObject::invokeMethod(state, "chooseLibrary", Q_ARG(QVariant, joined)));
            REQUIRE(QMetaObject::invokeMethod(state, "beginRemoveLibrary"));
            REQUIRE(state->setProperty("deleteFolder", delete_folder));
            REQUIRE(state->setProperty("deleteTyped", typed));
        };
    };
    shoot(laptop, "library", "remove_library", remove(false, {}));
    shoot(laptop, "library", "remove_library_delete", remove(true, QStringLiteral("Def")));
}

TEST_CASE("Collaboration shots", "[.syncshots]")
{
    using seventhslide::app::CollaborationLog;
    const char* out = std::getenv("SEVENTHSLIDE_SHOTS_DIR");
    REQUIRE(out != nullptr);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    AppComputer church(QStringLiteral("John's MacBook"));
    AppComputer laptop(QStringLiteral("Jane's PC"));
    put_online(church);
    join(church, laptop, QStringLiteral("editor"));
    REQUIRE(church.sync->setComputerPicture(QStringLiteral("song"), QStringLiteral("#2fb3a0")).isEmpty());
    auto* log = church.sync->log();
    REQUIRE(wait_until([&] { return log->canPost() && laptop.sync->log()->canPost() && log->rowCount() > 0; }));
    const auto rows = [&](int n) { REQUIRE(wait_until([&] { return log->rowCount() == n; })); };
    const int had = log->rowCount();
    REQUIRE(log->send(QStringLiteral("Hey, could you update the wording on “Amazing Grace”? There are some typos."))
                .isEmpty());
    rows(had + 1);
    REQUIRE(laptop.sync->log()->send(QStringLiteral("Sure, I'll go do that!")).isEmpty());
    rows(had + 2);
    // Two songs in one statement, so they go in one send.
    laptop.db().exec("UPDATE songs SET"
                     " lyrics = CASE WHEN title = 'Amazing Grace' THEN '[V1]\nfixed line' ELSE lyrics END,"
                     " key = CASE WHEN title = 'Amazing Grace' THEN 'A' ELSE key END,"
                     " copyright = CASE WHEN title = 'It Is Well' THEN 'Public domain' ELSE copyright END");
    rows(had + 3);
    REQUIRE(log->send(QStringLiteral("That looks great, thanks!")).isEmpty());
    rows(had + 4);
    settle(church, laptop);
    log->setExpanded(log->data(log->index(had + 2), CollaborationLog::EntryIdRole).toString(), true);

    // Before the window is opened: the dot on the bar's button.
    {
        seventhslide::test::OperatorUi ui(church.facade, QStringLiteral("songs"), church.sync.get());
        REQUIRE(ui.window() != nullptr);
        ui.window()->resize(1400, 900);
        ui.window()->show();
        run_events_for(800);
        REQUIRE(ui.window()->grabWindow().save(QString::fromStdString(std::string(out) + "/collaboration_unread.png")));
    }

    const auto shoot = [&](AppComputer& c, const char* tab, const std::string& name, bool picker = false,
                           bool scrolled = false) {
        seventhslide::test::OperatorUi ui(c.facade, QStringLiteral("songs"), c.sync.get());
        REQUIRE(ui.window() != nullptr);
        ui.window()->resize(1400, tab != nullptr ? 900 : 1100);
        ui.window()->show();
        if (tab != nullptr) {
            QObject* dialog = ui.find_component("AppSettingsDialog");
            REQUIRE(dialog != nullptr);
            QMetaObject::invokeMethod(dialog, "openPage", Q_ARG(QVariant, QString::fromLatin1(tab)));
        } else {
            QObject* window = ui.find_component("CollaborationWindow");
            REQUIRE(window != nullptr);
            REQUIRE(QMetaObject::invokeMethod(window, "show"));
        }
        if (scrolled) {
            run_events_for(300);
            QObject* page = ui.find_component("SettingsSyncPanel");
            REQUIRE(page != nullptr);
            page->setProperty("contentY", page->property("contentHeight").toReal() - page->property("height").toReal());
        }
        if (picker) {
            run_events_for(300);
            QObject* button = ui.find_component("PicturePickerButton");
            REQUIRE(button != nullptr);
            REQUIRE(QMetaObject::invokeMethod(button, "clicked"));
        }
        run_events_for(800);
        REQUIRE(ui.window()->grabWindow().save(QString::fromStdString(std::string(out) + "/" + name + ".png")));
        std::cout << ui.warnings();
    };
    shoot(church, nullptr, "collaboration_window");
    shoot(church, "sync", "collaboration_sync_page");
    shoot(church, "sync", "collaboration_picture_menu", true);
    shoot(church, "sync", "collaboration_computers", false, true);
}
