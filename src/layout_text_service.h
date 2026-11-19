#pragma once

#include "render_core/prepared_text.h"
#include "render_core/style.h"
#include "render_core/text_backend.h"

#include <algorithm>

namespace jellyframe::detail {

// Internal, owned request values. No pointers into Style or this object's old
// address survive copies/moves. This is not another host-provider interface.
struct LayoutTextFont {
    std::uint32_t family = 0;
    std::int32_t size = 0, letters = 0, words = 0;
    std::uint16_t weight = 400;
    TextFaceStyle style = TextFaceStyle::Normal;
    TextFontRequest request() const noexcept;
    std::int64_t spacing_for(std::uint32_t codepoint) const noexcept {
        return static_cast<std::int64_t>(letters) + (codepoint == ' ' ? words : 0);
    }
};

// CSS inputs are checked without passing through the old integer-font callback
// boundary. Scalar tracking is separate from the font request and glyph metrics.
// Refusal preserves output. CSS family hashes are resolved by the collector.
TextSystemStatus layout_text_font(const Style&, LayoutTextFont&) noexcept;

inline std::int32_t bounded_word_spacing_q1000(int font_size, int word_spacing) {
    return bounded_letter_spacing(font_size, word_spacing) * 1000;
}

inline std::int64_t scalar_spacing_q1000(std::string_view text, std::size_t glyph_count,
                                         std::int32_t letters, std::int32_t words) {
    return static_cast<std::int64_t>(glyph_count) * letters +
        (words ? static_cast<std::int64_t>(std::count(text.begin(), text.end(), ' ')) * words : 0);
}

} // namespace jellyframe::detail
