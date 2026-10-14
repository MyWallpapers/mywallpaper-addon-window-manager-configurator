#include "overlay.hpp"
#include <algorithm>
#include <objidl.h> // GDI+ requires COM declarations before gdiplus.h.
#include <gdiplus.h>
#include <iostream>
#include <stdexcept>
namespace wm {
using namespace Gdiplus;
static Gdiplus::Color NativeColor(Color c, int opacity = 100) {
    return {static_cast<BYTE>(static_cast<int>(c.a) * opacity / 100), c.r, c.g, c.b};
}
static void Rounded(GraphicsPath &p, REAL x, REAL y, REAL width, REAL height, REAL radius) {
    radius = std::min({radius, width / 2, height / 2});
    if (radius <= 0) {
        p.AddRectangle(RectF(x, y, width, height));
        return;
    }
    const REAL d = radius * 2;
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + width - d, y, d, d, 270, 90);
    p.AddArc(x + width - d, y + height - d, d, d, 0, 90);
    p.AddArc(x, y + height - d, d, d, 90, 90);
    p.CloseFigure();
}
Overlay::Overlay(std::function<void(int)> choose)
    : config_(ParseConfig(Json::object())), choose_(std::move(choose)) {
    WNDCLASSW cls{};
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpfnWndProc = Proc;
    cls.lpszClassName = L"MWWindowPlacementOverlay";
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&cls);
    window_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_TOPMOST,
        cls.lpszClassName,
        L"Zones de placement", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, cls.hInstance, this);
    if (!window_)
        throw std::runtime_error("Impossible de créer l’aperçu des zones");
}
Overlay::~Overlay() {
    if (window_)
        DestroyWindow(window_);
}
LRESULT CALLBACK Overlay::Proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto self = reinterpret_cast<Overlay *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<Overlay *>(reinterpret_cast<CREATESTRUCTW *>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self && message == WM_MOUSEACTIVATE)
        return MA_NOACTIVATE;
    if (self && (message == WM_MOUSEMOVE || message == WM_LBUTTONUP) && self->interactive_) {
        POINT cursor{};
        GetCursorPos(&cursor);
        int index = -1;
        for (size_t i = 0; i < self->zones_.size(); ++i)
            if (Contains(self->zones_[i].rect, cursor.x, cursor.y)) {
                index = static_cast<int>(i);
                break;
            }
        if (message == WM_MOUSEMOVE)
            self->Select(index);
        else if (index >= 0)
            self->choose_(index);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
void Overlay::Show(const Rect &work, const std::vector<OverlayZone> &zones, const Config &config,
                   int selected, bool interactive) {
    if (visible_ && work_ == work && zones_ == zones && config_.values == config.values &&
        selected_ == selected && interactive_ == interactive)
        return;
    work_ = work;
    zones_ = zones;
    config_ = config;
    selected_ = selected;
    interactive_ = interactive;
    visible_ = true;
    // Preserve Windows' z-order bits when changing click-through behavior.
    const auto ex = GetWindowLongPtrW(window_, GWL_EXSTYLE);
    SetWindowLongPtrW(window_, GWL_EXSTYLE,
                      interactive ? ex & ~WS_EX_TRANSPARENT : ex | WS_EX_TRANSPARENT);
    Render();
    if (!SetWindowPos(window_, HWND_TOPMOST, work.x, work.y, work.width, work.height,
                      SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW))
        throw std::runtime_error("Windows a refusé le placement de l’aperçu");
}
void Overlay::Hide() {
    visible_ = false;
    interactive_ = false;
    ShowWindow(window_, SW_HIDE);
}
void Overlay::Select(int index) {
    if (index != selected_) {
        selected_ = index;
        if (visible_)
            Render();
    }
}
void Overlay::SavePreview(const Rect &work, const std::vector<OverlayZone> &zones,
                          const Config &config, int selected, const std::filesystem::path &file) {
    Hide();
    work_ = work;
    zones_ = zones;
    config_ = config;
    selected_ = selected;
    Render(&file);
}
void Overlay::Render(const std::filesystem::path *file) {
    if (work_.width <= 0 || work_.height <= 0 ||
        uint64_t{static_cast<uint32_t>(work_.width)} * static_cast<uint32_t>(work_.height) >
            64000000)
        throw std::runtime_error("Écran trop grand pour l’aperçu");
    const HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = work_.width;
    info.bmiHeader.biHeight = -work_.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *pixels = nullptr;
    const HBITMAP dib = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!dib || !memory) {
        if (dib)
            DeleteObject(dib);
        if (memory)
            DeleteDC(memory);
        ReleaseDC(nullptr, screen);
        throw std::runtime_error("Mémoire graphique indisponible");
    }
    const auto old = SelectObject(memory, dib);
    {
        Bitmap bitmap(work_.width, work_.height, work_.width * 4, PixelFormat32bppPARGB,
                      static_cast<BYTE *>(pixels));
        Graphics g(&bitmap);
        g.SetCompositingMode(CompositingModeSourceCopy);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
        g.SetCompositingMode(CompositingModeSourceOver);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        const auto start = ParseColor(config_.text("startColor")),
                   end = ParseColor(config_.text("endColor")),
                   active = ParseColor(config_.text("activeColor")),
                   text = ParseColor(config_.text("textColor"));
        const REAL scale = static_cast<REAL>(GetDpiForWindow(window_)) / 96.0f;
        for (size_t i = 0; i < zones_.size(); ++i) {
            const auto &r = zones_[i].rect;
            const REAL x = static_cast<REAL>(r.x - work_.x), y = static_cast<REAL>(r.y - work_.y),
                       w = static_cast<REAL>(r.width), h = static_cast<REAL>(r.height);
            const bool chosen = static_cast<int>(i) == selected_;
            const int opacity = config_.number(chosen ? "activeOpacity" : "opacity");
            const REAL radius = config_.number("cornerRadius") * scale;
            const int depth = config_.number("shadowDepth");
            for (int ring = depth; ring > 0; ring -= 2) {
                GraphicsPath shadow;
                Rounded(shadow, x - ring / 2.0f, y + depth / 3.0f - ring / 2.0f, w + ring, h + ring,
                        radius + ring / 2.0f);
                SolidBrush brush(Gdiplus::Color(3, 0, 0, 0));
                g.FillPath(&brush, &shadow);
            }
            GraphicsPath shape;
            Rounded(shape, x, y, w, h, radius);
            const auto mode = config_.text("surfaceMode");
            if (mode == "gradient") {
                const auto direction = config_.text("gradientDirection");
                LinearGradientBrush brush(RectF(x, y, std::max(1.0f, w), std::max(1.0f, h)),
                                          NativeColor(start, opacity), NativeColor(end, opacity),
                                          direction == "horizontal" ? LinearGradientModeHorizontal
                                          : direction == "vertical"
                                              ? LinearGradientModeVertical
                                              : LinearGradientModeForwardDiagonal);
                g.FillPath(&brush, &shape);
            } else {
                SolidBrush brush(
                    NativeColor(start, mode == "translucent" ? std::min(opacity, 60) : opacity));
                g.FillPath(&brush, &shape);
            }
            if (chosen) {
                SolidBrush tint(NativeColor(active, 12));
                g.FillPath(&tint, &shape);
            }
            const REAL border = config_.number("borderWidth") * scale;
            if (border > 0) {
                Pen pen(NativeColor(chosen ? active : text, chosen ? 100 : 70), border);
                pen.SetAlignment(PenAlignmentInset);
                g.DrawPath(&pen, &shape);
            }
            if (config_.flag("zoneLabels") && w > 80 && h > 60) {
                const REAL labelw = std::min(w - 20, 240.0f * scale),
                           labelh = std::min(h - 12, 82.0f * scale);
                const REAL lx = x + (w - labelw) / 2, ly = y + (h - labelh) / 2;
                GraphicsPath plate;
                Rounded(plate, lx, ly, labelw, labelh, 8 * scale);
                const bool light_text = (text.r * 299 + text.g * 587 + text.b * 114) > 128000;
                SolidBrush platebrush(light_text ? Gdiplus::Color(255, 13, 23, 35)
                                                 : Gdiplus::Color(255, 255, 248, 235));
                g.FillPath(&platebrush, &plate);
                SolidBrush letters(NativeColor(text));
                FontFamily family(L"Segoe UI");
                Font number(&family, 24 * scale, FontStyleBold, UnitPixel),
                    name(&family, 14 * scale, FontStyleRegular, UnitPixel);
                StringFormat format;
                format.SetAlignment(StringAlignmentCenter);
                format.SetLineAlignment(StringAlignmentCenter);
                format.SetTrimming(StringTrimmingEllipsisCharacter);
                const auto n = Wide(std::to_string(i + 1));
                g.DrawString(n.c_str(), -1, &number, RectF(lx, ly + 4 * scale, labelw, 34 * scale),
                             &format, &letters);
                const auto label = Wide(zones_[i].name);
                g.DrawString(label.c_str(), -1, &name,
                             RectF(lx + 8 * scale, ly + 38 * scale, labelw - 16 * scale,
                                   labelh - 40 * scale),
                             &format, &letters);
            }
        }
        if (file) {
            UINT count = 0, size = 0;
            GetImageEncodersSize(&count, &size);
            std::vector<BYTE> bytes(size);
            auto encoders = reinterpret_cast<ImageCodecInfo *>(bytes.data());
            GetImageEncoders(count, size, encoders);
            bool saved = false;
            for (UINT i = 0; i < count; ++i)
                if (wcscmp(encoders[i].MimeType, L"image/png") == 0) {
                    saved = bitmap.Save(file->c_str(), &encoders[i].Clsid, nullptr) == Ok;
                    break;
                }
            if (!saved) {
                SelectObject(memory, old);
                DeleteObject(dib);
                DeleteDC(memory);
                ReleaseDC(nullptr, screen);
                throw std::runtime_error("Export PNG impossible");
            }
        }
    }
    if (!file) {
        POINT position{work_.x, work_.y}, source{};
        SIZE size{work_.width, work_.height};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        if (!UpdateLayeredWindow(window_, screen, &position, &size, memory, &source, 0, &blend,
                                 ULW_ALPHA))
            std::cerr << "Aperçu Windows refusé : " << GetLastError() << '\n';
    }
    SelectObject(memory, old);
    DeleteObject(dib);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
}
} // namespace wm
