#pragma once
#include "render_core/contextual_text_paragraph.h"
#include "render_core/layout.h"
#include "render_core/layer_tree.h"
#if !JELLYFRAME_RENDER_CORE_CONTEXTUAL_LAYOUT_ENABLED
#error "Contextual layout requires JELLYFRAME_ENABLE_CONTEXTUAL_LAYOUT"
#endif
#define JELLYFRAME_CONTEXTUAL_TEXT_LAYOUT_API_VERSION 1
namespace jellyframe::contextual_text {
// Explicit resource selection for every admitted paragraph. There is no scalar
// fallback. Static ordinary blocks may nest; each text host has one direct Text
// child. Mixed inline/atomic/effect/clip/contents topology is not admitted.
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
class LayoutResult {
public:
  struct State;
  bool valid() const noexcept { return bool(state_); }
  // The tree is shaped-only and explicitly rejected by legacy LayerTreeBuilder.
  // Nodes remain borrowed under ordinary LayoutBox DOM lifetime rules.
  const LayoutBox* tree() const noexcept;
  const std::vector<LayoutParagraph>& paragraphs() const noexcept;
  std::uint64_t source_generation() const noexcept;
private:
  std::shared_ptr<const State> state_;
  friend class ::jellyframe::LayoutEngine;
};
// Retains both geometry and resource ownership. Background commands are frozen
// in normal tree order; insert each paragraph after its corresponding command
// count. These lines must use contextual paint, never a scalar text painter.
// DOM must remain alive and unchanged during capture. Afterwards painting these
// value-owned commands/packets needs no DOM access. Whole-frame transactional
// presentation remains the caller's responsibility (for example a back buffer).
class LayerCapture {
public:
  struct State;
  bool valid() const noexcept { return bool(state_); }
  const LayerNode* layer_tree() const noexcept;
  const LayoutResult& layout() const noexcept;
  const std::vector<std::uint32_t>& after_command_counts() const noexcept;
private:
  std::shared_ptr<const State> state_;
  friend class ::jellyframe::LayerTreeBuilder;
};
} // namespace jellyframe::contextual_text
