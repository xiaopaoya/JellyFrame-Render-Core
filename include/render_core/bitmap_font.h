#pragma once

#include <cstddef>
#include <cstdint>

namespace jellyframe {

struct BitmapFontGlyph {
    std::uint32_t codepoint = 0;
    std::uint8_t width = 0;
    std::uint8_t height = 0;
    std::uint8_t advance = 0;
    std::uint8_t bytes_per_row = 0;
    const std::uint8_t* rows = nullptr;
    std::uint8_t bits_per_pixel = 1;
};

struct BitmapFont {
    // Glyphs must be sorted by ascending Unicode codepoint; lookup is binary.
    const BitmapFontGlyph* glyphs = nullptr;
    std::size_t glyph_count = 0;
    std::uint8_t line_height = 0;
    std::uint8_t fallback_advance = 0;
};

const BitmapFontGlyph* find_bitmap_glyph(const BitmapFont& font, std::uint32_t codepoint);

} // namespace jellyframe
