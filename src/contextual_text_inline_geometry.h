#pragma once

#include "render_core/contextual_text_paragraph.h"
#include "render_core/geometry.h"

#include <cstdint>
#include <vector>

namespace jellyframe::shaping_binding { class Snapshot; }

namespace jellyframe::detail {

struct ContextualTextInkBindingAccess {
    static const shaping_binding::Snapshot& snapshot(
        const contextual_text::Binding&) noexcept;
};

enum class ContextualTextInkStatus {
    Complete,
    InvalidInput,
    BindingMismatch,
    ResourceLimit,
    AllocationFailure
};

struct ContextualTextGlyphInk {
    std::uint32_t cluster_index = 0;
    std::uint32_t glyph_index = 0;
    Rect bounds{};
};

// Computes per-glyph raster ink bounds for one retained JFSP2 paragraph line.
// Bounds use paragraph coordinates and the same rounding as contextual paint.
// Output and work publish only after the complete line has been validated.
ContextualTextInkStatus contextual_text_line_glyph_ink(
    const contextual_text::Paragraph&, std::size_t line_index,
    std::int32_t baseline_x_q1000, std::int32_t baseline_y_q1000,
    std::vector<ContextualTextGlyphInk>& output,
    std::uint64_t& work) noexcept;

} // namespace jellyframe::detail
