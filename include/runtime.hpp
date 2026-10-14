#pragma once
#include "overlay.hpp"
#include <atomic>
#include <map>
#include <optional>
#include <set>
#include <shobjidl.h>
#include <thread>
#include <wrl/client.h>
namespace wm {
class Runtime {
  public:
    explicit Runtime(DWORD fixture_pid = 0);
    ~Runtime();
    int Run();

  private:
    struct Monitor {
        HMONITOR handle{};
        Rect work, screen;
        std::string device;
        bool primary{};
        int dpi{96};
    };
    struct Previous {
        Identity identity;
        WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
        Rect rect;
    };
    struct Animation {
        Identity identity;
        Rect from, to;
        ULONGLONG started{};
        int duration{};
        Rect requested;
        bool waiting{};
    };
    struct Drag {
        Identity identity;
        POINT start{}, cursor{};
        Rect original;
        bool resize{}, left{}, top{}, active{}, native{}, changed{};
        int zone{-1};
        std::string edge;
        Rect requested;
        bool waiting{};
    };
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK Mouse(int, WPARAM, LPARAM);
    static LRESULT CALLBACK Keyboard(int, WPARAM, LPARAM);
    static void CALLBACK Event(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);
    void Frame(const Json &frame);
    void ReadFrames();
    void Tick();
    void Settings(Config config);
    void BindHotkeys();
    void Cleanup();
    void Reply(const Json &value);
    void Notice(const std::string &text);
    std::string Image(HWND window) const;
    bool Eligible(HWND window, bool allow_fullscreen = false, bool fast = false) const;
    bool Fullscreen(HWND window) const;
    std::string Desktop(HWND window) const;
    std::string MemoryKey(HWND window) const;
    std::vector<Monitor> Monitors() const;
    Monitor MonitorFor(HWND window, const std::string &choice = {}) const;
    std::vector<OverlayZone> Zones(HWND window, const Monitor &monitor) const;
    HWND Target(const Json &payload) const;
    void Palette(HWND window);
    void Choose(int zone);
    void Menu();
    bool Execute(HWND window, const std::string &action, int zone = -1,
                 const std::string &monitor_choice = {});
    void Place(HWND window, const Rect &rect, bool animate = true);
    void Remember(HWND window);
    void Opened(HWND window);
    void Persist();
    void SaveRecovery();
    OwnedState &Own(HWND window);
    void EndDrag();
    bool Modifier() const;
    bool WantsZones() const;
    void PreviewDrag();
    HWND window_{};
    DWORD fixture_pid_{};
    bool ready_{}, stopping_{}, cleaned_{}, contained_{};
    std::atomic<bool> failed_{};
    HANDLE mutex_{}, guardian_{}, stop_event_{}, frame_done_{};
    std::thread reader_;
    HHOOK mouse_hook_{}, keyboard_hook_{};
    std::vector<HWINEVENTHOOK> events_;
    Config config_;
    Overlay overlay_;
    Microsoft::WRL::ComPtr<IVirtualDesktopManager> desktops_;
    std::filesystem::path recovery_path_, memory_path_, scratch_;
    Json memory_ = {{"version", 1}, {"placements", Json::object()}, {"desktops", Json::object()}};
    std::vector<OwnedState> owned_;
    std::map<HWND, Previous> previous_;
    std::optional<Animation> animation_;
    Drag drag_;
    Identity palette_target_;
    std::map<int, std::pair<std::string, int>> hotkeys_;
    std::vector<std::string> hotkey_failures_;
    std::map<HWND, ULONGLONG> opened_;
    std::map<HWND, Identity> seen_;
    HWND last_foreground_{};
    Identity last_target_;
    std::map<std::string, Rect> work_areas_;
    UINT taskbar_created_{};
    bool tray_added_{};
    std::string last_notice_;
    uint64_t placements_{};
    std::wstring owner_property_;
    uintptr_t next_cookie_{1};
    static Runtime *active_;
};
} // namespace wm
