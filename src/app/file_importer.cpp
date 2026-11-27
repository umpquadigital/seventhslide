// Copies imported files off the GUI thread; see file_importer.hpp.

#include "file_importer.hpp"

#include <seventhslide/library/library.hpp>
#include <seventhslide/util/file_copy.hpp>

#include <QMetaObject>
#include <QUuid>

#include <algorithm>
#include <chrono>
#include <string_view>
#include <system_error>
#include <utility>

namespace seventhslide::app {

namespace {

using library::k_staging_prefix;
using library::k_staging_suffix;

/// Hidden, and with an extension no listing accepts.
[[nodiscard]] std::filesystem::path staging_path(const std::filesystem::path& dir)
{
    return dir
           / (std::string(k_staging_prefix)
              + QUuid::createUuid().toString(QUuid::Id128).toStdString()
              + std::string(k_staging_suffix));
}

[[nodiscard]] bool is_staging_name(std::string_view name)
{
    return name.size() > k_staging_prefix.size() + k_staging_suffix.size()
           && name.starts_with(k_staging_prefix) && name.ends_with(k_staging_suffix);
}

/// Progress reaches the GUI thread at most this often per file.
constexpr auto k_progress_interval = std::chrono::milliseconds(50);

}  // namespace

FileImporter::FileImporter(std::function<void()> on_progress, QObject* parent)
    : QObject(parent)
    , on_progress_(std::move(on_progress))
{
    pool_.setMaxThreadCount(1);
}

FileImporter::~FileImporter()
{
    cancel_all();
    pool_.waitForDone();
}

double FileImporter::progress() const noexcept
{
    if (files_total_ <= 0) {
        return 0.0;
    }
    return std::clamp((files_done_ + current_fraction_) / files_total_, 0.0, 1.0);
}

void FileImporter::start(ImportBatch batch, Finished on_finished)
{
    files_total_ += static_cast<int>(batch.files.size());
    if (on_progress_) {
        on_progress_();
    }
    pool_.start([this, batch = std::move(batch), cancelled = cancelled_,
                 on_finished = std::move(on_finished)]() mutable {
        copy_batch(batch, cancelled);
        QMetaObject::invokeMethod(
            this,
            [this, batch = std::move(batch), cancelled,
             on_finished = std::move(on_finished)]() mutable {
                deliver(std::move(batch), cancelled, on_finished);
            },
            Qt::QueuedConnection);
    });
}

void FileImporter::cancel_all()
{
    cancelled_->store(true);
    // Batches started from now on are a new generation.
    cancelled_ = std::make_shared<std::atomic<bool>>(false);
}

void FileImporter::copy_batch(ImportBatch& batch, const CancelFlag& cancelled)
{
    for (auto& file : batch.files) {
        const auto finish_file = [this] {
            QMetaObject::invokeMethod(this, [this] { file_done(); }, Qt::QueuedConnection);
        };
        if (!file.error.empty()) {
            finish_file();
            continue;
        }
        if (cancelled->load()) {
            file.error = import_stopped();
            finish_file();
            continue;
        }
        std::error_code ec;
        const auto size = std::filesystem::file_size(file.source, ec);
        if (ec || !std::filesystem::is_regular_file(file.source, ec)) {
            file.error = import_not_found();
            finish_file();
            continue;
        }
        if (size > batch.max_bytes) {
            file.error = import_too_large();
            finish_file();
            continue;
        }
        std::filesystem::create_directories(batch.dir, ec);
        const auto staged = staging_path(batch.dir);
        auto last_report = std::chrono::steady_clock::now();
        const auto outcome = util::copy_file_chunked(
            file.source, staged, [&](std::uintmax_t copied) {
                const auto now = std::chrono::steady_clock::now();
                if (size > 0 && now - last_report >= k_progress_interval) {
                    last_report = now;
                    const double fraction =
                        static_cast<double>(copied) / static_cast<double>(size);
                    QMetaObject::invokeMethod(
                        this, [this, fraction] { file_progress(fraction); },
                        Qt::QueuedConnection);
                }
                return !cancelled->load();
            });
        switch (outcome) {
        case util::CopyOutcome::Copied:
            file.staged = staged;
            break;
        case util::CopyOutcome::Stopped:
            file.error = import_stopped();
            break;
        case util::CopyOutcome::Failed:
            file.error = import_not_copied();
            break;
        }
        finish_file();
    }
}

void FileImporter::file_progress(double fraction)
{
    current_fraction_ = fraction;
    if (on_progress_) {
        on_progress_();
    }
}

void FileImporter::file_done()
{
    ++files_done_;
    current_fraction_ = 0.0;
    if (on_progress_) {
        on_progress_();
    }
}

void FileImporter::deliver(ImportBatch batch,
                           const CancelFlag& cancelled,
                           const Finished& on_finished)
{
    // Cancelled after the copy finished but before this ran: the library the
    // files were meant for is gone, so they are too.
    if (cancelled->load()) {
        for (auto& file : batch.files) {
            if (!file.staged.empty()) {
                std::error_code ec;
                std::filesystem::remove(file.staged, ec);
                file.staged.clear();
                file.error = import_stopped();
            }
        }
    }
    files_total_ -= static_cast<int>(batch.files.size());
    files_done_ -= static_cast<int>(batch.files.size());
    if (files_total_ <= 0) {
        files_total_ = 0;
        files_done_ = 0;
        current_fraction_ = 0.0;
    }
    if (on_progress_) {
        on_progress_();
    }
    if (on_finished) {
        on_finished(std::move(batch));
    }
}

void FileImporter::sweep_staging(const std::filesystem::path& dir)
{
    std::error_code ec;
    if (dir.empty() || !std::filesystem::is_directory(dir, ec)) {
        return;
    }
    std::vector<std::filesystem::path> stale;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (is_staging_name(entry.path().filename().string())) {
            stale.push_back(entry.path());
        }
    }
    // A download's folder too, and what is in it: only the app makes a
    // folder with a staging name.
    for (const auto& path : stale) {
        std::filesystem::remove_all(path, ec);
    }
}

}  // namespace seventhslide::app
