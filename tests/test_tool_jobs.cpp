// The media tools' jobs: one process each, read as it prints, reporting
// once from the event loop; and the controller that finds the tools and
// reads their versions. A shell script stands in for each tool.

#include <catch2/catch_test_macros.hpp>

#include "test_qt_app.hpp"
#include "fake_tool.hpp"
#include "test_temp_dir.hpp"
#include "tool_jobs.hpp"
#include "tools_controller.hpp"

#include <QCoreApplication>
#include <QTest>

#include <seventhslide/platform/tools.hpp>

#include <charconv>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using seventhslide::app::ToolJob;
using seventhslide::app::ToolJobs;
using seventhslide::app::ToolJobSpec;
using seventhslide::app::ToolJobState;
using seventhslide::app::ToolsController;
using seventhslide::platform::ToolSearch;
using seventhslide::platform::ToolSource;

namespace {

/// A tool named as ``path`` says whose body is ``script`` (after
/// ``#!/bin/sh``): its path, as this system names a program.
fs::path write_script(const fs::path& path, std::string_view script)
{
    if (!seventhslide::test::fake_tools_runnable()) {
        SKIP("no sh to run a stand-in tool with");
    }
    return seventhslide::test::write_fake_tool(path.parent_path(), path.filename().string(),
                                               script);
}

/// ``progress=0.25`` lines give their number.
std::optional<double> parse_progress(std::string_view line)
{
    constexpr std::string_view key = "progress=";
    if (!line.starts_with(key)) {
        return std::nullopt;
    }
    double value = 0.0;
    const auto rest = line.substr(key.size());
    if (std::from_chars(rest.data(), rest.data() + rest.size(), value).ec != std::errc{}) {
        return std::nullopt;
    }
    return value;
}

struct Report {
    bool delivered = false;
    ToolJob job;
};

ToolJobs::Finished into(Report& report)
{
    return [&report](const ToolJob& job) {
        report.delivered = true;
        report.job = job;
    };
}

bool wait_delivered(const Report& report, int ms = seventhslide::test::k_tool_wait_ms)
{
    return QTest::qWaitFor([&report] { return report.delivered; }, ms);
}

} // namespace

TEST_CASE("A job's progress is read as it prints, and it reports Done from the event loop",
          "[tools][jobs]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir tmp;
    const auto tool = write_script(tmp.path / "fake",
                                   "echo progress=0.25\necho hello\necho progress=0.5\n"
                                   "printf 'progress=1.0'\n");
    ToolJobs jobs;
    int changes = 0;
    QObject::connect(&jobs, &ToolJobs::changed, [&changes] { ++changes; });
    Report report;
    ToolJobSpec spec;
    spec.tool = "fake";
    spec.program = tool;
    spec.display = "A job";
    spec.progress = parse_progress;
    spec.capture_stdout = true;
    const qint64 id = jobs.start(spec, into(report));
    REQUIRE(id > 0);
    CHECK(jobs.busy());
    CHECK_FALSE(report.delivered);
    REQUIRE(jobs.jobs().size() == 1);
    CHECK(jobs.jobs()[0].state == ToolJobState::Running);
    CHECK(jobs.jobs()[0].display == "A job");
    CHECK(jobs.running("fake").has_value());

    REQUIRE(wait_delivered(report));
    CHECK(report.job.id == id);
    CHECK(report.job.state == ToolJobState::Done);
    CHECK(report.job.progress == 1.0);
    CHECK(report.job.exit_code == 0);
    CHECK(report.job.error.empty());
    CHECK(report.job.output == "progress=0.25\nhello\nprogress=0.5\nprogress=1.0");
    CHECK_FALSE(jobs.busy());
    CHECK(jobs.jobs().empty());
    CHECK(changes >= 3);
}

TEST_CASE("A job that exits with an error reports Failed with the last of its stderr",
          "[tools][jobs]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir tmp;
    const auto tool = write_script(tmp.path / "fake",
                                   "echo 'warming up' >&2\necho 'boom: no such codec' >&2\n"
                                   "exit 3\n");
    ToolJobs jobs;
    Report report;
    ToolJobSpec spec;
    spec.tool = "fake";
    spec.program = tool;
    jobs.start(spec, into(report));
    REQUIRE(wait_delivered(report));
    CHECK(report.job.state == ToolJobState::Failed);
    CHECK(report.job.exit_code == 3);
    CHECK(report.job.error == "warming up\nboom: no such codec");
}

TEST_CASE("A job that exits with an error and says nothing reports its exit code",
          "[tools][jobs]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir tmp;
    const auto tool = write_script(tmp.path / "fake", "exit 7\n");
    ToolJobs jobs;
    Report report;
    ToolJobSpec spec;
    spec.tool = "fake";
    spec.program = tool;
    jobs.start(spec, into(report));
    REQUIRE(wait_delivered(report));
    CHECK(report.job.state == ToolJobState::Failed);
    CHECK(report.job.error == "Exited with code 7");
}

TEST_CASE("A program that is not there reports Failed, still from the event loop",
          "[tools][jobs]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir tmp;
    ToolJobs jobs;
    Report report;
    ToolJobSpec spec;
    spec.tool = "fake";
    spec.program = tmp.path / "missing";
    jobs.start(spec, into(report));
    CHECK_FALSE(report.delivered);
    REQUIRE(wait_delivered(report));
    CHECK(report.job.state == ToolJobState::Failed);
    CHECK(report.job.error.find("Could not start") != std::string::npos);
    CHECK(report.job.error.find("missing") != std::string::npos);
    CHECK_FALSE(jobs.busy());
}

TEST_CASE("Cancelling a running job stops it and reports Cancelled", "[tools][jobs]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir tmp;
    // Ignores a polite request; terminate ends it.
    const auto tool = write_script(tmp.path / "fake", "echo progress=0.1\nsleep 30\n");
    ToolJobs jobs;
    Report report;
    ToolJobSpec spec;
    spec.tool = "fake";
    spec.program = tool;
    spec.progress = parse_progress;
    spec.stop_input = "q";
    const qint64 id = jobs.start(spec, into(report));
    REQUIRE(QTest::qWaitFor([&jobs] { return jobs.jobs()[0].progress > 0.0; }, seventhslide::test::k_tool_wait_ms));
    jobs.cancel(id);
    REQUIRE(wait_delivered(report, ToolJobs::k_grace_ms + seventhslide::test::k_tool_wait_ms));
    CHECK(report.job.state == ToolJobState::Cancelled);
    CHECK_FALSE(jobs.busy());
}

TEST_CASE("A job that stops cleanly when asked still reports Cancelled", "[tools][jobs]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir tmp;
    // Like ffmpeg: a 'q' on stdin, and it finishes up and exits 0.
    const auto tool = write_script(tmp.path / "fake",
                                   "echo progress=0.1\nread line\nexit 0\n");
    ToolJobs jobs;
    Report report;
    ToolJobSpec spec;
    spec.tool = "fake";
    spec.program = tool;
    spec.progress = parse_progress;
    spec.stop_input = "q\n";
    const qint64 id = jobs.start(spec, into(report));
    REQUIRE(QTest::qWaitFor([&jobs] { return jobs.jobs()[0].progress > 0.0; }, seventhslide::test::k_tool_wait_ms));
    jobs.cancel(id);
    REQUIRE(wait_delivered(report));
    CHECK(report.job.state == ToolJobState::Cancelled);
}

TEST_CASE("Jobs of one tool run one at a time; another tool's runs beside them",
          "[tools][jobs]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir tmp;
    // Each waits until a file named after its argument appears.
    const auto tool = write_script(tmp.path / "fake",
                                   "while [ ! -e \"$1\" ]; do sleep 0.05; done\n");
    ToolJobs jobs;
    Report first;
    Report second;
    Report other;
    ToolJobSpec spec;
    spec.program = tool;
    spec.tool = "a";
    spec.args = {(tmp.path / "go1").string()};
    const qint64 id1 = jobs.start(spec, into(first));
    spec.args = {(tmp.path / "go2").string()};
    const qint64 id2 = jobs.start(spec, into(second));
    spec.tool = "b";
    spec.args = {(tmp.path / "go3").string()};
    jobs.start(spec, into(other));

    auto states = jobs.jobs();
    REQUIRE(states.size() == 3);
    CHECK(states[0].id == id1);
    CHECK(states[0].state == ToolJobState::Running);
    CHECK(states[1].id == id2);
    CHECK(states[1].state == ToolJobState::Queued);
    CHECK(states[2].state == ToolJobState::Running);

    std::ofstream{tmp.path / "go1"} << "";
    REQUIRE(wait_delivered(first));
    CHECK(first.job.state == ToolJobState::Done);
    CHECK_FALSE(second.delivered);
    REQUIRE(QTest::qWaitFor(
        [&jobs, id2] {
            const auto running = jobs.running("a");
            return running.has_value() && running->id == id2;
        },
        10000));

    std::ofstream{tmp.path / "go2"} << "";
    std::ofstream{tmp.path / "go3"} << "";
    REQUIRE(wait_delivered(second));
    REQUIRE(wait_delivered(other));
    CHECK(second.job.state == ToolJobState::Done);
    CHECK(other.job.state == ToolJobState::Done);
    CHECK_FALSE(jobs.busy());
}

TEST_CASE("cancel_all takes the queued jobs too, and each reports Cancelled", "[tools][jobs]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir tmp;
    const auto tool = write_script(tmp.path / "fake", "sleep 30\n");
    ToolJobs jobs;
    Report first;
    Report second;
    ToolJobSpec spec;
    spec.tool = "a";
    spec.program = tool;
    jobs.start(spec, into(first));
    jobs.start(spec, into(second));
    REQUIRE(jobs.jobs().size() == 2);
    jobs.cancel_all();
    REQUIRE(wait_delivered(second));
    CHECK(second.job.state == ToolJobState::Cancelled);
    REQUIRE(wait_delivered(first, ToolJobs::k_grace_ms + seventhslide::test::k_tool_wait_ms));
    CHECK(first.job.state == ToolJobState::Cancelled);
    CHECK_FALSE(jobs.busy());
}

TEST_CASE("Destroying the runner kills what is running and reports nothing", "[tools][jobs]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir tmp;
    const auto tool = write_script(tmp.path / "fake", "sleep 30\n");
    Report report;
    {
        ToolJobs jobs;
        ToolJobSpec spec;
        spec.tool = "a";
        spec.program = tool;
        jobs.start(spec, into(report));
        REQUIRE(QTest::qWaitFor([&jobs] { return jobs.running("a").has_value(); }, seventhslide::test::k_tool_wait_ms));
    }
    QTest::qWait(50);
    CHECK_FALSE(report.delivered);
}

TEST_CASE("The tools controller finds the tools and reads their versions", "[tools]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir tmp;
    const auto ffmpeg_path =
        write_script(tmp.path / "bin" / "ffmpeg",
                     "echo 'ffmpeg version 9.9-test Copyright (c) the FFmpeg developers'\n"
                     "echo 'built with gcc'\n");
    write_script(tmp.path / "bin" / "yt-dlp", "echo 2025.01.15\n");
    ToolSearch search;
    search.platform_id = seventhslide::test::tool_platform();
    search.path_env = (tmp.path / "bin").string();
    search.tools_dir = tmp.path / "tools";

    ToolsController tools{search};
    CHECK_FALSE(tools.refreshed());
    CHECK(tools.tools().isEmpty());

    tools.refresh();
    CHECK(tools.refreshed());
    REQUIRE(tools.found().size() == 3);
    CHECK(tools.found()[0].name == "ffmpeg");
    CHECK(tools.found()[1].name == "ffprobe");
    CHECK(tools.found()[2].name == "yt-dlp");
    REQUIRE(tools.location("ffmpeg").has_value());
    CHECK(tools.location("ffmpeg")->path == ffmpeg_path);
    CHECK(tools.location("ffmpeg")->source == ToolSource::System);
    CHECK_FALSE(tools.location("ffprobe").has_value());
    CHECK(tools.probing());

    REQUIRE(QTest::qWaitFor([&tools] { return !tools.probing(); }, seventhslide::test::k_tool_wait_ms));
    CHECK(tools.found()[0].version == "9.9-test");
    CHECK(tools.found()[0].error.empty());
    CHECK(tools.found()[2].version == "2025.01.15");
    CHECK(tools.found()[1].version.empty());

    const auto list = tools.tools();
    REQUIRE(list.size() == 3);
    const auto ffmpeg = list[0].toMap();
    CHECK(ffmpeg.value("name").toString() == "ffmpeg");
    CHECK(ffmpeg.value("found").toBool());
    CHECK(ffmpeg.value("version").toString() == "9.9-test");
    CHECK(ffmpeg.value("source").toString() == "system");
    CHECK(ffmpeg.value("path").toString() == QString::fromStdString(ffmpeg_path.string()));
    CHECK_FALSE(ffmpeg.value("probing").toBool());
    CHECK_FALSE(list[1].toMap().value("found").toBool());
    CHECK(tools.toolsDir() == QString::fromStdString((tmp.path / "tools").string()));
}

TEST_CASE("A tool that is there but will not run is found, with its error", "[tools]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir tmp;
    write_script(tmp.path / "bin" / "ffprobe", "echo 'wrong architecture' >&2\nexit 126\n");
    ToolSearch search;
    search.platform_id = seventhslide::test::tool_platform();
    search.path_env = (tmp.path / "bin").string();

    ToolsController tools{search};
    tools.refresh();
    REQUIRE(QTest::qWaitFor([&tools] { return !tools.probing(); }, seventhslide::test::k_tool_wait_ms));
    REQUIRE(tools.location("ffprobe").has_value());
    CHECK(tools.found()[1].version.empty());
    CHECK(tools.found()[1].error == "wrong architecture");

    // Refreshing after the tool is fixed reads it again.
    write_script(tmp.path / "bin" / "ffprobe", "echo 'ffprobe version 7.0 Copyright'\n");
    tools.refresh();
    REQUIRE(QTest::qWaitFor([&tools] { return !tools.probing(); }, seventhslide::test::k_tool_wait_ms));
    CHECK(tools.found()[1].version == "7.0");
    CHECK(tools.found()[1].error.empty());
}
