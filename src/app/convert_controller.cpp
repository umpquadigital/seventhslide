// Convert: library files re-encoded by ffmpeg; see convert_controller.hpp.

#include "convert_controller.hpp"

#include "refusals.hpp"

#include "media_controller.hpp"
#include "open_library.hpp"
#include "qvariant_json.hpp"
#include "tools_controller.hpp"

#include <seventhslide/library/library.hpp>
#include <seventhslide/platform/tools.hpp>
#include <seventhslide/util/leaf_filename.hpp>

#include <QMetaObject>
#include <QPointer>
#include <QTemporaryDir>
#include <QUrl>
#include <QUuid>

#include <algorithm>
#include <system_error>
#include <utility>

namespace seventhslide::app {

namespace {

QString no_tools()
{
    return ConvertController::tr(
        "Converting needs ffmpeg and ffprobe, which were not found. See Settings › Updates & Tools.");
}

QVariantMap probe_map(const media::MediaProbe& probe)
{
    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("error"), QString{}},
        {QStringLiteral("width"), probe.width},
        {QStringLiteral("height"), probe.height},
        {QStringLiteral("fps"), probe.fps},
        {QStringLiteral("duration"), probe.duration_s},
        {QStringLiteral("size"), static_cast<qint64>(probe.size_bytes)},
        {QStringLiteral("hasVideo"), probe.has_video},
        {QStringLiteral("hasAudio"), probe.has_audio},
        {QStringLiteral("videoCodec"),
         QString::fromStdString(media::codec_display_name(probe.video_codec))},
        {QStringLiteral("audioCodec"),
         QString::fromStdString(media::codec_display_name(probe.audio_codec))},
        {QStringLiteral("sampleRate"), probe.sample_rate},
        {QStringLiteral("channels"), probe.channels},
        {QStringLiteral("frame"), QString{}},
    };
}

QVariantMap probe_error(const QString& why)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), why}};
}

/// The first line of a tool's error, which is the part that says what went
/// wrong; the rest is detail for the log.
QString first_line(const std::string& error)
{
    const auto end = error.find('\n');
    return QString::fromStdString(end == std::string::npos ? error : error.substr(0, end));
}

/// The kind of file ``choice`` makes.
db::MediaKind output_kind(const media::ConvertChoice& choice)
{
    if (std::holds_alternative<media::AudioConvert>(choice)) {
        return db::MediaKind::Audio;
    }
    if (std::holds_alternative<media::ImageConvert>(choice)) {
        return db::MediaKind::Image;
    }
    return db::MediaKind::Video;
}

std::string_view kind_word(db::MediaKind kind)
{
    switch (kind) {
    case db::MediaKind::Video:
        return "video";
    case db::MediaKind::Audio:
        return "audio";
    case db::MediaKind::Image:
        break;
    }
    return "image";
}

} // namespace

ConvertController::ConvertController(ToolsController& tools,
                                     OpenLibrary& library,
                                     MediaController& media,
                                     QObject* parent)
    : QObject(parent)
    , tools_(tools)
    , library_(library)
    , media_(media)
{
    // A job's progress moved, or one started: the list follows.
    connect(&tools_.jobs(), &ToolJobs::changed, this, [this] {
        if (!conversions_.empty()) {
            emit jobsChanged();
        }
    });
}

ConvertController::~ConvertController()
{
    // The jobs report to us; nothing may report after we are gone.
    for (const auto& conversion : conversions_.entries()) {
        if (conversion->job_id != 0) {
            tools_.jobs().cancel(conversion->job_id);
        }
        if (!conversion->staged.empty()) {
            std::error_code ec;
            std::filesystem::remove(conversion->staged, ec);
        }
    }
}

std::filesystem::path ConvertController::source_path(db::MediaKind kind,
                                                     const std::string& filename) const
{
    return library_.media_root() / db::media_subdir(kind) / util::leaf_filename(filename);
}

qint64 ConvertController::probeMedia(MediaKindQml::Kind kind, const QString& filename)
{
    const qint64 id = ++last_request_id_;
    const auto refuse_probe = [this, id](const QString& why) {
        QMetaObject::invokeMethod(
            this, [this, id, why] { emit probeFinished(id, probe_error(why)); },
            Qt::QueuedConnection);
        return id;
    };
    const auto mk = MediaKindQml::to_db(kind);
    tools_.ensure_refreshed();
    const auto ffprobe = tools_.location(platform::k_tool_ffprobe);
    if (!mk) {
        return refuse_probe(refusal::unknown_media_kind());
    }
    if (library_.media_root().empty()) {
        return refuse_probe(refusal::no_library());
    }
    if (!ffprobe) {
        return refuse_probe(no_tools());
    }
    const auto source = source_path(*mk, filename.toStdString());
    ToolJobSpec spec;
    spec.tool = std::string(platform::k_tool_ffprobe);
    spec.program = ffprobe->path;
    spec.args = media::ffprobe_args(source);
    spec.display = filename.toStdString();
    spec.capture_stdout = true;
    const bool video = *mk == db::MediaKind::Video;
    tools_.jobs().start(std::move(spec), [self = QPointer(this), id, source,
                                          video](const ToolJob& job) {
        if (!self) {
            return;
        }
        ConvertController* const me = self.data();
        if (job.state != ToolJobState::Done) {
            emit me->probeFinished(id, probe_error(job.state == ToolJobState::Cancelled
                                                       ? tr("Stopped")
                                                       : first_line(job.error)));
            return;
        }
        const auto probe = media::parse_probe(job.output);
        if (!probe) {
            emit me->probeFinished(id, probe_error(tr("ffprobe said nothing it could read.")));
            return;
        }
        if (video && probe->has_video) {
            me->grab_frame(id, source, *probe);
            return;
        }
        emit me->probeFinished(id, probe_map(*probe));
    });
    return id;
}

void ConvertController::grab_frame(qint64 id,
                                   const std::filesystem::path& source,
                                   const media::MediaProbe& probe)
{
    QVariantMap result = probe_map(probe);
    const auto ffmpeg = tools_.location(platform::k_tool_ffmpeg);
    if (!frames_) {
        frames_ = std::make_unique<QTemporaryDir>();
    }
    if (!ffmpeg || !frames_->isValid()) {
        emit probeFinished(id, result);
        return;
    }
    const std::filesystem::path dest =
        frames_->filePath(QStringLiteral("frame-%1.jpg").arg(id)).toStdString();
    ToolJobSpec spec;
    // Its own lane: never behind a conversion.
    spec.tool = "frame";
    spec.program = ffmpeg->path;
    // A third of the way in, past a fade from black, but not far into a
    // long one.
    const double at = probe.duration_s > 0.0 ? std::min(probe.duration_s / 3.0, 10.0) : 0.0;
    spec.args = media::frame_args(source, at, 180, dest);
    spec.display = source.filename().string();
    tools_.jobs().start(std::move(spec), [self = QPointer(this), id, dest,
                                          result](const ToolJob& job) mutable {
        if (!self) {
            return;
        }
        std::error_code ec;
        if (job.state == ToolJobState::Done && std::filesystem::exists(dest, ec)) {
            result.insert(QStringLiteral("frame"),
                          QUrl::fromLocalFile(QString::fromStdString(dest.string())).toString());
        }
        emit self->probeFinished(id, result);
    });
}

qint64 ConvertController::convertMedia(MediaKindQml::Kind kind,
                                       const QStringList& filenames,
                                       const QVariantMap& choice,
                                       qint64 folderId,
                                       bool replaceOriginal,
                                       const QString& name)
{
    const auto mk = MediaKindQml::to_db(kind);
    if (!mk) {
        return refuse(refusal::unknown_media_kind());
    }
    const auto parsed = media::convert_choice_from_json(kind_word(*mk), to_json(choice));
    if (!parsed) {
        return refuse(tr("That conversion is not one SeventhSlide offers."));
    }
    if (library_.media_root().empty()) {
        return refuse(refusal::no_library());
    }
    tools_.ensure_refreshed();
    const auto ffprobe = tools_.location(platform::k_tool_ffprobe);
    if (!tools_.location(platform::k_tool_ffmpeg) || !ffprobe) {
        return refuse(no_tools());
    }
    if (filenames.isEmpty()) {
        return refuse(tr("Choose a file to convert."));
    }
    const db::MediaKind out_kind = output_kind(*parsed);
    if (replaceOriginal && out_kind != *mk) {
        return refuse(tr("Only a file of the same kind can replace the original."));
    }
    // A name is for one file; several are each titled by what changed.
    std::string title = filenames.size() == 1 ? name.trimmed().toStdString() : std::string{};
    for (char& ch : title) {
        if (ch == '/' || ch == '\\') {
            ch = ' ';
        }
    }

    Request request;
    request.id = ++last_request_id_;
    request.pending = static_cast<int>(filenames.size());
    request.out_kind = out_kind;
    requests_.push_back(request);
    for (const auto& source : filenames) {
        auto conversion = std::make_unique<Conversion>();
        conversion->id = ++last_conversion_id_;
        conversion->request_id = request.id;
        conversion->kind = *mk;
        conversion->out_kind = out_kind;
        conversion->name = title;
        conversion->filename = util::leaf_filename(source.toStdString());
        conversion->source_title = media_.title_of(*mk, conversion->filename);
        conversion->choice = *parsed;
        if (folderId > 0) {
            conversion->folder_id = folderId;
        }
        conversion->replace = replaceOriginal;
        conversion->media_root = library_.media_root();
        conversion->title = conversion->source_title;
        start_probe(conversions_.add(std::move(conversion)), ffprobe->path);
    }
    emit jobsChanged();
    return request.id;
}

void ConvertController::start_probe(Conversion& conversion, const std::filesystem::path& ffprobe)
{
    ToolJobSpec spec;
    spec.tool = std::string(platform::k_tool_ffprobe);
    spec.program = ffprobe;
    spec.args = media::ffprobe_args(source_path(conversion.kind, conversion.filename));
    spec.display = conversion.source_title;
    spec.capture_stdout = true;
    const qint64 id = conversion.id;
    conversion.job_id = tools_.jobs().start(std::move(spec),
                                            [self = QPointer(this), id](const ToolJob& job) {
                                                if (self) {
                                                    self->on_probed(id, job);
                                                }
                                            });
}

void ConvertController::on_probed(qint64 conversion_id, const ToolJob& job)
{
    Conversion* conversion = conversions_.find(conversion_id);
    if (conversion == nullptr) {
        return;
    }
    conversion->job_id = 0;
    if (conversion->cancelled || job.state == ToolJobState::Cancelled) {
        finish(conversion_id, std::nullopt, tr("Stopped"));
        return;
    }
    if (job.state != ToolJobState::Done) {
        finish(conversion_id, std::nullopt,
               tr("Could not be read: %1").arg(first_line(job.error)));
        return;
    }
    const auto probe = media::parse_probe(job.output);
    if (!probe) {
        finish(conversion_id, std::nullopt, tr("Could not be read"));
        return;
    }
    const auto ffmpeg = tools_.location(platform::k_tool_ffmpeg);
    if (!ffmpeg) {
        finish(conversion_id, std::nullopt, no_tools());
        return;
    }
    const std::string extension{
        std::filesystem::path(conversion->filename).extension().string()};
    if (!conversion->name.empty()) {
        conversion->title = conversion->name + std::string(media::extension_of(conversion->choice));
    } else if (conversion->replace) {
        conversion->title = media::replacement_title(conversion->source_title, conversion->choice);
    } else {
        conversion->title = media::converted_title(conversion->source_title, conversion->choice,
                                                   *probe, extension);
    }
    // Hidden and with an extension no listing accepts, beside where it will
    // go: moving it into place is a rename, and a crash leaves only what the
    // importer's sweep removes.
    conversion->staged =
        conversion->media_root / db::media_subdir(conversion->out_kind)
        / (std::string(library::k_staging_prefix)
           + QUuid::createUuid().toString(QUuid::Id128).toStdString()
           + std::string(library::k_staging_suffix));

    // The kind it makes may have no folder yet (a library's first sound).
    std::error_code made_dir;
    std::filesystem::create_directories(conversion->staged.parent_path(), made_dir);
    if (made_dir) {
        finish(conversion_id, std::nullopt,
               tr("Could not write to the library’s media folder"));
        return;
    }

    ToolJobSpec spec;
    spec.tool = std::string(platform::k_tool_ffmpeg);
    spec.program = ffmpeg->path;
    spec.args = media::ffmpeg_args(conversion->choice, *probe,
                                   source_path(conversion->kind, conversion->filename),
                                   conversion->staged);
    spec.display = conversion->title;
    const double duration = probe->duration_s;
    spec.progress = [duration](std::string_view line) {
        return media::ffmpeg_progress(line, duration);
    };
    spec.stop_input = "q";
    conversion->writing = true;
    conversion->job_id = tools_.jobs().start(
        std::move(spec), [self = QPointer(this), conversion_id](const ToolJob& done) {
            if (self) {
                self->on_written(conversion_id, done);
            }
        });
    emit jobsChanged();
}

void ConvertController::on_written(qint64 conversion_id, const ToolJob& job)
{
    Conversion* conversion = conversions_.find(conversion_id);
    if (conversion == nullptr) {
        return;
    }
    conversion->job_id = 0;
    if (conversion->cancelled || job.state == ToolJobState::Cancelled) {
        finish(conversion_id, std::nullopt, tr("Stopped"));
        return;
    }
    if (job.state != ToolJobState::Done) {
        finish(conversion_id, std::nullopt, first_line(job.error));
        return;
    }
    if (library_.media_root() != conversion->media_root) {
        finish(conversion_id, std::nullopt,
               tr("Another library was opened before it finished"));
        return;
    }
    const std::optional<std::string> replacing =
        conversion->replace ? std::optional<std::string>{conversion->filename} : std::nullopt;
    QString error;
    const std::string extension{media::extension_of(conversion->choice)};
    auto filename = media_.adopt_staged(conversion->out_kind, conversion->staged, extension,
                                        conversion->title, conversion->folder_id, replacing,
                                        error);
    // adopt_staged has the staged file either way.
    conversion->staged.clear();
    finish(conversion_id, std::move(filename), error);
}

void ConvertController::finish(qint64 conversion_id,
                               std::optional<std::string> filename,
                               QString error)
{
    std::unique_ptr<Conversion> done = conversions_.take(conversion_id);
    if (!done) {
        return;
    }
    if (!done->staged.empty()) {
        std::error_code ec;
        std::filesystem::remove(done->staged, ec);
    }
    const auto request = std::find_if(requests_.begin(), requests_.end(),
                                      [&done](const Request& r) { return r.id == done->request_id; });
    if (request != requests_.end()) {
        if (filename) {
            request->filenames.push_back(QString::fromStdString(*filename));
        } else {
            request->errors.push_back(QStringLiteral("%1: %2")
                                          .arg(QString::fromStdString(done->source_title), error));
        }
        if (--request->pending == 0) {
            const QVariantMap result{
                {QStringLiteral("ok"), request->errors.isEmpty()},
                {QStringLiteral("count"), request->filenames.size()},
                {QStringLiteral("filenames"), request->filenames},
                {QStringLiteral("errors"), request->errors},
                {QStringLiteral("kind"), static_cast<int>(MediaKindQml::from_db(request->out_kind))},
            };
            const qint64 id = request->id;
            requests_.erase(request);
            emit jobsChanged();
            emit convertFinished(id, result);
            return;
        }
    }
    emit jobsChanged();
}

void ConvertController::cancelConversion(qint64 id)
{
    // Its job reports Cancelled, and finish() cleans up from there.
    (void)conversions_.cancel(id, tools_.jobs());
}

QVariantList ConvertController::jobs() const
{
    return conversions_.rows(tools_.jobs());
}

double ConvertController::progress() const
{
    return conversions_.progress(tools_.jobs());
}

qint64 ConvertController::refuse(const QString& why)
{
    const qint64 id = ++last_request_id_;
    QMetaObject::invokeMethod(
        this,
        [this, id, why] {
            emit convertFinished(id, {{QStringLiteral("ok"), false},
                                      {QStringLiteral("count"), 0},
                                      {QStringLiteral("filenames"), QStringList{}},
                                      {QStringLiteral("errors"), QStringList{why}}});
        },
        Qt::QueuedConnection);
    return id;
}

} // namespace seventhslide::app
