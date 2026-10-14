#include "model.hpp"
#include <dwmapi.h>
#include <fcntl.h>
#include <io.h>
#include <iostream>
#include <map>
#include <thread>
#include <windows.h>
static HWND control;
static std::map<int, HWND> windows;
static HANDLE job;
static void Create(int index) {
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &info);
    const auto title = L"Fixture " + std::to_wstring(index);
    HWND w = CreateWindowExW(WS_EX_NOACTIVATE, L"MWWindowManagerFixture", title.c_str(),
                             WS_OVERLAPPEDWINDOW, info.rcWork.left + 60 + index * 25,
                             info.rcWork.top + 60 + index * 25, 480, 300, nullptr, nullptr,
                             GetModuleHandleW(nullptr), nullptr);
    if (!w)
        throw std::runtime_error("Fixture creation failed");
    windows[index] = w;
    ShowWindow(w, SW_SHOWNOACTIVATE);
}
static wm::Json State(int index) {
    auto w = windows.at(index);
    RECT r{}, visible{};
    GetWindowRect(w, &r);
    if (FAILED(DwmGetWindowAttribute(w, DWMWA_EXTENDED_FRAME_BOUNDS, &visible, sizeof(visible))))
        visible = r;
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST), &info);
    return {
        {"index", index},
        {"window", reinterpret_cast<uintptr_t>(w)},
        {"visible", IsWindowVisible(w) != FALSE},
        {"minimized", IsIconic(w) != FALSE},
        {"topmost", (GetWindowLongPtrW(w, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0},
        {"style", GetWindowLongPtrW(w, GWL_STYLE)},
        {"exStyle", GetWindowLongPtrW(w, GWL_EXSTYLE)},
        {"rect", {r.left, r.top, r.right - r.left, r.bottom - r.top}},
        {"visibleRect",
         {visible.left, visible.top, visible.right - visible.left, visible.bottom - visible.top}},
        {"work",
         {info.rcWork.left, info.rcWork.top, info.rcWork.right - info.rcWork.left,
          info.rcWork.bottom - info.rcWork.top}},
        {"dpi", GetDpiForWindow(w)}};
}
static LRESULT CALLBACK Proc(HWND w, UINT m, WPARAM a, LPARAM b) {
    if (m == WM_APP + 1) {
        std::unique_ptr<wm::Json> p(reinterpret_cast<wm::Json *>(b));
        try {
            const auto action = p->value("action", std::string{});
            const auto index = p->value("index", 0);
            wm::Json result;
            if (action == "new") {
                Create(index);
                result = State(index);
            } else if (action == "state")
                result = State(index);
            else if (action == "hold") {
                Sleep(std::clamp(p->value("milliseconds", 450), 0, 1500));
                result = {{"held", true}};
            } else if (action == "activate") {
                const auto target = windows.at(index);
                SetWindowLongPtrW(target, GWL_EXSTYLE,
                                  GetWindowLongPtrW(target, GWL_EXSTYLE) & ~WS_EX_NOACTIVATE);
                ShowWindow(target, SW_RESTORE);
                result = {{"activated", SetForegroundWindow(target) != FALSE}};
            } else if (action == "overlay-click") {
                const POINT point{p->at("x").get<int>(), p->at("y").get<int>()};
                SetCursorPos(point.x, point.y);
                const auto target = WindowFromPoint(point);
                wchar_t name[128]{};
                GetClassNameW(target, name, 128);
                if (std::wstring(name) != L"MWWindowPlacementOverlay")
                    throw std::runtime_error("Palette does not receive mouse hits");
                const auto ex = GetWindowLongPtrW(target, GWL_EXSTYLE);
                if (!(ex & WS_EX_TOPMOST) || (ex & WS_EX_TRANSPARENT))
                    throw std::runtime_error("Palette z-order/click-through state is incorrect");
                INPUT input[2]{};
                input[0].type = input[1].type = INPUT_MOUSE;
                input[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
                input[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
                if (SendInput(2, input, sizeof(INPUT)) != 2)
                    throw std::runtime_error("Cannot send fixture palette click");
                result = {{"clicked", true}};
            }
            else if (action == "destroy") {
                DestroyWindow(windows.at(index));
                windows.erase(index);
                result = {{"destroyed", index}};
            } else if (action == "assign-job") {
                if (job)
                    CloseHandle(job);
                job = CreateJobObjectW(nullptr, nullptr);
                JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
                info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info,
                                                     sizeof(info)))
                    throw std::runtime_error("Cannot create fixture job");
                HANDLE child = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE,
                                           p->at("pid").get<DWORD>());
                const auto ok = child && AssignProcessToJobObject(job, child);
                if (child)
                    CloseHandle(child);
                if (!ok)
                    throw std::runtime_error("Cannot assign fixture job");
                result = {{"assigned", true}};
            } else if (action == "kill-job") {
                if (!job || !TerminateJobObject(job, 77))
                    throw std::runtime_error("Cannot terminate fixture job");
                CloseHandle(job);
                job = nullptr;
                result = {{"terminated", true}};
            } else if (action == "exit") {
                PostQuitMessage(0);
                result = {{"exiting", true}};
            }
            std::cout << result.dump() << '\n' << std::flush;
        } catch (const std::exception &e) {
            std::cout << wm::Json{{"error", e.what()}}.dump() << '\n' << std::flush;
        }
        return 0;
    }
    return DefWindowProcW(w, m, a, b);
}
int wmain() {
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW c{};
    c.hInstance = GetModuleHandleW(nullptr);
    c.lpfnWndProc = Proc;
    c.lpszClassName = L"MWWindowManagerFixture";
    RegisterClassW(&c);
    c.lpszClassName = L"MWWindowManagerFixtureControl";
    RegisterClassW(&c);
    control = CreateWindowW(c.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                            c.hInstance, nullptr);
    Create(0);
    Create(1);
    std::cout << wm::Json{{"pid", GetCurrentProcessId()}, {"state", State(0)}}.dump() << '\n'
              << std::flush;
    std::thread input([] {
        std::string line;
        while (std::getline(std::cin, line)) {
            try {
                auto p = std::make_unique<wm::Json>(wm::Json::parse(line));
                if (!PostMessageW(control, WM_APP + 1, 0, reinterpret_cast<LPARAM>(p.get())))
                    break;
                p.release();
            } catch (...) {
            }
        }
        PostMessageW(control, WM_CLOSE, 0, 0);
    });
    MSG m{};
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    CancelSynchronousIo(static_cast<HANDLE>(input.native_handle()));
    input.join();
    for (const auto &[i, w] : windows) {
        (void)i;
        DestroyWindow(w);
    }
    if (job)
        CloseHandle(job);
    DestroyWindow(control);
    return 0;
}
