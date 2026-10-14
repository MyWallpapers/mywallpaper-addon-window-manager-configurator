#include "model.hpp"
#include "wire.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
static void Check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> static void Reject(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    Check(rejected, "Expected rejection");
}
int main() {
    try {
        auto c = wm::ParseConfig(wm::Json::object());
        Check(c.text("layout") == "duo", "Default layout");
        Check(wm::Manifest().at("settings").size() <= 100, "Settings limit");
        auto color = wm::ParseColor("#12345678");
        Check(color.r == 0x12 && color.g == 0x34 && color.b == 0x56 && color.a == 0x78,
              "Alpha last");
        Reject([] { wm::ParseColor("#QQQQQQ"); });
        Reject([] { wm::ParseConfig({{"innerGap", -1}}); });
        Reject([] { wm::ParseConfig({{"gridRows", 1.5}}); });
        Reject([] { wm::ParseConfig({{"enabled", 1}}); });
        Reject([] { wm::ParseConfig({{"layout", "no-such-layout"}}); });
        auto custom = wm::ParseConfig({{"profile", "custom"}, {"startColor", "#12345678"}});
        Check(custom.text("startColor") == "#12345678", "Custom palette");
        auto preset = wm::ParseConfig({{"profile", "paper"}, {"startColor", "#12345678"}});
        Check(preset.text("startColor") == "#F6EBD4FF", "Profile defaults");
        for (const auto &p : wm::Manifest().at("presets")) {
            const auto config = wm::ParseConfig(p.at("settings"));
            Check(!wm::Layout(config).empty(), "Profile layout");
        }
        auto zones = wm::Layout(c);
        Check(zones.size() == 2, "Duo layout");
        const wm::Rect work{-1920, -200, 1920, 1080};
        const auto left = wm::ZoneRect(zones[0], work, 12, 12),
                   right = wm::ZoneRect(zones[1], work, 12, 12);
        Check(left.x == work.x + 12 && left.y == work.y + 12, "Negative origin");
        Check(right.x - left.x - left.width == 12, "Single shared gap");
        Check(right.x + right.width == work.x + work.width - 12, "Right margin");
        for (int width = 101; width < 2100; width += 7) {
            const wm::Rect r{-500, 20, width, 721};
            const auto a = wm::ZoneRect(zones[0], r, 3, 13), b = wm::ZoneRect(zones[1], r, 3, 13);
            Check(b.x - a.x - a.width == 13, "Odd gap distribution");
        }
        auto grid = wm::ParseConfig({{"layout", "grid"}, {"gridColumns", 6}, {"gridRows", 4}});
        Check(wm::Layout(grid).size() == 24, "Custom grid");
        auto focus = wm::ParseConfig({{"layout", "focus"}, {"mainShare", 64}});
        Check(wm::Layout(focus).size() == 3 && wm::Layout(focus)[0].width == .64, "Focus layout");
        const auto mapped = wm::MapToMonitor({-1900, -100, 700, 600}, work, {0, 0, 2560, 1400});
        Check(mapped.x >= 0 && mapped.y >= 0 && mapped.x + mapped.width <= 2560,
              "Monitor transfer");
        const auto center = wm::CenterRect(c, work, {-2500, -400, 2200, 1800});
        Check(center.width == work.width && center.height == work.height,
              "Oversized window center clamps");
        Check(wm::MatchesApp(" Code; notepad.exe , calc ", "NOTEPAD.EXE"),
              "Case-insensitive names");
        Check(!wm::MatchesApp("notepad", "notepad++.exe"), "No broad matching");
        auto advanced = wm::ParseConfig(
            {{"advancedEnabled", true},
             {"advancedZones",
              "[{\"x\":0,\"y\":0,\"width\":0.4,\"height\":1,\"name\":\"Lecture\"}]"}});
        Check(wm::Layout(advanced).size() == 1, "Advanced is optional");
        Reject([] {
            wm::ParseConfig(
                {{"advancedEnabled", true},
                 {"advancedZones", "[{\"x\":0.9,\"y\":0,\"width\":0.4,\"height\":1}]"}});
        });
        Check(wm::Ease("smooth", 0) == 0 && wm::Ease("smooth", 1) == 1, "Motion endpoints");
        for (int i = 0; i <= 100; ++i)
            Check(wm::Ease("spring", i / 100.0) >= 0 && wm::Ease("spring", i / 100.0) <= 1,
                  "Bounded spring");
        std::stringstream wire;
        wm::Json large = {{"payload", std::string(2 * wm::wire::max_chunk + 21, 'x')}};
        wm::wire::Write(wire, large);
        auto decoded = wm::wire::Read(wire);
        Check(decoded && *decoded == large, "Ordered multi-chunk roundtrip");
        Check(!wm::wire::Read(wire), "Clean EOF");
        Reject([] {
            std::stringstream in(
                std::string{char(1), char(0), char(0), static_cast<char>(-128), 'x'});
            wm::wire::Read(in);
        });
        Reject([] {
            std::stringstream in("abc");
            wm::wire::Read(in);
        });
        std::cout << "Model and process-v2 contracts passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
