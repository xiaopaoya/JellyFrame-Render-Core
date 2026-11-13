#pragma once
#include "core_text_paragraph.h"
namespace jellyframe::detail {

struct NaturalImageSizingFrame;
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
    Color background_color;
    TextShadowStyle text_shadow;
    int font_size = 14;
    int line_height = 0;
    int indent = 0;
    TextAlign align = TextAlign::Start;
    bool nowrap = false;
    bool anywhere = false;
    bool preserves_whitespace = false;
    bool text_decoration_underline = false;
    bool text_decoration_line_through = false;
    GeneratedContentKind before_kind = GeneratedContentKind::None;
    GeneratedContentKind after_kind = GeneratedContentKind::None;
    std::string before_text;
    std::string after_text;
    Color before_color;
    Color after_color;
    int before_weight = 400;
    int after_weight = 400;
    bool before_color_specified = false;
    bool after_color_specified = false;
    bool before_weight_specified = false;
    bool after_weight_specified = false;
    bool before_left_specified = false;
    bool after_left_specified = false;
    bool atomic_image = false;
    bool natural_image_sized = false;
    ImageIntrinsicSize image_intrinsic{};
    std::string image_source;
    std::int64_t image_width_units = 0;
    int image_height = 0;
    CoreTextAtomicPlacement image_placement{};
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
    SharedInlineSnapshot() : members(0), owners(0), owner_paints(0), omitted(0), atomic_placements(0) {}
    const TextSystem* system = nullptr;
    std::uint64_t generation = 0;
    std::uint64_t dom_generation = 0;
    bool whitespace_mode_set = false;
    bool preserves_whitespace = false;
    const Node* dom_root = nullptr;
    int content_offset_x = 0;
    int content_offset_y = 0;
    std::int64_t width_units = 0;
    std::array<int, 32> host_geometry{};
    ComputedSizeStorage host_computed_sizes;
    CoreTextParagraph paragraph;
    std::vector<SharedInlineMember> members;
    std::vector<std::size_t> owners;
    struct OwnerPaint {
        Color color;
        int font_size = 14;
        TextShadowStyle text_shadow;
        bool text_decoration_underline = false;
        bool text_decoration_line_through = false;
    };
    std::vector<OwnerPaint> owner_paints;
    std::vector<SharedInlineOmittedSource> omitted;
    std::vector<CoreTextAtomicPlacement> atomic_placements;
    CoreTextParagraphUsage usage;
    std::size_t retained_bytes = 0;
};
struct SharedInlineBinding {
    std::shared_ptr<const SharedInlineSnapshot> snapshot;
    std::size_t member = 0;
};
// LayerTree supplies a callback that resolves the existing image handle and
// appends the typed image command to this paragraph's private display draft.
using SharedInlineImageEmitter = bool (*)(const LayoutBox&, DisplayList&, void* context);
// All allocation, admission, measurement and geometry happen before publication.
// A failed attempt leaves every participating box and cache untouched.
bool layout_shared_inline(LayoutBox&, int x, int y, int width,
    const LayoutEngineOptions&, int& height, std::int64_t precise_width_units = -1,
    NaturalImageSizingFrame* natural_images = nullptr) noexcept;
void retire_shared_inline(LayoutBox&) noexcept;
bool shared_inline_current(const SharedInlineBinding&, const TextSystem*) noexcept;
bool shared_inline_contains(const LayoutBox&, int x, int y) noexcept;
Rect shared_inline_damage_bounds(const LayoutBox&) noexcept;
std::size_t shared_inline_command_count(const SharedInlineBinding&, const TextSystem*) noexcept;
bool emit_shared_inline(const LayoutBox&, DisplayList&,
    SharedInlineImageEmitter = nullptr, void* image_context = nullptr) noexcept;
}
