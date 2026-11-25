#pragma once
#include "render_core/contextual_text_layout.h"
namespace jellyframe::contextual_text {
// A shared paragraph records byte lengths in the owned Paragraph::source()
// and borrowed DOM identities under the layout's document lifetime guard.
struct InlineTextOwner {
  const Node* node;
  std::size_t length;
};
struct SharedTextOwners {
  std::size_t paragraph_index;
  std::vector<InlineTextOwner> owners;
};
struct LayoutResult::State {
  LayoutBoxPtr tree;
  std::vector<LayoutParagraph> paragraphs;
  std::vector<SharedTextOwners> shared_owners;
  const Node* source_root = nullptr;
  std::uint64_t source_generation = 0;
  NodeLifetimeObserverRegistration source_lifetime;
  bool source_is_current() const noexcept {
    return source_lifetime.active() && !source_root->parent &&
        source_root->mutation_generation == source_generation;
  }
};
struct LayerCapture::State {
  LayoutResult layout;
  LayerNodePtr layer_tree;
  std::vector<std::uint32_t> after_command_counts;
  std::vector<ContextualTextHitFragment> text_hit_fragments;
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
  const bool shared_inline;
  ContextualLayoutFrame(const contextual_text::LayoutOptions& o, std::uint64_t& w, bool shared)
      : options(o), work(w), shared_inline(shared) {}
  contextual_text::Status admit(const RenderObject&, std::size_t max_boxes, std::size_t max_depth);
  std::int64_t prepare(LayoutBox&, std::int64_t x, std::int64_t y, std::int64_t width);
  std::int64_t prepare_shared(LayoutBox&, std::int64_t x, std::int64_t y, std::int64_t width);
  std::int64_t prepare_paragraph(LayoutBox&, std::string_view source, std::int64_t x,
      std::int64_t y, std::int64_t width, bool shared);
  bool finalize(LayoutBoxPtr);
};
}
