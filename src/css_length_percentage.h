#pragma once

#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>

namespace jellyframe::detail {

struct ParsedLengthPercentage { double length_pixels = 0; double percentage = 0; };

inline std::string_view trim_length_percentage(std::string_view value) {
    const auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f'; };
    while (!value.empty() && ws(value.front())) value.remove_prefix(1);
    while (!value.empty() && ws(value.back())) value.remove_suffix(1);
    return value;
}

inline bool parse_length_percentage_term(std::string_view token, int font_size, int viewport_width,
                                        int viewport_height, double& length, double& percentage) {
    token = trim_length_percentage(token);
    if (token.empty()) return false;
    const std::string text(token);
    char* end = nullptr;
    const double number = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || !std::isfinite(number)) return false;
    const std::string_view unit(end, text.c_str() + text.size() - end);
    if (unit == "%") { percentage = number; return true; }
    double scale = 0;
    if (unit.empty() || unit == "px") scale = 1;
    else if (unit == "em") scale = font_size;
    else if (unit == "rem") scale = 16;
    else if (unit == "pc") scale = 16;
    else if (unit == "pt") scale = 96.0 / 72.0;
    else if (unit == "in") scale = 96;
    else if (unit == "cm") scale = 96.0 / 2.54;
    else if (unit == "mm") scale = 96.0 / 25.4;
    else if (unit == "q") scale = 96.0 / 101.6;
    else if (unit == "vw") scale = viewport_width / 100.0;
    else if (unit == "vh") scale = viewport_height / 100.0;
    else return false;
    length = number * scale;
    return std::isfinite(length);
}

inline bool parse_length_percentage_calc(std::string_view input, ParsedLengthPercentage& output,
                                        int font_size = 16, int viewport_width = 360, int viewport_height = 240) {
    input = trim_length_percentage(input);
    if (input.size() < 7 || input.substr(0, 5) != "calc(" || input.back() != ')') return false;
    const auto expression = input.substr(5, input.size() - 6);
    if (expression.find('(') != std::string_view::npos || expression.find(')') != std::string_view::npos) return false;
    std::size_t operation = std::string_view::npos;
    const auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f'; };
    for (std::size_t i = 1; i + 1 < expression.size(); ++i) {
        if (expression[i] != '+' && expression[i] != '-') continue;
        if (!ws(expression[i - 1]) || !ws(expression[i + 1]) || operation != std::string_view::npos) return false;
        operation = i;
    }
    if (operation == std::string_view::npos) return false;
    ParsedLengthPercentage candidate;
    double right_length = 0, right_percentage = 0;
    if (font_size <= 0 || viewport_width <= 0 || viewport_height <= 0 ||
        !parse_length_percentage_term(expression.substr(0, operation), font_size, viewport_width, viewport_height,
                                      candidate.length_pixels, candidate.percentage) ||
        !parse_length_percentage_term(expression.substr(operation + 1), font_size, viewport_width, viewport_height,
                                      right_length, right_percentage)) return false;
    const double sign = expression[operation] == '-' ? -1.0 : 1.0;
    candidate.length_pixels += sign * right_length;
    candidate.percentage += sign * right_percentage;
    if (!std::isfinite(candidate.length_pixels) || !std::isfinite(candidate.percentage)) return false;
    output = candidate;
    return true;
}

} // namespace jellyframe::detail
