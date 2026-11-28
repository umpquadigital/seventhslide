// SeventhSlide's own updates: the check against GitHub's list of releases,
// the channels, and the download of this build's file, kept only when it
// matches the digest GitHub reports and checked again before it is
// installed. A saved reply (fixtures/updates/releases.json), a file://
// folder and a small HTTP server on this computer stand in for GitHub, so
// nothing here goes online, and nothing is installed.

#include <catch2/catch_test_macros.hpp>

#include "app_updater.hpp"
#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QHostAddress>
#include <QSettings>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace fs = std::filesystem;
using seventhslide::app::AppUpdater;
using seventhslide::update::parse_version;

namespace {

const std::string k_deb = "seventhslide-ubuntu-24.04-amd64.deb";

std::string read(const fs::path& path)
{
    std::ifstream in{path, std::ios::binary};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

QUrl folder_url(const fs::path& dir)
{
    return QUrl::fromLocalFile(QString::fromStdString(dir.string()) + QStringLiteral("/"));
}

struct Fixture {
    seventhslide::test::QtBootstrap qt;
    seventhslide::test::TempDir tmp;
    AppUpdater updater;

    explicit Fixture(std::string_view running = "1.3.0", std::string package = k_deb)
    {
        for (const char* key : {"updates/autoCheck", "updates/channel", "updates/checked"}) {
            QSettings().remove(QString::fromLatin1(key));
        }
        updater.set_build(*parse_version(running), std::move(package));
        updater.set_releases_url(QUrl::fromLocalFile(
            QString::fromStdString((fs::path(SEVENTHSLIDE_FIXTURES_DIR) / "updates"
                                    / "releases.json")
                                       .string())));
        updater.set_download_base(folder_url(tmp.path / "release"));
        updater.set_download_dir(tmp.path / "updates");
    }

    /// ``checkNow`` and wait for its outcome.
    std::pair<bool, QString> check()
    {
        QSignalSpy checked(&updater, &AppUpdater::checked);
        updater.checkNow();
        REQUIRE(checked.wait(10000));
        return {checked[0][0].toBool(), checked[0][1].toString()};
    }

    /// ``download`` and wait for its outcome.
    std::pair<bool, QString> download()
    {
        QSignalSpy finished(&updater, &AppUpdater::downloadFinished);
        updater.download();
        REQUIRE(finished.wait(10000));
        return {finished[0][0].toBool(), finished[0][1].toString()};
    }

    /// The release folder serves ``content`` as ``name``. The fixture's
    /// digest for a file is that of its tag and name, run together.
    void publish(const std::string& name, const std::string& content)
    {
        fs::create_directories(tmp.path / "release");
        std::ofstream{tmp.path / "release" / name, std::ios::binary} << content;
    }
};

std::string sha256(const std::string& bytes)
{
    return QCryptographicHash::hash(QByteArray::fromStdString(bytes), QCryptographicHash::Sha256)
        .toHex()
        .toStdString();
}

/// A list of releases holding one, v9.0.0, whose deb is ``size`` bytes
/// long and has ``content``'s digest.
std::string one_release(const std::string& content, long long size)
{
    const std::string base = "https://github.com/umpquadigital/seventhslide/releases";
    return R"([{"tag_name": "v9.0.0", "html_url": ")" + base + R"(/tag/v9.0.0", "assets": [{"name": ")"
           + k_deb + R"(", "size": )" + std::to_string(size) + R"(, "digest": "sha256:)"
           + sha256(content) + R"(", "browser_download_url": ")" + base + "/download/v9.0.0/" + k_deb
           + R"("}]}])";
}

/// An HTTP server on this computer that answers every request with
/// ``response``, and keeps the last request it read.
class HttpStub {
public:
    explicit HttpStub(QByteArray response)
        : response_(std::move(response))
    {
        REQUIRE(server_.listen(QHostAddress::LocalHost));
        QObject::connect(&server_, &QTcpServer::newConnection, &server_, [this] {
            QTcpSocket* socket = server_.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                request += socket->readAll();
                if (request.contains("\r\n\r\n")) {
                    socket->write(response_);
                    socket->disconnectFromHost();
                }
            });
        });
    }

    [[nodiscard]] QUrl url() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/repos/releases").arg(server_.serverPort()));
    }

    QByteArray request;

private:
    QTcpServer server_;
    QByteArray response_;
};

} // namespace

TEST_CASE("A check finds the newer release, its notes and its page", "[app-updater]")
{
    Fixture f;
    CHECK_FALSE(f.updater.available());
    CHECK_FALSE(f.updater.lastChecked().isValid());

    const auto [ok, message] = f.check();
    CHECK(ok);
    CHECK(message == QStringLiteral("SeventhSlide 1.3.1 is available."));
    CHECK(f.updater.status() == message);
    CHECK_FALSE(f.updater.failed());
    CHECK_FALSE(f.updater.busy());
    CHECK(f.updater.lastChecked().isValid());

    CHECK(f.updater.available());
    CHECK(f.updater.version() == QStringLiteral("1.3.0"));
    CHECK(f.updater.latestVersion() == QStringLiteral("1.3.1"));
    CHECK(f.updater.releaseUrl()
          == QStringLiteral("https://github.com/umpquadigital/seventhslide/releases/tag/v1.3.1"));
    const QVariantList notes = f.updater.notes();
    REQUIRE(notes.size() == 5);
    const QVariantMap first = notes[0].toMap();
    CHECK(first.value(QStringLiteral("kind")).toString() == QStringLiteral("heading"));
    CHECK(first.value(QStringLiteral("text")).toString() == QStringLiteral("What's Changed"));
    const QVariantMap fix = notes[2].toMap();
    CHECK(fix.value(QStringLiteral("kind")).toString() == QStringLiteral("item"));
    CHECK(fix.value(QStringLiteral("marker")).toString() == QStringLiteral("\u2022"));
    CHECK(fix.value(QStringLiteral("depth")).toInt() == 0);
    CHECK(fix.value(QStringLiteral("text")).toString().endsWith(QStringLiteral("in #41")));
    CHECK(notes[4].toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("paragraph"));
    CHECK(f.updater.canDownload());
    CHECK_FALSE(f.updater.downloaded());
}

TEST_CASE("The newest version is up to date, and betas come only on the beta channel",
          "[app-updater]")
{
    Fixture f("1.3.1");
    CHECK(f.updater.channel() == QStringLiteral("stable"));
    auto [ok, message] = f.check();
    CHECK(ok);
    CHECK(message == QStringLiteral("SeventhSlide is up to date."));
    CHECK_FALSE(f.updater.available());

    // Changing the channel looks again at the releases already read.
    f.updater.setChannel(QStringLiteral("beta"));
    CHECK(f.updater.channel() == QStringLiteral("beta"));
    CHECK(f.updater.available());
    CHECK(f.updater.latestVersion() == QStringLiteral("1.4.0-beta.1"));
    f.updater.setChannel(QStringLiteral("nightly"));
    CHECK(f.updater.channel() == QStringLiteral("beta"));
    f.updater.setChannel(QStringLiteral("stable"));
    CHECK_FALSE(f.updater.available());
}

TEST_CASE("A beta follows the beta channel until another is chosen", "[app-updater]")
{
    Fixture f("1.4.0-beta.1");
    CHECK(f.updater.channel() == QStringLiteral("beta"));
    REQUIRE(f.check().first);
    CHECK_FALSE(f.updater.available());

    f.updater.setChannel(QStringLiteral("stable"));
    CHECK(QSettings().value(QStringLiteral("updates/channel")).toString()
          == QStringLiteral("stable"));
    CHECK_FALSE(f.updater.available());
}

TEST_CASE("A check that fails says so and keeps what was known", "[app-updater]")
{
    Fixture f;
    REQUIRE(f.check().first);

    f.updater.set_releases_url(QUrl::fromLocalFile(
        QString::fromStdString((f.tmp.path / "missing.json").string())));
    auto [ok, message] = f.check();
    CHECK_FALSE(ok);
    CHECK(message.startsWith(QStringLiteral("Could not reach GitHub")));
    CHECK(f.updater.failed());
    CHECK(f.updater.available());

    {
        std::ofstream{f.tmp.path / "limit.json"} << R"({"message": "API rate limit exceeded"})";
    }
    f.updater.set_releases_url(QUrl::fromLocalFile(
        QString::fromStdString((f.tmp.path / "limit.json").string())));
    std::tie(ok, message) = f.check();
    CHECK_FALSE(ok);
    CHECK(message.contains(QStringLiteral("could not be read")));

    f.updater.clearStatus();
    CHECK(f.updater.status().isEmpty());
    CHECK_FALSE(f.updater.failed());
}

TEST_CASE("A build that does not check never asks", "[app-updater]")
{
    Fixture f;
    f.updater.set_build(*parse_version("1.3.0"), k_deb, false);
    CHECK_FALSE(f.updater.checks());
    QSignalSpy checked(&f.updater, &AppUpdater::checked);
    f.updater.checkNow();
    f.updater.start(std::chrono::milliseconds(0));
    CHECK_FALSE(f.updater.busy());
    CHECK(f.updater.status().isEmpty());
    seventhslide::test::flush_deferred_deletes();
    QCoreApplication::processEvents();
    CHECK(checked.isEmpty());
}

TEST_CASE("Starting checks soon after, whenever the last check was", "[app-updater]")
{
    Fixture f;
    QSettings().setValue(QStringLiteral("updates/checked"), QDateTime::currentDateTimeUtc());
    QSignalSpy checked(&f.updater, &AppUpdater::checked);
    f.updater.start(std::chrono::milliseconds(0));
    REQUIRE(checked.wait(10000));
    CHECK(checked[0][0].toBool());
    CHECK(f.updater.available());
}

TEST_CASE("This build's file is downloaded and kept once it matches its digest", "[app-updater]")
{
    Fixture f;
    REQUIRE(f.check().first);
    f.publish(k_deb, "v1.3.1" + k_deb);

    const auto [ok, message] = f.download();
    CHECK(ok);
    CHECK(message == QStringLiteral("SeventhSlide 1.3.1 is downloaded and ready to install."));
    REQUIRE(f.updater.downloaded());
    CHECK(f.updater.progress() == 1.0);
    CHECK(f.updater.downloaded_path() == f.tmp.path / "updates" / k_deb);
    CHECK(read(f.updater.downloaded_path()) == "v1.3.1" + k_deb);
    CHECK_FALSE(fs::exists(f.tmp.path / "updates" / (k_deb + ".part")));

    // A release on another channel is another download.
    f.updater.setChannel(QStringLiteral("beta"));
    CHECK_FALSE(f.updater.downloaded());
    f.updater.setChannel(QStringLiteral("stable"));
    CHECK(f.updater.downloaded());
}

TEST_CASE("A download that does not match its digest is not kept", "[app-updater]")
{
    Fixture f;
    REQUIRE(f.check().first);
    // An earlier download's leftovers go as the next one starts.
    fs::create_directories(f.tmp.path / "updates");
    std::ofstream{f.tmp.path / "updates" / "old.deb"} << "old";

    f.publish(k_deb, "something else");
    const auto [ok, message] = f.download();
    CHECK_FALSE(ok);
    CHECK(message.contains(QStringLiteral("checksum")));
    CHECK(f.updater.failed());
    CHECK_FALSE(f.updater.downloaded());
    CHECK(fs::is_empty(f.tmp.path / "updates"));
}

TEST_CASE("A download that cannot be fetched says so", "[app-updater]")
{
    Fixture f;
    REQUIRE(f.check().first);
    const auto [ok, message] = f.download();
    CHECK_FALSE(ok);
    CHECK(message.startsWith(QStringLiteral("The download stopped")));
    CHECK(fs::is_empty(f.tmp.path / "updates"));
}

TEST_CASE("A build that was not packaged offers the release page, not a download",
          "[app-updater]")
{
    Fixture f("1.3.0", "");
    REQUIRE(f.check().first);
    CHECK(f.updater.available());
    CHECK_FALSE(f.updater.canDownload());
    QSignalSpy finished(&f.updater, &AppUpdater::downloadFinished);
    f.updater.download();
    CHECK_FALSE(f.updater.busy());
    CHECK(finished.isEmpty());

    // Nor does a package the release does not have.
    f.updater.set_build(*parse_version("1.3.0"), "seventhslide-linux-arm64.deb");
    CHECK_FALSE(f.updater.canDownload());
}

TEST_CASE("A check names SeventhSlide to GitHub, and says when GitHub holds it back",
          "[app-updater]")
{
    Fixture f;
    for (const char* status : {"403 Forbidden", "429 Too Many Requests"}) {
        INFO(status);
        HttpStub github(QByteArray("HTTP/1.1 ") + status
                        + "\r\nContent-Type: application/json\r\nContent-Length: 2\r\n"
                          "Connection: close\r\n\r\n{}");
        f.updater.set_releases_url(github.url());
        const auto [ok, message] = f.check();
        CHECK_FALSE(ok);
        CHECK(message.contains(QStringLiteral("try again later")));
        const QString request = QString::fromLatin1(github.request);
        CHECK(request.contains(QStringLiteral("User-Agent: SeventhSlide/"), Qt::CaseInsensitive));
        CHECK(request.contains(QStringLiteral("Accept: application/vnd.github+json"),
                               Qt::CaseInsensitive));
    }
}

TEST_CASE("The first check waits while a download runs, and the download finishes",
          "[app-updater]")
{
    Fixture f;
    REQUIRE(f.check().first);
    f.publish(k_deb, "v1.3.1" + k_deb);
    QSignalSpy finished(&f.updater, &AppUpdater::downloadFinished);
    f.updater.download();
    REQUIRE(f.updater.downloading());
    // The first check comes due while the download runs: it is not started
    // over it.
    f.updater.start(std::chrono::milliseconds(0));
    REQUIRE(finished.wait(10000));
    CHECK(finished[0][0].toBool());
    CHECK(f.updater.downloaded());
}

TEST_CASE("The channel holds while a download runs", "[app-updater]")
{
    Fixture f;
    REQUIRE(f.check().first);
    f.publish(k_deb, "v1.3.1" + k_deb);
    QSignalSpy finished(&f.updater, &AppUpdater::downloadFinished);
    f.updater.download();
    REQUIRE(f.updater.busy());
    f.updater.setChannel(QStringLiteral("beta"));
    CHECK(f.updater.channel() == QStringLiteral("stable"));
    REQUIRE(finished.wait(10000));
    CHECK(finished[0][0].toBool());
    CHECK(f.updater.latestVersion() == QStringLiteral("1.3.1"));
    CHECK(f.updater.downloaded());
}

TEST_CASE("A download's progress leaves the release and its notes alone", "[app-updater]")
{
    Fixture f;
    REQUIRE(f.check().first);
    f.publish(k_deb, "v1.3.1" + k_deb);
    QSignalSpy changed(&f.updater, &AppUpdater::changed);
    QSignalSpy state(&f.updater, &AppUpdater::stateChanged);
    REQUIRE(f.download().first);
    CHECK(changed.isEmpty());
    CHECK_FALSE(state.isEmpty());
}

TEST_CASE("A cancelled download keeps nothing", "[app-updater]")
{
    Fixture f;
    REQUIRE(f.check().first);
    f.publish(k_deb, "v1.3.1" + k_deb);
    QSignalSpy finished(&f.updater, &AppUpdater::downloadFinished);
    f.updater.download();
    f.updater.cancelDownload();
    REQUIRE((!finished.isEmpty() || finished.wait(10000)));
    CHECK_FALSE(finished[0][0].toBool());
    CHECK(finished[0][1].toString() == QStringLiteral("The download was cancelled."));
    CHECK_FALSE(f.updater.downloaded());
    CHECK_FALSE(f.updater.busy());
    CHECK(fs::is_empty(f.tmp.path / "updates"));
}

TEST_CASE("A download longer than GitHub says the file is, is not kept", "[app-updater]")
{
    Fixture f("8.0.0");
    const std::string content = "the real file";
    {
        std::ofstream{f.tmp.path / "one.json"} << one_release(content, 5);
    }
    f.updater.set_releases_url(
        QUrl::fromLocalFile(QString::fromStdString((f.tmp.path / "one.json").string())));
    REQUIRE(f.check().first);
    REQUIRE(f.updater.canDownload());
    f.publish(k_deb, content);
    const auto [ok, message] = f.download();
    CHECK_FALSE(ok);
    CHECK(message.contains(QStringLiteral("checksum")));
    CHECK(fs::is_empty(f.tmp.path / "updates"));
}

TEST_CASE("A downloaded file is checked again before it is installed", "[app-updater]")
{
    Fixture f;
    REQUIRE(f.check().first);
    f.publish(k_deb, "v1.3.1" + k_deb);
    REQUIRE(f.download().first);
    // Changed where it waits.
    {
        std::ofstream{f.updater.downloaded_path(), std::ios::binary} << "something else";
    }
    QSignalSpy finished(&f.updater, &AppUpdater::installFinished);
    f.updater.install();
    REQUIRE(finished.size() == 1);
    CHECK_FALSE(finished[0][0].toBool());
    CHECK(finished[0][1].toString().contains(QStringLiteral("changed since it was checked")));
    CHECK_FALSE(f.updater.downloaded());
    CHECK_FALSE(f.updater.busy());
    CHECK(fs::is_empty(f.tmp.path / "updates"));
}

TEST_CASE("Starting removes what an earlier run downloaded", "[app-updater]")
{
    Fixture f;
    QSettings().setValue(QStringLiteral("updates/autoCheck"), false);
    fs::create_directories(f.tmp.path / "updates");
    {
        std::ofstream{f.tmp.path / "updates" / k_deb} << "old";
        std::ofstream{f.tmp.path / "updates" / (k_deb + ".part")} << "half";
    }
    f.updater.start(std::chrono::hours(1));
    CHECK(fs::is_empty(f.tmp.path / "updates"));
}
