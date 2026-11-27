#pragma once

// The media tools the app runs beside itself - ffmpeg, ffprobe and yt-dlp:
// where each was found and which version it is, for Settings › Tools and
// for the jobs that convert and download media (``jobs()``).
//
// Nothing is looked for until ``refresh`` is called (Settings calls it when
// its page opens; a job's caller calls ``ensure_refreshed`` first), so
// making a session starts no process. Each version is read by running the
// tool once, as a job of its own, beside any real work.
//
// SessionFacade owns one and exposes it as `session.tools`.

#include "tool_jobs.hpp"
#include "ytdlp_updater.hpp"

#include <seventhslide/platform/tools.hpp>

#include <QtQml/qqmlregistration.h>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::app {

class ToolsController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The session owns the media tools: session.tools")

    /// One entry per tool, in ``k_tool_names``' order: ``name``, ``found``,
    /// ``path``, ``source`` (chosen / managed / bundled / system), ``version``,
    /// ``probing`` (its version is being read) and ``error`` (it is there but
    /// would not run).
    Q_PROPERTY(QVariantList tools READ tools NOTIFY toolsChanged)
    /// A version is still being read.
    Q_PROPERTY(bool probing READ probing NOTIFY toolsChanged)
    /// Where the app keeps the copies it manages.
    Q_PROPERTY(QString toolsDir READ toolsDir NOTIFY toolsChanged)
    /// The app's own yt-dlp, and keeping it up to date.
    Q_PROPERTY(seventhslide::app::YtdlpUpdater* ytdlp READ ytdlp CONSTANT)

public:
    struct Tool {
        std::string name;
        std::optional<platform::ToolLocation> location;
        std::string version;
        /// Why the version could not be read, from the tool's stderr.
        std::string error;
        bool probing = false;
    };

    explicit ToolsController(platform::ToolSearch search, QObject* parent = nullptr);
    ~ToolsController() override;

    /// Look elsewhere (tests). Forgets what was found until the next refresh.
    void set_search(platform::ToolSearch search);

    /// Look for every tool again and read its version.
    Q_INVOKABLE void refresh();
    /// ``refresh`` once, if it has not run yet.
    void ensure_refreshed();
    [[nodiscard]] bool refreshed() const noexcept { return refreshed_; }

    [[nodiscard]] const std::vector<Tool>& found() const noexcept { return tools_; }
    [[nodiscard]] std::optional<platform::ToolLocation> location(std::string_view name) const;

    [[nodiscard]] QVariantList tools() const;
    [[nodiscard]] bool probing() const;
    [[nodiscard]] QString toolsDir() const;

    [[nodiscard]] ToolJobs& jobs() noexcept { return jobs_; }
    /// The jobs' tool that reads ``name``'s version: a lane of its own, so a
    /// version is read beside a conversion, not after it.
    [[nodiscard]] static std::string version_lane(std::string_view name);
    [[nodiscard]] const platform::ToolSearch& search() const noexcept { return search_; }
    [[nodiscard]] YtdlpUpdater* ytdlp() noexcept { return ytdlp_.get(); }

signals:
    void toolsChanged();

private:
    void probe(Tool& tool);

    platform::ToolSearch search_;
    std::vector<Tool> tools_;
    ToolJobs jobs_;
    /// The version probes still out, so a refresh can drop them.
    std::vector<qint64> probes_;
    bool refreshed_ = false;
    /// Last: it reads the search above.
    std::unique_ptr<YtdlpUpdater> ytdlp_;
};

} // namespace seventhslide::app
