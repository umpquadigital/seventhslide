#pragma once

// The work Convert and Download each run with a tool (ffmpeg, yt-dlp), as
// their QML job lists show it: one entry per conversion or download, found
// by id, cancelled through the tool job running for it, and taken off the
// list when it ends.
//
// ``Entry`` is the controller's own struct. It has at least ``qint64 id``,
// ``std::string title``, ``qint64 job_id`` (the tool job running for it; 0
// while none is) and ``bool cancelled``. ``showing`` says when its tool
// job's progress is its own: a conversion's only once it writes (its first
// job only reads the source).

#include "tool_jobs.hpp"

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtTypes>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace seventhslide::app {

template <typename Entry>
class ToolJobList {
public:
    using Showing = bool (*)(const Entry&);

    explicit ToolJobList(Showing showing = [](const Entry&) { return true; })
        : showing_(showing)
    {
    }

    Entry& add(std::unique_ptr<Entry> entry)
    {
        entries_.push_back(std::move(entry));
        return *entries_.back();
    }

    [[nodiscard]] Entry* find(qint64 id) const
    {
        const auto it = position(id);
        return it == entries_.end() ? nullptr : it->get();
    }

    /// Take ``id`` off the list; null when it is not on it.
    [[nodiscard]] std::unique_ptr<Entry> take(qint64 id)
    {
        const auto it = std::find_if(entries_.begin(), entries_.end(),
                                     [id](const auto& entry) { return entry->id == id; });
        if (it == entries_.end()) {
            return nullptr;
        }
        auto entry = std::move(*it);
        entries_.erase(it);
        return entry;
    }

    /// Mark ``id`` cancelled and stop the tool job running for it, which
    /// reports Cancelled. False when there is no such entry, or it already
    /// was.
    bool cancel(qint64 id, ToolJobs& tools)
    {
        Entry* entry = find(id);
        if (entry == nullptr || entry->cancelled) {
            return false;
        }
        entry->cancelled = true;
        if (entry->job_id != 0) {
            tools.cancel(entry->job_id);
        }
        return true;
    }

    /// ``[{id, title, running, progress}]``, in the order they were added.
    [[nodiscard]] QVariantList rows(const ToolJobs& tools) const
    {
        const auto running = tools.jobs();
        QVariantList list;
        for (const auto& entry : entries_) {
            const auto [active, fraction] = shown(*entry, running);
            list.push_back(QVariantMap{
                {QStringLiteral("id"), entry->id},
                {QStringLiteral("title"), QString::fromStdString(entry->title)},
                {QStringLiteral("running"), active},
                {QStringLiteral("progress"), fraction},
            });
        }
        return list;
    }

    /// The entries' progress, averaged; 0 with none.
    [[nodiscard]] double progress(const ToolJobs& tools) const
    {
        if (entries_.empty()) {
            return 0.0;
        }
        const auto running = tools.jobs();
        double sum = 0.0;
        for (const auto& entry : entries_) {
            sum += shown(*entry, running).fraction;
        }
        return sum / static_cast<double>(entries_.size());
    }

    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    [[nodiscard]] const std::vector<std::unique_ptr<Entry>>& entries() const noexcept
    {
        return entries_;
    }

private:
    struct Shown {
        bool active = false;
        double fraction = 0.0;
    };

    [[nodiscard]] auto position(qint64 id) const
    {
        return std::find_if(entries_.begin(), entries_.end(),
                            [id](const auto& entry) { return entry->id == id; });
    }

    /// Whether ``entry``'s tool job runs, and how far it has got.
    [[nodiscard]] Shown shown(const Entry& entry, const std::vector<ToolJob>& running) const
    {
        Shown out;
        if (entry.job_id == 0 || !showing_(entry)) {
            return out;
        }
        for (const auto& job : running) {
            if (job.id == entry.job_id) {
                out.active = job.state == ToolJobState::Running;
                out.fraction = job.progress;
            }
        }
        return out;
    }

    Showing showing_;
    std::vector<std::unique_ptr<Entry>> entries_;
};

} // namespace seventhslide::app
