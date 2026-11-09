#pragma once
#include "css_selector_tokens.h"
#include <string_view>

namespace jellyframe::detail::ratio_tokens {
// Literal parser subset of the experiment's CSS condition token scanner.
// Keep token boundaries intact without importing conditional-rule machinery.
inline bool digit(char c) noexcept { return c >= '0' && c <= '9'; }
inline void trivia(std::string_view text, std::size_t& pos) noexcept {
    while (pos < text.size()) {
        const char c = text[pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') ++pos;
        else if (pos + 1 < text.size() && text[pos] == '/' && text[pos + 1] == '*') {
            const auto end = text.find("*/", pos + 2);
            pos = end == std::string_view::npos ? text.size() : end + 2;
        } else break;
    }
}
inline std::size_t number_end(std::string_view text, std::size_t pos) noexcept {
    const auto begin = pos;
    if (pos == text.size()) return begin;
    if (text[pos] == '+' || text[pos] == '-') ++pos;
    if (pos == text.size() || !(digit(text[pos]) ||
        (text[pos] == '.' && pos + 1 < text.size() && digit(text[pos + 1])))) return begin;
    while (pos < text.size() && digit(text[pos])) ++pos;
    if (pos + 1 < text.size() && text[pos] == '.' && digit(text[pos + 1])) {
        pos += 2;
        while (pos < text.size() && digit(text[pos])) ++pos;
    }
    if (pos < text.size() && (text[pos] == 'e' || text[pos] == 'E')) {
        auto exponent = pos + 1;
        if (exponent < text.size() && (text[exponent] == '+' || text[exponent] == '-')) ++exponent;
        if (exponent < text.size() && digit(text[exponent])) {
            pos = exponent + 1;
            while (pos < text.size() && digit(text[pos])) ++pos;
        }
    }
    return pos;
}
} // namespace jellyframe::detail::ratio_tokens
