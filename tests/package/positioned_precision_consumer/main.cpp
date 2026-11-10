#include <render_core/css_parser.h>
#include <render_core/hit_test.h>
#include <render_core/html_parser.h>
#include <render_core/layer_tree.h>
#include <render_core/layout.h>
#include <render_core/software_renderer.h>

#include <iostream>
#include <stdexcept>
#include <string>

using namespace jellyframe;

const LayoutBox* find_box(const LayoutBox& root, const char* id) {
    if (root.node && root.node->attribute("id") == id) return &root;
    for (const auto& child : root.children) {
        if (const auto* found = find_box(*child, id)) return found;
    }
    return nullptr;
}

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void check_geometry(const LayoutBox& root, int x, int y, int width, int height) {
    const auto* box = find_box(root, "c");
    require(box, "Missing installed-consumer child");
    const auto& rect = box->rect;
    if (rect.x != x || rect.y != y || rect.width != width || rect.height != height) {
        std::cerr << "Unexpected installed geometry " << rect.x << ',' << rect.y
                  << ',' << rect.width << ',' << rect.height << '\n';
        throw std::runtime_error("Installed geometry mismatch");
    }
}

int main() {
    try {
        const std::string base = "*{margin:0;padding:0;border:0}"
            "document,html,body{margin:0;padding:0;width:776px;height:501px}"
            "#p{position:absolute;left:50px;top:40px;width:240px;height:180px;"
            "padding:20px;border:6px solid;box-sizing:content-box}"
            "#c{width:20px;height:12px;background:#ff0000}";
        struct Case { const char* css; int x, y, width, height; };
        const Case cases[] = {
            {"position:absolute;left:25%;top:25%",126,101,20,12},
            {"position:absolute;right:25%;bottom:25%",246,199,20,12},
            {"position:absolute;left:-25%;top:calc(25% - 5px)",-14,96,20,12},
            {"position:absolute;left:25%;right:25%;top:0;width:auto",126,46,140,12},
            {"position:relative;left:25%;top:25%",136,111,20,12},
            {"position:fixed;left:25%;top:25%",194,125,20,12},
        };
        for (const auto& test : cases) {
            auto dom = HtmlParser{}.parse("<div id='p'><div id='c'></div></div>");
            StyleResolver resolver(CssParser{}.parse(base + "#c{" + test.css + "}"));
            auto layout = LayoutEngine(resolver).layout(*dom,776,501);
            check_geometry(*layout,test.x,test.y,test.width,test.height);
        }

        auto dom = HtmlParser{}.parse("<div id='p'><div id='c'></div></div>");
        StyleResolver resolver(CssParser{}.parse(base +
            "#c{position:absolute;left:25%;top:25%}"));
        LayoutEngine engine(resolver);
        auto before = engine.layout(*dom,776,501);
        const auto* original = find_box(*before,"c");
        require(original && original->node,"Missing mutation target");
        const_cast<Node*>(original->node)->set_attribute("style","left:50%;top:50%");
        auto after = engine.layout(*dom,776,501);
        check_geometry(*after,196,156,20,12);
        require(HitTester{}.hit_test(*after,198,158).node == original->node,
                "Updated layout hit target mismatch");
        require(HitTester{}.hit_test(*after,128,103).node != original->node,
                "Old hit footprint survived mutation");
        auto layers = LayerTreeBuilder{}.build(*after);
        require(HitTester{}.hit_test(*layers,198,158).node == original->node,
                "Updated layer hit target mismatch");
        auto image = SoftwareCompositor{}.render(*layers,776,501,Color{255,255,255,255});
        const auto pixel = image.pixel(198,158);
        require(pixel.r == 255 && pixel.g == 0 && pixel.b == 0 && pixel.a == 255,
                "Updated positioned child did not paint at its hit geometry");
        std::cout << "Installed positioned geometry, mutation, hit and paint passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
