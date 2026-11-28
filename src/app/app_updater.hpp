#pragma once

// SeventhSlide's own updates: the check for a newer release on GitHub, and
// the download and install of this build's package from it.
//
// The check reads the repository's list of releases from GitHub's API
// (``update::parse_releases``), once soon after the app starts and then
// once a day while it runs, unless ``autoCheck`` is off. A check that fails
// (offline, or GitHub limiting how often a network may ask) only changes
// ``status``: nothing ever opens on its own, so nothing interrupts a
// service. ``channel`` picks stable or beta releases.
//
// One thing runs at a time: a check, a download or an install. While one
// does (``busy``), another is not started and the channel does not change,
// so the release being downloaded or installed stays the one it was.
//
// A packaged build knows its own file in each release (``package_file()``,
// from SEVENTHSLIDE_PACKAGE_FILE). ``download`` fetches that file into the
// cache's ``updates/`` folder and keeps it only when its SHA-256 matches the
// digest GitHub reports for it; ``install`` checks it again first. Install
// then opens the Windows installer or the macOS disk image, and asks the
// window to close (``quitForInstall``) so it can be replaced, or installs a
// deb or an rpm through pkexec, which asks for an administrator's password;
// ``restart`` then opens the new version. A build that was not packaged
// offers the release page instead. What was downloaded goes when the app
// next starts.
//
// A build configured with SEVENTHSLIDE_UPDATE_CHECK off never checks
// (``checks`` is false): its updates come from whoever packaged it.
//
// SessionFacade owns one: `session.updates`. Creating it starts nothing;
// main calls ``start`` for the app.
//
// Seam: Qt Core + Qt Network + Qt Gui (QDesktopServices). Call on the GUI
// thread.

#include <seventhslide/update/releases.hpp>

#include <QtQml/qqmlregistration.h>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class QNetworkAccessManager;

namespace seventhslide::app {

class AppUpdater : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The session checks for updates: session.updates")

    // ``changed``: the settings and the newer release. ``stateChanged``: what
    // is under way and how far it is, which a download changes often, so the
    // release's notes are not laid out again for each step.

    /// The version running: ``1.3.0`` or ``1.3.0-beta.2``.
    Q_PROPERTY(QString version READ version CONSTANT)
    /// This build checks for updates at all.
    Q_PROPERTY(bool checks READ checks CONSTANT)
    /// Check soon after the app starts and once a day. Kept on this computer.
    Q_PROPERTY(bool autoCheck READ autoCheck WRITE setAutoCheck NOTIFY changed)
    /// ``stable`` or ``beta``. Kept on this computer; until it is set, a
    /// beta follows betas and a final version final versions. It does not
    /// change while ``busy``.
    Q_PROPERTY(QString channel READ channel WRITE setChannel NOTIFY changed)
    /// When a check last reached GitHub; invalid before the first.
    Q_PROPERTY(QDateTime lastChecked READ lastChecked NOTIFY changed)

    /// A release newer than this one is on ``channel``.
    Q_PROPERTY(bool available READ available NOTIFY changed)
    /// The newer release's version and its page.
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(QString releaseUrl READ releaseUrl NOTIFY changed)
    /// Its notes, in blocks of plain text: ``kind`` (heading, item,
    /// paragraph or code), ``text``, a list item's ``marker`` (a bullet or
    /// its number) and ``depth`` (how far it is nested).
    Q_PROPERTY(QVariantList notes READ notes NOTIFY changed)
    /// The newer release has this build's own file, with a digest to check
    /// it against, so the app can download and install it.
    Q_PROPERTY(bool canDownload READ canDownload NOTIFY changed)
    /// Installing opens an installer and closes SeventhSlide (Windows,
    /// macOS), rather than installing beside it (a deb or an rpm).
    Q_PROPERTY(bool installCloses READ installCloses CONSTANT)

    /// A check, a download or an install is under way.
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    /// What the last check, download or install came to, or what it is doing.
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    /// ``status`` reports a failure.
    Q_PROPERTY(bool failed READ failed NOTIFY stateChanged)
    /// The newer release's file is being downloaded.
    Q_PROPERTY(bool downloading READ downloading NOTIFY stateChanged)
    /// How much of the download has arrived, 0 to 1; 0 when the size is
    /// not known yet.
    Q_PROPERTY(double progress READ progress NOTIFY stateChanged)
    /// The newer release's file is downloaded, checked and ready to install.
    Q_PROPERTY(bool downloaded READ downloaded NOTIFY stateChanged)
    /// The newer release is installed as a deb or an rpm: ``restart`` runs it.
    Q_PROPERTY(bool installed READ installed NOTIFY stateChanged)

public:
    /// Where GitHub lists the releases.
    static constexpr const char* k_releases_url =
        "https://api.github.com/repos/umpquadigital/seventhslide/releases?per_page=30";
    /// The repository every link must point into.
    static update::Repository repository();

    explicit AppUpdater(QObject* parent = nullptr);
    ~AppUpdater() override;

    AppUpdater(const AppUpdater&) = delete;
    AppUpdater& operator=(const AppUpdater&) = delete;
    AppUpdater(AppUpdater&&) = delete;
    AppUpdater& operator=(AppUpdater&&) = delete;

    /// Remove what an earlier run downloaded; then check ``first`` after
    /// this call (if ``autoCheck`` is on), and again whenever a day has
    /// passed since the last check, looking every ``every``. The app's own
    /// calls use a minute and an hour.
    void start(std::chrono::milliseconds first = std::chrono::minutes(1),
               std::chrono::milliseconds every = std::chrono::hours(1));

    // ---- For tests ----------------------------------------------------
    /// Read the releases from ``url`` instead of GitHub.
    void set_releases_url(const QUrl& url);
    /// Download a release's files from ``base`` (a folder URL, ending in /)
    /// instead of their links.
    void set_download_base(const QUrl& base);
    /// Play another build: its version, its package's file, and whether it
    /// checks.
    void set_build(update::Version running, std::string package_file, bool checks = true);
    /// Keep downloads in ``dir`` instead of the cache's ``updates/``.
    void set_download_dir(std::filesystem::path dir);

    /// Check GitHub now; nothing while ``busy``. ``checked`` reports once,
    /// from the event loop.
    Q_INVOKABLE void checkNow();
    /// Download the newer release's file; nothing while ``busy``.
    /// ``downloadFinished`` reports once.
    Q_INVOKABLE void download();
    /// Stop the download in progress, and keep nothing of it.
    Q_INVOKABLE void cancelDownload();
    /// Check the downloaded file again, then install it; nothing while
    /// ``busy``. ``installFinished`` reports once.
    Q_INVOKABLE void install();
    /// After a deb or an rpm is installed: close, and open the new version
    /// once this one has gone.
    Q_INVOKABLE void restart();
    /// Forget what the last check came to, as the Settings page opens.
    /// Nothing while something is under way.
    Q_INVOKABLE void clearStatus();

    [[nodiscard]] QString version() const;
    [[nodiscard]] bool checks() const noexcept { return checks_; }
    [[nodiscard]] bool autoCheck() const;
    void setAutoCheck(bool on);
    [[nodiscard]] QString channel() const;
    void setChannel(const QString& channel);
    [[nodiscard]] QDateTime lastChecked() const;
    [[nodiscard]] bool available() const noexcept { return offer_.has_value(); }
    [[nodiscard]] QString latestVersion() const;
    [[nodiscard]] QString releaseUrl() const;
    [[nodiscard]] QVariantList notes() const { return notes_; }
    [[nodiscard]] bool canDownload() const;
    [[nodiscard]] bool installCloses() const noexcept;
    [[nodiscard]] bool busy() const noexcept { return reply_ != nullptr || installer_ != nullptr; }
    [[nodiscard]] QString status() const { return status_; }
    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] bool downloading() const noexcept { return reply_ != nullptr && target_.has_value(); }
    [[nodiscard]] double progress() const noexcept { return progress_; }
    [[nodiscard]] bool downloaded() const;
    [[nodiscard]] bool installed() const;

    /// The downloaded file, once ``downloaded``.
    [[nodiscard]] std::filesystem::path downloaded_path() const { return downloaded_path_; }

signals:
    void changed();
    void stateChanged();
    void checked(bool ok, const QString& message);
    void downloadFinished(bool ok, const QString& message);
    void installFinished(bool ok, const QString& message);
    /// The window closes as it does when the user closes it: the installer
    /// replaces the app only once it has quit, or the new version opens
    /// once it has (``restart``).
    void quitForInstall();

private:
    /// A release's file, as it was when its download began.
    struct Target {
        update::Version version;
        update::ReleaseFile file;
    };

    void on_timer();
    void fetch_releases();
    void on_releases(QNetworkReply* reply);
    void on_download_data(QNetworkReply* reply);
    /// Hash and write one piece of the download; false once it is not the
    /// file (more than its size) or cannot be written.
    bool write_chunk(const QByteArray& chunk);
    void on_download_done(QNetworkReply* reply);
    void on_installer_done(int exit_code, QProcess::ExitStatus how);
    void on_installer_error(QProcess::ProcessError error);
    /// The offer from the releases last read, for ``channel`` now, and its
    /// notes laid out.
    void reconsider();
    [[nodiscard]] update::Channel channel_value() const;
    /// This build's file in the offered release, or nullptr.
    [[nodiscard]] const update::ReleaseFile* own_file() const;
    [[nodiscard]] std::filesystem::path download_dir() const;
    /// Remove every file in ``download_dir``.
    void clear_download_dir() const;
    /// ``status`` becomes ``message``; true when it is not a failure.
    void report(bool ok, const QString& message);
    /// ``report``, then ``installFinished``.
    void finish_install(bool ok, const QString& message);
    QNetworkAccessManager& network();
    /// The part-written download, removed.
    void discard_part();

    update::Version running_;
    std::string package_file_;
    bool checks_ = false;
    QUrl releases_url_;
    QUrl download_base_;
    std::filesystem::path download_dir_;
    /// This program, as it was started: a deb or an rpm replaces the file.
    QString program_;

    QNetworkAccessManager* network_ = nullptr;
    QTimer timer_;
    /// The check or the download under way.
    QPointer<QNetworkReply> reply_;
    /// What the download under way is of.
    std::optional<Target> target_;
    bool cancelled_ = false;
    bool write_failed_ = false;
    std::unique_ptr<QFile> part_;
    std::unique_ptr<QCryptographicHash> hash_;
    QPointer<QProcess> installer_;

    /// The releases the last check read.
    std::vector<update::Release> releases_;
    /// A newer release on ``channel``, and its notes laid out.
    std::optional<update::Release> offer_;
    QVariantList notes_;
    /// The file downloaded and checked, and what it is.
    std::filesystem::path downloaded_path_;
    std::optional<Target> downloaded_;
    /// The version a deb or an rpm installed.
    std::optional<update::Version> installed_version_;
    double progress_ = 0.0;
    /// The whole percent ``status`` last said.
    int percent_ = -1;
    QString status_;
    bool failed_ = false;
};

} // namespace seventhslide::app
