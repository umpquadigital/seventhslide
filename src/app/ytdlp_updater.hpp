#pragma once

// The app's own copy of yt-dlp: installed into the tools folder and kept up
// to date, so a site that changes does not wait for a new SeventhSlide.
//
// It is yt-dlp's official standalone build for this platform (no Python
// needed), from the latest release. A check fetches the release's
// SHA2-256SUMS and compares the digest listed for that build with the copy
// the app has; the same digest means up to date, and no other request is
// made. A new build is downloaded beside the old one, checked against that
// digest, and only then renamed over it, so a failed or tampered download
// never replaces a working copy.
//
// With ``autoUpdate`` (on by default, kept on this computer), ``when_fresh``
// checks at most once a day, before a download. A yt-dlp the system
// installed (pip, a package manager) is used when the app has none of its
// own, and never updated by the app.
//
// ``uninstall`` deletes the app's own copy; downloads then use a yt-dlp the
// system has, if any. It refuses while anything runs that copy, since
// Windows will not delete a program that is running.
//
// ToolsController owns one: `session.tools.ytdlp`.
//
// Seam: Qt Core + Qt Network. Call on the GUI thread.

#include <QtQml/qqmlregistration.h>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class QNetworkAccessManager;

namespace seventhslide::app {

class ToolsController;

class YtdlpUpdater : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The session keeps yt-dlp: session.tools.ytdlp")

    /// A check or a download is running.
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    /// What a check, install or update is doing now; or what the last check,
    /// update or uninstall came to, when that says more than ``installed``
    /// does (up to date, updated, why it failed). Empty otherwise.
    Q_PROPERTY(QString status READ status NOTIFY changed)
    /// The last check, install, update or uninstall failed; ``status`` says
    /// why.
    Q_PROPERTY(bool failed READ failed NOTIFY changed)
    /// The app's own copy is installed.
    Q_PROPERTY(bool installed READ installed NOTIFY changed)
    /// Check before a download, at most once a day. Kept on this computer.
    Q_PROPERTY(bool autoUpdate READ autoUpdate WRITE setAutoUpdate NOTIFY changed)

public:
    /// Where yt-dlp's latest release keeps its files.
    static constexpr const char* k_release_base =
        "https://github.com/yt-dlp/yt-dlp/releases/latest/download/";
    static constexpr const char* k_sums_name = "SHA2-256SUMS";
    /// How long a check holds before ``when_fresh`` checks again.
    static constexpr qint64 k_check_every_s = 24 * 60 * 60;

    YtdlpUpdater(ToolsController& tools, QObject* parent = nullptr);
    ~YtdlpUpdater() override;

    YtdlpUpdater(const YtdlpUpdater&) = delete;
    YtdlpUpdater& operator=(const YtdlpUpdater&) = delete;
    YtdlpUpdater(YtdlpUpdater&&) = delete;
    YtdlpUpdater& operator=(YtdlpUpdater&&) = delete;

    /// Fetch releases from ``base`` (a folder URL, ending in /) instead (tests).
    void set_release_base(const QUrl& base);
    /// The build for ``platform_id`` on ``arch`` instead of this computer's
    /// (tests).
    void set_platform(std::string platform_id, std::string arch);

    /// Install the app's own copy, or bring it up to date. ``finished``
    /// reports once, from the event loop.
    Q_INVOKABLE void update();

    /// Delete the app's own copy. Returns why it was not deleted (``status``
    /// says it too, unless a check is running), or empty once it is gone or
    /// was never there.
    Q_INVOKABLE QString uninstall();

    /// Forget what the last check, update or uninstall came to (Settings
    /// does, as its page opens). Nothing while a check runs.
    Q_INVOKABLE void clearStatus();

    /// Before a download: with ``autoUpdate`` on, the app's own copy
    /// installed and its last check a day or more ago, check (and update)
    /// first. ``then`` runs once, from the event loop, whatever the check
    /// came to.
    void when_fresh(std::function<void()> then);

    [[nodiscard]] bool busy() const noexcept { return reply_ != nullptr; }
    [[nodiscard]] QString status() const { return status_; }
    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] bool installed() const;
    [[nodiscard]] bool autoUpdate() const;
    void setAutoUpdate(bool on);

    /// Where the app's own copy is.
    [[nodiscard]] std::filesystem::path managed_path() const;

signals:
    void changed();
    void finished(bool ok, const QString& message);

private:
    void fetch_sums();
    void on_sums();
    void fetch_build(const QString& asset, const QString& digest);
    void on_build_data();
    void on_build_done(const QString& digest);
    /// The check is over: ``finished`` reports ``message``, ``status`` keeps
    /// it when it is ``news`` (more than whether yt-dlp is installed), and
    /// every waiter runs.
    void done(bool ok, const QString& message, bool news = true);
    /// ``uninstall`` refused: ``why`` is its ``status``.
    QString refuse_uninstall(const QString& why);
    [[nodiscard]] QUrl url_of(const QString& name) const;
    /// The platform set for tests, else the tools' own.
    [[nodiscard]] const std::string& platform_id() const;
    [[nodiscard]] QDateTime last_check() const;

    ToolsController& tools_;
    QNetworkAccessManager* network_ = nullptr;
    QUrl base_;
    /// Set for tests only; empty follows the tools' search.
    std::string platform_id_;
    std::string arch_;
    QPointer<QNetworkReply> reply_;
    /// The download in progress, and its digest so far.
    std::unique_ptr<QFile> part_;
    std::unique_ptr<QCryptographicHash> hash_;
    QString status_;
    bool failed_ = false;
    /// Runs once the check in progress is over.
    std::vector<std::function<void()>> waiting_;
};

} // namespace seventhslide::app
