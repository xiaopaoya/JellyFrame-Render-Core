#pragma once

#include "render_core/arena.h"
#include "render_core/dom.h"
#include "render_core/feature_config.h"
#include "render_core/geometry.h"
#include "render_core/render_tree.h"
#include "render_core/style.h"
#include "render_core/text_backend.h"
#include "render_core/text_system.h"

#include <memory>
#include <atomic>
#include <string>
#include <vector>

namespace jellyframe {

struct LayoutBox;

struct PreparedTextLine {
    std::string packet;
    int origin_x = 0;
    int baseline_y = 0;
    int width = 0;
};

struct LayoutBoxDeleter {
    bool arena_owned = false;
    void operator()(LayoutBox* box) const;
};

using LayoutBoxPtr = std::unique_ptr<LayoutBox, LayoutBoxDeleter>;

// This is intentionally owned by the layout box: a layout tree and its layer
// tree are one frame-local snapshot, so the result cannot outlive its inputs.
struct TextLayoutCache {
    bool valid = false;
    std::string source_text;
    std::string rendered_text;
    std::vector<std::string> lines;
    TextMeasureProvider text_measure;
    const TextSystem* text_system = nullptr;
    std::uint64_t text_system_generation = 0;
    bool prepared_text = false;
    std::string prepared_packet;
    std::vector<PreparedTextLine> prepared_lines;
    int prepared_origin_x = 0;
    int prepared_baseline_y = 0;
    int available_width = 0;
    int content_width = 0;
    int rect_x = 0;
    int rect_y = 0;
    int rect_width = 0;
    int rect_height = 0;
    int viewport_width = 0;
    int viewport_height = 0;
    int font_size = 0;
    int font_weight = 0;
    std::uint32_t font_family_hash = 0;
    int line_height = 0;
    int text_indent = 0;
    int letter_spacing = 0;
    TextTransform text_transform = TextTransform::None;
    bool overflow_wrap_anywhere = false;
    bool white_space_nowrap = false;
    bool text_overflow_ellipsis = false;
    TextAlign text_align = TextAlign::Start;
};

// Final flow geometry before pixel projection, also restored by speculative
// layout transactions. Extents -1 mark unmeasured content; -2 preserves an
// out-of-flow box's static flow origin until final positioned layout.
struct LayoutContentGeometry {
    std::int64_t x = 0, y = 0, width = -1;

    constexpr LayoutContentGeometry() = default;
    constexpr LayoutContentGeometry(std::int64_t origin_x, std::int64_t origin_y,
                                    std::int64_t content_width, std::int64_t content_height,
                                    bool definite_height)
        : x(origin_x), y(origin_y), width(content_width),
          encoded_height_(encode_height(content_height, definite_height)) {}

    constexpr std::int64_t height() const {
        return encoded_height_ <= -3 ? -(encoded_height_ + 3) : encoded_height_;
    }
    constexpr bool height_definite() const { return encoded_height_ >= 0; }
    void set_height_definite(bool definite) { encoded_height_ = encode_height(height(), definite); }

private:
    // Measured Q1000 extents are bounded by INT_MAX * 1000. Nonnegative
    // encoded heights are definite; -(height + 3) is indefinite. The -1/-2
    // sentinels stay unmeasured/static, saving a bool and eight-byte padding.
    static constexpr std::int64_t encode_height(std::int64_t height, bool definite) {
        return height < 0 || definite ? height : -height - 3;
    }
    std::int64_t encoded_height_ = -1;
};
static_assert(sizeof(LayoutContentGeometry) == 4 * sizeof(std::int64_t));

struct LayoutBox {
    const Node* node = nullptr;
    Style style;
    Rect rect;
    LayoutContentGeometry content_geometry;
    int viewport_width = 0;
    int viewport_height = 0;
    TextLayoutCache text_layout_cache;
    std::vector<LayoutBoxPtr> children;
};

// Cumulative allowance for source queries and explicit intrinsic planner
// charges in one public layout call. Other layout/paint work is outside this
// budget. Individual
// source queries also retain their own safety ceilings. Scratch is a peak of
// simultaneously live query/planner payload, not an accumulated byte count.
struct LayoutIntrinsicQueryLimits {
    std::size_t max_boxes = 256;
    std::size_t max_depth = 32;
    std::size_t max_source_bytes = 8192;
    std::size_t max_measurements = 512;
    std::size_t max_work = 65536;
    std::size_t max_scratch_bytes = 32768;
    const std::atomic_bool* cancellation = nullptr;
};

struct LayoutIntrinsicQueryStatistics {
    std::size_t boxes = 0;
    std::size_t source_bytes = 0;
    std::size_t measurements = 0;
    std::size_t work = 0;
    std::size_t queries = 0;
    std::size_t resource_refusals = 0;
    std::size_t peak_scratch_bytes = 0;
};

namespace detail { class IntrinsicSourceQueryLedger; }

struct LayoutEngineOptions {
    std::size_t max_layout_boxes = 4096;
    DiagnosticSink* diagnostics = nullptr;
    std::size_t max_layout_depth = 64;
    const TextSystem* text_system = nullptr;
    std::size_t max_prepared_text_glyphs = 128;
    LayoutIntrinsicQueryLimits intrinsic_query_limits;
    // Borrowed synchronous output, replaced once at the end of each public
    // layout call. Includes failed queries and speculative probes; rollback
    // never refunds work already performed.
    LayoutIntrinsicQueryStatistics* intrinsic_query_statistics = nullptr;
};

class LayoutEngine {
public:
    explicit LayoutEngine(const StyleResolver& style_resolver,
                          TextMeasureProvider text_measure = {},
                          LayoutEngineOptions options = {});

    LayoutBoxPtr layout(const Node& root, int viewport_width) const;
    LayoutBoxPtr layout(const Node& root, int viewport_width, int viewport_height) const;
    LayoutBoxPtr layout(const Node& root, int viewport_width, MonotonicArena& arena) const;
    LayoutBoxPtr layout(const Node& root, int viewport_width, int viewport_height, MonotonicArena& arena) const;
    LayoutBoxPtr layout(const RenderObject& render_tree, int viewport_width) const;
    LayoutBoxPtr layout(const RenderObject& render_tree, int viewport_width, int viewport_height) const;
    LayoutBoxPtr layout(const RenderObject& render_tree, int viewport_width, MonotonicArena& arena) const;
    LayoutBoxPtr layout(const RenderObject& render_tree, int viewport_width, int viewport_height, MonotonicArena& arena) const;

private:
    const StyleResolver& style_resolver_;
    TextMeasureProvider text_measure_;
    LayoutEngineOptions options_;
    detail::IntrinsicSourceQueryLedger* intrinsic_queries_ = nullptr;

    int layout_fixed_table(LayoutBox& box, int x, int y, int available_width, int height, std::size_t depth) const;
    struct PreciseBoxContext;
    int layout_box(LayoutBox& box, int x, int y, int width, int height, std::size_t depth,
                   const PreciseBoxContext* precision = nullptr, std::int64_t* flow_units = nullptr) const;
    int layout_precise_block(LayoutBox&, int height, std::size_t depth,
                             const PreciseBoxContext&, std::int64_t* flow_units) const;
    int layout_text_box(LayoutBox& box,
                        int border_box_x,
                        int border_box_y,
                        int content_width,
                        int min_width,
                        int height) const;
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
    bool layout_wrapped_row_flex(LayoutBox&, std::int64_t content_x, std::int64_t content_y,
                                std::int64_t content_width, int containing_height,
                                std::size_t depth, std::int64_t& natural_height) const;
    bool layout_precise_flex(LayoutBox&, std::int64_t content_width, std::int64_t fixed_height,
                             const PreciseBoxContext&, std::int64_t content_x, std::int64_t content_y,
                             int legacy_height, std::size_t depth, std::int64_t& natural_height) const;
    int layout_flex_box(LayoutBox& box,
                        int content_x,
                        int content_y,
                        int content_width,
                        int containing_height,
                        std::size_t depth) const;
    int layout_grid_box(LayoutBox& box,
                        int content_x,
                        int content_y,
                        int content_width,
                        int containing_height,
                        std::size_t depth) const;
#endif
    int layout_inline_children(LayoutBox& box, int content_x, int content_y, int content_width, std::size_t depth) const;
    struct PositionedContext;
    void layout_positioned_children(LayoutBox&, const PositionedContext&, std::size_t depth) const;
    LayoutBoxPtr build_with_arena(const RenderObject& render_tree,
                                  int viewport_width,
                                  int viewport_height,
                                  MonotonicArena* arena) const;
    void build_layout_tree(const RenderObject& object, LayoutBox& box, MonotonicArena* arena,
                           bool& unsupported_mixed_dimension) const;
    LayoutBoxPtr make_layout_box(MonotonicArena* arena) const;
};

std::size_t count_layout_boxes(const LayoutBox& root);

} // namespace jellyframe
