#pragma once

// macOS: the app answers the system's request to quit (log out, restart,
// shut down, the Dock's Quit, a script's `quit`) only once it has quit
// (mac_quit.mm).
//
// AppKit hands that request to the app delegate, and Qt's always answers
// "cancelled", even when it is about to quit, so that it can return from
// exec() and leave through main(). A restart or log out reads that as the
// app refusing, and stops ("SeventhSlide canceled restart"). Here the
// request is held (a suspended Apple event) while the app quits as for
// Command+Q, and answered as main() ends.

namespace seventhslide::app {

/// Take over the quit Apple event. Call once the event loop runs (AppKit has
/// installed its own handler by then, which this one replaces).
void handle_mac_quit_events();

/// Answers a quit request held while the app shut down, as it goes out of
/// scope. Declare it first in main(), so it goes last.
class MacQuitAnswer {
public:
    MacQuitAnswer() = default;
    ~MacQuitAnswer();
    MacQuitAnswer(const MacQuitAnswer&) = delete;
    MacQuitAnswer& operator=(const MacQuitAnswer&) = delete;
};

}  // namespace seventhslide::app
