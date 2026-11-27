#pragma once

// ``seventhslide --server ...``: where ``main`` goes, before anything else is
// set up, when asked for the server (``asks_for_server``). It makes a
// ``QCoreApplication`` (never a ``QGuiApplication``: no window system is
// needed or touched) and runs the server, or one of its commands.
//
// Seam: Qt Core.

namespace seventhslide::server {

/// The exit status: 0 for done, 1 for not done (why is printed), 2 for a
/// command line that is not one.
[[nodiscard]] int run(int argc, char** argv);

}  // namespace seventhslide::server
