// Export (see the header).

#include "export_controller.hpp"

#include "entry_error.hpp"
#include "refusals.hpp"
#include "freeform_library_controller.hpp"
#include "library_controller.hpp"
#include "local_file_path.hpp"
#include "media_controller.hpp"
#include "open_library.hpp"
#include "preview_resources.hpp"
#include "qvariant_json.hpp"

#include <seventhslide/db/font_repository.hpp>
#include <seventhslide/db/media_kind.hpp>
#include <seventhslide/db/media_repository.hpp>
#include <seventhslide/db/schema.hpp>
#include <seventhslide/db/sync_records.hpp>
#include <seventhslide/exchange/adapt.hpp>
#include <seventhslide/exchange/bundle_io.hpp>
#include <seventhslide/freeform/doc_edit.hpp>
#include <seventhslide/freeform/media_refs.hpp>
#include <seventhslide/model/live_source.hpp>
#include <seventhslide/model/static_media.hpp>
#include <seventhslide/model/web_page.hpp>
#include <seventhslide/theme/theme_library.hpp>
#include <seventhslide/model/output_library.hpp>
#include <seventhslide/parse/song_xml.hpp>
#include <seventhslide/render/scene_compositor.hpp>
#include <seventhslide/session/session.hpp>
#include <seventhslide/session/still_frames.hpp>
#include <seventhslide/theme/theme_picks.hpp>
#include <seventhslide/util/atomic_file.hpp>
#include <seventhslide/util/file_copy.hpp>
#include <seventhslide/util/file_name.hpp>
#include <seventhslide/util/leaf_filename.hpp>
#include <seventhslide/util/strings.hpp>
#include <seventhslide/util/zip_writer.hpp>
#include <seventhslide/version.hpp>

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QImageWriter>
#include <QMetaObject>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace seventhslide::app {

namespace {

namespace fs = std::filesystem;

/// Progress reaches the GUI thread at most this often.
constexpr auto k_progress_interval = std::chrono::milliseconds(100);
/// The part of an image export that is laying the slides out; painting and
/// encoding them is the rest.
constexpr double k_layout_share = 0.15;

class QueuedProgress final : public ExportProgress {
public:
    QueuedProgress(std::shared_ptr<std::atomic<bool>> cancelled, std::function<void(double)> post)
        : cancelled_(std::move(cancelled))
        , post_(std::move(post))
    {
    }

    [[nodiscard]] bool stopped() const override { return cancelled_->load(); }

    void report(double fraction) const override
    {
        const auto now = std::chrono::steady_clock::now();
        if (fraction < 1.0 && now - last_ < k_progress_interval) {
            return;
        }
        last_ = now;
        post_(std::clamp(fraction, 0.0, 1.0));
    }

private:
    std::shared_ptr<std::atomic<bool>> cancelled_;
    std::function<void(double)> post_;
    mutable std::chrono::steady_clock::time_point last_{};
};

/// A path as the system writes it (backslashes on Windows, whichever way a
/// folder chosen as a URL came in), through UTF-16 so no code page is
/// involved.
[[nodiscard]] QString path_text(const fs::path& path)
{
    return QDir::toNativeSeparators(QString::fromStdU16String(path.u16string()));
}

/// A hidden name in ``dir`` to write a file under until it is whole.
[[nodiscard]] fs::path staging_path(const fs::path& dir)
{
    return dir
           / (".seventhslide-export-" + QUuid::createUuid().toString(QUuid::Id128).toStdString()
              + ".part");
}

/// What ``dir`` already holds, by lower-case name, for ``unique_file_name``.
[[nodiscard]] std::unordered_set<std::string> names_in(const fs::path& dir)
{
    std::unordered_set<std::string> taken;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        taken.insert(util::to_lower(entry.path().filename().string()));
    }
    return taken;
}

/// A name in ``dir`` (whose names are ``taken``) for ``stem`` + ``extension``,
/// taken from then on.
[[nodiscard]] std::string claim_name(std::unordered_set<std::string>& taken,
                                     std::string_view stem,
                                     std::string_view extension)
{
    auto name = util::unique_file_name(stem, extension, taken);
    taken.insert(util::to_lower(name));
    return name;
}

/// The folder an export writes into: ``raw`` when it is one.
[[nodiscard]] std::optional<fs::path> export_folder(const QString& raw)
{
    auto dir = local_path_from_qml(raw);
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) {
        return std::nullopt;
    }
    return dir;
}

// --- slides as images ---------------------------------------------------

struct StillEntry {
    std::string title;
    session::StillSource source;
};

/// Everything an image export reads, copied off the session and library.
struct StillsJob {
    /// The outputs exported, in order.
    std::vector<model::OutputConfig> outputs;
    std::vector<std::string> theme_priority;
    std::string ccli_licence_number;
    theme::OutputThemeMap service_themes;
    std::vector<StillEntry> entries;
    fs::path file;
};

/// ``n`` written in at least two digits, and as many as ``count`` needs, so
/// the names sort in order.
[[nodiscard]] std::string padded(std::size_t n, std::size_t count)
{
    auto digits = std::to_string(n);
    const auto width = std::max<std::size_t>(2, std::to_string(count).size());
    return std::string(width - std::min(width, digits.size()), '0') + digits;
}

[[nodiscard]] QByteArray png_bytes(const QImage& image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, "png");
    if (image.isNull() || !writer.write(image)) {
        return {};
    }
    return bytes;
}

[[nodiscard]] util::ZipTime zip_time_now()
{
    const auto now = QDateTime::currentDateTime();
    return {now.date().year(), now.date().month(), now.date().day(),
            now.time().hour(), now.time().minute(), now.time().second()};
}

/// One entry laid out: its stills on every output, in the job's order.
struct LaidOut {
    const StillEntry* entry = nullptr;
    std::vector<session::OutputStills> stills;
};

/// Worker thread. The slides of ``job`` painted at each output's canvas size
/// into a ZIP at ``job.file``: ``NN-SS Title.png``, under a folder per output
/// when there are several.
[[nodiscard]] ExportOutcome write_stills(const StillsJob& job,
                                         PreviewResources& previews,
                                         const ExportProgress& progress)
{
    ExportOutcome out;
    out.path = path_text(job.file);
    const session::StackBuildContext ctx{previews.measurer(), job.outputs, job.theme_priority,
                                         job.ccli_licence_number, job.service_themes};

    // Lay everything out first, so the painting knows how much there is.
    std::vector<LaidOut> laid;
    std::size_t total = 0;
    std::size_t most_slides = 0;
    for (std::size_t i = 0; i < job.entries.size(); ++i) {
        if (progress.stopped()) {
            out.stopped = true;
            return out;
        }
        auto stills = session::still_frames(job.entries[i].source, ctx);
        std::size_t frames = 0;
        for (const auto& output : stills) {
            frames += output.frames.size();
            most_slides = std::max(most_slides, output.frames.size());
        }
        if (frames == 0) {
            ++out.skipped;
        } else {
            total += frames;
            laid.push_back({&job.entries[i], std::move(stills)});
        }
        progress.report(k_layout_share * static_cast<double>(i + 1)
                        / static_cast<double>(job.entries.size()));
    }
    if (total == 0) {
        out.errors.push_back(
            ExportController::tr("Nothing here has slides or pictures to export."));
        return out;
    }
    if (total > util::k_zip_max_entries) {
        out.errors.push_back(ExportController::tr(
            "There are too many slides for one file: export fewer at a time."));
        return out;
    }

    const auto staged = staging_path(job.file.parent_path());
    std::ofstream stream(staged, std::ios::binary | std::ios::trunc);
    // Whatever stops the export leaves no archive: none of it was saved.
    const auto discard = [&] {
        stream.close();
        std::error_code ec;
        fs::remove(staged, ec);
        out.count = 0;
    };
    const auto fail = [&](QString why) {
        discard();
        out.errors.push_back(std::move(why));
        return out;
    };
    if (!stream) {
        return fail(ExportController::tr("Could not write to %1.")
                        .arg(path_text(job.file.parent_path())));
    }
    util::ZipWriter zip(stream, zip_time_now());

    // A folder per output when there are several, named as the outputs are.
    std::vector<std::string> folders(job.outputs.size());
    if (job.outputs.size() > 1) {
        std::unordered_set<std::string> taken;
        for (std::size_t o = 0; o < job.outputs.size(); ++o) {
            folders[o] = claim_name(taken, util::portable_file_name(job.outputs[o].name, "Output"), "")
                         + "/";
        }
    }

    std::size_t done = 0;
    for (std::size_t o = 0; o < job.outputs.size(); ++o) {
        const auto canvas = model::output_canvas(job.outputs[o]);
        for (std::size_t k = 0; k < laid.size(); ++k) {
            auto& frames = laid[k].stills[o].frames;
            const auto stem = util::portable_file_name(laid[k].entry->title, "Untitled");
            for (std::size_t s = 0; s < frames.size(); ++s) {
                if (progress.stopped()) {
                    discard();
                    out.stopped = true;
                    return out;
                }
                render::StillPaint still;
                still.frame = scene::share_frame(std::move(frames[s]));
                still.size = QSize(canvas.width, canvas.height);
                still.cache = previews.image_cache();
                still.faces = &previews.measurer().faces();
                const QByteArray png = png_bytes(render::paint_still(still));
                const auto name = folders[o] + padded(k + 1, laid.size()) + "-"
                                  + padded(s + 1, most_slides) + " " + stem + ".png";
                if (png.isEmpty()) {
                    return fail(ExportController::tr("Could not draw %1.")
                                    .arg(QString::fromStdString(name)));
                }
                if (!zip.add(name, std::string_view(png.constData(), static_cast<std::size_t>(png.size())))) {
                    return fail(QString::fromStdString(zip.error()));
                }
                ++out.count;
                progress.report(k_layout_share
                                + (1.0 - k_layout_share) * static_cast<double>(++done)
                                      / static_cast<double>(total));
            }
        }
    }
    if (!zip.finish()) {
        return fail(QString::fromStdString(zip.error()));
    }
    stream.close();
    if (stream.fail() || !util::move_file_durably(staged, job.file)) {
        return fail(ExportController::tr("Could not save %1.").arg(path_text(job.file)));
    }
    return out;
}

/// Where an image export is saved: ``raw``, a ``.zip`` when it names no
/// extension, in a folder that exists. Nullopt with ``why`` otherwise.
[[nodiscard]] std::optional<fs::path> archive_path(const QString& raw, QString& why,
                                                  std::string_view extension = ".zip")
{
    auto file = local_path_from_qml(raw);
    if (file.empty() || file.filename().empty()) {
        why = ExportController::tr("Choose where to save the export.");
        return std::nullopt;
    }
    if (!file.has_extension()) {
        file += std::string(extension);
    }
    std::error_code ec;
    if (!fs::is_directory(file.parent_path(), ec)) {
        why = ExportController::tr("The folder %1 does not exist.")
                  .arg(path_text(file.parent_path()));
        return std::nullopt;
    }
    return file;
}

/// The outputs, themes and target of an image export: what every one shares.
/// Empty, or why it cannot start.
[[nodiscard]] QString prepare_stills(StillsJob& job,
                                     const session::Session& session,
                                     const QString& output_id,
                                     const QString& file)
{
    if (output_id.isEmpty()) {
        job.outputs = session.outputs();
    } else if (const auto* oc = model::find_output_by_id(session.outputs(), output_id.toStdString())) {
        job.outputs = {*oc};
    } else {
        return refusal::output_gone();
    }
    if (job.outputs.empty()) {
        return ExportController::tr("There are no outputs to draw the slides for.");
    }
    job.theme_priority = session.theme_priority();
    job.ccli_licence_number = session.ccli_licence_number();
    QString why;
    const auto path = archive_path(file, why);
    if (!path) {
        return why;
    }
    job.file = *path;
    return {};
}

/// The work of image export ``job``.
[[nodiscard]] ExportController::Work stills_work(std::shared_ptr<const StillsJob> job,
                                                 PreviewResources& previews)
{
    return [job = std::move(job), &previews](const ExportProgress& progress) {
        return write_stills(*job, previews, progress);
    };
}

// --- songs as OpenLyrics ------------------------------------------------

/// Worker thread.
[[nodiscard]] ExportOutcome write_song_files(const std::vector<model::Song>& songs,
                                             const fs::path& dir,
                                             const ExportProgress& progress)
{
    ExportOutcome out;
    out.path = path_text(dir);
    const std::string created_in = "SeventhSlide " + std::string(version_string());
    auto taken = names_in(dir);
    for (std::size_t i = 0; i < songs.size(); ++i) {
        if (progress.stopped()) {
            out.stopped = true;
            return out;
        }
        const auto& song = songs[i];
        const auto name = claim_name(taken, util::portable_file_name(song.title, "Song"), ".xml");
        if (util::write_file_atomically(dir / name, parse::write_song_xml(song, created_in))) {
            ++out.count;
        } else {
            out.errors.push_back(ExportController::tr("%1: could not be written")
                                     .arg(QString::fromStdString(song.title)));
        }
        progress.report(static_cast<double>(i + 1) / static_cast<double>(songs.size()));
    }
    return out;
}

// --- media files back out -----------------------------------------------

struct MediaExportFile {
    fs::path source;
    std::string title;
    /// The library file's extension (``.mp4``), which its copy keeps.
    std::string extension;
};

/// A library folder, as a folder of the export, or (the root) the folder
/// chosen.
struct MediaExportFolder {
    std::string name;
    std::vector<MediaExportFile> files;
    std::vector<MediaExportFolder> folders;
};

[[nodiscard]] std::uintmax_t bytes_in(const MediaExportFolder& folder)
{
    std::uintmax_t total = 0;
    for (const auto& file : folder.files) {
        std::error_code ec;
        const auto size = fs::file_size(file.source, ec);
        total += ec ? 0 : size;
    }
    for (const auto& sub : folder.folders) {
        total += bytes_in(sub);
    }
    return total;
}

/// ``file``'s name in the export: its title, keeping the library file's
/// extension (once, whether the title has it or not).
[[nodiscard]] std::string media_file_stem(const MediaExportFile& file)
{
    std::string_view title = file.title;
    if (!file.extension.empty() && title.size() > file.extension.size()
        && util::iequals(title.substr(title.size() - file.extension.size()), file.extension)) {
        title.remove_suffix(file.extension.size());
    }
    return util::portable_file_name(title, "Untitled");
}

class MediaCopier {
public:
    MediaCopier(const ExportProgress& progress, std::uintmax_t total_bytes, ExportOutcome& out)
        : progress_(progress)
        , total_(std::max<std::uintmax_t>(1, total_bytes))
        , out_(out)
    {
    }

    /// Copy ``folder``'s files and folders into ``dir``. False once stopped.
    bool copy_into(const fs::path& dir, const MediaExportFolder& folder)
    {
        auto taken = names_in(dir);
        for (const auto& file : folder.files) {
            if (!copy_file(dir, file, taken)) {
                return false;
            }
        }
        for (const auto& sub : folder.folders) {
            const auto sub_dir = dir / claim_name(taken, util::portable_file_name(sub.name, "Folder"), "");
            std::error_code ec;
            if (!fs::create_directory(sub_dir, ec) && ec) {
                out_.errors.push_back(ExportController::tr("%1: the folder could not be made")
                                          .arg(QString::fromStdString(sub.name)));
                continue;
            }
            if (!copy_into(sub_dir, sub)) {
                return false;
            }
        }
        return true;
    }

private:
    bool copy_file(const fs::path& dir,
                   const MediaExportFile& file,
                   std::unordered_set<std::string>& taken)
    {
        const auto title = QString::fromStdString(file.title);
        if (progress_.stopped()) {
            return false;
        }
        std::error_code ec;
        if (!fs::is_regular_file(file.source, ec)) {
            out_.errors.push_back(
                ExportController::tr("%1: the file is missing from the library").arg(title));
            return true;
        }
        const auto staged = staging_path(dir);
        const std::uintmax_t before = copied_;
        const auto outcome = util::copy_file_chunked(file.source, staged, [&](std::uintmax_t copied) {
            copied_ = before + copied;
            progress_.report(static_cast<double>(copied_) / static_cast<double>(total_));
            return !progress_.stopped();
        });
        switch (outcome) {
        case util::CopyOutcome::Stopped:
            return false;
        case util::CopyOutcome::Failed:
            out_.errors.push_back(ExportController::tr("%1: could not be copied").arg(title));
            return true;
        case util::CopyOutcome::Copied:
            break;
        }
        const auto name = claim_name(taken, media_file_stem(file), file.extension);
        fs::rename(staged, dir / name, ec);
        if (ec) {
            fs::remove(staged, ec);
            out_.errors.push_back(ExportController::tr("%1: could not be saved").arg(title));
            return true;
        }
        ++out_.count;
        return true;
    }

    const ExportProgress& progress_;
    std::uintmax_t total_;
    std::uintmax_t copied_ = 0;
    ExportOutcome& out_;
};

/// Worker thread.
[[nodiscard]] ExportOutcome copy_media(const MediaExportFolder& root,
                                       const fs::path& dir,
                                       const ExportProgress& progress)
{
    ExportOutcome out;
    out.path = path_text(dir);
    MediaCopier copier(progress, bytes_in(root), out);
    if (!copier.copy_into(dir, root)) {
        out.stopped = true;
    }
    return out;
}

[[nodiscard]] QVariantMap result_map(const ExportOutcome& outcome)
{
    return {
        {QStringLiteral("ok"), outcome.errors.isEmpty()},
        {QStringLiteral("count"), outcome.count},
        {QStringLiteral("skipped"), outcome.skipped},
        {QStringLiteral("errors"), outcome.errors},
        {QStringLiteral("notes"), outcome.notes},
        {QStringLiteral("path"), outcome.path},
    };
}

}  // namespace

ExportController::ExportController(session::Session& session,
                                   PreviewResources& previews,
                                   OpenLibrary& open_library,
                                   LibraryController& library,
                                   FreeformLibraryController& freeform,
                                   MediaController& media,
                                   QObject* parent)
    : QObject(parent)
    , session_(session)
    , previews_(previews)
    , open_library_(open_library)
    , library_(library)
    , freeform_(freeform)
    , media_(media)
{
    pool_.setMaxThreadCount(1);
}

ExportController::~ExportController()
{
    cancel_exports();
    pool_.waitForDone();
}

double ExportController::progress() const noexcept
{
    const int all = pending_ + reported_;
    if (all <= 0) {
        return 0.0;
    }
    return std::clamp((reported_ + running_fraction_) / all, 0.0, 1.0);
}

void ExportController::cancel_exports()
{
    cancelled_->store(true);
    // Exports asked for from now on are a new generation.
    cancelled_ = std::make_shared<std::atomic<bool>>(false);
}

QUrl ExportController::lastFolder() const
{
    if (!last_folder_.empty()) {
        return QUrl::fromLocalFile(path_text(last_folder_));
    }
    return QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
}

QUrl ExportController::suggestedFile(const QString& title) const
{
    return suggestedFileWith(title, QStringLiteral(".zip"));
}

QUrl ExportController::suggestedFileWith(const QString& title, const QString& extension) const
{
    const auto folder = local_path_from_qml(lastFolder().toString());
    return QUrl::fromLocalFile(path_text(
        folder / (util::portable_file_name(title.toStdString(), "Export") + extension.toStdString())));
}

QString ExportController::freeformFileExtension(int items, bool anyFolder) const
{
    return QString::fromUtf8(
        exchange::bundle_extension_for(static_cast<std::size_t>(std::max(items, 0)), anyFolder));
}

qint64 ExportController::start(const fs::path& folder, Work work)
{
    if (folder != last_folder_) {
        last_folder_ = folder;
        emit lastFolderChanged();
    }
    const qint64 id = ++last_request_id_;
    ++pending_;
    emit progressChanged();
    pool_.start([this, id, work = std::move(work), cancelled = cancelled_]() {
        const QueuedProgress progress(cancelled, [this](double fraction) {
            QMetaObject::invokeMethod(
                this, [this, fraction] { set_running_fraction(fraction); }, Qt::QueuedConnection);
        });
        ExportOutcome outcome;
        try {
            outcome = work(progress);
        } catch (const std::exception& ex) {
            outcome.errors.push_back(report_entry_error("export", ex));
        }
        // Cancelled while the last of it was written: it still stopped.
        outcome.stopped = outcome.stopped || cancelled->load();
        QMetaObject::invokeMethod(
            this, [this, id, outcome = std::move(outcome)] { report(id, outcome); },
            Qt::QueuedConnection);
    });
    return id;
}

qint64 ExportController::refuse(const QString& why)
{
    const qint64 id = ++last_request_id_;
    ExportOutcome outcome;
    outcome.errors.push_back(why);
    QMetaObject::invokeMethod(
        this, [this, id, result = result_map(outcome)] { emit exportFinished(id, result); },
        Qt::QueuedConnection);
    return id;
}

void ExportController::set_running_fraction(double fraction)
{
    running_fraction_ = fraction;
    emit progressChanged();
}

void ExportController::report(qint64 id, const ExportOutcome& outcome)
{
    --pending_;
    ++reported_;
    running_fraction_ = 0.0;
    if (pending_ <= 0) {
        pending_ = 0;
        reported_ = 0;
    }
    emit progressChanged();
    if (!outcome.stopped) {
        emit exportFinished(id, result_map(outcome));
        return;
    }
    ExportOutcome stopped = outcome;
    stopped.errors = {tr("The export was stopped.")};
    emit exportFinished(id, result_map(stopped));
}

qint64 ExportController::exportServiceImages(qint64 serviceId,
                                             const QVariant& themeMap,
                                             const QString& outputId,
                                             const QString& file)
try {
    auto* repo = library_.service_repo();
    if (repo == nullptr) {
        return refuse(refusal::no_library());
    }
    const auto service = serviceId >= 0 ? repo->get_service(serviceId) : std::nullopt;
    if (!service) {
        return refuse(refusal::service_gone());
    }
    auto job = std::make_shared<StillsJob>();
    if (auto why = prepare_stills(*job, session_, outputId, file); !why.isEmpty()) {
        return refuse(why);
    }
    // The picks being edited when the dialog passes them, else the stored ones.
    job->service_themes = theme::service_themes_from_json(
        themeMap.isValid() && !themeMap.isNull() ? to_json(themeMap) : service->theme_map);
    for (auto& item : repo->list_items(serviceId)) {
        // A divider is the rundown's own heading, not something it shows.
        if (item.kind() == model::ContentKind::Divider || item.kind() == model::ContentKind::Empty) {
            continue;
        }
        auto title = item.title();
        job->entries.push_back({std::move(title), std::move(item)});
    }
    if (job->entries.empty()) {
        return refuse(tr("This service has nothing in it to export."));
    }
    const auto folder = job->file.parent_path();
    return start(folder, stills_work(std::move(job), previews_));
} catch (const std::exception& ex) {
    return refuse(report_entry_error(__func__, ex));
}

qint64 ExportController::exportSongImages(const QVariantList& songIds,
                                          const QString& outputId,
                                          const QString& file)
try {
    auto* repo = library_.song_repo();
    const auto ids = parse_id_list(songIds);
    if (repo == nullptr) {
        return refuse(refusal::no_library());
    }
    if (!ids || ids->empty()) {
        return refuse(tr("Choose the songs to export."));
    }
    auto job = std::make_shared<StillsJob>();
    if (auto why = prepare_stills(*job, session_, outputId, file); !why.isEmpty()) {
        return refuse(why);
    }
    for (const auto id : *ids) {
        if (auto song = repo->get(id)) {
            auto title = song->title;
            job->entries.push_back({std::move(title), std::move(*song)});
        }
    }
    const auto folder = job->file.parent_path();
    return start(folder, stills_work(std::move(job), previews_));
} catch (const std::exception& ex) {
    return refuse(report_entry_error(__func__, ex));
}

qint64 ExportController::exportBibleImages(const QVariantList& ranges,
                                           const QString& outputId,
                                           const QString& file)
try {
    const qint64 bible_id = library_.selectedBibleId();
    if (bible_id < 0) {
        return refuse(refusal::no_bible_chosen());
    }
    auto job = std::make_shared<StillsJob>();
    if (auto why = prepare_stills(*job, session_, outputId, file); !why.isEmpty()) {
        return refuse(why);
    }
    for (const auto& raw : ranges) {
        const auto range = raw.toMap();
        const int start_verse = range.value(QStringLiteral("verseStart")).toInt();
        const int end_verse = range.value(QStringLiteral("verseEnd")).toInt();
        if (auto passage = library_.bible_passage(bible_id, library_.selectedBibleBook(),
                                                  library_.selectedBibleChapter(), start_verse,
                                                  end_verse)) {
            auto title = passage->ref;
            job->entries.push_back({std::move(title), std::move(*passage)});
        }
    }
    if (job->entries.empty()) {
        return refuse(tr("Choose the verses to export."));
    }
    const auto folder = job->file.parent_path();
    return start(folder, stills_work(std::move(job), previews_));
} catch (const std::exception& ex) {
    return refuse(report_entry_error(__func__, ex));
}

qint64 ExportController::exportFreeformImages(const QVariantList& itemIds,
                                              const QVariantList& folderIds,
                                              const QString& outputId,
                                              const QString& file)
try {
    auto* repo = freeform_.repo();
    const auto items = parse_id_list(itemIds);
    const auto folders = parse_id_list(folderIds);
    if (repo == nullptr) {
        return refuse(refusal::no_library());
    }
    if (!items || !folders || (items->empty() && folders->empty())) {
        return refuse(tr("Choose the Freeform items to export."));
    }
    auto job = std::make_shared<StillsJob>();
    if (auto why = prepare_stills(*job, session_, outputId, file); !why.isEmpty()) {
        return refuse(why);
    }
    // Each item once, however it was chosen: itself, or in a folder chosen
    // (or in a folder inside one).
    std::set<std::int64_t> seen;
    const auto add_item = [&](std::int64_t id) {
        if (!seen.insert(id).second) {
            return;
        }
        if (auto content = freeform_.live_content(id)) {
            auto title = content->name;
            job->entries.push_back({std::move(title), std::move(*content)});
        }
    };
    const auto tree = repo->folders();
    std::set<std::int64_t> seen_folders;
    const auto add_folder = [&](const auto& self, std::int64_t folder_id) -> void {
        if (!seen_folders.insert(folder_id).second) {
            return;
        }
        for (const auto& row : repo->list_browser_rows(folder_id)) {
            add_item(row.id);
        }
        for (const auto& child : tree.children(folder_id)) {
            if (child.id) {
                self(self, *child.id);
            }
        }
    };
    for (const auto id : *items) {
        add_item(id);
    }
    for (const auto id : *folders) {
        add_folder(add_folder, id);
    }
    if (job->entries.empty()) {
        return refuse(tr("There is nothing in those folders to export."));
    }
    const auto folder = job->file.parent_path();
    return start(folder, stills_work(std::move(job), previews_));
} catch (const std::exception& ex) {
    return refuse(report_entry_error(__func__, ex));
}

qint64 ExportController::exportSongXml(const QVariantList& songIds, const QString& folder)
try {
    auto* repo = library_.song_repo();
    const auto ids = parse_id_list(songIds);
    if (repo == nullptr) {
        return refuse(refusal::no_library());
    }
    if (!ids || ids->empty()) {
        return refuse(tr("Choose the songs to export."));
    }
    const auto dir = export_folder(folder);
    if (!dir) {
        return refuse(tr("Choose a folder to export to."));
    }
    auto songs = std::make_shared<std::vector<model::Song>>();
    for (const auto id : *ids) {
        if (auto song = repo->get(id)) {
            songs->push_back(std::move(*song));
        }
    }
    return start(*dir, [songs, dir = *dir](const ExportProgress& progress) {
        return write_song_files(*songs, dir, progress);
    });
} catch (const std::exception& ex) {
    return refuse(report_entry_error(__func__, ex));
}

qint64 ExportController::exportMediaFiles(MediaKindQml::Kind kind,
                                          const QStringList& filenames,
                                          const QVariantList& folderIds,
                                          const QString& folder)
try {
    const auto mk = MediaKindQml::to_db(kind);
    auto* repo = media_.media_repo();
    const auto folder_ids = parse_id_list(folderIds);
    if (!mk) {
        return refuse(refusal::unknown_media_kind());
    }
    if (repo == nullptr || open_library_.media_root().empty()) {
        return refuse(refusal::no_library());
    }
    if (!folder_ids || (filenames.isEmpty() && folder_ids->empty())) {
        return refuse(tr("Choose the files to export."));
    }
    const auto dir = export_folder(folder);
    if (!dir) {
        return refuse(tr("Choose a folder to export to."));
    }
    const auto media_dir = open_library_.media_root() / db::media_subdir(*mk);
    const auto file_of = [&](const std::string& filename) {
        const auto leaf = util::leaf_filename(filename);
        return MediaExportFile{media_dir / leaf, media_.title_of(*mk, leaf),
                               fs::path(leaf).extension().string()};
    };
    const auto tree = repo->folders(*mk);
    const auto folder_of = [&](const auto& self, const model::Folder& f) -> MediaExportFolder {
        MediaExportFolder out{f.name, {}, {}};
        for (const auto& item : repo->list_folder_items(*mk, *f.id)) {
            out.files.push_back(file_of(item.filename));
        }
        for (const auto& child : tree.children(*f.id)) {
            if (child.id) {
                out.folders.push_back(self(self, child));
            }
        }
        return out;
    };

    auto root = std::make_shared<MediaExportFolder>();
    for (const auto& filename : filenames) {
        root->files.push_back(file_of(filename.toStdString()));
    }
    for (const auto id : *folder_ids) {
        if (auto f = tree.get(id); f && f->id) {
            root->folders.push_back(folder_of(folder_of, *f));
        }
    }
    return start(*dir, [root, dir = *dir](const ExportProgress& progress) {
        return copy_media(*root, dir, progress);
    });
} catch (const std::exception& ex) {
    return refuse(report_entry_error(__func__, ex));
}

// --- Freeform files and service packages ------------------------------------

namespace {

/// How wide an item's preview in a Freeform file is drawn.
constexpr int k_preview_width = 480;

/// What the files a document names are, read off the library on the GUI
/// thread so the worker can find them: the media folder, the themes' folder,
/// and the library's titles of its pictures and videos.
struct MediaIndex {
    fs::path media_root;
    fs::path theme_dir;
    std::map<std::string, std::string, std::less<>> picture_titles;
    std::map<std::string, std::string, std::less<>> video_titles;
};

[[nodiscard]] MediaIndex media_index(OpenLibrary& library, MediaController& media)
{
    MediaIndex index;
    index.media_root = library.media_root();
    index.theme_dir = theme::theme_assets_dir(index.media_root);
    if (auto* repo = media.media_repo()) {
        for (const auto& file : repo->list_files(db::MediaKind::Image, false)) {
            index.picture_titles.emplace(file.filename, file.title());
        }
        for (const auto& file : repo->list_files(db::MediaKind::Video, false)) {
            index.video_titles.emplace(file.filename, file.title());
        }
    }
    return index;
}

/// A plain file name: not empty, ".", ".." or holding a separator.
[[nodiscard]] bool plain_name(std::string_view name)
{
    return !name.empty() && name != "." && name != ".."
           && name.find_first_of("/\\") == std::string_view::npos;
}

/// ``title`` without ``filename``'s extension when it ends in it, as a name
/// for a file of the package ("Bake sale.png" -> "Bake sale").
[[nodiscard]] std::string title_stem(std::string_view title, std::string_view filename)
{
    const auto ext = fs::path(std::string(filename)).extension().string();
    if (!ext.empty() && title.size() > ext.size() && util::iequals(title.substr(title.size() - ext.size()), ext)) {
        title.remove_suffix(ext.size());
    }
    return util::portable_file_name(title, "Untitled");
}

/// Where the file ``ref`` (a library file's name, or a ``/static/...`` URL)
/// is, what the library calls it, and its place as sync names it
/// (``images/<name>``). Its path is left empty when it is not on this
/// computer. Only the three folders a document's files are in are looked
/// in, unlike ``render::resolve_static_asset``, which serves every one.
struct FoundFile {
    exchange::FileToPack file;
    std::string sync_path;
};

[[nodiscard]] FoundFile find_file(const MediaIndex& index, std::string_view ref, canvas::MediaRefKind kind)
{
    FoundFile found;
    found.file.kind = kind;
    const bool video = kind == canvas::MediaRefKind::Video;
    std::string_view sub = db::media_subdir(video ? db::MediaKind::Video : db::MediaKind::Image);
    std::string_view name;
    constexpr std::string_view k_static = "/static/";
    if (ref.starts_with(k_static)) {
        const auto rest = ref.substr(k_static.size());
        const auto slash = rest.find('/');
        if (slash == std::string_view::npos) {
            return found;
        }
        sub = rest.substr(0, slash);
        name = rest.substr(slash + 1);
    } else {
        name = model::media_basename(ref);
    }
    if (!plain_name(name)) {
        return found;
    }
    fs::path path;
    if (sub == model::k_theme_backgrounds_subdir) {
        path = index.theme_dir / std::string(name);
        found.file.title = std::string(name);
    } else if (sub == model::k_images_subdir || sub == model::k_videos_subdir) {
        path = index.media_root / std::string(sub) / std::string(name);
        const auto& titles = sub == model::k_videos_subdir ? index.video_titles : index.picture_titles;
        const auto it = titles.find(name);
        found.file.title = it != titles.end() ? it->second : std::string(name);
        found.sync_path = std::string(sub) + "/" + std::string(name);
    } else {
        return found;
    }
    std::error_code ec;
    if (!index.media_root.empty() && fs::is_regular_file(path, ec)) {
        found.file.path = std::move(path);
    }
    return found;
}

/// Every file ``doc`` names, found, into ``media``; the sync places of those
/// not on this computer into ``missing``.
void find_files(const MediaIndex& index, const freeform::Document& doc,
                std::map<std::string, exchange::FileToPack, std::less<>>& media, std::vector<std::string>* missing)
{
    freeform::visit_document_media(doc, [&](const std::string& ref, canvas::MediaRefKind kind) {
        if (ref.empty() || media.contains(ref)) {
            return;
        }
        auto found = find_file(index, ref, kind);
        if (found.file.path.empty() && missing != nullptr && !found.sync_path.empty()) {
            missing->push_back(found.sync_path);
        }
        media.emplace(ref, std::move(found.file));
    });
}

/// The faces of every family the items name, and the families with none.
void find_fonts(const std::vector<db::LibraryFont>& library, const fs::path& media_root,
                exchange::BundleSource& source)
{
    // Each family once, case aside, as the documents first spell it.
    std::map<std::string, std::string> families;
    for (const auto& item : source.items) {
        for (const auto& family : db::font_families_named_in(item.document.to_json())) {
            families.try_emplace(util::to_lower(family), family);
        }
    }
    std::set<std::string> found;
    for (const auto& [family, spelled] : families) {
        bool any = false;
        for (const auto& font : library) {
            if (util::to_lower(font.family) == family) {
                if (found.insert(font.filename).second) {
                    source.fonts.push_back({media_root / std::string(model::k_fonts_subdir) / font.filename, font});
                }
                any = true;
            }
        }
        if (!any) {
            source.fonts_not_included.push_back(spelled);
        }
    }
}

[[nodiscard]] std::set<std::string, std::less<>> output_ids(const std::vector<model::OutputConfig>& outputs)
{
    std::set<std::string, std::less<>> ids;
    for (const auto& oc : outputs) {
        ids.insert(oc.id);
    }
    return ids;
}

/// A document too large for a library record to hold, which no library
/// could import.
[[nodiscard]] bool too_large(const freeform::Document& doc)
{
    // In the file each reference is an entry's name, which may be longer
    // than the library's (``media/<64 hex>.ext``): room is left for that.
    std::size_t refs = 0;
    freeform::visit_document_media(doc, [&refs](const std::string& ref, canvas::MediaRefKind) {
        refs += ref.empty() ? 0 : 1;
    });
    const std::size_t room = 96 * refs;
    return doc.to_json().dump().size() + room > db::k_largest_record;
}

/// What every Freeform file and package reads, copied off the session and
/// the library on the GUI thread.
struct BundleJob {
    exchange::BundleSource source;
    std::vector<model::OutputConfig> outputs;
    std::vector<std::string> theme_priority;
    std::string ccli_licence_number;
    theme::OutputThemeMap service_themes;
    fs::path file;
    int skipped = 0;
    QStringList notes;
};

/// A package's own: its rows and what they need to be found.
struct PackageJob : BundleJob {
    MediaIndex media;
    std::vector<db::LibraryFont> fonts;
    std::vector<model::ServiceItem> rows;
    std::map<std::string, std::string, std::less<>> stack_names;
    model::Service service;
    /// Each loose file's name in the package -> what it was named for.
    std::map<std::string, std::string, std::less<>> loose_titles;
};

/// Worker thread. Item ``doc``'s first slide, drawn small, as PNG; empty
/// when it has none.
[[nodiscard]] std::string preview_png(const freeform::Document& doc, const session::StackBuildContext& ctx,
                                      PreviewResources& previews)
{
    const session::StillSource source = session::LiveFreeformContent{doc.name, doc, std::nullopt};
    for (const auto& deck : doc.decks) {
        const auto* oc = model::find_output_by_id(ctx.outputs, deck.output_id);
        if (oc == nullptr) {
            continue;
        }
        auto frame = session::first_still_frame(source, ctx, deck.output_id);
        if (!frame) {
            continue;
        }
        const auto canvas = model::output_canvas(*oc);
        render::StillPaint still;
        still.frame = scene::share_frame(std::move(*frame));
        still.size = QSize(canvas.width, canvas.height);
        still.cache = previews.image_cache();
        still.faces = &previews.measurer().faces();
        const auto image = render::paint_still(still).scaledToWidth(k_preview_width, Qt::SmoothTransformation);
        const auto png = png_bytes(image);
        return std::string(png.constData(), static_cast<std::size_t>(png.size()));
    }
    return {};
}

[[nodiscard]] exchange::Progress bundle_progress(const ExportProgress& progress, double from, double to)
{
    return [&progress, from, to](std::uint64_t done, std::uint64_t total) {
        if (progress.stopped()) {
            return false;
        }
        const double part = total == 0 ? 1.0 : static_cast<double>(done) / static_cast<double>(total);
        progress.report(from + (to - from) * part);
        return true;
    };
}

/// What did not go, as notes for people.
void note_left_out(const exchange::WriteOutcome& written, ExportOutcome& out)
{
    for (const auto& title : written.media_not_included) {
        out.notes.push_back(ExportController::tr("“%1” is not on this computer, so it is not in the file.")
                                .arg(QString::fromStdString(title)));
    }
    for (const auto& title : written.media_unreadable) {
        out.notes.push_back(ExportController::tr("“%1” could not be read, so it is not in the file.")
                                .arg(QString::fromStdString(title)));
    }
    for (const auto& family : written.fonts_not_included) {
        out.notes.push_back(ExportController::tr("The library has no font “%1”, so it is not in the file.")
                                .arg(QString::fromStdString(family)));
    }
    for (const auto& face : written.font_files_unreadable) {
        out.notes.push_back(ExportController::tr("The font file of “%1” could not be read, so it is not in the file.")
                                .arg(QString::fromStdString(face)));
    }
}

/// The files a package's rows bring, each once, named for their rows.
class LooseFiles {
public:
    LooseFiles(const MediaIndex& media, std::size_t rows) : media_(media), rows_(rows) {}

    /// Library file ``filename`` of ``kind``, if it is here: its path.
    [[nodiscard]] std::optional<fs::path> here(db::MediaKind kind, const std::string& filename) const
    {
        if (!plain_name(filename)) {
            return std::nullopt;
        }
        auto path = media_.media_root / std::string(db::media_subdir(kind)) / filename;
        std::error_code ec;
        return fs::is_regular_file(path, ec) ? std::optional<fs::path>(std::move(path)) : std::nullopt;
    }

    /// ``path`` in the package as ``Media/NN <name>`` (``row`` from 1), or the
    /// name it already has there. ``within`` puts it in a folder of its own
    /// ("NN Notices/MM <name>").
    std::string add(const fs::path& path, std::size_t row, std::string_view name, std::string_view within = {},
                    std::size_t at = 0, std::size_t of = 0)
    {
        const auto key = path.string();
        if (const auto it = by_path_.find(key); it != by_path_.end()) {
            return it->second;
        }
        const auto ext = util::to_lower(path.extension().string());
        std::string entry = std::string(exchange::k_package_media_dir) + padded(row, rows_) + " ";
        entry += within.empty() ? std::string(name) + ext
                                : std::string(within) + "/" + padded(at, of) + " " + std::string(name) + ext;
        by_path_.emplace(key, entry);
        titles_.emplace(entry, std::string(name));
        files_.push_back({entry, path});
        return entry;
    }

    [[nodiscard]] const std::vector<exchange::LooseFile>& files() const noexcept { return files_; }
    /// What a loose file was named for.
    [[nodiscard]] std::string title_of(const std::string& entry) const
    {
        const auto it = titles_.find(entry);
        return it != titles_.end() ? it->second : entry;
    }

private:
    const MediaIndex& media_;
    std::size_t rows_;
    std::map<std::string, std::string, std::less<>> by_path_;
    std::map<std::string, std::string, std::less<>> titles_;
    std::vector<exchange::LooseFile> files_;
};

[[nodiscard]] db::MediaKind media_kind_of(model::ContentKind kind) noexcept
{
    return kind == model::ContentKind::Video   ? db::MediaKind::Video
           : kind == model::ContentKind::Audio ? db::MediaKind::Audio
                                               : db::MediaKind::Image;
}

/// Worker thread: what one row of a package is in its rundown, into ``row``.
/// False when it goes nowhere (an item with no slides to carry).
[[nodiscard]] bool package_row(PackageJob& job, const session::StackBuildContext& ctx, const model::ServiceItem& item,
                               std::size_t place, LooseFiles& loose, exchange::RundownRow& row)
{
    const auto& data = item.data;
    const auto text = [&data](const char* key) {
        const auto it = data.find(key);
        return it != data.end() && it->is_string() ? it->get<std::string>() : std::string();
    };
    const auto row_title = [&row] { return QString::fromStdString(row.title); };
    switch (row.kind) {
    case model::ContentKind::Song:
    case model::ContentKind::Bible:
    case model::ContentKind::Freeform: {
        auto doc = session::freeform_from_service_item(item, ctx);
        if (!doc || !exchange::keep_decks_of(*doc, output_ids(job.outputs))) {
            ++job.skipped;
            job.notes.push_back(ExportController::tr("“%1” has no slides to carry, so it is not in the file.").arg(row_title()));
            return false;
        }
        if (too_large(*doc)) {
            ++job.skipped;
            job.notes.push_back(
                ExportController::tr("“%1” is larger than a library item may be, so it is not in the file.").arg(row_title()));
            return false;
        }
        doc->name = row.title;
        job.source.items.push_back({row.title, {}, {}, std::move(*doc), {}});
        row.item = exchange::item_entry(job.source.items.size());
        return true;
    }
    case model::ContentKind::Image:
    case model::ContentKind::Video:
    case model::ContentKind::Audio: {
        const auto filename = text("filename");
        if (const auto path = loose.here(media_kind_of(row.kind), filename)) {
            row.files = {loose.add(*path, place, title_stem(row.title, filename))};
        } else {
            // The row goes all the same, without its file.
            job.notes.push_back(
                ExportController::tr("The file of “%1” is not on this computer, so the row goes without it.").arg(row_title()));
        }
        for (const char* key : {"autoplay", "loop"}) {
            if (const auto it = data.find(key); it != data.end() && it->is_boolean()) {
                row.data[key] = *it;
            }
        }
        return true;
    }
    case model::ContentKind::ImageFolder: {
        const auto folder_name = text("folder_name").empty() ? row.title : text("folder_name");
        const auto folder = util::portable_file_name(folder_name, "Pictures");
        const auto images = data.find("images");
        const std::size_t count = images != data.end() && images->is_array() ? images->size() : 0;
        std::size_t missing = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const auto& v = (*images)[i];
            const auto filename = v.is_string() ? v.get<std::string>() : std::string();
            const auto path = loose.here(db::MediaKind::Image, filename);
            if (!path) {
                ++missing;
                continue;
            }
            const auto title_it = job.media.picture_titles.find(filename);
            const auto title = title_it != job.media.picture_titles.end() ? title_it->second : filename;
            row.files.push_back(loose.add(*path, place, title_stem(title, filename), folder, i + 1, count));
        }
        if (missing > 0) {
            job.notes.push_back(ExportController::tr("%n of the %1 pictures of “%2” are not on this computer, so they are not in "
                                    "the file.",
                                    "", static_cast<int>(missing))
                                    .arg(count)
                                    .arg(row_title()));
        }
        row.data["folder_name"] = folder_name;
        return true;
    }
    case model::ContentKind::Web: {
        auto page = model::web_page_from_item_data(data);
        page.id.reset();  // a row of this library alone
        row.data = model::web_item_data(page);
        return true;
    }
    case model::ContentKind::Live:
        // As the row holds it, a stream's key with it: without it, the row
        // would not play where it lands.
        row.data = model::live_item_data(model::live_source_from_item_data(data));
        return true;
    case model::ContentKind::Divider:
        return true;
    case model::ContentKind::Empty:
        return false;
    }
    return false;
}

/// Worker thread: a package's rows into ``job.source`` (its items and its
/// rundown), and the files they bring.
[[nodiscard]] std::vector<exchange::LooseFile> package_rows(PackageJob& job, const session::StackBuildContext& ctx,
                                                            const ExportProgress& progress, double share)
{
    LooseFiles loose(job.media, job.rows.size());
    exchange::Rundown rundown;
    rundown.name = job.service.name;
    rundown.tags = job.service.tags;
    rundown.service_at = job.service.service_at;
    rundown.audio_fades = job.service.audio_fades;
    std::map<std::int64_t, std::size_t> place_of;  // row id -> place in the rundown
    std::vector<std::vector<std::int64_t>> also_of;
    for (std::size_t r = 0; r < job.rows.size(); ++r) {
        if (progress.stopped()) {
            return {};
        }
        const auto& item = job.rows[r];
        exchange::RundownRow row;
        row.kind = item.kind();
        row.title = item.title();
        if (!package_row(job, ctx, item, r + 1, loose, row)) {
            continue;
        }
        if (const auto stack = model::service_item_stack_id(item); !stack.empty()) {
            row.stack_id = stack;
            const auto it = job.stack_names.find(stack);
            row.stack_name = it != job.stack_names.end() ? it->second : std::string();
        }
        if (item.item_id) {
            place_of[*item.item_id] = rundown.rows.size();
        }
        also_of.push_back(model::service_item_also_play(item));
        rundown.rows.push_back(std::move(row));
        progress.report(share * static_cast<double>(r + 1) / static_cast<double>(job.rows.size()));
    }
    for (std::size_t r = 0; r < rundown.rows.size(); ++r) {
        for (const auto id : also_of[r]) {
            if (const auto it = place_of.find(id); it != place_of.end() && it->second != r) {
                rundown.rows[r].also_play.push_back(it->second);
            }
        }
    }
    job.source.rundown = std::move(rundown);
    for (const auto& f : loose.files()) {
        job.loose_titles.emplace(f.name, loose.title_of(f.name));
    }
    return loose.files();
}

/// Removes the files it is given unless told they were kept.
class StagedFiles {
public:
    StagedFiles() = default;
    StagedFiles(const StagedFiles&) = delete;
    StagedFiles& operator=(const StagedFiles&) = delete;
    ~StagedFiles()
    {
        for (const auto& path : paths_) {
            std::error_code ec;
            fs::remove(path, ec);
        }
    }
    /// ``path``, to be removed; a copy, as the list grows.
    fs::path add(fs::path path)
    {
        paths_.push_back(path);
        return path;
    }
    void keep(const fs::path& path) { std::erase(paths_, path); }

private:
    std::vector<fs::path> paths_;
};

/// Worker thread: each item's preview, from ``from`` to ``to`` of the way.
[[nodiscard]] bool draw_previews(BundleJob& job, const session::StackBuildContext& ctx, PreviewResources& previews,
                                 const ExportProgress& progress, double from, double to)
{
    auto& items = job.source.items;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (progress.stopped()) {
            return false;
        }
        items[i].preview_png = preview_png(items[i].document, ctx, previews);
        progress.report(from + (to - from) * static_cast<double>(i + 1) / static_cast<double>(items.size()));
    }
    return true;
}

/// Worker thread. Freeform file ``job`` at ``job.file``.
[[nodiscard]] ExportOutcome write_freeform_file(BundleJob& job, PreviewResources& previews,
                                                const ExportProgress& progress)
{
    ExportOutcome out;
    out.path = path_text(job.file);
    out.skipped = job.skipped;
    out.notes = job.notes;
    const session::StackBuildContext ctx{previews.measurer(), job.outputs, job.theme_priority,
                                         job.ccli_licence_number, job.service_themes};
    if (!draw_previews(job, ctx, previews, progress, 0.0, 0.15)) {
        out.stopped = true;
        return out;
    }
    StagedFiles staged;
    const auto bundle = staged.add(staging_path(job.file.parent_path()));
    const auto written = exchange::write_bundle(job.source, bundle, bundle_progress(progress, 0.15, 1.0));
    if (written.stopped || progress.stopped()) {
        out.stopped = true;
        return out;
    }
    if (!written.ok) {
        out.errors.push_back(QString::fromStdString(written.error));
        return out;
    }
    note_left_out(written, out);
    if (!util::move_file_durably(bundle, job.file)) {
        out.errors.push_back(ExportController::tr("Could not save %1.").arg(path_text(job.file)));
        return out;
    }
    staged.keep(bundle);
    out.count = static_cast<int>(job.source.items.size());
    return out;
}

/// Worker thread. Package ``job`` at ``job.file``: its rows converted, its
/// Freeform file written under a hidden name, then packed with the rows'
/// files.
[[nodiscard]] ExportOutcome write_package_file(PackageJob& job, PreviewResources& previews,
                                               const ExportProgress& progress)
{
    ExportOutcome out;
    out.path = path_text(job.file);
    const session::StackBuildContext ctx{previews.measurer(), job.outputs, job.theme_priority,
                                         job.ccli_licence_number, job.service_themes};
    const auto loose = package_rows(job, ctx, progress, 0.15);
    if (progress.stopped()) {
        out.stopped = true;
        return out;
    }
    // The items' files are found here: what a conversion names (a theme's
    // own picture) is known only now.
    for (const auto& item : job.source.items) {
        find_files(job.media, item.document, job.source.media, nullptr);
    }
    find_fonts(job.fonts, job.media.media_root, job.source);
    out.skipped = job.skipped;
    out.notes = job.notes;
    if (!job.source.rundown || job.source.rundown->rows.empty()) {
        out.errors.push_back(ExportController::tr("This service has nothing in it to export."));
        return out;
    }
    if (!draw_previews(job, ctx, previews, progress, 0.15, 0.25)) {
        out.stopped = true;
        return out;
    }

    StagedFiles staged;
    const auto dir = job.file.parent_path();
    const auto bundle = staged.add(staging_path(dir));
    const auto written = exchange::write_bundle(job.source, bundle, bundle_progress(progress, 0.25, 0.6));
    if (written.stopped || progress.stopped()) {
        out.stopped = true;
        return out;
    }
    if (!written.ok) {
        out.errors.push_back(QString::fromStdString(written.error));
        return out;
    }
    note_left_out(written, out);
    const auto zip = staged.add(staging_path(dir));
    const auto bundle_name =
        util::portable_file_name(job.service.name, "Service") + std::string(exchange::k_bundle_extension);
    const auto packed = exchange::write_package(zip, bundle_name, bundle, loose, bundle_progress(progress, 0.6, 1.0));
    if (packed.stopped || progress.stopped()) {
        out.stopped = true;
        return out;
    }
    if (!packed.ok) {
        out.errors.push_back(QString::fromStdString(packed.error));
        return out;
    }
    for (const auto& name : packed.media_not_included) {
        const auto it = job.loose_titles.find(name);
        out.notes.push_back(ExportController::tr("“%1” could not be read, so it is not in the file.")
                                .arg(QString::fromStdString(it != job.loose_titles.end() ? it->second : name)));
    }
    if (!util::move_file_durably(zip, job.file)) {
        out.errors.push_back(ExportController::tr("Could not save %1.").arg(path_text(job.file)));
        return out;
    }
    staged.keep(zip);
    out.count = static_cast<int>(job.source.rundown ? job.source.rundown->rows.size() : 0);
    return out;
}

/// Where a Freeform file or package goes: ``raw``, ending in ``extension``
/// (one of ``allowed``; anything else has ``extension`` added), in a folder
/// that exists. Nullopt with ``why`` otherwise.
[[nodiscard]] std::optional<fs::path> exchange_path(const QString& raw, std::string_view extension,
                                                    std::initializer_list<std::string_view> allowed, QString& why)
{
    auto file = archive_path(raw, why, extension);
    if (!file) {
        return std::nullopt;
    }
    const auto ext = file->extension().string();
    if (std::none_of(allowed.begin(), allowed.end(), [&](std::string_view a) { return util::iequals(ext, a); })) {
        *file += std::string(extension);
    }
    return file;
}

/// The library's files on their way from another computer among
/// ``sync_paths`` (``images/<name>``), and its fonts' (when ``fonts``).
[[nodiscard]] QString still_coming(MediaController& media, const std::vector<std::string>& sync_paths,
                                   const std::vector<db::LibraryFont>* fonts, const fs::path& media_root)
{
    for (const auto& path : sync_paths) {
        if (media.on_its_way(path)) {
            return ExportController::tr("Some of its pictures or videos are still on their way from another computer. "
                                         "Export once they have come.");
        }
    }
    if (fonts != nullptr) {
        for (const auto& font : *fonts) {
            std::error_code ec;
            const auto sync_path = std::string(model::k_fonts_subdir) + "/" + font.filename;
            if (!fs::exists(media_root / sync_path, ec) && media.on_its_way(sync_path)) {
                return ExportController::tr("The library's font “%1” is still on its way from another computer. "
                                             "Export once it has come, so any words in it are laid out in it.")
                    .arg(QString::fromStdString(font.family));
            }
        }
    }
    return {};
}

/// What every Freeform file and package begins with: the outputs and how
/// they theme, and who made it, read off the session and the library.
struct BundleHeader {
    std::vector<model::OutputConfig> outputs;
    std::vector<std::string> theme_priority;
    std::string ccli_licence_number;
    std::string made_by;
    std::string made_at;
    std::string source_library;

    BundleHeader(const session::Session& session, OpenLibrary& library)
        : outputs(session.outputs())
        , theme_priority(session.theme_priority())
        , ccli_licence_number(session.ccli_licence_number())
        , made_by("SeventhSlide " + std::string(version_string()))
        , made_at(QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString())
    {
        if (auto* db = library.database()) {
            source_library = db::library_uid(*db);
        }
    }

    /// Into ``job``: its outputs and themes, and its file's header.
    void fill(BundleJob& job) const
    {
        job.outputs = outputs;
        job.theme_priority = theme_priority;
        job.ccli_licence_number = ccli_licence_number;
        job.source.made_by = made_by;
        job.source.made_at = made_at;
        job.source.source_library = source_library;
        for (const auto& oc : outputs) {
            job.source.outputs.push_back({oc.id, oc.name, model::output_canvas(oc)});
        }
    }
};

}  // namespace

qint64 ExportController::exportFreeformFile(const QVariantList& itemIds, const QVariantList& folderIds,
                                            const QString& file)
try {
    auto* repo = freeform_.repo();
    auto* db = open_library_.database();
    const auto items = parse_id_list(itemIds);
    const auto folders = parse_id_list(folderIds);
    if (repo == nullptr || db == nullptr) {
        return refuse(refusal::no_library());
    }
    if (!items || !folders || (items->empty() && folders->empty())) {
        return refuse(tr("Choose the Freeform items to export."));
    }
    QString why;
    const auto extension = exchange::bundle_extension_for(items->size(), !folders->empty());
    const auto path = exchange_path(file, extension, {exchange::k_single_extension, exchange::k_bundle_extension}, why);
    if (!path) {
        return refuse(why);
    }
    auto job = std::make_shared<BundleJob>();
    job->file = *path;
    const BundleHeader header(session_, open_library_);
    if (header.outputs.empty()) {
        return refuse(tr("There are no outputs for the slides to go to."));
    }
    header.fill(*job);
    auto& source = job->source;
    const auto ids = output_ids(job->outputs);
    const auto index = media_index(open_library_, media_);
    std::vector<std::string> missing;

    const auto add_item = [&](std::int64_t id, const std::string& folder_key) {
        const auto item = repo->get_item(id);
        if (!item) {
            return;
        }
        const auto name = QString::fromStdString(item->name);
        auto doc = freeform::load_document(item->document, item->name);
        if (!doc) {
            ++job->skipped;
            const auto refused = freeform::document_refusal(item->document);
            job->notes.push_back(refused.empty()
                                     ? tr("“%1” cannot be read: it is damaged.").arg(name)
                                     : tr("“%1” cannot be read. %2").arg(name, QString::fromStdString(refused)));
            return;
        }
        if (!exchange::keep_decks_of(*doc, ids)) {
            ++job->skipped;
            job->notes.push_back(tr("“%1” has no slides for this library's outputs.").arg(name));
            return;
        }
        if (too_large(*doc)) {
            ++job->skipped;
            job->notes.push_back(tr("“%1” is larger than a library item may be.").arg(name));
            return;
        }
        find_files(index, *doc, source.media, &missing);
        source.items.push_back({item->name, folder_key, item->tags, std::move(*doc), {}});
    };

    // The folders first, each with all beneath it, so an item chosen and in
    // a folder chosen keeps its folder. A folder inside another chosen is
    // walked as part of it.
    const auto tree = repo->folders();
    const std::set<std::int64_t> chosen(folders->begin(), folders->end());
    const auto inside_chosen = [&](std::int64_t id) {
        auto at = tree.get(id);
        for (std::size_t steps = 0; at && at->parent_id && steps < 1000; ++steps) {
            if (chosen.contains(*at->parent_id)) {
                return true;
            }
            at = tree.get(*at->parent_id);
        }
        return false;
    };
    std::set<std::int64_t> seen;
    std::set<std::int64_t> seen_folders;
    const auto add_folder = [&](const auto& self, std::int64_t folder_id, const std::string& parent) -> void {
        if (!seen_folders.insert(folder_id).second) {
            return;
        }
        const auto folder = tree.get(folder_id);
        const std::string key = "f" + std::to_string(folder_id);
        source.folders.push_back({key, folder ? folder->name : std::string("Folder"), parent});
        for (const auto& row : repo->list_browser_rows(folder_id)) {
            if (seen.insert(row.id).second) {
                add_item(row.id, key);
            }
        }
        for (const auto& child : tree.children(folder_id)) {
            if (child.id) {
                self(self, *child.id, key);
            }
        }
    };
    for (const auto id : *folders) {
        if (!inside_chosen(id)) {
            add_folder(add_folder, id, {});
        }
    }
    for (const auto id : *items) {
        if (seen.insert(id).second) {
            add_item(id, {});
        }
    }
    if (source.items.empty()) {
        return refuse(job->notes.isEmpty() ? tr("There is nothing in those folders to export.")
                                           : job->notes.join(QStringLiteral("\n")));
    }
    db::FontRepository fonts(*db);
    const auto library_fonts = fonts.list_all();
    find_fonts(library_fonts, index.media_root, source);
    std::vector<db::LibraryFont> named_fonts;
    named_fonts.reserve(source.fonts.size());
    for (const auto& f : source.fonts) {
        named_fonts.push_back(f.font);
    }
    if (const auto coming = still_coming(media_, missing, &named_fonts, index.media_root); !coming.isEmpty()) {
        return refuse(coming);
    }
    for (const auto& [ref, media] : source.media) {
        if (!media.path.empty()) {
            std::error_code ec;
            const auto size = fs::file_size(media.path, ec);
            const auto kind = media.kind == canvas::MediaRefKind::Video ? db::MediaKind::Video : db::MediaKind::Image;
            if (!ec && size > db::max_import_bytes(kind)) {
                job->notes.push_back(tr("“%1” is larger than a library takes in, so the library it "
                                        "goes to will leave it out.")
                                         .arg(QString::fromStdString(media.title)));
            }
        }
    }
    const auto folder = job->file.parent_path();
    return start(folder, [job, &previews = previews_](const ExportProgress& progress) {
        return write_freeform_file(*job, previews, progress);
    });
} catch (const std::exception& ex) {
    return refuse(report_entry_error(__func__, ex));
}

qint64 ExportController::exportServicePackage(qint64 serviceId, const QVariant& themeMap, const QString& file)
try {
    auto* repo = library_.service_repo();
    auto* db = open_library_.database();
    if (repo == nullptr || db == nullptr) {
        return refuse(refusal::no_library());
    }
    const auto service = serviceId >= 0 ? repo->get_service(serviceId) : std::nullopt;
    if (!service) {
        return refuse(refusal::service_gone());
    }
    QString why;
    const auto path = exchange_path(file, exchange::k_package_extension, {exchange::k_package_extension}, why);
    if (!path) {
        return refuse(why);
    }
    auto job = std::make_shared<PackageJob>();
    job->file = *path;
    const BundleHeader header(session_, open_library_);
    if (header.outputs.empty()) {
        return refuse(tr("There are no outputs for the slides to go to."));
    }
    header.fill(*job);
    // The picks being edited when the dialog passes them, else the stored ones.
    job->service_themes = theme::service_themes_from_json(
        themeMap.isValid() && !themeMap.isNull() ? to_json(themeMap) : service->theme_map);
    job->service = *service;
    job->rows = repo->list_items(serviceId);
    if (job->rows.empty()) {
        return refuse(tr("This service has nothing in it to export."));
    }
    for (const auto& def : session_.stack_defs()) {
        job->stack_names.emplace(def.id, def.name);
    }
    job->media = media_index(open_library_, media_);
    job->fonts = db::FontRepository(*db).list_all();
    // Its rows' own files, and every font (a theme's are known only once
    // the rows are converted), must be here first.
    std::vector<std::string> sync_paths;
    for (const auto& item : job->rows) {
        const auto kind = item.kind();
        const auto add = [&](db::MediaKind media_kind, const nlohmann::json& name) {
            if (name.is_string() && plain_name(name.get<std::string>())) {
                const auto sync_path = std::string(db::media_subdir(media_kind)) + "/" + name.get<std::string>();
                std::error_code ec;
                if (!fs::exists(job->media.media_root / sync_path, ec)) {
                    sync_paths.push_back(sync_path);
                }
            }
        };
        if (kind == model::ContentKind::Freeform) {
            // A Freeform row's document names its own files, known now.
            if (const auto doc = item.data.find("document"); doc != item.data.end()) {
                if (auto loaded = freeform::load_document(*doc, item.title())) {
                    std::map<std::string, exchange::FileToPack, std::less<>> found;
                    find_files(job->media, *loaded, found, &sync_paths);
                }
            }
        } else if (kind == model::ContentKind::Image || kind == model::ContentKind::Video
                   || kind == model::ContentKind::Audio) {
            add(media_kind_of(kind), item.data.value("filename", nlohmann::json()));
        } else if (kind == model::ContentKind::ImageFolder && item.data.contains("images")
                   && item.data["images"].is_array()) {
            for (const auto& name : item.data["images"]) {
                add(db::MediaKind::Image, name);
            }
        }
    }
    if (const auto coming = still_coming(media_, sync_paths, &job->fonts, job->media.media_root); !coming.isEmpty()) {
        return refuse(coming);
    }
    const auto folder = job->file.parent_path();
    return start(folder, [job, &previews = previews_](const ExportProgress& progress) {
        return write_package_file(*job, previews, progress);
    });
} catch (const std::exception& ex) {
    return refuse(report_entry_error(__func__, ex));
}

}  // namespace seventhslide::app
