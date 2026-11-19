#include "layout_text_service.h"
#include "render_core/text_backend.h"
#include <limits>
namespace jellyframe::detail {
std::size_t count_word_spacing_separators(std::string_view text) noexcept {
    std::size_t count = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto byte = static_cast<unsigned char>(text[i]);
        // C2 A0 is U+00A0. A UTF-8 continuation byte cannot start this pair.
        if (byte == ' ' || (byte == 0xc2U && i + 1 < text.size() &&
            static_cast<unsigned char>(text[i + 1]) == 0xa0U))
            ++count;
    }
    return count;
}

TextFontRequest LayoutTextFont::request() const noexcept {
    TextFontRequest result;
    result.families = &family;
    result.family_count = 1;
    result.size_q1000 = size;
    result.weight = weight;
    result.style = style;
    return result;
}
TextSystemStatus layout_text_font(const Style& style, LayoutTextFont& output) noexcept {
    if (style.font_size < 0 || style.font_size > std::numeric_limits<std::int32_t>::max()/1000 ||
        style.font_weight < 1 || style.font_weight > 1000) return TextSystemStatus::InvalidRequest;
    LayoutTextFont draft;
    draft.family = style.font_family_hash;
    draft.size = style.font_size*1000;
    draft.letters = bounded_letter_spacing(style.font_size, style.letter_spacing) * 1000;
    draft.words = bounded_word_spacing_q1000(style.font_size, style.word_spacing);
    draft.weight = static_cast<std::uint16_t>(style.font_weight);
    output = draft;
    return TextSystemStatus::Ok;
}
}
