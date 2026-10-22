#include "../SimpleGame/stdafx.h"
#include "../SimpleGame/Profiler.cpp"
#include <fstream>
#include <thread>
#include <stdexcept>

void Check(bool ok, const char *message)
{
    if (!ok)
    {
        throw std::runtime_error(message);
    }
}
std::wstring Read(HANDLE output, COORD start, DWORD count)
{
    std::wstring text(count, L' ');
    DWORD read = 0;
    Check(ReadConsoleOutputCharacterW(output, text.data(), count, start, &read) != FALSE, "read cells");
    text.resize(read);
    return text;
}
int Child()
{
    std::ofstream result("x64/RevisionChecks/log-console-result.txt", std::ios::binary);
    try
    {
        FILE *consoleStream = nullptr;
        Check(freopen_s(&consoleStream, "CONOUT$", "w", stdout) == 0, "console output stream");
        HANDLE consoleOutput =
            CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                        OPEN_EXISTING, 0, nullptr);
        Check(consoleOutput != INVALID_HANDLE_VALUE, "console output handle");
        SetStdHandle(STD_OUTPUT_HANDLE, consoleOutput);
        HANDLE consoleInput =
            CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                        OPEN_EXISTING, 0, nullptr);
        Check(consoleInput != INVALID_HANDLE_VALUE, "console input handle");
        SetStdHandle(STD_INPUT_HANDLE, consoleInput);
        profiling::InitializeConsoleOutput();
        Check(GetConsoleOutputCP() == CP_UTF8, "UTF-8 output page");
        Check(GetConsoleCP() == CP_UTF8, "UTF-8 input page");
        char encoded[128] = {};
        std::snprintf(encoded, sizeof(encoded), "%ls", L"한글 경로");
        Check(std::string(encoded) == "한글 경로", "UTF-8 wide path formatting");
        HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
        std::printf("이전 진단 유지\n");
        std::fflush(stdout);
        CONSOLE_SCREEN_BUFFER_INFO initial = {};
        Check(GetConsoleScreenBufferInfo(output, &initial) != FALSE, "initial console");
        const auto history = Read(output, {0, 0}, initial.dwSize.X);
        Check(history.find(L'이') != std::wstring::npos && history.find(L'전') != std::wstring::npos,
              "printf Korean glyphs");
        profiling::ConsoleReport report;
        std::string old = "성능 요약 OLD\n";
        std::string fresh = "성능 요약 NEW\n";
        for (int i = 0; i < 140; ++i)
        {
            old += "cpu_scope_" + std::to_string(i) + ": 123.456\n";
            fresh += "cpu_scope_" + std::to_string(i) + ": 456.789\n";
        }
        Check(report.Write(old), "first fixed report");
        CONSOLE_SCREEN_BUFFER_INFO first = {};
        GetConsoleScreenBufferInfo(output, &first);
        Check(first.srWindow.Top == initial.srWindow.Top && first.srWindow.Bottom == initial.srWindow.Bottom,
              "first report moved viewport");
        Check(first.dwCursorPosition.Y > initial.dwCursorPosition.Y, "separate diagnostics cursor");
        Check(Read(output, {0, 0}, initial.dwSize.X) == history, "startup history overwritten");
        Check(report.Write(fresh), "updated fixed report");
        CONSOLE_SCREEN_BUFFER_INFO second = {};
        GetConsoleScreenBufferInfo(output, &second);
        Check(second.dwCursorPosition.Y == first.dwCursorPosition.Y &&
                  second.dwCursorPosition.X == first.dwCursorPosition.X,
              "update moved cursor");
        Check(second.srWindow.Top == first.srWindow.Top && second.srWindow.Bottom == first.srWindow.Bottom,
              "update scrolled viewport");
        Check(Read(output, {0, short(initial.dwCursorPosition.Y + 1)}, initial.dwSize.X).find(L"456.789") !=
                  std::wstring::npos,
              "report value not replaced");
        std::printf("새로운 진단 보존\n");
        std::fflush(stdout);
        const auto diagnostic = Read(output, first.dwCursorPosition, first.dwSize.X);
        CONSOLE_SCREEN_BUFFER_INFO diagnosticView = {};
        GetConsoleScreenBufferInfo(output, &diagnosticView);
        Check(report.Write(fresh), "update after diagnostic");
        CONSOLE_SCREEN_BUFFER_INFO last = {};
        GetConsoleScreenBufferInfo(output, &last);
        Check(Read(output, first.dwCursorPosition, first.dwSize.X) == diagnostic,
              "later diagnostic overwritten");
        Check(last.dwCursorPosition.Y == diagnosticView.dwCursorPosition.Y, "later cursor moved");
        Check(last.srWindow.Top == diagnosticView.srWindow.Top, "later diagnostic view moved");
        INPUT_RECORD key = {};
        key.EventType = KEY_EVENT;
        key.Event.KeyEvent.bKeyDown = TRUE;
        key.Event.KeyEvent.wRepeatCount = 1;
        key.Event.KeyEvent.wVirtualKeyCode = VK_END;
        DWORD queued = 0;
        Check(WriteConsoleInputW(consoleInput, &key, 1, &queued) != FALSE, "page key");
        Check(report.Write(fresh), "report final page");
        const auto pageText = Read(output, {0, initial.dwCursorPosition.Y}, initial.dwSize.X);
        Check(pageText.find(L"cpu_scope_1") != std::wstring::npos, "final metric page accessible");
        GetConsoleScreenBufferInfo(output, &last);
        Check(last.srWindow.Top == diagnosticView.srWindow.Top, "page navigation scrolled viewport");
        Check(report.Write("짧은 보고\n"), "short report");
        Check(Read(output, {0, short(initial.dwCursorPosition.Y + 1)}, initial.dwSize.X).find(L"cpu_scope") ==
                  std::wstring::npos,
              "stale row not cleared");
        HANDLE redirected =
            CreateFileW(L"x64/RevisionChecks/log-console-redirect.txt", GENERIC_WRITE, FILE_SHARE_READ,
                        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        Check(redirected != INVALID_HANDLE_VALUE, "redirected output file");
        SetStdHandle(STD_OUTPUT_HANDLE, redirected);
        Check(!report.Write(fresh), "redirected output must retain stream fallback");
        SetStdHandle(STD_OUTPUT_HANDLE, output);
        CloseHandle(redirected);
        result << "PASS: UTF-8 Korean/wide paths, retained history, fixed cursor/viewport, external "
                  "diagnostics, manual scroll, stale rows, redirected fallback\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        result << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
int main(int argc, char **argv)
{
    if (argc > 1)
    {
        return Child();
    }
    HANDLE inRead = nullptr, inWrite = nullptr, outRead = nullptr, outWrite = nullptr;
    Check(CreatePipe(&inRead, &inWrite, nullptr, 0) != FALSE, "input pipe");
    Check(CreatePipe(&outRead, &outWrite, nullptr, 0) != FALSE, "output pipe");
    HPCON console = nullptr;
    Check(SUCCEEDED(CreatePseudoConsole({110, 40}, inRead, outWrite, 0, &console)), "pseudoconsole");
    CloseHandle(inRead);
    CloseHandle(outWrite);
    std::thread drain([&]() {
        char data[4096];
        DWORD count;
        while (ReadFile(outRead, data, sizeof(data), &count, nullptr) && count)
        {
        }
    });
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<unsigned char> storage(bytes);
    STARTUPINFOEXW start = {};
    start.StartupInfo.cb = sizeof(start);
    start.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    Check(InitializeProcThreadAttributeList(start.lpAttributeList, 1, 0, &bytes) != FALSE, "attribute list");
    Check(UpdateProcThreadAttribute(start.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, console,
                                    sizeof(console), nullptr, nullptr) != FALSE,
          "console attribute");
    wchar_t self[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring command = L"\"" + std::wstring(self) + L"\" --child";
    PROCESS_INFORMATION process = {};
    Check(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, EXTENDED_STARTUPINFO_PRESENT,
                         nullptr, nullptr, &start.StartupInfo, &process) != FALSE,
          "child process");
    Check(WaitForSingleObject(process.hProcess, 15000) == WAIT_OBJECT_0, "child timeout");
    DWORD exit = 1;
    GetExitCodeProcess(process.hProcess, &exit);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    DeleteProcThreadAttributeList(start.lpAttributeList);
    ClosePseudoConsole(console);
    CloseHandle(inWrite);
    drain.join();
    CloseHandle(outRead);
    return int(exit);
}
