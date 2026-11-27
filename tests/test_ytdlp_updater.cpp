// The app's own yt-dlp: installed, checked and updated from a release, and
// only ever replaced by a build whose checksum matches. A file:// folder
// stands in for yt-dlp's release page, so nothing here goes online.

#include <catch2/catch_test_macros.hpp>

#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"
#include "tools_controller.hpp"
#include "ytdlp_updater.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QSettings>
#include <QSignalSpy>
#include <QTest>
#include <QUrl>

#include <seventhslide/media/ytdlp.hpp>
#include <seventhslide/platform/tools.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>

namespace fs = std::filesystem;
using seventhslide::app::ToolsController;
using seventhslide::app::YtdlpUpdater;
using seventhslide::platform::ToolSource;

namespace {

// The updater plays this computer's own kind: a copy is found by its file
// name (yt-dlp.exe on Windows), and on Linux and macOS by its execute bit,
// which a Windows file system has none of.
#if defined(_WIN32)
constexpr std::string_view k_platform = "windows";
#elif defined(__APPLE__)
constexpr std::string_view k_platform = "macos";
#else
constexpr std::string_view k_platform = "linux";
#endif

/// The release's build for this computer: yt-dlp_linux, yt-dlp.exe, ...
std::string release_asset()
{
    return seventhslide::media::ytdlp_release_asset(k_platform, "x86_64");
}

std::string sha256(const std::string& bytes)
{
    return QCryptographicHash::hash(QByteArray::fromStdString(bytes), QCryptographicHash::Sha256)
        .toHex()
        .toStdString();
}

std::string read(const fs::path& path)
{
    std::ifstream in{path, std::ios::binary};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

/// A release folder holding ``build`` as this computer's asset
/// (release_asset()), and a SHA2-256SUMS that lists ``listed`` for it (the
/// build's own digest by default) beside another build's.
void publish(const fs::path& release, const std::string& build, std::string listed = {})
{
    fs::create_directories(release);
    std::ofstream{release / release_asset(), std::ios::binary} << build;
    if (listed.empty()) {
        listed = sha256(build);
    }
    std::ofstream{release / "SHA2-256SUMS"} << sha256("other") << "  yt-dlp_other\n"
                                            << listed << "  " << release_asset() << "\n";
}

struct Fixture {
    seventhslide::test::TempDir tmp;
    ToolsController tools;
    YtdlpUpdater& updater;

    Fixture()
        : tools([this] {
            seventhslide::test::ensure_qt_application();
            seventhslide::platform::ToolSearch search;
            search.platform_id = std::string{k_platform};
            search.tools_dir = tmp.path / "tools";
            return search;
        }())
        , updater(*tools.ytdlp())
    {
        updater.set_platform(std::string{k_platform}, "x86_64");
        updater.set_release_base(QUrl::fromLocalFile(
            QString::fromStdString((tmp.path / "release").string()) + QStringLiteral("/")));
        QSettings().remove(QStringLiteral("tools/ytdlpChecked"));
        QSettings().remove(QStringLiteral("tools/ytdlpAutoUpdate"));
    }

    /// ``update`` and wait for its outcome.
    std::pair<bool, QString> update()
    {
        QSignalSpy finished(&updater, &YtdlpUpdater::finished);
        updater.update();
        REQUIRE(finished.wait(10000));
        return {finished[0][0].toBool(), finished[0][1].toString()};
    }

    /// Wait for the versions an install reads (it finds the tools again)
    /// to be read: until then, uninstall refuses.
    void settle()
    {
        REQUIRE(QTest::qWaitFor([this] { return !tools.jobs().busy(); }, 10000));
    }
};

const std::string k_build_a = "#!/bin/sh\necho 2026.10.01\n";
const std::string k_build_b = "#!/bin/sh\necho 2026.10.22\n";

} // namespace

TEST_CASE("yt-dlp is installed from its release, then found as the app's own", "[ytdlp][updater]")
{
    Fixture f;
    publish(f.tmp.path / "release", k_build_a);
    CHECK_FALSE(f.updater.installed());

    const auto [ok, message] = f.update();
    CHECK(ok);
    CHECK(message == QStringLiteral("Installed"));
    REQUIRE(f.updater.installed());
    CHECK(read(f.updater.managed_path()) == k_build_a);
    CHECK_FALSE(fs::exists(f.tmp.path / "tools" / ".yt-dlp.download"));
    // Found first, ahead of any other copy.
    const auto found = f.tools.location("yt-dlp");
    REQUIRE(found.has_value());
    CHECK(found->source == ToolSource::Managed);
    CHECK_FALSE(f.updater.busy());
}

TEST_CASE("A copy that matches the release is up to date; a newer release replaces it",
          "[ytdlp][updater]")
{
    Fixture f;
    publish(f.tmp.path / "release", k_build_a);
    REQUIRE(f.update().first);

    auto [ok, message] = f.update();
    CHECK(ok);
    CHECK(message == QStringLiteral("Up to date"));

    publish(f.tmp.path / "release", k_build_b);
    std::tie(ok, message) = f.update();
    CHECK(ok);
    CHECK(message == QStringLiteral("Updated"));
    CHECK(read(f.updater.managed_path()) == k_build_b);
}

TEST_CASE("A download that does not match the published checksum is never used",
          "[ytdlp][updater]")
{
    Fixture f;
    publish(f.tmp.path / "release", k_build_a);
    REQUIRE(f.update().first);

    // The release says one thing, and serves another.
    publish(f.tmp.path / "release", k_build_b, sha256("something else"));
    const auto [ok, message] = f.update();
    CHECK_FALSE(ok);
    CHECK(message.contains(QStringLiteral("checksum")));
    CHECK(read(f.updater.managed_path()) == k_build_a);
    CHECK_FALSE(fs::exists(f.tmp.path / "tools" / ".yt-dlp.download"));
}

TEST_CASE("A release with no build for this computer, or none at all, says so",
          "[ytdlp][updater]")
{
    Fixture f;
    auto [ok, message] = f.update();
    CHECK_FALSE(ok);
    CHECK(message.contains(QStringLiteral("Could not reach")));

    fs::create_directories(f.tmp.path / "release");
    std::ofstream{f.tmp.path / "release" / "SHA2-256SUMS"} << sha256("x") << "  yt-dlp_other\n";
    std::tie(ok, message) = f.update();
    CHECK_FALSE(ok);
    CHECK(message.contains(QString::fromStdString(release_asset())));

    f.updater.set_platform("plan9", "x86_64");
    std::tie(ok, message) = f.update();
    CHECK_FALSE(ok);
    CHECK_FALSE(f.updater.installed());
}

TEST_CASE("Before a download, yt-dlp is checked at most once a day", "[ytdlp][updater]")
{
    Fixture f;
    publish(f.tmp.path / "release", k_build_a);
    REQUIRE(f.update().first);
    publish(f.tmp.path / "release", k_build_b);

    // Checked just now: the download goes ahead with the copy it has.
    bool ran = false;
    f.updater.when_fresh([&ran] { ran = true; });
    CHECK_FALSE(ran);
    REQUIRE(QTest::qWaitFor([&ran] { return ran; }, 5000));
    CHECK(read(f.updater.managed_path()) == k_build_a);

    // A day on, it updates first.
    QSettings().setValue(QStringLiteral("tools/ytdlpChecked"),
                         QDateTime::currentDateTimeUtc().addDays(-2));
    ran = false;
    f.updater.when_fresh([&ran] { ran = true; });
    REQUIRE(QTest::qWaitFor([&ran] { return ran; }, 10000));
    CHECK(read(f.updater.managed_path()) == k_build_b);

    // Turned off, it never checks.
    f.updater.setAutoUpdate(false);
    CHECK_FALSE(f.updater.autoUpdate());
    QSettings().setValue(QStringLiteral("tools/ytdlpChecked"),
                         QDateTime::currentDateTimeUtc().addDays(-2));
    publish(f.tmp.path / "release", k_build_a);
    ran = false;
    f.updater.when_fresh([&ran] { ran = true; });
    REQUIRE(QTest::qWaitFor([&ran] { return ran; }, 5000));
    CHECK(read(f.updater.managed_path()) == k_build_b);
    f.updater.setAutoUpdate(true);
}

TEST_CASE("With no copy of its own, the app checks nothing before a download",
          "[ytdlp][updater]")
{
    Fixture f;
    publish(f.tmp.path / "release", k_build_a);
    bool ran = false;
    f.updater.when_fresh([&ran] { ran = true; });
    REQUIRE(QTest::qWaitFor([&ran] { return ran; }, 5000));
    CHECK_FALSE(f.updater.installed());
}

TEST_CASE("The app's own copy can be uninstalled, and installed again", "[ytdlp][updater]")
{
    Fixture f;
    // Nothing to remove is no refusal.
    CHECK(f.updater.uninstall().isEmpty());

    publish(f.tmp.path / "release", k_build_a);
    REQUIRE(f.update().first);
    REQUIRE(f.updater.installed());
    f.settle();

    QSignalSpy changed(&f.updater, &YtdlpUpdater::changed);
    CHECK(f.updater.uninstall().isEmpty());
    CHECK_FALSE(f.updater.installed());
    CHECK_FALSE(fs::exists(f.updater.managed_path()));
    CHECK(f.updater.status().isEmpty());
    CHECK_FALSE(f.updater.failed());
    CHECK(changed.count() >= 1);
    CHECK_FALSE(QSettings().contains(QStringLiteral("tools/ytdlpChecked")));
    // No longer found as the app's own.
    const auto found = f.tools.location("yt-dlp");
    CHECK((!found || found->source != ToolSource::Managed));

    const auto [ok, message] = f.update();
    CHECK(ok);
    CHECK(message == QStringLiteral("Installed"));
    CHECK(f.updater.installed());
}

TEST_CASE("The app's own copy is not uninstalled while anything runs it", "[ytdlp][updater]")
{
    Fixture f;
    publish(f.tmp.path / "release", k_build_a);
    REQUIRE(f.update().first);

    SECTION("a check")
    {
        QSignalSpy finished(&f.updater, &YtdlpUpdater::finished);
        f.updater.update();
        REQUIRE(f.updater.busy());
        const QString status = f.updater.status();
        CHECK_FALSE(f.updater.uninstall().isEmpty());
        // The check's progress stays what the page shows.
        CHECK(f.updater.status() == status);
        REQUIRE(finished.wait(10000));
    }
    // A job is listed until it reports, from the event loop: it is there
    // when uninstall asks, whether or not its program ran.
    SECTION("a download or a look-up")
    {
        bool reported = false;
        seventhslide::app::ToolJobSpec spec;
        spec.tool = std::string{seventhslide::platform::k_tool_ytdlp};
        spec.program = f.updater.managed_path();
        f.tools.jobs().start(std::move(spec), [&reported](const auto&) { reported = true; });
        const QString why = f.updater.uninstall();
        CHECK(why.contains(QStringLiteral("downloading")));
        CHECK(f.updater.status() == why);
        CHECK(f.updater.failed());
        REQUIRE(QTest::qWaitFor([&reported] { return reported; }, 10000));
    }
    SECTION("reading its version")
    {
        bool reported = false;
        seventhslide::app::ToolJobSpec spec;
        spec.tool = ToolsController::version_lane(seventhslide::platform::k_tool_ytdlp);
        spec.program = f.updater.managed_path();
        f.tools.jobs().start(std::move(spec), [&reported](const auto&) { reported = true; });
        const QString why = f.updater.uninstall();
        CHECK(why.contains(QStringLiteral("version")));
        CHECK(f.updater.failed());
        REQUIRE(QTest::qWaitFor([&reported] { return reported; }, 10000));
    }
    CHECK(f.updater.installed());

    // Once nothing runs it, it goes, and the refusal is forgotten.
    f.settle();
    CHECK(f.updater.uninstall().isEmpty());
    CHECK_FALSE(f.updater.installed());
    CHECK_FALSE(f.updater.failed());
    CHECK(f.updater.status().isEmpty());
}

TEST_CASE("The status says what is news beside whether yt-dlp is installed",
          "[ytdlp][updater]")
{
    Fixture f;
    publish(f.tmp.path / "release", k_build_a);

    // Installed: the page says so by itself.
    REQUIRE(f.update().first);
    CHECK(f.updater.status().isEmpty());
    CHECK_FALSE(f.updater.failed());

    REQUIRE(f.update().first);
    CHECK(f.updater.status() == QStringLiteral("Up to date"));
    CHECK_FALSE(f.updater.failed());

    fs::remove_all(f.tmp.path / "release");
    CHECK_FALSE(f.update().first);
    CHECK(f.updater.status().contains(QStringLiteral("Could not reach")));
    CHECK(f.updater.failed());

    // Forgotten as the page opens again.
    QSignalSpy changed(&f.updater, &YtdlpUpdater::changed);
    f.updater.clearStatus();
    CHECK(f.updater.status().isEmpty());
    CHECK_FALSE(f.updater.failed());
    CHECK(changed.count() == 1);
    f.updater.clearStatus();
    CHECK(changed.count() == 1);
}
