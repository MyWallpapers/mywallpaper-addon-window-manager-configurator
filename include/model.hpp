#pragma once
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
namespace wm {
using Json = nlohmann::json;
struct Rect {
    int x{}, y{}, width{}, height{};
    bool operator==(const Rect &) const = default;
};
struct Zone {
    double x{}, y{}, width{1}, height{1};
    std::string name;
};
struct Color {
    uint8_t r{}, g{}, b{}, a{255};
};
struct Config {
    Json values;
    std::vector<Zone> custom_zones;
    bool flag(const char *key) const {
        return values.at(key).get<bool>();
    }
    int number(const char *key) const {
        return values.at(key).get<int>();
    }
    std::string text(const char *key) const {
        return values.at(key).get<std::string>();
    }
};
const Json &Manifest();
Config ParseConfig(const Json &settings);
Color ParseColor(const std::string &value);
std::vector<Zone> Layout(const Config &config, const std::string &layout = {});
Rect ZoneRect(const Zone &zone, const Rect &work, int outer_gap, int inner_gap);
Rect CenterRect(const Config &config, const Rect &work, const Rect &current);
Rect MapToMonitor(const Rect &window, const Rect &from, const Rect &to);
bool Contains(const Rect &rect, int x, int y);
bool MatchesApp(const std::string &list, const std::string &image);
double Ease(const std::string &mode, double progress);
} // namespace wm
