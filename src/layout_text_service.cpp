#include "layout_text_service.h"
#include <limits>
namespace jellyframe::detail {
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
    if (style.letter_spacing != 0) return TextSystemStatus::UnsupportedStyle;
    LayoutTextFont draft;
    draft.family = style.font_family_hash;
    draft.size = style.font_size*1000;
    draft.weight = static_cast<std::uint16_t>(style.font_weight);
    output = draft;
    return TextSystemStatus::Ok;
}
}
