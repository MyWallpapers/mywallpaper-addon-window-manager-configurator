#include "runtime.hpp"
#include "wire.hpp"
#include <algorithm>
#include <dwmapi.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <shlobj.h>
#include <stdexcept>
namespace wm {
Runtime *Runtime::active_ = nullptr;
constexpr UINT kFrame = WM_APP + 1, kEndInput = WM_APP + 2, kTray = WM_APP + 3;
static bool Down(int key) {
    return (GetAsyncKeyState(key) & 0x8000) != 0;
}
static Rect VisibleRect(HWND window) {
    RECT rect{};
    if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect))))
        return {rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top};
    return WindowRect(window);
}
static int KeyCode(const std::string &name) {
    std::string key = name;
    for (auto &c : key)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (key.empty())
        return 0;
    if (key.size() == 1 && std::isalnum(static_cast<unsigned char>(key[0])))
        return key[0];
    if (key == "SPACE")
        return VK_SPACE;
    if (key == "LEFT")
        return VK_LEFT;
    if (key == "RIGHT")
        return VK_RIGHT;
    if (key == "UP")
        return VK_UP;
    if (key == "DOWN")
        return VK_DOWN;
    if (key[0] == 'F' && key.size() <= 3) {
        try {
            size_t used{};
            int n = std::stoi(key.substr(1), &used);
            if (used == key.size() - 1 && n >= 1 && n <= 24)
                return VK_F1 + n - 1;
        } catch (...) {
        }
    }
    return -1;
}
Runtime::Runtime(DWORD fixture_pid)
    : fixture_pid_(fixture_pid), config_(ParseConfig(Json::object())),
      overlay_([this](int zone) { Choose(zone); }) {
    if (active_)
        throw std::runtime_error("Une instance existe déjà");
    active_ = this;
    stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    frame_done_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    WNDCLASSW cls{};
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpfnWndProc = Proc;
    cls.lpszClassName = L"MWWindowManagerCompanion";
    RegisterClassW(&cls);
    window_ = CreateWindowExW(WS_EX_TOOLWINDOW, cls.lpszClassName, L"Placement des fenêtres",
                              WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, cls.hInstance, this);
    if (!window_ || !stop_event_ || !frame_done_)
        throw std::runtime_error("Initialisation Win32 impossible");
    taskbar_created_ = RegisterWindowMessageW(L"TaskbarCreated");
    (void)CoCreateInstance(CLSID_VirtualDesktopManager, nullptr, CLSCTX_INPROC_SERVER,
                           IID_PPV_ARGS(&desktops_));
}
Runtime::~Runtime() {
    Cleanup();
    if (window_)
        DestroyWindow(window_);
    if (stop_event_)
        CloseHandle(stop_event_);
    if (frame_done_)
        CloseHandle(frame_done_);
    active_ = nullptr;
}
int Runtime::Run() {
    reader_ = std::thread([this] { ReadFrames(); });
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    Cleanup();
    return failed_ ? 1 : 0;
}
void Runtime::ReadFrames() {
    try {
        while (WaitForSingleObject(stop_event_, 0) == WAIT_TIMEOUT) {
            auto frame = wire::Read(std::cin);
            if (!frame)
                break;
            auto value = std::make_unique<Json>(std::move(*frame));
            if (!PostMessageW(window_, kFrame, 0, reinterpret_cast<LPARAM>(value.get())))
                break;
            value.release();
            HANDLE signals[] = {stop_event_, frame_done_};
            if (WaitForMultipleObjects(2, signals, FALSE, INFINITE) == WAIT_OBJECT_0)
                break;
        }
    } catch (const std::exception &e) {
        std::cerr << "Entrée process-v2 : " << e.what() << '\n';
        failed_ = true;
    }
    PostMessageW(window_, kEndInput, 0, 0);
}
LRESULT CALLBACK Runtime::Proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto self = reinterpret_cast<Runtime *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<Runtime *>(reinterpret_cast<CREATESTRUCTW *>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self)
        return DefWindowProcW(window, message, wparam, lparam);
    try {
        if (message == kFrame) {
            std::unique_ptr<Json> frame(reinterpret_cast<Json *>(lparam));
            self->Frame(*frame);
            SetEvent(self->frame_done_);
            return 0;
        }
        if (message == kEndInput || message == WM_CLOSE) {
            self->stopping_ = true;
            PostQuitMessage(0);
            return 0;
        }
        if (message == WM_TIMER) {
            self->Tick();
            return 0;
        }
        if (message == WM_HOTKEY) {
            const auto found = self->hotkeys_.find(static_cast<int>(wparam));
            if (found != self->hotkeys_.end()) {
                const auto w = GetForegroundWindow();
                if (found->second.first == "palette")
                    self->Palette(w);
                else
                    self->Execute(w, found->second.first, found->second.second);
            }
            return 0;
        }
        if (message == kTray) {
            if (LOWORD(lparam) == WM_RBUTTONUP || LOWORD(lparam) == WM_CONTEXTMENU ||
                LOWORD(lparam) == WM_LBUTTONUP)
                self->Menu();
            return 0;
        }
        if (message == self->taskbar_created_ && self->ready_ && !self->fixture_pid_) {
            self->tray_added_ = false;
            self->Settings(self->config_);
            return 0;
        }
        if (message == WM_DISPLAYCHANGE) {
            self->animation_.reset();
            self->overlay_.Hide();
            self->work_areas_.clear();
            return 0;
        }
        if (message == WM_QUERYENDSESSION)
            return TRUE;
        if (message == WM_ENDSESSION && wparam) {
            self->Cleanup();
            return 0;
        }
    } catch (const std::exception &e) {
        self->Notice(e.what());
        if (message == kFrame) {
            SetEvent(self->frame_done_);
            if (!self->ready_) {
                try {
                    wire::Write(std::cout, {{"v", 5},
                                            {"type", "error"},
                                            {"code", "invalid_init"},
                                            {"message", e.what()}});
                } catch (...) {
                }
                self->failed_ = true;
                PostQuitMessage(1);
            } else
                self->Reply({{"event", "rejected"}, {"message", e.what()}});
        }
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
void Runtime::Reply(const Json &value) {
    wire::Write(std::cout,
                {{"type", "message"}, {"v", 5}, {"target", "broadcast"}, {"payload", value}});
}
void Runtime::Notice(const std::string &message) {
    last_notice_ = message;
    std::cerr << "Placement : " << message << '\n';
    if (tray_added_ && !fixture_pid_) {
        NOTIFYICONDATAW icon{sizeof(icon)};
        icon.hWnd = window_;
        icon.uID = 1;
        icon.uFlags = NIF_INFO;
        icon.dwInfoFlags = NIIF_INFO;
        wcscpy_s(icon.szInfoTitle, L"Placement des fenêtres");
        const auto text = Wide(message);
        wcsncpy_s(icon.szInfo, text.c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &icon);
    }
}
void Runtime::Frame(const Json &frame) {
    if (!frame.is_object() || frame.value("v", 0) != 5)
        throw std::invalid_argument("Version process-v2 attendue : 5");
    const auto type = frame.value("type", std::string{});
    if (!ready_) {
        if (type != "init")
            throw std::invalid_argument("Le premier message doit initialiser le companion");
        const auto addon = frame.at("addonId").get<std::string>(),
                   digest = frame.at("artifactDigest").get<std::string>();
        if (addon.empty() || addon.size() > 256 ||
            frame.value("targetArchitecture", std::string{}) != "windows-x86_64" ||
            frame.value("version", std::string{}).empty() || digest.size() != 71 ||
            !digest.starts_with("sha256:") ||
            !std::all_of(digest.begin() + 7, digest.end(),
                         [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); }))
            throw std::invalid_argument("Identité ou architecture d’initialisation invalide");
        scratch_ = Wide(frame.at("scratchDirectory").get<std::string>());
        if (!scratch_.is_absolute() || !std::filesystem::is_directory(scratch_))
            throw std::invalid_argument("Le dossier local du host doit exister et être absolu");
        auto parsed = ParseConfig(frame.at("deviceSettings"));
        const auto name = L"Local\\MyWallpaperWindowPlacement" +
                          (fixture_pid_ ? L"_fixture_" + std::to_wstring(fixture_pid_) : L"");
        mutex_ = CreateMutexW(nullptr, TRUE, name.c_str());
        if (!mutex_ || GetLastError() == ERROR_ALREADY_EXISTS)
            throw std::runtime_error("Cet add-on est déjà actif. Gardez une seule couche active.");
        owner_property_ = L"MyWallpaper.WindowPlacement." + std::to_wstring(GetCurrentProcessId()) +
                          L"." + std::to_wstring(Identify(window_).created);
        if (!fixture_pid_) {
            PWSTR local = nullptr;
            if (FAILED(
                    SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &local)))
                throw std::runtime_error("Stockage local indisponible");
            const auto storage = std::filesystem::path(local) / L"MyWallpaper" / L"Addons" /
                                 L"window-manager-configurator";
            CoTaskMemFree(local);
            std::filesystem::create_directories(storage);
            scratch_ = storage;
        }
        BOOL in_job = FALSE;
        if (!IsProcessInJob(GetCurrentProcess(), nullptr, &in_job))
            throw std::runtime_error("Le contexte de supervision Windows est inconnu");
        contained_ = in_job != FALSE;
        RecoverPending(scratch_);
        recovery_path_ = scratch_ / (L"window-recovery-" + std::to_wstring(GetCurrentProcessId()) +
                                     L"-" + std::to_wstring(Identify(window_).created) + L".json");
        memory_path_ = scratch_ / L"window-memory.json";
        if (std::filesystem::exists(memory_path_)) {
            try {
                if (std::filesystem::file_size(memory_path_) <= 1024 * 1024) {
                    std::ifstream in(memory_path_, std::ios::binary);
                    Json data;
                    in >> data;
                    if (data.value("version", 0) == 1 && data.at("placements").is_object() &&
                        data.at("placements").size() <= 256 && data.at("desktops").is_object() &&
                        data.at("desktops").size() <= 64)
                        memory_ = std::move(data);
                }
            } catch (const std::exception &) {
                Notice("La mémoire locale est illisible ; les placements restent disponibles.");
            }
        }
        SaveRecovery();
        guardian_ = StartGuardian(recovery_path_);
        EnumWindows(
            [](HWND w, LPARAM p) -> BOOL {
                auto self = reinterpret_cast<Runtime *>(p);
                if (self->Eligible(w, true))
                    self->seen_[w] = Identify(w);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(this));
        ready_ = true;
        Settings(std::move(parsed));
        last_foreground_ = GetForegroundWindow();
        SetTimer(window_, 1, 16, nullptr);
        wire::Write(std::cout, {{"type", "ready"}, {"v", 5}});
        return;
    }
    if (type == "init")
        throw std::invalid_argument("Initialisation déjà effectuée");
    if (type == "settings") {
        Settings(ParseConfig(frame.at("deviceSettings")));
        Reply({{"event", "settings-applied"},
               {"settingsDefinitions", Manifest().at("settings").size()},
               {"hotkeyConflicts", hotkey_failures_}});
        return;
    }
    if (type == "shutdown") {
        stopping_ = true;
        Cleanup();
        PostQuitMessage(0);
        return;
    }
    if (type != "message")
        throw std::invalid_argument("Message process-v2 inconnu");
    if (frame.value("surface", std::string{}) != "wallpaper" &&
        frame.value("surface", std::string{}) != "interface")
        throw std::invalid_argument("Surface de message invalide");
    const auto &payload = frame.at("payload");
    if (!payload.is_object())
        throw std::invalid_argument("Commande attendue sous forme d’objet");
    const auto action = payload.value("action", std::string{});
    if (action == "work-area") {
        if (payload.value("version", 0) != 1)
            throw std::invalid_argument("Version de zone de travail inconnue");
        const auto device = payload.at("monitorDevice").get<std::string>();
        const auto &r = payload.at("rect");
        Rect area{r.at("x"), r.at("y"), r.at("width"), r.at("height")};
        bool found = false;
        for (const auto &m : Monitors())
            if (m.device == device && area.width > 0 && area.height > 0 && area.x >= m.screen.x &&
                area.y >= m.screen.y &&
                int64_t{area.x} + area.width <= int64_t{m.screen.x} + m.screen.width &&
                int64_t{area.y} + area.height <= int64_t{m.screen.y} + m.screen.height)
                found = true;
        if (!found)
            throw std::invalid_argument("Zone de travail hors de l’écran déclaré");
        work_areas_[device] = area;
        Reply({{"event", "work-area-applied"}});
        return;
    }
    const auto target = Target(payload);
    if (action == "status") {
        Json monitors = Json::array();
        for (const auto &m : Monitors())
            monitors.push_back({{"device", m.device},
                                {"work", {m.work.x, m.work.y, m.work.width, m.work.height}},
                                {"dpi", m.dpi}});
        Reply({{"event", "status"},
               {"enabled", config_.flag("enabled")},
               {"placements", placements_},
               {"monitors", monitors},
               {"hotkeyConflicts", hotkey_failures_},
               {"notice", last_notice_},
               {"guardianRunning", guardian_ && WaitForSingleObject(guardian_, 0) == WAIT_TIMEOUT},
               {"contained", contained_},
               {"trayMode", contained_ ? "minimize-accessible" : "hide-with-guardian"},
               {"desktopApi", desktops_ != nullptr},
               {"targetImage", Image(target)}});
        return;
    }
    if (action == "render-profile" && fixture_pid_) {
        const auto file = payload.at("file").get<std::string>();
        const std::filesystem::path destination = Wide(file);
        if (!destination.is_absolute() || destination.extension() != L".png")
            throw std::invalid_argument("Chemin PNG absolu attendu");
        const auto monitor = MonitorFor(target);
        auto zones = Zones(target, monitor);
        // Render without showing a full-screen overlay over the user's active session.
        const Rect work{0, 0, 960, 600};
        const auto layout = Layout(config_);
        zones.clear();
        for (const auto &z : layout)
            zones.push_back({ZoneRect(z, work, 20, 16), z.name});
        overlay_.SavePreview(work, zones, config_, 0, destination);
        Reply({{"event", "rendered"}, {"file", file}});
        return;
    }
    if (action == "palette") {
        Palette(target);
        Reply({{"event", "palette-shown"}});
        return;
    }
    if (action == "clear-memory") {
        memory_["placements"] = Json::object();
        memory_["desktops"] = Json::object();
        Persist();
        Reply({{"event", "memory-cleared"}});
        return;
    }
    if (action == "save-desktop-layout") {
        const auto desktop = Desktop(target);
        if (desktop.empty())
            throw std::runtime_error("Ce bureau n’est pas accessible par l’API Windows");
        if (memory_["desktops"].size() >= 64 && !memory_["desktops"].contains(desktop))
            throw std::runtime_error("La mémoire de bureaux est pleine");
        memory_["desktops"][desktop] = config_.text("layout");
        Persist();
        Reply({{"event", "desktop-layout-saved"}});
        return;
    }
    const auto success = Execute(target, action, payload.value("zone", -1));
    Reply({{"event", "action"}, {"action", action}, {"ok", success}});
}
void Runtime::Settings(Config config) {
    overlay_.Hide();
    animation_.reset();
    drag_ = {};
    config_ = std::move(config);
    for (const auto &[id, action] : hotkeys_) {
        (void)action;
        UnregisterHotKey(window_, id);
    }
    hotkeys_.clear();
    hotkey_failures_.clear();
    if (mouse_hook_) {
        UnhookWindowsHookEx(mouse_hook_);
        mouse_hook_ = nullptr;
    }
    if (keyboard_hook_) {
        UnhookWindowsHookEx(keyboard_hook_);
        keyboard_hook_ = nullptr;
    }
    for (auto event : events_)
        UnhookWinEvent(event);
    events_.clear();
    if (!config_.flag("enabled")) {
        owned_.erase(std::remove_if(owned_.begin(), owned_.end(),
                                    [](const auto &s) {
                                        if (RestoreOwned(s)) {
                                            ReleaseOwner(s);
                                            return true;
                                        }
                                        return false;
                                    }),
                     owned_.end());
        previous_.clear();
        SaveRecovery();
        return;
    }
    if (!fixture_pid_) {
        BindHotkeys();
        mouse_hook_ = SetWindowsHookExW(WH_MOUSE_LL, Mouse, GetModuleHandleW(nullptr), 0);
        keyboard_hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, Keyboard, GetModuleHandleW(nullptr), 0);
        if (!mouse_hook_ || !keyboard_hook_)
            Notice("Les gestes globaux n’ont pas pu être activés ; le menu de placement reste "
                   "disponible.");
        if (!tray_added_) {
            NOTIFYICONDATAW icon{sizeof(icon)};
            icon.hWnd = window_;
            icon.uID = 1;
            icon.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
            icon.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
            icon.uCallbackMessage = kTray;
            wcscpy_s(icon.szTip, L"Placement des fenêtres — zones, position, premier plan");
            tray_added_ = Shell_NotifyIconW(NIM_ADD, &icon) != FALSE;
        }
    }
    for (const auto event : {EVENT_OBJECT_SHOW, EVENT_OBJECT_DESTROY, EVENT_SYSTEM_MOVESIZESTART,
                             EVENT_SYSTEM_MOVESIZEEND, EVENT_SYSTEM_FOREGROUND}) {
        const auto hook = SetWinEventHook(event, event, nullptr, Event, fixture_pid_, 0,
                                          WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        if (hook)
            events_.push_back(hook);
        else
            Notice("Un événement de fenêtre n’est pas disponible.");
    }
}
void Runtime::BindHotkeys() {
    const auto prefix = config_.text("hotkeyPrefix");
    if (prefix == "off")
        return;
    const UINT modifiers = prefix == "ctrl-alt"  ? MOD_CONTROL | MOD_ALT
                           : prefix == "win-alt" ? MOD_WIN | MOD_ALT
                                                 : MOD_CONTROL | MOD_SHIFT;
    int id = 1;
    std::set<std::pair<UINT, int>> combinations;
    auto bind = [&](const std::string &action, int zone, int key, UINT mods,
                    const std::string &name) {
        if (key == 0)
            return;
        if (key < 0 || !combinations.insert({mods, key}).second) {
            hotkey_failures_.push_back(name);
            return;
        }
        const int slot = id++;
        if (RegisterHotKey(window_, slot, mods | MOD_NOREPEAT, key))
            hotkeys_[slot] = {action, zone};
        else
            hotkey_failures_.push_back(name);
    };
    for (const auto &pair :
         std::vector<std::pair<const char *, const char *>>{{"paletteKey", "palette"},
                                                            {"centerKey", "center"},
                                                            {"restoreKey", "restore"},
                                                            {"nextMonitorKey", "next-monitor"},
                                                            {"topmostKey", "topmost"},
                                                            {"trayKey", "tray"},
                                                            {"rememberKey", "remember"},
                                                            {"fullscreenKey", "fullscreen"}})
        bind(pair.second, -1, KeyCode(config_.text(pair.first)), modifiers,
             config_.text(pair.first));
    if (config_.flag("arrowShortcuts")) {
        bind("left", -1, VK_LEFT, modifiers, "Left");
        bind("right", -1, VK_RIGHT, modifiers, "Right");
        bind("top", -1, VK_UP, modifiers, "Up");
        bind("bottom", -1, VK_DOWN, modifiers, "Down");
        for (const auto &[key, action] :
             std::vector<std::pair<int, std::string>>{{VK_NUMPAD7, "top-left"},
                                                      {VK_NUMPAD9, "top-right"},
                                                      {VK_NUMPAD1, "bottom-left"},
                                                      {VK_NUMPAD3, "bottom-right"},
                                                      {VK_NUMPAD5, "center"}})
            bind(action, -1, key, modifiers, action);
    }
    if (config_.flag("touchpadShortcut"))
        bind("palette", -1, VK_F12, MOD_CONTROL | MOD_SHIFT, "Ctrl+Shift+F12");
    if (!hotkey_failures_.empty())
        Notice("Un ou plusieurs raccourcis sont déjà utilisés ou invalides. Choisissez d’autres "
               "touches dans les réglages.");
}
std::string Runtime::Image(HWND window) const {
    const auto identity = Identify(window);
    if (!identity.created)
        return {};
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, identity.pid);
    if (!process)
        return {};
    std::wstring path(32768, 0);
    DWORD length = static_cast<DWORD>(path.size());
    const BOOL ok = QueryFullProcessImageNameW(process, 0, path.data(), &length);
    CloseHandle(process);
    if (!ok)
        return {};
    path.resize(length);
    auto result = Utf8(std::filesystem::path(path).filename().wstring());
    for (auto &c : result)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}
bool Runtime::Fullscreen(HWND w) const {
    const auto monitor = MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(monitor, &info);
    const auto rect = VisibleRect(w);
    return !(GetWindowLongPtrW(w, GWL_STYLE) & WS_CAPTION) && rect.x <= info.rcMonitor.left + 1 &&
           rect.y <= info.rcMonitor.top + 1 && rect.x + rect.width >= info.rcMonitor.right - 1 &&
           rect.y + rect.height >= info.rcMonitor.bottom - 1;
}
bool Runtime::Eligible(HWND window, bool allow_fullscreen, bool fast) const {
    if (!window || !IsWindow(window) || !IsWindowVisible(window) || IsIconic(window) ||
        GetAncestor(window, GA_ROOT) != window || GetWindow(window, GW_OWNER))
        return false;
    DWORD pid{};
    GetWindowThreadProcessId(window, &pid);
    if (pid == GetCurrentProcessId() || !pid)
        return false;
    wchar_t cls[256]{};
    GetClassNameW(window, cls, 256);
    if (fixture_pid_ && (pid != fixture_pid_ || wcscmp(cls, L"MWWindowManagerFixture") != 0))
        return false;
    if (GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD ||
        GetWindowLongPtrW(window, GWL_EXSTYLE) &
            (WS_EX_TOOLWINDOW | (fixture_pid_ ? 0 : WS_EX_NOACTIVATE)))
        return false;
    if (wcscmp(cls, L"Shell_TrayWnd") == 0 || wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0 ||
        wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0)
        return false;
    BOOL cloaked{};
    if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
        cloaked)
        return false;
    if (desktops_ && !fast) {
        BOOL current = TRUE;
        if (SUCCEEDED(desktops_->IsWindowOnCurrentVirtualDesktop(window, &current)) && !current)
            return false;
    }
    const auto image = Image(window);
    if (image.empty() || MatchesApp("mywallpaper.exe,mywallpaper-desktop.exe", image) ||
        MatchesApp(config_.text("excludedApps"), image))
        return false;
    if (!config_.text("onlyApps").empty() && !MatchesApp(config_.text("onlyApps"), image))
        return false;
    if (config_.flag("respectFullscreen") && !allow_fullscreen && Fullscreen(window)) {
        bool ours = false;
        for (const auto &s : owned_)
            if (s.identity.window == window && s.fullscreen_owned && Alive(s.identity))
                ours = true;
        if (!ours)
            return false;
    }
    return true;
}
std::string Runtime::Desktop(HWND window) const {
    GUID guid{};
    if (!desktops_ || FAILED(desktops_->GetWindowDesktopId(window, &guid)))
        return {};
    wchar_t buffer[40]{};
    StringFromGUID2(guid, buffer, 40);
    return Utf8(buffer);
}
std::string Runtime::MemoryKey(HWND window) const {
    wchar_t cls[256]{};
    GetClassNameW(window, cls, 256);
    return Image(window) + "|" + Utf8(cls) + "|" + Desktop(window);
}
std::vector<Runtime::Monitor> Runtime::Monitors() const {
    std::vector<Monitor> result;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR handle, HDC, LPRECT, LPARAM data) -> BOOL {
            auto list = reinterpret_cast<std::vector<Monitor> *>(data);
            MONITORINFOEXW info{};
            info.cbSize = sizeof(info);
            if (!GetMonitorInfoW(handle, &info))
                return TRUE;
            UINT x = 96, y = 96;
            (void)GetDpiForMonitor(handle, MDT_EFFECTIVE_DPI, &x, &y);
            list->push_back(
                {handle,
                 {info.rcWork.left, info.rcWork.top, info.rcWork.right - info.rcWork.left,
                  info.rcWork.bottom - info.rcWork.top},
                 {info.rcMonitor.left, info.rcMonitor.top,
                  info.rcMonitor.right - info.rcMonitor.left,
                  info.rcMonitor.bottom - info.rcMonitor.top},
                 Utf8(info.szDevice),
                 (info.dwFlags & MONITORINFOF_PRIMARY) != 0,
                 static_cast<int>(x)});
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&result));
    for (auto &m : result)
        if (work_areas_.contains(m.device))
            m.work = work_areas_.at(m.device);
    std::sort(result.begin(), result.end(), [](const Monitor &a, const Monitor &b) {
        return a.screen.x == b.screen.x ? a.screen.y < b.screen.y : a.screen.x < b.screen.x;
    });
    return result;
}
Runtime::Monitor Runtime::MonitorFor(HWND window, const std::string &selected) const {
    const auto list = Monitors();
    if (list.empty())
        throw std::runtime_error("Aucun écran accessible");
    const auto choice = selected.empty() ? config_.text("targetMonitor") : selected;
    POINT cursor{};
    GetCursorPos(&cursor);
    const auto handle = choice == "cursor" ? MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST)
                                           : MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    for (const auto &m : list)
        if (choice == "primary" ? m.primary : m.handle == handle)
            return m;
    return list.front();
}
std::vector<OverlayZone> Runtime::Zones(HWND window, const Monitor &monitor) const {
    std::string layout;
    const auto desktop = Desktop(window);
    if (config_.flag("desktopLayouts") && memory_.at("desktops").contains(desktop))
        layout = memory_.at("desktops").at(desktop).get<std::string>();
    std::vector<OverlayZone> result;
    for (const auto &zone : Layout(config_, layout))
        result.push_back(
            {ZoneRect(zone, monitor.work, MulDiv(config_.number("outerGap"), monitor.dpi, 96),
                      MulDiv(config_.number("innerGap"), monitor.dpi, 96)),
             zone.name});
    return result;
}
HWND Runtime::Target(const Json &payload) const {
    if (!fixture_pid_)
        return GetForegroundWindow();
    struct Search {
        DWORD pid;
        int index;
        HWND found{};
    } search{fixture_pid_, payload.value("fixtureIndex", 0)};
    EnumWindows(
        [](HWND w, LPARAM data) -> BOOL {
            auto s = reinterpret_cast<Search *>(data);
            DWORD pid{};
            GetWindowThreadProcessId(w, &pid);
            wchar_t cls[256]{};
            GetClassNameW(w, cls, 256);
            if (pid == s->pid && wcscmp(cls, L"MWWindowManagerFixture") == 0) {
                wchar_t title[32]{};
                GetWindowTextW(w, title, 32);
                if (std::wstring(title) == L"Fixture " + std::to_wstring(s->index)) {
                    s->found = w;
                    return FALSE;
                }
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.found;
}
void Runtime::Palette(HWND target) {
    if (!config_.flag("enabled") || !Eligible(target))
        return;
    if (overlay_.Visible() && overlay_.Interactive()) {
        overlay_.Hide();
        return;
    }
    palette_target_ = Identify(target);
    const auto monitor = MonitorFor(target);
    overlay_.Show(monitor.work, Zones(target, monitor), config_, 0, true);
}
void Runtime::Choose(int zone) {
    const auto identity = palette_target_;
    overlay_.Hide();
    if (Alive(identity))
        Execute(identity.window, "zone", zone);
}
void Runtime::Place(HWND window, const Rect &target, bool animate) {
    (void)Own(window);
    SaveRecovery();
    auto id = Identify(window);
    if (!id.created)
        throw std::runtime_error("Fenêtre inaccessible");
    if (previous_.size() >= 128 && !previous_.contains(window))
        throw std::runtime_error("Trop de fenêtres retenues ; redémarrez cet add-on");
    if (!previous_.contains(window) || !Alive(previous_.at(window).identity)) {
        Previous p;
        p.identity = id;
        p.rect = WindowRect(window);
        if (!GetWindowPlacement(window, &p.placement))
            throw std::runtime_error("Le placement initial n’est pas accessible");
        previous_[window] = p;
    }
    if (IsZoomed(window))
        ShowWindowAsync(window, SW_RESTORE);
    const auto current = WindowRect(window), visible = VisibleRect(window);
    Rect adjusted{target.x - (visible.x - current.x), target.y - (visible.y - current.y),
                  target.width + (current.width - visible.width),
                  target.height + (current.height - visible.height)};
    MINMAXINFO limits{};
    DWORD_PTR ignored{};
    if (SendMessageTimeoutW(window, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&limits),
                            SMTO_ABORTIFHUNG | SMTO_BLOCK, 50, &ignored)) {
        adjusted.width = std::max(adjusted.width, static_cast<int>(limits.ptMinTrackSize.x));
        adjusted.height = std::max(adjusted.height, static_cast<int>(limits.ptMinTrackSize.y));
    }
    BOOL system_animation = TRUE;
    (void)SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &system_animation, 0);
    const int duration = animate && !config_.flag("reducedMotion") && system_animation &&
                                 config_.text("motion") != "instant"
                             ? config_.number("duration")
                             : 0;
    if (duration)
        animation_ = Animation{id, current, adjusted, GetTickCount64(), duration};
    else {
        animation_.reset();
        if (!SetWindowPos(window, nullptr, adjusted.x, adjusted.y, adjusted.width, adjusted.height,
                          SWP_NOACTIVATE | SWP_NOZORDER | SWP_ASYNCWINDOWPOS))
            throw std::runtime_error("Windows a refusé le placement de cette fenêtre");
        // An asynchronous request is not yet the window's actual placement.
        animation_ = Animation{id, current, adjusted, GetTickCount64(), 0, adjusted, true};
    }
    ++placements_;
}
OwnedState &Runtime::Own(HWND window) {
    const auto id = Identify(window);
    for (auto &s : owned_)
        if (s.identity == id &&
            reinterpret_cast<uintptr_t>(GetPropW(window, s.property.c_str())) == s.cookie)
            return s;
    if (!id.created)
        throw std::runtime_error("L’identité de cette fenêtre n’est pas accessible");
    owned_.erase(std::remove_if(owned_.begin(), owned_.end(),
                                [](const OwnedState &s) {
                                    return !Alive(s.identity) ||
                                           reinterpret_cast<uintptr_t>(GetPropW(
                                               s.identity.window, s.property.c_str())) != s.cookie;
                                }),
                 owned_.end());
    if (owned_.size() >= 128)
        throw std::runtime_error("Trop d’états de fenêtre retenus");
    const auto style = GetWindowLongPtrW(window, GWL_STYLE),
               ex = GetWindowLongPtrW(window, GWL_EXSTYLE);
    OwnedState state{id, style, ex, style, ex, WindowRect(window)};
    state.property = owner_property_;
    state.cookie = next_cookie_++;
    if (!SetPropW(window, state.property.c_str(), reinterpret_cast<HANDLE>(state.cookie)))
        throw std::runtime_error("Windows a refusé le suivi de cette fenêtre");
    owned_.push_back(std::move(state));
    return owned_.back();
}
void Runtime::SaveRecovery() {
    if (!recovery_path_.empty())
        WriteRecovery(recovery_path_, owned_);
}
bool Runtime::Execute(HWND window, const std::string &action, int zone,
                      const std::string &monitor_choice) {
    if (!config_.flag("enabled"))
        return false;
    if (action == "restore-tray") {
        for (auto &s : owned_)
            if (s.identity.window == window && s.tray_owned && Alive(s.identity)) {
                ShowWindowAsync(window, SW_SHOWNOACTIVATE);
                s.tray_owned = false;
                SaveRecovery();
                return true;
            }
        return false;
    }
    if (!Eligible(window, action == "restore"))
        return false;
    if (action == "remember") {
        Remember(window);
        Notice("Position retenue pour la prochaine ouverture de cette application.");
        return true;
    }
    if (action == "topmost") {
        auto &s = Own(window);
        s.expected_topmost = (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0;
        s.topmost_owned = s.expected_topmost != ((s.ex_style & WS_EX_TOPMOST) != 0);
        SaveRecovery();
        if (!SetWindowPos(window, s.expected_topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                          SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_ASYNCWINDOWPOS))
            throw std::runtime_error("Windows a refusé le premier plan");
        Notice(s.expected_topmost ? "Fenêtre gardée au premier plan." : "Premier plan désactivé.");
        return true;
    }
    if (action == "tray") {
        if (!guardian_ || WaitForSingleObject(guardian_, 0) != WAIT_TIMEOUT)
            throw std::runtime_error("Le gardien de récupération n’est plus actif");
        auto &s = Own(window);
        s.tray_owned = true;
        s.tray_minimized = contained_;
        SaveRecovery();
        ShowWindowAsync(window, contained_ ? SW_MINIMIZE : SW_HIDE);
        Notice(contained_ ? "Fenêtre réduite. Elle reste aussi accessible dans la barre des tâches."
                          : "Fenêtre rangée. Retrouvez-la dans le menu de l’add-on.");
        return true;
    }
    if (action == "fullscreen") {
        auto &s = Own(window);
        animation_.reset();
        if (s.fullscreen_owned) {
            OwnedState restore = s;
            restore.topmost_owned = false;
            restore.tray_owned = false;
            if (!RestoreOwned(restore))
                throw std::runtime_error("Windows n’a pas encore restauré le plein écran");
            s.fullscreen_owned = false;
            SaveRecovery();
            return true;
        }
        s.before_fullscreen = WindowRect(window);
        s.style = GetWindowLongPtrW(window, GWL_STYLE);
        s.ex_style = (s.ex_style & WS_EX_TOPMOST) |
                     (GetWindowLongPtrW(window, GWL_EXSTYLE) & ~WS_EX_TOPMOST);
        s.expected_style = s.style & ~(WS_CAPTION | WS_THICKFRAME);
        s.expected_ex_style =
            GetWindowLongPtrW(window, GWL_EXSTYLE) & ~(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE);
        s.fullscreen_owned = true;
        SaveRecovery();
        SetWindowLongPtrW(window, GWL_STYLE, s.expected_style);
        SetWindowLongPtrW(window, GWL_EXSTYLE, s.expected_ex_style);
        const auto screen = MonitorFor(window, monitor_choice).screen;
        if (!SetWindowPos(window, nullptr, screen.x, screen.y, screen.width, screen.height,
                          SWP_NOACTIVATE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_ASYNCWINDOWPOS))
            throw std::runtime_error("Windows a refusé le plein écran");
        return true;
    }
    if (action == "restore") {
        animation_.reset();
        for (auto &s : owned_)
            if (s.identity.window == window && s.fullscreen_owned && Alive(s.identity)) {
                OwnedState restore = s;
                restore.topmost_owned = false;
                restore.tray_owned = false;
                if (!RestoreOwned(restore))
                    throw std::runtime_error("Windows n’a pas encore restauré le plein écran");
                s.fullscreen_owned = false;
                SaveRecovery();
                return true;
            }
        const bool marked = std::any_of(owned_.begin(), owned_.end(), [&](const auto &s) {
            return s.identity.window == window &&
                   reinterpret_cast<uintptr_t>(GetPropW(window, s.property.c_str())) == s.cookie;
        });
        if (marked && previous_.contains(window) && Alive(previous_.at(window).identity)) {
            auto saved = previous_.at(window);
            if (saved.placement.showCmd == SW_SHOWMAXIMIZED) {
                saved.placement.flags |= WPF_ASYNCWINDOWPLACEMENT;
                if (!SetWindowPlacement(window, &saved.placement))
                    throw std::runtime_error("Windows a refusé la restauration du placement");
            } else
                if (!SetWindowPos(window, nullptr, saved.rect.x, saved.rect.y, saved.rect.width,
                                  saved.rect.height,
                                  SWP_NOACTIVATE | SWP_NOZORDER | SWP_ASYNCWINDOWPOS))
                    throw std::runtime_error("Windows a refusé la restauration du placement");
            previous_.erase(window);
            return true;
        }
        return false;
    }
    const auto monitor = MonitorFor(window, monitor_choice);
    if (action == "center") {
        Place(window, CenterRect(config_, monitor.work, VisibleRect(window)));
        return true;
    }
    if (action == "next-monitor" || action == "previous-monitor") {
        const auto monitors = Monitors();
        if (monitors.size() < 2)
            return false;
        const auto from = MonitorFor(window, "window");
        auto index = std::find_if(monitors.begin(), monitors.end(),
                                  [&](const auto &m) { return m.handle == from.handle; }) -
                     monitors.begin();
        index = (index + (action == "next-monitor" ? 1 : static_cast<int>(monitors.size()) - 1)) %
                monitors.size();
        Place(window, MapToMonitor(VisibleRect(window), from.work, monitors[index].work));
        return true;
    }
    if (action == "maximize") {
        Place(window, monitor.work);
        return true;
    }
    const auto zones = Zones(window, monitor);
    if (action.starts_with("zone") && action.size() == 5 && action[4] >= '1' && action[4] <= '9')
        zone = action[4] - '1';
    if (action == "zone" || action.starts_with("zone")) {
        if (zone < 0 || static_cast<size_t>(zone) >= zones.size())
            return false;
        Place(window, zones[zone].rect);
        return true;
    }
    Zone region{0, 0, 1, 1, {}};
    if (action == "left")
        region = {0, 0, .5, 1, {}};
    else if (action == "right")
        region = {.5, 0, .5, 1, {}};
    else if (action == "top")
        region = {0, 0, 1, .5, {}};
    else if (action == "bottom")
        region = {0, .5, 1, .5, {}};
    else if (action == "top-left")
        region = {0, 0, .5, .5, {}};
    else if (action == "top-right")
        region = {.5, 0, .5, .5, {}};
    else if (action == "bottom-left")
        region = {0, .5, .5, .5, {}};
    else if (action == "bottom-right")
        region = {.5, .5, .5, .5, {}};
    else
        return false;
    Place(window,
          ZoneRect(region, monitor.work, MulDiv(config_.number("outerGap"), monitor.dpi, 96),
                   MulDiv(config_.number("innerGap"), monitor.dpi, 96)));
    return true;
}
void Runtime::Remember(HWND window) {
    const auto key = MemoryKey(window);
    const auto monitor = MonitorFor(window, "window");
    const auto rect = VisibleRect(window);
    if (memory_["placements"].size() >= 256 && !memory_["placements"].contains(key))
        throw std::runtime_error("La mémoire de positions est pleine ; effacez-la depuis le menu.");
    memory_["placements"][key] = {
        {"monitor", monitor.device},
        {"x", (rect.x - monitor.work.x) / static_cast<double>(monitor.work.width)},
        {"y", (rect.y - monitor.work.y) / static_cast<double>(monitor.work.height)},
        {"width", rect.width / static_cast<double>(monitor.work.width)},
        {"height", rect.height / static_cast<double>(monitor.work.height)}};
    Persist();
}
void Runtime::Persist() {
    AtomicJson(memory_path_, memory_);
}
void Runtime::Opened(HWND window) {
    if (!Eligible(window) || !config_.flag("enabled"))
        return;
    const auto image = Image(window);
    for (int i = 1; i <= 3; ++i) {
        const auto suffix = std::to_string(i);
        if (MatchesApp(config_.text(("ruleApp" + suffix).c_str()), image)) {
            const auto action = config_.text(("ruleAction" + suffix).c_str());
            if (action != "none")
                Execute(window, action, -1, config_.text(("ruleMonitor" + suffix).c_str()));
            if (config_.flag(("ruleTopmost" + suffix).c_str()) &&
                !(GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST))
                Execute(window, "topmost");
            return;
        }
    }
    const auto key = MemoryKey(window);
    if (memory_["placements"].contains(key)) {
        const auto &p = memory_["placements"].at(key);
        auto monitor = MonitorFor(window, "window");
        for (const auto &m : Monitors())
            if (m.device == p.value("monitor", std::string{}))
                monitor = m;
        Rect remembered{
            monitor.work.x + static_cast<int>(p.at("x").get<double>() * monitor.work.width),
            monitor.work.y + static_cast<int>(p.at("y").get<double>() * monitor.work.height),
            static_cast<int>(p.at("width").get<double>() * monitor.work.width),
            static_cast<int>(p.at("height").get<double>() * monitor.work.height)};
        Place(window, MapToMonitor(remembered, monitor.work, monitor.work));
        return;
    }
    const auto action = config_.text("newWindowAction");
    if (action != "none")
        Execute(window, action);
}
void CALLBACK Runtime::Event(HWINEVENTHOOK, DWORD event, HWND window, LONG object, LONG child,
                             DWORD, DWORD) {
    auto self = active_;
    if (!self || !self->ready_ || self->stopping_)
        return;
    try {
        if (event == EVENT_OBJECT_SHOW && object == OBJID_WINDOW && child == CHILDID_SELF &&
            self->Eligible(window)) {
            const auto id = Identify(window);
            if (!self->seen_.contains(window) || self->seen_.at(window) != id) {
                if (self->opened_.size() < 128)
                    self->opened_[window] = GetTickCount64();
                self->seen_[window] = id;
            }
        }
        if (event == EVENT_OBJECT_DESTROY && object == OBJID_WINDOW && child == CHILDID_SELF) {
            self->seen_.erase(window);
            self->opened_.erase(window);
            self->previous_.erase(window);
        }
        if (event == EVENT_SYSTEM_MOVESIZESTART && self->Eligible(window)) {
            self->animation_.reset();
            if (!self->drag_.active) {
                POINT cursor{};
                GetCursorPos(&cursor);
                DWORD_PTR hit{};
                const LPARAM point = static_cast<LPARAM>(
                    MAKELONG(static_cast<SHORT>(cursor.x), static_cast<SHORT>(cursor.y)));
                const bool received =
                    SendMessageTimeoutW(window, WM_NCHITTEST, 0, point,
                                        SMTO_ABORTIFHUNG | SMTO_BLOCK, 25, &hit) != 0;
                const bool resize = received && hit >= HTLEFT && hit <= HTBOTTOMRIGHT;
                self->drag_ = {Identify(window),
                               cursor,
                               cursor,
                               WindowRect(window),
                               resize,
                               false,
                               false,
                               true,
                               true};
            }
        }
        if (event == EVENT_SYSTEM_MOVESIZEEND && self->drag_.native &&
            self->drag_.identity.window == window)
            self->EndDrag();
        if (event == EVENT_SYSTEM_FOREGROUND) {
            const auto previous = self->last_foreground_;
            self->last_foreground_ = window;
            if (self->Eligible(window))
                self->last_target_ = Identify(window);
            if (self->config_.flag("focusGuard") && self->opened_.contains(window) &&
                self->Eligible(previous, true) && self->Eligible(window, true)) {
                LASTINPUTINFO input{sizeof(input)};
                if (GetLastInputInfo(&input) &&
                    GetTickCount() - input.dwTime >
                        static_cast<DWORD>(self->config_.number("focusGrace"))) {
                    FLASHWINFO flash{sizeof(flash), window, FLASHW_TRAY, 3, 0};
                    FlashWindowEx(&flash);
                    if (!SetForegroundWindow(previous))
                        self->last_notice_ = "Windows n’a pas autorisé la restitution du focus.";
                }
            }
        }
    } catch (const std::exception &e) {
        std::cerr << "Événement de fenêtre : " << e.what() << '\n';
    }
}
bool Runtime::Modifier() const {
    const auto mode = config_.text("dragModifier");
    return Down(VK_MENU) &&
           (mode == "alt" || (mode == "win-alt" && (Down(VK_LWIN) || Down(VK_RWIN))) ||
            (mode == "ctrl-alt" && Down(VK_CONTROL)));
}
bool Runtime::WantsZones() const {
    const auto mode = config_.text("snapDuringDrag");
    return !drag_.resize && (mode == "always" || (mode == "shift" && Down(VK_SHIFT)));
}
LRESULT CALLBACK Runtime::Mouse(int code, WPARAM event, LPARAM data) {
    auto self = active_;
    if (code < 0 || !self || !self->ready_ || self->stopping_ || !self->config_.flag("enabled"))
        return CallNextHookEx(nullptr, code, event, data);
    const auto info = reinterpret_cast<MSLLHOOKSTRUCT *>(data);
    try {
        if (self->drag_.active && !self->drag_.native) {
            if (event == WM_MOUSEMOVE) {
                self->drag_.cursor = info->pt;
                self->drag_.changed = true;
                return 1;
            }
            if ((self->drag_.resize && event == WM_RBUTTONUP) ||
                (!self->drag_.resize && event == WM_LBUTTONUP)) {
                self->EndDrag();
                return 1;
            }
        }
        if ((event == WM_LBUTTONDOWN || event == WM_RBUTTONDOWN) && self->Modifier() &&
            !self->drag_.active) {
            const bool resize = event == WM_RBUTTONDOWN;
            if (!self->config_.flag(resize ? "resizeAnywhere" : "dragAnywhere"))
                return CallNextHookEx(nullptr, code, event, data);
            const auto target = GetAncestor(WindowFromPoint(info->pt), GA_ROOT);
            if (!self->Eligible(target, false, true) || IsZoomed(target))
                return CallNextHookEx(nullptr, code, event, data);
            const auto rect = WindowRect(target);
            (void)self->Own(target);
            self->animation_.reset();
            self->drag_ = {Identify(target),
                           info->pt,
                           info->pt,
                           rect,
                           resize,
                           info->pt.x < rect.x + rect.width / 2,
                           info->pt.y < rect.y + rect.height / 2,
                           true,
                           false};
            return 1;
        }
    } catch (...) {
        self->overlay_.Hide();
        self->drag_ = {};
    }
    return CallNextHookEx(nullptr, code, event, data);
}
LRESULT CALLBACK Runtime::Keyboard(int code, WPARAM event, LPARAM data) {
    auto self = active_;
    if (code < 0 || !self || !self->overlay_.Visible() || !self->overlay_.Interactive())
        return CallNextHookEx(nullptr, code, event, data);
    const auto info = reinterpret_cast<KBDLLHOOKSTRUCT *>(data);
    const auto key = info->vkCode;
    const bool handled = key == VK_ESCAPE || key == VK_RETURN || key == VK_LEFT ||
                         key == VK_RIGHT || key == VK_UP || key == VK_DOWN ||
                         (key >= '1' && key <= '9') || (key >= VK_NUMPAD1 && key <= VK_NUMPAD9);
    if (!handled)
        return CallNextHookEx(nullptr, code, event, data);
    if (event == WM_KEYDOWN || event == WM_SYSKEYDOWN) {
        try {
            if (key == VK_ESCAPE)
                self->overlay_.Hide();
            else if (key == VK_RETURN)
                self->Choose(self->overlay_.Selected());
            else if (key >= '1' && key <= '9')
                self->Choose(static_cast<int>(key - '1'));
            else if (key >= VK_NUMPAD1 && key <= VK_NUMPAD9)
                self->Choose(static_cast<int>(key - VK_NUMPAD1));
            else {
                const auto count = static_cast<int>(self->overlay_.Count());
                if (count)
                    self->overlay_.Select((self->overlay_.Selected() + count +
                                           (key == VK_LEFT || key == VK_UP ? -1 : 1)) %
                                          count);
            }
        } catch (const std::exception &e) {
            self->Notice(e.what());
        }
    }
    return 1;
}
void Runtime::PreviewDrag() {
    if (!drag_.active)
        return;
    const auto window = drag_.identity.window;
    const auto monitor = MonitorFor(window, "cursor");
    const auto zones = Zones(window, monitor);
    drag_.zone = -1;
    drag_.edge.clear();
    const bool coexist =
        drag_.native && config_.text("fancyzonesMode") == "coexist" && Down(VK_SHIFT);
    if (coexist) {
        overlay_.Hide();
        return;
    }
    if (!drag_.native && WantsZones())
        for (size_t i = 0; i < zones.size(); ++i)
            if (Contains(zones[i].rect, drag_.cursor.x, drag_.cursor.y)) {
                drag_.zone = static_cast<int>(i);
                break;
            }
    if (drag_.zone < 0 && config_.flag("edgeSnap") && !drag_.resize) {
        const int edge = MulDiv(config_.number("edgeDistance"), monitor.dpi, 96);
        const auto &r = monitor.work;
        const auto p = drag_.cursor;
        if (p.x < r.x + edge)
            drag_.edge = p.y < r.y + edge              ? "top-left"
                         : p.y > r.y + r.height - edge ? "bottom-left"
                                                       : "left";
        else if (p.x > r.x + r.width - edge)
            drag_.edge = p.y < r.y + edge              ? "top-right"
                         : p.y > r.y + r.height - edge ? "bottom-right"
                                                       : "right";
        else if (p.y < r.y + edge)
            drag_.edge = "maximize";
    }
    if (!config_.flag("showPreview")) {
        overlay_.Hide();
        return;
    }
    if (!drag_.edge.empty()) {
        Zone z = drag_.edge == "left"           ? Zone{0, 0, .5, 1, {}}
                 : drag_.edge == "right"        ? Zone{.5, 0, .5, 1, {}}
                 : drag_.edge == "top-left"     ? Zone{0, 0, .5, .5, {}}
                 : drag_.edge == "top-right"    ? Zone{.5, 0, .5, .5, {}}
                 : drag_.edge == "bottom-left"  ? Zone{0, .5, .5, .5, {}}
                 : drag_.edge == "bottom-right" ? Zone{.5, .5, .5, .5, {}}
                                                : Zone{0, 0, 1, 1, {}};
        overlay_.Show(
            monitor.work,
            {{ZoneRect(z, monitor.work, config_.number("outerGap"), config_.number("innerGap")),
              "Placer ici"}},
            config_, 0, false);
    } else if (drag_.zone >= 0 || (!drag_.native && WantsZones()))
        overlay_.Show(monitor.work, zones, config_, drag_.zone, false);
    else
        overlay_.Hide();
}
void Runtime::EndDrag() {
    if (!drag_.active)
        return;
    const auto drag = drag_;
    drag_ = {};
    overlay_.Hide();
    if (!Alive(drag.identity))
        return;
    if (drag.zone >= 0)
        Execute(drag.identity.window, "zone", drag.zone, "cursor");
    else if (!drag.edge.empty())
        Execute(drag.identity.window, drag.edge, -1, "cursor");
    else if (config_.flag("rememberOnMove"))
        Remember(drag.identity.window);
}
void Runtime::Tick() {
    if (!ready_ || stopping_)
        return;
    const auto now = GetTickCount64();
    if (animation_) {
        auto &a = *animation_;
        if (!Alive(a.identity)) {
            animation_.reset();
        } else if (a.waiting && WindowRect(a.identity.window) != a.requested) {
            if (now - a.started > static_cast<ULONGLONG>(a.duration + 1000)) {
                animation_.reset();
                Notice("Cette application ne répond pas au placement.");
            }
        } else {
            const double progress =
                             a.duration ? std::min(1.0, (now - a.started) /
                                                           static_cast<double>(a.duration))
                                        : 1.0,
                         eased = Ease(config_.text("motion"), progress);
            auto lerp = [&](int from, int to) {
                return static_cast<int>(std::lround(from + (to - from) * eased));
            };
            const Rect requested{lerp(a.from.x, a.to.x), lerp(a.from.y, a.to.y),
                                 lerp(a.from.width, a.to.width), lerp(a.from.height, a.to.height)};
            if (progress >= 1 && WindowRect(a.identity.window) == a.to) {
                const auto window = a.identity.window;
                animation_.reset();
                if (config_.flag("rememberOnMove"))
                    Remember(window);
            } else if (!SetWindowPos(a.identity.window, nullptr, requested.x, requested.y,
                                     requested.width, requested.height,
                                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS)) {
                animation_.reset();
                Notice("Windows a interrompu le placement.");
            } else {
                a.requested = requested;
                a.waiting = true;
            }
        }
    }
    if (drag_.active) {
        if (!Alive(drag_.identity)) {
            drag_ = {};
            overlay_.Hide();
        } else {
            if (drag_.native) {
                GetCursorPos(&drag_.cursor);
                PreviewDrag();
            } else if (drag_.changed &&
                       (!drag_.waiting || WindowRect(drag_.identity.window) == drag_.requested)) {
                drag_.changed = false;
                const int dx = drag_.cursor.x - drag_.start.x, dy = drag_.cursor.y - drag_.start.y;
                auto r = drag_.original;
                if (drag_.resize) {
                    if (drag_.left) {
                        r.x += dx;
                        r.width -= dx;
                    } else
                        r.width += dx;
                    if (drag_.top) {
                        r.y += dy;
                        r.height -= dy;
                    } else
                        r.height += dy;
                    if (r.width < 160) {
                        if (drag_.left)
                            r.x -= 160 - r.width;
                        r.width = 160;
                    }
                    if (r.height < 100) {
                        if (drag_.top)
                            r.y -= 100 - r.height;
                        r.height = 100;
                    }
                } else {
                    r.x += dx;
                    r.y += dy;
                }
                if (SetWindowPos(drag_.identity.window, nullptr, r.x, r.y, r.width, r.height,
                                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS)) {
                    drag_.requested = r;
                    drag_.waiting = true;
                }
                PreviewDrag();
            }
        }
    }
    for (auto it = opened_.begin(); it != opened_.end();) {
        if (now - it->second > 400) {
            const auto w = it->first;
            it = opened_.erase(it);
            Opened(w);
        } else
            ++it;
    }
    if (!config_.flag("enabled") && !owned_.empty()) {
        owned_.erase(std::remove_if(owned_.begin(), owned_.end(),
                                    [](const auto &s) {
                                        if (RestoreOwned(s)) {
                                            ReleaseOwner(s);
                                            return true;
                                        }
                                        return false;
                                    }),
                     owned_.end());
        SaveRecovery();
    }
    if (overlay_.Visible() && overlay_.Interactive() && !Alive(palette_target_))
        overlay_.Hide();
}
void Runtime::Menu() {
    const auto foreground = GetForegroundWindow();
    const auto target = Eligible(foreground)  ? foreground
                        : Alive(last_target_) ? last_target_.window
                                              : nullptr;
    HMENU menu = CreatePopupMenu();
    for (const auto &[id, title] : std::vector<std::pair<UINT, const wchar_t *>>{
             {1, L"Choisir une zone…"},
             {2, L"Centrer la fenêtre"},
             {3, L"Écran suivant"},
             {4, L"Garder au premier plan"},
             {5, L"Ranger dans la zone de notification"},
             {6, L"Plein écran réversible"},
             {7, L"Revenir au placement précédent"},
             {8, L"Retenir cette position"},
             {9, L"Retenir cette disposition pour ce bureau"}})
        AppendMenuW(menu, MF_STRING | (config_.flag("enabled") && Eligible(target) ? 0 : MF_GRAYED),
                    id, title);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    UINT id = 100;
    std::map<UINT, HWND> hidden;
    for (const auto &s : owned_)
        if (s.tray_owned && Alive(s.identity)) {
            wchar_t title[256]{};
            GetWindowTextW(s.identity.window, title, 256);
            AppendMenuW(menu, MF_STRING, id, (L"Retrouver : " + std::wstring(title)).c_str());
            hidden[id++] = s.identity.window;
        }
    AppendMenuW(menu, MF_STRING, 10, L"Retrouver toutes les fenêtres rangées");
    AppendMenuW(menu, MF_STRING, 11, L"Effacer les positions mémorisées");
    if (!hotkey_failures_.empty())
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"Raccourcis en conflit : voir les réglages");
    POINT p{};
    GetCursorPos(&p);
    SetForegroundWindow(window_);
    const auto chosen =
        TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, p.x, p.y, 0, window_, nullptr);
    DestroyMenu(menu);
    PostMessageW(window_, WM_NULL, 0, 0);
    if (chosen == 1)
        Palette(target);
    else if (chosen >= 2 && chosen <= 8) {
        const char *actions[] = {"center",     "next-monitor", "topmost", "tray",
                                 "fullscreen", "restore",      "remember"};
        Execute(target, actions[chosen - 2]);
    } else if (chosen == 9) {
        const auto desktop = Desktop(target);
        if (!desktop.empty()) {
            memory_["desktops"][desktop] = config_.text("layout");
            Persist();
            Notice("Disposition retenue pour ce bureau.");
        }
    } else if (chosen == 10) {
        for (auto &s : owned_)
            if (s.tray_owned && Alive(s.identity)) {
                ShowWindowAsync(s.identity.window, SW_SHOWNOACTIVATE);
                s.tray_owned = false;
            }
        SaveRecovery();
    } else if (chosen == 11) {
        memory_["placements"] = Json::object();
        memory_["desktops"] = Json::object();
        Persist();
        Notice("Positions mémorisées effacées.");
    } else if (hidden.contains(chosen))
        Execute(hidden.at(chosen), "restore-tray");
}
void Runtime::Cleanup() {
    if (cleaned_)
        return;
    cleaned_ = true;
    stopping_ = true;
    SetEvent(stop_event_);
    SetEvent(frame_done_);
    KillTimer(window_, 1);
    overlay_.Hide();
    animation_.reset();
    if (reader_.joinable()) {
        CancelSynchronousIo(static_cast<HANDLE>(reader_.native_handle()));
        reader_.join();
    }
    if (mouse_hook_)
        UnhookWindowsHookEx(mouse_hook_);
    if (keyboard_hook_)
        UnhookWindowsHookEx(keyboard_hook_);
    mouse_hook_ = keyboard_hook_ = nullptr;
    for (auto event : events_)
        UnhookWinEvent(event);
    events_.clear();
    for (const auto &[id, action] : hotkeys_) {
        (void)action;
        UnregisterHotKey(window_, id);
    }
    hotkeys_.clear();
    owned_.erase(std::remove_if(owned_.begin(), owned_.end(),
                                [](const auto &s) {
                                    if (RestoreOwned(s)) {
                                        ReleaseOwner(s);
                                        return true;
                                    }
                                    return false;
                                }),
                 owned_.end());
    try {
        SaveRecovery();
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
    }
    if (tray_added_) {
        NOTIFYICONDATAW icon{sizeof(icon)};
        icon.hWnd = window_;
        icon.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &icon);
        tray_added_ = false;
    }
    if (guardian_) {
        CloseHandle(guardian_);
        guardian_ = nullptr;
    }
    if (mutex_) {
        ReleaseMutex(mutex_);
        CloseHandle(mutex_);
        mutex_ = nullptr;
    }
}
} // namespace wm
