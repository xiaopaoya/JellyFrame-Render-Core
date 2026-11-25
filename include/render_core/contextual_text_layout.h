#pragma once
#include "render_core/contextual_text_paragraph.h"
#include "render_core/layout.h"
#include "render_core/layer_tree.h"
#include <vector>
#if !JELLYFRAME_RENDER_CORE_CONTEXTUAL_LAYOUT_ENABLED
#error "Contextual layout requires JELLYFRAME_ENABLE_CONTEXTUAL_LAYOUT"
#endif
#define JELLYFRAME_CONTEXTUAL_TEXT_LAYOUT_API_VERSION 1
#define JELLYFRAME_CONTEXTUAL_TEXT_HIT_CAPTURE_API_VERSION 1
#define JELLYFRAME_CONTEXTUAL_SHARED_INLINE_API_VERSION 1
namespace jellyframe::contextual_text {
// Explicit resource selection for every admitted paragraph. There is no scalar
// fallback. Static ordinary blocks may nest. A block can contain direct text
// or a shared run of uniformly styled text and neutral inline spans.
struct LayoutOptions {
  Binding binding;
  std::uint32_t font_family_hash = 0;
  ParagraphLimits paragraph;
  std::size_t max_paragraphs = 32, max_lines = 128, max_source_bytes = 8192;
  std::size_t max_text_result_bytes = 1024U * 1024U;
};
// Explicit retained text capacities and traversal/shaping work only. Excludes
// tree storage, Binding assets, allocator metadata and transients; not peak RAM.
struct LayoutUsage {
  std::size_t boxes = 0, source_bytes = 0, paragraphs = 0, lines = 0;
  std::size_t shape_calls = 0, text_result_bytes = 0;
  std::uint64_t work = 0;
};
struct LayoutParagraph {
  Paragraph paragraph;
  const LayoutBox* owner = nullptr;
  std::int32_t origin_x_q1000 = 0, origin_y_q1000 = 0;
  std::int32_t content_width_q1000 = 0;
  TextAlign align = TextAlign::Start;
  // Final line pen: x = origin_x + (content_width - advance_x) * alignment,
  // where alignment is 0/start, 1/2/center, 1/end; use signed int64 arithmetic
  // (overwide lines have negative slack). y = origin_y + line.baseline_y.
  // All admitted final pen coordinates fit signed Q1000 int32.
  Color color;
};
// Limits for the explicit hit capture overload. The original overload captures
// paint only for direct-text layouts. Shared inline runs require hit capture.
struct TextHitCaptureOptions {
  std::uint64_t max_work = 262144;
  std::size_t max_fragments = 16384;
};
// Raster-space glyph bounds clipped to the capture viewport and associated
// with an original UTF-8 source range.
// paragraph_index selects LayoutResult::paragraphs(); owner_index is local to
// that paragraph. Resolve an owner through LayoutResult::text_owner().
struct ContextualTextHitFragment {
  std::uint32_t paragraph_index = 0, line_index = 0;
  std::uint32_t owner_index = 0, cluster_index = 0, glyph_index = 0;
  std::uint32_t source_begin = 0, source_end = 0;
  Rect ink{};
};
class LayoutResult {
public:
  struct State;
  bool valid() const noexcept { return bool(state_); }
  // The tree is shaped-only and explicitly rejected by legacy LayerTreeBuilder.
  // Nodes remain borrowed under ordinary LayoutBox DOM lifetime rules.
  const LayoutBox* tree() const noexcept;
  const std::vector<LayoutParagraph>& paragraphs() const noexcept;
  // Requires a live document and the source generation used to build this layout.
  const Node* text_owner(std::size_t paragraph, std::size_t owner) const noexcept;
  std::uint64_t source_generation() const noexcept;
private:
  std::shared_ptr<const State> state_;
  friend class ::jellyframe::LayoutEngine;
  friend class ::jellyframe::LayerTreeBuilder;
};
// Retains both geometry and resource ownership. Background commands are frozen
// in normal tree order; insert each paragraph after its corresponding command
// count. These lines must use contextual paint, never a scalar text painter.
// Capture refuses an expired or stale document. DOM must not be mutated
// concurrently. Painting these value-owned commands/packets needs no DOM access.
// The caller is responsible for whole-frame transactional presentation.
class LayerCapture {
public:
  struct State;
  bool valid() const noexcept { return bool(state_); }
  const LayerNode* layer_tree() const noexcept;
  // Paragraphs and paint geometry remain usable after DOM destruction. DOM
  // pointers reachable through layout().tree() keep their borrowed lifetime.
  const LayoutResult& layout() const noexcept;
  const std::vector<std::uint32_t>& after_command_counts() const noexcept;
  // Value-owned, paragraph-local source hit fragments. They are committed with
  // the same capture as the background commands and shaped line packets.
  const std::vector<ContextualTextHitFragment>& text_hit_fragments() const noexcept;
private:
  std::shared_ptr<const State> state_;
  friend class ::jellyframe::LayerTreeBuilder;
};
} // namespace jellyframe::contextual_text
