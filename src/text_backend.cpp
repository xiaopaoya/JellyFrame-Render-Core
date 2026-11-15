#include "render_core/text_backend.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <limits>

namespace jellyframe {
namespace {

constexpr std::uint32_t kFontFamilyFnvOffset = 0x811c9dc5U;
constexpr std::uint32_t kFontFamilyFnvPrime = 0x01000193U;

int clamp_nonnegative_int64(std::int64_t value) {
    return static_cast<int>(std::clamp<std::int64_t>(
        value, 0, std::numeric_limits<int>::max()));
}

std::string_view trim_family_view(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    if (value.size() >= 2 &&
        ((value.front() == '"' && value.back() == '"') ||
         (value.front() == '\'' && value.back() == '\''))) {
        value.remove_prefix(1);
        value.remove_suffix(1);
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
            value.remove_prefix(1);
        }
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
            value.remove_suffix(1);
        }
    }
    return value;
}

bool family_equals_ascii(std::string_view family, std::string_view expected) {
    family = trim_family_view(family);
    if (family.size() != expected.size()) {
        return false;
    }
    for (std::size_t index = 0; index < family.size(); ++index) {
        const char normalized = static_cast<char>(std::tolower(static_cast<unsigned char>(family[index])));
        if (normalized != expected[index]) {
            return false;
        }
    }
    return true;
}

} // namespace

std::uint32_t normalized_font_family_hash(std::string_view family) {
    family = trim_family_view(family);
    if (family.empty() || is_generic_font_family(family)) {
        return 0;
    }

    std::uint32_t hash = kFontFamilyFnvOffset;
    bool pending_space = false;
    bool wrote = false;
    for (char ch : family) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (std::isspace(byte) != 0) {
            pending_space = wrote;
            continue;
        }
        if (pending_space) {
            hash ^= static_cast<std::uint8_t>(' ');
            hash *= kFontFamilyFnvPrime;
            pending_space = false;
        }
        const std::uint8_t normalized =
            byte < 0x80U ? static_cast<std::uint8_t>(std::tolower(byte)) : byte;
        hash ^= normalized;
        hash *= kFontFamilyFnvPrime;
        wrote = true;
    }
    return wrote ? hash : 0;
}

bool is_generic_font_family(std::string_view family) {
    return family_equals_ascii(family, "serif") ||
        family_equals_ascii(family, "sans-serif") ||
        family_equals_ascii(family, "monospace") ||
        family_equals_ascii(family, "cursive") ||
        family_equals_ascii(family, "fantasy") ||
        family_equals_ascii(family, "system-ui") ||
        family_equals_ascii(family, "ui-serif") ||
        family_equals_ascii(family, "ui-sans-serif") ||
        family_equals_ascii(family, "ui-monospace") ||
        family_equals_ascii(family, "emoji") ||
        family_equals_ascii(family, "math") ||
        family_equals_ascii(family, "fangsong");
}

int bounded_letter_spacing(int font_size, int letter_spacing) {
    const int safe_font_size = std::max(1, font_size);
    const int lower = -std::max(1, safe_font_size / 2);
    const int upper = clamp_nonnegative_int64(static_cast<std::int64_t>(safe_font_size) * 2);
    return std::max(lower, std::min(upper, letter_spacing));
}

} // namespace jellyframe
