#pragma once

#include "render_core/render_tree.h"

namespace jellyframe::detail {

// A generated text leaf is the sole user of this alias. The shared pointer
// retains the leaf's text Node across render-tree and layout-tree lifetimes.
inline const ContentsSourceTopology generated_text_owner{};

inline bool owns_generated_text(const std::shared_ptr<const ContentsSourceTopology>& owner) noexcept {
    return owner.get() == &generated_text_owner;
}

struct GeneratedFlexGridText final : Node {
    explicit GeneratedFlexGridText(const Node& origin) noexcept
        : Node(NodeType::Text), origin(&origin), document(&origin) {
        while (document->parent) document = document->parent;
        generation = document->mutation_generation;
    }

    const Node* origin;
    const Node* document;
    std::uint64_t generation;
};

inline const Node* current_generated_text_origin(const Node& node) noexcept {
    const auto& text = static_cast<const GeneratedFlexGridText&>(node);
    return text.document->mutation_generation == text.generation ? text.origin : nullptr;
}

} // namespace jellyframe::detail
