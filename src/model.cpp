#include "model.hpp"
#include "manifest_data.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>
namespace wm {
const Json &Manifest() {
    static const Json data = Json::parse(kManifest);
    return data;
}
Color ParseColor(const std::string &value) {
    if ((value.size() != 7 && value.size() != 9) || value[0] != '#')
        throw std::invalid_argument("Couleur attendue : #RRGGBB ou #RRGGBBAA");
    for (size_t i = 1; i < value.size(); ++i)
        if (!std::isxdigit(static_cast<unsigned char>(value[i])))
            throw std::invalid_argument("Couleur invalide");
    auto byte = [&](size_t i) {
        return static_cast<uint8_t>(std::stoi(value.substr(i, 2), nullptr, 16));
    };
    return {byte(1), byte(3), byte(5), value.size() == 9 ? byte(7) : uint8_t{255}};
}
Config ParseConfig(const Json &input) {
    if (!input.is_object())
        throw std::invalid_argument("Les réglages doivent former un objet");
    Config result;
    for (const auto &definition : Manifest().at("settings")) {
        const auto type = definition.at("type").get<std::string>();
        if (type == "section" || type == "button")
            continue;
        const auto id = definition.at("id").get<std::string>();
        const Json value = input.contains(id) ? input.at(id) : definition.at("default");
        bool valid = true;
        if (type == "boolean")
            valid = value.is_boolean();
        else if (type == "range")
            valid = value.is_number() && std::isfinite(value.get<double>()) &&
                    value.get<double>() >= definition.at("min").get<double>() &&
                    value.get<double>() <= definition.at("max").get<double>() &&
                    std::floor(value.get<double>()) == value.get<double>();
        else
            valid = value.is_string() && value.get_ref<const std::string &>().size() <= 32768;
        if (!valid)
            throw std::invalid_argument("Valeur invalide : " + id);
        if (type == "color")
            (void)ParseColor(value.get<std::string>());
        if (type == "select") {
            valid = false;
            for (const auto &option : definition.at("options"))
                if (option.at("value") == value)
                    valid = true;
            if (!valid)
                throw std::invalid_argument("Choix inconnu : " + id);
        }
        result.values[id] = value;
    }
    if (result.text("profile") != "custom") {
        const auto palette = Json::parse(kPalettes).at(result.text("profile"));
        for (auto it = palette.begin(); it != palette.end(); ++it)
            result.values[it.key()] = it.value();
    }
    if (result.flag("advancedEnabled") && !result.text("advancedZones").empty()) {
        const auto zones = Json::parse(result.text("advancedZones"));
        if (!zones.is_array() || zones.empty() || zones.size() > 12)
            throw std::invalid_argument("Choisissez entre une et douze zones");
        for (const auto &value : zones) {
            Zone z;
            if (!value.is_object())
                throw std::invalid_argument("Zone invalide");
            for (const auto *key : {"x", "y", "width", "height"})
                if (!value.contains(key) || !value.at(key).is_number())
                    throw std::invalid_argument("Une zone doit avoir x, y, width et height");
            z.x = value.at("x");
            z.y = value.at("y");
            z.width = value.at("width");
            z.height = value.at("height");
            if (!std::isfinite(z.x) || !std::isfinite(z.y) || !std::isfinite(z.width) ||
                !std::isfinite(z.height) || z.x < 0 || z.y < 0 || z.width <= 0 || z.height <= 0 ||
                z.x + z.width > 1.000001 || z.y + z.height > 1.000001)
                throw std::invalid_argument("Une zone doit rester à l’intérieur de l’écran");
            z.name = value.value(
                "name", std::string{"Zone " + std::to_string(result.custom_zones.size() + 1)});
            if (z.name.size() > 128)
                throw std::invalid_argument("Nom de zone trop long");
            result.custom_zones.push_back(z);
        }
    }
    return result;
}
std::vector<Zone> Layout(const Config &c, const std::string &override_layout) {
    if (!c.custom_zones.empty())
        return c.custom_zones;
    const auto layout = override_layout.empty() ? c.text("layout") : override_layout;
    if (layout == "duo") {
        const auto split = c.number("splitShare") / 100.0;
        return {{0, 0, split, 1, "À gauche"}, {split, 0, 1 - split, 1, "À droite"}};
    }
    if (layout == "focus") {
        const auto split = c.number("mainShare") / 100.0;
        return {{0, 0, split, 1, "Espace principal"},
                {split, 0, 1 - split, .5, "Outil du haut"},
                {split, .5, 1 - split, .5, "Outil du bas"}};
    }
    const int cols = layout == "quad"      ? 2
                     : layout == "columns" ? c.number("columns")
                                           : c.number("gridColumns");
    const int rows = layout == "quad" ? 2 : layout == "columns" ? 1 : c.number("gridRows");
    std::vector<Zone> result;
    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col)
            result.push_back({col / static_cast<double>(cols), row / static_cast<double>(rows),
                              1.0 / cols, 1.0 / rows, "Zone " + std::to_string(result.size() + 1)});
    return result;
}
Rect ZoneRect(const Zone &z, const Rect &work, int outer, int inner) {
    const int x1 = static_cast<int>(std::lround(work.x + work.width * z.x));
    const int y1 = static_cast<int>(std::lround(work.y + work.height * z.y));
    const int x2 = static_cast<int>(std::lround(work.x + work.width * (z.x + z.width)));
    const int y2 = static_cast<int>(std::lround(work.y + work.height * (z.y + z.height)));
    const int left = z.x < .00001 ? outer : inner / 2;
    const int top = z.y < .00001 ? outer : inner / 2;
    const int right = z.x + z.width > .99999 ? outer : inner - inner / 2;
    const int bottom = z.y + z.height > .99999 ? outer : inner - inner / 2;
    return {x1 + left, y1 + top, std::max(1, x2 - x1 - left - right),
            std::max(1, y2 - y1 - top - bottom)};
}
Rect CenterRect(const Config &c, const Rect &work, const Rect &current) {
    double width = current.width, height = current.height;
    const auto size = c.text("centerSize");
    if (size != "keep") {
        double ratio = size == "compact" ? .5 : size == "comfortable" ? .7 : .88;
        width = work.width * (size == "custom" ? c.number("centerWidth") / 100.0 : ratio);
        height = work.height * (size == "custom" ? c.number("centerHeight") / 100.0 : ratio);
    }
    const int w = std::clamp(static_cast<int>(width), 1, work.width),
              h = std::clamp(static_cast<int>(height), 1, work.height);
    return {work.x + (work.width - w) / 2, work.y + (work.height - h) / 2, w, h};
}
Rect MapToMonitor(const Rect &r, const Rect &from, const Rect &to) {
    const int width =
        std::clamp(static_cast<int>(std::lround(r.width * static_cast<double>(to.width) /
                                                std::max(1, from.width))),
                   1, to.width);
    const int height =
        std::clamp(static_cast<int>(std::lround(r.height * static_cast<double>(to.height) /
                                                std::max(1, from.height))),
                   1, to.height);
    const int x =
        to.x +
        std::clamp(static_cast<int>(std::lround((r.x - from.x) * static_cast<double>(to.width) /
                                                std::max(1, from.width))),
                   0, to.width - width);
    const int y =
        to.y +
        std::clamp(static_cast<int>(std::lround((r.y - from.y) * static_cast<double>(to.height) /
                                                std::max(1, from.height))),
                   0, to.height - height);
    return {x, y, width, height};
}
bool Contains(const Rect &r, int x, int y) {
    return x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height;
}
bool MatchesApp(const std::string &list, const std::string &image) {
    auto lower = [](std::string s) {
        for (auto &c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const auto wanted = lower(image);
    std::string item;
    auto matches = [&] {
        const auto first = item.find_first_not_of(" \t\r");
        if (first == std::string::npos)
            return false;
        auto name = lower(item.substr(first, item.find_last_not_of(" \t\r") - first + 1));
        if (!name.ends_with(".exe"))
            name += ".exe";
        return name == wanted;
    };
    for (char c : list) {
        if (c == ',' || c == ';' || c == '\n' || c == '|') {
            if (matches())
                return true;
            item.clear();
        } else
            item += c;
    }
    return matches();
}
double Ease(const std::string &mode, double t) {
    t = std::clamp(t, 0.0, 1.0);
    if (mode == "instant")
        return 1;
    if (mode == "spring")
        return std::clamp(1 - std::exp(-8 * t) * std::cos(8 * t), 0.0, 1.0);
    return 1 - std::pow(1 - t, 3);
}
} // namespace wm
