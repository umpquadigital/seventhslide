#pragma once

// Download from the internet: a link's video or sound fetched by yt-dlp
// into the library - video to the Videos tab, sound alone to Audio.
//
// A link is looked up first (``lookupUrl``: what it holds, the heights it
// comes in), then downloaded (``downloadUrl``) into a staging folder in the
// kind's media folder; each file yt-dlp finishes there is taken into the
// library (MediaController::adopt_staged), and the folder goes afterwards,
// whatever happened. Before either, the app's own yt-dlp is brought up to
// date when a day has passed (YtdlpUpdater::when_fresh).
//
// SessionFacade owns one and exposes it as `session.download`.

#include "tool_job_list.hpp"
#include "tool_jobs.hpp"

#include <seventhslide/db/media_kind.hpp>
#include <seventhslide/media/ytdlp.hpp>

#include <QtQml/qqmlregistration.h>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace seventhslide::app {

class MediaController;
class OpenLibrary;
class ToolsController;

class DownloadController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The session owns downloads: session.download")

    /// A download has not finished.
    Q_PROPERTY(bool downloading READ downloading NOTIFY jobsChanged)
    /// Every download that has not finished, in order: ``id``, ``title``,
    /// ``running`` (false while it waits its turn), ``progress`` (0 to 1).
    Q_PROPERTY(QVariantList jobs READ jobs NOTIFY jobsChanged)
    /// 0 to 1 across them.
    Q_PROPERTY(double progress READ progress NOTIFY jobsChanged)

public:
    DownloadController(ToolsController& tools,
                       OpenLibrary& library,
                       MediaController& media,
                       QObject* parent = nullptr);
    ~DownloadController() override;

    DownloadController(const DownloadController&) = delete;
    DownloadController& operator=(const DownloadController&) = delete;
    DownloadController(DownloadController&&) = delete;
    DownloadController& operator=(DownloadController&&) = delete;

    /// What ``url`` holds. Returns a request id; ``lookupFinished`` reports
    /// it once, from the event loop: ``{ok, error, missing (no yt-dlp),
    /// updateSuggested, playlist, title, uploader, duration, thumbnail,
    /// heights (highest first), hasVideo, entries [{title, duration}]}``.
    Q_INVOKABLE qint64 lookupUrl(const QString& url);

    /// Download ``url``: ``options`` are ``audioOnly``, ``maxHeight`` (0:
    /// the best), ``playlist`` (every item), ``container`` (a video's: mp4,
    /// webm, mkv) and ``audioFormat`` (sound alone: m4a, mp3, opus, flac,
    /// wav). ``title`` is what the list of
    /// downloads calls it. Video is filed in ``videoFolderId``, sound alone
    /// in ``audioFolderId`` (-1: none). Returns a request id;
    /// ``downloadFinished`` reports it once, from the event loop: ``{ok,
    /// count, filenames, errors, updateSuggested}``.
    Q_INVOKABLE qint64 downloadUrl(const QString& url,
                                   const QVariantMap& options,
                                   const QString& title,
                                   qint64 videoFolderId,
                                   qint64 audioFolderId);

    /// Stop download ``id`` (from ``jobs``). It reports as stopped and
    /// leaves no file.
    Q_INVOKABLE void cancelDownload(qint64 id);

    [[nodiscard]] bool downloading() const noexcept { return !downloads_.empty(); }
    [[nodiscard]] QVariantList jobs() const;
    [[nodiscard]] double progress() const;

signals:
    void lookupFinished(qint64 requestId, const QVariantMap& result);
    void downloadFinished(qint64 requestId, const QVariantMap& result);
    void jobsChanged();

private:
    struct Download {
        qint64 id = 0;
        std::string url;
        std::string title;
        media::DownloadChoice choice;
        db::MediaKind kind = db::MediaKind::Video;
        std::optional<std::int64_t> folder_id;
        /// The library it goes into: one opened meanwhile does not get it.
        std::filesystem::path media_root;
        std::filesystem::path staging;
        qint64 job_id = 0;
        bool cancelled = false;
    };

    void start(qint64 id);
    void on_done(qint64 id, const ToolJob& job);
    /// Take ``download`` out and report it.
    void finish(qint64 id, QStringList filenames, QStringList errors, bool update_suggested);
    /// A lookup or download that cannot start, reported from the event loop.
    void refuse_lookup(qint64 id, const QString& why, bool missing);
    qint64 refuse_download(const QString& why);

    ToolsController& tools_;
    OpenLibrary& library_;
    MediaController& media_;
    ToolJobList<Download> downloads_;
    qint64 last_id_ = 0;
};

} // namespace seventhslide::app
