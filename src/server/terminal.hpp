#pragma once

// Asking the person at the server's terminal: a password without showing
// it, a line, a yes. Where stdin is not a terminal (a script), lines are
// read as they come. Not part of the module's API.
//
// Seam: the platform's terminal calls.

#include <string>
#include <string_view>

namespace seventhslide::server {

/// Throws ``std::runtime_error`` when nothing more can be read.
[[nodiscard]] std::string read_secret(std::string_view prompt);
[[nodiscard]] std::string read_line(std::string_view prompt);
/// Whether the answer to ``prompt`` is "yes".
[[nodiscard]] bool confirm(std::string_view prompt);

/// Whether a person can be at this program's input and output: not a pipe, a
/// service or a container with no terminal.
[[nodiscard]] bool has_terminal();

/// On Windows, where the program is a windowed one (seventhslide.exe; not
/// seventhslide-server.exe), print to the console it was started from.
/// Then, as for any console there, read and write it as UTF-8, which is
/// what the program's text is, until the program ends. Nothing elsewhere.
void use_parent_console();

}  // namespace seventhslide::server
