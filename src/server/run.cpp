#include "seventhslide/server/run.hpp"

#include "commands.hpp"
#include "seventhslide/server/command_line.hpp"
#include "seventhslide/server/config_screen.hpp"
#include "seventhslide/server/data_folder.hpp"
#include "seventhslide/server/server.hpp"
#include "seventhslide/version.hpp"
#include "terminal.hpp"

#include <QCoreApplication>
#include <QLoggingCategory>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef Q_OS_WIN
#include <QAbstractNativeEventFilter>

#include <windows.h>
#else
#include <QSocketNotifier>

#include <csignal>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace seventhslide::server {
namespace {

#ifdef Q_OS_WIN

BOOL WINAPI on_console_event(DWORD)
{
    QMetaObject::invokeMethod(QCoreApplication::instance(), &QCoreApplication::quit,
                              Qt::QueuedConnection);
    return TRUE;
}

/// A polite stop with no console: QProcess::terminate(), and what else
/// stops a program that has no window, posts WM_CLOSE to its main thread.
/// The same as SIGTERM elsewhere.
class QuitOnClose final : public QAbstractNativeEventFilter
{
public:
    bool nativeEventFilter(const QByteArray& type, void* message, qintptr* /*result*/) override
    {
        if (type == "windows_generic_MSG" && static_cast<const MSG*>(message)->message == WM_CLOSE) {
            QCoreApplication::quit();
            return true;
        }
        return false;
    }
};

void quit_on_signals()
{
    SetConsoleCtrlHandler(on_console_event, TRUE);
    static QuitOnClose quit_on_close;
    QCoreApplication::instance()->installNativeEventFilter(&quit_on_close);
}

#else

/// A signal handler may only write; the event loop reads the other end.
int g_signal_pipe[2] = {-1, -1};

void on_signal(int)
{
    const char one = 1;
    if (::write(g_signal_pipe[0], &one, 1) < 0) {
        // nothing more a signal handler can do
    }
}

void quit_on_signals()
{
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, g_signal_pipe) != 0) {
        return;
    }
    auto* notifier = new QSocketNotifier(g_signal_pipe[1], QSocketNotifier::Read,
                                         QCoreApplication::instance());
    QObject::connect(notifier, &QSocketNotifier::activated, notifier, [] {
        char got = 0;
        if (::read(g_signal_pipe[1], &got, 1) >= 0) {
            QCoreApplication::quit();
        }
    });
    struct sigaction action {};
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    for (const int signal : {SIGTERM, SIGINT, SIGHUP}) {
        // One ignored already stays so: started with nohup, the server
        // outlives the terminal that started it.
        struct sigaction before {};
        if (sigaction(signal, nullptr, &before) == 0 && before.sa_handler == SIG_IGN) {
            continue;
        }
        sigaction(signal, &action, nullptr);
    }
}

#endif

/// ``args`` as typed again, with the data folder said, for a person to run as is.
std::string command_again(const std::vector<std::string>& args, const std::filesystem::path& data)
{
    std::string out;
    for (const auto& arg : args) {
        const bool plain = !arg.empty() && arg.find_first_of(" \t\"'\\$") == std::string::npos;
        out += (out.empty() ? "" : " ") + (plain ? arg : "\"" + arg + "\"");
    }
    if (std::ranges::find(args, "--data") == args.end()) {
        out += " --data \"" + data.string() + "\"";
    }
    return out;
}

int serve(const CommandLine& cl)
{
    // A line a log keeps on its own: when, and what.
    qSetMessagePattern(QStringLiteral(
        "%{time yyyy-MM-dd hh:mm:ss} %{if-warning}warning: %{endif}%{if-critical}error: %{endif}%{message}"));
    ServerOptions options;
    options.data = cl.data;
    options.port = cl.port;
    options.addresses = cl.addresses;
    options.name = cl.name;
    options.discovery = !cl.no_discovery;
    std::unique_ptr<Server> server;
    try {
        server = std::make_unique<Server>(std::move(options));
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
    quit_on_signals();
    const int status = QCoreApplication::exec();
    server.reset();
    std::cerr << "The server stopped.\n";
    return status;
}

}  // namespace

int run(int argc, char** argv)
{
#if defined(__GLIBC__)
    // A thread for each library held: the C library would keep memory for
    // each (30 libraries: 900 MB at the peak, rather than 200). A few pools,
    // shared, before any thread starts; MALLOC_ARENA_MAX still decides.
    if (std::getenv("MALLOC_ARENA_MAX") == nullptr) {
        (void)mallopt(M_ARENA_MAX, 4);
    }
#endif
    use_parent_console();
    // Read before Qt sees them: Qt takes some arguments for itself.
    const std::vector<std::string> args(argv + 2, argv + argc);
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("SeventhSlide"));
    QCoreApplication::setOrganizationName(QStringLiteral("SeventhSlide"));
    QCoreApplication::setApplicationVersion(QString::fromUtf8(seventhslide::version_string()));

    CommandLine cl;
    try {
        cl = parse_command_line(args);
    } catch (const std::invalid_argument& ex) {
        std::cerr << ex.what() << "\n\n" << usage();
        return 2;
    } catch (const std::runtime_error& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
    // Not for running it: a container's root may well run it on a folder it was
    // handed.
    if (cl.command != Command::Help && cl.command != Command::Run) {
        const bool writes = cl.command == Command::Config || cl.command == Command::Settings;
        if (const auto why = user_refused(cl.data, writes, command_again(args, cl.data))) {
            std::cerr << *why << '\n';
            return 1;
        }
    }
    switch (cl.command) {
    case Command::Help:
        std::cout << usage();
        return 0;
    case Command::Run:
        return serve(cl);
    case Command::Config:
        if (!has_terminal()) {
            std::cerr << "The settings screen needs a terminal. For a script, every part of it is a "
                         "command: see seventhslide --server help.\n";
            return 2;
        }
        return run_config_screen(cl.data);
    case Command::Join:
    case Command::Status:
    case Command::Sync:
    case Command::Admin:
    case Command::Address:
    case Command::Leave:
    case Command::Settings:
        break;
    }
    return run_command(cl);
}

}  // namespace seventhslide::server
