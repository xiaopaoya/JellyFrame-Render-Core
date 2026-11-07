#include "block_precision_cases.h"
#include "render_core/css_parser.h"
#include "render_core/html_parser.h"
#include "render_core/layout.h"
#include "render_core/hit_test.h"
#include "render_core/software_renderer.h"
#include "../../src/computed_size_geometry.h"
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace jellyframe;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
const LayoutBox* by_id(const LayoutBox& box, const char* id) {
    if (box.node && box.node->attribute("id") == id) return &box;
    for (const auto& child : box.children) if (auto* found = by_id(*child,id)) return found;
    return nullptr;
}
int main() {
    try {
        for (const auto& c : block_precision_cases::cases) {
            auto dom = HtmlParser{}.parse("<div id='p'><div id='c'><div id='g'></div></div></div>");
            StyleResolver resolver(CssParser{}.parse(c.css));
            auto layout = LayoutEngine(resolver).layout(*dom,320,240);
            unsigned index=0;
            for (auto id : {"p","c","g"}) {
                const auto* box=by_id(*layout,id);
                const auto* r=c.rects[index++];
                if (!box || box->rect.x!=r[0] || box->rect.y!=r[1] || box->rect.width!=r[2] || box->rect.height!=r[3]) {
                    std::cerr << id << " expected=" << r[0] << ',' << r[1] << ',' << r[2] << ',' << r[3] << '\n';
                    if (box) std::cerr << "actual=" << box->rect.x << ',' << box->rect.y << ',' << box->rect.width << ',' << box->rect.height << '\n';
                    throw std::runtime_error("nested ordinary geometry differs from rational oracle");
                }
            }
        }
        {
            auto dom = HtmlParser{}.parse("<div id='p'><div id='a'></div><div id='b'></div><div id='c'></div></div>");
            StyleResolver resolver(CssParser{}.parse("*{margin:0;padding:0;border:0}#p{width:25px}#p>div{height:10.4px}#a{background:#ff0000}#b{background:#00ff00}#c{background:#0000ff}"));
            auto layout=LayoutEngine(resolver).layout(*dom,320,240);
            check(by_id(*layout,"p")->rect.height==31,"fractional child heights accumulate before projection");
            check(by_id(*layout,"b")->rect.y==10 && by_id(*layout,"b")->rect.height==11 &&
                  by_id(*layout,"c")->rect.y==21,"adjacent projected edges do not leave seams");
            auto layers=LayerTreeBuilder{}.build(*layout);
            auto frame=SoftwareCompositor{}.render(*layers,320,240,Color{255,255,255,255});
            check(frame.pixel(2,20).g==255 && frame.pixel(2,21).b==255,"paint uses projected shared edges");
            check(HitTester{}.hit_test(*layout,2,20).node==by_id(*layout,"b")->node &&
                  HitTester{}.hit_test(*layers,2,21).node==by_id(*layout,"c")->node,"layout and layer hit testing agree with paint");
        }
        {
            auto dom=HtmlParser{}.parse("<div id='p'><div id='c'><div id='g'></div></div></div>");
            StyleResolver resolver(CssParser{}.parse("*{margin:0;padding:0;border:0}#p{width:100.25px;height:100.25px;box-sizing:border-box;padding:10px;border:2px solid black}#c{box-sizing:border-box;width:120px;height:100%;min-width:50.4px;max-width:37.5%;min-height:40.4px;max-height:30.2px;padding:3px;border:1px solid black}#g{width:100%;height:100%}"));
            auto layout=LayoutEngine(resolver).layout(*dom,320,240);
            const auto* child=by_id(*layout,"c");const auto* grand=by_id(*layout,"g");
            check(child->rect.width==50 && child->rect.height==40,"minimum wins conflicting maximum in both axes");
            check(grand->rect.width==42 && grand->rect.height==32,"nested percentages use constrained fractional content boxes");
        }
        {
            auto dom=HtmlParser{}.parse("<div id='p'><div id='c'></div></div>");
            StyleResolver resolver(CssParser{}.parse("*{margin:0;padding:0;border:0}#p{width:100.25px}#c{height:50%;width:37.5%}"));
            auto layout=LayoutEngine(resolver).layout(*dom,320,240);
            check(by_id(*layout,"c")->rect.height==0,"auto-height ordinary parent leaves percentage height indefinite");
            check(by_id(*layout,"c")->rect.width==38,"fractional percent reaches final geometry");
        }
        {
            auto dom=HtmlParser{}.parse("<div id='p'><div id='c'></div></div>");
            StyleResolver resolver(CssParser{}.parse("*{margin:0;padding:0;border:0}#p{width:100.25px}#c{width:37.5%;height:1px}"));
            MonotonicArena arena(256);
            auto layout=LayoutEngine(resolver).layout(*dom,320,240,arena);
            check(by_id(*layout,"c")->rect.width==38,"arena consumer uses the same fractional layout path");
        }
        {
            using namespace detail;
            check(scale_size_units(kDimensionPrecisionMaximum,2147483647LL*1000,100000)==kDimensionPrecisionMaximum,
                  "extreme manual percentages saturate without overflowing");
            check(scale_size_units(-1,1,1)==-2 && scale_size_units(1,1,0)==-2,"invalid arithmetic refuses");
            check(project_size_edge(std::numeric_limits<std::int64_t>::min())==std::numeric_limits<int>::min() &&
                  project_size_extent(std::numeric_limits<std::int64_t>::max(),5)==0,"projection remains defined at int64 extremes");
        }
        {
            auto dom=HtmlParser{}.parse("<div id='p'><div id='c'></div></div>");
            StyleResolver resolver(CssParser{}.parse("*{margin:0;padding:0;border:0}#p{width:100.25px}#c{width:37.5%;height:1px;margin-left:auto;margin-right:auto}"));
            auto layout=LayoutEngine(resolver).layout(*dom,320,240);
            const auto* child=by_id(*layout,"c");
            // Width 37.594; free space 62.656; origin 31.328; right 68.922.
            check(child->rect.x==31 && child->rect.width==38,"auto margins retain exact free space before edge projection");
            VectorDiagnosticSink diagnostics;
            LayoutEngineOptions options;options.max_layout_depth=2;options.diagnostics=&diagnostics;
            auto limited=LayoutEngine(resolver,{},options).layout(*dom,320,240);
            bool depth_reported=false;
            for (const auto& diagnostic : diagnostics.diagnostics())
                depth_reported |= diagnostic.code=="layout-depth-limit";
            check(depth_reported && by_id(*limited,"c")->rect.width==0,
                  "precision path reports depth refusal and leaves deeper geometry unlaid out");
        }
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
        {
            auto dom=HtmlParser{}.parse("<div id='p'><div id='c'></div></div>");
            StyleResolver resolver(CssParser{}.parse("*{margin:0;padding:0;border:0}#p{display:flex;width:100px}#c{flex:1 1 0;width:20.5px;height:5px}"));
            auto layout=LayoutEngine(resolver).layout(*dom,320,240);
            const auto* child=by_id(*layout,"c");
            detail::SizeKind kind;std::int64_t units=0;
            check(child->rect.width==100 && child->style.width==21 &&
                  child->style.computed_sizes.get(detail::SizeProperty::Width,kind,units) && units==20500,
                  "Flex allocation does not overwrite retained authored width");
        }
#endif
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
    std::cout << "72 ordinary rational scenes and integration checks passed\n";
}
