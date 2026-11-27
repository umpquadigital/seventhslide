// The app's own copy of yt-dlp; see ytdlp_updater.hpp.

#include "ytdlp_updater.hpp"

#include "tools_controller.hpp"

#include <seventhslide/media/ytdlp.hpp>
#include <seventhslide/platform/tools.hpp>
#include <seventhslide/util/atomic_file.hpp>

#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QSysInfo>

#include <system_error>
#include <utility>

namespace seventhslide::app {

namespace {

const QString k_auto_key = QStringLiteral("tools/ytdlpAutoUpdate");
const QString k_checked_key = QStringLiteral("tools/ytdlpChecked");

/// The file's SHA-256 as lower-case hex; empty when it cannot be read.
QString file_digest(const std::filesystem::path& path)
{
    QFile file(QString::fromStdString(path.string()));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        return {};
    }
    return QString::fromLatin1(hash.result().toHex());
}

} // namespace

YtdlpUpdater::YtdlpUpdater(ToolsController& tools, QObject* parent)
    : QObject(parent)
    , tools_(tools)
    , network_(new QNetworkAccessManager(this))
    , base_(QString::fromLatin1(k_release_base))
    , arch_(QSysInfo::currentCpuArchitecture().toStdString())
{
}

YtdlpUpdater::~YtdlpUpdater()
{
    if (reply_) {
        reply_->disconnect(this);
        reply_->abort();
    }
    if (part_) {
        part_->remove();
    }
}

void YtdlpUpdater::set_release_base(const QUrl& base)
{
    base_ = base;
}

void YtdlpUpdater::set_platform(std::string platform_id, std::string arch)
{
    platform_id_ = std::move(platform_id);
    arch_ = std::move(arch);
}

const std::string& YtdlpUpdater::platform_id() const
{
    return platform_id_.empty() ? tools_.search().platform_id : platform_id_;
}

std::filesystem::path YtdlpUpdater::managed_path() const
{
    return tools_.search().tools_dir
           / platform::tool_file_name(platform::k_tool_ytdlp, platform_id());
}

bool YtdlpUpdater::installed() const
{
    return platform::is_executable_file(managed_path(), platform_id());
}

bool YtdlpUpdater::autoUpdate() const
{
    return QSettings().value(k_auto_key, true).toBool();
}

void YtdlpUpdater::setAutoUpdate(bool on)
{
    if (on == autoUpdate()) {
        return;
    }
    QSettings().setValue(k_auto_key, on);
    emit changed();
}

QDateTime YtdlpUpdater::last_check() const
{
    return QSettings().value(k_checked_key).toDateTime();
}

QUrl YtdlpUpdater::url_of(const QString& name) const
{
    return base_.resolved(QUrl(name));
}

void YtdlpUpdater::update()
{
    if (busy()) {
        return;
    }
    fetch_sums();
}

void YtdlpUpdater::when_fresh(std::function<void()> then)
{
    const QDateTime checked = last_check();
    const bool due = !checked.isValid()
                     || checked.secsTo(QDateTime::currentDateTimeUtc()) >= k_check_every_s;
    if (busy()) {
        waiting_.push_back(std::move(then));
        return;
    }
    if (!autoUpdate() || !installed() || !due) {
        QMetaObject::invokeMethod(this, std::move(then), Qt::QueuedConnection);
        return;
    }
    waiting_.push_back(std::move(then));
    fetch_sums();
}

void YtdlpUpdater::fetch_sums()
{
    if (media::ytdlp_release_asset(platform_id(), arch_).empty()) {
        // Reported from the event loop, as every other outcome is.
        QMetaObject::invokeMethod(
            this,
            [this] {
                done(false, tr("yt-dlp has no build of its own for this computer."));
            },
            Qt::QueuedConnection);
        return;
    }
    status_ = tr("Checking for updates…");
    QNetworkRequest request(url_of(QString::fromLatin1(k_sums_name)));
    reply_ = network_->get(request);
    connect(reply_, &QNetworkReply::finished, this, &YtdlpUpdater::on_sums);
    emit changed();
}

void YtdlpUpdater::on_sums()
{
    QNetworkReply* reply = reply_;
    reply_ = nullptr;
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        done(false, tr("Could not reach yt-dlp’s releases: %1").arg(reply->errorString()));
        return;
    }
    const QByteArray body = reply->readAll();
    const auto sums = media::parse_sha256sums(std::string_view{body.constData(),
                                                               static_cast<std::size_t>(body.size())});
    const QString asset = QString::fromStdString(media::ytdlp_release_asset(platform_id(), arch_));
    const auto it = sums.find(asset.toStdString());
    if (it == sums.end()) {
        done(false, tr("yt-dlp’s latest release has no %1.").arg(asset));
        return;
    }
    const QString digest = QString::fromStdString(it->second);
    if (installed() && file_digest(managed_path()) == digest) {
        QSettings().setValue(k_checked_key, QDateTime::currentDateTimeUtc());
        done(true, tr("Up to date"));
        return;
    }
    fetch_build(asset, digest);
}

void YtdlpUpdater::fetch_build(const QString& asset, const QString& digest)
{
    std::error_code ec;
    std::filesystem::create_directories(tools_.search().tools_dir, ec);
    const auto part_path = tools_.search().tools_dir / ".yt-dlp.download";
    part_ = std::make_unique<QFile>(QString::fromStdString(part_path.string()));
    if (!part_->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        part_.reset();
        done(false, tr("Could not write to %1").arg(QString::fromStdString(
                        tools_.search().tools_dir.string())));
        return;
    }
    hash_ = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    status_ = installed() ? tr("Updating…") : tr("Installing…");
    reply_ = network_->get(QNetworkRequest(url_of(asset)));
    connect(reply_, &QNetworkReply::readyRead, this, &YtdlpUpdater::on_build_data);
    connect(reply_, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        if (total > 0) {
            status_ = (installed() ? tr("Updating… %1%") : tr("Installing… %1%"))
                          .arg(got * 100 / total);
            emit changed();
        }
    });
    connect(reply_, &QNetworkReply::finished, this, [this, digest] { on_build_done(digest); });
    emit changed();
}

void YtdlpUpdater::on_build_data()
{
    if (!reply_ || !part_) {
        return;
    }
    const QByteArray chunk = reply_->readAll();
    hash_->addData(chunk);
    part_->write(chunk);
}

void YtdlpUpdater::on_build_done(const QString& digest)
{
    QNetworkReply* reply = reply_;
    reply_ = nullptr;
    reply->deleteLater();
    // What arrived after the last readyRead.
    while (part_ && reply->bytesAvailable() > 0) {
        const QByteArray chunk = reply->readAll();
        hash_->addData(chunk);
        part_->write(chunk);
    }
    const auto discard = [this] {
        if (part_) {
            part_->remove();
            part_.reset();
        }
    };
    if (reply->error() != QNetworkReply::NoError) {
        discard();
        done(false, tr("The download stopped: %1").arg(reply->errorString()));
        return;
    }
    part_->close();
    if (QString::fromLatin1(hash_->result().toHex()) != digest) {
        discard();
        done(false, tr("The download did not match yt-dlp’s published checksum, so it was "
                       "not used."));
        return;
    }
    part_->setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                          | QFileDevice::ReadGroup | QFileDevice::ExeGroup
                          | QFileDevice::ReadOther | QFileDevice::ExeOther);
    const bool had = installed();
    // In place of the old copy in one step. std::filesystem::rename will
    // not replace a file on every Windows (MinGW's), and Windows refuses
    // while the old copy runs or is being read; move_file_durably replaces,
    // and waits a moment for a reader to let go.
    const std::filesystem::path part_path{part_->fileName().toStdU16String()};
    if (!util::move_file_durably(part_path, managed_path())) {
        discard();
        done(false, tr("Could not put the new yt-dlp in place (is a download running?)"));
        return;
    }
    part_.reset();
    QSettings().setValue(k_checked_key, QDateTime::currentDateTimeUtc());
    tools_.refresh();
    done(true, had ? tr("Updated") : tr("Installed"));
}

void YtdlpUpdater::done(bool ok, const QString& status)
{
    status_ = status;
    hash_.reset();
    emit changed();
    emit finished(ok, status);
    auto waiting = std::move(waiting_);
    waiting_.clear();
    for (auto& then : waiting) {
        then();
    }
}

} // namespace seventhslide::app
