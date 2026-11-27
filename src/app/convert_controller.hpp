#pragma once

// Convert: library files re-encoded by ffmpeg into new files in the library.
//
// Each file is two jobs on the media tools (ToolsController): ffprobe reads
// it, then ffmpeg writes the new file to a staging name in the kind's media
// folder. Only a finished file is taken into the library
// (MediaController::adopt_staged): beside the source, or in its place with
// Replace original. A conversion that fails or is cancelled leaves nothing
// behind, and the source is untouched until a replacement is in place.
//
// SessionFacade owns one and exposes it as `session.convert`.

#include "media_kind_qml.hpp"
#include "tool_job_list.hpp"
#include "tool_jobs.hpp"

#include <seventhslide/db/media_kind.hpp>
#include <seventhslide/media/convert.hpp>

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

class QTemporaryDir;

namespace seventhslide::app {

class MediaController;
class OpenLibrary;
class ToolsController;

class ConvertController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The session owns Convert: session.convert")

    /// A conversion has not finished.
    Q_PROPERTY(bool converting READ converting NOTIFY jobsChanged)
    /// Every conversion that has not finished, in the order they run:
    /// ``id``, ``title`` (the new file's, once its source is read),
    /// ``running`` (false while it waits its turn), ``progress`` (0 to 1).
    Q_PROPERTY(QVariantList jobs READ jobs NOTIFY jobsChanged)
    /// 0 to 1 across them.
    Q_PROPERTY(double progress READ progress NOTIFY jobsChanged)

public:
    ConvertController(ToolsController& tools,
                      OpenLibrary& library,
                      MediaController& media,
                      QObject* parent = nullptr);
    ~ConvertController() override;

    ConvertController(const ConvertController&) = delete;
    ConvertController& operator=(const ConvertController&) = delete;
    ConvertController(ConvertController&&) = delete;
    ConvertController& operator=(ConvertController&&) = delete;

    /// Read library file ``filename`` of ``kind`` for the dialog. Returns a
    /// request id; ``probeFinished`` reports it once, from the event loop:
    /// ``{ok, error, width, height, fps, duration, size, hasVideo, hasAudio,
    /// videoCodec, audioCodec (as people call them), sampleRate, channels,
    /// frame}`` (the probe ConvertText reads). ``frame``, for a video, is a
    /// ``file://`` URL of one of its frames, or empty when none was made.
    Q_INVOKABLE qint64 probeMedia(seventhslide::app::MediaKindQml::Kind kind,
                                  const QString& filename);

    /// Convert each of ``filenames`` (library files of ``kind``) as ``choice``
    /// says (media::convert_choice_from_json). What it makes goes to its own
    /// kind's library: a video made into sound goes to Audio. Without
    /// ``replaceOriginal`` each new file is filed in ``folderId`` (-1: none),
    /// a folder of the kind it makes; with it, each takes its source's place,
    /// which only a file of the same kind can. ``name``, for one file, is
    /// the new file's title without its extension; empty, or for several
    /// files, each is titled by what changed. Returns a request id;
    /// ``convertFinished`` reports it once, from the event loop, when every
    /// file is done: ``{ok, count, filenames, errors, kind}``, ``kind`` the
    /// MediaKind the files went to, an error reading ``"<title>: <reason>"``.
    Q_INVOKABLE qint64 convertMedia(seventhslide::app::MediaKindQml::Kind kind,
                                    const QStringList& filenames,
                                    const QVariantMap& choice,
                                    qint64 folderId,
                                    bool replaceOriginal,
                                    const QString& name = {});

    /// Stop conversion ``id`` (from ``jobs``). It reports as cancelled and
    /// leaves no file.
    Q_INVOKABLE void cancelConversion(qint64 id);

    [[nodiscard]] bool converting() const noexcept { return !conversions_.empty(); }
    [[nodiscard]] QVariantList jobs() const;
    [[nodiscard]] double progress() const;

signals:
    void probeFinished(qint64 requestId, const QVariantMap& result);
    void convertFinished(qint64 requestId, const QVariantMap& result);
    void jobsChanged();

private:
    /// One ``convertMedia`` call, reported once all its files are done.
    struct Request {
        qint64 id = 0;
        int pending = 0;
        QStringList filenames;
        QStringList errors;
        db::MediaKind out_kind = db::MediaKind::Video;
    };

    /// One file's conversion: read, then written, then taken in.
    struct Conversion {
        qint64 id = 0;
        qint64 request_id = 0;
        db::MediaKind kind = db::MediaKind::Video;
        /// What it makes: Audio for a video made into sound.
        db::MediaKind out_kind = db::MediaKind::Video;
        /// The title asked for, without its extension; empty for one made
        /// from what changed.
        std::string name;
        /// The source, as the library names it.
        std::string filename;
        std::string source_title;
        media::ConvertChoice choice;
        std::optional<std::int64_t> folder_id;
        bool replace = false;
        /// The library the source is in: a file finished after another
        /// library opened is not taken into it.
        std::filesystem::path media_root;
        /// The new file's title, once the source is read.
        std::string title;
        std::filesystem::path staged;
        /// The job running for it now: ffprobe, then ffmpeg.
        qint64 job_id = 0;
        bool writing = false;
        bool cancelled = false;
    };

    void start_probe(Conversion& conversion, const std::filesystem::path& ffprobe);
    /// Save a frame of ``source`` for the dialog, then report probe ``id``.
    void grab_frame(qint64 id, const std::filesystem::path& source,
                    const media::MediaProbe& probe);
    void on_probed(qint64 conversion_id, const ToolJob& job);
    void on_written(qint64 conversion_id, const ToolJob& job);
    /// ``conversion`` is over: take it out, report it into its request,
    /// and report the request when it was the last.
    void finish(qint64 conversion_id, std::optional<std::string> filename, QString error);
    [[nodiscard]] std::filesystem::path source_path(db::MediaKind kind,
                                                    const std::string& filename) const;
    /// A request that cannot start: reported from the event loop.
    qint64 refuse(const QString& why);

    ToolsController& tools_;
    OpenLibrary& library_;
    MediaController& media_;
    /// A conversion's progress is its ffmpeg job's, once it writes.
    ToolJobList<Conversion> conversions_{[](const Conversion& c) { return c.writing; }};
    std::vector<Request> requests_;
    qint64 last_request_id_ = 0;
    qint64 last_conversion_id_ = 0;
    /// The dialog's frames, made when first wanted; gone with the app.
    std::unique_ptr<QTemporaryDir> frames_;
};

} // namespace seventhslide::app
