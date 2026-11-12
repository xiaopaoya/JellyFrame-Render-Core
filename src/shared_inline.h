#pragma once
#include "core_text_paragraph.h"
namespace jellyframe::detail {
struct SharedInlineProof {
    SharedInlineProof() : source(0, '\0'), tag(0, '\0') {}
    const LayoutBox* box = nullptr;
    const Node* node = nullptr;
    const Node* parent = nullptr;
    std::size_t parent_member = 0;
    std::size_t children = 0;
    std::size_t dom_children = 0;
    std::size_t dom_index = 0;
    std::size_t render_index = 0;
    std::string source;
    std::string tag;
    LayoutTextFont font;
    Color color;
    int line_height = 0;
    int indent = 0;
    TextAlign align = TextAlign::Start;
    bool nowrap = false;
    bool anywhere = false;
};
struct SharedInlineMember {
    SharedInlineMember() : fragments(0) {}
    SharedInlineProof proof;
    Rect bounds;
    Rect ink_bounds;
    std::vector<Rect> fragments;
};
struct SharedInlineOmittedSource {
    SharedInlineOmittedSource() : source(0, '\0') {}
    const Node* node = nullptr;
    std::size_t parent_member = 0;
    std::size_t dom_index = 0;
    std::string source;
};
struct SharedInlineSnapshot {
    SharedInlineSnapshot() : members(0), owners(0), omitted(0) {}
    const TextSystem* system = nullptr;
    std::uint64_t generation = 0;
    std::uint64_t dom_generation = 0;
    const Node* dom_root = nullptr;
    int content_offset_x = 0;
    int content_offset_y = 0;
    std::int64_t width_units = 0;
    std::array<int, 32> host_geometry{};
    ComputedSizeStorage host_computed_sizes;
    CoreTextParagraph paragraph;
    std::vector<SharedInlineMember> members;
    std::vector<std::size_t> owners;
    std::vector<SharedInlineOmittedSource> omitted;
    CoreTextParagraphUsage usage;
    std::size_t retained_bytes = 0;
};
struct SharedInlineBinding {
    std::shared_ptr<const SharedInlineSnapshot> snapshot;
    std::size_t member = 0;
};
// All allocation, admission, measurement and geometry happen before publication.
// A failed attempt leaves every participating box and cache untouched.
bool layout_shared_inline(LayoutBox&, int x, int y, int width,
    const LayoutEngineOptions&, int& height, std::int64_t precise_width_units = -1) noexcept;
void retire_shared_inline(LayoutBox&) noexcept;
bool shared_inline_current(const SharedInlineBinding&, const TextSystem*) noexcept;
bool shared_inline_contains(const LayoutBox&, int x, int y) noexcept;
Rect shared_inline_damage_bounds(const LayoutBox&) noexcept;
bool emit_shared_inline(const LayoutBox&, DisplayList&) noexcept;
}
