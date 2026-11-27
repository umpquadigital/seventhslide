#pragma once

// The media library: images, videos and audio.
//
// Owns the media repository, a library tab per media kind (db::MediaKind; a
// MediaLibrary: its folders, browser and folder calls), and the thumbnail
// cache. Where the files live is the
// open library's; rows are added to the rundown through IRundown; a
// playlist goes live through the session, and liveChanged tells the
// application to follow.
//
// SessionFacade owns one and exposes it as `session.media`.

#include "library_models.hpp"
#include "file_importer.hpp"
#include "media_kind_qml.hpp"
#include "media_library.hpp"
#include "media_thumb_cache.hpp"
#include "open_library.hpp"
#include "qml_ids.hpp"

#include <seventhslide/db/media_repository.hpp>
#include <seventhslide/freeform/own_media.hpp>
#include <seventhslide/session/session.hpp>

#include <QtQml/qqmlregistration.h>
#include <QObject>
#include <QThreadPool>

#include <array>
#include <atomic>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace seventhslide::app {

class IRundown;

/// What sync knows of a file of the open library that is not here yet.
struct FileArrival {
    enum class State {
        /// Its bytes are coming: ``bytes`` of ``size`` so far.
        Downloading,
        /// Asked for, or to be, of a computer connected.
        Queued,
        /// No computer that shares the library is connected.
        Waiting,
        /// Every computer connected said it has not got it.
        Nowhere,
        /// Not here, and a computer connected is itself getting it (``via``).
        Elsewhere,
    };
    State state = State::Queued;
    std::int64_t bytes = 0;
    std::int64_t size = 0;
    /// ``Elsewhere``: the name of the computer it is on its way to.
    std::string via = {};
    bool operator==(const FileArrival&) const = default;
};

class MediaController : public QObject {
    Q_OBJECT
    QML_ANONYMOUS

    Q_PROPERTY(seventhslide::app::ImageListModel* images READ images CONSTANT)
    Q_PROPERTY(qint64 videoFolderId READ videoFolderId NOTIFY changed)
    Q_PROPERTY(qint64 audioFolderId READ audioFolderId NOTIFY changed)
    Q_PROPERTY(QString mediaViewMode READ mediaViewMode WRITE setMediaViewMode NOTIFY
                   viewModeChanged)
    /// An import has been started and has not finished.
    Q_PROPERTY(bool importing READ importing NOTIFY importProgressChanged)
    /// 0 to 1 across every import still running.
    Q_PROPERTY(double importProgress READ importProgress NOTIFY importProgressChanged)
    /// Moves whenever a library file arrives from another computer: a binding
    /// calling ``mediaFileUrl`` or ``mediaThumbUrl`` passes it, so a file that
    /// was missing shows once it is here.
    Q_PROPERTY(int filesRevision READ filesRevision NOTIFY filesArrived)
    /// Moves whenever what ``arrival`` says of any file changes (at most once
    /// a second while files come).
    Q_PROPERTY(int arrivalsRevision READ arrivalsRevision NOTIFY arrivalsChanged)
    /// Sync is paused while something is live: nothing on its way moves until
    /// nothing is.
    Q_PROPERTY(bool syncPaused READ syncPaused NOTIFY arrivalsChanged)

public:
    /// ``rundown`` adds the rows "add to service" creates.
    MediaController(OpenLibrary& library,
                    session::Session& session,
                    IRundown& rundown,
                    QObject* parent = nullptr);
    /// Stops thumbnail warming: queued jobs are dropped and a running one
    /// stops after its current file, so teardown never waits on a backlog.
    /// Imports stop the same way (see FileImporter).
    ~MediaController() override;

    /// Point the library at a newly opened database, or let go of one closing.
    /// Either one stops every import still running: each finishes with its
    /// files reported as stopped.
    void attach(db::Database& db);
    void detach();
    [[nodiscard]] bool attached() const noexcept { return media_repo_ != nullptr; }

    [[nodiscard]] ImageListModel* images() noexcept { return &image_model_; }

    /// ``kind``'s library tab: its folders, browser and folder calls, the
    /// same as every library's (FolderLibrary). Null for a kind outside the
    /// enum (a bare number from QML).
    Q_INVOKABLE seventhslide::app::MediaLibrary* library(seventhslide::app::MediaKindQml::Kind kind);
    [[nodiscard]] MediaLibrary& library_for(db::MediaKind kind) noexcept
    {
        return *libraries_[db::media_index(kind)];
    }
    [[nodiscard]] const MediaLibrary& library_for(db::MediaKind kind) const noexcept
    {
        return *libraries_[db::media_index(kind)];
    }

    /// ``kind``'s browser rows.
    [[nodiscard]] MediaBrowserModel* browser(db::MediaKind kind) noexcept;

    /// The folder ``kind``'s browser shows; -1 for the root.
    [[nodiscard]] qint64 folder_id(db::MediaKind kind) const noexcept;

    [[nodiscard]] qint64 videoFolderId() const noexcept { return folder_id(db::MediaKind::Video); }
    [[nodiscard]] qint64 audioFolderId() const noexcept { return folder_id(db::MediaKind::Audio); }

    [[nodiscard]] bool importing() const noexcept { return importer_.busy(); }

    [[nodiscard]] double importProgress() const noexcept { return importer_.progress(); }

    /// ``list`` or ``icons``; remembered across runs.
    [[nodiscard]] QString mediaViewMode() const { return media_view_mode_; }

    void setMediaViewMode(const QString& mode);

    Q_INVOKABLE bool addImageToService(const QString& filename, int at_index = -1);

    Q_INVOKABLE bool addImageFolderToService(qint64 folder_id, int at_index = -1);

    Q_INVOKABLE bool addVideoToService(const QString& filename, int at_index = -1);

    /// Expand a video folder into one service item per file.
    Q_INVOKABLE bool addVideoFolderToService(qint64 folder_id, int at_index = -1);

    Q_INVOKABLE bool addAudioToService(const QString& filename, int at_index = -1);

    /// Expand an audio folder into one service item per file.
    Q_INVOKABLE bool addAudioFolderToService(qint64 folder_id, int at_index = -1);

    /// Standalone live image folder from the library.
    Q_INVOKABLE bool playImageFolder(qint64 folder_id, int start_index = 0);

    /// Retitle a library file. ``new_stem`` is the title without extension
    /// (the extension is kept). The file keeps its name on disk, and service
    /// items already made keep the title they were made with. Returns
    /// ``{filename}`` on success or ``{error}``.
    Q_INVOKABLE QVariantMap renameMediaFile(seventhslide::app::MediaKindQml::Kind kind,
                                            const QString& filename,
                                            const QString& new_stem);

    /// The audio fades audio file ``filename`` sets for itself: only the
    /// fields it sets ({fade_in_ms?, fade_out_ms?, overlap_ms?}).
    Q_INVOKABLE QVariantMap audioFileFades(const QString& filename);
    /// Keep ``fades`` as audio file ``filename``'s own: any of fade_in_ms,
    /// fade_out_ms and overlap_ms (ms, each clamped). A field it does not
    /// hold follows the open service's, then each output's. Wherever the
    /// file plays, it fades so from its next change on. Empty = ok.
    Q_INVOKABLE QString setAudioFileFades(const QString& filename, const QVariantMap& fades);

    /// ``file://`` URL for a library file (full-res; not for UI thumbs).
    /// ``revision`` (``filesRevision``) is only there for a binding to follow.
    Q_INVOKABLE QString mediaFileUrl(seventhslide::app::MediaKindQml::Kind kind, const QString& filename,
                                     int revision = 0) const;

    /// ``image://mediathumb/...`` URL for scaled library / gallery thumbs: an
    /// image's picture, a video's frame. Empty for sound, or a missing file.
    /// ``revision`` (``filesRevision``) is only there for a binding to follow.
    /// A Freeform's own video (by its URL) has its frame made the first time
    /// it is asked for; the URL comes once it is there (``filesRevision``).
    Q_INVOKABLE QString mediaThumbUrl(seventhslide::app::MediaKindQml::Kind kind, const QString& filename,
                                      int revision = 0);
    [[nodiscard]] int filesRevision() const noexcept { return files_revision_; }

    /// What sync knows of library file ``filename`` while it is not here:
    /// ``{state, progress, text}``, ``state`` one of "downloading" (with
    /// ``progress`` 0 to 1), "queued", "waiting", "elsewhere" (on its way to a
    /// computer connected, which has none to give yet) and "missing", the last
    /// when no computer connected has it; while ``syncPaused`` what would move
    /// says it is paused. Empty when it is here, or sync knows
    /// nothing of it. ``revision`` (``arrivalsRevision``) is only there for a
    /// binding to follow.
    Q_INVOKABLE QVariantMap arrival(seventhslide::app::MediaKindQml::Kind kind, const QString& filename,
                                    int revision = 0) const;
    [[nodiscard]] int arrivalsRevision() const noexcept { return arrivals_revision_; }
    /// What a delete confirm adds when some of files ``filenames`` and folders
    /// ``folder_ids`` (the files in them, however deep) of ``kind`` are on
    /// their way (``on_its_way``): that deleting them deletes them everywhere,
    /// those that have them too. Empty when none are.
    Q_INVOKABLE QString arrivingDeleteNote(seventhslide::app::MediaKindQml::Kind kind,
                                           const QStringList& filenames,
                                           const QVariantList& folder_ids) const;
    /// What sync knows of the open library's files not here, by path under the
    /// media folder ("images/<name>"). From SyncController; empty without it.
    void set_arrivals(std::map<std::string, FileArrival> arrivals);
    /// ``arrival`` for the file at ``path`` ("<folder>/<name>").
    [[nodiscard]] QVariantMap arrival_at(const std::string& path) const;
    [[nodiscard]] bool syncPaused() const noexcept { return sync_paused_; }
    /// Whether sync knows of any file of the open library not here.
    [[nodiscard]] bool has_arrivals() const noexcept { return !arrivals_.empty(); }
    /// From SyncController: what is on its way is held while something is live.
    void set_sync_paused(bool paused);
    /// Whether sync says library file ``path`` ("<folder>/<name>") is not here
    /// but coming: from a computer connected, or one that is not yet. Not one
    /// every computer connected said it has not got.
    [[nodiscard]] bool on_its_way(const std::string& path) const;

    /// Resolve ``images|videos`` + basename under the media folder. Safe from
    /// any thread: the thumb provider calls it on Qt's pixmap reader.
    [[nodiscard]] std::filesystem::path resolve_media_path(const QString& subdir,
                                                         const QString& filename) const;

    /// The file name patterns ``kind`` imports, for a file dialog filter:
    /// ``"*.mp3 *.wav ..."``.
    Q_INVOKABLE QString mediaFilePatterns(seventhslide::app::MediaKindQml::Kind kind) const;

    /// Library display title for a media file (its display name, else the filename).
    Q_INVOKABLE QString mediaFileDisplayName(seventhslide::app::MediaKindQml::Kind kind, const QString& filename);

    /// Copy files into ``kind``'s media folder under new opaque names, register
    /// them titled by their source names, and file them in ``folder_id`` when
    /// it is a folder (-1 = none). The copy runs
    /// off the GUI thread: this returns a request id at once, and
    /// ``importFinished`` reports that id exactly once, from the event loop.
    qint64 import_files(db::MediaKind kind, const QStringList& local_paths, qint64 folder_id = -1);

    /// ``import_files`` for QML.
    Q_INVOKABLE qint64 importMedia(seventhslide::app::MediaKindQml::Kind kind,
                                   const QStringList& local_paths,
                                   qint64 folder_id = -1);

    /// Copy pictures or videos into the folder of a Freeform document's own
    /// files that ``owner`` has (freeform/own_media.hpp), never the
    /// library's: nothing is registered or listed. As ``import_files``, the
    /// copy runs off the GUI thread and ``importFinished`` reports the id
    /// once, its ``filenames`` the files' URLs for the document to name.
    /// Each is called what its source is, or ``title`` when one is given.
    qint64 import_freeform_media(db::MediaKind kind, const QStringList& local_paths,
                                 const std::string& owner, const std::string& title = {});
    /// Sync brought Freeform documents' own files: what shows them looks again.
    void own_media_arrived();

    /// What to tell the operator about files just imported (``titles``,
    /// ``bytes`` together); empty for nothing. Sync's: a synced library's
    /// size limit they would go over.
    using ImportNote = std::function<QString(const QStringList& titles, std::int64_t bytes)>;
    /// Asked after each import that placed files; an empty one asks nothing.
    void set_import_note(ImportNote note) { import_note_ = std::move(note); }

    /// Take a file made outside the importer - a conversion's output, left
    /// at ``staged`` (a staging name in ``kind``'s media folder) - into the
    /// library under a new opaque name ending in ``extension``, titled
    /// ``title``.
    ///
    /// Without ``replacing`` it is filed in ``folder_id`` (when that folder
    /// still exists). With ``replacing`` (a library file of ``kind``) it takes
    /// that file's place instead: its folder links and every use
    /// (MediaRepository::retarget_file) move to it in the same transaction
    /// that registers it and drops the original's row; the original's bytes
    /// go only once that has committed, and the open rundown is read again.
    ///
    /// Returns the new filename, or nullopt with ``error`` set. Either way
    /// ``staged`` is gone afterwards.
    std::optional<std::string> adopt_staged(db::MediaKind kind,
                                            const std::filesystem::path& staged,
                                            std::string_view extension,
                                            std::string_view title,
                                            std::optional<std::int64_t> folder_id,
                                            const std::optional<std::string>& replacing,
                                            QString& error);

    /// A file staged in ``kind``'s media folder, for ``adopt_staged_batch``.
    struct StagedMedia {
        db::MediaKind kind = db::MediaKind::Image;
        std::filesystem::path staged;
        std::string extension;
        std::string title;
        std::optional<std::int64_t> folder_id;
    };
    /// ``adopt_staged`` for many files, inside the caller's transaction:
    /// each is moved to a new opaque name and registered (in its folder when
    /// it has one). Returns the new names by place. Throws on a database
    /// error; the paths each file was moved to are in ``moved`` either way,
    /// for the caller to remove if its transaction does not commit. Nothing
    /// is announced until ``announce_adopted``.
    [[nodiscard]] std::vector<std::string> adopt_staged_batch(const std::vector<StagedMedia>& files,
                                                              std::vector<std::filesystem::path>& moved);
    /// After the caller's transaction committed: the new files' thumbs, the
    /// listings of their kinds, and the size note.
    void announce_adopted(const std::vector<StagedMedia>& files, const std::vector<std::string>& filenames);
    /// The data an image-folder live item or rundown row carries: the folder's
    /// name and its images in order. Nullopt when there is no such folder.
    [[nodiscard]] std::optional<nlohmann::json> image_folder_data(std::int64_t folder_id) const;
    /// The listing of ``kind``'s folders, read again (a folder made elsewhere).
    void refresh_folders(db::MediaKind kind);

    /// Background-generate disk thumbs for filenames of ``kind`` currently listed (existing libraries),
    /// or for the files of a Freeform's ``owner`` (freeform/own_media.hpp).
    void schedule_warm_media_thumbs(db::MediaKind kind, std::vector<std::string> filenames,
                                    const std::string& owner = {});
    /// From the warming pool: a video's thumbs were made, so its row can show
    /// them. Rebuilds the video listing on the GUI thread; for a Freeform's
    /// own video (``own``), which no listing shows, bumps ``filesRevision``.
    void post_video_thumbs_made(const std::shared_ptr<std::atomic<bool>>& cancelled, bool own = false);

    [[nodiscard]] db::MediaRepository* media_repo() noexcept { return media_repo_.get(); }

    /// What the library calls ``filename``: its display name, else the
    /// filename itself (as for a file the registry does not hold).
    [[nodiscard]] std::string title_of(db::MediaKind kind, const std::string& filename) const;
    /// The data a live item or rundown row playing library file ``filename``
    /// carries, titled as the library titles it.
    [[nodiscard]] nlohmann::json item_data(db::MediaKind kind,
                                           const std::string& filename,
                                           bool loop = false) const;

    /// Re-read the image registry and every media browser. Used when a library
    /// is opened, switched, or a profile is applied. One folder edit uses
    /// ``refresh_listing``.
    void refresh();
    /// Re-read ``kind``'s registry and browser only: sync changed them.
    void refresh(db::MediaKind kind);

    /// Files ``names`` of ``kind`` came from another computer: their rows show
    /// them, and their thumbnails are made.
    void files_arrived(db::MediaKind kind, const std::vector<std::string>& names);

    /// A different library is about to become active. Bumps the media URL
    /// token. Does not rebuild
    /// browsers; the caller does that with ``refresh`` after the new root is set.
    void on_library_switched();
    /// Drop queued thumbnail jobs and stop the running one after its current
    /// file. Call before the media folder they write into goes away.
    void cancel_thumb_warmers();


signals:
    /// The media lists, folders or search moved.
    void changed();
    void viewModeChanged();
    /// The import ``import_files`` returned ``requestId``
    /// for is over. ``result``: ``{ok, count, errors, filenames}``, where
    /// ``filenames`` are the imported files' names in the library and
    /// ``errors`` reads ``"<source name>: <reason>"``, one per file.
    void importFinished(qint64 requestId, const QVariantMap& result);
    void importProgressChanged();
    /// An entry point failed and has no return value to carry the reason:
    /// the shell shows ``message`` to the operator (see entry_error.hpp).
    void errorOccurred(const QString& message);
    /// A library file arrived (``files_arrived``).
    void filesArrived();
    void arrivalsChanged();
    /// A playlist went live: the application reloads what follows live
    /// content and repaints (see LibraryController::liveChanged).
    void liveChanged(bool rebuildModels);

private:
    /// Each kind's library tab asks this for what only it can do.
    friend class MediaLibrary;

    /// What a listing refresh reads again besides the folders: ``Files`` is
    /// the kind's registry (for images, the image list as well).
    enum class Reread { FoldersOnly, Files };

    /// Take ``filenames`` out of ``kind``'s library: out of their folders,
    /// out of the registry, off the disk. An image something still shows is
    /// hidden and kept instead. Callers have refused first where the kind
    /// refuses (``in_service_refusal``).
    void remove_library_files(db::MediaKind kind, const std::vector<std::string>& filenames);
    /// Delete a library file. Empty return = ok.
    /// An image still shown by a service or a Freeform is hidden (kept on
    /// disk); an orphan is removed from disk. A video or audio file is removed
    /// from disk, and refused while a service item plays it
    /// (``db::hidden_when_in_use``).
    QString delete_media_file(db::MediaKind kind, const QString& filename);
    /// Delete a folder, its subfolders and their files. Each file follows the
    /// ``delete_media_file`` rules, except that a file still filed in a folder
    /// outside the deleted tree stays. For video and audio, one file in use
    /// refuses the whole delete, and nothing changes.
    QString delete_folder(db::MediaKind kind, std::int64_t folder_id);

    // --- Service items that play a library file by name (video, audio). ---
    // A service item keeps the filename it plays, so deleting the file would
    // break the item.

    /// One service item per file of ``filenames`` (empty names skipped), at
    /// ``at_index`` or the end. ``kind`` is Video or Audio.
    bool add_playback_items(db::MediaKind kind, std::vector<std::string> filenames, int at_index);
    /// Every file of video or audio folder ``folder_id``, as service rows.
    bool add_playback_folder(db::MediaKind kind, qint64 folder_id, int at_index);
    /// An import that cannot start: a new request id whose ``importFinished``
    /// reports ``why`` from the event loop.
    qint64 refuse_import(const QString& why);

    /// Why ``filenames`` cannot be deleted while services play them, or empty.
    /// ``in_folder`` words it for a folder delete.
    [[nodiscard]] QString in_service_refusal(db::MediaKind kind,
                                             const std::vector<std::string>& filenames,
                                             bool in_folder);
    void rebuild_media_browsers();

    /// Where an import's files go once they are copied.
    struct ImportTarget {
        db::MediaKind kind = db::MediaKind::Image;
        std::optional<std::int64_t> folder_id;
    };

    /// Move a copied batch into the library and report it. Runs from the
    /// event loop, so it never lets an exception out.
    void finish_import(const ImportTarget& target, ImportBatch batch) noexcept;
    /// ``finish_import`` for ``import_freeform_media``: each file under a
    /// name of its own in the owner's folder, and nothing registered.
    void finish_freeform_import(db::MediaKind kind, const std::string& owner,
                                const std::string& title, ImportBatch batch) noexcept;

    /// Where the file ``ref`` names is: a library file of ``kind`` by its
    /// name, or a Freeform's own file by its URL. Whether it is there is the
    /// caller's to ask.
    struct MediaPlace {
        std::filesystem::path path;
        /// As sync names it ("images/<name>", "freeform_media/<owner>/<name>").
        std::string sync_path;
        /// The file's own name, which its thumbnails go by.
        std::string leaf;
        std::optional<freeform::OwnMedia> own;
    };
    [[nodiscard]] std::optional<MediaPlace> place_of(db::MediaKind kind, const std::string& ref) const;
    /// Rename staged files into place, register them, refresh the browser.
    /// Fills ``errors`` and returns the names the files now have.
    [[nodiscard]] QStringList commit_import(const ImportTarget& target,
                                            ImportBatch& batch,
                                            QStringList& errors);

    void rebuild_media_browser(db::MediaKind kind);

    /// Rebuild one browser, after reading ``what`` again.
    void refresh_listing(db::MediaKind kind, Reread what);
    /// Tell the session each audio file's own fades (none with no library).
    void push_audio_fades();


    [[nodiscard]] QString file_url_for_media(db::MediaKind kind,
                                             const std::string& filename) const;

    [[nodiscard]] QString media_thumb_url_for(db::MediaKind kind,
                                              const std::string& filename) const;

    OpenLibrary& library_;
    session::Session& session_;
    IRundown& rundown_;
    /// Every image-provider URL carries this, so same-named media in a new
    /// library cannot be served from the previous one's caches.
    int library_revision_ = 0;
    int files_revision_ = 0;
    std::map<std::string, FileArrival> arrivals_;
    bool sync_paused_ = false;
    int arrivals_revision_ = 0;
    /// Thumbnail warming runs here, one job at a time. Each job holds the
    /// cancel flag of the batch it was queued in by shared_ptr, so the flag
    /// outlives this object for as long as any job can read it.
    QThreadPool warm_thumbs_pool_;
    std::shared_ptr<std::atomic<bool>> warm_thumbs_cancelled_ =
        std::make_shared<std::atomic<bool>>(false);
    /// Images and videos this library has already queued for warming. A browser rebuild
    /// (every search keystroke, every folder change) queues only the rest; an
    /// import queues its files again, as they may replace older ones.
    std::unordered_set<std::string> warm_queued_;
    /// A rebuild for new video thumbs is queued and has not run yet.
    std::atomic<bool> video_thumbs_posted_{false};
    std::unique_ptr<db::MediaRepository> media_repo_;

    ImageListModel image_model_;
    /// Children of this object, so QML never takes ownership of one.
    std::array<MediaLibrary*, db::k_media_kinds.size()> libraries_{};
    QString media_view_mode_;
    qint64 last_import_id_ = 0;
    ImportNote import_note_;
    /// Last, so it is destroyed first: its worker is stopped and joined before
    /// anything a finished import would touch goes away.
    FileImporter importer_;
};

} // namespace seventhslide::app
