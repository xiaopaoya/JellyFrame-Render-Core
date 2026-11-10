#pragma once

#include "render_core/style.h"
#include "render_core/text_system.h"
#include "text_system_access.h"

#include <algorithm>
#include <limits>
#include <string_view>

namespace jellyframe::detail {

inline TextSystemStatus measure_core_text(const TextSystem& text_system,
                                          std::string_view text,
                                          const Style& style,
                                          TextRunMetrics& metrics,
                                          std::size_t work_limit,
                                          std::size_t& work_used) noexcept {
    work_used = 0;
    if (style.letter_spacing != 0) return TextSystemStatus::UnsupportedStyle;
    if (style.font_size < 0 || style.font_size > std::numeric_limits<std::int32_t>::max() / 1000)
        return TextSystemStatus::InvalidRequest;
    std::uint32_t family_id = 0;
    const auto family_status = TextSystemAccess::resolve_family_hash(
        text_system, style.font_family_hash, family_id, work_limit, work_used);
    if (family_status != TextSystemStatus::Ok) return family_status;
    TextFontRequest request;
    request.families = &family_id;
    request.family_count = 1;
    request.size_q1000 = style.font_size * 1000;
    request.weight = static_cast<std::uint16_t>(std::clamp(style.font_weight, 1, 1000));
    std::size_t measured_work = 0;
    const auto status = TextSystemAccess::measure(text_system, text, request, metrics,
        work_limit - work_used, measured_work);
    work_used += measured_work;
    return status;
}

} // namespace jellyframe::detail
