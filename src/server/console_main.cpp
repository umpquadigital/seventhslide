// seventhslide-server.exe: `seventhslide --server` as a Windows console
// program. seventhslide.exe is a window program, and a command prompt
// neither waits for one nor gives it its keyboard: the prompt came back at
// once, %ERRORLEVEL% was not the server's, and the settings screen and the
// shell took turns with the keys. This one is the console's like any other
// command; it is the same server (run.cpp), and takes the same arguments
// with the --server left off. Built on Windows only: elsewhere the app is
// a console program already.

#include "seventhslide/server/run.hpp"

#include <vector>

int main(int argc, char* argv[])
{
    static char server_flag[] = "--server";
    std::vector<char*> args;
    args.reserve(static_cast<std::size_t>(argc) + 2);
    args.push_back(argv[0]);
    args.push_back(server_flag);
    for (int i = 1; i < argc; ++i) {
        args.push_back(argv[i]);
    }
    args.push_back(nullptr);
    return seventhslide::server::run(static_cast<int>(args.size()) - 1, args.data());
}
