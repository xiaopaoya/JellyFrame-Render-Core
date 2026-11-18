#pragma once
#include "render_core/contextual_text_layout.h"
namespace jellyframe::contextual_text {
struct LayoutResult::State {
  LayoutBoxPtr tree;
  std::vector<LayoutParagraph> paragraphs;
  const Node* source_root = nullptr;
  std::uint64_t source_generation = 0;
  NodeLifetimeObserverRegistration source_lifetime;
};
struct LayerCapture::State {
  LayoutResult layout;
  LayerNodePtr layer_tree;
  std::vector<std::uint32_t> after_command_counts;
};
}
namespace jellyframe::detail {
// Untouched scalar caches have width zero; populated caches have a
// nonnegative width. The sentinel reserves no extra LayoutBox storage.
inline bool is_contextual_layout_box(const LayoutBox& box) noexcept {
  return !box.text_layout_cache.valid && box.text_layout_cache.available_width == -1;
}
struct ContextualLayoutFrame {
  const contextual_text::LayoutOptions& options;
  std::uint64_t& work;
  contextual_text::LayoutUsage usage;
  contextual_text::Status status = contextual_text::Status::ok;
  std::shared_ptr<contextual_text::LayoutResult::State> draft;
  std::size_t expected_paragraphs = 0;
  ContextualLayoutFrame(const contextual_text::LayoutOptions& o, std::uint64_t& w) : options(o), work(w) {}
  contextual_text::Status admit(const RenderObject&, std::size_t max_boxes, std::size_t max_depth);
  std::int64_t prepare(LayoutBox&, std::int64_t x, std::int64_t y, std::int64_t width);
  bool finalize(LayoutBoxPtr);
};
}
