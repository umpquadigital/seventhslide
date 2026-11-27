#pragma once

// A stand-in for a program the app runs (ffmpeg, ffprobe, yt-dlp): a POSIX
// sh script. Elsewhere the script is the program. Windows runs no script as
// a program, so there the program is fake_tool.exe (fake_tool.cpp) under
// the tool's name, which runs the script beside it with Git for Windows' sh;
// without that sh, a test that needs one SKIPs (fake_tools_runnable).

#include <seventhslide/platform/paths.hpp>
#include <seventhslide/platform/tools.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <QByteArray>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#endif

namespace seventhslide::test {

/// The platform the tools are run on: this one, so a search finds the
/// stand-ins under the names this system's programs have.
[[nodiscard]] inline std::string tool_platform()
{
    return platform::PathEnvironment::system().platform_id;
}

/// ``dirs`` as this system's PATH: ';' between them on Windows, ':' elsewhere.
[[nodiscard]] inline std::string tool_path_env(std::initializer_list<std::filesystem::path> dirs)
{
    const char separator = tool_platform() == "windows" ? ';' : ':';
    std::string out;
    for (const auto& dir : dirs) {
        if (!out.empty()) {
            out += separator;
        }
        out += dir.string();
    }
    return out;
}

/// How long a test waits on a job a stand-in runs before it fails as hung.
/// Not a measure: a stand-in ends in well under a second as a rule, but on
/// Windows one started from the tests can sit about five seconds in the
/// system's loader before it runs at all (seen with Defender's real-time
/// scanning on), more on a busy machine.
inline constexpr int k_tool_wait_ms = 30000;

/// A stand-in can be run here: always, but on Windows only with Git's sh,
/// which is then handed to fake_tool.exe (SEVENTHSLIDE_TEST_SH).
[[nodiscard]] inline bool fake_tools_runnable()
{
#if defined(_WIN32)
    static const QString sh = [] {
        QString found = QStandardPaths::findExecutable(QStringLiteral("sh"));
        const QStringList git_roots{qEnvironmentVariable("ProgramFiles") + QStringLiteral("/Git"),
                                    qEnvironmentVariable("ProgramW6432") + QStringLiteral("/Git")};
        for (const auto& root : git_roots) {
            for (const char* sub : {"/usr/bin/sh.exe", "/bin/sh.exe"}) {
                if (found.isEmpty() && QFileInfo::exists(root + QLatin1String(sub))) {
                    found = root + QLatin1String(sub);
                }
            }
        }
        if (!found.isEmpty()) {
            qputenv("SEVENTHSLIDE_TEST_SH", found.toUtf8());
        }
        return found;
    }();
    return !sh.isEmpty();
#else
    return true;
#endif
}

/// A stand-in for ``name`` in ``dir`` that runs ``body`` (sh, after its
/// #!/bin/sh line): its path, as this system names the program.
inline std::filesystem::path write_fake_tool(const std::filesystem::path& dir,
                                             std::string_view name, std::string_view body)
{
    namespace fs = std::filesystem;
    fs::create_directories(dir);
    const fs::path program = dir / platform::tool_file_name(name, tool_platform());
#if defined(_WIN32)
    (void)fake_tools_runnable();
    {
        // Binary: sh reads a CR at a line's end as part of the line.
        std::ofstream out{dir / (std::string(name) + ".sh"), std::ios::binary};
        out << "#!/bin/sh\n" << body << "\n";
    }
    fs::copy_file(SEVENTHSLIDE_FAKE_TOOL, program, fs::copy_options::overwrite_existing);
#else
    {
        std::ofstream out{program};
        out << "#!/bin/sh\n" << body << "\n";
    }
    fs::permissions(program, fs::perms::owner_all, fs::perm_options::replace);
#endif
    return program;
}

}  // namespace seventhslide::test
