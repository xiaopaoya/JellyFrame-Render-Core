#pragma once

#include "intrinsic_source_query.h"

namespace jellyframe::detail {

struct IntrinsicSourceHeightResult {
    IntrinsicSourceStatus status = IntrinsicSourceStatus::Unsupported;
    std::int64_t border_box_block_size_units = -1;
    std::size_t boxes_visited = 0;
    std::size_t source_bytes = 0;
    std::size_t measurements = 0;
    std::size_t work = 0;
    std::size_t peak_scratch_bytes = 0;
};

// Measures an admitted horizontal LTR block source at an explicit content
// inline size. Normal line-height comes from Core TextSystem font metrics;
// explicit line-height uses the style's pixel value. The result includes root
// vertical padding and border, excludes margins, and commits only on Complete.
IntrinsicSourceHeightResult query_intrinsic_source_height(
    const LayoutBox& box, const TextSystem& text_system,
    std::int64_t available_inline_size_units,
    IntrinsicSourceQueryLimits limits = {}) noexcept;

} // namespace jellyframe::detail
