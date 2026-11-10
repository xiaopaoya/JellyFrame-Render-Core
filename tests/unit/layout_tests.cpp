#include "render_core/arena.h"
#include "render_core/css_parser.h"
#include "render_core/html_parser.h"
#include "render_core/layout.h"
#include "render_core/render_tree.h"

#include <iostream>
#include <stdexcept>
#include <string>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

const LayoutBox* find_first_by_tag(const LayoutBox& box, const std::string& tag_name) {
    if (box.node != nullptr && box.node->type == NodeType::Element && box.node->tag_name == tag_name) {
        return &box;
    }
    for (const auto& child : box.children) {
        const LayoutBox* found = find_first_by_tag(*child, tag_name);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

const LayoutBox* find_first_by_id(const LayoutBox& box, const std::string& id) {
    if (box.node != nullptr && box.node->type == NodeType::Element && box.node->attribute("id") == id) {
        return &box;
    }
    for (const auto& child : box.children) {
        const LayoutBox* found = find_first_by_id(*child, id);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

bool has_diagnostic_code(const VectorDiagnosticSink& sink, const std::string& code) {
    for (const Diagnostic& diagnostic : sink.diagnostics()) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

void fixed_separate_table_uses_columns_spacing_and_shared_rows() {
    const Style initial_style;
    check(!table_border_is_collapsed(initial_style) && table_border_spacing_x(initial_style) == 2 &&
          table_border_spacing_y(initial_style) == 2,
          "default table style matches separate borders with two-pixel spacing");
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><table id='grid'><caption id='cap'>Caption</caption><colgroup><col id='col1'><col id='col2'><col id='col3'></colgroup>"
        "<tbody><tr><td id='a'>Alpha</td><td id='b'>Short</td><td id='c'>Ready</td></tr>"
        "<tr><td id='d'>Beta</td><td id='wrap'>A longer fixed-width cell wraps onto multiple lines to size the row.</td>"
        "<td id='f'>Pending</td></tr>"
        "<tr><td id='g'>Gamma</td><td id='break'>First line<br>Second line</td><td id='i'>Done</td></tr>"
        "<tr><td id='j'>Delta</td><td id='k'>Stable</td><td id='l'>Ready</td></tr></tbody></table></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "#grid { width: 420px; table-layout: fixed; border-spacing: 6px 4px; border: 2px solid #243447; }"
        "#col1 { width: 112px; } #col2 { width: 180px; } #col3 { width: 112px; }"
        "td { padding: 8px; border: 1px solid #8493a3; vertical-align: top; line-height: 20px; }"
        "caption { padding: 7px 10px; border: 1px solid #69798a; background-color: #dae9ff; }"));
    auto render_tree = RenderTreeBuilder(resolver).build(*document);
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*render_tree, 1000);
    const LayoutBox* table = find_first_by_id(*layout_tree, "grid");
    const LayoutBox* caption = find_first_by_id(*layout_tree, "cap");
    const LayoutBox* a = find_first_by_id(*layout_tree, "a");
    const LayoutBox* b = find_first_by_id(*layout_tree, "b");
    const LayoutBox* c = find_first_by_id(*layout_tree, "c");
    const LayoutBox* d = find_first_by_id(*layout_tree, "d");
    const LayoutBox* wrap = find_first_by_id(*layout_tree, "wrap");
    const LayoutBox* f = find_first_by_id(*layout_tree, "f");
    const LayoutBox* g = find_first_by_id(*layout_tree, "g");
    const LayoutBox* br = find_first_by_id(*layout_tree, "break");
    const LayoutBox* i = find_first_by_id(*layout_tree, "i");
    const LayoutBox* j = find_first_by_id(*layout_tree, "j");
    check(table && caption && a && b && c && d && wrap && f && g && br && i && j,
          "fixed table fixture boxes exist");
    if (table->rect.width != 432)
        throw std::runtime_error("fixed table width actual=" + std::to_string(table->rect.width) +
            " style=" + std::to_string(table->style.width) + " props=" +
            std::to_string(table->style.table_properties) + " rows=" + std::to_string(table->children.size()) +
            " columns=" + std::to_string(find_first_by_id(*layout_tree, "col1")->style.width) + "," +
            std::to_string(find_first_by_id(*layout_tree, "col2")->style.width) + "," +
            std::to_string(find_first_by_id(*layout_tree, "col3")->style.width));
    check(table->rect.height > caption->rect.height + a->rect.height + wrap->rect.height,
          "table outer height includes caption, row spacing and borders");
    check(caption->rect.width == table->rect.width && caption->rect.y == table->rect.y,
          "top caption occupies table width before the grid");
    check(a->rect.x == d->rect.x && b->rect.x == wrap->rect.x && c->rect.x == f->rect.x,
          "column edges remain aligned between rows");
    check(a->rect.height == b->rect.height && b->rect.height == c->rect.height,
          "short row cells share a height");
    check(d->rect.height == wrap->rect.height && wrap->rect.height == f->rect.height &&
          wrap->rect.height > b->rect.height, "wrapped content determines shared row height");
    check(wrap->rect.y > b->rect.y && f->rect.y > c->rect.y, "later row follows the wrapped row height");
    check(g->rect.y > wrap->rect.y && i->rect.y == g->rect.y && br->rect.height > a->rect.height,
          "explicit hard break forms a later row with a shared taller cell height");
    check(j->rect.y > g->rect.y && table->rect.height > j->rect.y - table->rect.y,
          "fourth row contributes to the table's bounded outer height");
    check(table_layout_is_fixed(table->style) && !table_border_is_collapsed(table->style) &&
          table_border_spacing_x(table->style) == 6 && table_border_spacing_y(table->style) == 4,
          "fixed table defaults to separate borders without a collapse declaration");

    const_cast<Node*>(wrap->node)->set_text_content(
        "A much longer replacement value that wraps repeatedly and increases this table row height");
    const_cast<Node*>(wrap->node)->set_attribute("style", "padding-top: 18px; padding-bottom: 18px;");
    auto mutated_render = RenderTreeBuilder(resolver).build(*document);
    auto mutated_layout = layout_engine.layout(*mutated_render, 1000);
    const LayoutBox* mutated_table = find_first_by_id(*mutated_layout, "grid");
    const LayoutBox* mutated_cell = find_first_by_id(*mutated_layout, "wrap");
    const LayoutBox* mutated_peer = find_first_by_id(*mutated_layout, "d");
    const LayoutBox* mutated_next = find_first_by_id(*mutated_layout, "g");
    check(mutated_table && mutated_cell && mutated_peer && mutated_next, "mutated table boxes exist");
    check(mutated_table->rect.height > table->rect.height && mutated_cell->rect.height == mutated_peer->rect.height,
          "text and padding mutation propagates the changed row height to its peer and table");
    check(mutated_next->rect.y > g->rect.y, "expanded row moves every following row down");
}

void fixed_table_positions_caption_subtree_and_resolves_auto_margins() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><table id='bottom'><caption id='caption'><span id='caption-text'>Bottom</span></caption>"
        "<colgroup><col style='width:100px'></colgroup><tbody><tr><td id='cell'>Cell</td></tr></tbody></table>"
        "<table id='center'><colgroup><col style='width:100px'></colgroup><tbody><tr><td>Cell</td></tr></tbody></table>"
        "<table id='left-auto'><colgroup><col style='width:100px'></colgroup><tbody><tr><td>Cell</td></tr></tbody></table>"
        "<table id='right-auto'><colgroup><col style='width:100px'></colgroup><tbody><tr><td>Cell</td></tr></tbody></table></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin:0; } table { width:100px; table-layout:fixed; border-spacing:0; }"
        "#bottom { caption-side:bottom; } #center { margin-left:auto; margin-right:auto; }"
        "#left-auto { margin-left:auto; margin-right:20px; } #right-auto { margin-left:15px; margin-right:auto; }"));
    auto render = RenderTreeBuilder(resolver).build(*document);
    LayoutEngine engine(resolver);
    auto layout = engine.layout(*render, 400);
    const LayoutBox* bottom = find_first_by_id(*layout, "bottom");
    const LayoutBox* caption = find_first_by_id(*layout, "caption");
    const LayoutBox* text = find_first_by_id(*layout, "caption-text");
    const LayoutBox* center = find_first_by_id(*layout, "center");
    const LayoutBox* left_auto = find_first_by_id(*layout, "left-auto");
    const LayoutBox* right_auto = find_first_by_id(*layout, "right-auto");
    check(bottom && caption && text && center && left_auto && right_auto,
          "caption and auto-margin fixed table boxes exist");
    check(caption->rect.y > bottom->rect.y && text->rect.y >= caption->rect.y &&
          text->rect.y + text->rect.height <= caption->rect.y + caption->rect.height,
          "bottom caption repositioning carries its text subtree with it");
    check(center->rect.x == 150, "two auto margins center fixed table in available width");
    check(left_auto->rect.x == 280, "left auto margin consumes remaining space beside fixed right margin");
    check(right_auto->rect.x == 15, "right auto margin preserves fixed left margin");
}

void fixed_table_refuses_spans_nested_tables_and_percentage_widths() {
    const char* cases[] = {
        "<table id='t'><colgroup><col style='width:40px'></colgroup><tbody><tr><td id='cell' colspan='1'>X</td></tr></tbody></table>",
        "<table id='t'><colgroup><col style='width:40px'></colgroup><tbody><tr><td id='cell'><table><tbody><tr><td>N</td></tr></tbody></table></td></tr></tbody></table>",
        "<table id='t'><colgroup><col style='width:40px'></colgroup><tbody><tr><td id='cell'>X</td></tr></tbody></table>",
    };
    const char* css_cases[] = {
        "#t { width:100px; table-layout:fixed; }",
        "#t { width:100px; table-layout:fixed; }",
        "#t { width:100%; table-layout:fixed; }",
    };
    for (std::size_t index = 0; index < 3; ++index) {
        HtmlParser html_parser;
        CssParser css_parser;
        auto document = html_parser.parse(std::string("<body>") + cases[index] + "</body>");
        StyleResolver resolver(css_parser.parse(css_cases[index]));
        auto render = RenderTreeBuilder(resolver).build(*document);
        LayoutEngine engine(resolver);
        auto layout = engine.layout(*render, 200);
        const LayoutBox* table = find_first_by_id(*layout, "t");
        check(table != nullptr, "unsupported table still has a bounded fallback box");
        if (index < 2) {
            const LayoutBox* cell = find_first_by_id(*layout, "cell");
            check(cell != nullptr && cell->rect.width != 40,
                  "spans and nested tables do not use fixed-track cell sizing");
        } else {
            check(table->rect.width == 200, "percentage table width uses the ordinary non-fixed fallback");
        }
    }
}

void check_matching_table_fallback(const LayoutBox& actual, const LayoutBox& expected) {
    if (actual.rect.x != expected.rect.x || actual.rect.y != expected.rect.y ||
        actual.rect.width != expected.rect.width || actual.rect.height != expected.rect.height) {
        throw std::runtime_error("refused table fallback geometry differs: actual=" +
            std::to_string(actual.rect.x) + "," + std::to_string(actual.rect.y) + "," +
            std::to_string(actual.rect.width) + "," + std::to_string(actual.rect.height) +
            " expected=" + std::to_string(expected.rect.x) + "," + std::to_string(expected.rect.y) + "," +
            std::to_string(expected.rect.width) + "," + std::to_string(expected.rect.height));
    }
    check(actual.style.width == expected.style.width &&
          actual.style.width_percent == expected.style.width_percent &&
          actual.style.computed_sizes.equals(expected.style.computed_sizes) &&
          actual.style.box_sizing_border_box == expected.style.box_sizing_border_box &&
          actual.style.margin.top == expected.style.margin.top &&
          actual.style.margin.right == expected.style.margin.right &&
          actual.style.margin.bottom == expected.style.margin.bottom &&
          actual.style.margin.left == expected.style.margin.left,
          "refused table restores temporary cell sizing and margins");
    const auto& a = actual.text_layout_cache;
    const auto& b = expected.text_layout_cache;
    check(a.valid == b.valid && a.source_text == b.source_text && a.rendered_text == b.rendered_text &&
          a.lines == b.lines && a.available_width == b.available_width && a.content_width == b.content_width &&
          a.rect_x == b.rect_x && a.rect_y == b.rect_y && a.rect_width == b.rect_width &&
          a.rect_height == b.rect_height && a.prepared_text == b.prepared_text &&
          a.prepared_packet == b.prepared_packet && a.prepared_lines.size() == b.prepared_lines.size(),
          "refused table leaves fallback text wrapping and cache geometry consistent");
    check(actual.children.size() == expected.children.size(), "fallback keeps the same subtree shape");
    for (std::size_t i = 0; i < actual.children.size(); ++i)
        check_matching_table_fallback(*actual.children[i], *expected.children[i]);
}

void fixed_table_refuses_extreme_and_unqualified_inputs_transactionally() {
    struct Case { const char* html; const char* css; };
    const Case cases[] = {
        {"<table><colgroup><col><col></colgroup><tbody><tr><td>A</td><td>B</td></tr></tbody></table>",
         "col{width:2147483000px}table{border-spacing:2px}"},
        // The first measured row and caption must roll back when row two exceeds int geometry.
        {"<table><caption><span>Caption wraps over a narrow track</span></caption>"
         "<colgroup><col></colgroup><tbody><tr><td><span>First measured row</span></td></tr>"
         "<tr><td><span>Second measured row</span></td></tr></tbody></table><div>After</div>",
         "col{width:40px}td{height:1500000000px;margin:3px;box-sizing:content-box}"},
        {"<table><colgroup><col></colgroup><tbody><tr><td><div style='display:table'>Nested</div>"
         "</td></tr></tbody></table>", "col{width:40px}"},
        {"<table><caption><span>Caption</span></caption><colgroup><col></colgroup>"
         "<tbody><tr><td>Cell</td></tr></tbody></table>", "col{width:40px}caption{margin-left:20px}"},
    };
    for (const auto& fixture : cases) {
        auto document = HtmlParser{}.parse(std::string("<body>") + fixture.html + "</body>");
        const std::string css = std::string("body{margin:0}table{width:100px;border-spacing:0}") + fixture.css;
        StyleResolver fixed(CssParser{}.parse(css + "table{table-layout:fixed}"));
        StyleResolver fallback(CssParser{}.parse(css));
        auto fixed_tree = LayoutEngine(fixed).layout(*RenderTreeBuilder(fixed).build(*document), 400);
        auto fallback_tree = LayoutEngine(fallback).layout(*RenderTreeBuilder(fallback).build(*document), 400);
        check_matching_table_fallback(*fixed_tree, *fallback_tree);
    }
}

void table_border_spacing_rejects_extra_components() {
    auto document = HtmlParser{}.parse("<body><table id='t'></table></body>");
    StyleResolver resolver(CssParser{}.parse(
        "table{border-spacing:7px 9px;border-spacing:3px 4px 99px}"));
    auto tree = LayoutEngine(resolver).layout(*document, 400);
    const auto* table = find_first_by_id(*tree, "t");
    check(table && table_border_spacing_x(table->style) == 7 && table_border_spacing_y(table->style) == 9,
          "invalid three-component border-spacing leaves the prior valid declaration in force");
}

void fixed_table_refuses_retained_sizing_and_constraints() {
    const char* declarations[] = {
        "table{width:100.4px}", "col{width:40.4px}", "td{height:20.6px}",
        "table{width:calc(100.4px);height:20px}",
        "td span{width:calc(10.4px);height:20px}",
        "table{width:50.5%}", "col{width:10.5%}", "td{width:10.5%}",
        "table{width:calc(100.4px + 0%)}", "col{width:calc(40px + 1%)}",
        "td{width:calc(10px + 1%)}", "td span{width:calc(10px + 1%)}",
        "table{min-width:120px}", "table{max-width:90px}", "table{height:50px}",
        "table{min-height:30px}", "table{max-height:30px}",
        "col{min-width:50px}", "col{max-width:30px}", "col{min-height:20px}",
        "td{min-width:70px}", "td{max-width:30px}", "td{min-height:20px}",
        "td{max-height:20px}", "td{min-width:calc(1px + 1%)}",
        "caption{width:50px}", "caption{min-width:50px}",
        "tr{height:100px}", "tbody{height:100px}", "tr{min-height:50px}",
        "tbody{max-height:50px}", "tr{width:30px}", "tbody{min-width:50px}",
        "colgroup{width:50px}", "col{height:50px}", "col{min-height:50px}",
        "tr{position:relative;top:10px}", "tbody{position:absolute;top:10px}",
        "colgroup{position:relative;left:10px}", "col{position:relative;left:10px}",
        "caption{position:relative;left:10px}", "table{padding:10px}",
        "table{position:absolute;left:10px}", "tr{padding:10px}", "tbody{margin:10px}",
        "col{border:1px solid black}",
    };
    for (const char* declaration : declarations) {
        auto document = HtmlParser{}.parse(
            "<body><table><caption><span>Caption</span></caption><colgroup><col></colgroup>"
            "<tbody><tr><td><span>Content wraps over a narrow track</span></td></tr>"
            "</tbody></table><div>Following</div></body>");
        const std::string css = std::string("body{margin:0}table{width:100px;border-spacing:0}") +
            "col{width:40px}" + declaration;
        StyleResolver fixed(CssParser{}.parse(css + "table{table-layout:fixed}"));
        StyleResolver fallback(CssParser{}.parse(css));
        auto actual = LayoutEngine(fixed).layout(*RenderTreeBuilder(fixed).build(*document), 400);
        auto expected = LayoutEngine(fallback).layout(*RenderTreeBuilder(fallback).build(*document), 400);
        try {
            check_matching_table_fallback(*actual, *expected);
        } catch (const std::exception& error) {
            throw std::runtime_error(std::string(declaration) + ": " + error.what());
        }
    }
}

void table_inherits_border_state_and_auto_resets_fixed() {
    const std::string html =
        "<body><div><table id='t'><colgroup><col style='width:40px'></colgroup>"
        "<tbody><tr><td id='cell'>Cell</td></tr></tbody></table></div></body>";
    for (const char* declaration : {"body{border-collapse:collapse}",
                                   "table{table-layout:auto}"}) {
        auto document = HtmlParser{}.parse(html);
        const std::string css = "body{margin:0}table{width:100px}";
        StyleResolver fixed(CssParser{}.parse(css + "table{table-layout:fixed}" + declaration));
        StyleResolver fallback(CssParser{}.parse(css + declaration));
        auto actual = LayoutEngine(fixed).layout(*RenderTreeBuilder(fixed).build(*document), 400);
        auto expected = LayoutEngine(fallback).layout(*RenderTreeBuilder(fallback).build(*document), 400);
        check_matching_table_fallback(*actual, *expected);
        const auto* table = find_first_by_id(*actual, "t");
        check(table && (table_border_is_collapsed(table->style) || !table_layout_is_fixed(table->style)),
              "inherited collapse or explicit auto disables the fixed separate path");
    }
    for (int local_override = 0; local_override < 2; ++local_override) {
        auto document = HtmlParser{}.parse(html);
        const std::string css = std::string(
            "body{margin:0;border-collapse:collapse;border-spacing:7px 9px}"
            "table{width:100px;table-layout:fixed;border-collapse:separate}") +
            (local_override ? "table{border-spacing:2px}" : "");
        StyleResolver resolver(CssParser{}.parse(css));
        auto tree = LayoutEngine(resolver).layout(*RenderTreeBuilder(resolver).build(*document), 400);
        const auto* table = find_first_by_id(*tree, "t");
        const auto* cell = find_first_by_id(*tree, "cell");
        const int sx = local_override ? 2 : 7;
        const int sy = local_override ? 2 : 9;
        check(table && cell && !table_border_is_collapsed(table->style) &&
              table_border_spacing_x(table->style) == sx && table_border_spacing_y(table->style) == sy &&
              cell->rect.x == table->rect.x + sx && cell->rect.y == table->rect.y + sy &&
              cell->rect.width == 100 - 2 * sx,
              "border state inherits through ancestors and explicit local values determine fixed grid edges");
        check(cell->style.width == -1 && !cell->style.box_sizing_border_box &&
              cell->style.computed_sizes.equals(Style{}.computed_sizes),
              "successful fixed layout restores temporary cell sizing and retained-size storage");
    }
}

void table_spacing_keeps_prior_value_for_unqualified_fractions() {
    for (const char* invalid : {"-0.4px", "0.4px", "0.6px", "7px -0.4px", "256px",
                                "calc(-0.4px)", "calc(0.4px)"}) {
        auto document = HtmlParser{}.parse("<body><table id='t'></table></body>");
        StyleResolver resolver(CssParser{}.parse(std::string(
            "table{border-spacing:7px 9px;border-spacing:") + invalid + "}"));
        auto tree = LayoutEngine(resolver).layout(*document, 400);
        const auto* table = find_first_by_id(*tree, "t");
        check(table && table_border_spacing_x(table->style) == 7 && table_border_spacing_y(table->style) == 9,
              "negative, fractional and out-of-range spacing preserve the prior integral declaration");
    }
}

void fixed_table_caption_side_inherits_and_honors_explicit_top() {
    const char* declarations[] = {
        "caption{caption-side:bottom}",
        "body{caption-side:bottom}",
        "table{caption-side:bottom}",
        "body{caption-side:bottom}caption{caption-side:top}",
    };
    for (std::size_t i = 0; i < 4; ++i) {
        auto document = HtmlParser{}.parse(
            "<body><table id='t'><caption id='cap'><span id='label'>Caption</span></caption>"
            "<colgroup><col style='width:100px'></colgroup><tbody><tr><td id='cell'>Cell</td>"
            "</tr></tbody></table></body>");
        StyleResolver resolver(CssParser{}.parse(std::string(
            "body{margin:0}table{width:100px;table-layout:fixed;border-spacing:0}") + declarations[i]));
        auto tree = LayoutEngine(resolver).layout(*RenderTreeBuilder(resolver).build(*document), 400);
        const auto* cap = find_first_by_id(*tree, "cap");
        const auto* label = find_first_by_id(*tree, "label");
        const auto* cell = find_first_by_id(*tree, "cell");
        const auto* table = find_first_by_id(*tree, "t");
        check(cap && label && cell && table, "caption-side provenance fixture exists");
        if (i < 3) {
            check(table_caption_side(cap->style) == TableCaptionSide::Bottom &&
                  cap->rect.y == cell->rect.y + cell->rect.height,
                  "local, table and ancestor bottom caption-side all place the caption after the grid");
        } else {
            check(table_caption_side(cap->style) == TableCaptionSide::Top && cap->rect.y == table->rect.y &&
                  cell->rect.y == cap->rect.y + cap->rect.height,
                  "explicit caption top overrides inherited bottom");
        }
        check(label->rect.y == cap->rect.y && !label->children.empty() &&
              label->children.front()->rect.y == cap->rect.y,
              "caption-side placement shifts the complete span and text subtree");
    }
}

void check_table_subtree_shift(const LayoutBox& shifted, const LayoutBox& baseline) {
    check(shifted.rect.x == baseline.rect.x + 13 && shifted.rect.y == baseline.rect.y + 17 &&
          shifted.rect.width == baseline.rect.width && shifted.rect.height == baseline.rect.height,
          "relative table offsets its whole subtree exactly once");
    check(shifted.children.size() == baseline.children.size(), "relative table preserves subtree shape");
    for (std::size_t i = 0; i < shifted.children.size(); ++i)
        check_table_subtree_shift(*shifted.children[i], *baseline.children[i]);
}

void fixed_table_relative_position_keeps_following_flow() {
    auto document = HtmlParser{}.parse(
        "<body><table id='t'><caption>Caption</caption><colgroup><col style='width:100px'></colgroup>"
        "<tbody><tr><td>Cell</td></tr></tbody></table><div id='next'>Next</div></body>");
    const std::string css = "body{margin:0}table{width:100px;table-layout:fixed;border-spacing:0}";
    StyleResolver base(CssParser{}.parse(css));
    StyleResolver relative(CssParser{}.parse(css + "table{position:relative;left:13px;top:17px}"));
    auto baseline = LayoutEngine(base).layout(*RenderTreeBuilder(base).build(*document), 400);
    auto shifted = LayoutEngine(relative).layout(*RenderTreeBuilder(relative).build(*document), 400);
    const auto* a = find_first_by_id(*baseline, "t");
    const auto* b = find_first_by_id(*shifted, "t");
    check(a && b, "relative table fixture exists");
    // Columns have no generated visual box; all laid-out grid/caption descendants shift.
    check(b->rect.x == a->rect.x + 13 && b->rect.y == a->rect.y + 17,
          "relative table offsets its outer border box");
    check_table_subtree_shift(*find_first_by_tag(*b, "caption"), *find_first_by_tag(*a, "caption"));
    check_table_subtree_shift(*find_first_by_tag(*b, "tbody"), *find_first_by_tag(*a, "tbody"));
    check(find_first_by_id(*baseline, "next")->rect.y == find_first_by_id(*shifted, "next")->rect.y,
          "relative table displacement leaves following sibling flow unchanged");
}

void fixed_table_distributes_surplus_proportionally_and_preserves_edges() {
    struct Case { int table; int first; int second; int spacing; int border; int expected_first; int expected_second; };
    const Case cases[] = {
        {300,40,40,0,0,150,150}, {301,40,40,0,0,151,150},
        {300,40,80,0,0,100,200}, {301,40,80,0,0,100,201}, {302,40,80,0,0,101,201},
        {313,40,80,3,2,100,200},
    };
    for (const auto& fixture : cases) {
        auto document = HtmlParser{}.parse(
            "<body><table id='t'><colgroup><col id='one'><col id='two'></colgroup><tbody><tr>"
            "<td id='a'>Long content that wraps only at the assigned track edge</td><td id='b'>B</td>"
            "</tr></tbody></table></body>");
        const std::string css = "body{margin:0}td{padding:0}table{table-layout:fixed;width:" +
            std::to_string(fixture.table) + "px;border-spacing:" + std::to_string(fixture.spacing) +
            "px;border:" + std::to_string(fixture.border) + "px solid black}#one{width:" +
            std::to_string(fixture.first) + "px}#two{width:" + std::to_string(fixture.second) + "px}";
        StyleResolver resolver(CssParser{}.parse(css));
        auto tree = LayoutEngine(resolver).layout(*RenderTreeBuilder(resolver).build(*document), 500);
        const auto* a = find_first_by_id(*tree, "a");
        const auto* b = find_first_by_id(*tree, "b");
        const auto* table = find_first_by_id(*tree, "t");
        check(a && b && table && a->rect.width == fixture.expected_first && b->rect.width == fixture.expected_second,
              "surplus tracks preserve explicit-column proportions with deterministic integer edges");
        check(table->rect.width == fixture.table && a->rect.x == table->rect.x + fixture.border + fixture.spacing &&
              b->rect.x == a->rect.x + a->rect.width + fixture.spacing &&
              b->rect.x + b->rect.width + fixture.spacing + fixture.border == table->rect.x + table->rect.width,
              "proportional growth preserves separate spacing and border edges without unused width");
        check(!a->children.empty() && a->children.front()->text_layout_cache.available_width == a->rect.width,
              "cell text wrapping uses the distributed track width");
    }
}

struct TextMeasureCounter {
    int calls = 0;
};

bool count_text_measure(const std::string& text,
                        int,
                        int,
                        TextMetrics* metrics,
                        void* context) {
    auto* counter = static_cast<TextMeasureCounter*>(context);
    ++counter->calls;
    metrics->width = static_cast<int>(text.size()) * 8;
    metrics->line_height = 20;
    return true;
}

void layout_tree_can_use_monotonic_arena() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main><p>Hello</p><p>World</p></main></body>");
    StyleResolver resolver(css_parser.parse("main { padding: 4px; } p { margin: 0; font-size: 16px; }"));
    RenderTreeBuilder render_tree_builder(resolver);
    MonotonicArena render_arena(256);
    auto render_tree = render_tree_builder.build(*document, render_arena);

    LayoutEngine layout_engine(resolver);
    MonotonicArena layout_arena(256);
    auto layout_tree = layout_engine.layout(*render_tree, 240, layout_arena);

    check(layout_tree != nullptr, "arena layout root exists");
    check(layout_arena.used_bytes() > 0, "arena layout consumes arena storage");
    check(find_first_by_tag(*layout_tree, "main") != nullptr, "arena layout contains main");
    check(find_first_by_tag(*layout_tree, "p") != nullptr, "arena layout contains paragraph");
    check(layout_tree->rect.height > 0, "arena layout computes geometry");
}

void layout_tree_respects_box_budget() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main><p>A</p><p>B</p><p>C</p></main></body>");
    StyleResolver resolver(css_parser.parse(""));
    RenderTreeBuilder render_tree_builder(resolver);
    auto render_tree = render_tree_builder.build(*document);

    LayoutEngine layout_engine(resolver, {}, LayoutEngineOptions{4});
    auto layout_tree = layout_engine.layout(*render_tree, 240);

    check(count_layout_boxes(*layout_tree) == 4, "layout tree is capped by box budget");
}

void layout_tree_reports_box_budget_diagnostic() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main><p>A</p><p>B</p><p>C</p></main></body>");
    StyleResolver resolver(css_parser.parse(""));
    RenderTreeBuilder render_tree_builder(resolver);
    auto render_tree = render_tree_builder.build(*document);
    VectorDiagnosticSink diagnostics;
    LayoutEngineOptions options;
    options.max_layout_boxes = 4;
    options.diagnostics = &diagnostics;
    LayoutEngine layout_engine(resolver, {}, options);
    auto layout_tree = layout_engine.layout(*render_tree, 240);

    check(count_layout_boxes(*layout_tree) == 4, "layout budget still caps boxes");
    check(has_diagnostic_code(diagnostics, "layout-box-limit"), "layout budget diagnostic is reported");
}

void layout_tree_reports_depth_budget_diagnostic() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main><section><p>Deep</p></section></main></body>");
    StyleResolver resolver(css_parser.parse("body, main, section, p { margin: 0; }"));
    RenderTreeBuilder render_tree_builder(resolver);
    auto render_tree = render_tree_builder.build(*document);
    VectorDiagnosticSink diagnostics;
    LayoutEngineOptions options;
    options.max_layout_depth = 2;
    options.diagnostics = &diagnostics;
    LayoutEngine layout_engine(resolver, {}, options);
    auto layout_tree = layout_engine.layout(*render_tree, 240);

    check(layout_tree != nullptr, "depth budget layout root exists");
    check(has_diagnostic_code(diagnostics, "layout-depth-limit"), "layout depth diagnostic is reported");
    const LayoutBox* main = find_first_by_tag(*layout_tree, "main");
    check(main == nullptr || main->rect.height == 0, "layout beyond depth budget is skipped");
}

void nested_flex_sizing_does_not_repeat_subtree_layout() {
    std::string html = "<body>";
    for (int depth = 0; depth < 12; ++depth) {
        html += "<div>";
    }
    html += "abc";
    for (int depth = 0; depth < 12; ++depth) {
        html += "</div>";
    }
    html += "</body>";

    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(html);
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; } div { display: flex; flex-direction: column; width: 100px; }"));
    RenderTreeBuilder render_tree_builder(resolver);
    auto render_tree = render_tree_builder.build(*document);
    TextMeasureCounter counter;
    LayoutEngine layout_engine(resolver, TextMeasureProvider{count_text_measure, &counter});
    auto layout_tree = layout_engine.layout(*render_tree, 100, 240);

    check(layout_tree != nullptr, "nested flex fixture produces a layout tree");
    check(counter.calls <= 4, "nested flex layout does not exponentially repeat text measurement");
}

void flex_row_distributes_grow_space() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><main><div id='a'></div><div id='b'></div><div id='fixed'></div></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { display: flex; width: 300px; gap: 10px; }"
        "div { min-width: 0; height: 10px; }"
        "#a { flex: 1; }"
        "#b { flex: 2; }"
        "#fixed { width: 60px; flex-shrink: 0; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 320);

    const LayoutBox* a = find_first_by_id(*layout_tree, "a");
    const LayoutBox* b = find_first_by_id(*layout_tree, "b");
    const LayoutBox* fixed = find_first_by_id(*layout_tree, "fixed");
    check(a != nullptr && b != nullptr && fixed != nullptr, "flex fixture boxes exist");
    check(fixed->rect.width == 60, "fixed flex item keeps width");
    check(a->rect.width >= 70 && a->rect.width <= 75, "flex:1 gets one grow share");
    check(b->rect.width >= 145 && b->rect.width <= 150, "flex:2 gets two grow shares");
    check(b->rect.x >= a->rect.x + a->rect.width + 10, "flex gap preserved");
}

void flex_row_shrinks_basis_widths() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main><div id='a'></div><div id='b'></div></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { display: flex; width: 180px; }"
        "div { min-width: 0; height: 10px; flex: 0 1 120px; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 240);

    const LayoutBox* a = find_first_by_id(*layout_tree, "a");
    const LayoutBox* b = find_first_by_id(*layout_tree, "b");
    check(a != nullptr && b != nullptr, "shrink flex fixture boxes exist");
    check(a->rect.width == 90 && b->rect.width == 90, "flex basis widths shrink evenly");
}

void flex_row_justifies_and_aligns_items() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main><div id='a'></div><div id='b'></div></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { display: flex; width: 200px; height: 50px; justify-content: center; align-items: center; }"
        "div { width: 40px; height: 10px; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 240);

    const LayoutBox* a = find_first_by_id(*layout_tree, "a");
    const LayoutBox* b = find_first_by_id(*layout_tree, "b");
    check(a != nullptr && b != nullptr, "justify/align flex fixture boxes exist");
    check(a->rect.x == 60 && b->rect.x == 100, "center justification places row as a group");
    check(a->rect.y == 20 && b->rect.y == 20, "center alignment places items vertically");
}

void flex_cross_axis_stretch_resolves_auto_size() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><main id='row'><div id='row-auto'></div><div id='row-fixed'></div></main>"
        "<main id='column'><div id='column-auto'></div><div id='column-fixed'></div></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { display: flex; align-items: stretch; }"
        "#row { width: 100px; height: 40px; }"
        "#row-auto { width: 20px; } #row-fixed { width: 20px; height: 10px; }"
        "#column { flex-direction: column; width: 60px; height: 40px; }"
        "#column-auto { height: 10px; } #column-fixed { width: 20px; height: 10px; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 120, 100);

    const LayoutBox* row_auto = find_first_by_id(*layout_tree, "row-auto");
    const LayoutBox* row_fixed = find_first_by_id(*layout_tree, "row-fixed");
    const LayoutBox* column_auto = find_first_by_id(*layout_tree, "column-auto");
    const LayoutBox* column_fixed = find_first_by_id(*layout_tree, "column-fixed");
    check(row_auto != nullptr && row_fixed != nullptr && column_auto != nullptr && column_fixed != nullptr,
          "cross-axis stretch fixture boxes exist");
    check(row_auto->rect.height == 40, "row stretch fills the line for an auto-height item");
    check(row_fixed->rect.height == 10, "row stretch preserves an explicit height");
    check(column_auto->rect.width == 60, "column stretch fills the line for an auto-width item");
    check(column_fixed->rect.width == 20, "column stretch preserves an explicit width");
}

void flex_cross_axis_alignment_uses_outer_size_and_align_self() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><main><div id='center'></div><div id='end'></div><div id='self'></div></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { display: flex; width: 120px; height: 50px; align-items: center; }"
        "div { width: 20px; height: 10px; margin-top: 4px; margin-bottom: 6px; }"
        "#end { align-self: flex-end; } #self { align-self: stretch; height: auto; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 140, 70);

    const LayoutBox* center = find_first_by_id(*layout_tree, "center");
    const LayoutBox* end = find_first_by_id(*layout_tree, "end");
    const LayoutBox* self = find_first_by_id(*layout_tree, "self");
    check(center != nullptr && end != nullptr && self != nullptr,
          "cross-axis alignment fixture boxes exist");
    check(center->rect.y == 19, "center aligns the item's outer margin box");
    check(end->rect.y == 34, "end aligns the item's outer margin box");
    check(self->rect.y == 4 && self->rect.height == 40,
          "align-self stretch fills the line while preserving margins");
}

void flex_wrap_aligns_items_within_each_line() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><main><div id='short'></div><div id='tall'></div><div id='next'></div></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { display: flex; flex-wrap: wrap; width: 90px; align-items: center; column-gap: 5px; row-gap: 7px; }"
        "div { width: 40px; }"
        "#short { height: 10px; } #tall { height: 20px; } #next { width: 90px; height: 8px; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 110, 80);

    const LayoutBox* short_item = find_first_by_id(*layout_tree, "short");
    const LayoutBox* tall = find_first_by_id(*layout_tree, "tall");
    const LayoutBox* next = find_first_by_id(*layout_tree, "next");
    check(short_item != nullptr && tall != nullptr && next != nullptr,
          "wrapped cross-axis alignment fixture boxes exist");
    check(short_item->rect.y == 5 && tall->rect.y == 0,
          "wrapped items align within the first line's cross size");
    check(next->rect.y == 27, "wrapped next line starts after line height and row gap");
}

void flex_row_supports_end_and_space_evenly_justification() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><main id='end'><div id='end-a'></div><div id='end-b'></div></main>"
        "<main id='even'><div id='even-a'></div><div id='even-b'></div></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { display: flex; width: 200px; height: 20px; }"
        "#end { justify-content: flex-end; }"
        "#even { justify-content: space-evenly; }"
        "div { width: 40px; height: 10px; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 240);

    const LayoutBox* end_a = find_first_by_id(*layout_tree, "end-a");
    const LayoutBox* end_b = find_first_by_id(*layout_tree, "end-b");
    const LayoutBox* even_a = find_first_by_id(*layout_tree, "even-a");
    const LayoutBox* even_b = find_first_by_id(*layout_tree, "even-b");
    check(end_a != nullptr && end_b != nullptr && even_a != nullptr && even_b != nullptr,
          "extended justify-content fixture boxes exist");
    check(end_a->rect.x == 120 && end_b->rect.x == 160,
          "flex-end places the row against the content end");
    check(even_a->rect.x == 40 && even_b->rect.x == 120,
          "space-evenly distributes equal leading, internal and trailing space");
}

void flex_wrap_stacks_lines_with_row_gap() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main><div id='a'></div><div id='b'></div></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { display: flex; flex-wrap: wrap; width: 90px; column-gap: 5px; row-gap: 7px; }"
        "div { width: 50px; height: 10px; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 120);

    const LayoutBox* a = find_first_by_id(*layout_tree, "a");
    const LayoutBox* b = find_first_by_id(*layout_tree, "b");
    check(a != nullptr && b != nullptr, "wrapped flex fixture boxes exist");
    check(a->rect.x == 0 && a->rect.y == 0, "first wrapped flex item starts first line");
    check(b->rect.x == 0 && b->rect.y == 17, "second wrapped flex item moves to next row with row-gap");
}

void flex_wrap_align_content_distributes_lines() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main><div id='a'></div><div id='b'></div></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { display: flex; flex-wrap: wrap; align-content: space-between; width: 90px; height: 40px; row-gap: 7px; }"
        "div { width: 90px; height: 10px; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 100);

    const LayoutBox* a = find_first_by_id(*layout_tree, "a");
    const LayoutBox* b = find_first_by_id(*layout_tree, "b");
    check(a != nullptr && b != nullptr, "align-content fixture boxes exist");
    check(a->rect.y == 0 && b->rect.y == 30,
          "align-content space-between distributes wrapped rows inside fixed height");
}

void flex_column_distributes_vertical_space_and_aligns_items() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><main><div id='a'></div><div id='b'></div><div id='c'></div></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { display: flex; flex-direction: column; width: 200px; height: 100px; "
        "justify-content: space-between; align-items: center; }"
        "#c { align-self: flex-end; }"
        "div { width: 40px; height: 10px; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 240);

    const LayoutBox* a = find_first_by_id(*layout_tree, "a");
    const LayoutBox* b = find_first_by_id(*layout_tree, "b");
    const LayoutBox* c = find_first_by_id(*layout_tree, "c");
    check(a != nullptr && b != nullptr && c != nullptr, "column flex fixture boxes exist");
    check(a->rect.x == 80 && b->rect.x == 80,
          "center alignment places column items on the cross axis");
    check(c->rect.x == 160, "align-self overrides the parent cross-axis alignment");
    check(a->rect.y == 0 && b->rect.y == 45 && c->rect.y == 90,
          "space-between distributes column items on the main axis");
}

void flex_column_distributes_grow_space() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main><div id='a'></div><div id='b'></div></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { display: flex; flex-direction: column; width: 100px; height: 100px; }"
        "div { flex: 1; min-height: 0; }"
        "#b { flex-grow: 3; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 120);

    const LayoutBox* a = find_first_by_id(*layout_tree, "a");
    const LayoutBox* b = find_first_by_id(*layout_tree, "b");
    check(a != nullptr && b != nullptr, "growing column flex fixture boxes exist");
    check(a->rect.height == 25 && b->rect.height == 75, "column grow factors divide available height");
}

void flex_order_reorders_in_flow_layout_without_touching_default_path() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><main><div id='a'></div><div id='badge'></div><div id='b'></div><div id='c'></div></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { display: flex; position: relative; width: 90px; height: 20px; }"
        "div { width: 20px; height: 10px; }"
        "#a { order: 1; } #b { order: 1; }"
        "#badge { position: absolute; order: -10; left: 0; top: 0; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 100);

    const LayoutBox* a = find_first_by_id(*layout_tree, "a");
    const LayoutBox* badge = find_first_by_id(*layout_tree, "badge");
    const LayoutBox* b = find_first_by_id(*layout_tree, "b");
    const LayoutBox* c = find_first_by_id(*layout_tree, "c");
    check(a != nullptr && badge != nullptr && b != nullptr && c != nullptr, "flex order fixture boxes exist");
    check(c->rect.x == 0 && a->rect.x == 20 && b->rect.x == 40,
          "nonzero order uses stable ascending flex item placement");
    check(badge->rect.x == 0 && badge->rect.y == 0,
          "absolute flex child is excluded from ordered in-flow placement");
}

void flex_column_resolves_percent_height_against_containing_box() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main><div id='fill'></div><div id='footer'></div></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; height: 120px; }"
        "main { display: flex; flex-direction: column; width: 100px; height: 100%; }"
        "#fill { flex: 1; }"
        "#footer { height: 10px; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 120, 120);

    const LayoutBox* fill = find_first_by_id(*layout_tree, "fill");
    const LayoutBox* footer = find_first_by_id(*layout_tree, "footer");
    check(fill != nullptr && footer != nullptr, "percent-height column flex fixture boxes exist");
    check(fill->rect.height == 110 && footer->rect.y == 110,
          "column flex resolves percentage container height before grow distribution");
}

void positioned_layout_offsets_without_flow_space() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><main><div id='normal'></div><div id='badge'></div><div id='after'></div></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { position: relative; width: 200px; padding: 10px; }"
        "div { height: 20px; }"
        "#normal { width: 40px; }"
        "#badge { position: absolute; top: 5px; right: 6px; width: 30px; height: 10px; }"
        "#after { width: 50px; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 240);

    const LayoutBox* main = find_first_by_tag(*layout_tree, "main");
    const LayoutBox* normal = find_first_by_id(*layout_tree, "normal");
    const LayoutBox* badge = find_first_by_id(*layout_tree, "badge");
    const LayoutBox* after = find_first_by_id(*layout_tree, "after");
    check(main != nullptr && normal != nullptr && badge != nullptr && after != nullptr,
          "positioned fixture boxes exist");
    check(after->rect.y == normal->rect.y + normal->rect.height,
          "absolute child does not consume block flow height");
    check(badge->rect.x == main->rect.x + main->style.border_width.left + main->style.padding.left +
              200 - 6 - badge->rect.width,
          "absolute right offset uses parent content box");
    check(badge->rect.y == main->rect.y + main->style.border_width.top + main->style.padding.top + 5,
          "absolute top offset uses parent content box");
}

void logical_inset_layout_uses_ltr_physical_offsets() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main><div id='badge'></div></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { position: relative; width: 100px; height: 50px; }"
        "#badge { position: absolute; inset: 6px 8px auto 4px; height: 10px; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 120, 80);

    const LayoutBox* badge = find_first_by_id(*layout_tree, "badge");
    check(badge != nullptr, "logical inset fixture exists");
    check(badge->rect.x == 4 && badge->rect.y == 6 && badge->rect.width == 88,
          "logical inset expands to the same LTR absolute-position geometry as physical offsets");
}

void relative_layout_offsets_visual_box_only() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main><div id='a'></div><div id='b'></div></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { width: 120px; }"
        "div { height: 10px; width: 20px; }"
        "#a { position: relative; left: 7px; top: 3px; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 160);

    const LayoutBox* a = find_first_by_id(*layout_tree, "a");
    const LayoutBox* b = find_first_by_id(*layout_tree, "b");
    check(a != nullptr && b != nullptr, "relative fixture boxes exist");
    check(a->rect.x == 7 && a->rect.y == 3, "relative box is visually offset");
    check(b->rect.y == 10, "relative offset does not change following flow position");
}

void border_box_sizing_keeps_declared_width_and_height() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main id='panel'><span>Text</span></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "#panel { box-sizing: border-box; width: 144px; height: 144px; padding: 8px; border: 2px solid #000; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 200);

    const LayoutBox* panel = find_first_by_id(*layout_tree, "panel");
    check(panel != nullptr, "border-box fixture exists");
    check(panel->rect.width == 144, "border-box width keeps declared border box");
    check(panel->rect.height == 144, "border-box height keeps declared border box");
}

void percentage_width_and_height_use_containing_box() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><main id='screen'><section id='card'></section><aside id='cap'></aside></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; width: 100%; height: 100%; }"
        "#screen { width: 100%; height: 100%; }"
        "#card { width: 50%; height: 25%; min-width: 80px; }"
        "#cap { width: 100%; height: 100%; max-height: 120px; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 320, 240);

    const LayoutBox* body = find_first_by_tag(*layout_tree, "body");
    const LayoutBox* screen = find_first_by_id(*layout_tree, "screen");
    const LayoutBox* card = find_first_by_id(*layout_tree, "card");
    const LayoutBox* cap = find_first_by_id(*layout_tree, "cap");
    check(body != nullptr && screen != nullptr && card != nullptr && cap != nullptr,
          "percentage fixture boxes exist");
    check(body->rect.width == 320 && body->rect.height == 240, "body percentages resolve to viewport");
    check(screen->rect.width == 320 && screen->rect.height == 240, "child percentages resolve to parent");
    check(card->rect.width == 160 && card->rect.height == 60, "nested percentages resolve cheaply");
    check(cap->rect.width == 320 && cap->rect.height == 120, "max-height clamps percentage height");
}

void viewport_units_use_actual_layout_viewport() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><main id='viewport'></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; width: 100%; height: 100%; }"
        "#viewport { width: 50vw; height: 50vh; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 172, 320);

    const LayoutBox* viewport = find_first_by_id(*layout_tree, "viewport");
    check(viewport != nullptr, "viewport unit fixture exists");
    check(viewport->rect.width == 86, "vw resolves against the actual viewport width");
    check(viewport->rect.height == 160, "vh resolves against the actual viewport height");
}

void responsive_layout_matrix_keeps_same_app_inside_three_targets() {
    struct Target {
        int width;
        int height;
        bool narrow;
    };
    constexpr Target targets[] = {
        {300, 300, false},
        {320, 240, false},
        {172, 320, true},
    };

    for (const Target target : targets) {
        HtmlParser html_parser;
        CssParser css_parser;
        auto document = html_parser.parse(
            "<body><main id='screen'><section id='primary'><p id='primary-label'>Primary content</p></section>"
            "<section id='secondary'><p id='secondary-label'>Secondary content</p></section></main></body>");
        CssParserOptions css_options;
        css_options.media_viewport_width = target.width;
        css_options.media_viewport_height = target.height;
        VectorDiagnosticSink css_diagnostics;
        css_options.diagnostics = &css_diagnostics;
        StyleResolver resolver(css_parser.parse(
            "body { margin: 0; width: 100%; height: 100%; }"
            "#screen { display: flex; flex-direction: row; box-sizing: border-box; width: 100%; "
            "height: 100%; gap: 8px; padding: 8px; }"
            "section { flex: 1 1 0; min-width: 0; height: auto; box-sizing: border-box; }"
            "p { margin: 0; width: 100%; overflow-wrap: anywhere; }"
            "@media (max-width: 200px) { #screen { flex-direction: column; gap: 4px; } "
            "section { width: 100%; flex: 0 0 40px; } }",
            css_options));
        RenderTreeBuilder render_tree_builder(resolver);
        auto render_tree = render_tree_builder.build(*document);
        VectorDiagnosticSink layout_diagnostics;
        LayoutEngineOptions layout_options;
        layout_options.diagnostics = &layout_diagnostics;
        LayoutEngine layout_engine(resolver, {}, layout_options);
        auto layout_tree = layout_engine.layout(*render_tree, target.width, target.height);

        const LayoutBox* screen = find_first_by_id(*layout_tree, "screen");
        const LayoutBox* primary = find_first_by_id(*layout_tree, "primary");
        const LayoutBox* secondary = find_first_by_id(*layout_tree, "secondary");
        check(screen != nullptr && primary != nullptr && secondary != nullptr,
              "responsive matrix boxes exist");
        check(screen->rect.width == target.width && screen->rect.height == target.height,
              "responsive matrix root fills every target viewport");
        const int content_left = screen->rect.x + screen->style.padding.left + screen->style.border_width.left;
        const int content_right = screen->rect.x + screen->rect.width -
            screen->style.padding.right - screen->style.border_width.right;
        check(primary->rect.x >= content_left && secondary->rect.x >= content_left &&
                  primary->rect.x + primary->rect.width <= content_right &&
                  secondary->rect.x + secondary->rect.width <= content_right,
              "responsive matrix children stay inside the padded viewport");
        if (target.narrow) {
            check(screen->style.flex_direction == FlexDirection::Column,
                  "narrow target selects the column media branch");
            check(primary->rect.width == secondary->rect.width &&
                      primary->rect.y < secondary->rect.y &&
                      secondary->rect.y >= primary->rect.y + primary->rect.height + 4,
                  "narrow target stacks full-width cards with the declared gap");
        } else {
            check(screen->style.flex_direction == FlexDirection::Row,
                  "wide targets keep the row media branch");
            check(primary->rect.y == secondary->rect.y &&
                      primary->rect.x < secondary->rect.x &&
                      primary->rect.width == secondary->rect.width &&
                      secondary->rect.x >= primary->rect.x + primary->rect.width + 8,
                  "wide targets distribute equal flex cards with the declared gap");
        }
        check(target.narrow
                  ? !has_diagnostic_code(css_diagnostics, "css-media-query-not-matched")
                  : has_diagnostic_code(css_diagnostics, "css-media-query-not-matched"),
              "responsive matrix reports only the non-selected media branch as unmatched");
        check(!has_diagnostic_code(layout_diagnostics, "visual-horizontal-overflow") &&
                  !has_diagnostic_code(layout_diagnostics, "visual-vertical-paint-overflow"),
              "responsive matrix has no viewport overflow diagnostic");
    }
}

void border_box_percent_width_accounts_for_edges() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main id='screen'><section id='card'></section></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "#screen { width: 200px; }"
        "#card { box-sizing: border-box; width: 100%; height: 20px; padding: 10px; border: 2px solid #000; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 240, 160);

    const LayoutBox* screen = find_first_by_id(*layout_tree, "screen");
    const LayoutBox* card = find_first_by_id(*layout_tree, "card");
    check(screen != nullptr && card != nullptr, "border-box percent fixture boxes exist");
    check(card->rect.width == 200, "border-box percent width keeps parent-sized border box");
    check(card->rect.x + card->rect.width <= screen->rect.x + screen->rect.width,
          "border-box percent child does not overflow parent horizontally");
}

void max_width_percent_clamps_nested_border_box() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><main id='screen'><section id='card'><div id='inner'></div></section></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "#screen { width: 172px; }"
        "#card { box-sizing: border-box; width: 300px; max-width: 100%; padding: 6px; border: 1px solid #000; }"
        "#inner { box-sizing: border-box; width: 100%; height: 18px; padding: 4px; border: 1px solid #000; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 172, 320);

    const LayoutBox* screen = find_first_by_id(*layout_tree, "screen");
    const LayoutBox* card = find_first_by_id(*layout_tree, "card");
    const LayoutBox* inner = find_first_by_id(*layout_tree, "inner");
    check(screen != nullptr && card != nullptr && inner != nullptr,
          "max-width percent fixture boxes exist");
    check(card->rect.width == 172, "max-width:100% clamps declared border-box width");
    check(card->rect.x + card->rect.width <= screen->rect.x + screen->rect.width,
          "clamped card stays inside narrow parent");
    check(inner->rect.x + inner->rect.width <= card->rect.x + card->rect.width - card->style.border_width.right,
          "nested border-box percent item stays inside clamped card");
}

void max_width_percent_clamps_nested_flex_grid_items() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><main id='screen'><section id='grid'><article id='left'><div id='left-inner'></div></article>"
        "<article id='right'><div id='right-inner'></div></article></section></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "#screen { display: flex; box-sizing: border-box; width: 172px; padding: 6px; border: 1px solid #000; }"
        "#grid { display: grid; grid-template-columns: 1fr 1fr; gap: 4px; flex: 1; min-width: 0; }"
        "article { box-sizing: border-box; width: 300px; max-width: 100%; padding: 5px; border: 1px solid #000; }"
        "article div { box-sizing: border-box; width: 100%; height: 18px; padding: 3px; border: 1px solid #000; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 172, 320);

    const LayoutBox* screen = find_first_by_id(*layout_tree, "screen");
    const LayoutBox* grid = find_first_by_id(*layout_tree, "grid");
    const LayoutBox* left = find_first_by_id(*layout_tree, "left");
    const LayoutBox* right = find_first_by_id(*layout_tree, "right");
    const LayoutBox* left_inner = find_first_by_id(*layout_tree, "left-inner");
    const LayoutBox* right_inner = find_first_by_id(*layout_tree, "right-inner");
    check(screen != nullptr && grid != nullptr && left != nullptr && right != nullptr &&
              left_inner != nullptr && right_inner != nullptr,
          "nested flex grid fixture boxes exist");
    const int screen_content_right = screen->rect.x + screen->rect.width -
        screen->style.border_width.right - screen->style.padding.right;
    check(grid->rect.x + grid->rect.width <= screen_content_right,
          "flex grid fits its border-box parent on a narrow target");
    check(left->rect.x + left->rect.width <= grid->rect.x + grid->rect.width &&
              right->rect.x + right->rect.width <= grid->rect.x + grid->rect.width,
          "max-width clamps each grid card inside the flex item");
    check(left_inner->rect.x + left_inner->rect.width <= left->rect.x + left->rect.width &&
              right_inner->rect.x + right_inner->rect.width <= right->rect.x + right->rect.width,
          "nested percent children stay inside clamped grid cards");
}

void grid_places_fixed_columns_and_spans() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><main><section id='a'></section><section id='b'></section><section id='wide'></section></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { display: grid; grid-template-columns: 40px 1fr; width: 120px; gap: 5px; }"
        "section { height: 10px; }"
        "#wide { grid-column: span 2; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 160);

    const LayoutBox* a = find_first_by_id(*layout_tree, "a");
    const LayoutBox* b = find_first_by_id(*layout_tree, "b");
    const LayoutBox* wide = find_first_by_id(*layout_tree, "wide");
    check(a != nullptr && b != nullptr && wide != nullptr, "grid fixture boxes exist");
    check(a->rect.x == 0 && a->rect.width == 40, "fixed grid column width applies");
    check(b->rect.x == 45 && b->rect.width == 75, "flexible grid column fills remaining width");
    check(wide->rect.x == 0 && wide->rect.y == 15 && wide->rect.width == 120,
          "span grid item moves to next row and covers both columns");
}

void grid_aligns_items_within_fixed_height_rows() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><main id='center'><span id='center-item'></span></main>"
        "<main id='end'><span id='end-item'></span></main>"
        "<main id='self'><span id='self-item'></span></main>"
        "<main id='stretch'><span id='stretch-item'></span></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { display: grid; width: 80px; height: 30px; grid-template-columns: 1fr; align-items: center; }"
        "span { display: block; height: 10px; margin-top: 2px; margin-bottom: 4px; }"
        "#end { align-items: flex-end; }"
        "#self-item { align-self: flex-end; }"
        "#stretch { align-items: stretch; } #stretch-item { height: auto; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 100, 160);

    const LayoutBox* center = find_first_by_id(*layout_tree, "center");
    const LayoutBox* center_item = find_first_by_id(*layout_tree, "center-item");
    const LayoutBox* end = find_first_by_id(*layout_tree, "end");
    const LayoutBox* end_item = find_first_by_id(*layout_tree, "end-item");
    const LayoutBox* self = find_first_by_id(*layout_tree, "self");
    const LayoutBox* self_item = find_first_by_id(*layout_tree, "self-item");
    const LayoutBox* stretch = find_first_by_id(*layout_tree, "stretch");
    const LayoutBox* stretch_item = find_first_by_id(*layout_tree, "stretch-item");
    check(center != nullptr && center_item != nullptr && end != nullptr && end_item != nullptr &&
              self != nullptr && self_item != nullptr && stretch != nullptr && stretch_item != nullptr,
          "grid alignment fixture boxes exist");
    check(center_item->rect.y - center->rect.y == 9,
          "grid center alignment uses the item's outer margin box");
    check(end_item->rect.y - end->rect.y == 16,
          "grid end alignment uses the item's outer margin box");
    check(self_item->rect.y - self->rect.y == 16,
          "grid align-self overrides the container alignment");
    check(stretch_item->rect.y - stretch->rect.y == 2 && stretch_item->rect.height == 24,
          "grid stretch fills a fixed-height row while preserving margins");
}

void grid_auto_column_uses_intrinsic_content_width() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><header id='header'><span id='leading'></span><b id='trailing'>08:42</b></header></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "#header { display: grid; width: 158px; grid-template-columns: 1fr auto; }"
        "#trailing { font-size: 11px; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 172, 80);

    const LayoutBox* header = find_first_by_id(*layout_tree, "header");
    const LayoutBox* leading = find_first_by_id(*layout_tree, "leading");
    const LayoutBox* trailing = find_first_by_id(*layout_tree, "trailing");
    check(header != nullptr && leading != nullptr && trailing != nullptr,
          "auto grid column fixture boxes exist");
    check(trailing->rect.width < header->rect.width / 2,
          "auto grid column shrink-wraps its text instead of sharing fractional space");
    check(trailing->rect.x + trailing->rect.width == header->rect.x + header->rect.width,
          "auto grid column is placed against the grid end");
    check(leading->rect.width + trailing->rect.width == header->rect.width,
          "fractional grid column receives the remaining width");
}

void range_author_dimensions_override_control_defaults() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><main><input id='level' type='range' min='0' max='100' value='25'></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; } main { width: 42px; }"
        "#level { display: block; width: 38px; height: 10px; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 80, 80);

    const LayoutBox* level = find_first_by_id(*layout_tree, "level");
    check(level != nullptr, "range dimension fixture exists");
    check(level->rect.width == 38 && level->rect.height == 10,
          "explicit range dimensions are not enlarged by control defaults");
}

void grid_places_explicit_rows_and_numeric_lines() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><main><section id='top'></section><section id='side'></section>"
        "<section id='bottom'></section></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { display: grid; width: 120px; height: 90px; grid-template-columns: 40px 1fr;"
        " grid-template-rows: 20px 1fr; gap: 5px; }"
        "section { box-sizing: border-box; }"
        "#top { grid-column: 1 / 3; grid-row: 1; }"
        "#side { grid-column: 1; grid-row: 2; }"
        "#bottom { grid-column: 2; grid-row: 2 / span 1; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 160, 120);

    const LayoutBox* top = find_first_by_id(*layout_tree, "top");
    const LayoutBox* side = find_first_by_id(*layout_tree, "side");
    const LayoutBox* bottom = find_first_by_id(*layout_tree, "bottom");
    check(top != nullptr && side != nullptr && bottom != nullptr, "explicit grid fixture boxes exist");
    check(top->rect.x == 0 && top->rect.y == 0 && top->rect.width == 120,
          "numeric grid end line spans both columns");
    check(side->rect.x == 0 && side->rect.y == 25 && side->rect.width == 40,
          "numeric first column and second row resolve");
    check(bottom->rect.x == 45 && bottom->rect.y == 25 && bottom->rect.width == 75,
          "numeric start plus span resolves remaining cell");
}

void grid_row_budget_uses_non_overlapping_block_fallback() {
    std::string html = "<body><main id='grid'>";
    for (int index = 0; index <= 128; ++index) {
        html += "<section id='cell-" + std::to_string(index) + "'>" + std::to_string(index) + "</section>";
    }
    html += "</main></body>";

    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(html);
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; } main { display: grid; width: 80px; row-gap: 1px; }"
        "section { height: 4px; box-sizing: border-box; }"));
    VectorDiagnosticSink diagnostics;
    LayoutEngineOptions options;
    options.diagnostics = &diagnostics;
    LayoutEngine layout_engine(resolver, {}, options);
    auto layout_tree = layout_engine.layout(*document, 80);

    const LayoutBox* penultimate = find_first_by_id(*layout_tree, "cell-127");
    const LayoutBox* fallback = find_first_by_id(*layout_tree, "cell-128");
    check(penultimate != nullptr && fallback != nullptr, "bounded grid fixture boxes exist");
    check(fallback->rect.y >= penultimate->rect.y + penultimate->rect.height,
          "row-budget fallback does not overlap the final tracked grid item");
    check(fallback->rect.width <= 80, "row-budget fallback remains within the grid content width");
    check(has_diagnostic_code(diagnostics, "grid-placement-budget"), "row-budget fallback is diagnosed");
}

void nowrap_text_overflow_reports_diagnostic() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><p id='status'>SuperLongStatusLabelWithoutBreaks</p></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "#status { width: 40px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }"));
    VectorDiagnosticSink diagnostics;
    LayoutEngine layout_engine(resolver, {}, LayoutEngineOptions{4096, &diagnostics});
    auto layout_tree = layout_engine.layout(*document, 160, 120);
    (void) layout_tree;

    bool found = false;
    for (const Diagnostic& diagnostic : diagnostics.diagnostics()) {
        if (diagnostic.code == "layout-text-overflow" ||
            diagnostic.code == "layout-text-overflow-ellipsis") {
            found = true;
            check(diagnostic.detail.find("text=\"SuperLongStatusLabelWithoutBreaks\"") != std::string::npos,
                  "overflow diagnostic includes text snippet");
            check(diagnostic.detail.find("measuredWidth=") != std::string::npos,
                  "overflow diagnostic includes measured width");
            check(diagnostic.detail.find("availableWidth=") != std::string::npos,
                  "overflow diagnostic includes available width");
            check(diagnostic.detail.find("node=\"p#status\"") != std::string::npos,
                  "overflow diagnostic includes node label");
            check(diagnostic.detail.find("path=\"") != std::string::npos &&
                  diagnostic.detail.find("p#status") != std::string::npos,
                  "overflow diagnostic includes stable dom path");
            break;
        }
    }
    check(found, "nowrap overflow emits layout diagnostic");
}

void layout_saturates_extreme_box_values() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main id='extreme'>Content</main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "#extreme { width: 2147483647px; padding: 2147483647px; "
        "border-width: 2147483647px; margin: 2147483647px; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*RenderTreeBuilder(resolver).build(*document), 240, 240);
    const LayoutBox* extreme = find_first_by_id(*layout_tree, "extreme");
    check(extreme != nullptr, "extreme layout fixture exists");
    check(extreme->rect.width >= 0 && extreme->rect.height >= 0,
          "extreme box arithmetic does not wrap dimensions negative");
}

} // namespace

int main() {
    try {
        layout_tree_can_use_monotonic_arena();
        fixed_separate_table_uses_columns_spacing_and_shared_rows();
        fixed_table_positions_caption_subtree_and_resolves_auto_margins();
        fixed_table_refuses_spans_nested_tables_and_percentage_widths();
        fixed_table_refuses_extreme_and_unqualified_inputs_transactionally();
        table_border_spacing_rejects_extra_components();
        fixed_table_refuses_retained_sizing_and_constraints();
        table_inherits_border_state_and_auto_resets_fixed();
        table_spacing_keeps_prior_value_for_unqualified_fractions();
        fixed_table_caption_side_inherits_and_honors_explicit_top();
        fixed_table_relative_position_keeps_following_flow();
        fixed_table_distributes_surplus_proportionally_and_preserves_edges();
        layout_tree_respects_box_budget();
        layout_tree_reports_box_budget_diagnostic();
        layout_saturates_extreme_box_values();
        layout_tree_reports_depth_budget_diagnostic();
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
        nested_flex_sizing_does_not_repeat_subtree_layout();
        flex_row_distributes_grow_space();
        flex_row_shrinks_basis_widths();
        flex_row_justifies_and_aligns_items();
        flex_cross_axis_stretch_resolves_auto_size();
        flex_cross_axis_alignment_uses_outer_size_and_align_self();
        flex_wrap_aligns_items_within_each_line();
        flex_row_supports_end_and_space_evenly_justification();
        flex_wrap_stacks_lines_with_row_gap();
        flex_wrap_align_content_distributes_lines();
        flex_column_distributes_vertical_space_and_aligns_items();
        flex_column_distributes_grow_space();
        flex_order_reorders_in_flow_layout_without_touching_default_path();
        flex_column_resolves_percent_height_against_containing_box();
#endif
        positioned_layout_offsets_without_flow_space();
        logical_inset_layout_uses_ltr_physical_offsets();
        relative_layout_offsets_visual_box_only();
        border_box_sizing_keeps_declared_width_and_height();
        percentage_width_and_height_use_containing_box();
        viewport_units_use_actual_layout_viewport();
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
        responsive_layout_matrix_keeps_same_app_inside_three_targets();
#endif
        border_box_percent_width_accounts_for_edges();
        max_width_percent_clamps_nested_border_box();
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
        max_width_percent_clamps_nested_flex_grid_items();
        grid_places_fixed_columns_and_spans();
        grid_aligns_items_within_fixed_height_rows();
        grid_auto_column_uses_intrinsic_content_width();
        range_author_dimensions_override_control_defaults();
        grid_places_explicit_rows_and_numeric_lines();
        grid_row_budget_uses_non_overlapping_block_fallback();
#endif
        nowrap_text_overflow_reports_diagnostic();
    } catch (const std::exception& error) {
        std::cerr << "layout test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "layout tests passed\n";
    return 0;
}
