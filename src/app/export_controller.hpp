#pragma once

// Export: what leaves the library as files to keep or take elsewhere.
//
//   - Slides as PNG images in one ZIP file: a service, songs, Bible verses or
//     Freeform items, each slide as an output paints it, on one output or on
//     every output (a folder each). A service exports its rows under its
//     service themes; library content exports as it plays from the library.
//   - Songs as OpenLyrics XML, a file per song, into a folder.
//   - Video, image and audio files back out of the library into a folder,
//     named by their titles; a library folder becomes a folder.
//   - Freeform items as a Freeform file (.ssff for one, .ssfb for several),
//     with the pictures, videos and fonts they use (exchange/bundle.hpp).
//   - A service as a package: a ZIP holding a Freeform file of its songs,
//     Bible passages and Freeform items (songs and Bible converted as they
//     play from the service) and its rundown, with its pictures, videos and
//     audio as files of their own.
//
// What an export reads from the library (songs, passages, Freeform
// documents, service rows, the outputs and their themes, folder trees) is
// read on the GUI thread when it is asked for (see gui_thread.hpp). The work -
// laying out and painting slides, writing and copying files - runs on one
// worker thread, an export at a time in the order they were asked for, so a
// long one holds up neither the operator nor the outputs. Each export reports
// exactly once, from the event loop, on ``exportFinished``. Nothing is ever
// left half written under a name: every file is written beside its place
// under a hidden name and moved there once whole, and an existing file in an
// export folder is never replaced (the new one is numbered instead).
//
// SessionFacade owns one and exposes it as `session.exporter`.

#include "media_kind_qml.hpp"

#include <QtQml/qqmlregistration.h>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>

namespace seventhslide::session {
class Session;
}

namespace seventhslide::app {

class FreeformLibraryController;
class LibraryController;
class MediaController;
class OpenLibrary;
class PreviewResources;

/// How far an export has got and whether to stop, for the work on the
/// worker thread.
class ExportProgress {
public:
    virtual ~ExportProgress() = default;
    /// The export was cancelled: stop at the next file or slide.
    [[nodiscard]] virtual bool stopped() const = 0;
    /// ``fraction`` (0 to 1) of the export is done. Cheap to call often.
    virtual void report(double fraction) const = 0;
};

/// What an export did: ``count`` files or images written, ``skipped``
/// things with nothing to write (a video in a service exported as images),
/// one entry in ``errors`` for each that failed, and where it wrote.
struct ExportOutcome {
    int count = 0;
    int skipped = 0;
    QStringList errors;
    /// What went otherwise than asked without failing it: a picture that was
    /// not on this computer, a row a package could not carry.
    QStringList notes;
    QString path;
    bool stopped = false;
};

class ExportController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The session owns Export: session.exporter")

    /// An export has been asked for and has not reported.
    Q_PROPERTY(bool exporting READ exporting NOTIFY progressChanged)
    /// 0 to 1 across every export that has not reported.
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    /// Where the export dialogs start (see ``lastFolder()``).
    Q_PROPERTY(QUrl lastFolder READ lastFolder NOTIFY lastFolderChanged)

public:
    ExportController(session::Session& session,
                     PreviewResources& previews,
                     OpenLibrary& open_library,
                     LibraryController& library,
                     FreeformLibraryController& freeform,
                     MediaController& media,
                     QObject* parent = nullptr);
    /// Stops every export and waits for the worker; the one running stops
    /// at its next slide or file chunk.
    ~ExportController() override;

    ExportController(const ExportController&) = delete;
    ExportController& operator=(const ExportController&) = delete;
    ExportController(ExportController&&) = delete;
    ExportController& operator=(ExportController&&) = delete;

    // Every export returns a request id at once; ``exportFinished`` reports
    // it once: ``{ok, count, skipped, errors, path}``, ``ok`` when nothing
    // failed, ``path`` the archive or folder written to. ``outputId`` names
    // the output whose slides are exported; empty for every output. ``file``
    // and ``folder`` are paths or file:// URLs.

    /// The rows of service ``serviceId`` as images, under ``themeMap`` (the
    /// service's per-output themes, ``{outputId: {text?, bg?}}``: Service
    /// Settings passes the map it is editing, saved or not).
    Q_INVOKABLE qint64 exportServiceImages(qint64 serviceId,
                                           const QVariant& themeMap,
                                           const QString& outputId,
                                           const QString& file);
    /// Library songs ``songIds`` as images, in that order.
    Q_INVOKABLE qint64 exportSongImages(const QVariantList& songIds,
                                        const QString& outputId,
                                        const QString& file);
    /// Verses of the chapter the Bibles tab shows, as images: one passage
    /// per ``{verseStart, verseEnd}`` of ``ranges``, as Add makes rows.
    Q_INVOKABLE qint64 exportBibleImages(const QVariantList& ranges,
                                         const QString& outputId,
                                         const QString& file);
    /// Freeform items ``itemIds``, then the items in ``folderIds`` and in
    /// their folders, as images.
    Q_INVOKABLE qint64 exportFreeformImages(const QVariantList& itemIds,
                                            const QVariantList& folderIds,
                                            const QString& outputId,
                                            const QString& file);
    /// Library songs ``songIds`` as OpenLyrics files in ``folder``.
    Q_INVOKABLE qint64 exportSongXml(const QVariantList& songIds, const QString& folder);
    /// Library files ``filenames`` of ``kind``, and folders ``folderIds`` with
    /// everything in them, copied into ``folder``.
    Q_INVOKABLE qint64 exportMediaFiles(seventhslide::app::MediaKindQml::Kind kind,
                                        const QStringList& filenames,
                                        const QVariantList& folderIds,
                                        const QString& folder);

    /// Freeform items ``itemIds``, then the folders ``folderIds`` with all
    /// they hold, kept as folders, as one Freeform file at ``file``.
    Q_INVOKABLE qint64 exportFreeformFile(const QVariantList& itemIds,
                                          const QVariantList& folderIds,
                                          const QString& file);
    /// Service ``serviceId`` as a package at ``file``, under ``themeMap`` as
    /// ``exportServiceImages`` takes it.
    Q_INVOKABLE qint64 exportServicePackage(qint64 serviceId,
                                            const QVariant& themeMap,
                                            const QString& file);

    /// Where the save dialog suggests an image export of ``title`` goes: a
    /// ``.zip`` named for it, in ``lastFolder``.
    Q_INVOKABLE QUrl suggestedFile(const QString& title) const;
    /// The same with ``extension`` (".ssff", ".ssfb", ".zip").
    Q_INVOKABLE QUrl suggestedFileWith(const QString& title, const QString& extension) const;
    /// The extension a Freeform file of ``items`` items, and folders or not,
    /// takes: ".ssff" for one item, else ".ssfb".
    Q_INVOKABLE QString freeformFileExtension(int items, bool anyFolder) const;
    /// The folder the last export went to this run, else the Documents
    /// folder: where the export dialogs start.
    [[nodiscard]] QUrl lastFolder() const;

    /// Stop every export: each reports as stopped and leaves no file of its
    /// own behind. The library closing calls this, as what an export reads
    /// (its pictures, its media files) goes with it.
    void cancel_exports();

    [[nodiscard]] bool exporting() const noexcept { return pending_ > 0; }
    [[nodiscard]] double progress() const noexcept;

    /// The work of one export, run on the worker thread.
    using Work = std::function<ExportOutcome(const ExportProgress&)>;

signals:
    void exportFinished(qint64 requestId, const QVariantMap& result);
    void progressChanged();
    void lastFolderChanged();

private:
    using CancelFlag = std::shared_ptr<std::atomic<bool>>;

    /// Queue ``work``, which writes into ``folder``; its outcome is reported
    /// under the id returned.
    qint64 start(const std::filesystem::path& folder, Work work);
    /// An export that cannot start: a new request id whose report says ``why``.
    qint64 refuse(const QString& why);
    /// GUI thread.
    void report(qint64 id, const ExportOutcome& outcome);
    void set_running_fraction(double fraction);

    session::Session& session_;
    PreviewResources& previews_;
    OpenLibrary& open_library_;
    LibraryController& library_;
    FreeformLibraryController& freeform_;
    MediaController& media_;
    qint64 last_request_id_ = 0;
    std::filesystem::path last_folder_;
    /// Exports asked for and not reported, and those reported since the
    /// last time none were pending, for ``progress``.
    int pending_ = 0;
    int reported_ = 0;
    double running_fraction_ = 0.0;
    /// Shared with the exports asked for since the last cancel.
    CancelFlag cancelled_ = std::make_shared<std::atomic<bool>>(false);
    /// One thread: exports write in order and contend for no disk.
    QThreadPool pool_;
};

}  // namespace seventhslide::app
