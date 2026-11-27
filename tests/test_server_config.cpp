// ``seventhslide --server config`` and ``settings``: the model against a server over
// its real socket, the settings kept and overridden, the refusals, and the screens
// driven by key without a terminal.
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <seventhslide/link/sync_settings.hpp>
#include <seventhslide/server/command_line.hpp>
#include <seventhslide/server/config_model.hpp>
#include <seventhslide/server/config_screen.hpp>
#include <seventhslide/server/control.hpp>
#include <seventhslide/server/data_folder.hpp>
#include <seventhslide/server/settings_edit.hpp>
#include <seventhslide/sync/member_list.hpp>

#include "../src/server/commands.hpp"
#include "link_test_helpers.hpp"
#include "server_test_helpers.hpp"
#include "test_temp_dir.hpp"

#include <QLockFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTcpServer>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include <chrono>
#include <fstream>
#include <future>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using nlohmann::json;
using seventhslide::link::SyncSettings;
using seventhslide::server::Action;
using seventhslide::server::AskServer;
using seventhslide::server::ConfigModel;
using seventhslide::server::ConfigUi;
using seventhslide::server::Done;
using seventhslide::server::Form;
using seventhslide::server::NoServer;
using seventhslide::server::parse_command_line;
using seventhslide::server::Snapshot;
using namespace seventhslide::test::link_fixture;
using namespace seventhslide::test::server_fixture;

namespace {

/// ``work`` off this thread, the server answering on this one meanwhile.
template <typename Work>
auto off_thread(Work work)
{
    auto result = std::async(std::launch::async, std::move(work));
    while (result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return result.get();
}

quint16 free_port()
{
    QTcpServer taken;
    REQUIRE(taken.listen(QHostAddress::LocalHost));
    return taken.serverPort();
}

/// A server that goes by its settings file, given no flag for any of them.
struct ByItsSettings {
    seventhslide::test::QtBootstrap qt;
    seventhslide::test::TempDir dir;
    std::unique_ptr<Server> server;

    std::filesystem::path data() const { return dir.path / "server"; }
    void start()
    {
        ServerOptions options;
        options.data = data();
        options.listen = QHostAddress::LocalHost;
        options.box_params = k_quick;
        server = std::make_unique<Server>(std::move(options));
    }
    void stop() { server.reset(); }
    /// A port of its own, and nothing said on this computer's network.
    ByItsSettings()
    {
        std::filesystem::create_directories(data());
        SyncSettings settings;
        settings.port = free_port();
        settings.discovery = false;
        REQUIRE(settings.save(data()));
        start();
    }
};

Done submit(const Form& form, const std::vector<std::string>& answers)
{
    return off_thread([&] { return form.submit(answers); });
}

const Action* action_named(const std::vector<Action>& actions, const std::string& label)
{
    for (const auto& action : actions) {
        if (action.label == label) {
            return &action;
        }
    }
    return nullptr;
}

std::vector<std::string> labels_of(const std::vector<Action>& actions)
{
    std::vector<std::string> out;
    for (const auto& action : actions) {
        out.push_back(action.label);
    }
    return out;
}

// --- The screens, with a server made of JSON. ---

json a_status()
{
    return {{"ok", true},
            {"server",
             {{"name", "Church server"},
              {"version", "1"},
              {"started", "2026-11-03T10:00:00Z"},
              {"fingerprint", "aaaa"},
              {"port", 8766},
              {"addresses", json::array({"10.0.0.2:8766"})},
              {"data", "/data"},
              {"disk_free", 1000},
              {"disk_total", 2000},
              {"sync_automatically", true}}},
            {"libraries",
             json::array({{{"uid", "abcdef0123456789abcdef0123456789"},
                           {"standing", "member"},
                           {"joined", true},
                           {"problem", ""},
                           {"size_limit", 0},
                           {"computers",
                            json::array({{{"machine", "server"}, {"name", "Church server"}, {"role", "viewer"},
                                          {"holds_admin_key", true}, {"this_computer", true}},
                                         {{"machine", "laptop"}, {"name", "Laptop"}, {"role", "editor"},
                                          {"holds_admin_key", true}, {"connected", true}}})}}})},
            {"connections", json::array()}};
}

/// Answers as a server would, and keeps what it was asked.
struct FakeServer {
    bool running = true;
    json last;
    int asked = 0;
    json settings{{"computer_name", "Church server"},
                  {"addresses", json::array()},
                  {"port", nullptr},
                  {"discovery", true},
                  {"sync_automatically", true}};

    AskServer ask()
    {
        return [this](const json& request, int) -> json {
            if (!running) {
                throw NoServer("No SeventhSlide server is running.");
            }
            last = request;
            ++asked;
            const auto command = request.value("command", std::string());
            if (command == "status") {
                return a_status();
            }
            if (command == "settings") {
                if (request.contains("set")) {
                    settings.update(request["set"]);
                }
                return {{"ok", true},
                        {"running", true},
                        {"settings", settings},
                        {"now", settings},
                        {"flags", json::array()},
                        {"after_restart", json::array()}};
            }
            if (request.value("action", std::string()) == "invite") {
                if (request.value("password", std::string()) != "right password") {
                    return {{"ok", false}, {"error", "That is not the library's admin password."}};
                }
                return {{"ok", true}, {"code", std::string(250, 'C')}, {"expires", "2026-11-10T10:00:00Z"}};
            }
            return {{"ok", true}};
        };
    }
};

/// The screens at the keyboard: work is done at once, on this thread.
struct Screens {
    FakeServer fake;
    std::filesystem::path data;
    bool left = false;
    std::vector<std::function<void()>> held;
    bool hold_work = false;
    std::unique_ptr<ConfigUi> ui;

    explicit Screens(const std::filesystem::path& where, bool running = true) : data(where)
    {
        fake.running = running;
        ui = std::make_unique<ConfigUi>(
            ConfigModel(data, fake.ask()),
            [this](std::function<void()> work, std::function<void()> then) {
                const auto all = [work = std::move(work), then = std::move(then)] {
                    work();
                    then();
                };
                if (hold_work) {
                    held.push_back(all);
                } else {
                    all();
                }
            },
            [this] { left = true; });
        ui->show(ConfigModel(data, fake.ask()).snapshot());
    }

    void press(const ftxui::Event& event) { (void)ui->component()->OnEvent(event); }
    void type(const std::string& what)
    {
        for (const char c : what) {
            press(ftxui::Event::Character(c));
        }
    }
    /// What the terminal would show, without its colours.
    std::string drawn()
    {
        auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(120), ftxui::Dimension::Fixed(40));
        ftxui::Render(screen, ui->component()->Render());
        return QString::fromStdString(screen.ToString())
            .remove(QRegularExpression(QStringLiteral("\x1b\\[[0-9;?]*[a-zA-Z]")))
            .toStdString();
    }
};

}  // namespace

TEST_CASE("Settings changed through the socket take effect, at once or at the next start", "[server][config]")
{
    ByItsSettings host;
    const ConfigModel model(host.data());
    auto now = off_thread([&] { return model.snapshot(); });
    REQUIRE(now.running);
    CHECK(now.problem.empty());
    auto fields = seventhslide::server::settings_fields(now);
    CHECK(fields.name == host.server->identity().computer);  // none chosen: what it goes by
    CHECK(seventhslide::server::setting_note(now, "port").empty());

    SECTION("nothing changed, nothing is written")
    {
        const auto before = SyncSettings::load(host.data());
        CHECK(off_thread([&] { return model.save_settings(now, fields); }).message == "Nothing to save: these are the settings already.");
        CHECK(SyncSettings::load(host.data()) == before);
        CHECK(before.computer_name.empty());
    }
    SECTION("a name, addresses and automatic sync, at once")
    {
        fields.name = "  Main server ";
        fields.addresses = "slides.example.org, 203.0.113.9:9000";
        fields.sync_automatically = false;
        REQUIRE(off_thread([&] { return model.save_settings(now, fields); }).ok);
        CHECK(host.server->identity().computer == "Main server");
        CHECK_FALSE(host.server->node().sync_automatically());
        CHECK(host.server->addresses().size() == 2);
        const auto saved = SyncSettings::load(host.data());
        CHECK(saved.computer_name == "Main server");
        CHECK(saved.addresses == std::vector<std::string>{"slides.example.org", "203.0.113.9:9000"});
        CHECK_FALSE(saved.sync_automatically);

        // Kept across a restart.
        host.stop();
        host.start();
        CHECK(host.server->identity().computer == "Main server");
        CHECK_FALSE(host.server->node().sync_automatically());
    }
    SECTION("an upload speed cap, at once, and kept across a restart")
    {
        fields.upload_cap = "5";
        REQUIRE(off_thread([&] { return model.save_settings(now, fields); }).ok);
        CHECK(host.server->node().upload_cap() == 5'000);
        CHECK(host.server->node().link().upload_cap() == 625'000);
        CHECK(SyncSettings::load(host.data()).upload_cap_kbit == 5'000);
        now = off_thread([&] { return model.snapshot(); });
        fields = seventhslide::server::settings_fields(now);
        CHECK(fields.upload_cap == "5");
        // Not one: nothing changes.
        fields.upload_cap = "fast";
        const auto refused = off_thread([&] { return model.save_settings(now, fields); });
        CHECK_FALSE(refused.ok);
        CHECK_THAT(refused.message, ContainsSubstring("megabits"));
        CHECK(SyncSettings::load(host.data()).upload_cap_kbit == 5'000);

        host.stop();
        host.start();
        CHECK(host.server->node().link().upload_cap() == 625'000);
        // None again.
        now = off_thread([&] { return model.snapshot(); });
        fields = seventhslide::server::settings_fields(now);
        fields.upload_cap = "";
        REQUIRE(off_thread([&] { return model.save_settings(now, fields); }).ok);
        CHECK(host.server->node().link().upload_cap() == 0);
        CHECK(SyncSettings::load(host.data()).upload_cap_kbit == 0);
    }
    SECTION("address sharing, at once")
    {
        CHECK(fields.share_address);
        fields.share_address = false;
        REQUIRE(off_thread([&] { return model.save_settings(now, fields); }).ok);
        CHECK_FALSE(host.server->node().link().share_address());
        CHECK_FALSE(SyncSettings::load(host.data()).share_address);
        host.stop();
        host.start();
        CHECK_FALSE(host.server->node().link().share_address());
    }
    SECTION("a port, when it next starts")
    {
        const auto was = host.server->port();
        const auto wanted = free_port();
        fields.port = std::to_string(wanted);
        const auto done = off_thread([&] { return model.save_settings(now, fields); });
        REQUIRE(done.ok);
        CHECK_THAT(done.message, ContainsSubstring("next starts"));
        CHECK(host.server->port() == was);
        now = off_thread([&] { return model.snapshot(); });
        CHECK(seventhslide::server::setting_note(now, "port") == "Takes effect when the server next starts");
        host.stop();
        host.start();
        CHECK(host.server->port() == wanted);
        now = off_thread([&] { return model.snapshot(); });
        CHECK(seventhslide::server::setting_note(now, "port").empty());
    }
    SECTION("what cannot be, said, with nothing changed")
    {
        const auto before = SyncSettings::load(host.data());
        auto bad = fields;
        bad.name = " ";
        CHECK_THAT(off_thread([&] { return model.save_settings(now, bad); }).message, ContainsSubstring("Give this computer a name"));
        bad = fields;
        bad.port = "70000";
        CHECK_THAT(off_thread([&] { return model.save_settings(now, bad); }).message, ContainsSubstring("whole number"));
        bad = fields;
        bad.addresses = "good.example.org, not:an:address";
        CHECK_THAT(off_thread([&] { return model.save_settings(now, bad); }).message, ContainsSubstring("not:an:address"));
        CHECK(SyncSettings::load(host.data()) == before);
    }
}

TEST_CASE("A flag sets a setting for its run, and the file keeps what was saved", "[server][config]")
{
    ServerHost host;  // given its name, addresses and port by flags
    const ConfigModel model(host.data());
    auto now = off_thread([&] { return model.snapshot(); });
    REQUIRE(now.running);
    CHECK(seventhslide::server::setting_note(now, "computer_name") == "A flag sets this for this run: Church server");
    CHECK_THAT(seventhslide::server::setting_note(now, "addresses"), ContainsSubstring("127.0.0.1"));
    CHECK_THAT(seventhslide::server::setting_note(now, "port"), ContainsSubstring("A flag sets this"));
    CHECK(seventhslide::server::setting_note(now, "discovery") == "A flag sets this for this run: off");
    CHECK(seventhslide::server::setting_note(now, "sync_automatically").empty());

    auto fields = seventhslide::server::settings_fields(now);
    fields.name = "Saved name";
    REQUIRE(off_thread([&] { return model.save_settings(now, fields); }).ok);
    CHECK(host->identity().computer == "Church server");  // the flag, for this run
    CHECK(SyncSettings::load(host.data()).computer_name == "Saved name");
    const auto reply = off_thread([&] { return model.snapshot(); }).settings;
    CHECK(reply["settings"]["computer_name"] == "Saved name");
    CHECK(reply["now"]["computer_name"] == "Church server");

    // Started without it, it goes by the file.
    host.stop();
    host.name.clear();
    host.start();
    CHECK(host->identity().computer == "Saved name");
}

TEST_CASE("With no server, settings are the file's, and libraries say to start it", "[server][config]")
{
    seventhslide::test::QtBootstrap qt;
    seventhslide::test::TempDir dir;
    const auto data = dir.path / "server";
    const ConfigModel model(data);
    const auto now = model.snapshot();
    CHECK_FALSE(now.running);
    CHECK(now.problem.empty());
    REQUIRE(now.settings.is_object());
    CHECK(now.settings["running"] == false);
    CHECK_FALSE(std::filesystem::exists(data));  // looking makes nothing

    auto fields = seventhslide::server::settings_fields(now);
    SECTION("edited directly")
    {
        fields.name = "Set before it ever ran";
        fields.port = "9123";
        fields.discovery = false;
        const auto done = model.save_settings(now, fields);
        REQUIRE(done.ok);
        CHECK_THAT(done.message, ContainsSubstring("starts with these"));
        const auto saved = SyncSettings::load(data);
        CHECK(saved.computer_name == "Set before it ever ran");
        CHECK(saved.port == 9123);
        CHECK_FALSE(saved.discovery);
#ifndef Q_OS_WIN
        CHECK((std::filesystem::status(data).permissions() & std::filesystem::perms::group_all) == std::filesystem::perms::none);
#endif
        CHECK_FALSE(std::filesystem::exists(data / "server.lock"));  // let go of
    }
    SECTION("left alone while another holds the lock")
    {
        std::filesystem::create_directories(data);
        QLockFile held(QString::fromStdString((data / "server.lock").string()));
        REQUIRE(held.tryLock(0));
        fields.name = "Not this";
        const auto done = model.save_settings(now, fields);
        CHECK_FALSE(done.ok);
        CHECK_THAT(done.message, ContainsSubstring("not answering"));
        CHECK(SyncSettings::load(data).computer_name.empty());
    }
    SECTION("not in SeventhSlide's own data folder")
    {
        std::filesystem::create_directories(data);
        std::ofstream(data / "libraries.json") << "{}";
        fields.name = "Not this";
        const auto done = model.save_settings(now, fields);
        CHECK_FALSE(done.ok);
        CHECK_THAT(done.message, ContainsSubstring("own data folder"));
        CHECK_FALSE(std::filesystem::exists(data / "sync_settings.json"));
    }
    SECTION("what needs a server says so")
    {
        const auto done = model.join_form().submit({"anything"});
        CHECK_FALSE(done.ok);
        CHECK_THAT(done.message, ContainsSubstring("Start it with"));
        CHECK_FALSE(model.sync_now().ok);
        CHECK(seventhslide::server::library_rows(now).empty());
        CHECK(model.library_actions(now, "abc").empty());
    }
}

TEST_CASE("A library's work, from the forms, reaches the computers", "[server][config]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    Computer stage{"stage", "Stage display"};
    ServerHost host;
    const auto founded = put_online(church);
    keep_at(church, host, founded.admin);
    REQUIRE(settle_all({&church}, host));
    const auto& uid = church.uid;
    const ConfigModel model(host.data());
    const auto look = [&] { return off_thread([&] { return model.snapshot(); }); };

    auto now = look();
    REQUIRE(now.running);
    REQUIRE(seventhslide::server::library_rows(now).size() == 1);
    CHECK(seventhslide::server::library_rows(now).front().label == uid.substr(0, 8));

    // Holding the key: everything the Sync page offers.
    auto of_library = model.library_actions(now, uid);
    CHECK(labels_of(of_library)
          == std::vector<std::string>{"Invite an Editor…", "Invite a Viewer…", "Size Limit…",
                                      "Change Password…", "Forgotten Password…", "New Recovery Code…", "Stop Syncing…"});

    // An invite: refused without the password, and a code with it.
    const auto& invite = action_named(of_library, "Invite a Viewer…")->form;
    REQUIRE(invite.fields.size() == 2);
    CHECK(invite.fields.at(0).value == "7");
    CHECK(invite.fields.at(1).kind == seventhslide::server::FormField::Kind::Secret);
    CHECK_THAT(submit(invite, {"7", "wrong password here"}).message, ContainsSubstring("not the library's admin password"));
    CHECK_THAT(submit(invite, {"99", k_passphrase}).message, ContainsSubstring("1 to 30"));
    const auto made = submit(invite, {"2", k_passphrase});
    REQUIRE(made.ok);
    REQUIRE_FALSE(made.code.empty());
    CHECK(seventhslide::sync::read_invite(made.code).invite.role == Role::Viewer);
    join_via(stage, uid, made.code, host);

    // Another, not used: it waits among the computers.
    const auto waiting = submit(invite, {"7", k_passphrase});
    REQUIRE(waiting.ok);
    const auto waiting_id = seventhslide::sync::read_invite(waiting.code).invite.id;

    join_via(laptop, uid, church.members().invite(Role::Editor, k_expires, founded.admin), host);
    REQUIRE(settle_all({&church, &laptop, &stage}, host));
    now = look();

    // The invite waiting: where its computer is, here alone, and its withdrawal.
    const auto rows = seventhslide::server::computer_rows(now, uid);
    const auto invite_row = std::ranges::find(rows, waiting_id, &seventhslide::server::ComputerRow::invite);
    REQUIRE(invite_row != rows.end());
    CHECK(invite_row->machine.empty());
    CHECK(invite_row->name == "Invited viewer " + waiting_id.substr(0, 6));
    CHECK_THAT(invite_row->state, ContainsSubstring("Waiting to join"));
    auto of_invite = model.invite_actions(now, uid, waiting_id);
    CHECK(labels_of(of_invite) == std::vector<std::string>{"Where It Is…", "Withdraw…"});
    REQUIRE(submit(action_named(of_invite, "Where It Is…")->form, {"far.example.org, 203.0.113.9:9000"}).ok);
    CHECK(host->reach().invite_addresses(uid, waiting_id)
          == std::vector<std::string>{"far.example.org:8766", "203.0.113.9:9000"});
    CHECK_FALSE(submit(action_named(of_invite, "Where It Is…")->form, {"not:an:address"}).ok);
    CHECK_FALSE(submit(action_named(of_invite, "Withdraw…")->form, {"wrong password here"}).ok);
    REQUIRE(submit(action_named(of_invite, "Withdraw…")->form, {k_passphrase}).ok);
    now = look();
    CHECK(model.invite_actions(now, uid, waiting_id).empty());
    CHECK(std::ranges::none_of(seventhslide::server::computer_rows(now, uid),
                               [&](const auto& row) { return row.invite == waiting_id; }));

    // A computer: this one has nothing done to it, the others do.
    CHECK(model.computer_actions(now, uid, host->identity().machine).empty());
    auto of_stage = model.computer_actions(now, uid, "stage");
    CHECK(labels_of(of_stage) == std::vector<std::string>{"Addresses…", "Make an Editor…", "Give the Admin Key…", "Remove…"});
    CHECK(labels_of(model.computer_actions(now, uid, "laptop"))
          == std::vector<std::string>{"Addresses…", "Make a Viewer…", "Remove…"});

    // The key, there and back.
    REQUIRE(submit(action_named(of_stage, "Give the Admin Key…")->form, {k_passphrase}).ok);
    REQUIRE(settle_all({&church, &laptop, &stage}, host));
    CHECK(church.list().find("stage")->holds_admin_key);
    now = look();
    of_stage = model.computer_actions(now, uid, "stage");
    CHECK(action_named(of_stage, "Give the Admin Key…") == nullptr);
    REQUIRE(submit(action_named(of_stage, "Stop Sending the Admin Key…")->form, {k_passphrase}).ok);
    REQUIRE(settle_all({&church, &laptop, &stage}, host));
    CHECK_FALSE(church.list().find("stage")->holds_admin_key);

    // An address, here alone, and gone again.
    REQUIRE(submit(action_named(of_stage, "Addresses…")->form, {"stage.example.org"}).ok);
    now = look();
    const auto stage_row = [&] {
        for (const auto& row : seventhslide::server::computer_rows(now, uid)) {
            if (row.machine == "stage") {
                return row;
            }
        }
        FAIL("no such computer");
        return seventhslide::server::ComputerRow{};
    };
    CHECK(stage_row().typed_addresses == std::vector<std::string>{"stage.example.org:8766"});
    CHECK(stage_row().role == "Viewer");
    CHECK(action_named(model.computer_actions(now, uid, "stage"), "Addresses…")->form.fields.at(0).value == "stage.example.org:8766");
    REQUIRE(submit(action_named(of_stage, "Addresses…")->form, {""}).ok);
    now = look();
    CHECK(stage_row().typed_addresses.empty());
    CHECK(church.list().find("stage")->addresses.empty());

    // A role, a size limit, a removal.
    REQUIRE(submit(action_named(model.computer_actions(now, uid, "laptop"), "Make a Viewer…")->form, {k_passphrase}).ok);
    const auto& limit = action_named(of_library, "Size Limit…")->form;
    CHECK(limit.fields.at(0).value == "none");
    CHECK_THAT(submit(limit, {"lots", k_passphrase}).message, ContainsSubstring("is not a size"));
    REQUIRE(submit(limit, {"50GB", k_passphrase}).ok);
    REQUIRE(submit(action_named(of_stage, "Remove…")->form, {k_passphrase}).ok);
    REQUIRE(settle_all({&church, &laptop}, host));
    auto list = church.list();
    CHECK(list.find("laptop")->role == Role::Viewer);
    CHECK(list.size_limit == 50'000'000'000);
    CHECK(list.find("stage")->removed);
    now = look();
    CHECK(action_named(model.library_actions(now, uid), "Size Limit…")->form.fields.at(0).value == "50.0 GB");
    CHECK(model.computer_actions(now, uid, "stage").empty());  // removed

    // The password: its checks, then with the current one, then forgotten.
    const auto& change = action_named(of_library, "Change Password…")->form;
    CHECK_THAT(submit(change, {k_passphrase, "a new password here", "not the same one"}).message, ContainsSubstring("not the same"));
    CHECK_THAT(submit(change, {k_passphrase, "short", "short"}).message, ContainsSubstring("characters"));
    const std::string second = "a second admin password";
    REQUIRE(submit(change, {k_passphrase, second, second}).ok);
    const auto recovery = submit(action_named(of_library, "New Recovery Code…")->form, {second});
    REQUIRE(recovery.ok);
    REQUIRE_FALSE(recovery.code.empty());
    const std::string third = "a third admin password";
    const auto forgotten = submit(action_named(of_library, "Forgotten Password…")->form, {recovery.code, third, third});
    REQUIRE(forgotten.ok);
    CHECK_FALSE(forgotten.code.empty());  // the code is spent: another
    CHECK_FALSE(submit(invite, {"2", second}).ok);
    CHECK(submit(invite, {"2", third}).ok);

    // And it leaves.
    const auto left = submit(action_named(of_library, "Stop Syncing…")->form, {});
    REQUIRE(left.ok);
    CHECK_THAT(left.message, ContainsSubstring("still lists this server"));
    CHECK(seventhslide::server::library_rows(look()).empty());
}

TEST_CASE("Without the admin key, there is no admin work to be offered", "[server][config]")
{
    Computer church{"church", "Church PC"};
    ServerHost host;
    const auto founded = put_online(church);
    REQUIRE(host.join(invite_to_reach(church, founded.admin))["ok"] == true);
    church.link.connect_to("127.0.0.1", host->port(), std::nullopt);
    REQUIRE(settle_all({&church}, host));
    REQUIRE(church.list().find(host->identity().machine) != nullptr);

    const ConfigModel model(host.data());
    const auto now = off_thread([&] { return model.snapshot(); });
    CHECK(labels_of(model.library_actions(now, church.uid)) == std::vector<std::string>{"Stop Syncing…"});
    CHECK(labels_of(model.computer_actions(now, church.uid, "church")) == std::vector<std::string>{"Addresses…"});
}

TEST_CASE("A library is joined from the form, by a code a terminal broke across lines", "[server][config]")
{
    Computer church{"church", "Church PC"};
    ServerHost host;
    const auto founded = put_online(church);
    auto code = invite_to_reach(church, founded.admin);
    // As copied off a terminal: a line break each so far.
    for (std::size_t at = 76; at < code.size(); at += 77) {
        code.insert(at, "\n");
    }
    code = " " + code + "  \n";
    const ConfigModel model(host.data());
    CHECK_THAT(submit(model.join_form(), {"  "}).message, ContainsSubstring("Paste the invite"));
    CHECK_FALSE(submit(model.join_form(), {"not an invite at all"}).ok);
    const auto done = submit(model.join_form(), {code});
    INFO(done.message);
    REQUIRE(done.ok);
    CHECK_THAT(done.message, ContainsSubstring(church.uid));
    CHECK(host->library(church.uid) != nullptr);
}

TEST_CASE("Which data folder, and who may use it", "[server][config]")
{
    seventhslide::test::TempDir dir;
    const auto pointer = dir.path / "server.json";
    const std::filesystem::path fallback = "/home/someone/server";
    using seventhslide::server::data_folder_from;

    CHECK(data_folder_from(pointer, fallback) == fallback);  // no such file
    std::ofstream(pointer) << R"({"data": "/var/lib/seventhslide-server"})";
    CHECK(data_folder_from(pointer, fallback) == "/var/lib/seventhslide-server");
    // One that says nothing is not quietly another server, with another key.
    for (const char* bad : {"not json", "{}", R"({"data": ""})", R"({"data": 7})"}) {
        std::ofstream(pointer) << bad;
        INFO(bad);
        CHECK_THROWS_WITH(data_folder_from(pointer, fallback), ContainsSubstring("does not say where"));
    }

    using seventhslide::server::wrong_user;
    const std::string right = "sudo -u slides seventhslide --server config";
    constexpr unsigned k_root = 0;
    CHECK_FALSE(wrong_user(1000, 1000, "slides", true, right));
    CHECK_FALSE(wrong_user(k_root, k_root, "root", true, right));  // a container's own folder
    CHECK_FALSE(wrong_user(k_root, 1000, "slides", false, right));  // only asks the server
    CHECK_THAT(wrong_user(k_root, 1000, "slides", true, right).value_or(""), ContainsSubstring(right));
    CHECK_THAT(wrong_user(k_root, 1000, "slides", true, right).value_or(""), ContainsSubstring("stop the service"));
    CHECK_THAT(wrong_user(1001, 1000, "slides", false, right).value_or(""), ContainsSubstring("belongs to slides"));
    // Not there yet: nobody's to refuse.
    CHECK_FALSE(seventhslide::server::user_refused(dir.path / "not-there", true, "config"));
    CHECK_FALSE(seventhslide::server::user_refused(dir.path, true, "config"));
}

TEST_CASE("The command line of config and settings", "[server][config]")
{
    using seventhslide::server::Command;
    CHECK(parse_command_line({"config", "--data", "/d"}).command == Command::Config);
    CHECK_THROWS_AS(parse_command_line({"config", "more"}), std::invalid_argument);
    CHECK_THROWS_AS(parse_command_line({"config", "--name", "x"}), std::invalid_argument);
    const auto read = parse_command_line({"settings", "--json", "--data", "/d"});
    CHECK(read.command == Command::Settings);
    CHECK(read.json);
    CHECK(read.action.empty());
    CHECK(parse_command_line({"settings", "addresses", "a", "b", "--data", "/d"}).action == std::vector<std::string>{"addresses", "a", "b"});
    CHECK_THROWS_AS(parse_command_line({"settings", "port"}), std::invalid_argument);

    const SyncSettings none;
    auto changed = none;
    using seventhslide::server::apply_settings_changes;
    CHECK(apply_settings_changes(changed, {{"port", 9000}, {"discovery", false}}).empty());
    CHECK(changed.port == 9000);
    CHECK_FALSE(changed.discovery);
    CHECK(apply_settings_changes(changed, {{"port", nullptr}}).empty());
    CHECK_FALSE(changed.port.has_value());
    // All or nothing.
    changed = none;
    CHECK_FALSE(apply_settings_changes(changed, {{"computer_name", "Fine"}, {"port", 0}}).empty());
    CHECK(changed == none);
    CHECK_THAT(apply_settings_changes(changed, {{"no_such_thing", 1}}), ContainsSubstring("no setting"));
    CHECK_THAT(apply_settings_changes(changed, json::array()), ContainsSubstring("Say which"));
    json many = json::array();
    for (std::size_t i = 0; i <= seventhslide::sync::k_most_addresses; ++i) {
        many.push_back("host" + std::to_string(i) + ".example.org");
    }
    CHECK_THAT(apply_settings_changes(changed, {{"addresses", many}}), ContainsSubstring("at most"));
    changed = none;
    CHECK(apply_settings_changes(changed, {{"upload_cap_kbit", 2'500}}).empty());
    CHECK(changed.upload_cap_kbit == 2'500);
    CHECK(apply_settings_changes(changed, {{"upload_cap_kbit", 0}}).empty());
    CHECK(changed.upload_cap_kbit == 0);
    CHECK_THAT(apply_settings_changes(changed, {{"upload_cap_kbit", 5}}), ContainsSubstring("megabits"));
    CHECK_THAT(apply_settings_changes(changed, {{"upload_cap_kbit", "fast"}}), ContainsSubstring("megabits"));
    CHECK(apply_settings_changes(changed, {{"share_address", false}}).empty());
    CHECK_FALSE(changed.share_address);
    CHECK_THAT(apply_settings_changes(changed, {{"share_address", "no"}}), ContainsSubstring("on or off"));
}

TEST_CASE("The screens show the three pages, and a key pressed next is not typed in a field", "[server][config][ui]")
{
    seventhslide::test::QtBootstrap qt;
    seventhslide::test::TempDir dir;
    Screens s(dir.path);
    CHECK_THAT(s.drawn(), ContainsSubstring("Church server"));
    CHECK_THAT(s.drawn(), ContainsSubstring("Running"));
    CHECK_THAT(s.drawn(), ContainsSubstring("Sync Now"));
    CHECK_THAT(s.drawn(), ContainsSubstring("Up to date."));

    s.press(ftxui::Event::Character('2'));
    CHECK_THAT(s.drawn(), ContainsSubstring("abcdef01"));
    CHECK_THAT(s.drawn(), ContainsSubstring("Join a Synced Library…"));

    s.press(ftxui::Event::Character('3'));
    CHECK_THAT(s.drawn(), ContainsSubstring("Routable address"));
    CHECK_THAT(s.drawn(), ContainsSubstring("Sync automatically"));
    CHECK(s.fake.settings["computer_name"] == "Church server");

    // Come from inside another page, q still leaves: it is not typed in the name.
    s.press(ftxui::Event::Character('2'));
    s.press(ftxui::Event::ArrowDown);
    (void)s.drawn();
    s.press(ftxui::Event::Character('3'));
    (void)s.drawn();
    CHECK_FALSE(s.left);
    s.press(ftxui::Event::Character('q'));
    CHECK(s.left);
    CHECK_THAT(s.drawn(), !ContainsSubstring("qChurch"));
}

TEST_CASE("In a settings field keys are typing, what is typed stays, and Save sends it", "[server][config][ui]")
{
    seventhslide::test::QtBootstrap qt;
    seventhslide::test::TempDir dir;
    Screens s(dir.path);
    s.press(ftxui::Event::Character('3'));
    s.press(ftxui::Event::ArrowDown);  // into the name
    CHECK_THAT(s.drawn(), ContainsSubstring("Esc leaves"));
    s.type("q1");
    CHECK_FALSE(s.left);
    CHECK_THAT(s.drawn(), ContainsSubstring("Church serverq1"));

    // How things stand now does not take it away.
    s.ui->show(ConfigModel(s.data, s.fake.ask()).snapshot());
    CHECK_THAT(s.drawn(), ContainsSubstring("Church serverq1"));

    // Down the page to Save, the last of it.
    for (int i = 0; i < 8; ++i) {
        s.press(ftxui::Event::ArrowDown);
    }
    (void)s.drawn();
    s.press(ftxui::Event::Return);
    CHECK(s.fake.settings["computer_name"] == "Church serverq1");
    CHECK_THAT(s.drawn(), ContainsSubstring("Saved."));

    // Out of the field, q leaves again.
    s.press(ftxui::Event::Character('3'));
    s.press(ftxui::Event::Character('q'));
    CHECK(s.left);
}

TEST_CASE("A form asks, keeps no password, and shows a code to copy", "[server][config][ui]")
{
    seventhslide::test::QtBootstrap qt;
    seventhslide::test::TempDir dir;
    Screens s(dir.path);
    s.press(ftxui::Event::Character('2'));
    (void)s.drawn();
    // Across to what to do; the computer picked is the server itself, so the
    // first thing is the library's first.
    s.press(ftxui::Event::ArrowDown);
    s.press(ftxui::Event::ArrowRight);
    s.press(ftxui::Event::ArrowRight);
    (void)s.drawn();
    s.press(ftxui::Event::Return);
    CHECK_THAT(s.drawn(), ContainsSubstring("Invite an editor"));
    CHECK_THAT(s.drawn(), ContainsSubstring("Good for (days)"));

    SECTION("Esc goes back, with nothing asked")
    {
        const auto asked = s.fake.asked;
        s.press(ftxui::Event::Escape);
        CHECK_THAT(s.drawn(), ContainsSubstring("What to do"));
        CHECK(s.fake.asked == asked);
    }
    SECTION("a wrong password is said, and gone")
    {
        s.press(ftxui::Event::Return);  // on to the password
        s.type("wrong one");
        CHECK_THAT(s.drawn(), !ContainsSubstring("wrong one"));
        CHECK_THAT(s.drawn(), ContainsSubstring("•••••••••"));
        s.press(ftxui::Event::Return);
        CHECK_THAT(s.drawn(), ContainsSubstring("not the library's admin password"));
        CHECK_THAT(s.drawn(), !ContainsSubstring("•••"));
        CHECK(s.fake.last["password"] == "wrong one");
    }
    SECTION("a code gets a page, and is printed on leaving when asked")
    {
        s.press(ftxui::Event::Return);
        s.type("right password");
        s.press(ftxui::Event::Return);
        const auto page = s.drawn();
        CHECK_THAT(page, ContainsSubstring("Good once"));
        // All of it, whatever the width of the terminal.
        CHECK(std::ranges::count(page, 'C') >= 250);
        CHECK(s.ui->to_print().empty());
        s.press(ftxui::Event::ArrowUp);
        (void)s.drawn();
        s.press(ftxui::Event::Return);  // tick it
        CHECK_THAT(s.drawn(), ContainsSubstring("Print it when I leave"));
        s.press(ftxui::Event::ArrowDown);
        (void)s.drawn();
        s.press(ftxui::Event::Return);  // close
        CHECK_THAT(s.drawn(), ContainsSubstring("What to do"));
        CHECK(s.ui->to_print() == std::vector<std::string>{std::string(250, 'C')});
    }
    SECTION("closed unticked, by Enter or Esc, it is not printed")
    {
        s.press(ftxui::Event::Return);
        s.type("right password");
        s.press(ftxui::Event::Return);
        (void)s.drawn();
        s.press(GENERATE(ftxui::Event::Return, ftxui::Event::Escape));
        CHECK_THAT(s.drawn(), ContainsSubstring("What to do"));
        CHECK(s.ui->to_print().empty());
    }
}

TEST_CASE("While something is being worked on, keys wait", "[server][config][ui]")
{
    seventhslide::test::QtBootstrap qt;
    seventhslide::test::TempDir dir;
    Screens s(dir.path);
    s.hold_work = true;
    s.press(ftxui::Event::ArrowDown);  // Sync Now
    (void)s.drawn();
    s.press(ftxui::Event::Return);
    REQUIRE(s.held.size() == 1);
    CHECK_THAT(s.drawn(), ContainsSubstring("Working..."));
    s.press(ftxui::Event::Character('q'));
    s.press(ftxui::Event::Return);
    CHECK_FALSE(s.left);
    CHECK(s.held.size() == 1);

    s.held.front()();
    CHECK_THAT(s.drawn(), !ContainsSubstring("Working..."));
    CHECK_THAT(s.drawn(), ContainsSubstring("Syncing every library now."));
    s.press(ftxui::Event::Character('q'));
    CHECK(s.left);
}

TEST_CASE("With no server, the screens say so, and settings still work", "[server][config][ui]")
{
    seventhslide::test::QtBootstrap qt;
    seventhslide::test::TempDir dir;
    const auto data = dir.path / "server";
    Screens s(data, /*running=*/false);
    CHECK_THAT(s.drawn(), ContainsSubstring("Not running"));
    CHECK_THAT(s.drawn(), ContainsSubstring("Start it"));
    CHECK_THAT(s.drawn(), !ContainsSubstring("Sync Now"));
    s.press(ftxui::Event::Character('2'));
    CHECK_THAT(s.drawn(), ContainsSubstring("Work on libraries needs the server running"));
    s.press(ftxui::Event::Character('3'));
    s.press(ftxui::Event::ArrowDown);
    s.type("x");
    for (int i = 0; i < 8; ++i) {
        s.press(ftxui::Event::ArrowDown);
    }
    (void)s.drawn();
    s.press(ftxui::Event::Return);
    CHECK_THAT(s.drawn(), ContainsSubstring("starts with these"));
    CHECK_FALSE(SyncSettings::load(data).computer_name.empty());
}

TEST_CASE("The program: no terminal, no screen, and settings for a script", "[server][config]")
{
#ifndef SEVENTHSLIDE_APP_PROGRAM
    SKIP("The program is not built here");
#else
    seventhslide::test::QtBootstrap qt;
    seventhslide::test::TempDir dir;
    const auto data = QString::fromStdString((dir.path / "server").string());
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("no-such-platform"));
    const auto command = [&](QStringList args) {
        QProcess p;
        p.setProcessEnvironment(env);
        p.setProcessChannelMode(QProcess::MergedChannels);
        args.prepend(QStringLiteral("--server"));
        args << QStringLiteral("--data") << data;
        p.start(QStringLiteral(SEVENTHSLIDE_APP_PROGRAM), args);
        REQUIRE(p.waitForStarted());
        p.closeWriteChannel();
        REQUIRE(p.waitForFinished(30000));
        return std::pair{p.exitCode(), p.readAll().toStdString()};
    };
    const auto [no_terminal, said] = command({QStringLiteral("config")});
    CHECK(no_terminal == 2);
    CHECK_THAT(said, ContainsSubstring("needs a terminal"));

    CHECK(command({QStringLiteral("settings"), QStringLiteral("name"), QStringLiteral("Main server")}).first == 0);
    CHECK(command({QStringLiteral("settings"), QStringLiteral("port"), QStringLiteral("9123")}).first == 0);
    CHECK(command({QStringLiteral("settings"), QStringLiteral("discovery"), QStringLiteral("off")}).first == 0);
    CHECK(command({QStringLiteral("settings"), QStringLiteral("port"), QStringLiteral("nonsense")}).first == 2);
    CHECK(command({QStringLiteral("settings"), QStringLiteral("sync"), QStringLiteral("maybe")}).first == 2);
    CHECK(command({QStringLiteral("settings"), QStringLiteral("upload-cap"), QStringLiteral("2.5")}).first == 0);
    CHECK(command({QStringLiteral("settings"), QStringLiteral("upload-cap"), QStringLiteral("fast")}).first == 2);
    CHECK(command({QStringLiteral("settings"), QStringLiteral("sharing"), QStringLiteral("off")}).first == 0);
    const auto [read, shown] = command({QStringLiteral("settings")});
    CHECK(read == 0);
    CHECK_THAT(shown, ContainsSubstring("Main server"));
    CHECK_THAT(shown, ContainsSubstring("9123"));
    CHECK_THAT(shown, ContainsSubstring("2.5 Mbit/s"));
    CHECK_THAT(shown, ContainsSubstring("Share its address        off"));
    CHECK_THAT(shown, ContainsSubstring("not running"));
    const auto saved = SyncSettings::load(data.toStdString());
    CHECK(saved.computer_name == "Main server");
    CHECK(saved.port == 9123);
    CHECK_FALSE(saved.discovery);
    CHECK(saved.upload_cap_kbit == 2'500);
    CHECK_FALSE(saved.share_address);
    CHECK(command({QStringLiteral("settings"), QStringLiteral("port"), QStringLiteral("none")}).first == 0);
    CHECK_FALSE(SyncSettings::load(data.toStdString()).port.has_value());
#endif
}
