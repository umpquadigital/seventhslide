// Runs the media tools as jobs; see tool_jobs.hpp.

#include "tool_jobs.hpp"

#include "seventhslide/util/strings.hpp"

#include <QMetaObject>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace seventhslide::app {

namespace {

/// How long the destructor waits for a killed process.
constexpr int k_destroy_wait_ms = 1000;

bool ended(ToolJobState state) noexcept
{
    return state == ToolJobState::Done || state == ToolJobState::Failed
           || state == ToolJobState::Cancelled;
}

} // namespace

std::string_view to_string(ToolJobState state) noexcept
{
    switch (state) {
    case ToolJobState::Queued:
        return "queued";
    case ToolJobState::Running:
        return "running";
    case ToolJobState::Done:
        return "done";
    case ToolJobState::Failed:
        return "failed";
    case ToolJobState::Cancelled:
        break;
    }
    return "cancelled";
}

ToolJobs::ToolJobs(QObject* parent)
    : QObject(parent)
{
}

ToolJobs::~ToolJobs()
{
    for (auto& entry : entries_) {
        if (entry->process && entry->process->state() != QProcess::NotRunning) {
            // Nothing reports from here: the signals go with the process.
            entry->process->disconnect(this);
            entry->process->kill();
            entry->process->waitForFinished(k_destroy_wait_ms);
        }
    }
}

qint64 ToolJobs::start(ToolJobSpec spec, Finished on_finished)
{
    auto entry = std::make_unique<Entry>();
    entry->job.id = next_id_++;
    entry->job.tool = spec.tool;
    entry->job.display = spec.display;
    entry->spec = std::move(spec);
    entry->on_finished = std::move(on_finished);
    const qint64 id = entry->job.id;
    entries_.push_back(std::move(entry));
    emit changed();
    pump();
    return id;
}

void ToolJobs::cancel(qint64 id)
{
    Entry* entry = find(id);
    if (entry == nullptr || entry->cancel_asked) {
        return;
    }
    entry->cancel_asked = true;
    if (entry->job.state == ToolJobState::Queued) {
        report(*entry, ToolJobState::Cancelled);
        return;
    }
    QProcess& process = *entry->process;
    if (process.state() == QProcess::NotRunning) {
        return;  // Exited already; its finished signal is on its way.
    }
    if (!entry->spec.stop_input.empty()) {
        process.write(entry->spec.stop_input.data(),
                      static_cast<qint64>(entry->spec.stop_input.size()));
    }
    process.closeWriteChannel();
    process.terminate();
    entry->kill_timer = new QTimer(this);
    entry->kill_timer->setSingleShot(true);
    entry->kill_timer->setInterval(k_grace_ms);
    connect(entry->kill_timer, &QTimer::timeout, this, [id, this] {
        if (Entry* e = find(id); e != nullptr && e->process
            && e->process->state() != QProcess::NotRunning) {
            e->process->kill();
        }
    });
    entry->kill_timer->start();
}

void ToolJobs::cancel_all()
{
    std::vector<qint64> ids;
    ids.reserve(entries_.size());
    for (const auto& entry : entries_) {
        ids.push_back(entry->job.id);
    }
    for (const qint64 id : ids) {
        cancel(id);
    }
}

std::vector<ToolJob> ToolJobs::jobs() const
{
    std::vector<ToolJob> out;
    out.reserve(entries_.size());
    for (const auto& entry : entries_) {
        if (!ended(entry->job.state)) {
            out.push_back(entry->job);
        }
    }
    return out;
}

std::optional<ToolJob> ToolJobs::running(std::string_view tool) const
{
    for (const auto& entry : entries_) {
        if (entry->job.state == ToolJobState::Running && entry->job.tool == tool) {
            return entry->job;
        }
    }
    return std::nullopt;
}

bool ToolJobs::has_jobs(std::string_view tool) const
{
    return std::any_of(entries_.begin(), entries_.end(),
                       [tool](const auto& entry) { return entry->job.tool == tool; });
}

void ToolJobs::pump()
{
    for (auto& candidate : entries_) {
        if (candidate->job.state != ToolJobState::Queued) {
            continue;
        }
        if (running(candidate->job.tool).has_value()) {
            continue;
        }
        launch(*candidate);
    }
}

void ToolJobs::launch(Entry& entry)
{
    entry.job.state = ToolJobState::Running;
    entry.process = std::make_unique<QProcess>(this);
    QProcess& process = *entry.process;
    process.setProgram(QString::fromStdString(entry.spec.program.string()));
    QStringList args;
    for (const auto& arg : entry.spec.args) {
        args << QString::fromStdString(arg);
    }
    process.setArguments(args);
    // Plain output: no colour codes in what the parsers and the error read.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("AV_LOG_FORCE_NOCOLOR"), QStringLiteral("1"));
    env.insert(QStringLiteral("NO_COLOR"), QStringLiteral("1"));
    process.setProcessEnvironment(env);

    const qint64 id = entry.job.id;
    connect(&process, &QProcess::readyReadStandardOutput, this, [id, this] {
        if (Entry* e = find(id)) {
            read(*e, QProcess::StandardOutput);
        }
    });
    connect(&process, &QProcess::readyReadStandardError, this, [id, this] {
        if (Entry* e = find(id)) {
            read(*e, QProcess::StandardError);
        }
    });
    connect(&process, &QProcess::finished, this,
            [id, this](int exit_code, QProcess::ExitStatus status) {
                if (Entry* e = find(id)) {
                    on_finished(*e, exit_code, status);
                }
            });
    connect(&process, &QProcess::errorOccurred, this, [id, this](QProcess::ProcessError error) {
        if (Entry* e = find(id)) {
            on_error(*e, error);
        }
    });
    emit changed();
    process.start(QIODevice::ReadWrite);
}

void ToolJobs::read(Entry& entry, QProcess::ProcessChannel channel)
{
    const QByteArray chunk = channel == QProcess::StandardOutput
                                 ? entry.process->readAllStandardOutput()
                                 : entry.process->readAllStandardError();
    take_lines(entry, channel, std::string_view{chunk.constData(),
                                                static_cast<std::size_t>(chunk.size())});
}

void ToolJobs::take_lines(Entry& entry, QProcess::ProcessChannel channel, std::string_view chunk)
{
    std::string& partial = channel == QProcess::StandardOutput ? entry.stdout_partial
                                                                : entry.stderr_partial;
    if (channel == QProcess::StandardOutput && entry.spec.capture_stdout) {
        entry.job.output.append(chunk);
    }
    partial.append(chunk);
    // ffmpeg rewrites its status line with \r; a line is what sits between
    // any two breaks.
    std::size_t start = 0;
    for (;;) {
        const std::size_t end = partial.find_first_of("\r\n", start);
        if (end == std::string::npos) {
            break;
        }
        on_line(entry, channel, std::string_view{partial}.substr(start, end - start));
        start = end + 1;
    }
    partial.erase(0, start);
}

void ToolJobs::on_line(Entry& entry, QProcess::ProcessChannel channel, std::string_view line)
{
    if (channel == QProcess::StandardError) {
        const std::string_view kept = util::trim_view(line);
        if (!kept.empty()) {
            entry.stderr_tail.emplace_back(kept);
            while (entry.stderr_tail.size() > k_error_lines) {
                entry.stderr_tail.pop_front();
            }
        }
    }
    if (channel != entry.spec.progress_channel || !entry.spec.progress) {
        return;
    }
    if (const auto fraction = entry.spec.progress(line)) {
        const double next = std::clamp(*fraction, 0.0, 1.0);
        if (next != entry.job.progress) {
            entry.job.progress = next;
            emit changed();
        }
    }
}

void ToolJobs::on_finished(Entry& entry, int exit_code, QProcess::ExitStatus status)
{
    // Whatever was printed after the last read, and a last line with no
    // break after it.
    read(entry, QProcess::StandardOutput);
    read(entry, QProcess::StandardError);
    if (!entry.stdout_partial.empty()) {
        on_line(entry, QProcess::StandardOutput, entry.stdout_partial);
        entry.stdout_partial.clear();
    }
    if (!entry.stderr_partial.empty()) {
        on_line(entry, QProcess::StandardError, entry.stderr_partial);
        entry.stderr_partial.clear();
    }
    entry.job.exit_code = status == QProcess::NormalExit ? exit_code : -1;
    if (entry.cancel_asked) {
        report(entry, ToolJobState::Cancelled);
        return;
    }
    if (status != QProcess::NormalExit) {
        std::string error = error_tail(entry);
        report(entry, ToolJobState::Failed,
               error.empty() ? std::string{"The program stopped unexpectedly."}
                             : std::move(error));
        return;
    }
    if (exit_code != 0) {
        std::string error = error_tail(entry);
        report(entry, ToolJobState::Failed,
               error.empty() ? "Exited with code " + std::to_string(exit_code)
                             : std::move(error));
        return;
    }
    entry.job.progress = 1.0;
    report(entry, ToolJobState::Done);
}

void ToolJobs::on_error(Entry& entry, QProcess::ProcessError error)
{
    // Every other error ends in finished, which reports.
    if (error != QProcess::FailedToStart) {
        return;
    }
    if (entry.cancel_asked) {
        report(entry, ToolJobState::Cancelled);
        return;
    }
    report(entry, ToolJobState::Failed,
           "Could not start " + entry.spec.program.string() + ".");
}

void ToolJobs::report(Entry& entry, ToolJobState state, std::string error)
{
    if (ended(entry.job.state)) {
        return;
    }
    entry.job.state = state;
    entry.job.error = std::move(error);
    if (entry.kill_timer != nullptr) {
        entry.kill_timer->stop();
        entry.kill_timer->deleteLater();
        entry.kill_timer = nullptr;
    }
    const qint64 id = entry.job.id;
    QMetaObject::invokeMethod(
        this,
        [id, this] {
            const auto it = std::find_if(entries_.begin(), entries_.end(),
                                         [id](const auto& e) { return e->job.id == id; });
            if (it == entries_.end()) {
                return;
            }
            std::unique_ptr<Entry> done = std::move(*it);
            entries_.erase(it);
            if (done->process) {
                done->process->disconnect(this);
                done->process.release()->deleteLater();
            }
            emit changed();
            pump();
            if (done->on_finished) {
                done->on_finished(done->job);
            }
        },
        Qt::QueuedConnection);
}

ToolJobs::Entry* ToolJobs::find(qint64 id)
{
    for (auto& entry : entries_) {
        if (entry->job.id == id) {
            return entry.get();
        }
    }
    return nullptr;
}

std::string ToolJobs::error_tail(const Entry& entry) const
{
    std::string out;
    for (const auto& line : entry.stderr_tail) {
        if (!out.empty()) {
            out += '\n';
        }
        out += line;
    }
    return out;
}

} // namespace seventhslide::app
