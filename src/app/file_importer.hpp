#pragma once

// Copies files the operator brings into the library, off the GUI thread.
//
// A video can be gigabytes. Copied on the GUI thread, it froze the operator
// UI, the outputs and the remote for as long as the copy took. Here the bytes
// move on one worker thread. Everything that touches the library (final names,
// the database, themes, the browsers) stays with the owner, on the GUI thread,
// in the callback a batch finishes with.
//
// Each file is copied to a hidden staging name in the directory it is going
// to. Moving it into place afterwards is then a rename on one filesystem, and
// a half-copied file never sits under a name the library lists.
//
// Batches run one at a time, in the order they were started. Each one reports
// back exactly once, on the GUI thread, from the event loop. The report never
// arrives from inside another call, so the session is not re-entered mid-change.
//
// Seam: Qt Core. Call on the GUI thread.

#include <QCoreApplication>
#include <QObject>
#include <QThreadPool>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace seventhslide::app {

struct ImportFile {
    std::filesystem::path source;
    /// The source's file name: what errors call it.
    std::string display;
    /// Where the worker left the bytes. Empty unless the copy succeeded.
    std::filesystem::path staged;
    /// Why the file was not copied. Empty when it was, or before the worker
    /// has run. A file that arrives with an error is not copied.
    std::string error;
};

struct ImportBatch {
    qint64 id = 0;
    /// The directory the files are going to.
    std::filesystem::path dir;
    std::uintmax_t max_bytes = std::numeric_limits<std::uintmax_t>::max();
    std::vector<ImportFile> files;
};

/// Why a file was not imported, as its error says it after its name
/// ("song.mp4: ..."): it was not there, it was too large, it could not be
/// copied in, or the import was cancelled (the library it was for closed).
[[nodiscard]] inline std::string import_not_found()
{
    return QCoreApplication::translate("FileImporter", "the file could not be found").toStdString();
}
[[nodiscard]] inline std::string import_too_large()
{
    return QCoreApplication::translate("FileImporter", "the file is larger than a library takes")
        .toStdString();
}
[[nodiscard]] inline std::string import_not_copied()
{
    return QCoreApplication::translate("FileImporter",
                                       "the file could not be copied into the library")
        .toStdString();
}
[[nodiscard]] inline std::string import_stopped()
{
    return QCoreApplication::translate("FileImporter",
                                       "the library was closed before the file was copied in")
        .toStdString();
}

class FileImporter : public QObject {
public:
    /// Receives a batch once its files are copied, or once it has been
    /// cancelled. Staged files then belong to the receiver: it moves them into
    /// place or removes them.
    using Finished = std::function<void(ImportBatch)>;

    explicit FileImporter(std::function<void()> on_progress, QObject* parent = nullptr);
    /// Cancels everything and waits for the worker. The running copy stops
    /// within one chunk, so this does not wait for a large file to finish.
    ~FileImporter() override;

    FileImporter(const FileImporter&) = delete;
    FileImporter& operator=(const FileImporter&) = delete;
    FileImporter(FileImporter&&) = delete;
    FileImporter& operator=(FileImporter&&) = delete;

    /// Queue ``batch`` behind any batch already started. ``on_finished`` runs
    /// once, on the GUI thread, unless this importer is destroyed first.
    void start(ImportBatch batch, Finished on_finished);

    /// Stop the running copy after its current chunk and every queued batch
    /// before it starts. Each one still reports, with every file it had not
    /// handed over marked ``import_stopped()`` and nothing of it left on disk.
    /// Batches started afterwards are not affected.
    void cancel_all();

    /// A batch has been started and has not reported yet.
    [[nodiscard]] bool busy() const noexcept { return files_total_ > 0; }

    /// 0 to 1 over the files of every batch that has not reported yet.
    [[nodiscard]] double progress() const noexcept;

    /// Remove the staging files a crash left in ``dir``, and a download's
    /// staging folders with what is in them.
    static void sweep_staging(const std::filesystem::path& dir);

private:
    using CancelFlag = std::shared_ptr<std::atomic<bool>>;

    /// Worker thread.
    void copy_batch(ImportBatch& batch, const CancelFlag& cancelled);
    /// GUI thread.
    void file_progress(double fraction);
    void file_done();
    void deliver(ImportBatch batch, const CancelFlag& cancelled, const Finished& on_finished);

    std::function<void()> on_progress_;
    /// Shared with the jobs of the batches started since the last cancel_all.
    CancelFlag cancelled_ = std::make_shared<std::atomic<bool>>(false);
    int files_total_ = 0;
    int files_done_ = 0;
    double current_fraction_ = 0.0;
    /// One thread, so batches copy in order and contend for no disk.
    QThreadPool pool_;
};

}  // namespace seventhslide::app
