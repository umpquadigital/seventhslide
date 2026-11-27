// Download from the internet with yt-dlp; see download_controller.hpp.

#include "download_controller.hpp"

#include "refusals.hpp"
#include "media_controller.hpp"
#include "open_library.hpp"
#include "tools_controller.hpp"
#include "ytdlp_updater.hpp"

#include <seventhslide/db/media_kind.hpp>
#include <seventhslide/library/library.hpp>
#include <seventhslide/platform/tools.hpp>

#include <QMetaObject>
#include <QPointer>
#include <QUuid>

#include <algorithm>
#include <sstream>
#include <system_error>
#include <utility>

namespace seventhslide::app {

namespace {

namespace fs = std::filesystem;

constexpr const char* k_no_ytdlp =
    "Downloading needs yt-dlp, which is not installed. See Settings › Tools.";

/// The line of yt-dlp's stderr that says what went wrong: its last ERROR,
/// without the word, else its last line.
QString error_of(const std::string& stderr_tail)
{
    std::istringstream lines{stderr_tail};
    std::string last;
    std::string error;
    for (std::string line; std::getline(lines, line);) {
        if (line.empty()) {
            continue;
        }
        last = line;
        if (line.starts_with("ERROR:")) {
            error = line.substr(6);
        }
    }
    std::string out = error.empty() ? last : error;
    while (!out.empty() && out.front() == ' ') {
        out.erase(0, 1);
    }
    return QString::fromStdString(out);
}

QVariantMap info_map(const media::YtdlpInfo& info)
{
    QVariantList heights;
    for (const int h : info.heights) {
        heights.push_back(h);
    }
    QVariantList entries;
    for (const auto& e : info.entries) {
        entries.push_back(QVariantMap{{QStringLiteral("title"), QString::fromStdString(e.title)},
                                      {QStringLiteral("duration"), e.duration_s}});
    }
    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("error"), QString{}},
        {QStringLiteral("missing"), false},
        {QStringLiteral("updateSuggested"), false},
        {QStringLiteral("playlist"), info.playlist},
        {QStringLiteral("title"), QString::fromStdString(info.title)},
        {QStringLiteral("uploader"), QString::fromStdString(info.uploader)},
        {QStringLiteral("duration"), info.duration_s},
        {QStringLiteral("thumbnail"), QString::fromStdString(info.thumbnail)},
        {QStringLiteral("heights"), heights},
        {QStringLiteral("hasVideo"), info.has_video},
        {QStringLiteral("entries"), entries},
    };
}

/// ``path`` sits directly in ``dir``: yt-dlp prints the paths it wrote, and
/// only those the app asked for are taken in.
bool directly_in(const fs::path& path, const fs::path& dir)
{
    std::error_code ec;
    const fs::path a = fs::weakly_canonical(path, ec);
    const fs::path b = fs::weakly_canonical(dir, ec);
    return !ec && a.parent_path() == b;
}

} // namespace

DownloadController::DownloadController(ToolsController& tools,
                                       OpenLibrary& library,
                                       MediaController& media,
                                       QObject* parent)
    : QObject(parent)
    , tools_(tools)
    , library_(library)
    , media_(media)
{
    connect(&tools_.jobs(), &ToolJobs::changed, this, [this] {
        if (!downloads_.empty()) {
            emit jobsChanged();
        }
    });
}

DownloadController::~DownloadController()
{
    for (const auto& download : downloads_.entries()) {
        if (download->job_id != 0) {
            tools_.jobs().cancel(download->job_id);
        }
        std::error_code ec;
        fs::remove_all(download->staging, ec);
    }
}

void DownloadController::refuse_lookup(qint64 id, const QString& why, bool missing)
{
    QMetaObject::invokeMethod(
        this,
        [this, id, why, missing] {
            emit lookupFinished(id, {{QStringLiteral("ok"), false},
                                     {QStringLiteral("error"), why},
                                     {QStringLiteral("missing"), missing},
                                     {QStringLiteral("updateSuggested"), false}});
        },
        Qt::QueuedConnection);
}

qint64 DownloadController::lookupUrl(const QString& url)
{
    const qint64 id = ++last_id_;
    const QString link = url.trimmed();
    if (link.isEmpty()) {
        refuse_lookup(id, tr("Paste a link first."), false);
        return id;
    }
    tools_.ensure_refreshed();
    if (!tools_.location(platform::k_tool_ytdlp)) {
        refuse_lookup(id, QString::fromUtf8(k_no_ytdlp), true);
        return id;
    }
    tools_.ytdlp()->when_fresh([self = QPointer(this), id, link] {
        if (!self) {
            return;
        }
        // Found again: an update may have just put the app's own copy in.
        const auto ytdlp = self->tools_.location(platform::k_tool_ytdlp);
        if (!ytdlp) {
            self->refuse_lookup(id, QString::fromUtf8(k_no_ytdlp), true);
            return;
        }
        ToolJobSpec spec;
        spec.tool = std::string(platform::k_tool_ytdlp);
        spec.program = ytdlp->path;
        spec.args = media::ytdlp_lookup_args(link.toStdString());
        spec.display = link.toStdString();
        spec.capture_stdout = true;
        const bool managed = ytdlp->source == platform::ToolSource::Managed;
        self->tools_.jobs().start(std::move(spec), [self, id, managed](const ToolJob& job) {
            if (!self) {
                return;
            }
            if (job.state != ToolJobState::Done) {
                emit self->lookupFinished(
                    id, {{QStringLiteral("ok"), false},
                         {QStringLiteral("error"), job.state == ToolJobState::Cancelled
                                                       ? tr("Stopped")
                                                       : error_of(job.error)},
                         {QStringLiteral("missing"), false},
                         {QStringLiteral("updateSuggested"),
                          managed && media::ytdlp_error_suggests_update(job.error)}});
                return;
            }
            const auto info = media::parse_ytdlp_info(job.output);
            if (!info) {
                emit self->lookupFinished(id, {{QStringLiteral("ok"), false},
                                               {QStringLiteral("error"),
                                                tr("yt-dlp said nothing about this link that "
                                                   "SeventhSlide could read.")},
                                               {QStringLiteral("missing"), false},
                                               {QStringLiteral("updateSuggested"), false}});
                return;
            }
            emit self->lookupFinished(id, info_map(*info));
        });
    });
    return id;
}

qint64 DownloadController::refuse_download(const QString& why)
{
    const qint64 id = ++last_id_;
    QMetaObject::invokeMethod(
        this,
        [this, id, why] {
            emit downloadFinished(id, {{QStringLiteral("ok"), false},
                                       {QStringLiteral("count"), 0},
                                       {QStringLiteral("filenames"), QStringList{}},
                                       {QStringLiteral("errors"), QStringList{why}},
                                       {QStringLiteral("updateSuggested"), false}});
        },
        Qt::QueuedConnection);
    return id;
}

qint64 DownloadController::downloadUrl(const QString& url,
                                       const QVariantMap& options,
                                       const QString& title,
                                       qint64 videoFolderId,
                                       qint64 audioFolderId)
{
    const QString link = url.trimmed();
    if (link.isEmpty()) {
        return refuse_download(tr("Paste a link first."));
    }
    if (library_.media_root().empty()) {
        return refuse_download(refusal::no_library());
    }
    tools_.ensure_refreshed();
    if (!tools_.location(platform::k_tool_ytdlp)) {
        return refuse_download(QString::fromUtf8(k_no_ytdlp));
    }
    auto download = std::make_unique<Download>();
    download->id = ++last_id_;
    download->url = link.toStdString();
    download->title = title.trimmed().isEmpty() ? download->url : title.trimmed().toStdString();
    download->choice.audio_only = options.value(QStringLiteral("audioOnly")).toBool();
    download->choice.max_height = std::max(0, options.value(QStringLiteral("maxHeight")).toInt());
    download->choice.playlist = options.value(QStringLiteral("playlist")).toBool();
    if (const QString c = options.value(QStringLiteral("container")).toString(); !c.isEmpty()) {
        download->choice.container = c.toStdString();
    }
    if (const QString f = options.value(QStringLiteral("audioFormat")).toString(); !f.isEmpty()) {
        download->choice.audio_format = f.toStdString();
    }
    download->kind = download->choice.audio_only ? db::MediaKind::Audio : db::MediaKind::Video;
    const qint64 folder = download->choice.audio_only ? audioFolderId : videoFolderId;
    if (folder > 0) {
        download->folder_id = folder;
    }
    download->media_root = library_.media_root();
    // A staging name, as an import's: hidden, listed by nothing, and swept
    // when a library opens after a crash.
    download->staging = download->media_root / db::media_subdir(download->kind)
                        / (std::string(library::k_staging_prefix)
                           + QUuid::createUuid().toString(QUuid::Id128).toStdString()
                           + std::string(library::k_staging_suffix));
    const qint64 id = download->id;
    (void)downloads_.add(std::move(download));
    emit jobsChanged();
    tools_.ytdlp()->when_fresh([self = QPointer(this), id] {
        if (self) {
            self->start(id);
        }
    });
    return id;
}

void DownloadController::start(qint64 id)
{
    Download* download = downloads_.find(id);
    if (download == nullptr) {
        return;
    }
    if (download->cancelled) {
        finish(id, {}, {tr("Stopped")}, false);
        return;
    }
    const auto ytdlp = tools_.location(platform::k_tool_ytdlp);
    if (!ytdlp) {
        finish(id, {}, {QString::fromUtf8(k_no_ytdlp)}, false);
        return;
    }
    std::error_code ec;
    fs::create_directories(download->staging, ec);
    if (ec) {
        finish(id, {}, {tr("Could not write to the library’s media folder")}, false);
        return;
    }
    const auto ffmpeg = tools_.location(platform::k_tool_ffmpeg);
    ToolJobSpec spec;
    spec.tool = std::string(platform::k_tool_ytdlp);
    spec.program = ytdlp->path;
    spec.args = media::ytdlp_download_args(download->url, download->choice, download->staging,
                                           ffmpeg ? ffmpeg->path : fs::path{});
    spec.display = download->title;
    spec.capture_stdout = true;
    auto progress = std::make_shared<media::YtdlpProgress>();
    spec.progress = [progress](std::string_view line) { return progress->read(line); };
    download->job_id = tools_.jobs().start(std::move(spec),
                                           [self = QPointer(this), id](const ToolJob& job) {
                                               if (self) {
                                                   self->on_done(id, job);
                                               }
                                           });
    emit jobsChanged();
}

void DownloadController::on_done(qint64 id, const ToolJob& job)
{
    Download* download = downloads_.find(id);
    if (download == nullptr) {
        return;
    }
    download->job_id = 0;
    if (download->cancelled || job.state == ToolJobState::Cancelled) {
        finish(id, {}, {tr("Stopped")}, false);
        return;
    }
    if (library_.media_root() != download->media_root) {
        finish(id, {}, {tr("Another library was opened before it finished")}, false);
        return;
    }
    QStringList filenames;
    QStringList errors;
    // What finished is taken in, even when a later item of a playlist
    // failed.
    std::istringstream lines{job.output};
    for (std::string line; std::getline(lines, line);) {
        const auto file = media::parse_ytdlp_file(line);
        if (!file) {
            continue;
        }
        const auto& [item_title, path] = *file;
        const std::string name = item_title.empty() ? download->title : item_title;
        if (!directly_in(path, download->staging)) {
            continue;
        }
        const std::string extension = path.extension().string();
        if (!db::is_media_filename(download->kind, path.filename().string())) {
            errors.push_back(tr("%1: came as %2, which the library does not take")
                                 .arg(QString::fromStdString(name),
                                      QString::fromStdString(extension)));
            continue;
        }
        QString error;
        const auto taken = media_.adopt_staged(download->kind, path, extension,
                                               name + extension, download->folder_id,
                                               std::nullopt, error);
        if (taken) {
            filenames.push_back(QString::fromStdString(*taken));
        } else {
            errors.push_back(QStringLiteral("%1: %2").arg(QString::fromStdString(name), error));
        }
    }
    bool update_suggested = false;
    if (job.state != ToolJobState::Done) {
        errors.push_back(error_of(job.error));
        const auto ytdlp = tools_.location(platform::k_tool_ytdlp);
        update_suggested = ytdlp && ytdlp->source == platform::ToolSource::Managed
                           && media::ytdlp_error_suggests_update(job.error);
    } else if (filenames.isEmpty() && errors.isEmpty()) {
        errors.push_back(tr("yt-dlp finished without a file"));
    }
    finish(id, filenames, errors, update_suggested);
}

void DownloadController::finish(qint64 id,
                                QStringList filenames,
                                QStringList errors,
                                bool update_suggested)
{
    std::unique_ptr<Download> done = downloads_.take(id);
    if (!done) {
        return;
    }
    // Whatever yt-dlp left: its parts, and a file the library did not take.
    std::error_code ec;
    fs::remove_all(done->staging, ec);
    const QVariantMap result{
        {QStringLiteral("ok"), errors.isEmpty()},
        {QStringLiteral("count"), filenames.size()},
        {QStringLiteral("filenames"), filenames},
        {QStringLiteral("errors"), errors},
        {QStringLiteral("updateSuggested"), update_suggested},
    };
    emit jobsChanged();
    emit downloadFinished(id, result);
}

void DownloadController::cancelDownload(qint64 id)
{
    // Not started yet (waiting for an update): start() reports it.
    (void)downloads_.cancel(id, tools_.jobs());
}

QVariantList DownloadController::jobs() const
{
    return downloads_.rows(tools_.jobs());
}

double DownloadController::progress() const
{
    return downloads_.progress(tools_.jobs());
}

} // namespace seventhslide::app
