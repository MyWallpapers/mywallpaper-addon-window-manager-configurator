#include "platform.hpp"
#include <fstream>
#include <stdexcept>
namespace wm {
std::wstring Wide(const std::string &text) {
    if (text.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0);
    if (!size)
        throw std::invalid_argument("Texte UTF-8 invalide");
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                        result.data(), size);
    return result;
}
std::string Utf8(const std::wstring &text) {
    if (text.empty())
        return {};
    const int size =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                            static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!size)
        throw std::invalid_argument("Texte UTF-16 invalide");
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}
Rect WindowRect(HWND window) {
    RECT r{};
    if (!GetWindowRect(window, &r))
        return {};
    return {r.left, r.top, r.right - r.left, r.bottom - r.top};
}
RECT NativeRect(const Rect &r) {
    return {r.x, r.y, r.x + r.width, r.y + r.height};
}
static uint64_t Creation(HANDLE process) {
    FILETIME c{}, e{}, k{}, u{};
    if (!GetProcessTimes(process, &c, &e, &k, &u))
        return 0;
    return (uint64_t{c.dwHighDateTime} << 32) | c.dwLowDateTime;
}
Identity Identify(HWND window) {
    DWORD pid{};
    GetWindowThreadProcessId(window, &pid);
    if (!pid)
        return {};
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return {};
    const auto created = Creation(process);
    CloseHandle(process);
    return {window, pid, created};
}
bool Alive(const Identity &identity) {
    return identity.window && identity.created && IsWindow(identity.window) &&
           Identify(identity.window) == identity;
}
void AtomicJson(const std::filesystem::path &path, const Json &value) {
    const auto temporary = path.wstring() + L".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file << value.dump();
        file.flush();
        if (!file)
            throw std::runtime_error("Impossible d’enregistrer l’état local");
    }
    DWORD error = 0;
    for (int attempt = 0; attempt < 8; ++attempt) {
        if (MoveFileExW(temporary.c_str(), path.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return;
        error = GetLastError();
        if (error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION &&
            error != ERROR_ACCESS_DENIED)
            break;
        Sleep(15);
    }
    throw std::runtime_error("Impossible de remplacer l’état local (Windows " +
                             std::to_string(error) + ")");
}
void WriteRecovery(const std::filesystem::path &path, const std::vector<OwnedState> &values) {
    Json entries = Json::array();
    for (const auto &s : values)
        if (Alive(s.identity))
            entries.push_back({{"window", reinterpret_cast<uintptr_t>(s.identity.window)},
                               {"pid", s.identity.pid},
                               {"created", s.identity.created},
                               {"style", s.style},
                               {"exStyle", s.ex_style},
                               {"expectedStyle", s.expected_style},
                               {"expectedExStyle", s.expected_ex_style},
                               {"rect",
                                {s.before_fullscreen.x, s.before_fullscreen.y,
                                 s.before_fullscreen.width, s.before_fullscreen.height}},
                               {"topmost", s.topmost_owned},
                               {"expectedTopmost", s.expected_topmost},
                               {"fullscreen", s.fullscreen_owned},
                               {"tray", s.tray_owned},
                               {"trayMinimized", s.tray_minimized},
                               {"property", Utf8(s.property)},
                               {"cookie", s.cookie}});
    AtomicJson(path, {{"version", 1},
                      {"ownerPid", GetCurrentProcessId()},
                      {"ownerCreated", Creation(GetCurrentProcess())},
                      {"entries", entries}});
}
bool RestoreOwned(const OwnedState &s) {
    if (!Alive(s.identity) ||
        reinterpret_cast<uintptr_t>(GetPropW(s.identity.window, s.property.c_str())) != s.cookie)
        return true;
    const auto w = s.identity.window;
    const auto style = GetWindowLongPtrW(w, GWL_STYLE), ex = GetWindowLongPtrW(w, GWL_EXSTYLE);
    bool complete = true;
    constexpr LONG_PTR dynamic = WS_VISIBLE | WS_MINIMIZE | WS_MAXIMIZE;
    if (s.fullscreen_owned && (style & ~dynamic) == (s.expected_style & ~dynamic) &&
        (ex & ~WS_EX_TOPMOST) == (s.expected_ex_style & ~WS_EX_TOPMOST)) {
        SetWindowLongPtrW(w, GWL_STYLE, (s.style & ~dynamic) | (style & dynamic));
        SetWindowLongPtrW(w, GWL_EXSTYLE, (s.ex_style & ~WS_EX_TOPMOST) | (ex & WS_EX_TOPMOST));
        complete = SetWindowPos(w, nullptr, s.before_fullscreen.x, s.before_fullscreen.y,
                                s.before_fullscreen.width, s.before_fullscreen.height,
                                SWP_NOACTIVATE | SWP_NOZORDER | SWP_FRAMECHANGED |
                                    SWP_ASYNCWINDOWPOS) != FALSE;
    }
    if (s.topmost_owned && ((ex & WS_EX_TOPMOST) != 0) == s.expected_topmost)
        complete = (SetWindowPos(
                        w, (s.ex_style & WS_EX_TOPMOST) ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS) != FALSE) &&
                   complete;
    if (s.tray_owned && (s.tray_minimized ? IsIconic(w) : !IsWindowVisible(w))) {
        ShowWindowAsync(w, SW_SHOWNOACTIVATE);
        complete = (s.tray_minimized ? !IsIconic(w) : IsWindowVisible(w) != FALSE) && complete;
    }
    return complete;
}
void ReleaseOwner(const OwnedState &s) {
    if (Alive(s.identity) &&
        reinterpret_cast<uintptr_t>(GetPropW(s.identity.window, s.property.c_str())) == s.cookie)
        RemovePropW(s.identity.window, s.property.c_str());
}
static int RecoverFile(const std::filesystem::path &path, DWORD owner, uint64_t created,
                       DWORD budget = 2000) {
    const auto deadline = GetTickCount64() + budget;
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return 0;
        if (std::filesystem::file_size(path) > 1024 * 1024)
            return 3;
        Json value;
        in >> value;
        in.close();
        if (value.at("version") != 1 || value.at("ownerPid") != owner ||
            value.at("ownerCreated") != created || !value.at("entries").is_array() ||
            value.at("entries").size() > 128)
            return 3;
        bool all_done = true;
        for (const auto &e : value.at("entries")) {
            const auto r = e.at("rect").get<std::vector<int>>();
            if (r.size() != 4)
                continue;
            OwnedState s{{reinterpret_cast<HWND>(e.at("window").get<uintptr_t>()),
                          e.at("pid").get<DWORD>(), e.at("created").get<uint64_t>()},
                         e.at("style"),
                         e.at("exStyle"),
                         e.at("expectedStyle"),
                         e.at("expectedExStyle"),
                         {r[0], r[1], r[2], r[3]},
                         e.at("topmost"),
                         e.at("expectedTopmost"),
                         e.at("fullscreen"),
                         e.at("tray")};
            s.property = Wide(e.at("property").get<std::string>());
            s.cookie = e.at("cookie").get<uintptr_t>();
            s.tray_minimized = e.value("trayMinimized", false);
            bool done = false;
            for (int attempt = 0; attempt < 20; ++attempt) {
                if (RestoreOwned(s)) {
                    ReleaseOwner(s);
                    done = true;
                    break;
                }
                if (GetTickCount64() >= deadline)
                    break;
                Sleep(50);
            }
            all_done = all_done && done;
        }
        if (all_done)
            std::filesystem::remove(path);
    } catch (...) {
        return 3;
    }
    return 0;
}
int Guardian(DWORD owner, uint64_t created, const std::filesystem::path &path) {
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, owner);
    if (!process || Creation(process) != created) {
        if (process)
            CloseHandle(process);
        return 2;
    }
    const auto event_name =
        L"Local\\MWWindowRecoveryReady_" + std::to_wstring(owner) + L"_" + std::to_wstring(created);
    HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, event_name.c_str());
    if (!ready) {
        CloseHandle(process);
        return 2;
    }
    SetEvent(ready);
    CloseHandle(ready);
    WaitForSingleObject(process, INFINITE);
    CloseHandle(process);
    return RecoverFile(path, owner, created);
}
void RecoverPending(const std::filesystem::path &directory) {
    int count = 0;
    const auto deadline = GetTickCount64() + 1000;
    for (const auto &item : std::filesystem::directory_iterator(directory)) {
        if (++count > 256 || GetTickCount64() >= deadline)
            break;
        const auto name = item.path().filename().wstring();
        if (!name.starts_with(L"window-recovery-") || item.path().extension() != L".json")
            continue;
        try {
            if (!item.is_regular_file() || item.file_size() > 1024 * 1024)
                continue;
            std::ifstream in(item.path(), std::ios::binary);
            Json value;
            in >> value;
            in.close();
            const auto owner = value.at("ownerPid").get<DWORD>();
            const auto created = value.at("ownerCreated").get<uint64_t>();
            HANDLE process =
                OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, owner);
            bool dead = !process && GetLastError() == ERROR_INVALID_PARAMETER;
            if (process) {
                dead = Creation(process) != created ||
                       WaitForSingleObject(process, 0) == WAIT_OBJECT_0;
                CloseHandle(process);
            }
            if (dead)
                (void)RecoverFile(item.path(), owner, created, 100);
        } catch (...) { /* Retain unreadable evidence; never guess its owner. */
        }
    }
}
HANDLE StartGuardian(const std::filesystem::path &path) {
    std::wstring executable(32768, 0);
    const auto length =
        GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (!length || length >= executable.size())
        throw std::runtime_error("Chemin du companion indisponible");
    executable.resize(length);
    // The host gives an absolute scratch path. Quote only generated numeric arguments and an
    // absolute path.
    if (path.wstring().find(L'"') != std::wstring::npos)
        throw std::invalid_argument("Chemin local invalide");
    const auto created = Creation(GetCurrentProcess());
    const auto event_name = L"Local\\MWWindowRecoveryReady_" +
                            std::to_wstring(GetCurrentProcessId()) + L"_" +
                            std::to_wstring(created);
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, event_name.c_str());
    if (!ready)
        throw std::runtime_error("Événement de récupération indisponible");
    std::wstring command = L"\"" + executable + L"\" --guardian " +
                           std::to_wstring(GetCurrentProcessId()) + L" " +
                           std::to_wstring(created) + L" \"" + path.wstring() + L"\"";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info)) {
        CloseHandle(ready);
        throw std::runtime_error("Le gardien de récupération n’a pas démarré");
    }
    CloseHandle(info.hThread);
    HANDLE signals[] = {ready, info.hProcess};
    const auto result = WaitForMultipleObjects(2, signals, FALSE, 3000);
    CloseHandle(ready);
    if (result != WAIT_OBJECT_0) {
        TerminateProcess(info.hProcess, 2);
        CloseHandle(info.hProcess);
        throw std::runtime_error("Le gardien de récupération n’est pas prêt");
    }
    return info.hProcess;
}
} // namespace wm
