#pragma once
#include "platform.hpp"
#include <functional>
namespace wm {
struct OverlayZone {
    Rect rect;
    std::string name;
    bool operator==(const OverlayZone &) const = default;
};
class Overlay {
  public:
    explicit Overlay(std::function<void(int)> choose);
    ~Overlay();
    void Show(const Rect &work, const std::vector<OverlayZone> &zones, const Config &config,
              int selected, bool interactive);
    void Hide();
    void Select(int index);
    bool Visible() const {
        return visible_;
    }
    bool Interactive() const {
        return interactive_;
    }
    int Selected() const {
        return selected_;
    }
    size_t Count() const {
        return zones_.size();
    }
    void SavePreview(const Rect &work, const std::vector<OverlayZone> &zones, const Config &config,
                     int selected, const std::filesystem::path &file);

  private:
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    void Render(const std::filesystem::path *file = nullptr);
    HWND window_{};
    Rect work_;
    std::vector<OverlayZone> zones_;
    Config config_;
    int selected_{-1};
    bool visible_{}, interactive_{};
    std::function<void(int)> choose_;
};
} // namespace wm
