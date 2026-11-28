// SeventhSlide's own updates; see app_updater.hpp.

#include "app_updater.hpp"

#include <seventhslide/platform/paths.hpp>
#include <seventhslide/version.hpp>

#include <QDesktopServices>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QSettings>
#include <QStringList>

#include <system_error>
#include <utility>

namespace seventhslide::app {

namespace {

const QString k_auto_key = QStringLiteral("updates/autoCheck");
const QString k_channel_key = QStringLiteral("updates/channel");
const QString k_skipped_key = QStringLiteral("updates/skipped");
const QString k_checked_key = QStringLiteral("updates/checked");

const QString k_stable = QStringLiteral("stable");
const QString k_beta = QStringLiteral("beta");

/// How long a check holds before the timer checks again.
constexpr qint64 k_check_every_s = 24 * 60 * 60;

QString from_std(const std::string& s)
{
    return QString::fromStdString(s);
}

QString path_text(const std::filesystem::path& path)
{
    return QString::fromStdU16String(path.u16string());
}

/// The request every call to GitHub makes: GitHub refuses one without a
/// User-Agent.
QNetworkRequest github_request(const QUrl& url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("SeventhSlide/%1").arg(QString::fromUtf8(version_string())));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}

} // namespace

update::Repository AppUpdater::repository()
{
    return {"umpquadigital", "seventhslide"};
}

AppUpdater::AppUpdater(QObject* parent)
    : QObject(parent)
    , package_file_(package_file())
    , checks_(update_check_enabled())
    , releases_url_(QString::fromLatin1(k_releases_url))
{
    // version_string() is the project's own version, which CMake checks is
    // one; a build that broke that would offer every release.
    running_ = update::parse_version(version_string()).value_or(update::Version{});
    connect(&timer_, &QTimer::timeout, this, &AppUpdater::on_timer);
}

AppUpdater::~AppUpdater()
{
    if (reply_) {
        reply_->disconnect(this);
        reply_->abort();
    }
    discard_part();
    // apt-get or dnf stopped halfway leaves the package system to repair:
    // quitting waits for it to finish instead.
    if (installer_ && installer_->state() != QProcess::NotRunning) {
        installer_->disconnect(this);
        installer_->waitForFinished(-1);
    }
}

void AppUpdater::start(std::chrono::milliseconds first, std::chrono::milliseconds every)
{
    if (!checks_) {
        return;
    }
    // The first check is not held to a day: a day-old reply is all the app
    // would otherwise know of, and it starts knowing nothing.
    QTimer::singleShot(first, this, [this, every] {
        if (autoCheck()) {
            fetch_releases();
        }
        timer_.start(every);
    });
}

void AppUpdater::set_releases_url(const QUrl& url)
{
    releases_url_ = url;
}

void AppUpdater::set_download_base(const QUrl& base)
{
    download_base_ = base;
}

void AppUpdater::set_build(update::Version running, std::string package_file, bool checks)
{
    running_ = std::move(running);
    package_file_ = std::move(package_file);
    checks_ = checks;
    reconsider();
    emit changed();
}

void AppUpdater::set_download_dir(std::filesystem::path dir)
{
    download_dir_ = std::move(dir);
}

std::filesystem::path AppUpdater::download_dir() const
{
    return download_dir_.empty() ? platform::Paths{}.data_dir() / "updates" : download_dir_;
}

QNetworkAccessManager& AppUpdater::network()
{
    if (network_ == nullptr) {
        network_ = new QNetworkAccessManager(this);
    }
    return *network_;
}

// ---- Settings -------------------------------------------------------------

QString AppUpdater::version() const
{
    return from_std(running_.text());
}

bool AppUpdater::autoCheck() const
{
    return QSettings().value(k_auto_key, true).toBool();
}

void AppUpdater::setAutoCheck(bool on)
{
    if (on == autoCheck()) {
        return;
    }
    QSettings().setValue(k_auto_key, on);
    emit changed();
}

update::Channel AppUpdater::channel_value() const
{
    const QString stored = QSettings().value(k_channel_key).toString();
    if (stored == k_beta) {
        return update::Channel::beta;
    }
    if (stored == k_stable) {
        return update::Channel::stable;
    }
    return update::default_channel(running_);
}

QString AppUpdater::channel() const
{
    return channel_value() == update::Channel::beta ? k_beta : k_stable;
}

void AppUpdater::setChannel(const QString& channel)
{
    if ((channel != k_stable && channel != k_beta) || channel == this->channel()) {
        return;
    }
    QSettings().setValue(k_channel_key, channel);
    reconsider();
    emit changed();
}

QDateTime AppUpdater::lastChecked() const
{
    return QSettings().value(k_checked_key).toDateTime();
}

std::optional<update::Version> AppUpdater::skipped_version() const
{
    return update::parse_version(QSettings().value(k_skipped_key).toString().toStdString());
}

// ---- The newer release ------------------------------------------------------

void AppUpdater::reconsider()
{
    const update::Release* offer =
        update::release_to_offer(releases_, channel_value(), running_, std::nullopt);
    offer_ = offer != nullptr ? std::optional<update::Release>(*offer) : std::nullopt;
}

bool AppUpdater::skipped() const
{
    return offer_ && skipped_version() == offer_->version;
}

QString AppUpdater::latestVersion() const
{
    return offer_ ? from_std(offer_->version.text()) : QString{};
}

QString AppUpdater::notes() const
{
    return offer_ ? from_std(update::notes_text(offer_->notes)) : QString{};
}

QString AppUpdater::releaseUrl() const
{
    return offer_ ? from_std(offer_->page_url) : QString{};
}

const update::ReleaseFile* AppUpdater::own_file() const
{
    if (!offer_ || package_file_.empty()) {
        return nullptr;
    }
    const update::ReleaseFile* file = offer_->file(package_file_);
    return file != nullptr && !file->sha256.empty() ? file : nullptr;
}

bool AppUpdater::canDownload() const
{
    return own_file() != nullptr && update::installer_for(package_file_) != update::Installer::none;
}

bool AppUpdater::downloaded() const
{
    std::error_code ec;
    return offer_ && downloaded_version_ == offer_->version
           && std::filesystem::is_regular_file(downloaded_path_, ec);
}

void AppUpdater::skipVersion()
{
    if (!offer_ || skipped()) {
        return;
    }
    QSettings().setValue(k_skipped_key, from_std(offer_->version.text()));
    emit changed();
}

void AppUpdater::report(bool ok, const QString& message)
{
    status_ = message;
    failed_ = !ok;
    emit changed();
}

void AppUpdater::clearStatus()
{
    if (busy() || (status_.isEmpty() && !failed_)) {
        return;
    }
    status_.clear();
    failed_ = false;
    emit changed();
}

// ---- The check --------------------------------------------------------------

void AppUpdater::checkNow()
{
    if (!checks_ || busy()) {
        return;
    }
    fetch_releases();
}

void AppUpdater::on_timer()
{
    const QDateTime last = lastChecked();
    const bool due = !last.isValid()
                     || last.secsTo(QDateTime::currentDateTimeUtc()) >= k_check_every_s;
    if (autoCheck() && due && !busy()) {
        fetch_releases();
    }
}

void AppUpdater::fetch_releases()
{
    QNetworkRequest request = github_request(releases_url_);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setTransferTimeout(30'000);
    downloading_ = false;
    reply_ = network().get(request);
    connect(reply_, &QNetworkReply::finished, this, &AppUpdater::on_releases);
    report(true, tr("Checking for updates…"));
}

void AppUpdater::on_releases()
{
    QNetworkReply* reply = reply_;
    reply_ = nullptr;
    reply->deleteLater();
    const auto fail = [this](const QString& message) {
        report(false, message);
        emit checked(false, message);
    };
    if (reply->error() != QNetworkReply::NoError) {
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // GitHub answers a network that asks too often (60 times an hour,
        // without signing in) with 403 or 429.
        if (code == 403 || code == 429) {
            fail(tr("GitHub is not answering checks from this network for now. SeventhSlide "
                    "will try again later."));
        } else {
            fail(tr("Could not reach GitHub: %1").arg(reply->errorString()));
        }
        return;
    }
    const QByteArray body = reply->readAll();
    auto releases = update::parse_releases(
        std::string_view{body.constData(), static_cast<std::size_t>(body.size())}, repository());
    if (!releases) {
        fail(tr("GitHub’s list of releases could not be read."));
        return;
    }
    releases_ = std::move(*releases);
    QSettings().setValue(k_checked_key, QDateTime::currentDateTimeUtc());
    reconsider();
    const QString message = offer_ ? tr("SeventhSlide %1 is available.").arg(latestVersion())
                                   : tr("SeventhSlide is up to date.");
    report(true, message);
    emit checked(true, message);
}

// ---- The download -----------------------------------------------------------

void AppUpdater::download()
{
    const update::ReleaseFile* file = own_file();
    if (busy() || file == nullptr || downloaded()) {
        return;
    }
    const std::filesystem::path dir = download_dir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    // The folder is the updater's own: a file an earlier download left,
    // whole or not, goes.
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        std::error_code rm;
        if (entry.is_regular_file(rm)) {
            std::filesystem::remove(entry.path(), rm);
        }
    }
    downloaded_version_.reset();
    downloaded_path_.clear();
    part_ = std::make_unique<QFile>(path_text(dir / (file->name + ".part")));
    if (!part_->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        part_.reset();
        const QString message = tr("Could not write to %1").arg(path_text(dir));
        report(false, message);
        emit downloadFinished(false, message);
        return;
    }
    hash_ = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    cancelled_ = false;
    write_failed_ = false;
    progress_ = 0.0;
    installed_ = false;
    const QUrl url = download_base_.isEmpty() ? QUrl(from_std(file->url))
                                              : download_base_.resolved(QUrl(from_std(file->name)));
    QNetworkRequest request = github_request(url);
    // Long enough for a slow network, as long as something keeps arriving.
    request.setTransferTimeout(60'000);
    downloading_ = true;
    reply_ = network().get(request);
    connect(reply_, &QNetworkReply::readyRead, this, &AppUpdater::on_download_data);
    connect(reply_, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        progress_ = total > 0 ? static_cast<double>(got) / static_cast<double>(total) : 0.0;
        report(true, tr("Downloading SeventhSlide %1… %2%")
                         .arg(latestVersion())
                         .arg(static_cast<int>(progress_ * 100)));
    });
    connect(reply_, &QNetworkReply::finished, this, &AppUpdater::on_download_done);
    report(true, tr("Downloading SeventhSlide %1…").arg(latestVersion()));
}

void AppUpdater::cancelDownload()
{
    if (reply_ && downloading_) {
        cancelled_ = true;
        reply_->abort();
    }
}

void AppUpdater::on_download_data()
{
    if (reply_ && !write_chunk(reply_->readAll())) {
        reply_->abort();
    }
}

bool AppUpdater::write_chunk(const QByteArray& chunk)
{
    if (!part_ || write_failed_) {
        return false;
    }
    hash_->addData(chunk);
    const update::ReleaseFile* file = own_file();
    // More than GitHub said the file holds is not the file.
    if (part_->write(chunk) != chunk.size()
        || (file != nullptr && file->size > 0 && part_->size() > file->size)) {
        write_failed_ = true;
        return false;
    }
    return true;
}

void AppUpdater::discard_part()
{
    if (part_) {
        part_->remove();
        part_.reset();
    }
}

void AppUpdater::on_download_done()
{
    QNetworkReply* reply = reply_;
    reply_ = nullptr;
    downloading_ = false;
    reply->deleteLater();
    // What arrived after the last readyRead.
    if (reply->bytesAvailable() > 0) {
        write_chunk(reply->readAll());
    }
    const auto fail = [this](const QString& message) {
        discard_part();
        hash_.reset();
        progress_ = 0.0;
        report(false, message);
        emit downloadFinished(false, message);
    };
    const update::ReleaseFile* file = own_file();
    if (cancelled_) {
        fail(tr("The download was cancelled."));
        return;
    }
    if (write_failed_ && part_ && part_->error() != QFileDevice::NoError) {
        fail(tr("Could not save the download: %1").arg(part_->errorString()));
        return;
    }
    if (reply->error() != QNetworkReply::NoError && !write_failed_) {
        fail(tr("The download stopped: %1").arg(reply->errorString()));
        return;
    }
    part_->close();
    if (file == nullptr || !offer_ || write_failed_
        || QString::fromLatin1(hash_->result().toHex()) != from_std(file->sha256)) {
        fail(tr("The download did not match the checksum GitHub published for it, so it was "
                "not kept."));
        return;
    }
    const std::filesystem::path done = download_dir() / file->name;
    QFile::remove(path_text(done));
    if (!part_->rename(path_text(done))) {
        fail(tr("Could not save the download: %1").arg(part_->errorString()));
        return;
    }
    part_.reset();
    hash_.reset();
    progress_ = 1.0;
    downloaded_path_ = done;
    downloaded_version_ = offer_->version;
    const QString message = tr("SeventhSlide %1 is downloaded and ready to install.")
                                .arg(latestVersion());
    report(true, message);
    emit downloadFinished(true, message);
}

// ---- The install ------------------------------------------------------------

void AppUpdater::install()
{
    if (busy() || !downloaded()) {
        return;
    }
    const update::Installer kind = update::installer_for(package_file_);
    const QString path = path_text(downloaded_path_);
    if (kind == update::Installer::windows_setup || kind == update::Installer::disk_image) {
        // Opened as a double-click opens it: Windows asks to run the
        // installer as an administrator, which starting it directly would
        // not, and the Finder mounts the disk image.
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
            const QString message = tr("Could not open %1.").arg(path);
            report(false, message);
            emit installFinished(false, message);
            return;
        }
        const QString message =
            kind == update::Installer::windows_setup
                ? tr("The installer is open. SeventhSlide closes so that it can be replaced.")
                : tr("The disk image is open. Drag SeventhSlide into Applications, replacing "
                     "the copy there, and open it again.");
        report(true, message);
        emit installFinished(true, message);
        emit quitForInstall();
        return;
    }
    const std::vector<std::string> command =
        update::install_command(kind, downloaded_path_.string());
    if (command.empty()) {
        return;
    }
    QStringList args;
    for (std::size_t i = 1; i < command.size(); ++i) {
        args << from_std(command[i]);
    }
    installer_ = new QProcess(this);
    installer_->setProgram(from_std(command.front()));
    installer_->setArguments(args);
    installer_->setProcessChannelMode(QProcess::MergedChannels);
    connect(installer_, &QProcess::finished, this, &AppUpdater::on_installer_done);
    connect(installer_, &QProcess::errorOccurred, this, &AppUpdater::on_installer_error);
    installer_->start();
    report(true, tr("Installing SeventhSlide %1… Enter an administrator’s password if you are "
                    "asked for one.")
                     .arg(latestVersion()));
}

void AppUpdater::on_installer_done(int exit_code, QProcess::ExitStatus how)
{
    QProcess* process = installer_;
    installer_ = nullptr;
    process->deleteLater();
    if (how == QProcess::NormalExit && exit_code == 0) {
        installed_ = true;
        const QString message = tr("SeventhSlide %1 is installed. It runs the next time "
                                   "SeventhSlide is opened.")
                                    .arg(latestVersion());
        report(true, message);
        emit installFinished(true, message);
        return;
    }
    // pkexec exits 126 when the password dialog is dismissed.
    if (how == QProcess::NormalExit && exit_code == 126) {
        const QString message = tr("The installation was cancelled.");
        report(false, message);
        emit installFinished(false, message);
        return;
    }
    const QStringList lines = QString::fromLocal8Bit(process->readAll())
                                  .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    const QString message = lines.isEmpty()
                                ? tr("The installation failed.")
                                : tr("The installation failed: %1").arg(lines.last().trimmed());
    report(false, message);
    emit installFinished(false, message);
}

void AppUpdater::on_installer_error(QProcess::ProcessError error)
{
    // Any other error is followed by ``finished``.
    if (error != QProcess::FailedToStart || installer_ == nullptr) {
        return;
    }
    QProcess* process = installer_;
    installer_ = nullptr;
    process->deleteLater();
    // No pkexec: the desktop's own software installer opens the package.
    if (QDesktopServices::openUrl(QUrl::fromLocalFile(path_text(downloaded_path_)))) {
        const QString message = tr("The package is open in your software installer.");
        report(true, message);
        emit installFinished(true, message);
        return;
    }
    const QString message = tr("Could not start the installation: %1").arg(process->errorString());
    report(false, message);
    emit installFinished(false, message);
}

} // namespace seventhslide::app
