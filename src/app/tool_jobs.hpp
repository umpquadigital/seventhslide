#pragma once

// Runs ffmpeg, ffprobe and yt-dlp as jobs: one process each, started from
// the GUI thread and read as it prints, so a conversion or a download never
// blocks the operator UI, the outputs or the remote. The process is the
// worker; no thread of the app's is tied up.
//
// Jobs of one tool run one at a time, in the order they were started, so two
// conversions do not fight for the CPU and two downloads for the network;
// different tools run beside each other. A job reports back exactly once,
// from the event loop, even when its program could not be started, so the
// caller is never re-entered from inside ``start``.
//
// Cancelling asks nicely first (ffmpeg stops cleanly on ``q`` and finishes
// the file it is writing), then terminates, then kills after a grace period.
// A cancelled job reports Cancelled whatever its exit code was.
//
// Seam: Qt Core. Call on the GUI thread.

#include <QObject>
#include <QProcess>
#include <QtGlobal>

#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class QTimer;

namespace seventhslide::app {

/// What one job runs, and how its output reads.
struct ToolJobSpec {
    /// ffmpeg, ffprobe or yt-dlp: jobs of one tool run one at a time.
    std::string tool;
    std::filesystem::path program;
    std::vector<std::string> args;
    /// What the UI calls it: the file converted, the link downloaded.
    std::string display;
    /// Reads one line of the program's output; the job's progress, 0 to 1,
    /// when the line says how far it is. Unset for a job with no progress.
    std::function<std::optional<double>(std::string_view line)> progress;
    /// Which output ``progress`` reads: ffmpeg's ``-progress pipe:1`` and
    /// yt-dlp print theirs on stdout.
    QProcess::ProcessChannel progress_channel = QProcess::StandardOutput;
    /// Keep everything the program prints on stdout (ffprobe's JSON, yt-dlp's
    /// listing). Off, stdout is read only for progress.
    bool capture_stdout = false;
    /// Written to the program's stdin to ask it to stop: ``q`` for ffmpeg.
    /// Empty for a program that takes no such request.
    std::string stop_input;
};

enum class ToolJobState { Queued, Running, Done, Failed, Cancelled };

[[nodiscard]] std::string_view to_string(ToolJobState state) noexcept;

struct ToolJob {
    qint64 id = 0;
    std::string tool;
    std::string display;
    ToolJobState state = ToolJobState::Queued;
    /// 0 to 1; 1 once Done.
    double progress = 0.0;
    /// The program's exit code once it has exited; -1 before, or if it never
    /// ran or crashed.
    int exit_code = -1;
    /// Why it Failed: the last lines it printed on stderr, or why it could
    /// not start. Empty otherwise.
    std::string error;
    /// What it printed on stdout, with ``capture_stdout``.
    std::string output;
};

class ToolJobs : public QObject {
    Q_OBJECT

public:
    /// Receives the job once it is Done, Failed or Cancelled.
    using Finished = std::function<void(const ToolJob&)>;

    /// After terminate, how long before kill.
    static constexpr int k_grace_ms = 2000;
    /// How many stderr lines a failure keeps.
    static constexpr std::size_t k_error_lines = 30;

    explicit ToolJobs(QObject* parent = nullptr);
    /// Kills what is running and waits for it, briefly. Nothing reports
    /// after this.
    ~ToolJobs() override;

    ToolJobs(const ToolJobs&) = delete;
    ToolJobs& operator=(const ToolJobs&) = delete;
    ToolJobs(ToolJobs&&) = delete;
    ToolJobs& operator=(ToolJobs&&) = delete;

    /// Queue ``spec`` behind the jobs of its tool. ``on_finished`` runs once,
    /// from the event loop, unless this object is destroyed first. Returns
    /// the job's id.
    qint64 start(ToolJobSpec spec, Finished on_finished);

    /// Stop job ``id``: taken out of the queue, or asked, terminated and then
    /// killed. It reports Cancelled. Unknown ids are ignored.
    void cancel(qint64 id);
    /// ``cancel`` for every job that has not reported.
    void cancel_all();

    /// Every job that has not reported, queued and running, oldest first.
    [[nodiscard]] std::vector<ToolJob> jobs() const;
    /// A job is queued or running.
    [[nodiscard]] bool busy() const noexcept { return !entries_.empty(); }
    /// The running job of ``tool``, if any.
    [[nodiscard]] std::optional<ToolJob> running(std::string_view tool) const;
    /// A job of ``tool`` has not reported yet: queued, running, or cancelled
    /// and still stopping.
    [[nodiscard]] bool has_jobs(std::string_view tool) const;

signals:
    /// A job was started, moved, or reported; a job's progress moved.
    void changed();

private:
    struct Entry {
        ToolJob job;
        ToolJobSpec spec;
        Finished on_finished;
        std::unique_ptr<QProcess> process;
        QTimer* kill_timer = nullptr;
        bool cancel_asked = false;
        /// Output not yet ended by a line break, per channel.
        std::string stdout_partial;
        std::string stderr_partial;
        std::deque<std::string> stderr_tail;
    };

    /// Start the first queued job of every tool that has none running.
    void pump();
    void launch(Entry& entry);
    /// Whole lines from ``channel``; the parser and the tail read them.
    void read(Entry& entry, QProcess::ProcessChannel channel);
    void take_lines(Entry& entry, QProcess::ProcessChannel channel, std::string_view chunk);
    void on_line(Entry& entry, QProcess::ProcessChannel channel, std::string_view line);
    void on_finished(Entry& entry, int exit_code, QProcess::ExitStatus status);
    void on_error(Entry& entry, QProcess::ProcessError error);
    /// Mark ``entry`` ``state`` and hand it over from the event loop.
    void report(Entry& entry, ToolJobState state, std::string error = {});
    [[nodiscard]] Entry* find(qint64 id);
    [[nodiscard]] std::string error_tail(const Entry& entry) const;

    std::vector<std::unique_ptr<Entry>> entries_;
    qint64 next_id_ = 1;
};

} // namespace seventhslide::app
