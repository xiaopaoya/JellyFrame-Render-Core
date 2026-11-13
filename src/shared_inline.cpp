#include "render_core/text_normalization.h"
#include "computed_size_geometry.h"
#include "natural_image_sizing.h"
#include "shared_inline.h"
#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>

namespace jellyframe::detail {
namespace {
constexpr std::size_t max_members = 128;
constexpr std::size_t max_depth = 32;
constexpr std::size_t max_source = 8192;
using S = TextSystemStatus;
bool contains(Rect r, int x, int y) {
    return x >= r.x && y >= r.y && static_cast<std::int64_t>(x) < static_cast<std::int64_t>(r.x)+r.width &&
        static_cast<std::int64_t>(y) < static_cast<std::int64_t>(r.y)+r.height;
}
Rect union_rect(Rect a, Rect b) {
    if (a.width <= 0 || a.height <= 0) return b;
    if (b.width <= 0 || b.height <= 0) return a;
    const int x = std::min(a.x, b.x), y = std::min(a.y, b.y);
    return {x, y, safe_span(x, std::max(safe_edge(a.x, a.width), safe_edge(b.x, b.width))),
        safe_span(y, std::max(safe_edge(a.y, a.height), safe_edge(b.y, b.height)))};
}
bool empty_edges(const EdgeSizes& e) {
    return e.left == 0 && e.right == 0 && e.top == 0 && e.bottom == 0;
}
bool neutral(const Style& s, bool host, bool allow_image_dimensions = false,
    bool allow_sharp_text_shadow = false, bool allow_inline_background = false,
    bool allow_text_decoration = false) {
    if (s.visibility_hidden || s.text_transform != TextTransform::None || s.letter_spacing != 0 ||
        s.text_overflow_ellipsis || (s.text_shadow.enabled &&
            (!allow_sharp_text_shadow || s.text_shadow.blur != 0)) ||
        (!allow_text_decoration && (s.text_decoration_underline || s.text_decoration_line_through)) ||
        s.before_content_kind != GeneratedContentKind::None ||
        s.after_content_kind != GeneratedContentKind::None) return false;
    if (host) return s.display == Display::Block;
    if (s.display != Display::Inline || s.position_type != PositionType::Static ||
        s.opacity != 1.0F || !s.transform.empty() || !s.overflow.empty() ||
        (s.background_color.a != 0 && !allow_inline_background) || s.box_shadow.enabled || s.outline_width != 0 ||
        !empty_edges(s.margin) || !empty_edges(s.padding) || !empty_edges(s.border_width) ||
        (!allow_image_dimensions && (s.width != -1 || s.height != -1 || s.width_percent != -1 || s.height_percent != -1)) ||
        (allow_image_dimensions && s.height_percent != -1) ||
        (!allow_image_dimensions && (s.min_width != -1 || s.max_width != -1 ||
            s.min_height != -1 || s.max_height != -1 || s.min_width_percent != -1 ||
            s.max_width_percent != -1 || s.min_height_percent != -1 || s.max_height_percent != -1)) ||
        s.border_radius != 0 || s.border_radius_percent != -1 || s.border_radius_axes_specified ||
        !s.z_index_auto || s.aspect_ratio_width != 0 || s.aspect_ratio_height != 0 ||
        s.list_style_type != ListStyleType::None) return false;
    if (!allow_image_dimensions && !s.computed_sizes.empty()) return false;
    if (allow_image_dimensions) {
        constexpr SizeProperty unsupported[] = {SizeProperty::FlexBasis,
            SizeProperty::InsetTop, SizeProperty::InsetRight, SizeProperty::InsetBottom, SizeProperty::InsetLeft};
        for (const auto property : unsupported) {
            SizeKind kind = SizeKind::Pixels;
            std::int64_t value = 0, percentage = 0;
            if (s.computed_sizes.read_typed(property, kind, value, percentage)) return false;
        }
    }
    for (const auto& background : s.background_layers)
        if (background.kind != BackgroundLayerKind::None) return false;
    return true;
}
bool has_image_constraints(const Style& style) {
    if (style.min_width != -1 || style.max_width != -1 || style.min_height != -1 ||
        style.max_height != -1 || style.min_width_percent != -1 || style.max_width_percent != -1 ||
        style.min_height_percent != -1 || style.max_height_percent != -1) return true;
    constexpr SizeProperty properties[] = {SizeProperty::MinWidth, SizeProperty::MaxWidth,
        SizeProperty::MinHeight, SizeProperty::MaxHeight};
    for (const auto property : properties) {
        SizeKind kind = SizeKind::Pixels;
        std::int64_t value = 0, percentage = 0;
        if (style.computed_sizes.read_typed(property, kind, value, percentage)) return true;
    }
    return false;
}
bool image_dimensions(const Style& style, std::int64_t containing_width,
    std::int64_t& width_units, int& height) {
    if (containing_width < 0 || containing_width > kDimensionPrecisionMaximum) return false;
    const auto resolve_percent = [&](std::int64_t percent, std::int64_t maximum,
        std::int64_t scale) {
        if (percent < 0 || percent > maximum) return false;
        width_units = scale_size_units(containing_width, percent, scale);
        return width_units >= 0 && width_units <= kDimensionPrecisionMaximum;
    };
    SizeKind kind = SizeKind::Pixels;
    std::int64_t value = 0, percentage = 0;
    if (style.computed_sizes.read_typed(SizeProperty::Width, kind, value, percentage)) {
        if (kind == SizeKind::Pixels) width_units = value;
        else if (kind == SizeKind::Percent) {
            if (!resolve_percent(value, 1000000, 100000)) return false;
        }
        else return false;
    } else if (style.width_percent >= 0) {
        if (!resolve_percent(style.width_percent, 1000, 100)) return false;
    } else if (style.width >= 0) {
        width_units = static_cast<std::int64_t>(style.width) * 1000;
    } else {
        return false;
    }
    if (style.computed_sizes.read_typed(SizeProperty::Height, kind, value, percentage)) {
        if (kind != SizeKind::Pixels) return false;
    } else {
        if (style.height < 0 || style.height_percent >= 0) return false;
        value = static_cast<std::int64_t>(style.height) * 1000;
    }
    if (width_units < 0 || width_units > kDimensionPrecisionMaximum || value < 0 ||
        value % 1000 != 0 || value / 1000 > std::numeric_limits<int>::max()) return false;
    height = static_cast<int>(value / 1000);
    return true;
}
bool inline_image_style_eligible(const Node& node, const Style& style) {
    if (node.type != NodeType::Element || node.tag_name != "img" || !node.children.empty() ||
        node.attribute("src").empty() || node.attributes.find("srcset") != node.attributes.end() ||
        node.attributes.find("sizes") != node.attributes.end() || style.text_shadow.enabled ||
        style.object_fit != ObjectFit::Fill || style.object_position.x_percent != 50 ||
        style.object_position.y_percent != 50 || style.image_rendering != ImageRendering::Auto ||
        !neutral(style, false, true)) return false;
    return true;
}
bool eligible_inline_image(const Node& node, const Style& style, std::int64_t containing_width,
    std::int64_t& width_units, int& height, NaturalImageSizingFrame* natural_images,
    ImageIntrinsicSize* retained_intrinsic = nullptr) {
    if (!inline_image_style_eligible(node, style)) return false;
    const bool constrained = has_image_constraints(style);
    if (!constrained && image_dimensions(style, containing_width, width_units, height)) return true;
    if (!natural_images || !natural_images->options.provider.intrinsic_size || style.height_percent >= 0)
        return false;
    const auto authored_width = computed_size_units(style, SizeProperty::Width, containing_width);
    const auto authored_height = computed_size_units(style, SizeProperty::Height, -1);
    const auto min_height = computed_size_units(style, SizeProperty::MinHeight, -1);
    const auto max_height = computed_size_units(style, SizeProperty::MaxHeight, -1);
    if (authored_width < -1 || authored_height < -1 ||
        min_height < -1 || max_height < -1 || (authored_width >= 0 && authored_height >= 0)) return false;
    const auto metadata = natural_images->metadata(node);
    const auto natural = natural_image_content_size(style, metadata, containing_width, -1, 0, 0);
    if (!natural.valid || natural.width < 0 || natural.height < 0 || natural.height % 1000 != 0 ||
        natural.width > kDimensionPrecisionMaximum || natural.height / 1000 > std::numeric_limits<int>::max())
        return false;
    width_units = natural.width;
    height = static_cast<int>(natural.height / 1000);
    if (retained_intrinsic) *retained_intrinsic = metadata;
    return true;
}
std::array<int, 32> host_geometry(const Style& s) {
    return {{s.width, s.height, s.min_width, s.max_width, s.min_height, s.max_height,
        s.width_percent, s.height_percent, s.min_width_percent, s.max_width_percent,
        s.min_height_percent, s.max_height_percent, s.margin.left, s.margin.right,
        s.margin.top, s.margin.bottom, s.padding.left, s.padding.right, s.padding.top,
        s.padding.bottom, s.border_width.left, s.border_width.right, s.border_width.top,
        s.border_width.bottom, s.margin_left_auto, s.margin_right_auto, s.aspect_ratio_width,
        s.aspect_ratio_height, s.aspect_ratio_auto, s.box_sizing_border_box,
        static_cast<int>(s.position_type), static_cast<int>(s.display)}};
}
bool same_font(const LayoutTextFont& a, const LayoutTextFont& b) {
    return a.family == b.family && a.size == b.size && a.weight == b.weight && a.style == b.style;
}
bool same_color(Color a, Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}
SharedInlineProof proof(const LayoutBox& box, std::size_t parent) {
    SharedInlineProof result;
    result.box = &box;
    result.node = box.node;
    result.parent = box.node->parent;
    result.parent_member = parent;
    result.children = box.children.size();
    result.dom_children = box.node->children.size();
    result.source = box.node->text;
    result.tag = box.node->tag_name;
    layout_text_font(box.style, result.font);
    result.color = box.style.color;
    result.background_color = box.style.background_color;
    result.text_shadow = box.style.text_shadow;
    result.font_size = box.style.font_size;
    result.line_height = box.style.line_height;
    result.indent = box.style.text_indent;
    result.align = box.style.text_align;
    result.nowrap = box.style.white_space_nowrap;
    result.anywhere = box.style.overflow_wrap_anywhere;
    result.text_decoration_underline = box.style.text_decoration_underline;
    result.text_decoration_line_through = box.style.text_decoration_line_through;
    return result;
}
struct BudgetRefusal {};
struct ContextBudget {
    const SharedInlineLayoutOptions& limits;
    SharedInlineLayoutStatistics& statistics;
    CoreTextParagraphUsage usage;
    std::size_t charged = 0;
    bool accepted = false;
    ~ContextBudget() {
        statistics.queries += usage.queries;
        statistics.query_bytes += usage.query_bytes;
        statistics.work += usage.work;
        statistics.acquired_bytes += usage.capacity;
        charged += usage.capacity;
        statistics.peak_context_charged_bytes = std::max(statistics.peak_context_charged_bytes, charged);
        if (accepted) ++statistics.accepted;
        else ++statistics.refusals;
    }
    std::size_t remaining_capacity() const {
        return std::min(limits.max_context_capacity_bytes-std::min(charged, limits.max_context_capacity_bytes),
            limits.max_total_capacity_bytes-std::min(statistics.acquired_bytes, limits.max_total_capacity_bytes));
    }
    void capacity(std::size_t bytes) {
        if (bytes > remaining_capacity()) throw BudgetRefusal{};
        charged += bytes;
        statistics.acquired_bytes += bytes;
    }
    void work(std::size_t amount) {
        if (amount > limits.max_work-std::min(statistics.work, limits.max_work)) throw BudgetRefusal{};
        statistics.work += amount;
    }
};
bool preflight(const Node& node, std::size_t depth, std::size_t& members,
    std::size_t& owners, ContextBudget& budget) {
    budget.work(1);
    if (depth > max_depth || members == max_members) return false;
    ++members;
    if (node.type == NodeType::Text || node.tag_name == "br" || node.tag_name == "wbr" ||
        node.tag_name == "img") ++owners;
    for (const auto& child : node.children)
        if (!preflight(*child, depth+1, members, owners, budget)) return false;
    return true;
}
struct Collector {
    SharedInlineSnapshot& snapshot;
    std::vector<CoreTextParagraphSpan> spans;
    std::size_t bytes = 0;
    ContextBudget& budget;
    NaturalImageSizingFrame* natural_images = nullptr;
    std::int64_t containing_width_units = 0;
    std::vector<CoreTextAtomicGeometry> atomic_geometry;
    bool collect(LayoutBox& box, std::size_t parent, std::size_t depth, std::size_t dom_index = 0, std::size_t render_index = 0) {
        budget.work(1);
        if (!box.node || depth > max_depth || snapshot.members.size() >= max_members) return false;
        const bool text = box.node->type == NodeType::Text;
        const bool marker = box.node->tag_name == "br" || box.node->tag_name == "wbr";
        const bool image = depth && box.node->tag_name == "img";
        std::int64_t image_width_units = 0;
        int image_height = 0;
        ImageIntrinsicSize image_intrinsic{};
        if (image ? !eligible_inline_image(*box.node, box.style, containing_width_units,
                image_width_units, image_height, natural_images, &image_intrinsic) :
                !neutral(box.style, depth == 0, false, true, depth != 0, true)) return false;
        if (depth && !text && !marker && !image && box.node->tag_name != "span") return false;
        if (box.node->text.size() > max_source-bytes) return false;
        bytes += box.node->text.size();
        budget.statistics.source_bytes += box.node->text.size();
        ++budget.statistics.members;
        budget.capacity(std::max<std::size_t>(15, box.node->text.size())+1+
            std::max<std::size_t>(15, box.node->tag_name.size())+1);
        if (image) budget.capacity(std::max<std::size_t>(15, box.node->attribute("src").size())+1);
        LayoutTextFont font;
        if (layout_text_font(box.style, font) != S::Ok) return false;
        if (depth && (!same_font(font, snapshot.members[0].proof.font) ||
            box.style.line_height != snapshot.members[0].proof.line_height ||
            box.style.white_space_nowrap != snapshot.members[0].proof.nowrap ||
            box.style.overflow_wrap_anywhere != snapshot.members[0].proof.anywhere)) return false;
        const auto member = snapshot.members.size();
        snapshot.members.emplace_back();
        snapshot.members.back().proof = proof(box, parent);
        snapshot.members.back().proof.dom_index = dom_index;
        snapshot.members.back().proof.render_index = render_index;
        if (image) {
            auto& image_proof = snapshot.members.back().proof;
            image_proof.atomic_image = true;
            image_proof.image_source = box.node->attribute("src");
            image_proof.image_width_units = image_width_units;
            image_proof.image_height = image_height;
            image_proof.natural_image_sized = image_intrinsic.state == ImageIntrinsicSizeState::Known;
            image_proof.image_intrinsic = image_intrinsic;
        }
        if (text || marker) {
            if (!box.children.empty()) return false;
            if (!snapshot.system->resolve_family_hash(font.family, font.family)) return false;
            CoreTextParagraphSpan span;
            span.font = font;
            span.line_height = box.style.line_height;
            span.source = text ? std::string_view(box.node->text) : std::string_view{};
            if (marker) span.kind = box.node->tag_name == "br"
                ? CoreTextParagraphSpanKind::ForcedBreak : CoreTextParagraphSpanKind::Opportunity;
            spans.push_back(span);
            snapshot.owners.push_back(member);
        } else if (image) {
            if (!box.children.empty()) return false;
            const auto span_owner = spans.size();
            auto& image_proof = snapshot.members.back().proof;
            CoreTextParagraphSpan span;
            span.font = font;
            span.line_height = box.style.line_height;
            span.kind = CoreTextParagraphSpanKind::Atomic;
            spans.push_back(span);
            snapshot.owners.push_back(member);
            image_proof.image_placement.owner = span_owner;
            atomic_geometry.push_back({span_owner, image_proof.image_width_units, image_proof.image_height});
        }
        std::size_t rendered = 0;
        for (std::size_t i = 0; i < box.node->children.size(); ++i) {
            const auto& node = *box.node->children[i];
            if (node.parent != box.node) return false;
            if (rendered < box.children.size() && box.children[rendered]->node == &node) {
                if (!collect(*box.children[rendered], member, depth+1, i, rendered)) return false;
                ++rendered;
                continue;
            }
            // RenderTree intentionally omits collapsible whitespace-only text.
            // Preserve its source-order participation and exact membership here.
            if (node.type != NodeType::Text || !node.children.empty() ||
                (!node.text.empty() && !is_collapsible_whitespace_text(node)) ||
                node.text.size() > max_source-bytes) return false;
            budget.work(1);
            bytes += node.text.size();
            budget.statistics.source_bytes += node.text.size();
            budget.capacity(std::max<std::size_t>(15,node.text.size())+1);
            snapshot.omitted.emplace_back();
            auto& omitted = snapshot.omitted.back();
            omitted.node = &node;
            omitted.parent_member = member;
            omitted.dom_index = i;
            omitted.source = node.text;
            auto source_font = font;
            if (!snapshot.system->resolve_family_hash(source_font.family, source_font.family)) return false;
            spans.push_back({node.text, source_font, box.style.line_height, CoreTextParagraphSpanKind::Text});
            snapshot.owners.push_back(member);
        }
        if (rendered != box.children.size()) return false;
        return true;
    }
};
int floor_pixel(std::int64_t n) {
    return static_cast<int>(n >= 0 ? n/1000 : -((-n+999)/1000));
}
int ceil_pixel(std::int64_t n) { return -floor_pixel(-n); }
Rect fragment_bounds(const CoreTextParagraphFragment& fragment, const CoreTextParagraphLine& line) {
    PreparedTextView packet;
    if (!decode_prepared_text(fragment.packet, packet)) throw std::runtime_error("invalid paragraph packet");
    std::int64_t left = std::numeric_limits<int>::max(), right = std::numeric_limits<int>::min();
    for (std::size_t i = 0; i < packet.size(); ++i) {
        PreparedTextGlyph glyph;
        packet.read(i, glyph);
        left = std::min(left, static_cast<std::int64_t>(glyph.x));
        right = std::max(right, static_cast<std::int64_t>(glyph.x)+glyph.geometry.advance);
    }
    if (!packet.size()) return {};
    const auto x = floor_pixel(left), edge = ceil_pixel(right);
    return {x, line.top, std::max(0, edge-x), line.height};
}
Rect fragment_ink(const CoreTextParagraphFragment& fragment, const CoreTextParagraphLine& line) {
    PreparedTextView packet;
    if (!decode_prepared_text(fragment.packet, packet)) throw std::runtime_error("invalid paragraph packet");
    Rect result;
    for (std::size_t i = 0; i < packet.size(); ++i) {
        PreparedTextGlyph glyph;
        packet.read(i, glyph);
        const auto left = static_cast<std::int64_t>(glyph.x)+glyph.geometry.bearing_x;
        const auto top = static_cast<std::int64_t>(line.baseline)*1000+glyph.geometry.bearing_y;
        const auto x = floor_pixel(left), y = floor_pixel(top);
        result = union_rect(result, {x, y, ceil_pixel(left+glyph.geometry.width)-x,
            ceil_pixel(top+glyph.geometry.height)-y});
    }
    return result;
}
void append_fragment(SharedInlineMember& member, Rect fragment) {
    for (auto& existing : member.fragments) {
        if (existing.y == fragment.y && existing.height == fragment.height &&
            safe_edge(existing.x, existing.width) >= fragment.x &&
            safe_edge(fragment.x, fragment.width) >= existing.x) {
            existing = union_rect(existing, fragment);
            return;
        }
    }
    member.fragments.push_back(fragment);
}
void text_decoration_rects(Rect rect, int font_size, bool line_through, bool underline,
    Rect& strike, Rect& under) {
    const int thickness = std::max(1, font_size / 12);
    const int inset = std::max(0, font_size / 12);
    strike = {safe_add(rect.x, inset), rect.y,
        std::max(0, safe_add(rect.width, safe_negate(safe_add(inset, inset)))), thickness};
    under = strike;
    if (line_through)
        strike.y = safe_add(rect.y, std::max(0, safe_span(thickness, rect.height) / 2));
    if (underline)
        under.y = safe_add(rect.y,
            std::max(0, safe_span(std::max(thickness + 1, font_size / 5), rect.height)));
}
std::size_t text_decoration_command_count(Rect rect, int font_size, bool line_through, bool underline) {
    const int inset = std::max(0, font_size / 12);
    if (std::max(0, safe_add(rect.width, safe_negate(safe_add(inset, inset)))) <= 0) return 0;
    return static_cast<std::size_t>(line_through) + static_cast<std::size_t>(underline);
}
}

bool layout_shared_inline(LayoutBox& host, int x, int y, int width,
    const LayoutEngineOptions& options, int& height, std::int64_t precise_width_units,
    NaturalImageSizingFrame* natural_images) noexcept {
    if (!host.node || !options.text_system || !options.text_system->ready() || host.children.empty() ||
        !neutral(host.style, true, false, true, false, true)) return false;
    SharedInlineLayoutStatistics local_statistics;
    auto& statistics = options.shared_inline.statistics ? *options.shared_inline.statistics : local_statistics;
    if (statistics.attempts >= options.shared_inline.max_attempts) { ++statistics.refusals;return false; }
    ++statistics.attempts;
    ContextBudget budget{options.shared_inline, statistics, {}, 0, false};
    try {
        std::size_t member_count = 0, owner_count = 0;
        if (!preflight(*host.node, 0, member_count, owner_count, budget) || member_count <= 2 || owner_count == 0) return false;
        budget.capacity(sizeof(SharedInlineSnapshot)+member_count*(sizeof(SharedInlineMember)+sizeof(SharedInlineOmittedSource))+
            owner_count*(sizeof(std::size_t)+sizeof(CoreTextParagraphSpan)));
        auto snapshot = std::make_shared<SharedInlineSnapshot>();
        snapshot->system = options.text_system;
        snapshot->generation = options.text_system->generation();
        snapshot->dom_root = host.node;
        std::size_t ancestors = 0;
        while (snapshot->dom_root && snapshot->dom_root->parent) {
            if (++ancestors > 128) return false;
            snapshot->dom_root = snapshot->dom_root->parent;
        }
        if (!snapshot->dom_root) return false;
        snapshot->dom_generation = snapshot->dom_root->mutation_generation;
        snapshot->content_offset_x = safe_add(host.style.padding.left, host.style.border_width.left);
        snapshot->content_offset_y = safe_add(host.style.padding.top, host.style.border_width.top);
        CoreTextParagraphOptions paragraph_options;
        paragraph_options.width_units = precise_width_units < 0
            ? static_cast<std::int64_t>(std::max(0, width))*1000 : precise_width_units;
        snapshot->width_units = paragraph_options.width_units;
        snapshot->members.reserve(member_count);
        snapshot->omitted.reserve(member_count);
        snapshot->owners.reserve(owner_count);
        Collector collector{*snapshot, std::vector<CoreTextParagraphSpan>(0), 0, budget,
            natural_images, paragraph_options.width_units, std::vector<CoreTextAtomicGeometry>(0)};
        collector.spans.reserve(owner_count);
        collector.atomic_geometry.reserve(owner_count);
        budget.capacity((snapshot->members.capacity()-member_count)*sizeof(SharedInlineMember)+
            (snapshot->omitted.capacity()-member_count)*sizeof(SharedInlineOmittedSource)+
            (snapshot->owners.capacity()-owner_count)*sizeof(std::size_t)+
            (collector.spans.capacity()-owner_count)*sizeof(CoreTextParagraphSpan)+
            collector.atomic_geometry.capacity()*sizeof(CoreTextAtomicGeometry));
        if (!collector.collect(host, 0, 0) || (collector.spans.empty() || snapshot->members.size() <= 2)) return false;
        paragraph_options.strut = snapshot->members[0].proof.font;
        if (!snapshot->system->resolve_family_hash(paragraph_options.strut.family, paragraph_options.strut.family)) return false;
        snapshot->host_geometry = host_geometry(host.style);
        snapshot->host_computed_sizes = host.style.computed_sizes;
        paragraph_options.line_height = host.style.line_height;
        paragraph_options.indent = host.style.text_indent;
        paragraph_options.align = host.style.text_align;
        paragraph_options.nowrap = host.style.white_space_nowrap;
        paragraph_options.anywhere = host.style.overflow_wrap_anywhere;
        CoreTextLayoutLimits limits;
        limits.max_prepared_glyphs = options.max_prepared_text_glyphs;
        limits.max_capacity_bytes = budget.remaining_capacity();
        limits.max_line_work = options.shared_inline.max_work-std::min(statistics.work, options.shared_inline.max_work);
        limits.max_queries = options.shared_inline.max_queries-std::min(statistics.queries, options.shared_inline.max_queries);
        limits.max_query_bytes = options.shared_inline.max_query_bytes-std::min(statistics.query_bytes, options.shared_inline.max_query_bytes);
        CoreTextParagraphAtomics atomics;
        if (build_core_text_paragraph(*snapshot->system, collector.spans.data(), collector.spans.size(),
            paragraph_options, snapshot->paragraph, limits, &budget.usage,
            collector.atomic_geometry.data(), collector.atomic_geometry.size(), &atomics) != S::Ok) return false;
        snapshot->atomic_placements = std::move(atomics.placements);
        snapshot->usage = budget.usage;
        statistics.queries += budget.usage.queries;
        statistics.query_bytes += budget.usage.query_bytes;
        statistics.work += budget.usage.work;
        budget.usage.queries = 0;
        budget.usage.query_bytes = 0;
        budget.usage.work = 0;
        // Transfer the planner charge now so subsequent geometry allocations
        // share the remaining operation/context capacity. Never refund it.
        budget.capacity(budget.usage.capacity);
        budget.usage.capacity = 0;
        const auto fragment_capacity = snapshot->paragraph.lines.size()+snapshot->atomic_placements.size();
        for (auto& member : snapshot->members) {
            budget.capacity(fragment_capacity*sizeof(Rect));
            member.fragments.reserve(fragment_capacity);
            budget.capacity((member.fragments.capacity()-fragment_capacity)*sizeof(Rect));
        }
        for (const auto& fragment : snapshot->paragraph.fragments) {
            budget.work((fragment.end-fragment.begin)*2+max_depth);
            const auto bounds = fragment_bounds(fragment, snapshot->paragraph.lines[fragment.line]);
            auto ink = fragment_ink(fragment, snapshot->paragraph.lines[fragment.line]);
            auto member = snapshot->owners[fragment.owner];
            const auto& owner_proof = snapshot->members[member].proof;
            const auto& owner_shadow = owner_proof.text_shadow;
            Rect shadow_ink = ink;
            if (owner_shadow.enabled) {
                shadow_ink.x = safe_add(shadow_ink.x, owner_shadow.offset_x);
                shadow_ink.y = safe_add(shadow_ink.y, owner_shadow.offset_y);
            }
            Rect strike, underline;
            text_decoration_rects(bounds, owner_proof.font_size,
                owner_proof.text_decoration_line_through, owner_proof.text_decoration_underline,
                strike, underline);
            if (owner_proof.text_decoration_line_through && strike.width > 0)
                ink = union_rect(ink, strike);
            if (owner_proof.text_decoration_underline && underline.width > 0)
                ink = union_rect(ink, underline);
            for (;;) {
                auto& scope = snapshot->members[member];
                scope.ink_bounds = union_rect(scope.ink_bounds, ink);
                if (owner_shadow.enabled) scope.ink_bounds = union_rect(scope.ink_bounds, shadow_ink);
                if (member == 0) break;
                append_fragment(scope, bounds);
                member = scope.proof.parent_member;
            }
        }
        for (const auto& placement : snapshot->atomic_placements) {
            budget.work(max_depth+1);
            const auto member_index = snapshot->owners[placement.owner];
            auto& image = snapshot->members[member_index];
            image.proof.image_placement = placement;
            const auto left = floor_pixel(placement.x);
            const auto right = ceil_pixel(static_cast<std::int64_t>(placement.x)+placement.advance);
            const Rect bounds{left, placement.top, std::max(0, right-left), placement.extent};
            auto current = member_index;
            for (;;) {
                auto& scope = snapshot->members[current];
                scope.ink_bounds = union_rect(scope.ink_bounds, bounds);
                if (current == 0) break;
                append_fragment(scope, bounds);
                current = scope.proof.parent_member;
            }
        }
        std::vector<std::shared_ptr<const SharedInlineBinding>> bindings(0);
        budget.capacity(snapshot->members.size()*(sizeof(std::shared_ptr<const SharedInlineBinding>)+
            sizeof(SharedInlineBinding)+2*sizeof(void*)));
        bindings.reserve(snapshot->members.size());
        budget.capacity((bindings.capacity()-snapshot->members.size())*sizeof(bindings[0]));
        snapshot->retained_bytes = sizeof(SharedInlineSnapshot)+snapshot->members.capacity()*sizeof(SharedInlineMember)+
            snapshot->owners.capacity()*sizeof(std::size_t)+snapshot->omitted.capacity()*sizeof(SharedInlineOmittedSource)+
            snapshot->atomic_placements.capacity()*sizeof(CoreTextAtomicPlacement)+snapshot->usage.capacity;
        for (std::size_t i = 0; i < snapshot->members.size(); ++i) {
            auto& member = snapshot->members[i];
            bool first = true;
            for (auto fragment : member.fragments) {
                member.bounds = first ? fragment : union_rect(member.bounds, fragment);
                first = false;
            }
            snapshot->retained_bytes += member.fragments.capacity()*sizeof(Rect)+member.proof.source.capacity()+
                member.proof.tag.capacity()+member.proof.image_source.capacity()+sizeof(SharedInlineBinding)+2*sizeof(void*);
            bindings.push_back(std::make_shared<SharedInlineBinding>(SharedInlineBinding{snapshot, i}));
        }
        for (const auto& source : snapshot->omitted)
            snapshot->retained_bytes += source.source.capacity()+1;
        if (snapshot->generation != snapshot->system->generation()) return false;
        // Publication uses only noexcept moves and scalar assignments. This also
        // composes with the enclosing speculative layout mutation journal.
        for (std::size_t i = 0; i < snapshot->members.size(); ++i) {
            auto& member = snapshot->members[i];
            auto& box = *const_cast<LayoutBox*>(member.proof.box);
            box.text_layout_cache.shared_inline = std::move(bindings[i]);
            if (i == 0) continue;
            if (member.proof.atomic_image) {
                const auto origin_x = static_cast<std::int64_t>(x);
                const auto origin_y = static_cast<std::int64_t>(y);
                const auto image_x = origin_x*1000+member.proof.image_placement.x;
                const auto image_y = origin_y*1000+static_cast<std::int64_t>(member.proof.image_placement.top)*1000;
                const auto left = floor_pixel(image_x);
                const auto right = ceil_pixel(image_x+member.proof.image_placement.advance);
                box.rect = {left, clamp_int64_to_int(image_y/1000), std::max(0, right-left),
                    member.proof.image_placement.extent};
                box.content_geometry = {image_x, image_y, member.proof.image_placement.advance,
                    static_cast<std::int64_t>(member.proof.image_placement.extent)*1000, true};
            } else {
                box.rect = member.bounds;
                box.rect.x = safe_add(x, box.rect.x);
                box.rect.y = safe_add(y, box.rect.y);
                box.content_geometry = {static_cast<std::int64_t>(box.rect.x)*1000,
                    static_cast<std::int64_t>(box.rect.y)*1000, static_cast<std::int64_t>(box.rect.width)*1000,
                    static_cast<std::int64_t>(box.rect.height)*1000, false};
            }
            box.text_layout_cache.valid = false;
        }
        height = snapshot->paragraph.height;
        budget.accepted = true;
        return true;
    } catch (const BudgetRefusal&) { return false; }
      catch (const std::bad_alloc&) { return false; }
      catch (const std::length_error&) { return false; }
      catch (const std::runtime_error&) { return false; }
}

void retire_shared_inline(LayoutBox& host) noexcept {
    if (!host.text_layout_cache.shared_inline) return;
    const auto snapshot = host.text_layout_cache.shared_inline->snapshot;
    const auto retire = [&](const auto& self, LayoutBox& box) -> void {
        if (box.text_layout_cache.shared_inline &&
            box.text_layout_cache.shared_inline->snapshot == snapshot) {
            box.text_layout_cache.shared_inline.reset();
            box.text_layout_cache.valid = false;
        }
        for (auto& child : box.children) self(self, *child);
    };
    retire(retire, host);
}

bool shared_inline_current(const SharedInlineBinding& binding, const TextSystem* system) noexcept {
    const auto& snapshot = *binding.snapshot;
    if (system != snapshot.system || !system || system->generation() != snapshot.generation) return false;
    const auto* root = snapshot.members[0].proof.node;
    if (!root) return false;
    std::size_t ancestors = 0;
    while (root->parent) {
        if (++ancestors > 128) return false;
        root = root->parent;
    }
    if (root != snapshot.dom_root || root->mutation_generation != snapshot.dom_generation) return false;
    const auto& host = *snapshot.members[0].proof.box;
    if ((host.content_geometry.width >= 0 && host.content_geometry.width != snapshot.width_units) ||
        host_geometry(host.style) != snapshot.host_geometry ||
        !host.style.computed_sizes.equals(snapshot.host_computed_sizes)) return false;
    const auto origin_x = static_cast<std::int64_t>(host.rect.x)+snapshot.content_offset_x;
    const auto origin_y = static_cast<std::int64_t>(host.rect.y)+snapshot.content_offset_y;
    for (std::size_t i = 0; i < snapshot.members.size(); ++i) {
        const auto& p = snapshot.members[i].proof;
        // Validate membership against the still-owned layout tree before any
        // descendant Node dereference. Detached/destroyed child pointers fail.
        if (i && (p.parent_member >= i ||
            snapshot.members[p.parent_member].proof.node->children.size() !=
            snapshot.members[p.parent_member].proof.dom_children ||
            p.dom_index >= snapshot.members[p.parent_member].proof.dom_children ||
            snapshot.members[p.parent_member].proof.node->children[p.dom_index].get() != p.node)) return false;
        if (i) {
            const auto& parent_box = *snapshot.members[p.parent_member].proof.box;
            if (p.render_index >= parent_box.children.size() ||
                parent_box.children[p.render_index].get() != p.box) return false;
        }
        const auto& box = *p.box;
        if (!p.node) return false;
        std::int64_t image_width = 0;
        int image_height = 0;
        const bool image_dimensions_current = p.natural_image_sized
            ? [&] {
                const auto natural = natural_image_content_size(box.style, p.image_intrinsic,
                    snapshot.width_units, -1, 0, 0);
                return natural.valid && natural.width == p.image_width_units &&
                    natural.height == static_cast<std::int64_t>(p.image_height) * 1000;
            }()
            : image_dimensions(box.style, snapshot.width_units, image_width, image_height) &&
                image_width == p.image_width_units && image_height == p.image_height;
        const bool style_current = p.atomic_image
            ? inline_image_style_eligible(*p.node, box.style) && image_dimensions_current &&
                p.node->attribute("src") == p.image_source
            : neutral(box.style, i == 0, false, true, i != 0, true);
        if (box.node != p.node || !p.node || p.node->parent != p.parent ||
            p.node->children.size() != p.dom_children ||
            box.children.size() != p.children || p.node->text != p.source || p.node->tag_name != p.tag ||
            !style_current) return false;
        const auto& bounds = snapshot.members[i].bounds;
        if (i && (box.rect.width != bounds.width || box.rect.height != bounds.height ||
            static_cast<std::int64_t>(box.rect.x)-bounds.x != origin_x ||
            static_cast<std::int64_t>(box.rect.y)-bounds.y != origin_y)) return false;
        if (i && p.atomic_image) {
            const auto expected_x = origin_x*1000+p.image_placement.x;
            const auto expected_y = origin_y*1000+
                static_cast<std::int64_t>(p.image_placement.top)*1000;
            if (box.content_geometry.x != expected_x || box.content_geometry.y != expected_y ||
                box.content_geometry.width != p.image_placement.advance ||
                box.content_geometry.height() != static_cast<std::int64_t>(p.image_placement.extent)*1000 ||
                !box.content_geometry.height_definite()) return false;
        }
        LayoutTextFont font;
        if (layout_text_font(box.style, font) != S::Ok || !same_font(font, p.font) ||
            !same_color(box.style.color, p.color) || !same_color(box.style.background_color, p.background_color) ||
            box.style.text_shadow.enabled != p.text_shadow.enabled ||
            box.style.text_shadow.uses_current_color != p.text_shadow.uses_current_color ||
            box.style.text_shadow.offset_x != p.text_shadow.offset_x ||
            box.style.text_shadow.offset_y != p.text_shadow.offset_y ||
            box.style.text_shadow.blur != p.text_shadow.blur ||
            !same_color(box.style.text_shadow.color, p.text_shadow.color) ||
            box.style.font_size != p.font_size ||
            box.style.text_decoration_underline != p.text_decoration_underline ||
            box.style.text_decoration_line_through != p.text_decoration_line_through ||
            box.style.line_height != p.line_height ||
            box.style.text_indent != p.indent || box.style.text_align != p.align ||
            box.style.white_space_nowrap != p.nowrap || box.style.overflow_wrap_anywhere != p.anywhere) return false;
    }
    for (const auto& source : snapshot.omitted) {
        const auto* parent = snapshot.members[source.parent_member].proof.node;
        if (source.dom_index >= parent->children.size() ||
            parent->children[source.dom_index].get() != source.node || source.node->parent != parent ||
            source.node->type != NodeType::Text || !source.node->children.empty() ||
            source.node->text != source.source) return false;
    }
    return true;
}

bool shared_inline_contains(const LayoutBox& box, int x, int y) noexcept {
    const auto& binding = *box.text_layout_cache.shared_inline;
    if (!shared_inline_current(binding, binding.snapshot->system)) return false;
    if (binding.member == 0) return contains(box.rect, x, y);
    const auto& member = binding.snapshot->members[binding.member];
    const auto dx = static_cast<std::int64_t>(box.rect.x)-member.bounds.x;
    const auto dy = static_cast<std::int64_t>(box.rect.y)-member.bounds.y;
    for (auto rect : member.fragments) {
        rect.x = clamp_int64_to_int(rect.x+dx);
        rect.y = clamp_int64_to_int(rect.y+dy);
        if (contains(rect, x, y)) return true;
    }
    return false;
}

Rect shared_inline_damage_bounds(const LayoutBox& box) noexcept {
    const auto& binding = *box.text_layout_cache.shared_inline;
    const auto& snapshot = *binding.snapshot;
    const auto& member = snapshot.members[binding.member];
    auto ink = member.ink_bounds;
    const auto dx = binding.member == 0 ? static_cast<std::int64_t>(box.rect.x)+snapshot.content_offset_x
        : static_cast<std::int64_t>(box.rect.x)-member.bounds.x;
    const auto dy = binding.member == 0 ? static_cast<std::int64_t>(box.rect.y)+snapshot.content_offset_y
        : static_cast<std::int64_t>(box.rect.y)-member.bounds.y;
    ink.x = clamp_int64_to_int(ink.x+dx);
    ink.y = clamp_int64_to_int(ink.y+dy);
    return union_rect(box.rect, ink);
}

std::size_t shared_inline_command_count(const SharedInlineBinding& binding,
    const TextSystem* system) noexcept {
    if (!shared_inline_current(binding, system) || binding.member != 0) return 0;
    const auto& snapshot = *binding.snapshot;
    std::size_t count = snapshot.paragraph.fragments.size() + snapshot.atomic_placements.size();
    const auto max_count = std::numeric_limits<std::size_t>::max();
    for (const auto& fragment : snapshot.paragraph.fragments) {
        if (fragment.owner >= snapshot.owners.size()) return 0;
        const auto member = snapshot.owners[fragment.owner];
        if (member >= snapshot.members.size()) return 0;
        if (snapshot.members[member].proof.text_shadow.enabled) {
            if (count == max_count) return 0;
            ++count;
        }
        const auto& proof = snapshot.members[member].proof;
        const auto bounds = fragment_bounds(fragment, snapshot.paragraph.lines[fragment.line]);
        const auto decorations = text_decoration_command_count(bounds, proof.font_size,
            proof.text_decoration_line_through, proof.text_decoration_underline);
        if (decorations > max_count-count) return 0;
        count += decorations;
    }
    for (std::size_t member = 1; member < snapshot.members.size(); ++member) {
        if (snapshot.members[member].proof.background_color.a == 0) continue;
        if (snapshot.members[member].fragments.size() > max_count-count) return 0;
        count += snapshot.members[member].fragments.size();
    }
    return count;
}

bool emit_shared_inline(const LayoutBox& box, DisplayList& display,
    SharedInlineImageEmitter image_emitter, void* image_context) noexcept {
    try {
    const auto& binding = *box.text_layout_cache.shared_inline;
    const auto& snapshot = *binding.snapshot;
    if (binding.member != 0 || !shared_inline_current(binding, snapshot.system) ||
        (!snapshot.atomic_placements.empty() && !image_emitter)) return false;
    const int origin_x = safe_add(box.rect.x, snapshot.content_offset_x);
    const int origin_y = safe_add(box.rect.y, snapshot.content_offset_y);
    DisplayList draft(0);
    const auto command_count = shared_inline_command_count(binding, snapshot.system);
    if (command_count == 0 && (!snapshot.paragraph.fragments.empty() ||
        !snapshot.atomic_placements.empty())) return false;
    draft.reserve(command_count);
    for (std::size_t member_index = 1; member_index < snapshot.members.size(); ++member_index) {
        const auto& member = snapshot.members[member_index];
        if (member.proof.background_color.a == 0) continue;
        for (auto rect : member.fragments) {
            rect.x = safe_add(rect.x, origin_x);
            rect.y = safe_add(rect.y, origin_y);
            if (rect.width <= 0 || rect.height <= 0) continue;
            DisplayCommand command;
            command.set_type(DisplayCommandType::FillRect);
            command.rect = rect;
            command.color = member.proof.background_color;
            command.color2 = command.color;
            draft.push_back(std::move(command));
        }
    }
    std::size_t fragment_index = 0, atomic_index = 0;
    for (std::size_t line_index = 0; line_index < snapshot.paragraph.lines.size(); ++line_index) {
        const auto has_fragment = [&] {
            return fragment_index < snapshot.paragraph.fragments.size() &&
                snapshot.paragraph.fragments[fragment_index].line == line_index;
        };
        const auto has_atomic = [&] {
            return atomic_index < snapshot.atomic_placements.size() &&
                snapshot.atomic_placements[atomic_index].line == line_index;
        };
        while (has_fragment() || has_atomic()) {
            const bool emit_atomic = has_atomic() && (!has_fragment() ||
                snapshot.atomic_placements[atomic_index].owner <
                    snapshot.paragraph.fragments[fragment_index].owner);
            if (emit_atomic) {
                const auto& placement = snapshot.atomic_placements[atomic_index++];
                if (placement.owner >= snapshot.owners.size()) return false;
                const auto member_index = snapshot.owners[placement.owner];
                if (member_index >= snapshot.members.size()) return false;
                const auto* image_box = snapshot.members[member_index].proof.box;
                if (!image_box || !snapshot.members[member_index].proof.atomic_image ||
                    !image_emitter(*image_box, draft, image_context)) return false;
                continue;
            }
            const auto& fragment = snapshot.paragraph.fragments[fragment_index++];
            if (fragment.owner >= snapshot.owners.size() || fragment.line >= snapshot.paragraph.lines.size()) return false;
            const auto& member = snapshot.members[snapshot.owners[fragment.owner]];
            const auto& line = snapshot.paragraph.lines[fragment.line];
            auto rect = union_rect(fragment_bounds(fragment, line), fragment_ink(fragment, line));
            rect.x = safe_add(rect.x, origin_x);
            rect.y = safe_add(rect.y, origin_y);
            const auto& shadow = member.proof.text_shadow;
            if (shadow.enabled) {
                Rect shadow_rect = rect;
                shadow_rect.x = safe_add(shadow_rect.x, shadow.offset_x);
                shadow_rect.y = safe_add(shadow_rect.y, shadow.offset_y);
                DisplayCommand shadow_command;
                shadow_command.set_type(DisplayCommandType::PreparedText);
                shadow_command.rect = shadow_rect;
                shadow_command.color = shadow.uses_current_color ? member.proof.color : shadow.color;
                shadow_command.text = fragment.packet;
                set_prepared_text_origin(shadow_command,
                    safe_add(origin_x, shadow.offset_x),
                    safe_add(safe_add(origin_y, line.baseline), shadow.offset_y));
                draft.push_back(std::move(shadow_command));
            }
            DisplayCommand command;
            command.set_type(DisplayCommandType::PreparedText);
            command.rect = rect;
            command.color = member.proof.color;
            command.text = fragment.packet;
            set_prepared_text_origin(command, origin_x, safe_add(origin_y, line.baseline));
            draft.push_back(std::move(command));
            auto decoration_bounds = fragment_bounds(fragment, line);
            decoration_bounds.x = safe_add(decoration_bounds.x, origin_x);
            decoration_bounds.y = safe_add(decoration_bounds.y, origin_y);
            Rect strike, underline;
            text_decoration_rects(decoration_bounds, member.proof.font_size,
                member.proof.text_decoration_line_through, member.proof.text_decoration_underline,
                strike, underline);
            if (member.proof.text_decoration_line_through && strike.width > 0) {
                DisplayCommand strike_command;
                strike_command.set_type(DisplayCommandType::FillRect);
                strike_command.rect = strike;
                strike_command.color = member.proof.color;
                strike_command.color2 = strike_command.color;
                draft.push_back(std::move(strike_command));
            }
            if (member.proof.text_decoration_underline && underline.width > 0) {
                DisplayCommand underline_command;
                underline_command.set_type(DisplayCommandType::FillRect);
                underline_command.rect = underline;
                underline_command.color = member.proof.color;
                underline_command.color2 = underline_command.color;
                draft.push_back(std::move(underline_command));
            }
        }
    }
    if (fragment_index != snapshot.paragraph.fragments.size() ||
        atomic_index != snapshot.atomic_placements.size()) return false;
    display.reserve(display.size()+draft.size());
    for (auto& command : draft) display.push_back(std::move(command));
    return true;
    } catch (const std::bad_alloc&) { return false; }
      catch (const std::length_error&) { return false; }
      catch (const std::runtime_error&) { return false; }
}
}
