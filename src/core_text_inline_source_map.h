#pragma once

#include "core_text_paragraph.h"
#include "core_text_cluster_map.h"
#include "render_core/contextual_text_paragraph.h"

#include <string_view>
#include <vector>

namespace jellyframe::detail {

enum class CoreTextInlineSourceMapStatus {
    Complete,
    InvalidInput,
    UnsupportedNormalization,
    WorkLimit,
    AllocationFailure
};

// Builds a token stream for a text-only, white-space:normal paragraph.
// The owner views use CoreTextParagraphSource::owner as their index. Dropped source
// bytes become omitted tokens at the corresponding normalized byte position.
// Output is replaced only on success.
CoreTextInlineSourceMapStatus build_core_text_inline_source_tokens(
    const CoreTextParagraph& paragraph,
    const std::string_view* owner_sources, std::size_t owner_count,
    std::vector<CoreTextClusterMapToken>& output, std::uint64_t* remaining_work = nullptr) noexcept;

// Projects the global token stream onto one contextual line packet. Visible
// source bytes are rebased to the packet's line-local coordinates. Consumed
// trailing whitespace is retained as omitted source records at the line end.
// Output is replaced only on success.
CoreTextInlineSourceMapStatus project_core_text_inline_line_tokens(
    const CoreTextParagraph& paragraph,
    const std::vector<CoreTextClusterMapToken>& source_tokens,
    std::size_t line_begin, std::size_t line_end, std::size_t consumed_end,
    std::vector<CoreTextClusterMapToken>& output) noexcept;

// End-to-end Stage A adapter for one real JFSP2 paragraph line. It verifies
// exact normalized-text identity and packet binding, projects source tokens,
// decodes cluster/glyph intervals and transactionally maps owners.
CoreTextInlineSourceMapStatus map_experimental_contextual_inline_line(
    const CoreTextParagraph& source_paragraph,
    const std::string_view* owner_sources, std::size_t owner_count,
    const contextual_text::Paragraph& shaped_paragraph, std::size_t line_index,
    CoreTextClusterMap& output,
    CoreTextClusterMapLimits limits = {},
    std::uint64_t* remaining_work = nullptr) noexcept;

// Reconstructs original UTF-8 intervals across ordered source owners.
CoreTextInlineSourceMapStatus build_contextual_owner_source(
    const contextual_text::Paragraph&, const std::string_view* owners,
    std::size_t owner_count, CoreTextParagraph& output, std::uint64_t& work) noexcept;

} // namespace jellyframe::detail
