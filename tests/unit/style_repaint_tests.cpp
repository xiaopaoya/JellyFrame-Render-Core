#include "render_core/animation_invalidation.h"
#include "render_core/css_parser.h"
#include "render_core/html_parser.h"
#include "render_core/layout.h"
#include "render_core/render_tree.h"
#include "render_core/style_repaint.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool fixed_measure(const std::string& text,
                   int,
                   int,
                   TextMetrics* metrics,
                   void*) {
    if (metrics == nullptr) {
        return false;
    }
    metrics->width = static_cast<int>(text.size()) * 8;
    metrics->line_height = 12;
    return true;
}

TextMeasureProvider fixed_text_measure() {
    return TextMeasureProvider{fixed_measure, nullptr};
}

Node* find_element_by_id(Node& node, const std::string& id) {
    if (node.type == NodeType::Element && node.attribute("id") == id) {
        return &node;
    }
    for (auto& child : node.children) {
        if (Node* found = find_element_by_id(*child, id)) {
            return found;
        }
    }
    return nullptr;
}

const LayoutBox* find_layout_by_id(const LayoutBox& box, const std::string& id) {
    if (box.node != nullptr && box.node->type == NodeType::Element && box.node->attribute("id") == id)
        return &box;
    for (const auto& child : box.children) {
        if (const LayoutBox* found = find_layout_by_id(*child, id)) return found;
    }
    return nullptr;
}

const RenderObject* find_render_object(const RenderObject& object, const Node* node) {
    if (object.node == node) return &object;
    for (const auto& child : object.children) {
        if (const auto* found = find_render_object(*child, node)) return found;
    }
    return nullptr;
}

struct Fixture {
    std::unique_ptr<Node> document;
    Stylesheet stylesheet;
    StyleResolver resolver;
    RenderObjectPtr render_tree;
    LayoutBoxPtr layout_tree;
};

Fixture build_fixture(const std::string& html, const std::string& css) {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(html);
    Stylesheet stylesheet = css_parser.parse(css);
    StyleResolver resolver(stylesheet);
    RenderTreeBuilder render_builder(resolver);
    auto render_tree = render_builder.build(*document);
    LayoutEngine layout_engine(resolver, fixed_text_measure());
    auto layout_tree = layout_engine.layout(*render_tree, 240);
    clear_dirty_flags(*document);
    return Fixture{
        std::move(document),
        std::move(stylesheet),
        std::move(resolver),
        std::move(render_tree),
        std::move(layout_tree),
    };
}

RenderObjectPtr rebuild_render_tree(const Node& document, const StyleResolver& resolver) {
    RenderTreeBuilder builder(resolver);
    return builder.build(document);
}

void paint_only_class_change_can_reuse_layout() {
    auto fixture = build_fixture(
        "<body><button id='pulse' class='pill'>Open</button></body>",
        ".pill { display: block; width: 80px; height: 20px; background-color: #111111; "
        "background-image: url('/a.png'), url('/b.png'), url('/c.png'), url('/d.png'); }"
        ".pill.active { background-color: #222222; transform: scale(1.05); opacity: .9; }");
    Node* pulse = find_element_by_id(*fixture.document, "pulse");
    check(pulse != nullptr, "pulse exists");
    pulse->set_attribute("class", "pill active");
    auto next_render = rebuild_render_tree(*fixture.document, fixture.resolver);

    check(style_dirty_can_reuse_layout(*fixture.document,
                                       *fixture.render_tree,
                                       *next_render,
                                       *fixture.layout_tree,
                                       fixed_text_measure()),
          "paint-only class change can reuse layout");
    check(apply_render_styles_to_layout(*next_render, *fixture.layout_tree),
          "new paint styles can be applied to retained layout");

    std::vector<StyleOverride> previous_overrides;
    std::vector<StyleOverride> current_overrides;
    collect_style_repaint_overrides(*fixture.render_tree,
                                    *next_render,
                                    previous_overrides,
                                    current_overrides);
    check(previous_overrides.size() == 1 && current_overrides.size() == 1,
          "paint-only class change produces repaint overrides");
    check(previous_overrides.front().has_transform && current_overrides.front().has_transform,
          "transform repaint override is captured");
    check(previous_overrides.front().has_background_color &&
              current_overrides.front().has_background_color,
          "background repaint override is captured");
    check(previous_overrides.front().has_opacity && current_overrides.front().has_opacity,
          "opacity repaint override is captured");

    RenderTreeOptions override_options;
    override_options.style_overrides = &current_overrides;
    auto overridden_tree = RenderTreeBuilder(fixture.resolver, override_options).build(*fixture.document);
    const auto* overridden = find_render_object(*overridden_tree, pulse);
    const auto* unoverridden = find_render_object(*next_render, pulse);
    check(overridden != nullptr && unoverridden != nullptr && overridden->style.background_layer_count == 4 &&
              overridden->style.background_layers[0].image.resource_id ==
                  unoverridden->style.background_layers[0].image.resource_id &&
              overridden->style.background_color.r == 34,
          "background color override preserves canonical image layers and updates the shared fallback color");
}

void fourth_background_layer_change_stays_conservative() {
    auto fixture = build_fixture(
        "<body><button id='pulse' class='pill'>Open</button></body>",
        ".pill { display:block; width:80px; height:20px; background-image:"
        "url('/a.png'),url('/b.png'),url('/c.png'),url('/d.png'); }"
        ".pill.active { background-image:url('/a.png'),url('/b.png'),url('/c.png'),url('/e.png'); }");
    Node* pulse = find_element_by_id(*fixture.document, "pulse");
    check(pulse != nullptr, "pulse exists for fourth-layer invalidation");
    pulse->set_attribute("class", "pill active");
    auto next_render = rebuild_render_tree(*fixture.document, fixture.resolver);
    check(!style_dirty_can_reuse_layout(*fixture.document,
                                        *fixture.render_tree,
                                        *next_render,
                                        *fixture.layout_tree,
                                        fixed_text_measure()),
          "a fourth background image change cannot hide behind the legacy first-layer projection");
}

void layout_class_change_stays_conservative() {
    auto fixture = build_fixture(
        "<body><button id='pulse' class='pill'>Open</button></body>",
        ".pill { display: block; width: 80px; height: 20px; }"
        ".pill.active { width: 120px; transform: scale(1.05); }");
    Node* pulse = find_element_by_id(*fixture.document, "pulse");
    check(pulse != nullptr, "pulse exists");
    pulse->set_attribute("class", "pill active");
    auto next_render = rebuild_render_tree(*fixture.document, fixture.resolver);

    check(!style_dirty_can_reuse_layout(*fixture.document,
                                        *fixture.render_tree,
                                        *next_render,
                                        *fixture.layout_tree,
                                        fixed_text_measure()),
          "layout-affecting class change does not reuse layout");
}

void transform_class_change_invalidates_old_and_new_bounds() {
    auto fixture = build_fixture(
        "<body><button id='pulse' class='pill'>Open</button></body>",
        ".pill { display: block; width: 80px; height: 20px; transform: translate(0px, 0px); }"
        ".pill.active { transform: translate(40px, 0px); background-color: #222222; }");
    Node* pulse = find_element_by_id(*fixture.document, "pulse");
    check(pulse != nullptr, "pulse exists");
    pulse->set_attribute("class", "pill active");
    auto next_render = rebuild_render_tree(*fixture.document, fixture.resolver);

    check(style_dirty_can_reuse_layout(*fixture.document,
                                       *fixture.render_tree,
                                       *next_render,
                                       *fixture.layout_tree,
                                       fixed_text_measure()),
          "transform-only class change can reuse layout");
    std::vector<StyleOverride> previous_overrides;
    std::vector<StyleOverride> current_overrides;
    collect_style_repaint_overrides(*fixture.render_tree,
                                    *next_render,
                                    previous_overrides,
                                    current_overrides);
    check(apply_render_styles_to_layout(*next_render, *fixture.layout_tree),
          "new transform style can be applied");
    const DirtyRegionResult region =
        compute_animation_dirty_region(*fixture.layout_tree,
                                       previous_overrides,
                                       current_overrides,
                                       AnimationInvalidationOptions{Rect{0, 0, 240, 120}, 4, 0});
    check(region.mode == DirtyRegionMode::DirtyRects && !region.rects.empty(),
          "transform style change produces dirty rect");
    check(region.rects.front().width >= 120,
          "transform dirty rect covers old and translated bounds");
}

void class_change_with_stable_text_can_reuse_layout() {
    auto fixture = build_fixture(
        "<body><button id='pulse' class='pill'>Open</button><strong id='frame'>01</strong></body>",
        ".pill { display: block; width: 80px; height: 20px; background-color: #111111; }"
        ".pill.active { background-color: #222222; transform: scale(1.05); }"
        "strong { display: block; font-size: 10px; line-height: 12px; }");
    Node* pulse = find_element_by_id(*fixture.document, "pulse");
    Node* frame = find_element_by_id(*fixture.document, "frame");
    check(pulse != nullptr && frame != nullptr, "nodes exist");
    frame->set_text_content("02");
    pulse->set_attribute("class", "pill active");
    auto next_render = rebuild_render_tree(*fixture.document, fixture.resolver);

    check(style_dirty_can_reuse_layout(*fixture.document,
                                       *fixture.render_tree,
                                       *next_render,
                                       *fixture.layout_tree,
                                       fixed_text_measure()),
          "paint-only class change plus stable text can reuse layout");
}

void class_change_with_wider_text_stays_conservative() {
    auto fixture = build_fixture(
        "<body><button id='pulse' class='pill'>Open</button><strong id='frame'>99</strong></body>",
        ".pill { display: block; width: 80px; height: 20px; background-color: #111111; }"
        ".pill.active { background-color: #222222; transform: scale(1.05); }"
        "strong { display: block; font-size: 10px; line-height: 12px; }");
    Node* pulse = find_element_by_id(*fixture.document, "pulse");
    Node* frame = find_element_by_id(*fixture.document, "frame");
    check(pulse != nullptr && frame != nullptr, "nodes exist");
    frame->set_text_content("100");
    pulse->set_attribute("class", "pill active");
    auto next_render = rebuild_render_tree(*fixture.document, fixture.resolver);

    check(!style_dirty_can_reuse_layout(*fixture.document,
                                        *fixture.render_tree,
                                        *next_render,
                                        *fixture.layout_tree,
                                        fixed_text_measure()),
          "paint-only class change with wider text does not reuse layout");
}

void flex_and_text_layout_fields_reject_paint_only_reuse() {
    auto fixture = build_fixture(
        "<body><div id='item' class='item'>Open</div></body>",
        ".item { display: flex; flex-direction: row; order: 0; align-self: auto; align-content: start;"
        " white-space: normal; text-overflow: clip; }"
        ".item.active { flex-direction: column; order: 1; align-self: center; align-content: center;"
        " white-space: nowrap; text-overflow: ellipsis; }");
    Node* item = find_element_by_id(*fixture.document, "item");
    check(item != nullptr, "layout fields item exists");
    item->set_attribute("class", "item active");
    auto next_render = rebuild_render_tree(*fixture.document, fixture.resolver);

    check(!style_dirty_can_reuse_layout(*fixture.document,
                                        *fixture.render_tree,
                                        *next_render,
                                        *fixture.layout_tree,
                                        fixed_text_measure()),
          "flex and text layout changes reject retained paint-only reuse");
}

void table_spacing_change_rejects_incremental_layout_reuse() {
    auto fixture = build_fixture(
        "<body><table id='grid' class='normal'><colgroup><col id='a'><col id='b'></colgroup>"
        "<tbody><tr><td>One</td><td>Two</td></tr></tbody></table></body>",
        "body { margin: 0; } table { width: 80px; table-layout: fixed; border-collapse: separate; }"
        ".normal { border-spacing: 2px 2px; } .wide { border-spacing: 8px 2px; }"
        "#a { width: 30px; } #b { width: 30px; }");
    Node* table = find_element_by_id(*fixture.document, "grid");
    check(table != nullptr, "fixed table exists for retained layout test");
    const LayoutBox* old_table = find_layout_by_id(*fixture.layout_tree, "grid");
    check(old_table != nullptr, "initial table layout exists");
    const int old_width = old_table->rect.width;
    table->set_attribute("class", "wide");
    auto next_render = rebuild_render_tree(*fixture.document, fixture.resolver);
    check(!style_dirty_can_reuse_layout(*fixture.document, *fixture.render_tree, *next_render,
                                        *fixture.layout_tree, fixed_text_measure()),
          "table spacing change rejects retained geometry reuse");
    LayoutEngine engine(fixture.resolver, fixed_text_measure());
    auto fresh_layout = engine.layout(*next_render, 240);
    const LayoutBox* fresh_table = find_layout_by_id(*fresh_layout, "grid");
    check(fresh_table != nullptr && fresh_table->rect.width > old_width,
          "full table layout applies changed separate-border spacing");
}

} // namespace

int main() {
    try {
        paint_only_class_change_can_reuse_layout();
        fourth_background_layer_change_stays_conservative();
        layout_class_change_stays_conservative();
        transform_class_change_invalidates_old_and_new_bounds();
        class_change_with_stable_text_can_reuse_layout();
        class_change_with_wider_text_stays_conservative();
        flex_and_text_layout_fields_reject_paint_only_reuse();
        table_spacing_change_rejects_incremental_layout_reuse();
    } catch (const std::exception& error) {
        std::cerr << "style repaint test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "style repaint tests passed\n";
    return 0;
}
