// SeventhSlide's own updates; see app_updater.hpp.

#include "app_updater.hpp"

#include <seventhslide/platform/paths.hpp>
#include <seventhslide/version.hpp>

#include <QCoreApplication>
#include <QDesktopServices>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QVariantMap>

#include <system_error>
#include <utility>

namespace seventhslide::app {

namespace {

const QString k_auto_key = QStringLiteral("updates/autoCheck");
const QString k_channel_key = QStringLiteral("updates/channel");
const QString k_checked_key = QStringLiteral("updates/checked");

const QString k_stable = QStringLiteral("stable");
const QString k_beta = QStringLiteral("beta");

/// How long a check holds before the timer checks again.
constexpr qint64 k_check_every_s = 24 * 60 * 60;

/// What apt-get or dnf writes as it installs. A file rather than a pipe:
/// once pkexec runs it as root, closing the app cannot stop it, and a pipe
/// with no reader would.
constexpr const char* k_install_log = "install.log";

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
    return request;
}

/// The file's SHA-256 as lower-case hex; empty when it cannot be read.
QString file_sha256(const std::filesystem::path& path)
{
    QFile file(path_text(path));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        return {};
    }
    return QString::fromLatin1(hash.result().toHex());
}

QVariantList notes_list(const update::Release& release)
{
    QVariantList out;
    for (const auto& block : update::notes_blocks(release.notes, AppUpdater::repository())) {
        const char* kind = "paragraph";
        switch (block.kind) {
        case update::NotesBlock::Kind::heading:
            kind = "heading";
            break;
        case update::NotesBlock::Kind::item:
            kind = "item";
            break;
        case update::NotesBlock::Kind::code:
            kind = "code";
            break;
        case update::NotesBlock::Kind::paragraph:
            break;
        }
        out.append(QVariantMap{{QStringLiteral("kind"), QString::fromLatin1(kind)},
                               {QStringLiteral("text"), from_std(block.text)},
                               {QStringLiteral("marker"), from_std(block.marker)},
                               {QStringLiteral("depth"), block.depth}});
    }
    return out;
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
    , program_(QCoreApplication::applicationFilePath())
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
    // An install still running is left to QProcess: it stops pkexec while
    // the password is still being asked for, and cannot stop apt-get or dnf
    // once they run as root, which then finish on their own.
    if (installer_) {
        installer_->disconnect(this);
    }
}

void AppUpdater::start(std::chrono::milliseconds first, std::chrono::milliseconds every)
{
    if (!checks_) {
        return;
    }
    if (!busy()) {
        clear_download_dir();
    }
    // The first check is not held to a day: a day-old reply is all the app
    // would otherwise know of, and it starts knowing nothing.
    QTimer::singleShot(first, this, [this, every] {
        if (autoCheck() && !busy()) {
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
    emit stateChanged();
}

void AppUpdater::set_download_dir(std::filesystem::path dir)
{
    download_dir_ = std::move(dir);
}

std::filesystem::path AppUpdater::download_dir() const
{
    if (!download_dir_.empty()) {
        return download_dir_;
    }
    // A data folder chosen for testing keeps everything; otherwise the
    // cache, which stays on this computer (Windows' local AppData, not the
    // roaming one) and which the system may empty.
    if (qEnvironmentVariableIsSet(platform::k_data_dir_env.data())) {
        return platform::Paths{}.data_dir() / "updates";
    }
    return std::filesystem::path(
               QStandardPaths::writableLocation(QStandardPaths::CacheLocation).toStdU16String())
           / "updates";
}

void AppUpdater::clear_download_dir() const
{
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(download_dir(), ec)) {
        std::error_code rm;
        if (entry.is_regular_file(rm)) {
            std::filesystem::remove(entry.path(), rm);
        }
    }
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
    if (busy() || (channel != k_stable && channel != k_beta) || channel == this->channel()) {
        return;
    }
    QSettings().setValue(k_channel_key, channel);
    reconsider();
    emit changed();
    emit stateChanged();
}

QDateTime AppUpdater::lastChecked() const
{
    return QSettings().value(k_checked_key).toDateTime();
}

// ---- The newer release ------------------------------------------------------

void AppUpdater::reconsider()
{
    const update::Release* offer = update::release_to_offer(releases_, channel_value(), running_);
    offer_ = offer != nullptr ? std::optional<update::Release>(*offer) : std::nullopt;
    notes_ = offer_ ? notes_list(*offer_) : QVariantList{};
}

QString AppUpdater::latestVersion() const
{
    return offer_ ? from_std(offer_->version.text()) : QString{};
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

bool AppUpdater::installCloses() const noexcept
{
    const update::Installer kind = update::installer_for(package_file_);
    return kind == update::Installer::windows_setup || kind == update::Installer::disk_image;
}

bool AppUpdater::downloaded() const
{
    std::error_code ec;
    return offer_ && downloaded_ && downloaded_->version == offer_->version
           && std::filesystem::is_regular_file(downloaded_path_, ec);
}

bool AppUpdater::installed() const
{
    return offer_ && installed_version_ == offer_->version;
}

void AppUpdater::report(bool ok, const QString& message)
{
    status_ = message;
    failed_ = !ok;
    emit stateChanged();
}

void AppUpdater::finish_install(bool ok, const QString& message)
{
    report(ok, message);
    emit installFinished(ok, message);
}

void AppUpdater::clearStatus()
{
    if (busy() || (status_.isEmpty() && !failed_)) {
        return;
    }
    status_.clear();
    failed_ = false;
    emit stateChanged();
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
    // The API answers from where it was asked.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);
    request.setTransferTimeout(30'000);
    QNetworkReply* reply = network().get(request);
    reply_ = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] { on_releases(reply); });
    report(true, tr("Checking for updates…"));
}

void AppUpdater::on_releases(QNetworkReply* reply)
{
    reply->deleteLater();
    if (reply != reply_) {
        return;
    }
    reply_ = nullptr;
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
    emit changed();
    const QString message = offer_ ? tr("SeventhSlide %1 is available.").arg(latestVersion())
                                   : tr("SeventhSlide is up to date.");
    report(true, message);
    emit checked(true, message);
}

// ---- The download -----------------------------------------------------------

void AppUpdater::download()
{
    const update::ReleaseFile* file = own_file();
    if (busy() || file == nullptr || !offer_ || downloaded()) {
        return;
    }
    const Target target{offer_->version, *file};
    const std::filesystem::path dir = download_dir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    // The folder is the updater's own: a file an earlier download left,
    // whole or not, goes.
    clear_download_dir();
    downloaded_.reset();
    downloaded_path_.clear();
    target_ = target;
    part_ = std::make_unique<QFile>(path_text(dir / (file->name + ".part")));
    if (!part_->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        part_.reset();
        target_.reset();
        const QString message = tr("Could not write to %1").arg(path_text(dir));
        report(false, message);
        emit downloadFinished(false, message);
        return;
    }
    hash_ = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    cancelled_ = false;
    write_failed_ = false;
    progress_ = 0.0;
    percent_ = -1;
    // GitHub's link sends the download on to where its files are kept: a
    // redirect to another host, over HTTPS still (Qt's default policy).
    const QUrl url = download_base_.isEmpty() ? QUrl(from_std(file->url))
                                              : download_base_.resolved(QUrl(from_std(file->name)));
    QNetworkRequest request = github_request(url);
    // Long enough for a slow network, as long as something keeps arriving.
    request.setTransferTimeout(60'000);
    QNetworkReply* reply = network().get(request);
    reply_ = reply;
    const QString version = from_std(target.version.text());
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] { on_download_data(reply); });
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, reply, version](qint64 got, qint64 total) {
                if (reply != reply_ || total <= 0) {
                    return;
                }
                const int percent = static_cast<int>(got * 100 / total);
                if (percent == percent_) {
                    return;
                }
                percent_ = percent;
                progress_ = static_cast<double>(got) / static_cast<double>(total);
                report(true, tr("Downloading SeventhSlide %1… %2%").arg(version).arg(percent));
            });
    connect(reply, &QNetworkReply::finished, this, [this, reply] { on_download_done(reply); });
    report(true, tr("Downloading SeventhSlide %1…").arg(version));
}

void AppUpdater::cancelDownload()
{
    if (downloading()) {
        cancelled_ = true;
        reply_->abort();
    }
}

void AppUpdater::on_download_data(QNetworkReply* reply)
{
    // Data queued before a cancel arrives after it, when the reply is shut.
    if (reply != reply_ || cancelled_ || !reply->isOpen()) {
        return;
    }
    if (!write_chunk(reply->readAll())) {
        reply->abort();
    }
}

bool AppUpdater::write_chunk(const QByteArray& chunk)
{
    if (!part_ || !target_ || write_failed_) {
        return false;
    }
    hash_->addData(chunk);
    // More than GitHub said the file holds is not the file.
    if (part_->write(chunk) != chunk.size()
        || (target_->file.size > 0 && part_->size() > target_->file.size)) {
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

void AppUpdater::on_download_done(QNetworkReply* reply)
{
    reply->deleteLater();
    if (reply != reply_ || !target_) {
        return;
    }
    const Target target = *target_;
    reply_ = nullptr;
    // What arrived after the last readyRead; an aborted reply has nothing.
    if (reply->error() == QNetworkReply::NoError && reply->bytesAvailable() > 0) {
        write_chunk(reply->readAll());
    }
    target_.reset();
    const auto fail = [this](const QString& message) {
        discard_part();
        hash_.reset();
        progress_ = 0.0;
        report(false, message);
        emit downloadFinished(false, message);
    };
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
    if (write_failed_ || QString::fromLatin1(hash_->result().toHex()) != from_std(target.file.sha256)) {
        fail(tr("The download did not match the checksum GitHub published for it, so it was "
                "not kept."));
        return;
    }
    const std::filesystem::path done = download_dir() / target.file.name;
    QFile::remove(path_text(done));
    if (!part_->rename(path_text(done))) {
        fail(tr("Could not save the download: %1").arg(part_->errorString()));
        return;
    }
    part_.reset();
    hash_.reset();
    progress_ = 1.0;
    downloaded_path_ = done;
    downloaded_ = target;
    const QString message = tr("SeventhSlide %1 is downloaded and ready to install.")
                                .arg(from_std(target.version.text()));
    report(true, message);
    emit downloadFinished(true, message);
}

// ---- The install ------------------------------------------------------------

void AppUpdater::install()
{
    if (busy() || !downloaded_ || !downloaded()) {
        return;
    }
    const Target target = *downloaded_;
    const QString version = from_std(target.version.text());
    // The file sat in a folder this user can write to since it was checked,
    // and is about to run as an administrator: check it again.
    if (file_sha256(downloaded_path_) != from_std(target.file.sha256)) {
        std::error_code ec;
        std::filesystem::remove(downloaded_path_, ec);
        downloaded_.reset();
        downloaded_path_.clear();
        finish_install(false, tr("The downloaded file has changed since it was checked, so it "
                                 "was not installed. Download it again."));
        return;
    }
    const update::Installer kind = update::installer_for(package_file_);
    const QString path = path_text(downloaded_path_);
    if (kind == update::Installer::windows_setup || kind == update::Installer::disk_image) {
        // Opened as a double-click opens it: Windows asks to run the
        // installer as an administrator, which starting it directly would
        // not, and the Finder mounts the disk image.
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
            finish_install(false, tr("Could not open %1.").arg(path));
            return;
        }
        finish_install(true, kind == update::Installer::windows_setup
                                 ? tr("The installer is open.")
                                 : tr("The disk image is open."));
        emit quitForInstall();
        return;
    }
    const std::vector<std::string> command =
        update::install_command(kind, downloaded_path_.string());
    if (command.empty()) {
        finish_install(false, tr("SeventhSlide cannot install %1 itself.").arg(path));
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
    installer_->setStandardOutputFile(path_text(download_dir() / k_install_log));
    connect(installer_, &QProcess::finished, this, &AppUpdater::on_installer_done);
    connect(installer_, &QProcess::errorOccurred, this, &AppUpdater::on_installer_error);
    // Said first: a program that cannot start reports at once.
    report(true, tr("Installing SeventhSlide %1… Enter an administrator’s password if you are "
                    "asked for one.")
                     .arg(version));
    installer_->start();
}

void AppUpdater::on_installer_done(int exit_code, QProcess::ExitStatus how)
{
    QProcess* process = installer_;
    installer_ = nullptr;
    process->deleteLater();
    const std::filesystem::path log = download_dir() / k_install_log;
    if (how == QProcess::NormalExit && exit_code == 0 && downloaded_) {
        installed_version_ = downloaded_->version;
        const QString version = from_std(downloaded_->version.text());
        // Installed: the package is not needed again.
        std::error_code ec;
        std::filesystem::remove(downloaded_path_, ec);
        std::filesystem::remove(log, ec);
        downloaded_.reset();
        downloaded_path_.clear();
        finish_install(true, tr("SeventhSlide %1 is installed. Restart SeventhSlide to use it.")
                                 .arg(version));
        return;
    }
    // pkexec exits 126 when the password dialog is dismissed.
    if (how == QProcess::NormalExit && exit_code == 126) {
        finish_install(false, tr("The installation was cancelled."));
        return;
    }
    QFile file(path_text(log));
    const QStringList lines = file.open(QIODevice::ReadOnly)
                                  ? QString::fromLocal8Bit(file.readAll())
                                        .split(QLatin1Char('\n'), Qt::SkipEmptyParts)
                                  : QStringList{};
    finish_install(false, lines.isEmpty()
                              ? tr("The installation failed.")
                              : tr("The installation failed: %1").arg(lines.last().trimmed()));
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
        finish_install(true, tr("The package is open in your software installer. Restart "
                                "SeventhSlide once it is installed."));
        return;
    }
    finish_install(false, tr("Could not start the installation: %1").arg(process->errorString()));
}

void AppUpdater::restart()
{
    if (!installed()) {
        return;
    }
    // Opened once this process has gone, so the new version does not start
    // beside it (two listening on the web remote's port, say). A deb or an
    // rpm put the new program where this one was.
    QString program = program_;
    program.remove(QStringLiteral(" (deleted)"));
    const bool started = QProcess::startDetached(
        QStringLiteral("/bin/sh"),
        {QStringLiteral("-c"),
         QStringLiteral("while kill -0 \"$1\" 2>/dev/null; do sleep 0.2; done; exec \"$0\""),
         program, QString::number(QCoreApplication::applicationPid())});
    if (!started) {
        report(false, tr("Could not restart SeventhSlide. Close it and open it again."));
        return;
    }
    emit quitForInstall();
}

} // namespace seventhslide::app
