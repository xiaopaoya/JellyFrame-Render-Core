#include "render_core/css_parser.h"
#include "render_core/html_parser.h"
#include "render_core/layout.h"
#include "render_core/render_tree.h"
#include "render_core/font_resource.h"
#include "render_core/text_system.h"
#include "../../src/computed_size_values.h"
#include "../../src/flex_auto_min.h"
#include "../../src/intrinsic_source_query.h"
#include "../../src/intrinsic_source_query_height.h"
#include "../../src/grid_track_provenance.h"

#include <iostream>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

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

void put_u16(std::vector<std::uint8_t>& bytes, std::size_t offset, unsigned value) {
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8U);
}

void put_u32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes[offset + i] = static_cast<std::uint8_t>(value >> (8U * i));
}

struct TestFont {
    std::vector<std::uint8_t> bytes;
    FontResource font;
};

TestFont make_test_font() {
    TestFont asset;
    constexpr std::size_t bitmap_offset = 168;
    asset.bytes.resize(bitmap_offset, 0);
    const char magic[] = "JFFONT2";
    for (unsigned i = 0; i < 8; ++i) asset.bytes[i] = static_cast<std::uint8_t>(magic[i]);
    put_u16(asset.bytes, 8, 2); put_u16(asset.bytes, 10, 128);
    put_u16(asset.bytes, 12, 40); put_u16(asset.bytes, 14, 1);
    put_u32(asset.bytes, 20, 1); put_u32(asset.bytes, 24, 128);
    put_u32(asset.bytes, 28, static_cast<std::uint32_t>(bitmap_offset));
    put_u16(asset.bytes, 36, 1000); put_u16(asset.bytes, 38, 16);
    put_u16(asset.bytes, 40, 400); asset.bytes[42] = 4;
    put_u32(asset.bytes, 44, 800); put_u32(asset.bytes, 48, 200); put_u32(asset.bytes, 52, 50);
    asset.bytes[64] = 1; put_u32(asset.bytes, 100, 1); put_u16(asset.bytes, 104, 1);
    put_u32(asset.bytes, 128, 'A'); put_u32(asset.bytes, 132, 1); put_u32(asset.bytes, 136, 500);
    put_u32(asset.bytes, 156, 0); put_u32(asset.bytes, 160, 2); put_u16(asset.bytes, 148, 1);
    put_u16(asset.bytes, 150, 2); put_u32(asset.bytes, 152, 1);
    asset.bytes.push_back(0xf0); asset.bytes.push_back(0xf0);
    put_u32(asset.bytes, 16, static_cast<std::uint32_t>(asset.bytes.size()));
    put_u32(asset.bytes, 32, static_cast<std::uint32_t>(asset.bytes.size() - bitmap_offset));
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t index = 0; index < asset.bytes.size(); ++index) {
        const auto value = index >= 96 && index < 100 ? 0 : asset.bytes[index];
        crc ^= value;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1U) ^ ((crc & 1U) ? 0xedb88320U : 0U);
    }
    put_u32(asset.bytes, 96, ~crc);
    check(asset.font.load(asset.bytes.data(), asset.bytes.size()) == FontResourceStatus::Loaded,
          "synthetic test font loads");
    return asset;
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

void block_axes_and_ranges_match_equivalent_pixels() {
    auto dom = HtmlParser{}.parse(
        "<div id='p'><div id='mixed'></div></div>");
    const char* css = "*{margin:0;padding:0;border:0}#p{width:200px;height:100px}";
    auto tree = render_tree(*dom, css);
    auto* mixed = find(*tree, "mixed");
    check(mixed != nullptr, "Block axis fixture exists");
    check(apply_computed_length_percentage(mixed->style, SizeProperty::Width, 200, 0) &&
          apply_computed_length_percentage(mixed->style, SizeProperty::MinWidth, 80, -50) &&
          apply_computed_length_percentage(mixed->style, SizeProperty::MaxWidth, 30, 50) &&
          apply_computed_length_percentage(mixed->style, SizeProperty::Height, 20, 50) &&
          apply_computed_length_percentage(mixed->style, SizeProperty::MinHeight, 80, -50) &&
          apply_computed_length_percentage(mixed->style, SizeProperty::MaxHeight, 50, 50),
          "all six Block dimensions retain their typed components");
    auto layout = LayoutEngine(StyleResolver(CssParser{}.parse(css))).layout(*tree, 320, 240);
    const auto* result = find(*layout, "mixed");
    check(result && result->rect.width == 130 && result->rect.height == 70,
          "signed mixed width/height and min/max resolve against their definite axis bases");
    check(result->style.computed_sizes.equals(mixed->style.computed_sizes) &&
          result->style.width == -1 && result->style.height == -1 &&
          result->style.min_width == -1 && result->style.min_height == -1 &&
          result->style.max_width == -1 && result->style.max_height == -1,
          "Block layout preserves the full authored typed snapshot and hidden proxies");

    auto pixel_dom = HtmlParser{}.parse(
        "<div id='p'><div id='mixed'></div></div>");
    const char* pixel_css = "*{margin:0;padding:0;border:0}#p{width:200px;height:100px}"
        "#mixed{width:130px;height:70px;min-width:0;max-width:130px;min-height:30px;max-height:100px}";
    StyleResolver pixel_resolver(CssParser{}.parse(pixel_css));
    auto pixel_tree = RenderTreeBuilder(pixel_resolver).build(*pixel_dom);
    auto pixel_layout = LayoutEngine(pixel_resolver).layout(*pixel_tree, 320, 240);
    const auto* pixel = find(*pixel_layout, "mixed");
    check(pixel && result->rect.width == pixel->rect.width && result->rect.height == pixel->rect.height,
          "mixed Block geometry matches the equivalent integer-pixel range");

    auto indefinite_dom = HtmlParser{}.parse(
        "<div id='p'><div id='mixed'></div></div>");
    const char* indefinite_css = "*{margin:0;padding:0;border:0}#p{width:200px}";
    auto indefinite_tree = render_tree(*indefinite_dom, indefinite_css);
    auto* indefinite = find(*indefinite_tree, "mixed");
    check(indefinite && apply_computed_length_percentage(
        indefinite->style, SizeProperty::Height, 50, 50),
        "indefinite-height mixed expression publishes");
    auto indefinite_layout = LayoutEngine(StyleResolver(CssParser{}.parse(indefinite_css)))
        .layout(*indefinite_tree, 320, 240);
    const auto* indefinite_result = find(*indefinite_layout, "mixed");
    check(indefinite_result && indefinite_result->rect.height == 0,
          "indefinite Block height treats the whole percentage-bearing expression as auto");
}

void unsupported_context_refuses_before_geometry() {
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
    auto dom = HtmlParser{}.parse(
        "<div id='prior'></div><div id='flex'><div id='mixed'></div></div>");
    StyleResolver resolver(CssParser{}.parse(
        "*{margin:0;padding:0;border:0}#prior{height:9px}"
        "#flex{display:flex;flex-wrap:wrap;width:100px}#mixed{height:5px}"));
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

    auto table_dom = HtmlParser{}.parse("<table id='table'><tr><td id='cell'></td></tr></table>");
    StyleResolver table_resolver(CssParser{}.parse(
        "*{margin:0;padding:0;border:0}#table{width:100px}"));
    auto table_tree = RenderTreeBuilder(table_resolver).build(*table_dom);
    auto* table_cell = find(*table_tree, "cell");
    check(table_cell && apply_computed_length_percentage(
        table_cell->style, SizeProperty::Width, 20, 50),
        "fixed-table mixed sizing fixture publishes");
    VectorDiagnosticSink table_diagnostics;
    LayoutEngineOptions table_options;
    table_options.diagnostics = &table_diagnostics;
    auto table_layout = LayoutEngine(table_resolver, {}, table_options).layout(*table_tree, 320, 240);
    check(find(*table_layout, "cell")->rect.width == 0 && !table_diagnostics.diagnostics().empty(),
          "fixed table refuses mixed dimensions before track or cell geometry changes");

    auto basis_dom = HtmlParser{}.parse("<div id='basis'></div>");
    StyleResolver basis_resolver(CssParser{}.parse("*{margin:0;padding:0;border:0}"));
    auto basis_tree = RenderTreeBuilder(basis_resolver).build(*basis_dom);
    auto* basis = find(*basis_tree, "basis");
    check(basis && basis->style.computed_sizes.set_length_percentage(
        SizeProperty::FlexBasis, 10, 25), "typed FlexBasis can be constructed for refusal test");
    diagnostics.clear();
    auto basis_layout = LayoutEngine(basis_resolver, {}, options).layout(*basis_tree, 320, 240);
    check(find(*basis_layout, "basis")->rect.width == 0 &&
        !diagnostics.diagnostics().empty(),
        "FlexBasis outside a qualified Flex item is explicitly refused");
}

void qualified_nowrap_flex_resolves_mixed_basis_and_constraints() {
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
    auto dom = HtmlParser{}.parse("<div id='flex'><div id='mixed'></div></div>");
    const char* css = "*{margin:0;padding:0;border:0}"
        "#flex{display:flex;width:100px;height:80px;align-items:flex-start}#mixed{flex:0 1 auto}";
    StyleResolver resolver(CssParser{}.parse(css));
    auto tree = RenderTreeBuilder(resolver).build(*dom);
    auto* mixed = find(*tree, "mixed");
    check(mixed &&
          apply_computed_length_percentage(mixed->style, SizeProperty::FlexBasis, 10, 25) &&
          apply_computed_length_percentage(mixed->style, SizeProperty::MinWidth, 80, -50) &&
          apply_computed_length_percentage(mixed->style, SizeProperty::MaxWidth, 30, 50) &&
          apply_computed_length_percentage(mixed->style, SizeProperty::Height, 20, 50),
          "qualified nowrap Flex item retains mixed basis and definite-axis constraints");
    const auto authored = mixed->style.computed_sizes;
    auto layout = LayoutEngine(resolver).layout(*tree, 320, 240);
    const auto* item = find(*layout, "mixed");
    check(item && item->rect.width == 35 && item->rect.height == 60 &&
          item->style.computed_sizes.equals(authored) && item->style.flex_basis == -1,
          "nowrap Flex solver uses the signed typed basis and restores the authored item Style");

    auto pixel_dom = HtmlParser{}.parse("<div id='flex'><div id='mixed'></div></div>");
    const char* pixel_css = "*{margin:0;padding:0;border:0}"
        "#flex{display:flex;width:100px;height:80px;align-items:flex-start}"
        "#mixed{flex:0 1 35px;width:35px;min-width:30px;max-width:80px;height:60px}";
    StyleResolver pixel_resolver(CssParser{}.parse(pixel_css));
    auto pixel_tree = RenderTreeBuilder(pixel_resolver).build(*pixel_dom);
    auto pixel_layout = LayoutEngine(pixel_resolver).layout(*pixel_tree, 320, 240);
    const auto* pixel_item = find(*pixel_layout, "mixed");
    check(pixel_item && item->rect.width == pixel_item->rect.width &&
          item->rect.height == pixel_item->rect.height,
          "qualified mixed nowrap Flex geometry matches equivalent pixel dimensions");

    auto column_dom = HtmlParser{}.parse("<div id='flex'><div id='mixed'></div></div>");
    const char* column_css = "*{margin:0;padding:0;border:0}"
        "#flex{display:flex;flex-direction:column;width:100px;height:80px;align-items:flex-start}"
        "#mixed{flex:0 1 auto}";
    StyleResolver column_resolver(CssParser{}.parse(column_css));
    auto column_tree = RenderTreeBuilder(column_resolver).build(*column_dom);
    auto* column_mixed = find(*column_tree, "mixed");
    check(column_mixed &&
          apply_computed_length_percentage(column_mixed->style, SizeProperty::FlexBasis, 20, 30) &&
          apply_computed_length_percentage(column_mixed->style, SizeProperty::Width, 10, 50) &&
          apply_computed_length_percentage(column_mixed->style, SizeProperty::MinHeight, 80, -50) &&
          apply_computed_length_percentage(column_mixed->style, SizeProperty::MaxHeight, 20, 50),
          "qualified column Flex retains mixed basis and vertical constraints");
    auto column_layout = LayoutEngine(column_resolver).layout(*column_tree, 320, 240);
    const auto* column_item = find(*column_layout, "mixed");
    check(column_item && column_item->rect.width == 60 && column_item->rect.height == 44,
          "column Flex resolves basis on height and preferred width on the definite cross axis");

    auto indefinite_dom = HtmlParser{}.parse("<div id='flex'><div id='mixed'></div></div>");
    const char* indefinite_css = "*{margin:0;padding:0;border:0}"
        "#flex{display:flex;flex-direction:column;width:100px}#mixed{flex:0 1 auto}";
    StyleResolver indefinite_resolver(CssParser{}.parse(indefinite_css));
    auto indefinite_tree = RenderTreeBuilder(indefinite_resolver).build(*indefinite_dom);
    auto* indefinite_item = find(*indefinite_tree, "mixed");
    check(indefinite_item && apply_computed_length_percentage(
        indefinite_item->style, SizeProperty::FlexBasis, 50, 50),
        "indefinite column Flex basis retains its length term");
    const auto indefinite_authored = indefinite_item->style;
    VectorDiagnosticSink indefinite_diagnostics;
    LayoutEngineOptions indefinite_options;
    indefinite_options.diagnostics = &indefinite_diagnostics;
    auto indefinite_layout = LayoutEngine(indefinite_resolver, {}, indefinite_options)
        .layout(*indefinite_tree, 320, 240);
    const auto* indefinite_result = find(*indefinite_layout, "mixed");
    check(indefinite_result && indefinite_result->rect.height == 0 &&
          indefinite_diagnostics.diagnostics().empty() &&
          indefinite_result->style.computed_sizes.equals(indefinite_authored.computed_sizes) &&
          indefinite_result->style.flex_basis == indefinite_authored.flex_basis &&
          indefinite_result->style.flex_basis_kind == indefinite_authored.flex_basis_kind,
          "indefinite column Flex fallback treats the whole mixed basis as content and restores Style");
#endif
}

void qualified_wrapped_row_flex_resolves_mixed_basis() {
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
    auto dom = HtmlParser{}.parse(
        "<div id='flex'><div id='first'>A</div><div id='second'>A</div></div>");
    const char* css = "*{margin:0;padding:0;border:0}"
        "#flex{display:flex;flex-wrap:wrap;width:100px}#first,#second{flex:0 1 auto}";
    StyleResolver resolver(CssParser{}.parse(css));
    auto tree = RenderTreeBuilder(resolver).build(*dom);
    auto* first = find(*tree, "first");
    auto* second = find(*tree, "second");
    check(first && second &&
          apply_computed_length_percentage(first->style, SizeProperty::FlexBasis, 20, 30) &&
          apply_computed_length_percentage(second->style, SizeProperty::FlexBasis, 60, 0),
          "qualified wrapped-row Flex retains authored mixed basis");
    auto font = make_test_font();
    TextFontFace face{1, 0, font.font};
    TextSystem text_system;
    check(text_system.bind(&face, 1) == TextSystemStatus::Ok, "wrapped Flex test font binds");
    LayoutEngineOptions options;
    options.text_system = &text_system;
    VectorDiagnosticSink diagnostics;
    options.diagnostics = &diagnostics;
    auto layout = LayoutEngine(resolver, {}, options).layout(*tree, 320, 240);
    const auto* first_item = find(*layout, "first");
    const auto* second_item = find(*layout, "second");
    SizeKind restored_kind = SizeKind::Pixels;
    std::int64_t restored_length = 0, restored_percentage = 0;
    const bool restored_basis = first_item && first_item->style.computed_sizes.read_typed(
        SizeProperty::FlexBasis, restored_kind, restored_length, restored_percentage);
    for (const auto& diagnostic : diagnostics.diagnostics())
        check(diagnostic.code != "unsupported-mixed-dimension-layout",
              "qualified wrapped-row Flex completes without mixed-dimension refusal");
    check(first_item && second_item && first_item->rect.width == 50 && second_item->rect.width == 60 &&
          second_item->rect.y > first_item->rect.y && diagnostics.diagnostics().empty() &&
          restored_basis && restored_kind == SizeKind::LengthPercentage &&
          restored_length == 20000 && restored_percentage == 30000,
          "wrapped-row Flex resolves basis and places the second item on the next line");
#endif
}

void qualified_flex_success_preserves_authored_state() {
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
    auto dom = HtmlParser{}.parse("<div id='flex'><div id='item'></div></div>");
    const char* css = "*{margin:0;padding:0;border:0}"
        "#flex{display:flex;width:100px;height:80px;align-items:flex-start}#item{flex:0 1 auto}";
    StyleResolver resolver(CssParser{}.parse(css));
    auto tree = RenderTreeBuilder(resolver).build(*dom);
    auto* item = find(*tree, "item");
    check(item && apply_computed_length_percentage(item->style, SizeProperty::FlexBasis, 20, 30) &&
          apply_computed_length_percentage(item->style, SizeProperty::MinWidth, 80, -50),
          "refusal fixture mixed basis and minimum publish");
    const auto authored_sizes = item->style.computed_sizes;
    const auto authored_style = item->style;
    VectorDiagnosticSink diagnostics;
    LayoutEngineOptions options;
    options.diagnostics = &diagnostics;
    auto layout = LayoutEngine(resolver, {}, options).layout(*tree, 320, 240);
    const auto* result = find(*layout, "item");
    check(result && result->rect.width == 50 &&
          result->style.computed_sizes.equals(authored_sizes) &&
          result->style.flex_basis == authored_style.flex_basis &&
          result->style.flex_basis_kind == authored_style.flex_basis_kind &&
          result->style.width == authored_style.width &&
          result->style.min_width == authored_style.min_width &&
          result->style.table_properties == authored_style.table_properties,
          "qualified Flex layout preserves mixed values and restores full authored style");
    check(diagnostics.diagnostics().empty(),
          "qualified Flex layout does not report mixed-dimension refusal");
#endif
}

void qualified_grid_item_resolves_mixed_width_constraints() {
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
    auto dom = HtmlParser{}.parse("<div id='grid'><div id='mixed'></div><div id='other'></div></div>");
    const char* css = "*{margin:0;padding:0;border:0}"
        "#grid{display:grid;width:200px;grid-template-columns:100px 100px}#mixed{height:10px}";
    StyleResolver resolver(CssParser{}.parse(css));
    auto tree = RenderTreeBuilder(resolver).build(*dom);
    auto* mixed = find(*tree, "mixed");
    check(mixed &&
          apply_computed_length_percentage(mixed->style, SizeProperty::Width, 100, 0) &&
          apply_computed_length_percentage(mixed->style, SizeProperty::MinWidth, 80, -50) &&
          apply_computed_length_percentage(mixed->style, SizeProperty::MaxWidth, 30, 50),
          "qualified Grid item retains mixed preferred/min/max width");
    const auto authored = mixed->style.computed_sizes;
    auto layout = LayoutEngine(resolver).layout(*tree, 320, 240);
    const auto* item = find(*layout, "mixed");
    check(item != nullptr, "qualified mixed Grid item is laid out");
    check(item->rect.width == 80, "Grid assigned track resolves mixed width constraints");
    check(item->style.computed_sizes.equals(authored) && item->style.width == -1 &&
          item->style.min_width == -1 && item->style.max_width == -1,
          "Grid restores the full temporary mixed Style");

    auto pixel_dom = HtmlParser{}.parse("<div id='grid'><div id='mixed'></div><div id='other'></div></div>");
    const char* pixel_css = "*{margin:0;padding:0;border:0}"
        "#grid{display:grid;width:200px;grid-template-columns:100px 100px}"
        "#mixed{width:100px;min-width:30px;max-width:80px;height:10px}";
    StyleResolver pixel_resolver(CssParser{}.parse(pixel_css));
    auto pixel_tree = RenderTreeBuilder(pixel_resolver).build(*pixel_dom);
    auto pixel_layout = LayoutEngine(pixel_resolver).layout(*pixel_tree, 320, 240);
    const auto* pixel_item = find(*pixel_layout, "mixed");
    check(pixel_item && item->rect.width == pixel_item->rect.width,
          "Grid mixed item width matches the equivalent pixel constraint");

    const auto verify_vertical_refusal = [](SizeProperty property, bool on_grid) {
        auto vertical_dom = HtmlParser{}.parse(
            "<div id='grid'><div id='mixed'></div><div id='other'></div></div>");
        StyleResolver vertical_resolver(CssParser{}.parse(
            "*{margin:0;padding:0;border:0}"
            "#grid{display:grid;width:200px;grid-template-columns:100px 100px}"
            "#mixed,#other{height:10px}"));
        auto vertical_tree = RenderTreeBuilder(vertical_resolver).build(*vertical_dom);
        auto* grid_source = find(*vertical_tree, "grid");
        auto* item_source = find(*vertical_tree, "mixed");
        auto* target = on_grid ? grid_source : item_source;
        check(target && apply_computed_length_percentage(target->style, property, 20, 50),
              "Grid vertical mixed refusal fixture publishes");

        VectorDiagnosticSink vertical_diagnostics;
        LayoutEngineOptions vertical_options;
        vertical_options.diagnostics = &vertical_diagnostics;
        auto vertical_layout = LayoutEngine(vertical_resolver, {}, vertical_options)
            .layout(*vertical_tree, 320, 240);
        const auto* grid = find(*vertical_layout, "grid");
        const auto* mixed = find(*vertical_layout, "mixed");
        const auto* other = find(*vertical_layout, "other");
        check(grid && mixed && other &&
              grid->rect.width == 0 && grid->rect.height == 0 &&
              mixed->rect.width == 0 && mixed->rect.height == 0 &&
              other->rect.width == 0 && other->rect.height == 0,
              "Grid vertical mixed sizing refuses before assigning any geometry");
        check(!vertical_diagnostics.diagnostics().empty() &&
              vertical_diagnostics.diagnostics().back().code ==
                  "unsupported-mixed-dimension-layout",
              "Grid vertical mixed sizing reports the explicit unsupported-context diagnostic");
    };
    for (const auto property : {SizeProperty::Height, SizeProperty::MinHeight, SizeProperty::MaxHeight}) {
        verify_vertical_refusal(property, false);
        verify_vertical_refusal(property, true);
    }
#endif
}

void mixed_grid_refuses_unresolved_tracks_and_spans_before_geometry() {
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
    const auto verify_refused = [](const char* css, const char* label) {
        auto dom = HtmlParser{}.parse(
            "<div id='grid'><div id='mixed'></div><div id='other'></div></div>");
        StyleResolver resolver(CssParser{}.parse(css));
        auto tree = RenderTreeBuilder(resolver).build(*dom);
        auto* mixed = find(*tree, "mixed");
        check(mixed && apply_computed_length_percentage(
            mixed->style, SizeProperty::Width, 40, 50),
            "Grid refusal fixture publishes typed mixed width");
        VectorDiagnosticSink diagnostics;
        LayoutEngineOptions options;
        options.diagnostics = &diagnostics;
        auto layout = LayoutEngine(resolver, {}, options).layout(*tree, 320, 240);
        const auto* grid = find(*layout, "grid");
        const auto* item = find(*layout, "mixed");
        check(grid && item && grid->rect.width == 0 && item->rect.width == 0 && item->rect.height == 0,
              label);
        check(!diagnostics.diagnostics().empty() &&
              diagnostics.diagnostics().back().code == "unsupported-mixed-dimension-layout",
              "unsupported mixed Grid is diagnosed during preflight");
    };

    verify_refused("*{margin:0;padding:0;border:0}"
                   "#grid{display:grid;width:100px;grid-template-columns:repeat(2,auto)}",
                   "auto-track Grid refuses before any geometry is assigned");
    verify_refused("*{margin:0;padding:0;border:0}#grid{display:grid;width:100px}",
                   "implicit auto-track Grid refuses before any geometry is assigned");
    verify_refused("*{margin:0;padding:0;border:0}"
                   "#grid{display:grid;width:100px;grid-template-columns:1fr 1fr}",
                   "flexible-track Grid refuses before any geometry is assigned");
    verify_refused("*{margin:0;padding:0;border:0}"
                   "#grid{display:grid;width:100px;grid-template-columns:repeat(2,minmax(50px,1fr))}",
                   "flexible minmax Grid refuses before any geometry is assigned");
    verify_refused("*{margin:0;padding:0;border:0}"
                   "#grid{display:grid;width:100px;grid-template-columns:50px 50px}"
                   "#mixed{grid-column:span 2}",
                   "spanning mixed Grid item refuses before any geometry is assigned");
#endif
}

void flexible_minmax_keeps_ordinary_grid_geometry() {
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
    const auto layout_grid = [](const char* columns) {
        auto dom = HtmlParser{}.parse(
            "<div id='grid'><div id='first'></div><div id='second'></div></div>");
        const std::string css = std::string("*{margin:0;padding:0;border:0}") +
            "#grid{display:grid;width:120px;grid-template-columns:" + columns + "}";
        StyleResolver resolver(CssParser{}.parse(css));
        auto tree = RenderTreeBuilder(resolver).build(*dom);
        const auto layout = LayoutEngine(resolver).layout(*tree, 320, 240);
        const auto* grid = find(*layout, "grid");
        const auto* first = find(*layout, "first");
        const auto* second = find(*layout, "second");
        check(grid && first && second, "explicit Grid fixture boxes exist");
        return std::array<int, 12>{
            grid->rect.x, grid->rect.y, grid->rect.width, grid->rect.height,
            first->rect.x, first->rect.y, first->rect.width, first->rect.height,
            second->rect.x, second->rect.y, second->rect.width, second->rect.height,
        };
    };

    const auto minmax_layout = layout_grid("repeat(2,minmax(50px,1fr))");
    const auto fixed_layout = layout_grid("50px 50px");
    check(minmax_layout == fixed_layout,
          "ordinary flexible minmax Grid preserves its existing minimum-width geometry");
#endif
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
        block_axes_and_ranges_match_equivalent_pixels();
        unsupported_context_refuses_before_geometry();
        qualified_nowrap_flex_resolves_mixed_basis_and_constraints();
        qualified_wrapped_row_flex_resolves_mixed_basis();
        qualified_flex_success_preserves_authored_state();
        qualified_grid_item_resolves_mixed_width_constraints();
        mixed_grid_refuses_unresolved_tracks_and_spans_before_geometry();
        flexible_minmax_keeps_ordinary_grid_geometry();
        temporary_override_restores_typed_storage_and_provenance();
        flex_auto_min_treats_mixed_minimum_as_authored();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "mixed dimension consumer tests passed\n";
    return 0;
}
