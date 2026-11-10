#pragma once

#include <array>
#include <cctype>
#include <string_view>

namespace jellyframe::css_background_detail {
inline std::string_view trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.remove_suffix(1);
    return value;
}

template<class Visitor>
bool visit_list(std::string_view value, Visitor visitor) {
    int depth = 0;
    char quote = 0;
    bool escaped = false;
    std::size_t start = 0, count = 0;
    for (std::size_t i = 0; i <= value.size(); ++i) {
        const char c = i == value.size() ? ',' : value[i];
        if (quote) {
            if (i == value.size()) return false;
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '\'' || c == '"') { quote = c; continue; }
        if (c == '(') ++depth;
        else if (c == ')') { if (--depth < 0) return false; }
        else if (c == ',' && depth == 0) {
            const auto item = trim(value.substr(start, i - start));
            if (item.empty() || !visitor(item, count++)) return false;
            start = i + 1;
        }
    }
    return depth == 0 && !quote && count != 0;
}

struct ImageListSyntax {
    std::array<std::string_view, 4> images{};
    std::size_t count = 0;
    std::string_view color;
};

template<class ImageValidator, class ColorValidator>
bool parse_image_list(std::string_view value, bool shorthand, ImageValidator image_valid,
                      ColorValidator color_valid, ImageListSyntax& output) {
    ImageListSyntax candidate;
    bool color_seen = false;
    const bool valid = visit_list(value, [&](std::string_view item, std::size_t index) {
        if (index >= candidate.images.size() || color_seen) return false;
        if (image_valid(item)) candidate.images[index] = item;
        else if (shorthand && color_valid(item)) {
            candidate.images[index] = "none";
            candidate.color = item;
            color_seen = true;
        } else {
            if (!shorthand) return false;
            int depth = 0;
            char quote = 0;
            bool found = false;
            for (std::size_t i = 0; i < item.size(); ++i) {
                const char c = item[i];
                if (quote) { if (c == quote && (i == 0 || item[i - 1] != '\\')) quote = 0; continue; }
                if (c == '\'' || c == '"') quote = c;
                else if (c == '(') ++depth;
                else if (c == ')') --depth;
                else if (depth == 0 && std::isspace(static_cast<unsigned char>(c))) {
                    const auto first = trim(item.substr(0, i));
                    const auto second = trim(item.substr(i + 1));
                    const bool first_image = image_valid(first), second_image = image_valid(second);
                    if ((first_image && color_valid(second)) || (color_valid(first) && second_image)) {
                        candidate.images[index] = first_image ? first : second;
                        candidate.color = first_image ? second : first;
                        color_seen = found = true;
                        break;
                    }
                }
            }
            if (!found) return false;
        }
        ++candidate.count;
        return true;
    });
    if (!valid) return false;
    output = candidate;
    return true;
}
} // namespace jellyframe::css_background_detail
