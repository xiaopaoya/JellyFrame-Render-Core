#include "render_core/css_parser.h"
#include "render_core/html_parser.h"
#include "render_core/layout.h"
#include "render_core/layer_tree.h"
#include "render_core/render_tree.h"

#include <cstdlib>
#include <iostream>

using namespace jellyframe;

const LayoutBox* by_id(const LayoutBox& box, const char* id) {
    if (box.node != nullptr && box.node->attribute("id") == id) return &box;
    for (const auto& child : box.children) {
        if (const LayoutBox* found = by_id(*child, id)) return found;
    }
    return nullptr;
}

int main() {
    HtmlParser html;
    CssParser css;
    auto document = html.parse(
        "<body><table id='t'><caption id='cap'><span id='cap-text'>Bottom</span></caption>"
        "<colgroup><col id='c1'><col id='c2'></colgroup><tbody><tr>"
        "<td id='a'>Alpha</td><td id='b'>Beta</td></tr></tbody></table></body>");
    StyleResolver resolver(css.parse(
        "body{margin:0}#t{width:100px;table-layout:fixed;border-spacing:4px 3px;"
        "caption-side:bottom;margin-left:auto;margin-right:auto}"
        "#c1{width:40px}#c2{width:50px}"
        "td{padding:5px;border:1px solid #000000;background-color:#d5f1e4}"
        "caption{background-color:#dae9ff}"));
    auto render = RenderTreeBuilder(resolver).build(*document);
    LayoutEngine engine(resolver);
    auto layout = engine.layout(*render, 240);
    const auto* table = by_id(*layout, "t");
    const auto* caption = by_id(*layout, "cap");
    const auto* caption_text = by_id(*layout, "cap-text");
    const auto* a = by_id(*layout, "a");
    const auto* b = by_id(*layout, "b");
    if (table == nullptr || caption == nullptr || caption_text == nullptr || a == nullptr || b == nullptr)
        return EXIT_FAILURE;
    if (table->rect.width != 102 || table->rect.x != 69 || caption->rect.y < a->rect.y + a->rect.height ||
        caption_text->rect.y < caption->rect.y ||
        caption_text->rect.y + caption_text->rect.height > caption->rect.y + caption->rect.height) {
        std::cerr << "width=" << table->rect.width << " x=" << table->rect.x
                  << " caption=" << caption->rect.y << " row=" << a->rect.y << "," << a->rect.height << '\n';
        return EXIT_FAILURE;
    }
    if (a->rect.x + a->rect.width > b->rect.x || a->rect.y != b->rect.y || a->rect.height != b->rect.height)
        return EXIT_FAILURE;
    if (!table_layout_is_fixed(table->style) || table_border_spacing_x(table->style) != 4 ||
        table_border_spacing_y(table->style) != 3) return EXIT_FAILURE;

    LayerTreeBuilder layer_builder;
    auto layers = layer_builder.build(*layout);
    const DisplayList commands = layer_builder.flatten(*layers);
    bool cell_background = false;
    bool caption_background = false;
    for (const DisplayCommand& command : commands) {
        if (command.type() != DisplayCommandType::FillRect) continue;
        const Color& color = command.color;
        cell_background = cell_background ||
            (color.r == 213 && color.g == 241 && color.b == 228);
        caption_background = caption_background ||
            (color.r == 218 && color.g == 233 && color.b == 255);
    }
    return cell_background && caption_background ? EXIT_SUCCESS : EXIT_FAILURE;
}
