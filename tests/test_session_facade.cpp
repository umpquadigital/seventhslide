// The facade as a whole: libraries and their switching, settings, profiles, errors and the wiring between controllers.

#include "destination_rows.hpp"
#include "refusals.hpp"
#include "font_controller.hpp"
#include "session_facade_fixture.hpp"
#include "stack_controller.hpp"

#include <seventhslide/db/bible_repository.hpp>
#include <seventhslide/db/config_repository.hpp>
#include <seventhslide/db/database.hpp>
#include <seventhslide/library/console_store.hpp>
#include <seventhslide/library/library.hpp>
#include <seventhslide/model/machine_settings.hpp>
#include <seventhslide/session/auto_advance.hpp>
#include <seventhslide/sync/changed.hpp>

#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>

namespace MediaKindQml = seventhslide::app::MediaKindQml;

using namespace seventhslide::test::facade;
namespace refusal = seventhslide::app::refusal;

TEST_CASE("SessionFacade app settings CCLI priority remote fonts", "[app]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    auto cur = facade.getAppSettings();
    REQUIRE_FALSE(cur.contains(QStringLiteral("previewVideoMode")));
    REQUIRE_FALSE(cur.contains(QStringLiteral("bundleLocalFonts")));
    REQUIRE_FALSE(cur.value(QStringLiteral("remoteEnabled")).toBool());

    QVariantMap fields;
    fields.insert(QStringLiteral("organizationName"), QStringLiteral(" Grace Church "));
    fields.insert(QStringLiteral("ccliLicenceNumber"), QStringLiteral("  998877 "));
    fields.insert(QStringLiteral("themePriority"),
                  QVariantList{QStringLiteral("global"),
                               QStringLiteral("service"),
                               QStringLiteral("content")});
    fields.insert(QStringLiteral("remoteEnabled"), true);
    fields.insert(QStringLiteral("remotePin"), QStringLiteral("2468"));
    REQUIRE(facade.updateAppSettings(fields).isEmpty());

    REQUIRE(facade.remoteEnabled());
    REQUIRE(facade.session().ccli_licence_number() == "998877");
    REQUIRE(facade.session().theme_priority().front() == "global");

    cur = facade.getAppSettings();
    REQUIRE(cur.value(QStringLiteral("ccliLicenceNumber")).toString()
            == QStringLiteral("998877"));
    REQUIRE(cur.value(QStringLiteral("remotePin")).toString() == QStringLiteral("2468"));
    REQUIRE(cur.value(QStringLiteral("organizationName")).toString()
            == QStringLiteral("Grace Church"));
    REQUIRE(cur.value(QStringLiteral("themePriority")).toList().front().toString()
            == QStringLiteral("global"));
    REQUIRE_FALSE(cur.contains(QStringLiteral("previewVideoMode")));
}

TEST_CASE("A stack's auto-advance settings are always whole: the defaults where it has none", "[app]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const seventhslide::session::AutoAdvanceState defaults;
    const auto check_defaults = [&](const QVariantMap& state) {
        CHECK(state.value(QStringLiteral("enabled")) == QVariant(defaults.enabled));
        CHECK(state.value(QStringLiteral("intervalSec")) == QVariant(defaults.interval_ms / 1000));
        CHECK(state.value(QStringLiteral("loop")) == QVariant(defaults.loop));
        CHECK(state.value(QStringLiteral("through")) == QVariant(defaults.through));
        CHECK(state.value(QStringLiteral("stopAtDividers")) == QVariant(defaults.stop_at_dividers));
    };
    const auto stack = facade.stacks()->focusedStackId();
    REQUIRE_FALSE(stack.isEmpty());
    check_defaults(facade.stacks()->autoAdvanceFor(stack));
    check_defaults(facade.stacks()->autoAdvanceFor(QString()));
    check_defaults(facade.stacks()->autoAdvanceFor(QStringLiteral("no-such-stack")));

    REQUIRE(facade.stacks()->setAutoAdvance(stack, true, 12, true, false, false).isEmpty());
    const auto state = facade.stacks()->autoAdvanceFor(stack);
    CHECK(state.value(QStringLiteral("intervalSec")).toInt() == 12);
    CHECK(state.value(QStringLiteral("loop")).toBool());
}

TEST_CASE("A library's folder is deleted only for its typed name, and only when it is the library's alone",
          "[app][library]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir tmp;
    SessionFacade facade;
    REQUIRE(facade.init_libraries(tmp.path));
    auto* libraries = facade.libraries();
    REQUIRE(libraries->createLibrary(QStringLiteral("Kids")).isEmpty());
    const auto kids = tmp.path / "libraries" / "kids";
    REQUIRE(std::filesystem::is_directory(kids));
    std::ofstream(kids / "media" / "song.png") << "picture";

    const auto listed = [&](const QString& id) {
        return std::ranges::any_of(libraries->libraries(), [&](const QVariant& row) {
            return row.toMap().value(QStringLiteral("id")).toString() == id;
        });
    };

    // Not the name: nothing happens.
    CHECK_FALSE(libraries->deleteLibrary(QStringLiteral("kids"), QStringLiteral("kids")).isEmpty());
    CHECK_FALSE(libraries->deleteLibrary(QStringLiteral("kids"), QString()).isEmpty());
    CHECK(std::filesystem::exists(kids / "media" / "song.png"));
    CHECK(listed(QStringLiteral("kids")));

    // Not the library in use.
    const auto active = libraries->activeLibraryId();
    const auto active_root = libraries->active_library_path().toStdString();
    CHECK_FALSE(libraries->deleteLibrary(active, libraries->activeLibraryName()).isEmpty());
    CHECK(std::filesystem::is_directory(active_root));
    CHECK(listed(active));

    // Not a folder added from elsewhere: it stays listed and on the disk.
    const auto elsewhere = tmp.path / "Downloads" / "Songs";
    std::filesystem::create_directories(elsewhere);
    std::ofstream(elsewhere / "mine.txt") << "not the app's";
    REQUIRE(libraries->attachLibrary(QString::fromStdString(elsewhere.string()), QStringLiteral("Songs")).isEmpty());
    CHECK_FALSE(libraries->deleteLibrary(QStringLiteral("songs"), QStringLiteral("Songs")).isEmpty());
    CHECK(std::filesystem::exists(elsewhere / "mine.txt"));
    CHECK(listed(QStringLiteral("songs")));

    // Its name: off the list, and gone from the disk.
    QSignalSpy changed(libraries, &seventhslide::app::LibraryRegistryController::librariesChanged);
    const QString deleted = libraries->deleteLibrary(QStringLiteral("kids"), QStringLiteral(" Kids "));
    INFO(deleted.toStdString());
    CHECK(deleted.isEmpty());
    CHECK_FALSE(std::filesystem::exists(kids));
    CHECK_FALSE(listed(QStringLiteral("kids")));
    CHECK(changed.count() == 1);
    CHECK(std::filesystem::exists(tmp.path / "libraries.json"));
}

TEST_CASE("A library list that cannot be read is reported at start and left on disk", "[app][library]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir tmp;
    const auto file = tmp.path / "libraries.json";
    const std::string bytes = "{ \"version\": 1, \"libraries\": [ {\"id\": \"kids\", ";
    std::ofstream(file, std::ios::binary) << bytes;

    SessionFacade facade;
    CHECK_FALSE(facade.init_libraries(tmp.path));
    CHECK_FALSE(facade.ready());
    CHECK(facade.databaseError().contains(QString::fromStdString(file.string())));
    CHECK(facade.libraries()->libraries().isEmpty());
    CHECK_FALSE(facade.libraries()->createLibrary(QStringLiteral("Kids")).isEmpty());

    std::ifstream in(file, std::ios::binary);
    CHECK(std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()) == bytes);
    CHECK_FALSE(std::filesystem::exists(tmp.path / "libraries" / "default"));
}

TEST_CASE("An output switched off stays off after a restart, in its library only", "[app][library][outputs]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir tmp;
    const auto stage_id = [](SessionFacade& f) {
        return f.outputSettings()->getOutputSettings(QStringLiteral("Stage")).value(QStringLiteral("id")).toString().toStdString();
    };
    {
        SessionFacade facade;
        REQUIRE(facade.init_libraries(tmp.path));
        REQUIRE(facade.outputSettings()->createOutput(QStringLiteral("Stage")).contains(QStringLiteral("name")));
        REQUIRE(facade.outputSettings()->setOutputOn(QStringLiteral("Stage"), false).isEmpty());
        REQUIRE_FALSE(facade.session().output_is_on(stage_id(facade)));
        // Another library has its own outputs, all on.
        REQUIRE(facade.libraries()->createLibrary(QStringLiteral("Kids")).isEmpty());
        REQUIRE(facade.libraries()->switchLibrary(QStringLiteral("kids")).isEmpty());
        CHECK(facade.session().outputs_off().empty());
        REQUIRE(facade.libraries()->switchLibrary(QStringLiteral("default")).isEmpty());
        CHECK_FALSE(facade.session().output_is_on(stage_id(facade)));
    }
    SessionFacade restarted;
    REQUIRE(restarted.init_libraries(tmp.path));
    CHECK_FALSE(restarted.session().output_is_on(stage_id(restarted)));
    CHECK(restarted.outputsOn()->rowCount() == 1);
    CHECK(restarted.outputs()->rowCount() == 2);

    REQUIRE(restarted.outputSettings()->setOutputOn(QStringLiteral("Stage"), true).isEmpty());
    SessionFacade again;
    REQUIRE(again.init_libraries(tmp.path));
    CHECK(again.session().outputs_off().empty());
}

TEST_CASE("Only a library made here starts with an output; one opened keeps what it has",
          "[app][library][outputs]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir tmp;
    {
        SessionFacade facade;
        REQUIRE(facade.init_libraries(tmp.path));
        // Made at the first start: Main.
        REQUIRE(facade.outputs()->rowCount() == 1);
        CHECK(facade.session().outputs().front().name == "Main");
        // The last output goes too.
        REQUIRE(facade.outputSettings()->deleteOutput(QStringLiteral("Main")).isEmpty());
        CHECK(facade.outputs()->rowCount() == 0);
        // A library made here starts with Main as well...
        REQUIRE(facade.libraries()->createLibrary(QStringLiteral("Kids")).isEmpty());
        REQUIRE(facade.libraries()->switchLibrary(QStringLiteral("kids")).isEmpty());
        CHECK(facade.outputs()->rowCount() == 1);
        // ...and one opened has what it has, none included.
        REQUIRE(facade.libraries()->switchLibrary(QStringLiteral("default")).isEmpty());
        CHECK(facade.outputs()->rowCount() == 0);
        CHECK(facade.outputsOn()->rowCount() == 0);
    }
    SessionFacade restarted;
    REQUIRE(restarted.init_libraries(tmp.path));
    CHECK(restarted.outputs()->rowCount() == 0);
    CHECK(restarted.session().primary_output() == nullptr);
    // Nothing was written for it.
    const auto* entry = restarted.open_library().registry()->active();
    REQUIRE(entry != nullptr);
    REQUIRE(std::filesystem::exists(entry->database_file()));
    auto db = seventhslide::db::Database::open_file(entry->database_file());
    CHECK(db.query_int("SELECT COUNT(*) FROM outputs", -1) == 0);
}

TEST_CASE("SessionFacade switches libraries and keeps each one separate", "[app][library]")
{
    seventhslide::test::ensure_qt_application();

    namespace fs = std::filesystem;
    const seventhslide::test::TempDir tmp;
    const fs::path& data_dir = tmp.path;

    SessionFacade facade;
    REQUIRE(facade.init_libraries(data_dir));
    REQUIRE(facade.ready());
    REQUIRE(facade.libraries()->libraries().size() == 1);
    REQUIRE(facade.libraries()->activeLibraryId() == QStringLiteral("default"));
    // The default library lives in its own folder, never in the data dir itself.
    REQUIRE(fs::exists(data_dir / "libraries" / "default" / "seventhslide.db"));
    REQUIRE_FALSE(fs::exists(data_dir / "seventhslide.db"));

    Song sunday;
    sunday.title = "Sunday Only";
    sunday.lyrics = "[V1]\nline";
    REQUIRE(facade.song_repo() != nullptr);
    (void)facade.song_repo()->insert(sunday);
    facade.library()->refresh_songs();
    REQUIRE(facade.library()->songs()->rowCount() == 1);

    REQUIRE(facade.libraries()->createLibrary(QStringLiteral("Kids")).isEmpty());
    REQUIRE(facade.libraries()->libraries().size() == 2);
    // Creating does not switch.
    REQUIRE(facade.libraries()->activeLibraryId() == QStringLiteral("default"));

    REQUIRE(facade.libraries()->switchLibrary(QStringLiteral("kids")).isEmpty());
    REQUIRE(facade.ready());
    REQUIRE(facade.libraries()->activeLibraryId() == QStringLiteral("kids"));
    // The bottom bar's names: the library's, and its seeded profile's.
    CHECK(facade.libraries()->activeLibraryName() == QStringLiteral("Kids"));
    CHECK_FALSE(facade.profiles()->activeProfileName().isEmpty());
    // A fresh library starts empty and gets its own database + media root.
    REQUIRE(facade.library()->songs()->rowCount() == 0);
    REQUIRE(fs::exists(data_dir / "libraries" / "kids" / "seventhslide.db"));
    REQUIRE(fs::is_directory(data_dir / "libraries" / "kids" / "media"));
    // Every library seeds its own default profile.
    REQUIRE(facade.profiles()->activeProfileId() >= 0);
    REQUIRE(facade.profiles()->styleProfiles().size() == 1);

    // The active library is never removable - that would strand the session.
    REQUIRE_FALSE(facade.libraries()->removeLibrary(QStringLiteral("kids")).isEmpty());

    REQUIRE(facade.libraries()->switchLibrary(QStringLiteral("default")).isEmpty());
    REQUIRE(facade.library()->songs()->rowCount() == 1);

    // The choice survives a restart.
    REQUIRE(facade.libraries()->switchLibrary(QStringLiteral("kids")).isEmpty());
    SessionFacade restarted;
    REQUIRE(restarted.init_libraries(data_dir));
    REQUIRE(restarted.libraries()->activeLibraryId() == QStringLiteral("kids"));
    REQUIRE(restarted.library()->songs()->rowCount() == 0);

    // A library whose folder went away says so.
    const auto missing = [&](const QString& id) {
        for (const auto& row : restarted.libraries()->libraries()) {
            if (row.toMap().value(QStringLiteral("id")).toString() == id)
                return row.toMap().value(QStringLiteral("missing")).toBool();
        }
        FAIL("no library " << id.toStdString());
        return false;
    };
    REQUIRE_FALSE(missing(QStringLiteral("default")));
    fs::remove_all(data_dir / "libraries" / "default");
    REQUIRE(missing(QStringLiteral("default")));
    // Its folder cannot be shown, nor one that is not a library.
    REQUIRE_FALSE(restarted.libraries()->showFolder(QStringLiteral("default")).isEmpty());
    REQUIRE_FALSE(restarted.libraries()->showFolder(QStringLiteral("nope")).isEmpty());
}

namespace {

/// The ``app_settings`` of the library at ``root``, read over a connection of
/// its own.
nlohmann::json library_settings(const std::filesystem::path& root)
{
    auto db = seventhslide::db::Database::open_file(seventhslide::library::database_file(root));
    return seventhslide::db::ConfigRepository(db).load_settings();
}

/// Whether a library's ``settings`` hold any of the web remote's.
bool holds_remote_settings(const nlohmann::json& settings)
{
    return settings.contains(seventhslide::model::k_setting_remote_enabled)
           || settings.contains(seventhslide::model::k_setting_remote_pin)
           || settings.contains(seventhslide::model::k_setting_remote_port);
}

}  // namespace

TEST_CASE("SessionFacade keeps the web remote's settings on this computer", "[app][libraries]")
{
    seventhslide::test::ensure_qt_application();
    namespace fs = std::filesystem;
    const seventhslide::test::TempDir tmp;
    const fs::path data_dir = tmp.path;
    const fs::path grace = data_dir / "libraries" / "default";
    const fs::path kids = data_dir / "libraries" / "kids";

    // Saving writes the remote's to this computer and the rest to the library.
    SessionFacade facade;
    REQUIRE(facade.init_libraries(data_dir));
    REQUIRE(facade
                .updateAppSettings({{QStringLiteral("remoteEnabled"), true},
                                    {QStringLiteral("remotePin"), QStringLiteral("2468")},
                                    {QStringLiteral("remotePort"), 9000},
                                    {QStringLiteral("organizationName"), QStringLiteral("Grace Church")}})
                .isEmpty());
    CHECK(facade.remotePin() == QStringLiteral("2468"));
    CHECK_FALSE(holds_remote_settings(library_settings(grace)));
    CHECK(library_settings(grace).value("organization_name", "") == "Grace Church");

    // Another library has the same remote, and its own settings.
    REQUIRE(facade.libraries()->createLibrary(QStringLiteral("Kids")).isEmpty());
    REQUIRE(facade.libraries()->switchLibrary(QStringLiteral("kids")).isEmpty());
    CHECK(facade.remotePin() == QStringLiteral("2468"));
    CHECK(facade.remotePort() == 9000);
    REQUIRE(facade
                .updateAppSettings({{QStringLiteral("remotePin"), QStringLiteral("3333")},
                                    {QStringLiteral("organizationName"), QStringLiteral("Kids")}})
                .isEmpty());
    CHECK(facade.remotePin() == QStringLiteral("3333"));
    CHECK_FALSE(holds_remote_settings(library_settings(kids)));
    CHECK(library_settings(kids).value("organization_name", "") == "Kids");

    // Every library, and the next run, see the same remote.
    REQUIRE(facade.libraries()->switchLibrary(QStringLiteral("default")).isEmpty());
    CHECK(facade.remotePin() == QStringLiteral("3333"));
    CHECK(facade.getAppSettings().value(QStringLiteral("organizationName")).toString()
          == QStringLiteral("Grace Church"));
    SessionFacade restarted;
    REQUIRE(restarted.init_libraries(data_dir));
    CHECK(restarted.remoteEnabled());
    CHECK(restarted.remotePin() == QStringLiteral("3333"));
    CHECK(restarted.remotePort() == 9000);
}

// Destinations used to be one machine-wide list keyed by output name,
// and every library open deleted the entries its outputs did not name. Two
// libraries whose outputs share a name ("Main") also shared one setup.
TEST_CASE("SessionFacade keeps each library's output destinations", "[app][libraries]")
{
    seventhslide::test::ensure_qt_application();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const std::filesystem::path data_dir = tmp.path().toStdString();

    const auto titles = [](const seventhslide::render::OutputWindowHost& host) {
        QStringList out;
        for (const auto& d : seventhslide::app::destination_rows(host)) {
            const auto m = d.toMap();
            out << m.value(QStringLiteral("output")).toString() + QLatin1Char('/')
                       + m.value(QStringLiteral("name")).toString();
        }
        return out;
    };

    seventhslide::render::OutputWindowHost host;
    SessionFacade facade;
    facade.set_output_host(&host);
    REQUIRE(facade.init_libraries(data_dir));
    const QString main = facade.primary_output_name();
    REQUIRE_FALSE(main.isEmpty());
    const QString sunday_main = QString::fromStdString(facade.session().primary_output()->id);
    const QString sunday = host.add_destination(sunday_main, QStringLiteral("display"));
    REQUIRE_FALSE(sunday.isEmpty());
    REQUIRE(host.set_destination_name(sunday, QStringLiteral("Sanctuary")));

    REQUIRE(facade.libraries()->createLibrary(QStringLiteral("Kids")).isEmpty());
    REQUIRE(facade.libraries()->switchLibrary(QStringLiteral("kids")).isEmpty());
    // Same output name, different library: nothing carried over.
    REQUIRE(facade.primary_output_name() == main);
    REQUIRE(seventhslide::app::destination_rows(host).isEmpty());
    const QString kids_main = QString::fromStdString(facade.session().primary_output()->id);
    REQUIRE(kids_main != sunday_main);
    const QString kids = host.add_destination(kids_main, QStringLiteral("display"));
    REQUIRE(host.set_destination_name(kids, QStringLiteral("Kids Room")));

    REQUIRE(facade.libraries()->switchLibrary(QStringLiteral("default")).isEmpty());
    REQUIRE(titles(host) == QStringList{sunday_main + QStringLiteral("/Sanctuary")});
    REQUIRE(facade.libraries()->switchLibrary(QStringLiteral("kids")).isEmpty());
    REQUIRE(titles(host) == QStringList{kids_main + QStringLiteral("/Kids Room")});

    // Stored by output id: a rename keeps them, and they survive a restart.
    REQUIRE(facade.outputSettings()->updateOutputSettings(main, {{QStringLiteral("name"), QStringLiteral("Front")},
                                          {QStringLiteral("canvasWidth"), 1920},
                                          {QStringLiteral("canvasHeight"), 1080}})
                .value(QStringLiteral("error"))
                .toString()
                .isEmpty());
    {
        seventhslide::render::OutputWindowHost host2;
        SessionFacade restarted;
        restarted.set_output_host(&host2);
        REQUIRE(restarted.init_libraries(data_dir));
        REQUIRE(restarted.libraries()->activeLibraryId() == QStringLiteral("kids"));
        REQUIRE(titles(host2) == QStringList{kids_main + QStringLiteral("/Kids Room")});
        REQUIRE(restarted.primary_output_name() == QStringLiteral("Front"));
    }

    // Deleting the output forgets its destinations for good. (A library keeps
    // at least one output, hence the spare.)
    REQUIRE(facade.outputSettings()->createOutput(QStringLiteral("Spare")).value(QStringLiteral("error")).isNull());
    REQUIRE(facade.outputSettings()->deleteOutput(QStringLiteral("Front")).isEmpty());
    REQUIRE(seventhslide::app::destination_rows(host).isEmpty());
    REQUIRE(facade.libraries()->switchLibrary(QStringLiteral("default")).isEmpty());
    REQUIRE(facade.libraries()->switchLibrary(QStringLiteral("kids")).isEmpty());
    REQUIRE(seventhslide::app::destination_rows(host).isEmpty());
    // The other library is untouched throughout.
    REQUIRE(facade.libraries()->switchLibrary(QStringLiteral("default")).isEmpty());
    REQUIRE(titles(host) == QStringList{sunday_main + QStringLiteral("/Sanctuary")});
}

TEST_CASE("SessionFacade profiles switch per-song verse order", "[app][library]")
{
    seventhslide::test::ensure_qt_application();

    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    REQUIRE(facade.ready());

    Song song;
    song.title = "Amazing Grace";
    song.lyrics = "[V1]\nline\n\n[C1]\nchorus";
    song.verse_order = "V1 C1";
    const auto sid = facade.song_repo()->insert(song);
    facade.refresh_library();

    const auto sunday = facade.profiles()->activeProfileId();
    REQUIRE(sunday >= 0);
    REQUIRE(facade.profiles()->createProfile(QStringLiteral("Evening")).isEmpty());

    qint64 evening = -1;
    for (const auto& row : facade.profiles()->styleProfiles()) {
        const auto m = row.toMap();
        if (m.value(QStringLiteral("name")).toString() == QLatin1String("Evening")) {
            evening = m.value(QStringLiteral("id")).toLongLong();
        }
    }
    REQUIRE(evening >= 0);

    REQUIRE(facade.profiles()->activateProfile(evening).isEmpty());
    REQUIRE(facade.profiles()->activeProfileId() == evening);
    REQUIRE(facade.song_repo()->get(sid)->verse_order.empty());

    facade.song_repo()->set_verse_orders({{sid, "V1 V1 C1"}});
    REQUIRE(facade.profiles()->activateProfile(sunday).isEmpty());
    REQUIRE(facade.song_repo()->get(sid)->verse_order == "V1 C1");

    REQUIRE(facade.profiles()->activateProfile(evening).isEmpty());
    REQUIRE(facade.song_repo()->get(sid)->verse_order == "V1 V1 C1");

    REQUIRE(facade.profiles()->deleteProfile(evening).isEmpty());
    REQUIRE(facade.profiles()->activeProfileId() == sunday);
    REQUIRE_FALSE(facade.profiles()->deleteProfile(sunday).isEmpty());
}

TEST_CASE("A read-only library's profile is not captured, and nothing is reported", "[app][library]")
{
    seventhslide::test::ensure_qt_application();

    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    auto* repo = facade.open_library().profile_repository();
    const auto active = facade.profiles()->activeProfileId();
    REQUIRE(active >= 0);
    const auto stored = repo->get(active)->data;
    // A song with a theme of its own: the live state is not what the profile
    // holds.
    Song song;
    song.title = "Amazing Grace";
    song.lyrics = "[V1]\nline";
    song.theme_map = {{"main", {{"song", {{"theme", "t1"}}}}}};
    const auto sid = facade.song_repo()->insert(song);

    facade.set_read_only(true);
    REQUIRE(facade.readOnly());
    facade.profiles()->capture_active_profile();
    CHECK(facade.open_library().last_error().isEmpty());
    CHECK(repo->get(active)->data == stored);

    // Writable again, the same capture writes it.
    facade.set_read_only(false);
    facade.profiles()->capture_active_profile();
    CHECK(facade.open_library().last_error().isEmpty());
    CHECK(repo->get(active)->data["songs"][std::to_string(sid)]["main"]["song"]["theme"] == "t1");
}

TEST_CASE("A library this computer only views opens read-only, and nothing is put right in it",
          "[app][library]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir tmp;
    SessionFacade facade;
    REQUIRE(facade.init_libraries(tmp.path));
    REQUIRE(facade.libraries()->createLibrary(QStringLiteral("Kids")).isEmpty());
    REQUIRE(facade.libraries()->switchLibrary(QStringLiteral("kids")).isEmpty());

    // The default library, over a connection of its own, holding what opening
    // it would put right: an output with no id and no themes, and no profile.
    const auto file = facade.open_library().registry()->find("default")->database_file();
    const auto count = [&](const char* sql) {
        auto db = seventhslide::db::Database::open_file(file);
        return db.query_int(sql, -1);
    };
    {
        auto db = seventhslide::db::Database::open_file(file);
        seventhslide::db::ConfigRepository config(db);
        auto outputs = config.load_outputs();
        REQUIRE(outputs.size() == 1);
        outputs.front().id.clear();
        outputs.front().song_themes.clear();
        config.save_outputs(outputs);
        db.exec("DELETE FROM style_profiles");
        db.exec("DELETE FROM sync_outbox");
    }

    facade.set_read_only_check([](const std::string& id) { return id == "default"; });
    REQUIRE(facade.libraries()->switchLibrary(QStringLiteral("default")).isEmpty());
    CHECK(facade.ready());
    CHECK(facade.readOnly());
    CHECK(facade.open_library().last_error().isEmpty());
    // Put right for the window, and not written.
    REQUIRE(facade.session().outputs().size() == 1);
    CHECK_FALSE(facade.session().outputs().front().id.empty());
    CHECK_FALSE(facade.session().outputs().front().song_themes.empty());
    CHECK(count("SELECT COUNT(*) FROM sync_outbox") == 0);
    CHECK(count("SELECT COUNT(*) FROM style_profiles") == 0);
    {
        auto db = seventhslide::db::Database::open_file(file);
        const auto stored = seventhslide::db::ConfigRepository(db).load_outputs();
        REQUIRE(stored.size() == 1);
        CHECK(stored.front().id.empty());
        CHECK(stored.front().song_themes.empty());
    }

    // Closing it writes nothing either, and the next library takes writes.
    REQUIRE(facade.libraries()->switchLibrary(QStringLiteral("kids")).isEmpty());
    CHECK_FALSE(facade.readOnly());
    CHECK(facade.open_library().last_error().isEmpty());
    CHECK(count("SELECT COUNT(*) FROM sync_outbox") == 0);
}

namespace {

/// A facade with two libraries, Kids open, and the default library as an
/// older build left it: an output with the id of the one before it, one with
/// none, and no profile. Only the default library is one this computer views.
struct ViewedLibrary {
    seventhslide::test::QtBootstrap qt;
    seventhslide::test::TempDir tmp;
    SessionFacade facade;
    std::filesystem::path file;

    ViewedLibrary()
    {
        REQUIRE(facade.init_libraries(tmp.path));
        REQUIRE(facade.libraries()->createLibrary(QStringLiteral("Kids")).isEmpty());
        REQUIRE(facade.libraries()->switchLibrary(QStringLiteral("kids")).isEmpty());
        file = facade.open_library().registry()->find("default")->database_file();
        auto db = seventhslide::db::Database::open_file(file);
        seventhslide::db::ConfigRepository config(db);
        auto outputs = config.load_outputs();
        REQUIRE(outputs.size() == 1);
        outputs.push_back(outputs.front());
        outputs.back().name = "Stage";
        outputs.push_back(outputs.front());
        outputs.back().name = "Lobby";
        outputs.back().id.clear();
        config.save_outputs(outputs);
        db.exec("DELETE FROM style_profiles");
        db.exec("DELETE FROM sync_outbox");
        facade.set_read_only_check([](const std::string& id) { return id == "default"; });
    }

    [[nodiscard]] std::vector<std::string> ids() const
    {
        std::vector<std::string> out;
        for (const auto& oc : facade.session().outputs()) {
            out.push_back(oc.id);
        }
        return out;
    }
    [[nodiscard]] std::vector<seventhslide::model::OutputConfig> stored() const
    {
        auto db = seventhslide::db::Database::open_file(file);
        return seventhslide::db::ConfigRepository(db).load_outputs();
    }
    [[nodiscard]] int count(const char* sql) const
    {
        auto db = seventhslide::db::Database::open_file(file);
        return db.query_int(sql, -1);
    }
};

}  // namespace

TEST_CASE("A viewed library's outputs keep the ids they are given from one open to the next",
          "[app][library]")
{
    ViewedLibrary lib;
    REQUIRE(lib.facade.libraries()->switchLibrary(QStringLiteral("default")).isEmpty());
    REQUIRE(lib.facade.readOnly());
    const auto ids = lib.ids();
    REQUIRE(ids.size() == 3);
    CHECK(std::set<std::string>(ids.begin(), ids.end()).size() == 3);
    CHECK(lib.stored().back().id.empty());

    // Read again after a sync, and opened again.
    seventhslide::sync::Changed outputs;
    outputs.add_all("outputs");
    lib.facade.reload_after_sync(outputs);
    CHECK(lib.ids() == ids);
    REQUIRE(lib.facade.libraries()->switchLibrary(QStringLiteral("kids")).isEmpty());
    REQUIRE(lib.facade.libraries()->switchLibrary(QStringLiteral("default")).isEmpty());
    CHECK(lib.ids() == ids);
    CHECK(lib.stored().back().id.empty());
    CHECK(lib.facade.open_library().last_error().isEmpty());
}

TEST_CASE("A viewed library made writable while open takes what opening it put right",
          "[app][library]")
{
    ViewedLibrary lib;
    REQUIRE(lib.facade.libraries()->switchLibrary(QStringLiteral("default")).isEmpty());
    REQUIRE(lib.facade.readOnly());
    const auto ids = lib.ids();
    REQUIRE(lib.count("SELECT COUNT(*) FROM style_profiles") == 0);

    // This computer is made an editor.
    lib.facade.set_read_only(false);
    CHECK_FALSE(lib.facade.readOnly());
    CHECK(lib.facade.open_library().last_error().isEmpty());
    const auto stored = lib.stored();
    REQUIRE(stored.size() == 3);
    for (std::size_t i = 0; i < stored.size(); ++i) {
        CHECK(stored[i].id == ids[i]);
        CHECK_FALSE(stored[i].song_themes.empty());
    }
    CHECK(lib.count("SELECT COUNT(*) FROM style_profiles") == 1);
    CHECK(lib.facade.profiles()->activeProfileId() >= 0);
    CHECK(lib.ids() == ids);
}

TEST_CASE("Whether a library opens read-only is asked of the library opening", "[app][library]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir tmp;
    SessionFacade facade;
    REQUIRE(facade.init_libraries(tmp.path));
    REQUIRE(facade.libraries()->createLibrary(QStringLiteral("Kids")).isEmpty());
    std::vector<std::string> asked;
    facade.set_read_only_check([&](const std::string& id) {
        asked.push_back(id);
        return id == "kids";
    });

    // Opened while another is the active one.
    auto& switcher = static_cast<seventhslide::app::ILibrarySwitcher&>(facade);
    switcher.close_library();
    REQUIRE(facade.open_library().registry()->active_id() == "default");
    REQUIRE(switcher.open_library(*facade.open_library().registry()->find("kids")));
    CHECK(asked == std::vector<std::string>{"kids"});
    CHECK(facade.readOnly());
}

TEST_CASE("A read-only library closing is not ready before it is not read-only", "[app][library]")
{
    ViewedLibrary lib;
    REQUIRE(lib.facade.libraries()->switchLibrary(QStringLiteral("default")).isEmpty());
    REQUIRE(lib.facade.readOnly());
    std::vector<std::pair<bool, bool>> seen;  // readOnly, ready
    QObject::connect(&lib.facade, &SessionFacade::readOnlyChanged, [&] {
        seen.emplace_back(lib.facade.readOnly(), lib.facade.ready());
    });
    static_cast<seventhslide::app::ILibrarySwitcher&>(lib.facade).close_library();
    REQUIRE(seen.size() == 1);
    CHECK_FALSE(seen.front().first);
    CHECK_FALSE(seen.front().second);
}

TEST_CASE("A library opened while another is active keeps its own outputs' settings here",
          "[app][library]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir tmp;
    SessionFacade facade;
    REQUIRE(facade.init_libraries(tmp.path));
    REQUIRE(facade.libraries()->createLibrary(QStringLiteral("Kids")).isEmpty());

    auto& switcher = static_cast<seventhslide::app::ILibrarySwitcher&>(facade);
    switcher.close_library();
    REQUIRE(facade.open_library().registry()->active_id() == "default");
    REQUIRE(switcher.open_library(*facade.open_library().registry()->find("kids")));
    CHECK(facade.open_library().destinations_library_id() == "kids");
}

TEST_CASE("Profile commands refuse bad input and announce what changed", "[app][library]")
{
    seventhslide::test::ensure_qt_application();

    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    auto* profiles = facade.profiles();
    QSignalSpy changed(profiles, &seventhslide::app::ProfileController::profilesChanged);
    QSignalSpy switched(profiles, &seventhslide::app::ProfileController::profileSwitched);

    const auto rows = [profiles]() {
        QMap<QString, QVariantMap> out;
        for (const auto& row : profiles->styleProfiles()) {
            const auto m = row.toMap();
            out.insert(m.value(QStringLiteral("name")).toString(), m);
        }
        return out;
    };
    const auto id_of = [&rows](const QString& name) {
        return rows().value(name).value(QStringLiteral("id"), -1).toLongLong();
    };

    const auto first = profiles->activeProfileId();
    REQUIRE(first >= 0);
    const auto first_name = rows().firstKey();
    REQUIRE(rows().value(first_name).value(QStringLiteral("active")).toBool());

    SECTION("activate")
    {
        CHECK(profiles->activateProfile(-1) == refusal::profile_gone());
        CHECK(profiles->activateProfile(first + 1000) == refusal::profile_gone());
        CHECK(profiles->activateProfile(first).isEmpty());
        CHECK(switched.count() == 0);
    }

    SECTION("create, rename and duplicate")
    {
        CHECK(profiles->createProfile(QStringLiteral("  ")) == refusal::name_required());
        REQUIRE(profiles->createProfile(QStringLiteral(" Evening ")).isEmpty());
        const auto evening = id_of(QStringLiteral("Evening"));
        REQUIRE(evening >= 0);
        CHECK_FALSE(rows().value(QStringLiteral("Evening")).value(QStringLiteral("active")).toBool());

        changed.clear();
        CHECK(profiles->renameProfile(evening, QStringLiteral(" ")) == refusal::name_required());
        CHECK(profiles->renameProfile(-1, QStringLiteral("Night")) == refusal::profile_gone());
        CHECK(profiles->renameProfile(evening + 1000, QStringLiteral("Night")) == refusal::profile_gone());
        CHECK(changed.count() == 0);
        REQUIRE(profiles->renameProfile(evening, QStringLiteral(" Night ")).isEmpty());
        CHECK(changed.count() == 1);
        CHECK(id_of(QStringLiteral("Night")) == evening);

        changed.clear();
        CHECK(profiles->duplicateProfile(-1, QString()) == refusal::profile_gone());
        CHECK(profiles->duplicateProfile(evening + 1000, QString()) == refusal::profile_gone());
        CHECK(changed.count() == 0);
        // A blank name copies the source's.
        REQUIRE(profiles->duplicateProfile(evening, QStringLiteral("  ")).isEmpty());
        CHECK(id_of(QStringLiteral("Night copy")) >= 0);
        REQUIRE(profiles->duplicateProfile(evening, QStringLiteral("Late")).isEmpty());
        CHECK(id_of(QStringLiteral("Late")) >= 0);
        CHECK(changed.count() == 2);
        CHECK(switched.count() == 0);
    }

    SECTION("delete")
    {
        CHECK(profiles->deleteProfile(first) == QStringLiteral("The last profile cannot be deleted."));
        CHECK(profiles->deleteProfile(-1) == refusal::profile_gone());
        REQUIRE(profiles->createProfile(QStringLiteral("Evening")).isEmpty());
        REQUIRE(profiles->createProfile(QStringLiteral("Late")).isEmpty());

        // Not the active one: the list changes, live assignments do not.
        changed.clear();
        REQUIRE(profiles->deleteProfile(id_of(QStringLiteral("Late"))).isEmpty());
        CHECK(changed.count() == 1);
        CHECK(switched.count() == 0);
        CHECK(profiles->activeProfileId() == first);

        // The active one: a survivor is activated, so that is a switch (the
        // facade re-reads the list from there).
        REQUIRE(profiles->deleteProfile(first).isEmpty());
        CHECK(switched.count() == 1);
        const auto survivor = id_of(QStringLiteral("Evening"));
        CHECK(profiles->activeProfileId() == survivor);
        CHECK(rows().value(QStringLiteral("Evening")).value(QStringLiteral("active")).toBool());
    }
}

TEST_CASE("A library that will not open rolls back to the previous one", "[app][library]")
{
    seventhslide::test::ensure_qt_application();

    namespace fs = std::filesystem;
    const seventhslide::test::TempDir tmp;
    const fs::path& data_dir = tmp.path;
    // The test takes the permissions off a library folder; they come back
    // before ``tmp`` goes, or it could not be removed.
    struct RestorePermissions {
        fs::path dir;
        ~RestorePermissions()
        {
            std::error_code ec;
            fs::permissions(dir / "libraries" / "broken",
                            fs::perms::owner_all,
                            fs::perm_options::add,
                            ec);
        }
    } restore{data_dir};

    SessionFacade facade;
    REQUIRE(facade.init_libraries(data_dir));

    Song keeper;
    keeper.title = "Still Here";
    keeper.lyrics = "[V1]\nline";
    (void)facade.song_repo()->insert(keeper);
    facade.library()->refresh_songs();

    REQUIRE(facade.libraries()->createLibrary(QStringLiteral("Broken")).isEmpty());
    // A directory where the database file cannot be created.
    const auto broken_root = data_dir / "libraries" / "broken";
    std::error_code ec;
    fs::permissions(broken_root, fs::perms::owner_read | fs::perms::owner_exec,
                    fs::perm_options::replace, ec);
    if (ec || fs::status(broken_root).permissions() != (fs::perms::owner_read | fs::perms::owner_exec)) {
        SKIP("cannot make a read-only directory here (running as root?)");
    }

    REQUIRE_FALSE(facade.libraries()->switchLibrary(QStringLiteral("broken")).isEmpty());
    // Still usable, still on the library that worked.
    REQUIRE(facade.libraries()->activeLibraryId() == QStringLiteral("default"));
    REQUIRE(facade.ready());
    REQUIRE(facade.library()->songs()->rowCount() == 1);

    // And the registry on disk agrees, so the next launch is fine too.
    SessionFacade restarted;
    REQUIRE(restarted.init_libraries(data_dir));
    REQUIRE(restarted.libraries()->activeLibraryId() == QStringLiteral("default"));
}

TEST_CASE("SessionFacade reports a database failure instead of throwing", "[app]")
{
    seventhslide::test::ensure_qt_application();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto path = std::filesystem::path(dir.filePath(QStringLiteral("broken.db")).toStdString());

    SessionFacade facade;
    REQUIRE(facade.open_database(path));
    const auto service_id = facade.library()->createService(QStringLiteral("Sunday"));
    REQUIRE(service_id >= 0);

    // Break the schema behind the facade's back, as a failing disk or a
    // foreign tool would, so every service query now errors.
    {
        auto breaker = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                 QStringLiteral("facade-breaker"));
        breaker.setDatabaseName(QString::fromStdString(path.string()));
        REQUIRE(breaker.open());
        QSqlQuery drop(breaker);
        REQUIRE(drop.exec(QStringLiteral("DROP TABLE service_items")));
        REQUIRE(drop.exec(QStringLiteral("DROP TABLE services")));
        breaker.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("facade-breaker"));

    REQUIRE(facade.library()->createService(QStringLiteral("Evening")) == -1);
    REQUIRE_FALSE(facade.library()->openService(service_id));
    REQUIRE_FALSE(facade.library()->renameService(service_id, QStringLiteral("Renamed")).isEmpty());
}

TEST_CASE("A library that fails to open leaves nothing of itself attached", "[app][library]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    const auto path = dir.path / "library.db";
    {
        SessionFacade maker;
        REQUIRE(maker.open_database(path));
    }
    const auto unattached = [](SessionFacade& facade) {
        CHECK_FALSE(facade.ready());
        CHECK_FALSE(facade.databaseError().isEmpty());
        CHECK_FALSE(facade.open_library().is_open());
        CHECK_FALSE(facade.library()->attached());
        CHECK_FALSE(facade.media()->attached());
        CHECK_FALSE(facade.freeformLibrary()->attached());
        CHECK(facade.media_root().empty());
    };

    SECTION("refused by the schema check")
    {
        {
            auto foreign = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                     QStringLiteral("facade-foreign"));
            foreign.setDatabaseName(QString::fromStdString((dir.path / "foreign.db").string()));
            REQUIRE(foreign.open());
            QSqlQuery create(foreign);
            REQUIRE(create.exec(QStringLiteral("CREATE TABLE notes(text TEXT)")));
            foreign.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("facade-foreign"));
        SessionFacade facade;
        REQUIRE_FALSE(facade.open_database(dir.path / "foreign.db"));
        unattached(facade);
    }

    SECTION("failing after the schema check")
    {
        // The schema's version is right but a table is gone, so reading the
        // outputs, after the repositories are attached, fails.
        {
            auto breaker = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                     QStringLiteral("facade-outputs"));
            breaker.setDatabaseName(QString::fromStdString(path.string()));
            REQUIRE(breaker.open());
            QSqlQuery drop(breaker);
            REQUIRE(drop.exec(QStringLiteral("DROP TABLE outputs")));
            breaker.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("facade-outputs"));
        SessionFacade facade;
        REQUIRE_FALSE(facade.open_database(path));
        unattached(facade);
    }

    SECTION("failing after the lists are filled")
    {
        // The songs are listed before the Bibles are read, and that fails.
        {
            SessionFacade maker;
            REQUIRE(maker.open_database(path));
            REQUIRE(maker.library()->createSong(QStringLiteral("Amazing Grace"),
                                                QStringLiteral("[V1]\nline"), {})
                        .contains(QStringLiteral("songId")));
            REQUIRE(maker.library()->createService(QStringLiteral("Easter")) > 0);
        }
        {
            auto breaker = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                     QStringLiteral("facade-bibles"));
            breaker.setDatabaseName(QString::fromStdString(path.string()));
            REQUIRE(breaker.open());
            QSqlQuery drop(breaker);
            REQUIRE(drop.exec(QStringLiteral("DROP TABLE verses")));
            REQUIRE(drop.exec(QStringLiteral("DROP TABLE bibles")));
            breaker.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("facade-bibles"));
        SessionFacade facade;
        REQUIRE_FALSE(facade.open_database(path));
        unattached(facade);
        // Nothing of it is still listed.
        CHECK(facade.library()->songs()->rowCount() == 0);
        CHECK(facade.library()->services()->rowCount() == 0);
    }
}

TEST_CASE("A failure with no return value reaches the operator", "[app][errors]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    std::vector<QString> errors;
    QObject::connect(&facade, &SessionFacade::errorOccurred,
                     [&errors](const QString& message) { errors.push_back(message); });

    // Break the library underneath the entry points.
    facade.open_library().database()->exec("DROP TABLE image_folder_items");
    facade.open_library().database()->exec("DROP TABLE image_folders");
    facade.open_library().database()->exec("DROP TABLE service_items");
    facade.open_library().database()->exec("DROP TABLE songs");

    SECTION("a controller's entry point")
    {
        REQUIRE_FALSE(facade.media()->playImageFolder(1, 0));
        REQUIRE(errors.size() == 1);
        // In words for the operator: SQLite's own ("no such table:
        // image_folders") stay in the log.
        CHECK(errors.front() == QStringLiteral("The library could not do that."));
        for (const auto* word : {"image_folders", "table", "SQL", "Unable to"}) {
            CHECK_FALSE(errors.front().contains(QLatin1String(word), Qt::CaseInsensitive));
        }
    }
    SECTION("a dialog's entry point")
    {
        // One that hands its error back for the dialog to show.
        facade.open_library().database()->exec("DROP TABLE services");
        const auto error = facade.library()->renameService(1, QStringLiteral("Renamed"));
        CHECK(error == QStringLiteral("The library could not do that."));
        CHECK(errors.empty());
    }
    SECTION("the facade's own entry point")
    {
        REQUIRE_FALSE(facade.live()->playSong(1));
        REQUIRE(errors.size() == 1);
        REQUIRE_FALSE(errors.front().isEmpty());
    }
    // Each of these answers what a view shows ("nothing uses it", no fades
    // of its own, books to choose from): without errorOccurred the view would
    // say something untrue.
    SECTION("what uses a font")
    {
        // The songs are among what is read.
        CHECK(facade.fonts()->uses(QStringLiteral("Inter")).isEmpty());
        REQUIRE(errors.size() == 1);
        CHECK(errors.front() == QStringLiteral("The library could not do that."));
    }
    SECTION("an audio file's own fades")
    {
        facade.open_library().database()->exec("DROP TABLE audio_files");
        CHECK(facade.media()->audioFileFades(QStringLiteral("hymn.mp3")).isEmpty());
        REQUIRE(errors.size() == 1);
        CHECK(errors.front() == QStringLiteral("The library could not do that."));
    }
    SECTION("a Bible reference")
    {
        const auto bible_id = facade.bible_repo()->import_bible(
            "KJV", "", {{.book = "John", .chapter = 3, .verse_num = 16, .text = "For God so loved"}});
        REQUIRE(facade.library()->selectBible(static_cast<qint64>(bible_id)));
        facade.open_library().database()->exec("DROP TABLE verses");
        const auto found = facade.library()->resolveBibleRef(QStringLiteral("John 3:16"));
        CHECK_FALSE(found.value(QStringLiteral("success")).toBool());
        // Said once, by the shell, and not again in place.
        CHECK_FALSE(found.contains(QStringLiteral("message")));
        REQUIRE(errors.size() == 1);
        CHECK(errors.front() == QStringLiteral("The library could not do that."));
    }
}

// The controllers only announce what changed; SessionFacade::wire_controllers
// decides what follows. A connection that went missing would fail silently,
// so each one is fired here and its effect checked.
TEST_CASE("The facade follows what every controller announces", "[app][wiring]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    QSignalSpy session_changed(&facade, &SessionFacade::sessionChanged);
    QSignalSpy frames(&facade, &SessionFacade::framesChanged);

    SECTION("live content changed: the models follow")
    {
        emit facade.library()->liveChanged(true);
        emit facade.media()->liveChanged(true);
        emit facade.freeformLibrary()->liveChanged(true);
        emit facade.stacks()->liveChanged(true);
        emit facade.program()->liveChanged(false);
        CHECK(session_changed.count() == 5);
    }
    SECTION("the title is read from what is live, with no signal to follow")
    {
        const auto output = facade.session().outputs().front().id;
        facade.session().play_freeform(seventhslide::app::FreeformLibraryController::live_content(
            -1, seventhslide::freeform::make_document("Promo", {output})));
        CHECK(facade.live()->current_title() == QStringLiteral("Promo"));
    }
    SECTION("outputs edited in place are saved")
    {
        auto* config = facade.open_library().config();
        const auto name = QString::fromStdString(facade.session().outputs().front().name);
        const auto renamed = facade.outputSettings()->updateOutputSettings(
            name, {{QStringLiteral("name"), QStringLiteral("Renamed")}});
        REQUIRE_FALSE(renamed.contains(QStringLiteral("error")));
        CHECK(config->load_outputs().front().name == "Renamed");
        const auto theme = facade.themes()->create(QStringLiteral("Renamed"), QStringLiteral("song"),
                                                   QStringLiteral("Kept"));
        REQUIRE_FALSE(theme.contains(QStringLiteral("error")));
        const auto stored = config->load_outputs().front().song_themes;
        CHECK(std::ranges::any_of(stored, [&](const auto& t) {
            return t.id == theme.value(QStringLiteral("id")).toString().toStdString();
        }));
    }
    SECTION("repaints and previews")
    {
        emit facade.playback()->repaintRequested();
        emit facade.destinations()->repaintRequested();
        CHECK(frames.count() == 2);
        QSignalSpy preview(&facade, &SessionFacade::previewRevisionChanged);
        emit facade.playback()->previewChanged();
        CHECK(preview.count() == 1);
        emit facade.destinations()->liveStateChanged();
        CHECK(session_changed.count() == 1);
    }
    SECTION("a profile switch re-reads the profiles")
    {
        QSignalSpy profiles(facade.profiles(), &seventhslide::app::ProfileController::profilesChanged);
        emit facade.profiles()->profileSwitched();
        CHECK(profiles.count() >= 1);
        CHECK(session_changed.count() >= 1);
    }
    SECTION("a library switch drops the media caches")
    {
        const auto images = facade.media_root() / "images";
        std::filesystem::create_directories(images);
        QImage pixel(1, 1, QImage::Format_RGB32);
        pixel.fill(Qt::red);
        REQUIRE(pixel.save(QString::fromStdString((images / "a.png").string())));
        const auto before = facade.media()->mediaThumbUrl(MediaKindQml::Image,
                                                          QStringLiteral("a.png"));
        REQUIRE_FALSE(before.isEmpty());
        emit facade.libraries()->librarySwitching();
        CHECK(facade.media()->mediaThumbUrl(MediaKindQml::Image, QStringLiteral("a.png"))
              != before);
    }
}

TEST_CASE("Stack commands pass the model's refusal through and save only what it accepts",
          "[app][stacks]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    auto* stacks = facade.stacks();
    const auto names = [stacks]() {
        QStringList out;
        for (const auto& row : stacks->stacks()) {
            out << row.toMap().value(QStringLiteral("name")).toString();
        }
        return out;
    };
    const auto id_of = [stacks](const QString& name) {
        for (const auto& row : stacks->stacks()) {
            const auto m = row.toMap();
            if (m.value(QStringLiteral("name")).toString() == name) {
                return m.value(QStringLiteral("id")).toString();
            }
        }
        return QString();
    };
    const QStringList start = names();
    REQUIRE(start.size() == 1);
    const QString main = id_of(start.front());

    CHECK(stacks->removeStack(main) == QStringLiteral("Cannot delete the last stack"));
    // A stack that is not there is refused in one sentence, whichever command.
    const auto nope = QStringLiteral("nope");
    CHECK(stacks->removeStack(nope) == refusal::stack_gone());
    CHECK(stacks->renameStack(nope, QStringLiteral("X")) == refusal::stack_gone());
    CHECK(stacks->setStackIcon(nope, QStringLiteral("layers")) == refusal::stack_gone());
    CHECK(stacks->reorderStacks({nope}) == refusal::stack_gone());
    CHECK(stacks->clearStack(nope) == refusal::stack_gone());
    CHECK(stacks->setStackTypeDefault(QStringLiteral("song"), nope) == refusal::stack_gone());
    CHECK(stacks->setStackPreview(nope, true) == refusal::stack_gone());
    CHECK(stacks->transition(nope) == refusal::stack_gone());
    CHECK(stacks->setAutoAdvance(nope, true, 5, false, false, false) == refusal::stack_gone());
    CHECK(stacks->reorderStacks({}) == QStringLiteral("Id list must include every stack"));
    CHECK(names() == start);

    // An empty name is the default one.
    REQUIRE(stacks->addStack(QString(), QStringLiteral("layers")).isEmpty());
    const QString added = id_of(QStringLiteral("Stack"));
    REQUIRE_FALSE(added.isEmpty());
    REQUIRE(stacks->renameStack(added, QStringLiteral("Lower Third")).isEmpty());
    REQUIRE(stacks->reorderStacks({added, main}).isEmpty());
    CHECK(names() == QStringList{QStringLiteral("Lower Third"), start.front()});

    // Saved: a second session on the same library reads the same stacks.
    SessionFacade again;
    REQUIRE(again.open_database(dir.path / "library.db"));
    QStringList reread;
    for (const auto& row : again.stacks()->stacks()) {
        reread << row.toMap().value(QStringLiteral("name")).toString();
    }
    CHECK(reread == names());
}

TEST_CASE("SRT codecs are found off the window's thread, and said when they are", "[app][srt]")
{
    // Opening a hardware encoder can take a second: the editor is answered
    // at once, empty while they are tried, and told when they are known.
    // (Known already when an earlier test in this run asked.)
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    auto* destinations = facade.destinations();
    bool ready = false;
    QObject::connect(destinations, &seventhslide::app::DestinationsController::streamCodecsReady,
                     [&] { ready = true; });
    auto rows = destinations->streamCodecs();
    if (rows.isEmpty()) {
        REQUIRE(QTest::qWaitFor([&] { return ready; }, 60000));
        rows = destinations->streamCodecs();
    }
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].toMap().value(QStringLiteral("key")) == QStringLiteral("h265"));
    CHECK(rows[1].toMap().value(QStringLiteral("key")) == QStringLiteral("h264"));
}

TEST_CASE("A stack's Preview is this computer's, kept per library and over a restart",
          "[app][stacks][preview]")
{
    using seventhslide::session::StackSide;
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir tmp;
    const auto row_of = [](SessionFacade& f, const QString& id) {
        for (const auto& row : f.stacks()->stacks()) {
            if (row.toMap().value(QStringLiteral("id")).toString() == id) {
                return row.toMap();
            }
        }
        return QVariantMap{};
    };
    const auto previews = [](SessionFacade& f) { return f.session().preview_stack_ids(); };
    using Ids = std::set<std::string, std::less<>>;
    QString main;
    QString lower;
    {
        SessionFacade facade;
        REQUIRE(facade.init_libraries(tmp.path));
        auto* stacks = facade.stacks();
        main = stacks->stacks().front().toMap().value(QStringLiteral("id")).toString();
        REQUIRE(stacks->addStack(QStringLiteral("Lower thirds"), QStringLiteral("layers")).isEmpty());
        lower = stacks->stacks().back().toMap().value(QStringLiteral("id")).toString();
        REQUIRE(lower != main);
        CHECK_FALSE(row_of(facade, main).value(QStringLiteral("preview")).toBool());

        QSignalSpy live(stacks, &seventhslide::app::StackController::liveChanged);
        REQUIRE(stacks->setStackPreview(main, true).isEmpty());
        CHECK(live.count() == 1);
        CHECK(previews(facade) == Ids{main.toStdString()});
        CHECK(row_of(facade, main).value(QStringLiteral("preview")).toBool());
        CHECK_FALSE(stacks->previewCued(main));
        // Asked again, nothing moves.
        REQUIRE(stacks->setStackPreview(main, true).isEmpty());
        CHECK(live.count() == 1);
        CHECK_FALSE(stacks->setStackPreview(QStringLiteral("nope"), true).isEmpty());

        // Kept on this computer, not in the library.
        const seventhslide::library::ConsoleStore store(tmp.path);
        CHECK(store.preview_stacks("default") == Ids{main.toStdString()});
        const auto saved = facade.open_library().config()->get_setting(
            seventhslide::model::k_setting_stacks);
        REQUIRE(saved);
        CHECK(saved->dump().find("preview") == std::string::npos);

        // A stack deleted is forgotten there.
        REQUIRE(stacks->setStackPreview(lower, true).isEmpty());
        REQUIRE(stacks->removeStack(lower).isEmpty());
        CHECK(store.preview_stacks("default") == Ids{main.toStdString()});
        CHECK(previews(facade) == Ids{main.toStdString()});

        // Another library has its own: none.
        REQUIRE(facade.libraries()->createLibrary(QStringLiteral("Kids")).isEmpty());
        REQUIRE(facade.libraries()->switchLibrary(QStringLiteral("kids")).isEmpty());
        CHECK(previews(facade).empty());
        const auto kids_main =
            facade.stacks()->stacks().front().toMap().value(QStringLiteral("id")).toString();
        REQUIRE(facade.stacks()->setStackPreview(kids_main, true).isEmpty());
        REQUIRE(facade.libraries()->switchLibrary(QStringLiteral("default")).isEmpty());
        CHECK(previews(facade) == Ids{main.toStdString()});
        // A library removed takes its own with it.
        REQUIRE(facade.libraries()->removeLibrary(QStringLiteral("kids")).isEmpty());
        CHECK(store.preview_stacks("kids").empty());
        CHECK(store.preview_stacks("default") == Ids{main.toStdString()});
    }
    SessionFacade restarted;
    REQUIRE(restarted.init_libraries(tmp.path));
    CHECK(restarted.session().preview_stack_ids() == Ids{main.toStdString()});

    // Something cued is what taking the Preview away drops; what is live
    // stays.
    auto* stack = restarted.session().find_stack(main.toStdString());
    REQUIRE(stack != nullptr);
    REQUIRE(stack->preview);
    seventhslide::model::Song song;
    song.title = "Be Thou My Vision";
    song.lyrics = "---[Verse:1]---\nBe thou my vision\n";
    song.verse_order = "v1";
    restarted.session().play_song(song, main.toStdString(), seventhslide::session::Land::Live);
    restarted.session().play_song(song, main.toStdString());
    CHECK(restarted.stacks()->previewCued(main));
    REQUIRE(restarted.stacks()->setStackPreview(main, false).isEmpty());
    CHECK(restarted.session().runtime({main.toStdString(), StackSide::Preview}) == nullptr);
    CHECK(restarted.session().find_stack(main.toStdString())->program.occupied());
    CHECK_FALSE(restarted.stacks()->previewCued(main));

    SessionFacade again;
    REQUIRE(again.init_libraries(tmp.path));
    CHECK(again.session().preview_stack_ids().empty());
}

TEST_CASE("A stack's Preview lasts the run without a console store", "[app][stacks][preview]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    auto* stacks = facade.stacks();
    const auto main = stacks->stacks().front().toMap().value(QStringLiteral("id")).toString();
    REQUIRE(stacks->setStackPreview(main, true).isEmpty());
    // Stack edits apply the settings again, and keep it.
    REQUIRE(stacks->addStack(QStringLiteral("Lower thirds"), QStringLiteral("layers")).isEmpty());
    CHECK(facade.session().preview_stack_ids()
          == std::set<std::string, std::less<>>{main.toStdString()});
    REQUIRE(stacks->setStackPreview(main, false).isEmpty());
    CHECK(facade.session().preview_stack_ids().empty());
}
