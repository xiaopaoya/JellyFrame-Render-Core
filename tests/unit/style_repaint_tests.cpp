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
    LayoutEngine layout_engine(resolver);
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
                                       *fixture.layout_tree),
          "paint-only style changes reuse layout without host text measurement");

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

void layout_affecting_style_and_text_changes_reject_reuse() {
    auto fixture = build_fixture(
        "<body><button id='pulse' class='pill'>Open</button></body>",
        ".pill { display: block; width: 80px; height: 20px; color: #111111; }"
        ".pill.wide { width: 96px; }");
    Node* pulse = find_element_by_id(*fixture.document, "pulse");
    check(pulse != nullptr, "reuse rejection target exists");

    pulse->set_attribute("class", "pill wide");
    auto wide_render = rebuild_render_tree(*fixture.document, fixture.resolver);
    check(!style_dirty_can_reuse_layout(*fixture.document,
                                        *fixture.render_tree,
                                        *wide_render,
                                        *fixture.layout_tree),
          "layout-affecting computed style rejects retained layout");

    clear_dirty_flags(*fixture.document);
    pulse->set_attribute("class", "pill");
    clear_dirty_flags(*fixture.document);
    pulse->children.front()->set_text("A different label");
    auto text_render = rebuild_render_tree(*fixture.document, fixture.resolver);
    check(!style_dirty_can_reuse_layout(*fixture.document,
                                        *fixture.render_tree,
                                        *text_render,
                                        *fixture.layout_tree),
          "text mutations reject layout reuse without consulting font metrics");
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
    LayoutEngine engine(fixture.resolver);
    auto fresh_layout = engine.layout(*next_render, 240);
    const LayoutBox* fresh_table = find_layout_by_id(*fresh_layout, "grid");
    check(fresh_table != nullptr && fresh_table->rect.width > old_width,
          "full table layout applies changed separate-border spacing");
}

} // namespace

int main() {
    try {
        paint_only_class_change_can_reuse_layout();
        layout_affecting_style_and_text_changes_reject_reuse();
        transform_class_change_invalidates_old_and_new_bounds();
        table_spacing_change_rejects_incremental_layout_reuse();
    } catch (const std::exception& error) {
        std::cerr << "style repaint test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "style repaint tests passed\n";
    return 0;
}
