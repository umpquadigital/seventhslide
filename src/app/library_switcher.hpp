#pragma once

// Closing the open library and opening another: what only the application's
// composition root can do, because it tears down and rebuilds everything that
// belongs to one library (the session's content, the repositories, the media
// roots, the profiles).
//
// SessionFacade implements it for LibraryRegistryController, which needs to
// know whether an open worked so it can fall back to the previous library.
//
// Seam: Qt Core. Call on the GUI thread.

#include <seventhslide/library/library.hpp>

namespace seventhslide::app {

class ILibrarySwitcher {
public:
    ILibrarySwitcher() = default;
    virtual ~ILibrarySwitcher() = default;
    ILibrarySwitcher(const ILibrarySwitcher&) = delete;
    ILibrarySwitcher& operator=(const ILibrarySwitcher&) = delete;
    ILibrarySwitcher(ILibrarySwitcher&&) = delete;
    ILibrarySwitcher& operator=(ILibrarySwitcher&&) = delete;

    /// Let go of the open library, if any.
    virtual void close_library() = 0;
    /// Open ``entry``. False when it would not open; OpenLibrary::last_error
    /// says why.
    [[nodiscard]] virtual bool open_library(const library::LibraryEntry& entry) = 0;
};

}  // namespace seventhslide::app
