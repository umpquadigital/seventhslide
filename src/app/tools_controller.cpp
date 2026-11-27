// Where the media tools are and which versions; see tools_controller.hpp.

#include "tools_controller.hpp"

#include <QVariantMap>

#include <algorithm>
#include <utility>

namespace seventhslide::app {

namespace {

/// The flag that makes ``name`` print its version and exit.
std::string version_flag(std::string_view name)
{
    return name == platform::k_tool_ytdlp ? "--version" : "-version";
}

std::string first_line(std::string_view text)
{
    const auto end = text.find_first_of("\r\n");
    return std::string{end == std::string_view::npos ? text : text.substr(0, end)};
}

} // namespace

ToolsController::ToolsController(platform::ToolSearch search, QObject* parent)
    : QObject(parent)
    , search_(std::move(search))
{
    connect(&jobs_, &ToolJobs::changed, this, &ToolsController::toolsChanged);
    ytdlp_ = std::make_unique<YtdlpUpdater>(*this);
}

ToolsController::~ToolsController() = default;

void ToolsController::set_search(platform::ToolSearch search)
{
    search_ = std::move(search);
    for (const qint64 id : probes_) {
        jobs_.cancel(id);
    }
    probes_.clear();
    tools_.clear();
    refreshed_ = false;
    emit toolsChanged();
}

void ToolsController::refresh()
{
    // A probe still out would answer for a tool that may have moved.
    for (const qint64 id : probes_) {
        jobs_.cancel(id);
    }
    probes_.clear();
    tools_.clear();
    for (const std::string_view name : platform::k_tool_names) {
        Tool tool;
        tool.name = std::string{name};
        tool.location = platform::find_tool(name, search_);
        tools_.push_back(std::move(tool));
    }
    refreshed_ = true;
    for (Tool& tool : tools_) {
        probe(tool);
    }
    emit toolsChanged();
}

void ToolsController::ensure_refreshed()
{
    if (!refreshed_) {
        refresh();
    }
}

std::optional<platform::ToolLocation> ToolsController::location(std::string_view name) const
{
    for (const Tool& tool : tools_) {
        if (tool.name == name) {
            return tool.location;
        }
    }
    return std::nullopt;
}

QVariantList ToolsController::tools() const
{
    QVariantList list;
    for (const Tool& tool : tools_) {
        QVariantMap entry;
        entry.insert(QStringLiteral("name"), QString::fromStdString(tool.name));
        entry.insert(QStringLiteral("found"), tool.location.has_value());
        entry.insert(QStringLiteral("path"),
                     tool.location ? QString::fromStdString(tool.location->path.string())
                                   : QString{});
        entry.insert(QStringLiteral("source"),
                     tool.location ? QString::fromUtf8(platform::to_string(tool.location->source))
                                   : QString{});
        entry.insert(QStringLiteral("version"), QString::fromStdString(tool.version));
        entry.insert(QStringLiteral("probing"), tool.probing);
        entry.insert(QStringLiteral("error"), QString::fromStdString(tool.error));
        list.push_back(entry);
    }
    return list;
}

bool ToolsController::probing() const
{
    return std::any_of(tools_.begin(), tools_.end(),
                       [](const Tool& tool) { return tool.probing; });
}

QString ToolsController::toolsDir() const
{
    return QString::fromStdString(search_.tools_dir.string());
}

void ToolsController::probe(Tool& tool)
{
    if (!tool.location) {
        return;  // not found: no version to read
    }
    ToolJobSpec spec;
    // Its own lane: a version is read beside a conversion, not after it.
    spec.tool = "version:" + tool.name;
    spec.program = tool.location->path;
    spec.args = {version_flag(tool.name)};
    spec.display = tool.name;
    spec.capture_stdout = true;
    tool.probing = true;
    tool.version.clear();
    tool.error.clear();
    const std::string name = tool.name;
    const qint64 id = jobs_.start(std::move(spec), [this, name](const ToolJob& job) {
        probes_.erase(std::remove(probes_.begin(), probes_.end(), job.id), probes_.end());
        const auto it = std::find_if(tools_.begin(), tools_.end(),
                                     [&name](const Tool& t) { return t.name == name; });
        if (it == tools_.end() || !it->probing) {
            return;
        }
        it->probing = false;
        if (job.state == ToolJobState::Done) {
            it->version = platform::parse_tool_version(first_line(job.output));
        } else if (job.state == ToolJobState::Failed) {
            it->error = job.error;
        }
        emit toolsChanged();
    });
    probes_.push_back(id);
}

} // namespace seventhslide::app
