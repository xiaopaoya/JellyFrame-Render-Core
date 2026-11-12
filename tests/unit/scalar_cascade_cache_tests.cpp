#include "../../src/scalar_cascade_cache.h"
#include "render_core/css_parser.h"
#include "render_core/render_tree.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <tuple>

// Fail one allocation at a time so cache fallback can exercise the ordinary
// resolver completely after a cache-local allocation failure.
static std::ptrdiff_t allocation_failure_countdown = -1;
void *operator new(std::size_t size) {
    if (allocation_failure_countdown == 0) {
        allocation_failure_countdown = -1;
        throw std::bad_alloc();
    }
    if (allocation_failure_countdown > 0)
        --allocation_failure_countdown;
    if (void *data = std::malloc(size ? size : 1))
        return data;
    throw std::bad_alloc();
}
void operator delete(void *data) noexcept {
    std::free(data);
}
void operator delete(void *data, std::size_t) noexcept {
    std::free(data);
}

using namespace jellyframe;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
StyleResolverOptions enabled() {
    StyleResolverOptions o;
    o.max_scalar_cache_entries = 16;
    o.max_scalar_cache_bytes = 32768;
    return o;
}
template <class T> bool equal_value(const T &a, const T &b) {
    return a == b;
}
bool equal_value(const Color &a, const Color &b);
bool equal_value(const EdgeSizes &a, const EdgeSizes &b);
bool equal_value(const ObjectPosition &a, const ObjectPosition &b);
bool equal_value(const BackgroundPaint &a, const BackgroundPaint &b);
bool equal_value(const BackgroundImageStyleValues &a, const BackgroundImageStyleValues &b);
bool equal_value(const BackgroundLayer &a, const BackgroundLayer &b);
bool equal_value(const TextShadowStyle &a, const TextShadowStyle &b);
bool equal_value(const BoxShadowStyle &a, const BoxShadowStyle &b);
bool equal_value(const StyleTransition &a, const StyleTransition &b);
bool equal_value(const StyleAnimation &a, const StyleAnimation &b);
bool equal_value(const Style &a, const Style &b);
template <class T, std::size_t N> bool equal_value(const std::array<T, N> &a, const std::array<T, N> &b) {
    for (std::size_t i = 0; i < N; ++i)
        if (!equal_value(a[i], b[i]))
            return false;
    return true;
}
template <class T> bool equal_value(const std::vector<T> &a, const std::vector<T> &b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!equal_value(a[i], b[i]))
            return false;
    return true;
}
bool equal_value(const detail::ComputedSizeStorage &a, const detail::ComputedSizeStorage &b) {
    return a.equals(b);
}
bool equal_value(const Color &a, const Color &b) {
    return equal_value(a.r, b.r) && equal_value(a.g, b.g) && equal_value(a.b, b.b) && equal_value(a.a, b.a);
}
bool equal_value(const EdgeSizes &a, const EdgeSizes &b) {
    return equal_value(a.top, b.top) && equal_value(a.right, b.right) && equal_value(a.bottom, b.bottom) &&
           equal_value(a.left, b.left);
}
bool equal_value(const ObjectPosition &a, const ObjectPosition &b) {
    return equal_value(a.x_percent, b.x_percent) && equal_value(a.y_percent, b.y_percent);
}
bool equal_value(const BackgroundPaint &a, const BackgroundPaint &b) {
    return equal_value(a.kind, b.kind) && equal_value(a.axis, b.axis) &&
           equal_value(a.stop_percent, b.stop_percent) && equal_value(a.color, b.color) &&
           equal_value(a.color2, b.color2);
}
bool equal_value(const BackgroundImageStyleValues &a, const BackgroundImageStyleValues &b) {
    return equal_value(a.resource_id, b.resource_id) && equal_value(a.size, b.size) &&
           equal_value(a.position_x_percent, b.position_x_percent) &&
           equal_value(a.position_y_percent, b.position_y_percent) && equal_value(a.origin, b.origin) &&
           equal_value(a.clip, b.clip) && equal_value(a.repeat_x, b.repeat_x) &&
           equal_value(a.repeat_y, b.repeat_y);
}
bool equal_value(const BackgroundLayer &a, const BackgroundLayer &b) {
    return equal_value(a.paint, b.paint) && equal_value(a.image, b.image) && equal_value(a.kind, b.kind);
}
bool equal_value(const TextShadowStyle &a, const TextShadowStyle &b) {
    return equal_value(a.enabled, b.enabled) && equal_value(a.uses_current_color, b.uses_current_color) &&
           equal_value(a.offset_x, b.offset_x) && equal_value(a.offset_y, b.offset_y) &&
           equal_value(a.blur, b.blur) && equal_value(a.color, b.color);
}
bool equal_value(const BoxShadowStyle &a, const BoxShadowStyle &b) {
    return equal_value(a.enabled, b.enabled) && equal_value(a.uses_current_color, b.uses_current_color) &&
           equal_value(a.offset_x, b.offset_x) && equal_value(a.offset_y, b.offset_y) &&
           equal_value(a.blur, b.blur) && equal_value(a.spread, b.spread) && equal_value(a.color, b.color);
}
bool equal_value(const StyleTransition &a, const StyleTransition &b) {
    return equal_value(a.property, b.property) && equal_value(a.duration_ms, b.duration_ms) &&
           equal_value(a.delay_ms, b.delay_ms) && equal_value(a.timing, b.timing) &&
           equal_value(a.cubic_bezier, b.cubic_bezier);
}
bool equal_value(const StyleAnimation &a, const StyleAnimation &b) {
    return equal_value(a.name, b.name) && equal_value(a.duration_ms, b.duration_ms) &&
           equal_value(a.delay_ms, b.delay_ms) && equal_value(a.timing, b.timing) &&
           equal_value(a.cubic_bezier, b.cubic_bezier) && equal_value(a.iteration_count, b.iteration_count) &&
           equal_value(a.infinite, b.infinite) && equal_value(a.direction, b.direction) &&
           equal_value(a.fill_mode, b.fill_mode);
}
bool equal_value(const Style &a, const Style &b) {
    return equal_value(a.computed_sizes, b.computed_sizes) && equal_value(a.display, b.display) &&
           equal_value(a.visibility_hidden, b.visibility_hidden) &&
           equal_value(a.visibility_specified, b.visibility_specified) &&
           equal_value(a.aspect_ratio_auto, b.aspect_ratio_auto) && equal_value(a.color, b.color) &&
           equal_value(a.color_specified, b.color_specified) &&
           equal_value(a.background_layers, b.background_layers) &&
           equal_value(a.background_layer_count, b.background_layer_count) &&
           equal_value(a.background_color, b.background_color) && equal_value(a.margin, b.margin) &&
           equal_value(a.margin_left_auto, b.margin_left_auto) &&
           equal_value(a.margin_right_auto, b.margin_right_auto) && equal_value(a.padding, b.padding) &&
           equal_value(a.border_width, b.border_width) && equal_value(a.border_color, b.border_color) &&
           equal_value(a.border_radius, b.border_radius) &&
           equal_value(a.border_radius_percent, b.border_radius_percent) &&
           equal_value(a.border_radius_x_values, b.border_radius_x_values) &&
           equal_value(a.border_radius_y_values, b.border_radius_y_values) &&
           equal_value(a.border_radius_x_percent_mask, b.border_radius_x_percent_mask) &&
           equal_value(a.border_radius_y_percent_mask, b.border_radius_y_percent_mask) &&
           equal_value(a.border_radius_axes_specified, b.border_radius_axes_specified) &&
           equal_value(a.width, b.width) && equal_value(a.height, b.height) &&
           equal_value(a.min_width, b.min_width) && equal_value(a.min_height, b.min_height) &&
           equal_value(a.max_width, b.max_width) && equal_value(a.max_height, b.max_height) &&
           equal_value(a.width_percent, b.width_percent) && equal_value(a.height_percent, b.height_percent) &&
           equal_value(a.min_width_percent, b.min_width_percent) &&
           equal_value(a.min_height_percent, b.min_height_percent) &&
           equal_value(a.max_width_percent, b.max_width_percent) &&
           equal_value(a.max_height_percent, b.max_height_percent) &&
           equal_value(a.aspect_ratio_width, b.aspect_ratio_width) &&
           equal_value(a.aspect_ratio_height, b.aspect_ratio_height) &&
           equal_value(a.font_size, b.font_size) &&
           equal_value(a.font_size_specified, b.font_size_specified) &&
           equal_value(a.font_weight, b.font_weight) &&
           equal_value(a.font_weight_specified, b.font_weight_specified) &&
           equal_value(a.font_family_hash, b.font_family_hash) &&
           equal_value(a.font_family_specified, b.font_family_specified) &&
           equal_value(a.line_height, b.line_height) &&
           equal_value(a.line_height_specified, b.line_height_specified) &&
           equal_value(a.text_indent, b.text_indent) &&
           equal_value(a.text_indent_specified, b.text_indent_specified) &&
           equal_value(a.letter_spacing, b.letter_spacing) &&
           equal_value(a.letter_spacing_specified, b.letter_spacing_specified) &&
           equal_value(a.text_transform, b.text_transform) &&
           equal_value(a.text_transform_specified, b.text_transform_specified) &&
           equal_value(a.text_decoration_underline, b.text_decoration_underline) &&
           equal_value(a.text_decoration_line_through, b.text_decoration_line_through) &&
           equal_value(a.text_decoration_specified, b.text_decoration_specified) &&
           equal_value(a.text_shadow, b.text_shadow) &&
           equal_value(a.text_shadow_specified, b.text_shadow_specified) &&
           equal_value(a.box_shadow, b.box_shadow) && equal_value(a.outline_width, b.outline_width) &&
           equal_value(a.outline_offset, b.outline_offset) && equal_value(a.outline_color, b.outline_color) &&
           equal_value(a.overflow, b.overflow) &&
           equal_value(a.overflow_wrap_anywhere, b.overflow_wrap_anywhere) &&
           equal_value(a.overflow_wrap_specified, b.overflow_wrap_specified) &&
           equal_value(a.white_space_nowrap, b.white_space_nowrap) &&
           equal_value(a.white_space_specified, b.white_space_specified) &&
           equal_value(a.text_overflow_ellipsis, b.text_overflow_ellipsis) &&
           equal_value(a.text_overflow_specified, b.text_overflow_specified) &&
           equal_value(a.opacity, b.opacity) && equal_value(a.transform, b.transform) &&
           equal_value(a.transform_origin_x_percent, b.transform_origin_x_percent) &&
           equal_value(a.transform_origin_y_percent, b.transform_origin_y_percent) &&
           equal_value(a.position, b.position) && equal_value(a.position_type, b.position_type) &&
           equal_value(a.inset_top, b.inset_top) && equal_value(a.inset_right, b.inset_right) &&
           equal_value(a.inset_bottom, b.inset_bottom) && equal_value(a.inset_left, b.inset_left) &&
           equal_value(a.inset_top_specified, b.inset_top_specified) &&
           equal_value(a.inset_right_specified, b.inset_right_specified) &&
           equal_value(a.inset_bottom_specified, b.inset_bottom_specified) &&
           equal_value(a.inset_left_specified, b.inset_left_specified) && equal_value(a.z_index, b.z_index) &&
           equal_value(a.z_index_auto, b.z_index_auto) &&
           equal_value(a.box_sizing_border_box, b.box_sizing_border_box) &&
           equal_value(a.column_gap, b.column_gap) && equal_value(a.row_gap, b.row_gap) &&
           equal_value(a.flex_direction, b.flex_direction) && equal_value(a.flex_wrap, b.flex_wrap) &&
           equal_value(a.align_self, b.align_self) && equal_value(a.align_content, b.align_content) &&
           equal_value(a.flex_grow, b.flex_grow) && equal_value(a.flex_shrink, b.flex_shrink) &&
           equal_value(a.flex_basis, b.flex_basis) && equal_value(a.flex_order, b.flex_order) &&
           equal_value(a.flex_basis_kind, b.flex_basis_kind) &&
           equal_value(a.grid_min_track_width, b.grid_min_track_width) &&
           equal_value(a.grid_template_column_count, b.grid_template_column_count) &&
           equal_value(a.grid_template_column_widths, b.grid_template_column_widths) &&
           equal_value(a.grid_template_row_heights, b.grid_template_row_heights) &&
           equal_value(a.grid_auto_row_min, b.grid_auto_row_min) &&
           equal_value(a.grid_column_start, b.grid_column_start) &&
           equal_value(a.grid_row_start, b.grid_row_start) &&
           equal_value(a.grid_template_row_count, b.grid_template_row_count) &&
           equal_value(a.grid_column_span, b.grid_column_span) &&
           equal_value(a.grid_row_span, b.grid_row_span) && equal_value(a.object_fit, b.object_fit) &&
           equal_value(a.object_position, b.object_position) &&
           equal_value(a.image_rendering, b.image_rendering) &&
           equal_value(a.list_style_type, b.list_style_type) &&
           equal_value(a.list_style_type_specified, b.list_style_type_specified) &&
           equal_value(a.before_content_kind, b.before_content_kind) &&
           equal_value(a.before_content_text, b.before_content_text) &&
           equal_value(a.before_counter_name, b.before_counter_name) &&
           equal_value(a.before_counter_suffix, b.before_counter_suffix) &&
           equal_value(a.before_color, b.before_color) &&
           equal_value(a.before_color_specified, b.before_color_specified) &&
           equal_value(a.before_font_weight, b.before_font_weight) &&
           equal_value(a.before_font_weight_specified, b.before_font_weight_specified) &&
           equal_value(a.before_left, b.before_left) &&
           equal_value(a.before_left_specified, b.before_left_specified) &&
           equal_value(a.after_content_kind, b.after_content_kind) &&
           equal_value(a.after_content_text, b.after_content_text) &&
           equal_value(a.after_counter_name, b.after_counter_name) &&
           equal_value(a.after_counter_suffix, b.after_counter_suffix) &&
           equal_value(a.after_color, b.after_color) &&
           equal_value(a.after_color_specified, b.after_color_specified) &&
           equal_value(a.after_font_weight, b.after_font_weight) &&
           equal_value(a.after_font_weight_specified, b.after_font_weight_specified) &&
           equal_value(a.after_left, b.after_left) &&
           equal_value(a.after_left_specified, b.after_left_specified) &&
           equal_value(a.text_align, b.text_align) &&
           equal_value(a.text_align_specified, b.text_align_specified) &&
           equal_value(a.justify_content, b.justify_content) && equal_value(a.align_items, b.align_items) &&
           equal_value(a.table_properties, b.table_properties) && equal_value(a.transitions, b.transitions) &&
           equal_value(a.animations, b.animations);
}
void same(const Style &a, const Style &b) {
    check(equal_value(a, b), "complete cached Style equals ordinary cascade");
}
void compare(StyleResolver &a, StyleResolver &b, const Node &node) {
    // Fresh contexts deliberately also exercise direct public DOM edits.
    StyleResolveContext ac, bc;
    same(a.resolve(node, ac), b.resolve(node, bc));
    same(a.resolve(node, ac), b.resolve(node, bc));
}
void contextual_inputs() {
    auto root = make_element("main");
    root->attributes["class"] = "on";
    auto &before = root->append_child(make_element("span"));
    auto &node = root->append_child(make_element("div"));
    node.attributes["data-label"] = "first";
    node.append_child(make_text("A"));
    const std::string css = "div{width:10px;color:blue} .on>div{height:20px} span+div{width:30px}"
                            "div:has(.flag){height:40px} div:empty{height:50px} div:hover{color:red}"
                            "div::before{content:attr(data-label)}";
    StyleResolver cached(CssParser{}.parse(css), enabled()), plain(CssParser{}.parse(css));
    compare(cached, plain, node);
    check(cached.statistics().scalar_cache_hits > 0, "repeated identical inputs hit cache");
    root->attributes["class"] = "off";
    compare(cached, plain, node);
    before.tag_name = "p";
    compare(cached, plain, node);
    node.children.clear();
    compare(cached, plain, node);
    node.append_child(make_element("i")).attributes["class"] = "flag";
    compare(cached, plain, node);
    node.attributes["data-label"] = "second";
    compare(cached, plain, node);
    cached.set_interaction_state(&node, nullptr, nullptr);
    plain.set_interaction_state(&node, nullptr, nullptr);
    compare(cached, plain, node);
    node.attributes["style"] = "width:77px!important;color:green";
    compare(cached, plain, node);
    node.attributes["style"] = "--x:81px;width:var(--x)";
    compare(cached, plain, node);
    root->set_attribute("style", "--x:91px");
    node.set_attribute("style", "width:var(--x)");
    compare(cached, plain, node);
    root->set_attribute("style", "--x:92px");
    compare(cached, plain, node);
    node.tag_name = "input";
    node.attributes["type"] = "hidden";
    compare(cached, plain, node);
}
void seeds_and_lifetime() {
    auto node = make_element("div");
    const std::string css = "div{width:2em;height:1vw;font-weight:bolder;line-height:inherit}";
    StyleResolver cached(CssParser{}.parse(css), enabled()), plain(CssParser{}.parse(css));
    for (int i = 0; i < 12; ++i) {
        StyleResolveContext a, b;
        a.viewport_width = b.viewport_width = 100 + i * 13;
        a.viewport_height = b.viewport_height = 200 + i;
        for (int repeat = 0; repeat < 2; ++repeat)
            same(cached.resolve(*node, a), plain.resolve(*node, b));
    }
    alignas(Node) unsigned char slot[sizeof(Node)];
    auto *reused = new (slot) Node(NodeType::Element);
    reused->tag_name = "div";
    compare(cached, plain, *reused);
    reused->~Node();
    reused = new (slot) Node(NodeType::Element);
    reused->tag_name = "button";
    reused->attributes["disabled"] = "";
    compare(cached, plain, *reused);
    reused->~Node();
    StyleResolver copied(cached);
    compare(copied, plain, *node);
    StyleResolver moved(std::move(copied));
    compare(moved, plain, *node);
    copied = cached;
    compare(copied, plain, *node);
    copied = std::move(moved);
    compare(copied, plain, *node);
}
void budgets_and_fallbacks() {
    auto node = make_element("div");
    node->attributes["style"] = "color:red;width:11px";
    StyleResolver plain(CssParser{}.parse(""));
    for (int mode = 0; mode < 4; ++mode) {
        auto o = enabled();
        if (mode == 0)
            o.max_scalar_cache_entries = 0;
        if (mode == 1)
            o.max_scalar_cache_bytes = 0;
        if (mode == 2)
            o.max_scalar_cache_bytes = 1;
        if (mode == 3)
            o.max_inline_declarations = 1;
        StyleResolver cached(CssParser{}.parse(""), o);
        if (mode < 3) {
            compare(cached, plain, *node);
            check(cached.statistics().scalar_cache_entries == 0, "refused cache retains no entries");
        } else {
            auto off = o;
            off.max_scalar_cache_entries = 0;
            StyleResolver limited(CssParser{}.parse(""), off);
            compare(cached, limited, *node);
        }
    }
    auto o = enabled();
    o.max_scalar_cache_entries = 1;
    StyleResolver limited(CssParser{}.parse(""), o);
    compare(limited, plain, *node);
    node->set_attribute("style", "width:12px");
    compare(limited, plain, *node);
    check(limited.statistics().scalar_cache_entries == 1 &&
              limited.statistics().scalar_cache_payload_bytes <= 32768,
          "entry/byte limits do not churn");
    const auto exact_bytes = limited.statistics().scalar_cache_payload_bytes;
    // Use the retained first key, not the deliberately refused replacement.
    node->set_attribute("style", "color:red;width:11px");
    for (int delta = -1; delta <= 0; ++delta) {
        auto boundary = enabled();
        boundary.max_scalar_cache_bytes = exact_bytes + delta;
        StyleResolver edge(CssParser{}.parse(""), boundary);
        compare(edge, plain, *node);
        check(edge.statistics().scalar_cache_entries == (delta ? 0u : 1u),
              "accounted byte boundary is exact");
    }
    auto huge = enabled();
    huge.max_scalar_cache_entries = huge.max_scalar_cache_bytes = std::numeric_limits<std::size_t>::max();
    StyleResolver capped(CssParser{}.parse(""), huge);
    for (int i = 0; i < 40; ++i) {
        node->attributes["id"] = std::to_string(i);
        compare(capped, plain, *node);
    }
    check(capped.statistics().scalar_cache_entries <= 16 &&
              capped.statistics().scalar_cache_payload_bytes <= 32768,
          "host limits cannot exceed hard caps");
    node->attributes["large"] = std::string(3000, 'x');
    compare(limited, plain, *node);
    node->attributes["large"] = std::string("a\0b", 3);
    compare(limited, plain, *node);
    node->attributes["large"] = std::string("a\0c", 3);
    compare(limited, plain, *node);
    VectorDiagnosticSink sink;
    o = enabled();
    o.diagnostics = &sink;
    StyleResolver diagnostic(CssParser{}.parse(""), o);
    node->set_attribute("style", "width:bad");
    (void)diagnostic.resolve(*node);
    const auto count = sink.size();
    (void)diagnostic.resolve(*node);
    check(count && sink.size() == count * 2 && diagnostic.statistics().scalar_cache_entries == 0,
          "diagnostics repeat on uncached path");
    for (const char *source :
         {"width:1.5px", "border-radius:3px 4px", "transition:opacity 1s", "grid-template-columns:1fr 2fr"}) {
        node->set_attribute("style", source);
        StyleResolver sparse(CssParser{}.parse(""), enabled());
        compare(sparse, plain, *node);
    }
    std::string many;
    for (int i = 0; i < 70; ++i)
        many += "div{width:" + std::to_string(i) + "px}";
    StyleResolver many_cached(CssParser{}.parse(many), enabled()), many_plain(CssParser{}.parse(many));
    node->remove_attribute("style");
    compare(many_cached, many_plain, *node);
    check(many_cached.statistics().scalar_cache_entries == 0, "large candidate sets use complete fallback");
    // Cache capacity and key framing also apply to cross-node reuse.
    auto binary = make_element("div");
    binary->attributes["data"] = std::string("a\0b", 3);
    binary->attributes["style"] = "color:red";
    StyleResolver exact(CssParser{}.parse("div::before{content:attr(data)}"), enabled());
    (void)exact.resolve(*binary);
    (void)exact.resolve(*binary);
    binary->attributes["data"] = std::string("a\0c", 3);
    StyleResolver binary_plain(CssParser{}.parse("div::before{content:attr(data)}"));
    compare(exact, binary_plain, *binary);
    check(exact.statistics().scalar_cache_misses == 2, "embedded NUL key suffix distinguishes entries");
}
void exact_inputs_and_registry() {
    auto first = make_element("div"), second = make_element("div");
    first->set_attribute("style", "--a:12px;--b:blue;width:var(--a);color:var(--b)");
    second->set_attribute("style", "--b:blue;--a:12px;width:var(--a);color:var(--b)");
    detail::ScalarCascadeKey left, right;
    CustomPropertyMap variables_left{{"--a", "12px"}, {"--b", "blue"}};
    CustomPropertyMap variables_right{{"--b", "blue"}, {"--a", "12px"}};
    left.inputs(*first, true, {}, variables_left);
    right.inputs(*first, true, {}, variables_right);
    check(left.valid && right.valid && left.bytes == right.bytes, "custom properties ordered by name");
    StyleResolver cached(CssParser{}.parse(""), enabled()), plain(CssParser{}.parse(""));
    compare(cached, plain, *first);
    compare(cached, plain, *second);
    second->attributes = first->attributes;
    const auto hits = cached.statistics().scalar_cache_hits;
    same(cached.resolve(*second), plain.resolve(*second));
    check(cached.statistics().scalar_cache_hits == hits + 1, "equivalent distinct nodes reuse cascade");
    auto image = make_element("img");
    image->set_attribute("width", "12.5");
    image->set_attribute("height", "25.25");
    compare(cached, plain, *image);
    auto hinted = cached.resolve(*image);
    check(hinted.aspect_ratio_width > 0 && hinted.aspect_ratio_height > 0 && !hinted.computed_sizes.empty(),
          "HTML dimensions retain fractional owner and ratio");
    image->set_attribute("width", "50.125%");
    compare(cached, plain, *image);

    auto layers = make_element("div");
    layers->set_attribute(
        "style", "background-image:url('/one.png'),url('/two.png'),url('/three.png'),url('/four.png');"
                 "background-position:left top,right bottom; background-size:cover,contain; "
                 "background-repeat:repeat-x,no-repeat");
    StyleResolver registry(CssParser{}.parse(""), enabled()), reference(CssParser{}.parse(""));
    const auto verify = [&](StyleResolver &resolver) {
        compare(resolver, reference, *layers);
        const auto style = resolver.resolve(*layers);
        check(style.background_layer_count == 4, "four-layer resource fixture");
        for (unsigned index = 0; index < 4; ++index) {
            const auto id = style.background_layers[index].image.resource_id;
            const auto expected_id = reference.resolve(*layers).background_layers[index].image.resource_id;
            const auto *url = resolver.background_image_resource_url(id);
            check(url && reference.background_image_resource_url(expected_id) &&
                      *url == *reference.background_image_resource_url(expected_id),
                  "each cached resource ID resolves correctly");
        }
    };
    verify(registry);
    StyleResolver copy(registry);
    verify(copy);
    StyleResolver moved(std::move(copy));
    check(copy.statistics().scalar_cache_entries == 0, "move construction clears source cache");
    verify(moved);
    StyleResolver empty_reference(CssParser{}.parse(""));
    same(copy.resolve(*layers), empty_reference.resolve(*layers));
    auto foreign = make_element("div");
    foreign->set_attribute("style", "background-image:url('/foreign.png')");
    copy.resolve(*foreign);
    copy = registry;
    verify(copy);
    copy = std::move(moved);
    check(moved.statistics().scalar_cache_entries == 0, "move assignment clears source cache");
    verify(copy);
    same(moved.resolve(*layers), empty_reference.resolve(*layers));
}
void context_after_assignment() {
    auto node = make_element("div");
    StyleResolver resolver(CssParser{}.parse("div{--w:12px;width:var(--w)}"), enabled());
    StyleResolveContext context;
    check(resolver.resolve(*node, context).width == 12, "initial assignment context");
    StyleResolver replacement(CssParser{}.parse("div{--w:33px;width:var(--w)}"), enabled());
    resolver = replacement;
    same(resolver.resolve(*node, context), replacement.resolve(*node));
    StyleResolver another(CssParser{}.parse("div{--w:44px;width:var(--w)}"), enabled());
    resolver = std::move(another);
    check(resolver.resolve(*node, context).width == 44, "move assignment refreshes existing context");
}
void allocation_fallbacks() {
    auto node = make_element("div");
    node->set_attribute("style", "width:12.5px;height:25.25px");
    const auto sheet = CssParser{}.parse("div::before{content:'a deliberately long generated content string "
                                         "to force retained copy allocation'}");
    StyleResolver plain(sheet);
    const auto expected = plain.resolve(*node);
    unsigned completed = 0;
    for (bool warm : {false, true}) {
        for (std::ptrdiff_t failure = 0; failure < 24; ++failure) {
            StyleResolver candidate(sheet, enabled());
            StyleResolveContext context;
            // Prepare non-cache resolver state before arming allocation failure.
            candidate.resolve(*node, context);
            if (!warm) {
                candidate = StyleResolver(sheet, enabled());
                context.clear();
            }
            allocation_failure_countdown = failure;
            try {
                auto result = candidate.resolve(*node, context);
                allocation_failure_countdown = -1;
                same(result, expected);
                ++completed;
            } catch (const std::bad_alloc &) {
                allocation_failure_countdown = -1;
            }
            same(candidate.resolve(*node), expected);
            same(candidate.resolve(*node), expected);
        }
    }
    check(completed > 24, "cache-local allocation failures fall back to complete ordinary resolution");
}
void fractional_size_admission() {
    for (unsigned index = 0; index < 11; ++index) {
        Style unsupported;
        const auto property = static_cast<detail::SizeProperty>(index);
        if (index < 7) {
            check(unsupported.computed_sizes.set_length_percentage(property, 2, 10), "mixed owner fixture");
        } else {
            check(unsupported.computed_sizes.set(property, detail::SizeKind::Pixels, 1.5), "inset fixture");
        }
        detail::ScalarCascadeCache cache;
        check(!cache.insert("mixed", unsupported, 16, 32768) && cache.entries == 0,
              "mixed dimensions, flex basis and inset owners refuse admission");
    }
    auto node = make_element("div");
    node->set_attribute("style", "width:12.5px;height:25.25px");
    StyleResolver cached(CssParser{}.parse(""), enabled()), plain(CssParser{}.parse(""));
    compare(cached, plain, *node);
    const auto retained = cached.resolve(*node);
    check(cached.statistics().scalar_cache_hits > 0, "fractional dimensions hit");
    auto changed = retained;
    changed.computed_sizes.clear(detail::SizeProperty::Width);
    changed.computed_sizes.set(detail::SizeProperty::Height, detail::SizeKind::Pixels, 30.75);
    same(cached.resolve(*node), retained);
    detail::SizeKind kind{};
    std::int64_t units = 0, percent = 0;
    check(retained.computed_sizes.read_typed(detail::SizeProperty::Width, kind, units, percent) &&
              kind == detail::SizeKind::Pixels && units == 12500,
          "fractional width retained exactly");
    check(retained.computed_sizes.read_typed(detail::SizeProperty::Height, kind, units, percent) &&
              units == 25250,
          "fractional height retained exactly");
    detail::ScalarCascadeCache integer_payload, fractional_payload;
    check(integer_payload.insert("key", Style{}, 16, 32768) &&
              fractional_payload.insert("key", retained, 16, 32768),
          "payload probes admitted");
    check(fractional_payload.payload_bytes - integer_payload.payload_bytes ==
              retained.computed_sizes.retained_payload_bytes(),
          "full fractional owner charged");
    const auto bytes = cached.statistics().scalar_cache_payload_bytes;
    for (std::size_t budget : {bytes - 1, bytes}) {
        auto options = enabled();
        options.max_scalar_cache_bytes = budget;
        StyleResolver boundary(CssParser{}.parse(""), options);
        compare(boundary, plain, *node);
        check(boundary.statistics().scalar_cache_entries == (budget == bytes ? 1u : 0u),
              "fractional owner exact byte boundary");
    }
    node->set_attribute("style", "width:12.5%;height:25.25px");
    StyleResolver percentage(CssParser{}.parse(""), enabled());
    compare(percentage, plain, *node);
    auto value = percentage.resolve(*node);
    check(value.computed_sizes.read_typed(detail::SizeProperty::Width, kind, units, percent) &&
              kind == detail::SizeKind::Percent && units == 12500,
          "percentage coefficient remains exact");
    for (const auto *declaration :
         {"left:1.5px", "flex-basis:2.5px", "width:12.5px;left:1.5px", "transition:opacity 1s"}) {
        node->set_attribute("style", declaration);
        StyleResolver unsupported(CssParser{}.parse(""), enabled());
        compare(unsupported, plain, *node);
        if (unsupported.statistics().scalar_cache_entries != 0)
            throw std::runtime_error(declaration);
    }
}
void overrides() {
    auto node = make_element("div");
    node->append_child(make_text("A"));
    StyleResolver cached(CssParser{}.parse("div{width:10px}"), enabled());
    StyleResolver plain(CssParser{}.parse("div{width:10px}"));
    for (int state = 0; state < 3; ++state) {
        node->set_attribute("style", state == 1 ? "color:blue;font-size:22px" : "color:red;font-size:16px");
        std::vector<StyleOverride> overrides(1);
        overrides[0].node = node.get();
        overrides[0].has_color = true;
        overrides[0].color = {0, 255, 0, 255};
        RenderTreeOptions ro;
        ro.style_overrides = &overrides;
        auto a = RenderTreeBuilder(cached, ro).build(*node);
        auto b = RenderTreeBuilder(plain, ro).build(*node);
        same(a->style, b->style);
        check(a->children.size() == b->children.size(), "host tree shape matches");
        for (std::size_t i = 0; i < a->children.size(); ++i)
            same(a->children[i]->style, b->children[i]->style);
    }
}
} // namespace
int main() {
    try {
        contextual_inputs();
        seeds_and_lifetime();
        budgets_and_fallbacks();
        fractional_size_admission();
        overrides();
        exact_inputs_and_registry();
        allocation_fallbacks();
        context_after_assignment();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
