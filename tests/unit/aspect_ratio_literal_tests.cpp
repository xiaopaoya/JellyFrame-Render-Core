#include "../../src/css_aspect_ratio.h"
#include "css_aspect_ratio_source_cases.h"
#include "css_aspect_ratio_numeric_cases.h"
#include "render_core/css_parser.h"
#include "render_core/html_parser.h"
#include "render_core/layout.h"
#include "render_core/render_tree.h"
#include "render_core/style_repaint.h"
#include <iostream>
#include <stdexcept>

using namespace jellyframe;
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
const RenderObject* by_id(const RenderObject& object, const char* id) {
    if (object.node && object.node->attribute("id") == id) return &object;
    for (const auto& child : object.children) if (auto* found = by_id(*child, id)) return found;
    return nullptr;
}
template<class Case> void replay(const Case& c) {
    detail::CssAspectRatio ratio{91,97,false};
    const auto accepted = detail::parse_css_aspect_ratio(c.value, ratio);
    if (accepted != c.accepted || (accepted ?
        ratio.width != c.width || ratio.height != c.height || ratio.automatic != c.automatic :
        ratio.width != 91 || ratio.height != 97 || ratio.automatic)) {
        std::cerr << "ratio input=" << c.value << '\n';
        throw std::runtime_error("frozen literal ratio oracle mismatch");
    }
}
int main() {
    try {
        for (const auto& c : tests::aspect_ratio_source::cases) replay(c);
        for (const auto& c : tests::aspect_ratio_numeric::cases) replay(c);
        for (const auto& c : tests::aspect_ratio_source::cases) {
            auto node = make_element("div");
            node->set_attribute("style", std::string("aspect-ratio:") + c.value);
            auto style = StyleResolver(CssParser{}.parse("")).resolve(*node);
            if (c.accepted) check(style.aspect_ratio_width == c.width && style.aspect_ratio_height == c.height &&
                style.aspect_ratio_auto == c.automatic, "accepted ratio declarations share the literal parser");
        }
        for (auto value : {"auto", "16/9", "auto 16/9", "1e3/5e2"}) {
            auto sheet = CssParser{}.parse(std::string("@supports (aspect-ratio:") + value + "){div{width:37px}}");
            auto node = make_element("div");
            check(StyleResolver(sheet).resolve(*node).width == 37, "supports accepts implemented ratio values");
        }
        for (auto value : {"bad/bad", "1px/2", "2/1auto"}) {
            auto sheet = CssParser{}.parse(std::string("@supports (aspect-ratio:") + value + "){div{width:37px}}");
            auto node = make_element("div");
            check(StyleResolver(sheet).resolve(*node).width == -1, "supports rejects malformed ratio tokens");
        }
        auto document = HtmlParser{}.parse(
            "<div id='parent' style='aspect-ratio:auto 16/9'>"
            "<div id='child' style='aspect-ratio:inherit'><div id='grandchild' style='aspect-ratio:inherit'></div></div>"
            "<div id='initial' style='aspect-ratio:initial'></div><div id='default'></div></div>");
        StyleResolver resolver(CssParser{}.parse(""));
        auto tree = RenderTreeBuilder(resolver).build(*document);
        for (auto id : {"parent", "child", "grandchild"}) {
            const auto* box = by_id(*tree, id);
            check(box && box->style.aspect_ratio_width == 16 && box->style.aspect_ratio_height == 9 &&
                  box->style.aspect_ratio_auto, "explicit ratio inheritance finalizes parent-first");
        }
        for (auto id : {"initial", "default"}) {
            const auto* box = by_id(*tree, id);
            check(box && box->style.aspect_ratio_width == 0 && box->style.aspect_ratio_height == 0 &&
                  box->style.aspect_ratio_auto, "ratio is not inherited by default");
        }
        auto root = make_element("div");
        root->set_attribute("style", "aspect-ratio:inherit");
        auto root_tree = RenderTreeBuilder(resolver).build(*root);
        check(root_tree->style.aspect_ratio_width == 0 && root_tree->style.aspect_ratio_auto,
              "root ratio inheritance resolves to initial");
        root->set_attribute("style", "width:80px;aspect-ratio:2/1");
        auto before = RenderTreeBuilder(resolver).build(*root);
        auto layout = LayoutEngine(resolver).layout(*before,320,240);
        root->set_attribute("style", "width:80px;aspect-ratio:auto 2/1");
        auto after = RenderTreeBuilder(resolver).build(*root);
        check(!before->style.aspect_ratio_auto && after->style.aspect_ratio_auto,
              "author ratio and auto ratio retain distinct identity");
        check(!style_dirty_can_reuse_layout(*root,*before,*after,*layout,{}),
              "ratio identity changes invalidate retained layout");
        root->set_attribute("style", "aspect-ratio:3/2;aspect-ratio:bad");
        auto style = resolver.resolve(*root);
        check(style.aspect_ratio_width == 3 && style.aspect_ratio_height == 2 && !style.aspect_ratio_auto,
              "invalid ratio declarations preserve prior state");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "literal ratio oracle and state tests passed\n";
}
