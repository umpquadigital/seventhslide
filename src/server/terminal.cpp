#include "terminal.hpp"

#include <QtGlobal>

#include <cstdio>
#include <iostream>
#include <stdexcept>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace seventhslide::server {
namespace {

std::string next_line()
{
    std::string line;
    if (!std::getline(std::cin, line) && line.empty()) {
        throw std::runtime_error("Nothing was typed.");
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return line;
}

}  // namespace

std::string read_secret(std::string_view prompt)
{
    std::cerr << prompt << std::flush;
#ifdef Q_OS_WIN
    const HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    const bool console = GetConsoleMode(in, &mode) != 0;
    if (console) {
        SetConsoleMode(in, mode & ~static_cast<DWORD>(ENABLE_ECHO_INPUT));
    }
    std::string line;
    try {
        line = next_line();
    } catch (...) {
        if (console) {
            SetConsoleMode(in, mode);
        }
        throw;
    }
    if (console) {
        SetConsoleMode(in, mode);
        std::cerr << '\n';
    }
    return line;
#else
    termios old{};
    const bool tty = isatty(STDIN_FILENO) != 0 && tcgetattr(STDIN_FILENO, &old) == 0;
    if (tty) {
        termios quiet = old;
        quiet.c_lflag &= ~static_cast<tcflag_t>(ECHO);
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &quiet);
    }
    std::string line;
    try {
        line = next_line();
    } catch (...) {
        if (tty) {
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &old);
        }
        throw;
    }
    if (tty) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &old);
        std::cerr << '\n';
    }
    return line;
#endif
}

std::string read_line(std::string_view prompt)
{
    std::cerr << prompt << std::flush;
    return next_line();
}

bool confirm(std::string_view prompt)
{
    try {
        return read_line(prompt) == "yes";
    } catch (const std::runtime_error&) {
        return false;
    }
}

bool has_terminal()
{
#ifdef Q_OS_WIN
    DWORD mode = 0;
    return GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) != 0
           && GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mode) != 0;
#else
    return isatty(STDIN_FILENO) != 0 && isatty(STDOUT_FILENO) != 0;
#endif
}

#ifdef Q_OS_WIN
namespace {

/// The console's code pages as UTF-8 while the program runs, and as they
/// were again after: the console is the command prompt's, and outlives it.
void use_utf8_console()
{
    struct Restore {
        UINT input = GetConsoleCP();
        UINT output = GetConsoleOutputCP();
        ~Restore()
        {
            if (input != 0) {
                SetConsoleCP(input);
            }
            if (output != 0) {
                SetConsoleOutputCP(output);
            }
        }
    };
    static Restore restore;
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
}

}  // namespace
#endif

void use_parent_console()
{
#ifdef Q_OS_WIN
    // Output already going somewhere (a file, a pipe, the console of
    // seventhslide-server.exe) stays there.
    if (GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) == FILE_TYPE_UNKNOWN
        && AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* stream = nullptr;
        freopen_s(&stream, "CONOUT$", "w", stdout);
        freopen_s(&stream, "CONOUT$", "w", stderr);
        freopen_s(&stream, "CONIN$", "r", stdin);
        std::ios::sync_with_stdio();
    }
    // No console (a service, Task Scheduler): GetConsoleCP says 0, and
    // nothing is changed.
    if (GetConsoleCP() != 0) {
        use_utf8_console();
    }
#endif
}

}  // namespace seventhslide::server
