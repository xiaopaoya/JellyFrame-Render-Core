#include "render_core/css_parser.h"
#include "render_core/html_parser.h"
#include "render_core/layout.h"
#include "render_core/render_tree.h"
#include "../../src/computed_size_values.h"
#include "../../src/flex_auto_min.h"
#include "../../src/intrinsic_source_query.h"
#include "../../src/intrinsic_source_query_height.h"

#include <iostream>
#include <stdexcept>
#include <string>

using namespace jellyframe;
using namespace jellyframe::detail;

namespace {

void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

RenderObject* find(RenderObject& object, const char* id) {
    if (object.node && object.node->attribute("id") == id) return &object;
    for (auto& child : object.children)
        if (auto* found = find(*child, id)) return found;
    return nullptr;
}

const LayoutBox* find(const LayoutBox& box, const char* id) {
    if (box.node && box.node->attribute("id") == id) return &box;
    for (const auto& child : box.children)
        if (const auto* found = find(*child, id)) return found;
    return nullptr;
}

RenderObjectPtr render_tree(const Node& dom, const char* css) {
    StyleResolver resolver(CssParser{}.parse(css));
    return RenderTreeBuilder(resolver).build(dom);
}

void supported_block_evaluates_signed_components() {
    auto dom = HtmlParser{}.parse(
        "<div id='p'><div id='a'></div><div id='b'></div></div>");
    const char* css = "*{margin:0;padding:0;border:0}#p{width:200px;height:40px}"
        "#a,#b{height:10px}";
    auto tree = render_tree(*dom, css);
    auto* a = find(*tree, "a");
    auto* b = find(*tree, "b");
    check(a && b, "mixed Block fixture boxes exist");
    check(apply_computed_length_percentage(a->style, SizeProperty::Width, 10, 25),
        "positive signed pair publishes atomically");
    check(apply_computed_length_percentage(b->style, SizeProperty::Width, -10, 25),
        "negative length publishes without early clamping");
    check(a->style.width == -1 && a->style.width_percent == -1,
        "mixed publication hides scalar compatibility proxies");

    auto layout = LayoutEngine(StyleResolver(CssParser{}.parse(css))).layout(*tree, 320, 240);
    const auto* laid_a = find(*layout, "a");
    const auto* laid_b = find(*layout, "b");
    check(laid_a && laid_b && laid_a->rect.width == 60 && laid_b->rect.width == 40,
        "precise Block evaluation combines signed terms before final clamping");
    check(laid_a->style.computed_sizes.equals(a->style.computed_sizes) &&
        laid_a->style.table_properties == a->style.table_properties,
        "layout retains authored typed storage and provenance");
    TextSystem text_system;
    const auto* laid_parent = find(*layout, "p");
    check(laid_parent != nullptr, "intrinsic query root exists");
    const auto intrinsic_width = query_intrinsic_source_width(*laid_parent, text_system,
        {IntrinsicPercentageBasisKind::CyclicSameAxis, -1});
    auto* height_probe = const_cast<LayoutBox*>(laid_a);
    check(height_probe->style.computed_sizes.set_length_percentage(
        SizeProperty::MinHeight, 10, 25), "mixed intrinsic height constraint publishes");
    const auto intrinsic_height = query_intrinsic_source_height(*height_probe, text_system, 200000);
    check(intrinsic_width.status == IntrinsicSourceStatus::Unsupported,
        "intrinsic width query refuses typed mixed dimensions");
    check(intrinsic_height.status == IntrinsicSourceStatus::Unsupported,
        "intrinsic height query refuses typed mixed dimensions");

    auto pixel_dom = HtmlParser{}.parse(
        "<div id='p'><div id='a'></div><div id='b'></div></div>");
    StyleResolver pixel_resolver(CssParser{}.parse(
        "*{margin:0;padding:0;border:0}#p{width:200px;height:40px}"
        "#a{width:60px;height:10px}#b{width:40px;height:10px}"));
    auto pixel_tree = RenderTreeBuilder(pixel_resolver).build(*pixel_dom);
    auto pixel_layout = LayoutEngine(pixel_resolver).layout(*pixel_tree, 320, 240);
    check(find(*pixel_layout, "a")->rect.width == laid_a->rect.width &&
        find(*pixel_layout, "b")->rect.width == laid_b->rect.width,
        "mixed precise results match equivalent ordinary pixel geometry");
}

void unsupported_context_refuses_before_geometry() {
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
    auto dom = HtmlParser{}.parse(
        "<div id='prior'></div><div id='flex'><div id='mixed'></div></div>");
    StyleResolver resolver(CssParser{}.parse(
        "*{margin:0;padding:0;border:0}#prior{height:9px}"
        "#flex{display:flex;width:100px}#mixed{height:5px}"));
    auto tree = RenderTreeBuilder(resolver).build(*dom);
    auto* mixed = find(*tree, "mixed");
    check(mixed && apply_computed_length_percentage(
        mixed->style, SizeProperty::Width, 10, 25), "Flex mixed fixture publishes");
    VectorDiagnosticSink diagnostics;
    LayoutEngineOptions options;
    options.diagnostics = &diagnostics;
    auto layout = LayoutEngine(resolver, {}, options).layout(*tree, 320, 240);
    check(find(*layout, "prior")->rect.width == 0 && find(*layout, "mixed")->rect.width == 0,
        "unsupported mixed context refuses before laying out preceding siblings");
    bool refused = false;
    for (const auto& diagnostic : diagnostics.diagnostics())
        refused |= diagnostic.code == "unsupported-mixed-dimension-layout";
    check(refused, "unsupported mixed context reports an explicit diagnostic");
#endif

    auto basis_dom = HtmlParser{}.parse("<div id='basis'></div>");
    StyleResolver basis_resolver(CssParser{}.parse("*{margin:0;padding:0;border:0}"));
    auto basis_tree = RenderTreeBuilder(basis_resolver).build(*basis_dom);
    auto* basis = find(*basis_tree, "basis");
    check(basis && basis->style.computed_sizes.set_length_percentage(
        SizeProperty::FlexBasis, 10, 25), "typed FlexBasis can be constructed for refusal test");
    diagnostics.clear();
    auto basis_layout = LayoutEngine(basis_resolver, {}, options).layout(*basis_tree, 320, 240);
    check(find(*basis_layout, "basis")->rect.width == 0 &&
        !diagnostics.diagnostics().empty(), "FlexBasis remains explicitly unsupported");
}

void temporary_override_restores_typed_storage_and_provenance() {
    auto dom = HtmlParser{}.parse("<div id='flex'><div id='child'></div></div>");
    StyleResolver resolver(CssParser{}.parse(
        "*{margin:0;padding:0;border:0}#flex{display:flex;flex-direction:column;width:100px}"
        "#child{flex:1;height:25.5%}"));
    auto tree = RenderTreeBuilder(resolver).build(*dom);
    auto* child = find(*tree, "child");
    check(child && apply_computed_size(child->style, SizeProperty::Height,
        SizeKind::Percent, 25.5), "fractional percent creates retained height storage");
    child->style.table_properties |= table_sizing_provenance_bit(SizeProperty::Height);
    const auto saved_sizes = child->style.computed_sizes;
    const auto saved_provenance = child->style.table_properties;

    auto layout = LayoutEngine(resolver).layout(*tree, 320, 240);
    const auto* laid_child = find(*layout, "child");
    check(laid_child && laid_child->style.computed_sizes.equals(saved_sizes) &&
        laid_child->style.table_properties == saved_provenance,
        "ordinary Flex height override restores typed storage and provenance");
}

void flex_auto_min_treats_mixed_minimum_as_authored() {
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
    Style style;
    check(style.computed_sizes.set_length_percentage(SizeProperty::MinWidth, 10, 25),
        "mixed minimum publishes to typed storage");
    check(flex_auto_min_kind(style, FlexMainAxis::Horizontal) == FlexAutoMinKind::NotAuto,
        "typed mixed minimum is not mistaken for absent auto minimum");
#endif
}

} // namespace

int main() {
    try {
        supported_block_evaluates_signed_components();
        unsupported_context_refuses_before_geometry();
        temporary_override_restores_typed_storage_and_provenance();
        flex_auto_min_treats_mixed_minimum_as_authored();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "mixed dimension consumer tests passed\n";
    return 0;
}
