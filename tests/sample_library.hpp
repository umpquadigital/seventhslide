#pragma once

// A small library with something in every list, for the tests that want a
// realistic one rather than the least that works: the UI shots and the web
// remote's browser tests. Songs, two services (one in a folder), a Bible,
// images in and out of a folder, videos and audio (empty files: listed but
// not playable), Freeform items in and out of a folder.

#include "session_facade.hpp"

#include <QtTypes>

#include <cstdint>

namespace seventhslide::test {

/// Ids of what the sample library holds, for a test to act on.
struct SampleLibrary {
    std::int64_t first_song = -1;  // "Amazing Grace"; the next id is "Holy Holy Holy"
    qint64 service = -1;           // "Sunday Service", at the top level
    qint64 other_service = -1;     // "Good Friday", in the folder "Easter"
    std::int64_t bible = -1;       // "Sample Bible": Psalms 23:1-2, John 3:16-18
    std::int64_t image_folder = -1;  // "Backgrounds": ocean.png, forest.png
    qint64 freeform = -1;          // "Welcome": a text and an ellipse
    qint64 freeform_folder = -1;   // "Announcements", empty
};

/// Fill the library ``facade`` has open, its database and its media folder,
/// with the sample content. "Sunday Service" is the open service; nothing is
/// live.
SampleLibrary fill_sample_library(app::SessionFacade& facade);

}  // namespace seventhslide::test
