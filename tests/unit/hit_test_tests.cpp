#include "render_core/css_parser.h"
#include "render_core/hit_test.h"
#include "render_core/form_control.h"
#include "render_core/html_parser.h"
#include "render_core/layer_tree.h"
#include "render_core/layout.h"
#include "render_core/render_tree.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct Pipeline {
    std::unique_ptr<Node> document;
    Stylesheet stylesheet;
    StyleResolver resolver;
    RenderObjectPtr render_tree;
    LayoutBoxPtr layout_tree;
    LayerNodePtr layer_tree;

    Pipeline(std::unique_ptr<Node> document_in,
             Stylesheet stylesheet_in,
             StyleResolver resolver_in,
             RenderObjectPtr render_tree_in,
             LayoutBoxPtr layout_tree_in,
             LayerNodePtr layer_tree_in)
        : document(std::move(document_in)),
          stylesheet(std::move(stylesheet_in)),
          resolver(std::move(resolver_in)),
          render_tree(std::move(render_tree_in)),
          layout_tree(std::move(layout_tree_in)),
          layer_tree(std::move(layer_tree_in)) {}
};

Pipeline build_pipeline(const char* html,
                        const char* css,
                        int viewport_width = 240,
                        LayerTreeBuilderOptions layer_options = {}) {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(html);
    Stylesheet stylesheet = css_parser.parse(css);
    StyleResolver resolver(stylesheet);
    RenderTreeBuilder render_tree_builder(resolver);
    auto render_tree = render_tree_builder.build(*document);
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*render_tree, viewport_width);
    LayerTreeBuilder layer_tree_builder(layer_options);
    auto layer_tree = layer_tree_builder.build(*layout_tree);
    return Pipeline(std::move(document), std::move(stylesheet), std::move(resolver),
                    std::move(render_tree), std::move(layout_tree), std::move(layer_tree));
}

int fixed_scroll_offset(const Node& node, int max_scroll_y, void*) {
    if (node.attribute("id") == "list") {
        return max_scroll_y;
    }
    return 0;
}

void basic_hit_test_returns_deepest_element() {
    auto pipeline = build_pipeline(
        "<body><section class='card'><button id='go'>Go</button></section></body>",
        "body { padding: 0; } .card { padding: 8px; background: #ffffff; }"
        "button { display: inline-block; width: 80px; height: 24px; }");

    HitTester hit_tester;
    HitTestResult result = hit_tester.hit_test(*pipeline.layer_tree, 12, 12);

    check(static_cast<bool>(result), "hit result exists");
    check(result.node != nullptr && result.node->attribute("id") == "go", "button is hit target");
}

void layer_order_prefers_higher_z_index() {
    auto pipeline = build_pipeline(
        "<body><div id='back'>Back</div><div id='front'>Front</div></body>",
        "div { display: block; width: 100px; height: 40px; position: relative; }"
        "#back { background: #2563eb; z-index: 1; }"
        "#front { background: #dc2626; z-index: 2; margin: -40px; }");

    HitTester hit_tester;
    HitTestResult result = hit_tester.hit_test(*pipeline.layer_tree, 4, 4);

    check(static_cast<bool>(result), "z-index hit result exists");
    check(result.node != nullptr && result.node->attribute("id") == "front", "higher z-index layer wins");
}

void overflow_clip_blocks_outside_hits() {
    auto pipeline = build_pipeline(
        "<body><section class='clip'><button id='inside'>Inside</button></section></body>",
        ".clip { width: 80px; height: 20px; overflow: hidden; }"
        "button { display: block; width: 80px; height: 40px; }");

    HitTester hit_tester;
    HitTestResult inside = hit_tester.hit_test(*pipeline.layer_tree, 4, 4);
    HitTestResult clipped = hit_tester.hit_test(*pipeline.layer_tree, 4, 30);

    check(static_cast<bool>(inside), "inside clipped layer hit exists");
    check(inside.node != nullptr && inside.node->attribute("id") == "inside", "inside button hit");
    check(!clipped || clipped.node->attribute("id") != "inside", "outside clip does not hit button");
}

void rounded_overflow_clip_blocks_corner_hits() {
    auto pipeline = build_pipeline(
        "<body><section class='clip'><button id='inside'>Inside</button></section></body>",
        "body { margin: 0; } .clip { width: 40px; height: 40px; overflow: hidden; border-radius: 12px; }"
        "button { display: block; width: 40px; height: 40px; }");

    HitTester hit_tester;
    HitTestResult center = hit_tester.hit_test(*pipeline.layer_tree, 20, 20);
    HitTestResult corner = hit_tester.hit_test(*pipeline.layer_tree, 0, 0);

    check(center.node != nullptr && center.node->attribute("id") == "inside",
          "rounded clip center hit targets the child");
    check(!corner || corner.node->attribute("id") != "inside",
          "rounded clip corner does not hit the child");
}

void scroll_container_hit_test_uses_scrolled_content_coordinates() {
    LayerTreeBuilderOptions options;
    options.scroll_resolver = ScrollOffsetResolver{fixed_scroll_offset, nullptr};
    auto pipeline = build_pipeline(
        "<body><section id='list'><button id='one'>One</button><button id='two'>Two</button></section></body>",
        "body { margin: 0; }"
        "#list { width: 90px; height: 24px; overflow: scroll; }"
        "button { display: block; width: 90px; height: 24px; }",
        120,
        options);

    HitTester hit_tester;
    HitTestResult result = hit_tester.hit_test(*pipeline.layer_tree, 8, 8);

    check(static_cast<bool>(result), "scrolled hit result exists");
    check(result.node != nullptr && result.node->attribute("id") == "two",
          "scroll container hit test targets visible scrolled child");
}

void visible_overflow_descendants_remain_hittable_outside_ancestor_rectangles() {
    for (const char* css : {
        "*{margin:0;padding:0}#parent{width:20px;height:20px}#child{width:80px;height:40px}",
        "*{margin:0;padding:0}#parent{width:20px;height:0}#child{position:absolute;left:0;top:0;width:80px;height:40px}"}) {
        auto pipeline = build_pipeline(
            "<body><section id='parent'><button id='child'>x</button></section></body>", css);
        HitTester hit;
        for (const auto result : {hit.hit_test(*pipeline.layout_tree, 60, 30),
                                  hit.hit_test(*pipeline.layer_tree, 60, 30)}) {
            check(result.node != nullptr && result.node->attribute("id") == "child",
                  "visible overflow remains hittable through narrow/zero-height ancestors");
            check(result.local_x == 60 && result.local_y == 30,
                  "overflow local coordinates remain relative to the actual target");
        }
        check(!hit.hit_test(*pipeline.layout_tree, 200, 200) &&
              !hit.hit_test(*pipeline.layer_tree, 200, 200),
              "searching overflow does not create an ancestor hit outside every rectangle");
    }
}

void all_overflow_clip_modes_prune_descendants_in_both_entry_points() {
    for (const char* mode : {"hidden", "clip", "scroll", "auto"}) {
        const std::string css = std::string("*{margin:0;padding:0}#parent{width:20px;height:20px;overflow:") +
            mode + "}#child{width:80px;height:40px}";
        auto pipeline = build_pipeline(
            "<body><section id='parent'><button id='child'>x</button></section></body>", css.c_str());
        HitTester hit;
        for (const auto result : {hit.hit_test(*pipeline.layout_tree, 60, 30),
                                  hit.hit_test(*pipeline.layer_tree, 60, 30)}) {
            check(!result || result.node->attribute("id") != "child",
                  "overflow clip mode blocks child outside clip in both entry points");
        }
    }
}

void rounded_clip_rejection_keeps_unrelated_lower_sibling_hittable() {
    for (const char* radius : {"12px", "50%", "20px / 10px"}) {
        const std::string css = std::string("*{margin:0;padding:0}#back{width:40px;height:40px}") +
            "#clip{position:absolute;left:0;top:0;width:40px;height:40px;overflow:hidden;border-radius:" + radius +
            "}#front{width:40px;height:40px}";
        auto pipeline = build_pipeline(
            "<body><button id='back'>back</button><section id='clip'><button id='front'>front</button></section></body>",
            css.c_str());
        HitTester hit;
        for (const auto result : {hit.hit_test(*pipeline.layout_tree, 0, 0),
                                  hit.hit_test(*pipeline.layer_tree, 0, 0)}) {
            check(result.node != nullptr && result.node->attribute("id") == "back",
                  "rejected rounded subtree does not suppress an unrelated underlying sibling");
        }
        const auto center = hit.hit_test(*pipeline.layer_tree, 20, 20);
        check(center.node != nullptr && center.node->attribute("id") == "front",
              "rounded clip still admits center in front layer");
    }
}

void transformed_layer_does_not_reappear_at_untransformed_fallback_position() {
    auto pipeline = build_pipeline(
        "<body><button id='back'>back</button><button id='front'>front</button></body>",
        "*{margin:0;padding:0;border:0}#back{width:20px;height:20px}"
        "#front{position:absolute;top:0;left:0;width:20px;height:20px;transform:translate(40px,0)}");
    HitTester hit;
    const auto old_position = hit.hit_test(*pipeline.layer_tree, 5, 5);
    const auto translated = hit.hit_test(*pipeline.layer_tree, 45, 5);
    check(old_position.node != nullptr && old_position.node->attribute("id") == "back",
          "translated child cannot be rediscovered at its old layout position");
    check(translated.node != nullptr && translated.node->attribute("id") == "front" &&
          translated.local_x == 5 && translated.local_y == 5,
          "translated child hit and local coordinates are preserved");
}


#if JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
Node* find_hit_node(Node& node, const char* id) {
    if (node.attribute("id") == id) return &node;
    for (auto& child : node.children) {
        if (auto* found = find_hit_node(*child, id)) return found;
    }
    return nullptr;
}
const LayerNode* find_hit_popup(const LayerNode& layer) {
    if ((layer.reasons & LayerReasonTransientOverlay) != 0U) return &layer;
    for (const auto& child : layer.children) {
        if (const auto* found = find_hit_popup(*child)) return found;
    }
    return nullptr;
}
int popup_test_scroll(const Node& node, int maximum, void*) {
    return node.attribute("id") == "list" ? std::min(100, maximum) : 0;
}
void scrolled_popup_owns_only_popup_bounds_and_keeps_control_hittable() {
    LayerTreeBuilderOptions options;
    options.scroll_resolver = ScrollOffsetResolver{popup_test_scroll, nullptr};
    auto pipeline = build_pipeline(
        "<body><section id='list'><div id='before'></div><select id='choice'>"
        "<option>One</option></select><div id='gap'></div><button id='back'>back</button>"
        "<div id='tail'></div></section></body>",
        "*{margin:0;padding:0;border:0}body{height:300px}"
        "#list{width:120px;height:160px;overflow:auto}#before{height:100px}"
        "select{display:block;width:100px;height:20px}#gap{height:80px}"
        "#back{display:block;width:100px;height:20px}#tail{height:200px}", 240, options);
    Node* select = find_hit_node(*pipeline.document, "choice");
    check(select != nullptr && set_select_popup_open(*select, true), "scrolled select opens");
    LayerTreeBuilder builder(options);
    pipeline.layer_tree = builder.build(*pipeline.layout_tree);
    const LayerNode* popup = find_hit_popup(*pipeline.layer_tree);
    check(popup != nullptr, "scrolled select creates actual popup layer");
    HitTester hit;
    const auto popup_hit = hit.hit_test(*pipeline.layer_tree, popup->bounds.x + 5, popup->bounds.y + 5);
    check(popup_hit.node == select, "popup bounds target the select");
    const auto control_hit = hit.hit_test(*pipeline.layer_tree, 5, 5);
    check(control_hit.node == select && control_hit.local_y == 5,
          "open popup does not exclusively own normal scrolled control");
    const auto stale_hit = hit.hit_test(*pipeline.layer_tree, 5, 105);
    check(stale_hit.node != nullptr && stale_hit.node->attribute("id") == "back",
          "popup cannot steal sibling hit at select unscrolled coordinates");
}
#endif

} // namespace

int main() {
    try {
        basic_hit_test_returns_deepest_element();
#if JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
        scrolled_popup_owns_only_popup_bounds_and_keeps_control_hittable();
#endif
        layer_order_prefers_higher_z_index();
        overflow_clip_blocks_outside_hits();
        rounded_overflow_clip_blocks_corner_hits();
        scroll_container_hit_test_uses_scrolled_content_coordinates();
        visible_overflow_descendants_remain_hittable_outside_ancestor_rectangles();
        all_overflow_clip_modes_prune_descendants_in_both_entry_points();
        rounded_clip_rejection_keeps_unrelated_lower_sibling_hittable();
        transformed_layer_does_not_reappear_at_untransformed_fallback_position();
    } catch (const std::exception& error) {
        std::cerr << "hit test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "hit test tests passed\n";
    return 0;
}
