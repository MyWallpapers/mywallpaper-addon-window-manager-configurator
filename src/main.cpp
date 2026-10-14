#include "runtime.hpp"
#include <fcntl.h>
#include <gdiplus.h>
#include <io.h>
#include <iostream>
int wmain(int argc, wchar_t **argv) {
    if (argc == 5 && std::wstring(argv[1]) == L"--guardian") {
        try {
            return wm::Guardian(static_cast<DWORD>(std::stoul(argv[2])), std::stoull(argv[3]),
                                std::filesystem::path(argv[4]));
        } catch (...) {
            return 2;
        }
    }
    DWORD fixture_pid = 0;
    if (argc == 3 && std::wstring(argv[1]) == L"--fixture-target") {
        try {
            fixture_pid = static_cast<DWORD>(std::stoul(argv[2]));
        } catch (...) {
            return 2;
        }
        if (!fixture_pid)
            return 2;
    } else if (argc != 1)
        return 2;
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token{};
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok)
        return 1;
    int result = 1;
    try {
        wm::Runtime runtime(fixture_pid);
        result = runtime.Run();
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
    }
    Gdiplus::GdiplusShutdown(token);
    if (SUCCEEDED(com))
        CoUninitialize();
    return result;
}
