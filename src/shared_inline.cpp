#include "render_core/text_normalization.h"
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
bool neutral(const Style& s, bool host) {
    if (s.visibility_hidden || s.text_transform != TextTransform::None || s.letter_spacing != 0 ||
        s.text_overflow_ellipsis || s.text_shadow.enabled || s.text_decoration_underline ||
        s.text_decoration_line_through || s.before_content_kind != GeneratedContentKind::None ||
        s.after_content_kind != GeneratedContentKind::None) return false;
    if (host) return s.display == Display::Block;
    if (s.display != Display::Inline || s.position_type != PositionType::Static ||
        s.opacity != 1.0F || !s.transform.empty() || !s.overflow.empty() ||
        s.background_color.a != 0 || s.box_shadow.enabled || s.outline_width != 0 ||
        !empty_edges(s.margin) || !empty_edges(s.padding) || !empty_edges(s.border_width) ||
        s.width != -1 || s.height != -1 || s.width_percent != -1 || s.height_percent != -1 ||
        s.min_width != -1 || s.max_width != -1 || s.min_height != -1 || s.max_height != -1 ||
        s.min_width_percent != -1 || s.max_width_percent != -1 ||
        s.min_height_percent != -1 || s.max_height_percent != -1 ||
        s.border_radius != 0 || s.border_radius_percent != -1 || s.border_radius_axes_specified ||
        !s.z_index_auto || s.aspect_ratio_width != 0 || s.aspect_ratio_height != 0 ||
        s.list_style_type != ListStyleType::None || !s.computed_sizes.empty()) return false;
    for (const auto& background : s.background_layers)
        if (background.kind != BackgroundLayerKind::None) return false;
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
    result.line_height = box.style.line_height;
    result.indent = box.style.text_indent;
    result.align = box.style.text_align;
    result.nowrap = box.style.white_space_nowrap;
    result.anywhere = box.style.overflow_wrap_anywhere;
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
    if (node.type == NodeType::Text || node.tag_name == "br" || node.tag_name == "wbr") ++owners;
    for (const auto& child : node.children)
        if (!preflight(*child, depth+1, members, owners, budget)) return false;
    return true;
}
struct Collector {
    SharedInlineSnapshot& snapshot;
    std::vector<CoreTextParagraphSpan> spans;
    std::size_t bytes = 0;
    ContextBudget& budget;
    bool collect(LayoutBox& box, std::size_t parent, std::size_t depth, std::size_t dom_index = 0, std::size_t render_index = 0) {
        budget.work(1);
        if (!box.node || depth > max_depth || snapshot.members.size() >= max_members ||
            !neutral(box.style, depth == 0)) return false;
        const bool text = box.node->type == NodeType::Text;
        const bool marker = box.node->tag_name == "br" || box.node->tag_name == "wbr";
        if (depth && !text && !marker && box.node->tag_name != "span") return false;
        if (box.node->text.size() > max_source-bytes) return false;
        bytes += box.node->text.size();
        budget.statistics.source_bytes += box.node->text.size();
        ++budget.statistics.members;
        budget.capacity(std::max<std::size_t>(15, box.node->text.size())+1+
            std::max<std::size_t>(15, box.node->tag_name.size())+1);
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
}

bool layout_shared_inline(LayoutBox& host, int x, int y, int width,
    const LayoutEngineOptions& options, int& height, std::int64_t precise_width_units) noexcept {
    if (!host.node || !options.text_system || !options.text_system->ready() || host.children.empty() ||
        !neutral(host.style, true)) return false;
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
        snapshot->members.reserve(member_count);
        snapshot->omitted.reserve(member_count);
        snapshot->owners.reserve(owner_count);
        Collector collector{*snapshot, std::vector<CoreTextParagraphSpan>(0), 0, budget};
        collector.spans.reserve(owner_count);
        budget.capacity((snapshot->members.capacity()-member_count)*sizeof(SharedInlineMember)+
            (snapshot->omitted.capacity()-member_count)*sizeof(SharedInlineOmittedSource)+
            (snapshot->owners.capacity()-owner_count)*sizeof(std::size_t)+
            (collector.spans.capacity()-owner_count)*sizeof(CoreTextParagraphSpan));
        if (!collector.collect(host, 0, 0) || (collector.spans.empty() || snapshot->members.size() <= 2)) return false;
        CoreTextParagraphOptions paragraph_options;
        paragraph_options.strut = snapshot->members[0].proof.font;
        if (!snapshot->system->resolve_family_hash(paragraph_options.strut.family, paragraph_options.strut.family)) return false;
        paragraph_options.width_units = precise_width_units < 0 ? static_cast<std::int64_t>(std::max(0, width))*1000 : precise_width_units;
        snapshot->width_units = paragraph_options.width_units;
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
        if (build_core_text_paragraph(*snapshot->system, collector.spans.data(), collector.spans.size(),
            paragraph_options, snapshot->paragraph, limits, &budget.usage) != S::Ok) return false;
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
        for (auto& member : snapshot->members) {
            budget.capacity(snapshot->paragraph.lines.size()*sizeof(Rect));
            member.fragments.reserve(snapshot->paragraph.lines.size());
            budget.capacity((member.fragments.capacity()-snapshot->paragraph.lines.size())*sizeof(Rect));
        }
        for (const auto& fragment : snapshot->paragraph.fragments) {
            budget.work((fragment.end-fragment.begin)*2+max_depth);
            const auto bounds = fragment_bounds(fragment, snapshot->paragraph.lines[fragment.line]);
            const auto ink = fragment_ink(fragment, snapshot->paragraph.lines[fragment.line]);
            auto member = snapshot->owners[fragment.owner];
            for (;;) {
                auto& scope = snapshot->members[member];
                scope.ink_bounds = union_rect(scope.ink_bounds, ink);
                if (member == 0) break;
                append_fragment(scope, bounds);
                member = scope.proof.parent_member;
            }
        }
        std::vector<std::shared_ptr<const SharedInlineBinding>> bindings(0);
        budget.capacity(snapshot->members.size()*(sizeof(std::shared_ptr<const SharedInlineBinding>)+
            sizeof(SharedInlineBinding)+2*sizeof(void*)));
        bindings.reserve(snapshot->members.size());
        budget.capacity((bindings.capacity()-snapshot->members.size())*sizeof(bindings[0]));
        snapshot->retained_bytes = sizeof(SharedInlineSnapshot)+snapshot->members.capacity()*sizeof(SharedInlineMember)+
            snapshot->owners.capacity()*sizeof(std::size_t)+snapshot->omitted.capacity()*sizeof(SharedInlineOmittedSource)+snapshot->usage.capacity;
        for (std::size_t i = 0; i < snapshot->members.size(); ++i) {
            auto& member = snapshot->members[i];
            bool first = true;
            for (auto fragment : member.fragments) {
                member.bounds = first ? fragment : union_rect(member.bounds, fragment);
                first = false;
            }
            snapshot->retained_bytes += member.fragments.capacity()*sizeof(Rect)+member.proof.source.capacity()+
                member.proof.tag.capacity()+sizeof(SharedInlineBinding)+2*sizeof(void*);
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
            box.rect = member.bounds;
            box.rect.x = safe_add(x, box.rect.x);
            box.rect.y = safe_add(y, box.rect.y);
            box.content_geometry = {static_cast<std::int64_t>(box.rect.x)*1000,
                static_cast<std::int64_t>(box.rect.y)*1000, static_cast<std::int64_t>(box.rect.width)*1000,
                static_cast<std::int64_t>(box.rect.height)*1000, false};
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
        if (box.node != p.node || !p.node || p.node->parent != p.parent ||
            p.node->children.size() != p.dom_children ||
            box.children.size() != p.children || p.node->text != p.source || p.node->tag_name != p.tag ||
            !neutral(box.style, i == 0)) return false;
        const auto& bounds = snapshot.members[i].bounds;
        if (i && (box.rect.width != bounds.width || box.rect.height != bounds.height ||
            static_cast<std::int64_t>(box.rect.x)-bounds.x != origin_x ||
            static_cast<std::int64_t>(box.rect.y)-bounds.y != origin_y)) return false;
        LayoutTextFont font;
        if (layout_text_font(box.style, font) != S::Ok || !same_font(font, p.font) ||
            !same_color(box.style.color, p.color) || box.style.line_height != p.line_height ||
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

bool emit_shared_inline(const LayoutBox& box, DisplayList& display) noexcept {
    try {
    const auto& binding = *box.text_layout_cache.shared_inline;
    const auto& snapshot = *binding.snapshot;
    if (binding.member != 0) return false;
    const int origin_x = safe_add(box.rect.x, snapshot.content_offset_x);
    const int origin_y = safe_add(box.rect.y, snapshot.content_offset_y);
    DisplayList draft(0);
    draft.reserve(snapshot.paragraph.fragments.size());
    for (const auto& fragment : snapshot.paragraph.fragments) {
        const auto& member = snapshot.members[snapshot.owners[fragment.owner]];
        const auto& line = snapshot.paragraph.lines[fragment.line];
        auto rect = union_rect(fragment_bounds(fragment, line), fragment_ink(fragment, line));
        rect.x = safe_add(rect.x, origin_x);
        rect.y = safe_add(rect.y, origin_y);
        DisplayCommand command;
        command.set_type(DisplayCommandType::PreparedText);
        command.rect = rect;
        command.color = member.proof.color;
        command.text = fragment.packet;
        set_prepared_text_origin(command, origin_x, safe_add(origin_y, line.baseline));
        draft.push_back(std::move(command));
    }
    display.reserve(display.size()+draft.size());
    for (auto& command : draft) display.push_back(std::move(command));
    return true;
    } catch (const std::bad_alloc&) { return false; }
      catch (const std::length_error&) { return false; }
      catch (const std::runtime_error&) { return false; }
}
}
