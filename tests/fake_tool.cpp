// fake_tool.exe, Windows only (fake_tool.hpp): copied in as a program the
// app runs (ffmpeg.exe), it runs the sh script beside it (ffmpeg.sh) with
// the sh SEVENTHSLIDE_TEST_SH names, given its own arguments, input and
// output, and ends with the script's exit code. The script and what it
// starts go with it when it is ended (a job object), as a test's stop or
// cancel ends the program.

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

int wmain()
{
    std::vector<wchar_t> self(32768);
    const DWORD length = GetModuleFileNameW(nullptr, self.data(), DWORD(self.size()));
    std::wstring script(self.data(), length);
    const auto dot = script.find_last_of(L'.');
    script = (dot == std::wstring::npos ? script : script.substr(0, dot)) + L".sh";

    const wchar_t* sh = _wgetenv(L"SEVENTHSLIDE_TEST_SH");
    if (sh == nullptr || *sh == L'\0') {
        std::fputs("fake_tool: SEVENTHSLIDE_TEST_SH is not set\n", stderr);
        return 127;
    }

    // Its arguments as they came: the command line less its first word.
    const std::wstring line = GetCommandLineW();
    std::size_t i = 0;
    if (!line.empty() && line[0] == L'"') {
        const auto close = line.find(L'"', 1);
        i = close == std::wstring::npos ? line.size() : close + 1;
    } else {
        while (i < line.size() && line[i] != L' ' && line[i] != L'\t') {
            ++i;
        }
    }
    std::wstring command = L"\"" + std::wstring(sh) + L"\" \"" + script + L"\"" + line.substr(i);
    std::vector<wchar_t> buffer(command.begin(), command.end());
    buffer.push_back(L'\0');

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits);

    STARTUPINFOW start{};
    start.cb = sizeof start;
    start.dwFlags = STARTF_USESTDHANDLES;
    start.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    start.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    start.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED, nullptr,
                        nullptr, &start, &child)) {
        std::fputs("fake_tool: cannot start sh\n", stderr);
        return 127;
    }
    AssignProcessToJobObject(job, child.hProcess);
    ResumeThread(child.hThread);
    WaitForSingleObject(child.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(child.hProcess, &code);
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    return int(code);
}
