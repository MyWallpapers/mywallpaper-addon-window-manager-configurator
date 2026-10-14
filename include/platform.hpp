#pragma once
#include "model.hpp"
#include <filesystem>
#include <windows.h>
namespace wm {
std::wstring Wide(const std::string &text);
std::string Utf8(const std::wstring &text);
Rect WindowRect(HWND window);
RECT NativeRect(const Rect &value);
struct Identity {
    HWND window{};
    DWORD pid{};
    uint64_t created{};
    bool operator==(const Identity &) const = default;
};
Identity Identify(HWND window);
bool Alive(const Identity &identity);
struct OwnedState {
    Identity identity;
    LONG_PTR style{}, ex_style{}, expected_style{}, expected_ex_style{};
    Rect before_fullscreen;
    bool topmost_owned{}, expected_topmost{}, fullscreen_owned{}, tray_owned{};
    std::wstring property;
    uintptr_t cookie{};
    bool tray_minimized{};
};
void AtomicJson(const std::filesystem::path &path, const Json &value);
void WriteRecovery(const std::filesystem::path &path, const std::vector<OwnedState> &values);
bool RestoreOwned(const OwnedState &state);
void ReleaseOwner(const OwnedState &state);
int Guardian(DWORD owner, uint64_t created, const std::filesystem::path &path);
HANDLE StartGuardian(const std::filesystem::path &path);
void RecoverPending(const std::filesystem::path &directory);
} // namespace wm
