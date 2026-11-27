#pragma once

// A fresh directory for one test, removed when it goes out of scope.
//
// ctest runs every test case as its own process, several at once, so the name
// must be unique across processes; QTemporaryDir guarantees that.
//
// Its path is the folder's real one, links and short names resolved (macOS's
// /var is a link to /private/var, and Windows' temp folder may be named
// C:\Users\RUNNER~1), as the app resolves the paths it keeps: a test
// comparing the two compares the same spelling.

#include <QDir>
#include <QTemporaryDir>

#include <filesystem>

namespace seventhslide::test {

class TempDir {
    QTemporaryDir dir_{QDir::tempPath() + QStringLiteral("/seventhslide-test-XXXXXX")};

public:
    const std::filesystem::path path{std::filesystem::canonical(QDir(dir_.path()).filesystemPath())};
};

}  // namespace seventhslide::test
