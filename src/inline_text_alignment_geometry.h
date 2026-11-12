#pragma once
#include "inline_fragment_lines.h"
#include <algorithm>
namespace jellyframe::detail {
struct InlineTextAlignmentGeometry {
    TextAlign alignment;
    std::int64_t offset, used_width;
    bool justified;
};
inline InlineTextAlignmentGeometry inline_text_alignment_geometry(
    TextAlign all, TextAlignLast last, InlineLineEnd ending, bool,
    std::int64_t width, std::int64_t natural, std::size_t) noexcept {
    auto alignment = all;
    if (ending != InlineLineEnd::SoftWrap) {
        if (last == TextAlignLast::Start) alignment = TextAlign::Start;
        if (last == TextAlignLast::Center) alignment = TextAlign::Center;
        if (last == TextAlignLast::End) alignment = TextAlign::End;
    }
    const auto spare = std::max<std::int64_t>(0, width-natural);
    return {alignment, alignment == TextAlign::Center ? spare/2 : alignment == TextAlign::End ? spare : 0, natural, false};
}
}
