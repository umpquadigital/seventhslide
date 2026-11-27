// The media library: images, videos and audio.
//
// Owns the media repository, the folder trees, the browser models and the thumbnail
// cache (see media_controller.hpp for what it borrows).

#include "media_controller.hpp"
#include "media_library.hpp"

#include "rundown.hpp"

#include "entry_error.hpp"
#include "refusals.hpp"

#include "qvariant_json.hpp"
#include "local_file_path.hpp"
#include "media_thumb_cache.hpp"
#include "qml_ids.hpp"

#include <seventhslide/db/media_kind.hpp>
#include <seventhslide/db/media_listing.hpp>
#include <seventhslide/freeform/own_media.hpp>
#include <seventhslide/model/static_media.hpp>
#include <seventhslide/platform/paths.hpp>
#include <seventhslide/util/atomic_file.hpp>
#include <seventhslide/util/leaf_filename.hpp>

#include <QCoreApplication>
#include <QMetaObject>
#include <QSettings>
#include <QThread>
#include <QUrl>
#include <QUuid>
#include <QVariant>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace seventhslide::app {

namespace {

// Icons or list follows the machine, not the library: it is how this
// operator likes to browse on this screen, so it is the one preference kept
// in QSettings. Everything else is stored per library, in its database.
const QString k_view_mode_setting = QStringLiteral("mediaBrowser/viewMode");

/// ``icons`` stays ``icons``; anything else (including unset) is ``list``.
[[nodiscard]] QString normalized_view_mode(const QString& mode)
{
    return mode.trimmed().toLower() == QLatin1String("icons") ? QStringLiteral("icons")
                                                              : QStringLiteral("list");
}

/// Whether ``kind``'s files have thumbnails: an image's own picture, a
/// video's frame. Sound shows its glyph.
[[nodiscard]] constexpr bool has_thumbs(db::MediaKind kind) noexcept
{
    return kind == db::MediaKind::Image || kind == db::MediaKind::Video;
}

/// Whether a video's thumbs are on disk. Until they are, its row has no
/// thumb URL and shows the glyph, so a file with no picture never asks.
[[nodiscard]] bool video_thumbs_made(const std::filesystem::path& media_dir,
                                     std::string_view filename)
{
    std::error_code ec;
    return std::filesystem::is_regular_file(
        video_thumb_path(media_dir, filename, k_media_thumb_tier_sm), ec);
}

/// Delete the thumbs of one file of ``kind``.
void remove_thumbs(const std::filesystem::path& media_dir, db::MediaKind kind,
                   std::string_view filename)
{
    if (kind == db::MediaKind::Image) {
        remove_media_thumbs(media_dir, filename);
    } else if (kind == db::MediaKind::Video) {
        remove_video_thumbs(media_dir, filename);
    }
}

/// Why a file imported as ``kind`` was not, as its error says it after its
/// name ("notes.txt: ..."), as file_importer.hpp's reasons are. Each kind's
/// words are whole, for a translation to word as its language does.
[[nodiscard]] QString not_a_kind_of(db::MediaKind kind)
{
    switch (kind) {
    case db::MediaKind::Image:
        return MediaController::tr("not a picture SeventhSlide can use");
    case db::MediaKind::Video:
        return MediaController::tr("not a video SeventhSlide can use");
    case db::MediaKind::Audio:
        return MediaController::tr("not an audio file SeventhSlide can use");
    }
    return MediaController::tr("not a file SeventhSlide can use");
}

[[nodiscard]] QVariantMap import_result(const QStringList& errors, const QStringList& filenames)
{
    QVariantMap out;
    out.insert(QStringLiteral("ok"), errors.isEmpty());
    out.insert(QStringLiteral("count"), static_cast<int>(filenames.size()));
    out.insert(QStringLiteral("errors"), errors);
    out.insert(QStringLiteral("filenames"), filenames);
    return out;
}

[[nodiscard]] QString file_error(const ImportFile& file, const std::string& why)
{
    return QString::fromStdString(file.display) + QStringLiteral(": ")
           + QString::fromStdString(why);
}

}  // namespace

void MediaController::refresh()
{
    push_audio_fades();
    if (media_repo_) {
        image_model_.set_images(media_repo_->list_files(db::MediaKind::Image, true));
        rebuild_media_browsers();
    } else {
        image_model_.set_images({});
        for (auto* lib : libraries_) {
            lib->browser()->set_entries({});
        }
        emit changed();
    }
}

void MediaController::refresh(db::MediaKind kind)
{
    refresh_listing(kind, Reread::Files);
}

void MediaController::on_library_switched()
{
    ++library_revision_;
    warm_queued_.clear();
    set_arrivals({});
}

void MediaController::files_arrived(db::MediaKind kind, const std::vector<std::string>& names)
{
    // Warmed again: a warmer that found the file missing made nothing, and a
    // video's row has no picture until it has.
    const auto subdir = std::string(db::media_subdir(kind)) + "/";
    bool arrivals = false;
    for (const auto& name : names) {
        warm_queued_.erase(name);
        arrivals = arrivals_.erase(subdir + name) > 0 || arrivals;
    }
    if (arrivals) {
        ++arrivals_revision_;
        emit arrivalsChanged();
    }
    ++files_revision_;
    refresh_listing(kind, Reread::FoldersOnly);
    emit filesArrived();
}

void MediaController::push_audio_fades()
{
    if (media_repo_) {
        session_.set_audio_file_fades(media_repo_->audio_fades_by_file());
    } else {
        session_.set_audio_file_fades({});
    }
}

void MediaController::refresh_listing(db::MediaKind kind, Reread what)
{
    if (kind == db::MediaKind::Audio && what == Reread::Files) {
        push_audio_fades();
    }
    if (what == Reread::Files && media_repo_ && kind == db::MediaKind::Image) {
        image_model_.set_images(media_repo_->list_files(db::MediaKind::Image, true));
    }
    rebuild_media_browser(kind);
    emit changed();
}

void MediaController::cancel_thumb_warmers()
{
    warm_thumbs_pool_.clear();
    warm_thumbs_cancelled_->store(true);
    // Jobs queued from now on belong to a new batch.
    warm_thumbs_cancelled_ = std::make_shared<std::atomic<bool>>(false);
    warm_queued_.clear();
}

void MediaController::attach(db::Database& db)
{
    // An import belongs to the library it was started in.
    importer_.cancel_all();
    media_repo_ = std::make_unique<db::MediaRepository>(db);
    for (const auto kind : db::k_media_kinds) {
        library_for(kind).folder_browser().attach(media_repo_->folders(kind));
    }
}

void MediaController::detach()
{
    // Their files are meant for this library and its database.
    importer_.cancel_all();
    media_repo_.reset();
    push_audio_fades();
    image_model_.set_images({});
    for (auto* lib : libraries_) {
        lib->folder_browser().detach();
        lib->browser()->set_entries({});
    }
    emit changed();
}

MediaController::MediaController(OpenLibrary& library,
                                 session::Session& session,
                                 IRundown& rundown,
                                 QObject* parent)
    : QObject(parent)
    , library_(library)
    , session_(session)
    , rundown_(rundown)
    , image_model_(this)
    , media_view_mode_(normalized_view_mode(QSettings().value(k_view_mode_setting).toString()))
    , importer_([this] { emit importProgressChanged(); })
{
    // Parented, so QML never takes ownership of one it is handed. What a
    // library says goes out as the controller's, and the controller's
    // ``changed`` reaches every library's ``folderId``.
    for (const auto kind : db::k_media_kinds) {
        auto* lib = new MediaLibrary(*this, kind, this);
        libraries_[db::media_index(kind)] = lib;
        connect(lib, &FolderLibrary::errorOccurred, this, &MediaController::errorOccurred);
        connect(this, &MediaController::changed, lib, &FolderLibrary::changed);
    }
    // One warmer at a time: the work is mostly disk, and a pool sized to the
    // CPU only competes with the UI for it.
    warm_thumbs_pool_.setMaxThreadCount(1);
}

MediaLibrary* MediaController::library(MediaKindQml::Kind kind)
{
    const auto mk = MediaKindQml::to_db(kind);
    return mk ? &library_for(*mk) : nullptr;
}

MediaBrowserModel* MediaController::browser(db::MediaKind kind) noexcept
{
    return library_for(kind).browser();
}

qint64 MediaController::folder_id(db::MediaKind kind) const noexcept
{
    return library_for(kind).folderId();
}

MediaController::~MediaController()
{
    cancel_thumb_warmers();
    warm_thumbs_pool_.waitForDone();
}

bool MediaController::playImageFolder(qint64 folder_id, int start_index)
try {
    if (!media_repo_ || folder_id < 0) {
        return false;
    }
    auto data = image_folder_data(folder_id);
    if (!data) {
        return false;
    }
    session::LiveMediaContent media;
    media.kind = model::ContentKind::ImageFolder;
    media.data = std::move(*data);
    const auto landed = session_.play_media(std::move(media));
    if (start_index > 0) {
        (void)session_.runtime(landed)->set_image_index(start_index);
    }
    emit liveChanged(true);
    return true;
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return false;
}

void MediaController::remove_library_files(db::MediaKind kind,
                                           const std::vector<std::string>& filenames)
{
    if (!media_repo_ || filenames.empty()) {
        return;
    }
    std::vector<std::string> names;
    names.reserve(filenames.size());
    for (const auto& raw : filenames) {
        if (auto fn = util::leaf_filename(raw); !fn.empty()) {
            names.push_back(std::move(fn));
        }
    }
    if (names.empty()) {
        return;
    }
    // One pass over the stored services and Freeforms for the whole selection.
    // Only images are kept for what still shows them; a kind that refuses
    // instead was checked by the caller.
    const auto shown = db::hidden_when_in_use(kind) ? media_repo_->referenced_images(names)
                                                    : std::unordered_set<std::string>{};

    std::vector<std::string> removed;
    {
        db::Transaction tx(*library_.database());
        for (const auto& fn : names) {
            media_repo_->unlink_filename_from_folders(kind, fn);
            if (shown.contains(fn)) {
                // Still shown by a service or a Freeform: hide it from the library
                // but keep the file (and the operator's display name).
                if (!media_repo_->get_file(kind, fn)) {
                    media_repo_->upsert_file(kind, fn, fn);
                }
                media_repo_->set_file_visible(kind, fn, false);
                continue;
            }
            media_repo_->remove_file(kind, fn);
            removed.push_back(fn);
        }
        tx.commit();
    }

    // Files go only once the rows are committed, so a failed delete cannot
    // leave rows pointing at missing files.
    const auto media_dir = library_.media_root();
    if (media_dir.empty()) {
        return;
    }
    const auto dir = media_dir / db::media_subdir(kind);
    for (const auto& fn : removed) {
        // Windows will not remove a file something has open (the thumbnailer
        // reading one just added, a virus scanner); remove_file waits for it
        // a moment. One that stays is out of the library, but on the disk.
        if (!util::remove_file(dir / fn)) {
            qWarning("SeventhSlide: could not remove %s; it is still in the media folder",
                     (dir / fn).string().c_str());
        }
        remove_thumbs(media_dir, kind, fn);
    }
}

QString MediaController::delete_media_file(db::MediaKind kind, const QString& filename)
{
    if (!media_repo_) {
        return refusal::no_library();
    }
    const auto fn = util::leaf_filename(filename.toStdString());
    if (fn.empty()) {
        return refusal::media_file_gone();
    }
    if (auto why = in_service_refusal(kind, {fn}, false); !why.isEmpty()) {
        return why;
    }
    remove_library_files(kind, {fn});
    refresh_listing(kind, Reread::Files);
    return {};
}

QString MediaController::delete_folder(db::MediaKind kind, std::int64_t folder_id)
{
    // Checked up front so a refusal leaves the whole folder untouched.
    const auto in_folder = media_repo_->list_folder_tree_filenames(kind, folder_id);
    if (auto why = in_service_refusal(kind, in_folder, true); !why.isEmpty()) {
        return why;
    }
    const auto filenames = media_repo_->delete_folder_tree(kind, folder_id);
    // Same rules as deleting the files one by one. A file still filed in a
    // folder outside the deleted tree is left alone.
    const auto still_linked = media_repo_->list_linked_filenames(kind);
    std::vector<std::string> released;
    for (const auto& fn : filenames) {
        if (!still_linked.contains(fn)) {
            released.push_back(fn);
        }
    }
    remove_library_files(kind, released);
    refresh_listing(kind, Reread::Files);
    return {};
}

QString MediaController::in_service_refusal(db::MediaKind kind,
                                            const std::vector<std::string>& filenames,
                                            bool in_folder)
{
    // An image in use is hidden instead (remove_library_files).
    if (db::hidden_when_in_use(kind) || filenames.empty()) {
        return {};
    }
    // Only videos and audio files get this far: one that is not a video is
    // an audio file.
    const bool video = kind == db::MediaKind::Video;
    if (in_folder) {
        const auto in_use =
            static_cast<int>(media_repo_->referenced_playback_files(kind, filenames).size());
        if (in_use == 0) {
            return {};
        }
        return video ? tr("%n video(s) in this folder are used by service items; remove them "
                          "from those services first.",
                          nullptr, in_use)
                     : tr("%n audio file(s) in this folder are used by service items; remove "
                          "them from those services first.",
                          nullptr, in_use);
    }
    for (const auto& fn : filenames) {
        const int refs = media_repo_->count_playback_refs(kind, fn);
        if (refs > 0) {
            return video ? tr("This video is used by %n service item(s); remove it from those "
                              "services first.",
                              nullptr, refs)
                         : tr("This audio file is used by %n service item(s); remove it from "
                              "those services first.",
                              nullptr, refs);
        }
    }
    return {};
}

nlohmann::json MediaController::item_data(db::MediaKind kind,
                                          const std::string& filename,
                                          bool loop) const
{
    const auto title = title_of(kind, filename);
    return kind == db::MediaKind::Image ? model::image_item_data(filename, title)
                                        : model::playback_item_data(filename, title, loop);
}

std::string MediaController::title_of(db::MediaKind kind, const std::string& filename) const
{
    const auto file = media_repo_ ? media_repo_->get_file(kind, filename) : std::nullopt;
    return file ? file->title() : filename;
}

bool MediaController::addImageToService(const QString& filename, int at_index)
try {
    const auto fn = util::leaf_filename(filename.toStdString());
    if (fn.empty()) {
        return false;
    }
    const auto idx = optional_index(at_index);
    return rundown_.append_service_item(model::k_item_type_image,
                                        item_data(db::MediaKind::Image, fn), std::nullopt, idx);
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return false;
}

bool MediaController::addImageFolderToService(qint64 folder_id, int at_index)
try {
    if (!media_repo_ || folder_id < 0) {
        return false;
    }
    auto data = image_folder_data(folder_id);
    if (!data) {
        return false;
    }
    const auto idx = optional_index(at_index);
    return rundown_.append_service_item(model::k_item_type_image_folder, std::move(*data),
                                        std::nullopt, idx);
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return false;
}

bool MediaController::addVideoFolderToService(qint64 folder_id, int at_index)
try {
    return add_playback_folder(db::MediaKind::Video, folder_id, at_index);
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return false;
}

bool MediaController::addAudioFolderToService(qint64 folder_id, int at_index)
try {
    return add_playback_folder(db::MediaKind::Audio, folder_id, at_index);
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return false;
}

bool MediaController::add_playback_folder(db::MediaKind kind, qint64 folder_id, int at_index)
{
    if (!media_repo_ || folder_id < 0) {
        return false;
    }
    std::vector<std::string> files;
    for (const auto& item : media_repo_->list_folder_items(kind, folder_id)) {
        files.push_back(item.filename);
    }
    return add_playback_items(kind, std::move(files), at_index);
}

bool MediaController::add_playback_items(db::MediaKind kind,
                                         std::vector<std::string> filenames,
                                         int at_index)
{
    std::vector<nlohmann::json> items;
    for (const auto& fn : filenames) {
        if (!fn.empty()) {
            items.push_back(item_data(kind, fn));
        }
    }
    const auto type = kind == db::MediaKind::Audio ? model::k_item_type_audio
                                                   : model::k_item_type_video;
    return rundown_.append_service_items(type, items, optional_index(at_index));
}

std::optional<nlohmann::json> MediaController::image_folder_data(std::int64_t folder_id) const
{
    const auto folder = media_repo_->folders(db::MediaKind::Image).get(folder_id);
    if (!folder) {
        return std::nullopt;
    }
    nlohmann::json images = nlohmann::json::array();
    for (const auto& item : media_repo_->list_folder_items(db::MediaKind::Image, folder_id)) {
        if (!item.filename.empty()) {
            images.push_back(item.filename);
        }
    }
    return nlohmann::json{
        {"folder_id", folder_id},
        {"folder_name", folder->name.empty() ? "Image Folder" : folder->name},
        {"images", std::move(images)},
    };
}

std::optional<MediaController::MediaPlace> MediaController::place_of(db::MediaKind kind,
                                                                     const std::string& ref) const
{
    if (ref.empty()) {
        return std::nullopt;
    }
    MediaPlace place;
    if (auto own = freeform::own_media_of(ref)) {
        place.path = freeform::own_media_path(library_.media_root(), *own);
        place.sync_path = std::string(model::k_freeform_media_subdir) + "/" + own->place();
        place.leaf = own->name;
        place.own = std::move(own);
        return place;
    }
    place.leaf = util::leaf_filename(ref);
    if (place.leaf.empty()) {
        return std::nullopt;
    }
    place.path = library_.media_root() / db::media_subdir(kind) / place.leaf;
    place.sync_path = std::string(db::media_subdir(kind)) + "/" + place.leaf;
    return place;
}

QString MediaController::file_url_for_media(db::MediaKind kind, const std::string& filename) const
{
    if (filename.empty() || library_.media_root().empty()) {
        return {};
    }
    const auto place = place_of(kind, filename);
    std::error_code ec;
    if (!place || !std::filesystem::is_regular_file(place->path, ec)) {
        return {};
    }
    return QUrl::fromLocalFile(QString::fromStdString(place->path.string())).toString();
}

QString MediaController::media_thumb_url_for(db::MediaKind kind, const std::string& filename) const
{
    if (!has_thumbs(kind) || filename.empty() || library_.media_root().empty()) {
        return {};
    }
    const auto place = place_of(kind, filename);
    if (!place) {
        return {};
    }
    // A missing file gets no URL, so its row shows the glyph. One stat per
    // row: whether the file stays inside the media folder is the provider's
    // check, made when a thumbnail is actually loaded.
    std::error_code ec;
    if (!std::filesystem::is_regular_file(place->path, ec)) {
        return {};
    }
    // An image is read on demand; a video's frame only once the warming pool
    // has made it (and the listing is rebuilt then).
    if (kind == db::MediaKind::Video && !video_thumbs_made(library_.media_root(), place->leaf)) {
        return {};
    }
    // A Freeform's own file goes by its URL, which says whose it is.
    const auto id = place->own ? filename : place->leaf;
    // ``v`` busts both the QML pixmap cache and the provider LRU when the active
    // library changes - two libraries can hold different files under one name.
    return QStringLiteral("image://mediathumb/%1/%2?v=%3")
        .arg(QString::fromUtf8(db::media_subdir(kind)),
             QString::fromUtf8(QUrl::toPercentEncoding(QString::fromStdString(id))))
        .arg(library_revision_);
}

void MediaController::rebuild_media_browsers()
{
    for (const auto kind : db::k_media_kinds) {
        rebuild_media_browser(kind);
    }
    emit changed();
}

void MediaController::rebuild_media_browser(db::MediaKind kind)
{
    auto& lib = library_for(kind);
    auto& browser = lib.folder_browser();
    auto& model = *lib.browser();
    // Every caller announces ``changed`` after the rebuild.
    (void)browser.drop_missing();
    if (!media_repo_) {
        model.set_entries({});
        return;
    }

    // Sound has no thumbnails; its tiles show a glyph.
    const bool thumbs = has_thumbs(kind);
    const auto listing =
        db::list_media(*media_repo_, kind, browser.folder(), browser.query().toStdString());

    auto entries = browser.folder_entries(listing.folders, media_repo_->folder_item_counts(kind));
    entries.reserve(entries.size() + listing.files.size());

    std::vector<std::string> warm_filenames;
    for (const auto& file : listing.files) {
        MediaBrowserEntry e;
        e.kind = MediaBrowserEntry::Kind::File;
        e.id = file.item_id;
        e.name = file.title;
        e.filename = file.filename;
        if (thumbs) {
            e.thumb_url = media_thumb_url_for(kind, file.filename).toStdString();
            if (warm_queued_.insert(file.filename).second) {
                warm_filenames.push_back(file.filename);
            }
        }
        entries.push_back(std::move(e));
    }

    model.set_entries(std::move(entries), browser.view());
    if (thumbs) {
        schedule_warm_media_thumbs(kind, std::move(warm_filenames));
    }
}

void MediaController::schedule_warm_media_thumbs(db::MediaKind kind,
                                                 std::vector<std::string> filenames,
                                                 const std::string& owner)
{
    if (filenames.empty()) {
        return;
    }
    const auto media_dir = library_.media_root();
    if (media_dir.empty()) {
        return;
    }
    // Queued, not dropped, until cancel_thumb_warmers: closing a library
    // cancels its batch, so a job never writes into an outgoing library's
    // media folder past the file it is on.
    // ``this`` outlives every job: the destructor waits for the pool.
    // A Freeform's own files are in its owner's folder; their thumbnails are
    // kept by name with the library's, as their names are unique too.
    const auto dir = owner.empty() ? media_dir / db::media_subdir(kind)
                                   : freeform::own_media_dir(media_dir, owner);
    warm_thumbs_pool_.start([this, media_dir, dir, kind, own = !owner.empty(),
                             filenames = std::move(filenames), cancelled = warm_thumbs_cancelled_] {
        for (const auto& fn : filenames) {
            if (cancelled->load()) {
                return;
            }
            const auto source = dir / fn;
            if (kind == db::MediaKind::Image) {
                ensure_media_thumbs_all_tiers(media_dir, source, fn);
                continue;
            }
            const bool had = video_thumbs_made(media_dir, fn);
            if (ensure_video_thumbs(media_dir, source, fn) && !had) {
                post_video_thumbs_made(cancelled, own);
            }
        }
    });
}

void MediaController::post_video_thumbs_made(const std::shared_ptr<std::atomic<bool>>& cancelled,
                                             bool own)
{
    if (own) {
        // A Freeform's own video: no listing shows it, the editor does.
        QMetaObject::invokeMethod(
            this,
            [this, cancelled] {
                if (!cancelled->load()) {
                    ++files_revision_;
                    emit filesArrived();
                }
            },
            Qt::QueuedConnection);
        return;
    }
    // One rebuild for however many thumbs land before the GUI thread gets to it.
    if (video_thumbs_posted_.exchange(true)) {
        return;
    }
    QMetaObject::invokeMethod(
        this,
        [this, cancelled] {
            video_thumbs_posted_.store(false);
            if (!cancelled->load()) {
                refresh_listing(db::MediaKind::Video, Reread::FoldersOnly);
            }
        },
        Qt::QueuedConnection);
}

void MediaController::setMediaViewMode(const QString& mode)
{
    const auto next = normalized_view_mode(mode);
    if (media_view_mode_ == next) {
        return;
    }
    media_view_mode_ = next;
    QSettings settings;
    settings.setValue(k_view_mode_setting, media_view_mode_);
    emit viewModeChanged();
}

QVariantMap MediaController::renameMediaFile(MediaKindQml::Kind kind,
                                             const QString& filename,
                                             const QString& new_stem)
try {
    QVariantMap out;
    const auto mk = MediaKindQml::to_db(kind);
    if (!mk) {
        out.insert(QStringLiteral("error"), refusal::unknown_media_kind());
        return out;
    }
    if (!media_repo_) {
        out.insert(QStringLiteral("error"), refusal::no_library());
        return out;
    }
    const auto old_fn = util::leaf_filename(filename.toStdString());
    if (old_fn.empty()) {
        out.insert(QStringLiteral("error"), refusal::media_file_gone());
        return out;
    }

    auto stem = new_stem.trimmed();
    stem.replace(QLatin1Char('/'), QString());
    stem.replace(QLatin1Char('\\'), QString());
    // Drop a trailing extension if the user typed one - we keep the original.
    {
        const auto as_path = std::filesystem::path(stem.toStdString());
        if (db::is_any_media_filename(as_path.filename().string())) {
            stem = QString::fromStdString(as_path.stem().string());
        }
    }
    stem = stem.trimmed();
    if (stem.isEmpty() || stem == QLatin1String(".") || stem == QLatin1String("..")) {
        out.insert(QStringLiteral("error"), refusal::name_required());
        return out;
    }

    auto file = media_repo_->get_file(*mk, old_fn);
    if (!file) {
        out.insert(QStringLiteral("error"), refusal::media_file_gone());
        return out;
    }
    // The title keeps its extension; the file keeps its name.
    auto ext = std::filesystem::path(file->title()).extension().string();
    if (ext.empty()) {
        ext = std::filesystem::path(file->filename).extension().string();
    }
    media_repo_->set_display_name(*mk, old_fn, stem.toStdString() + ext);
    refresh_listing(*mk, Reread::Files);
    out.insert(QStringLiteral("filename"), QString::fromStdString(old_fn));
    return out;
} catch (const std::exception& ex) {
    return {{QStringLiteral("error"), app::report_entry_error(__func__, ex)}};
}

QVariantMap MediaController::audioFileFades(const QString& filename)
try {
    if (!media_repo_) {
        return {};
    }
    return to_qvariant(media_repo_->audio_fades(util::leaf_filename(filename.toStdString())))
        .toMap();
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return {};
}

QString MediaController::setAudioFileFades(const QString& filename, const QVariantMap& fades)
try {
    if (!media_repo_) {
        return refusal::no_library();
    }
    const auto fn = util::leaf_filename(filename.toStdString());
    if (fn.empty() || !media_repo_->get_file(db::MediaKind::Audio, fn)) {
        return refusal::media_file_gone();
    }
    const nlohmann::json given = to_json(QVariant(fades));
    media_repo_->set_audio_fades(fn, given);
    push_audio_fades();
    return {};
} catch (const std::exception& ex) {
    return app::report_entry_error(__func__, ex);
}

QString MediaController::mediaFileUrl(MediaKindQml::Kind kind, const QString& filename,
                                      int /*revision*/) const
{
    const auto mk = MediaKindQml::to_db(kind);
    if (!mk) {
        return {};
    }
    return file_url_for_media(*mk, filename.toStdString());
}

QString MediaController::mediaThumbUrl(MediaKindQml::Kind kind, const QString& filename,
                                       int /*revision*/)
{
    const auto mk = MediaKindQml::to_db(kind);
    if (!mk) {
        return {};
    }
    const auto ref = filename.toStdString();
    auto url = media_thumb_url_for(*mk, ref);
    // A Freeform's own video has its frame made the first time it is shown:
    // no listing warms it. The URL follows once it is there.
    if (url.isEmpty() && *mk == db::MediaKind::Video) {
        if (const auto place = place_of(*mk, ref); place && place->own) {
            std::error_code ec;
            if (std::filesystem::is_regular_file(place->path, ec)
                && warm_queued_.insert(place->leaf).second) {
                schedule_warm_media_thumbs(*mk, {place->leaf}, place->own->owner);
            }
        }
    }
    return url;
}

void MediaController::set_arrivals(std::map<std::string, FileArrival> arrivals)
{
    if (arrivals == arrivals_) {
        return;
    }
    arrivals_ = std::move(arrivals);
    ++arrivals_revision_;
    emit arrivalsChanged();
}

QString MediaController::arrivingDeleteNote(MediaKindQml::Kind kind, const QStringList& filenames,
                                            const QVariantList& folder_ids) const
try {
    const auto mk = MediaKindQml::to_db(kind);
    if (!mk || arrivals_.empty()) {
        return {};
    }
    std::set<std::string> names;
    for (const auto& filename : filenames) {
        names.insert(util::leaf_filename(filename.toStdString()));
    }
    if (media_repo_) {
        for (const auto& id : folder_ids) {
            for (auto& fn : media_repo_->list_folder_tree_filenames(*mk, id.toLongLong())) {
                names.insert(std::move(fn));
            }
        }
    }
    const auto subdir = std::string(db::media_subdir(*mk)) + "/";
    const auto coming = std::ranges::count_if(names, [&](const std::string& fn) {
        return !fn.empty() && on_its_way(subdir + fn);
    });
    if (coming == 0) {
        return {};
    }
    // Shown only for what is still coming: a delete goes to every computer
    // whatever is selected, but what looks broken here is what is deleted in
    // haste.
    if (filenames.size() == 1 && folder_ids.isEmpty()) {
        return tr("It is still on its way to this computer. Deleting it deletes it on every "
                  "computer that shares the library, those that have it too.");
    }
    if (coming == 1) {
        return tr("One of them is still on its way to this computer. Deleting deletes it on "
                  "every computer that shares the library, those that have it too.");
    }
    return tr("%1 of them are still on their way to this computer. Deleting deletes them on "
              "every computer that shares the library, those that have them too.")
        .arg(coming);
} catch (const std::exception& ex) {
    // Only a note: the confirm still asks without it.
    qWarning("SeventhSlide: could not tell which files are coming: %s", ex.what());
    return {};
}

bool MediaController::on_its_way(const std::string& path) const
{
    const auto it = arrivals_.find(path);
    return it != arrivals_.end() && it->second.state != FileArrival::State::Nowhere;
}

QVariantMap MediaController::arrival(MediaKindQml::Kind kind, const QString& filename,
                                     int /*revision*/) const
{
    const auto mk = MediaKindQml::to_db(kind);
    if (!mk || arrivals_.empty()) {
        return {};
    }
    const auto place = place_of(*mk, filename.toStdString());
    return place ? arrival_at(place->sync_path) : QVariantMap{};
}

QVariantMap MediaController::arrival_at(const std::string& path) const
{
    const auto it = arrivals_.find(path);
    if (it == arrivals_.end()) {
        return {};
    }
    const auto& a = it->second;
    QString state;
    QString text;
    double progress = -1;
    switch (a.state) {
    case FileArrival::State::Downloading:
        state = QStringLiteral("downloading");
        if (a.size > 0) {
            progress = std::clamp(static_cast<double>(a.bytes) / static_cast<double>(a.size), 0.0, 1.0);
            const auto percent = static_cast<int>(progress * 100);
            text = sync_paused_ ? tr("Paused at %1%").arg(percent) : tr("Downloading %1%").arg(percent);
        } else {
            text = sync_paused_ ? tr("Paused") : tr("Downloading");
        }
        break;
    case FileArrival::State::Queued:
        state = QStringLiteral("queued");
        text = sync_paused_ ? tr("Paused") : tr("On its way");
        break;
    case FileArrival::State::Waiting:
        state = QStringLiteral("waiting");
        text = tr("Waiting for a computer that has it");
        break;
    case FileArrival::State::Elsewhere:
        state = QStringLiteral("elsewhere");
        text = a.via.empty() ? tr("On its way to another computer")
                             : tr("On its way to %1").arg(QString::fromStdString(a.via));
        break;
    case FileArrival::State::Nowhere:
        state = QStringLiteral("missing");
        text = tr("Missing on every computer connected");
        break;
    }
    return {{QStringLiteral("state"), state},
            {QStringLiteral("progress"), progress},
            {QStringLiteral("text"), text}};
}

void MediaController::set_sync_paused(bool paused)
{
    if (paused == sync_paused_) {
        return;
    }
    sync_paused_ = paused;
    ++arrivals_revision_;
    emit arrivalsChanged();
}

std::filesystem::path MediaController::resolve_media_path(const QString& subdir,
                                                      const QString& filename) const
{
    const auto sub = subdir.trimmed().toLower().toStdString();
    // A Freeform's own file whole (its URL says where it is); anything else
    // by its leaf.
    auto fn = filename.toStdString();
    if (!freeform::own_media_of(fn)) {
        fn = util::leaf_filename(fn);
    }
    return library_.resolve_media_path(sub, fn);
}

QString MediaController::mediaFilePatterns(MediaKindQml::Kind kind) const
{
    const auto mk = MediaKindQml::to_db(kind);
    if (!mk) {
        return {};
    }
    QStringList patterns;
    for (const auto ext : db::media_extensions(*mk)) {
        patterns.push_back(QStringLiteral("*") + QString::fromUtf8(ext));
    }
    return patterns.join(QLatin1Char(' '));
}

QString MediaController::mediaFileDisplayName(MediaKindQml::Kind kind, const QString& filename)
try {
    const auto mk = MediaKindQml::to_db(kind);
    if (!mk) {
        return {};
    }
    if (const auto own = freeform::own_media_of(filename.toStdString())) {
        // Not the library's: called what it was brought in as.
        return QString::fromStdString(freeform::own_media_title(own->name));
    }
    const auto fn = util::leaf_filename(filename.toStdString());
    if (fn.empty()) {
        return {};
    }
    return QString::fromStdString(title_of(*mk, fn));
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return filename;
}

bool MediaController::addVideoToService(const QString& filename, int at_index)
try {
    return add_playback_items(db::MediaKind::Video, {util::leaf_filename(filename.toStdString())},
                              at_index);
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return false;
}

bool MediaController::addAudioToService(const QString& filename, int at_index)
try {
    return add_playback_items(db::MediaKind::Audio, {util::leaf_filename(filename.toStdString())},
                              at_index);
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return false;
}

qint64 MediaController::importMedia(MediaKindQml::Kind kind,
                                    const QStringList& local_paths,
                                    qint64 folder_id)
{
    const auto mk = MediaKindQml::to_db(kind);
    if (!mk) {
        return refuse_import(refusal::unknown_media_kind());
    }
    return import_files(*mk, local_paths, folder_id);
}

qint64 MediaController::refuse_import(const QString& why)
{
    // Reported like any other import, once the caller holds the id.
    const qint64 id = ++last_import_id_;
    QMetaObject::invokeMethod(
        this, [this, id, why] { emit importFinished(id, import_result({why}, {})); },
        Qt::QueuedConnection);
    return id;
}

std::optional<std::string> MediaController::adopt_staged(
    db::MediaKind kind,
    const std::filesystem::path& staged,
    std::string_view extension,
    std::string_view title,
    std::optional<std::int64_t> folder_id,
    const std::optional<std::string>& replacing,
    QString& error)
{
    const auto discard = [&staged] {
        std::error_code ec;
        std::filesystem::remove(staged, ec);
    };
    const auto media_dir = library_.media_root();
    if (!media_repo_ || media_dir.empty()) {
        discard();
        error = refusal::no_library();
        return std::nullopt;
    }
    const auto dir = media_dir / db::media_subdir(kind);
    // Opaque and never changing, as an import's name is.
    const std::string filename =
        QUuid::createUuid().toString(QUuid::Id128).toStdString() + std::string(extension);
    const auto dest = dir / filename;
    std::error_code ec;
    std::filesystem::rename(staged, dest, ec);
    if (ec) {
        discard();
        error = tr("The new file could not be moved into the library.");
        return std::nullopt;
    }
    try {
        db::Transaction tx(*library_.database());
        media_repo_->upsert_file(kind, filename, title);
        if (replacing) {
            (void)media_repo_->retarget_file(kind, *replacing, filename);
            media_repo_->remove_file(kind, *replacing);
        } else if (folder_id && library_for(kind).folder_browser().contains(*folder_id)) {
            (void)media_repo_->add_folder_item(kind, *folder_id, filename);
        }
        tx.commit();
    } catch (const std::exception& ex) {
        // Nothing points at it: the rows were rolled back.
        std::filesystem::remove(dest, ec);
        error = app::report_entry_error(__func__, ex);
        return std::nullopt;
    }
    if (replacing) {
        // A file still open somewhere (a stack playing it) may refuse to go
        // on some systems; it is out of the library either way.
        (void)util::remove_file(dir / *replacing);
        remove_thumbs(media_dir, kind, *replacing);
    }
    if (!replacing && import_note_) {
        // A new file in the library, as an import's (a synced library's size
        // limit); one replacing another takes its place.
        const auto size = std::filesystem::file_size(dest, ec);
        if (const auto note = import_note_({QString::fromUtf8(title)},
                                           ec ? 0 : static_cast<std::int64_t>(size));
            !note.isEmpty()) {
            emit errorOccurred(note);
        }
    }
    warm_queued_.insert(filename);
    if (has_thumbs(kind)) {
        schedule_warm_media_thumbs(kind, {filename});
    }
    refresh_listing(kind, Reread::Files);
    if (replacing) {
        (void)rundown_.sync_open_service_items();
    }
    return filename;
}

std::vector<std::string> MediaController::adopt_staged_batch(const std::vector<StagedMedia>& files,
                                                            std::vector<std::filesystem::path>& moved)
{
    const auto media_dir = library_.media_root();
    if (!media_repo_ || media_dir.empty()) {
        throw std::runtime_error("No library is open");
    }
    std::vector<std::string> names;
    names.reserve(files.size());
    for (const auto& file : files) {
        const std::string filename =
            QUuid::createUuid().toString(QUuid::Id128).toStdString() + file.extension;
        const auto dest = media_dir / db::media_subdir(file.kind) / filename;
        std::error_code ec;
        std::filesystem::rename(file.staged, dest, ec);
        if (ec) {
            throw std::runtime_error("Could not move a new file into the library");
        }
        moved.push_back(dest);
        media_repo_->upsert_file(file.kind, filename, file.title);
        if (file.folder_id) {
            (void)media_repo_->add_folder_item(file.kind, *file.folder_id, filename);
        }
        names.push_back(filename);
    }
    return names;
}

void MediaController::announce_adopted(const std::vector<StagedMedia>& files,
                                       const std::vector<std::string>& filenames)
{
    std::set<db::MediaKind> kinds;
    QStringList titles;
    std::int64_t bytes = 0;
    std::map<db::MediaKind, std::vector<std::string>> by_kind;
    for (std::size_t i = 0; i < files.size() && i < filenames.size(); ++i) {
        kinds.insert(files[i].kind);
        titles.push_back(QString::fromStdString(files[i].title));
        std::error_code ec;
        const auto size = std::filesystem::file_size(
            library_.media_root() / db::media_subdir(files[i].kind) / filenames[i], ec);
        bytes += ec ? 0 : static_cast<std::int64_t>(size);
        warm_queued_.insert(filenames[i]);
        by_kind[files[i].kind].push_back(filenames[i]);
    }
    for (auto& [kind, names] : by_kind) {
        if (has_thumbs(kind)) {
            schedule_warm_media_thumbs(kind, std::move(names));
        }
    }
    for (const auto kind : kinds) {
        refresh_listing(kind, Reread::Files);
    }
    if (import_note_ && !titles.isEmpty()) {
        if (const auto note = import_note_(titles, bytes); !note.isEmpty()) {
            emit errorOccurred(note);
        }
    }
}

void MediaController::refresh_folders(db::MediaKind kind)
{
    refresh_listing(kind, Reread::FoldersOnly);
}

qint64 MediaController::import_files(db::MediaKind kind,
                                     const QStringList& local_paths,
                                     qint64 folder_id)
{
    const auto media_dir = library_.media_root();
    if (!media_repo_ || media_dir.empty()) {
        return refuse_import(refusal::no_library());
    }
    const qint64 id = ++last_import_id_;

    ImportTarget target;
    target.kind = kind;
    if (folder_id > 0) {
        target.folder_id = folder_id;
    }
    ImportBatch batch;
    batch.id = id;
    batch.dir = media_dir / db::media_subdir(kind);
    batch.max_bytes = db::max_import_bytes(kind);
    batch.files.reserve(static_cast<std::size_t>(local_paths.size()));
    for (const auto& raw : local_paths) {
        ImportFile file;
        file.source = local_path_from_qml(raw);
        file.display = file.source.filename().string();
        if (!db::is_media_filename(kind, file.display)) {
            file.error = not_a_kind_of(kind).toStdString();
        }
        batch.files.push_back(std::move(file));
    }
    importer_.start(std::move(batch), [this, target](ImportBatch done) {
        finish_import(target, std::move(done));
    });
    return id;
}

qint64 MediaController::import_freeform_media(db::MediaKind kind, const QStringList& local_paths,
                                              const std::string& owner, const std::string& title)
{
    const auto media_dir = library_.media_root();
    if (media_dir.empty() || !media_repo_) {
        return refuse_import(refusal::no_library());
    }
    if (!freeform::is_own_media_part(owner) || kind == db::MediaKind::Audio) {
        return refuse_import(refusal::freeform_gone());
    }
    const qint64 id = ++last_import_id_;
    ImportBatch batch;
    batch.id = id;
    batch.dir = freeform::own_media_dir(media_dir, owner);
    std::error_code ec;
    std::filesystem::create_directories(batch.dir, ec);
    batch.max_bytes = db::max_import_bytes(kind);
    batch.files.reserve(static_cast<std::size_t>(local_paths.size()));
    for (const auto& raw : local_paths) {
        ImportFile file;
        file.source = local_path_from_qml(raw);
        file.display = file.source.filename().string();
        if (!db::is_media_filename(kind, file.display)) {
            file.error = not_a_kind_of(kind).toStdString();
        }
        batch.files.push_back(std::move(file));
    }
    importer_.start(std::move(batch), [this, kind, owner, title](ImportBatch done) {
        finish_freeform_import(kind, owner, title, std::move(done));
    });
    return id;
}

void MediaController::finish_freeform_import(db::MediaKind kind, const std::string& owner,
                                             const std::string& title, ImportBatch batch) noexcept
{
    QStringList errors;
    QStringList urls;
    try {
        QStringList titles;
        std::int64_t bytes = 0;
        std::vector<std::string> warm;
        for (auto& file : batch.files) {
            if (!file.error.empty()) {
                errors.push_back(file_error(file, file.error));
                continue;
            }
            const freeform::OwnMedia own{
                owner, freeform::new_own_media_name(title.empty() ? file.display : title,
                                                    file.source.extension().string())};
            const auto dest = batch.dir / own.name;
            std::error_code ec;
            std::filesystem::rename(file.staged, dest, ec);
            if (ec) {
                std::filesystem::remove(file.staged, ec);
                errors.push_back(file_error(file, import_not_copied()));
                continue;
            }
            urls.push_back(QString::fromStdString(freeform::own_media_url(own)));
            titles.push_back(QString::fromStdString(file.display));
            const auto size = std::filesystem::file_size(dest, ec);
            bytes += ec ? 0 : static_cast<std::int64_t>(size);
            warm.push_back(own.name);
            warm_queued_.insert(own.name);
        }
        if (import_note_ && !titles.isEmpty()) {
            if (const auto note = import_note_(titles, bytes); !note.isEmpty()) {
                emit errorOccurred(note);
            }
        }
        if (has_thumbs(kind) && !warm.empty()) {
            schedule_warm_media_thumbs(kind, std::move(warm), owner);
        }
    } catch (const std::exception& ex) {
        errors.push_back(app::report_entry_error(__func__, ex));
    }
    emit importFinished(batch.id, import_result(errors, urls));
}

void MediaController::own_media_arrived()
{
    // Nothing lists them; what shows them looks again.
    ++files_revision_;
    emit filesArrived();
}

void MediaController::finish_import(const ImportTarget& target, ImportBatch batch) noexcept
{
    QStringList errors;
    QStringList filenames;
    try {
        filenames = commit_import(target, batch, errors);
    } catch (const std::exception& ex) {
        // The files are in place and registered. Only the browser refresh
        // after them failed.
        const auto message = app::report_entry_error(__func__, ex);
        errors.push_back(message);
        emit errorOccurred(message);
    }
    emit importFinished(batch.id, import_result(errors, filenames));
}

QStringList MediaController::commit_import(const ImportTarget& target,
                                          ImportBatch& batch,
                                          QStringList& errors)
{
    struct Placed {
        std::filesystem::path path;
        std::string filename;
        const ImportFile* file;
    };
    std::vector<Placed> placed;

    // Until the rows are committed, a failure takes every file of the batch
    // back off the disk: a file the library does not know about is litter.
    try {
        for (auto& file : batch.files) {
            if (!file.error.empty()) {
                errors.push_back(file_error(file, file.error));
                continue;
            }
            // The name is opaque and never changes (the registry holds what
            // the library shows), so two files may share a source name, and a
            // name means one file on every computer the library is shared with.
            const auto filename = QUuid::createUuid().toString(QUuid::Id128).toStdString()
                                  + file.source.extension().string();
            const auto dest = batch.dir / filename;
            std::error_code ec;
            std::filesystem::rename(file.staged, dest, ec);
            if (ec) {
                errors.push_back(file_error(file, import_not_copied()));
                continue;
            }
            file.staged.clear();
            placed.push_back({dest, filename, &file});
        }
        // A folder deleted while the files copied: they land loose instead.
        const std::int64_t folder_id =
            !placed.empty() && target.folder_id && library_for(target.kind).folder_browser().contains(*target.folder_id)
                ? *target.folder_id
                : -1;
        if (!placed.empty()) {
            db::Transaction tx(*library_.database());
            for (const auto& p : placed) {
                media_repo_->upsert_file(target.kind, p.filename, p.file->display);
                if (folder_id >= 0) {
                    (void)media_repo_->add_folder_item(target.kind, folder_id, p.filename);
                }
            }
            tx.commit();
        }
    } catch (const std::exception& ex) {
        for (const auto& p : placed) {
            std::error_code ec;
            std::filesystem::remove(p.path, ec);
        }
        placed.clear();
        errors.push_back(app::report_entry_error(__func__, ex));
    }
    for (const auto& file : batch.files) {
        if (!file.staged.empty()) {
            std::error_code ec;
            std::filesystem::remove(file.staged, ec);
        }
    }
    if (placed.empty()) {
        return {};
    }

    QStringList filenames;
    QStringList titles;
    std::int64_t bytes = 0;
    std::vector<std::string> warm;
    warm.reserve(placed.size());
    for (const auto& p : placed) {
        filenames.push_back(QString::fromStdString(p.filename));
        titles.push_back(QString::fromStdString(p.file->display));
        std::error_code ec;
        const auto size = std::filesystem::file_size(p.path, ec);
        bytes += ec ? 0 : static_cast<std::int64_t>(size);
        warm.push_back(p.filename);
        warm_queued_.insert(p.filename);
    }
    if (import_note_) {
        if (const auto note = import_note_(titles, bytes); !note.isEmpty()) {
            emit errorOccurred(note);
        }
    }
    if (has_thumbs(target.kind)) {
        // Thumbs are the expensive half of an import (decode and scale, three
        // tiers per file), so they go to the warming pool too.
        schedule_warm_media_thumbs(target.kind, std::move(warm));
    }
    refresh_listing(target.kind, Reread::Files);
    return filenames;
}

}  // namespace seventhslide::app
