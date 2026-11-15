#include "render_core/bitmap_font.h"

namespace jellyframe {

const BitmapFontGlyph* find_bitmap_glyph(const BitmapFont& font, std::uint32_t codepoint) {
    if (font.glyphs == nullptr) {
        return nullptr;
    }
    std::size_t first = 0;
    std::size_t count = font.glyph_count;
    while (count > 0) {
        const std::size_t step = count / 2;
        const std::size_t index = first + step;
        const std::uint32_t candidate = font.glyphs[index].codepoint;
        if (candidate < codepoint) {
            first = index + 1;
            count -= step + 1;
        } else {
            count = step;
        }
    }
    if (first < font.glyph_count && font.glyphs[first].codepoint == codepoint) {
        return &font.glyphs[first];
    }
    return nullptr;
}

} // namespace jellyframe
