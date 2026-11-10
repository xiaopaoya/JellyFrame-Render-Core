#include "render_core/css_parser.h"
#include "render_core/html_parser.h"
#include "render_core/layout.h"
#include "render_core/dirty_region.h"
#include "render_core/hit_test.h"
#include "render_core/layer_tree.h"
#include "render_core/software_renderer.h"
#include "render_core/style_repaint.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace jellyframe;

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
const LayoutBox& by_id(const LayoutBox& root, const char* id) {
    const auto find = [&](const auto& self, const LayoutBox& box) -> const LayoutBox* {
        if (box.node && box.node->attribute("id") == id) return &box;
        for (const auto& child : box.children) if (const auto* result = self(self, *child)) return result;
        return nullptr;
    };
    const auto* result = find(find, root);
    if (!result) throw std::runtime_error(std::string("missing box ")+id);
    return *result;
}
void rect(const LayoutBox& box, int x, int y, int width, int height, const char* message) {
    if (box.rect.x != x || box.rect.y != y || box.rect.width != width || box.rect.height != height) {
        std::cerr << message << " expected=" << x << ',' << y << ',' << width << ',' << height
            << " actual=" << box.rect.x << ',' << box.rect.y << ',' << box.rect.width << ',' << box.rect.height << '\n';
        throw std::runtime_error(message);
    }
}
const std::string reset = "*{margin:0;padding:0;border:0}document,html,body{margin:0;padding:0}";
const std::string parent_rule = "#p{position:absolute;left:50px;top:40px;width:240px;height:180px;"
    "padding:20px;border:6px solid;box-sizing:content-box}#c{width:20px;height:12px}";

void browser_reference_cases() {
    // Historical independent Chrome157 fixture and raw observations are mapped
    // and hashed in docs/validation/c5_positioned_precision_20261110.md.
    struct Case { const char* child; const char* parent; bool wrapper; int x,y,width,height; };
    const Case cases[] = {
        {"position:absolute;left:25%;top:25%", "", false, 126,101,20,12},
        {"position:absolute;left:25%;top:25%", "", true, 126,101,20,12},
        {"position:absolute;right:25%;bottom:25%", "", false, 246,199,20,12},
        {"position:absolute;left:-25%;top:calc(25% - 5px)", "", false, -14,96,20,12},
        {"position:absolute;left:25%;right:25%;top:0;width:auto", "", false, 126,46,140,12},
        {"position:relative;left:25%;top:25%", "", false, 136,111,20,12},
        {"position:relative;left:25%;top:25%", "height:auto", false, 136,66,20,12},
        {"position:fixed;left:25%;top:25%", "", false, 194,125,20,12},
        {"position:absolute;left:25%;top:25%", "width:320px;height:240px", false, 146,116,20,12},
        {"position:absolute;left:50%;top:50%", "left:50.25px;top:40.25px;width:240.5px;height:180.5px",
            false, 197,157,20,12},
    };
    for (const auto& c : cases) {
        auto dom = HtmlParser{}.parse(c.wrapper
            ? "<div id='p'><div id='w'><div id='c'></div></div></div>"
            : "<div id='p'><div id='c'></div></div>");
        StyleResolver resolver(CssParser{}.parse(reset+parent_rule+"#c{"+c.child+"}#p{"+c.parent+
            "}#w{margin:13px 0 0 31px;padding:17px;width:120px;height:60px}"));
        auto tree = LayoutEngine(resolver).layout(*dom,776,501);
        rect(by_id(*tree,"c"),c.x,c.y,c.width,c.height,c.child);
    }
}

void signed_edges_and_relative_precedence() {
    auto dom = HtmlParser{}.parse("<div id='p'><div id='c'></div><div id='after'></div></div>");
    const char* cases[] = {
        "position:absolute;left:calc(-25% + 5px);top:-25%",
        "position:absolute;right:-25%;bottom:calc(-25% - 5px)",
        "position:relative;right:-25%;bottom:calc(25% - 5px)",
        "position:relative;left:25%;right:90%;top:25%;bottom:90%",
    };
    const int expected[][2] = {{-9,-9},{386,314},{136,26},{136,111}};
    for (unsigned i=0;i<4;++i) {
        StyleResolver resolver(CssParser{}.parse(reset+parent_rule+"#c{"+cases[i]+"}#after{height:7px}"));
        auto tree = LayoutEngine(resolver).layout(*dom,776,501);
        rect(by_id(*tree,"c"),expected[i][0],expected[i][1],20,12,cases[i]);
        check(by_id(*tree,"after").rect.y == (i<2 ? 66 : 78),"relative and absolute offsets preserve following flow");
    }
    for (const char* offsets : {"top:25%;bottom:7px", "top:calc(25% + 5px);bottom:7px"}) {
        StyleResolver resolver(CssParser{}.parse(reset+parent_rule+"#p{height:auto}#c{position:relative;"+offsets+"}"));
        auto tree = LayoutEngine(resolver).layout(*dom,776,501);
        check(by_id(*tree,"c").rect.y==59,"indefinite percent becomes auto and permits opposite pixel edge");
    }
}

void stretch_constraints_and_nested_containing_blocks() {
    auto dom = HtmlParser{}.parse("<div id='p'><div id='c'><div id='g'></div></div></div>");
    StyleResolver resolver(CssParser{}.parse(reset+parent_rule+
        "#c{position:absolute;left:25%;right:25%;top:25%;bottom:25%;width:auto;height:auto;"
        "min-width:150px;max-width:160px;min-height:80px;max-height:90px;padding:5px;border:2px solid}"
        "#g{position:absolute;right:10%;bottom:10%;width:20px;height:10px}"));
    auto tree = LayoutEngine(resolver).layout(*dom,776,501);
    rect(by_id(*tree,"c"),126,101,164,104,"opposing insets obey min/max with decoration");
    // Child padding box starts (128,103), size 160x100.
    rect(by_id(*tree,"g"),252,183,20,10,"nested positioned box establishes its own padding box");
    check(by_id(*tree,"c").style.width==-1 && by_id(*tree,"c").style.height==-1,
        "temporary stretch allocations restore source scalars");
    check(by_id(*tree,"c").style.computed_sizes.empty()==false,
        "restored style preserves retained inset coefficients");
}

void resize_mutation_and_initial_containing_block() {
    auto dom = HtmlParser{}.parse("<div id='w' style='width:300px;height:100px;padding:20px'>"
        "<div id='c' style='position:absolute;left:25%;top:25%;width:20px;height:12px'></div>"
        "<div id='f' style='position:fixed;right:25%;bottom:25%;width:20px;height:12px'></div></div>");
    StyleResolver resolver(CssParser{}.parse(reset));
    LayoutEngine engine(resolver);
    auto first = engine.layout(*dom,400,300);
    rect(by_id(*first,"c"),100,75,20,12,"no positioned ancestor uses initial containing block");
    rect(by_id(*first,"f"),280,213,20,12,"fixed opposite edges use viewport height");
    auto second = engine.layout(*dom,800,600);
    rect(by_id(*second,"c"),200,150,20,12,"viewport resize recomputes absolute initial basis");
    rect(by_id(*second,"f"),580,438,20,12,"viewport resize recomputes fixed opposite edges");
    auto* wrapper = const_cast<Node*>(by_id(*second,"w").node);
    wrapper->set_attribute("style", "position:relative;width:200px;height:80px;padding:20px");
    auto third = engine.layout(*dom,800,600);
    rect(by_id(*third,"c"),60,30,20,12,"mutation establishes actual padding containing block");
    rect(by_id(*third,"f"),580,438,20,12,"fixed remains attached to viewport after mutation");
}

void relative_fractional_parent_shift_and_pixel_regressions() {
    auto dom = HtmlParser{}.parse("<div id='p'><div id='c'><div id='g'></div></div><div id='after'></div></div>");
    StyleResolver resolver(CssParser{}.parse(reset+
        "#p{position:relative;width:100.5px;height:80.5px;left:0.25px;top:0.25px}"
        "#c{position:relative;width:20px;height:12px;left:50%;top:50%}"
        "#g{position:absolute;left:50%;top:50%;width:2px;height:2px}#after{height:7px}"));
    auto tree=LayoutEngine(resolver).layout(*dom,400,300);
    rect(by_id(*tree,"c"),51,41,20,12,"relative fractional shifts combine before projection");
    rect(by_id(*tree,"g"),61,47,2,2,"absolute descendant follows relative ancestor exactly once");
    check(by_id(*tree,"after").rect.y==12,"relative shifts preserve flow placeholders");
    StyleResolver pixels(CssParser{}.parse(reset+
        "#p{position:relative;width:100px;height:80px;left:7px;top:3px}"
        "#c{position:absolute;left:4px;top:6px;width:20px;height:12px}"
        "#g{position:fixed;left:2px;top:5px;width:2px;height:2px}"));
    auto pixel_tree=LayoutEngine(pixels).layout(*dom,400,300);
    rect(by_id(*pixel_tree,"c"),11,9,20,12,"pixel-only absolute follows relative parent");
    rect(by_id(*pixel_tree,"g"),2,5,2,2,"nested fixed avoids relative parent displacement");
}

void parsing_preserves_or_rejects_without_viewport_substitution() {
    auto dom=HtmlParser{}.parse("<div id='p'><div id='c'></div></div>");
    for (const char* suffix : {"left:bogus", "left:calc(25% + nope)", "left:calc(25% + 1px + 2px)"}) {
        StyleResolver resolver(CssParser{}.parse(reset+parent_rule+
            "#c{position:absolute;left:25%;top:25%;"+suffix+"}"));
        auto tree=LayoutEngine(resolver).layout(*dom,776,501);
        rect(by_id(*tree,"c"),126,101,20,12,"invalid inset preserves previous retained declaration");
    }
    StyleResolver automatic(CssParser{}.parse(reset+parent_rule+
        "#c{position:absolute;left:25%;left:auto;right:25%;top:25%;top:auto;bottom:25%}"));
    auto tree=LayoutEngine(automatic).layout(*dom,776,501);
    rect(by_id(*tree,"c"),246,199,20,12,"auto clears retained coefficient and source flag");
    StyleResolver extremes(CssParser{}.parse(reset+parent_rule+
        "#c{position:absolute;left:-1000%;top:calc(1000% + 2147483647px)}"));
    auto extreme_tree=LayoutEngine(extremes).layout(*dom,std::numeric_limits<int>::max(),std::numeric_limits<int>::max());
    check(by_id(*extreme_tree,"c").rect.x<0 && by_id(*extreme_tree,"c").rect.y==std::numeric_limits<int>::max(),
        "signed extreme coefficient arithmetic saturates without wrapping");
}
}

void automatic_insets_keep_static_flow_origin() {
    auto dom=HtmlParser{}.parse("<div id='p'><div id='before'></div><div id='c'></div><div id='after'></div></div>");
    StyleResolver resolver(CssParser{}.parse(reset+
        "#p{position:relative;left:7px;top:3px;width:100px;padding:10px}"
        "#before{height:15px}#c{position:absolute;width:20px;height:12px}#after{height:5px}"));
    auto tree=LayoutEngine(resolver).layout(*dom,400,300);
    rect(by_id(*tree,"c"),17,28,20,12,"automatic insets keep static block-flow origin after preceding sibling");
    rect(by_id(*tree,"after"),17,28,100,5,"out-of-flow static placeholder consumes no height");
}

bool same_color(Color a, Color b) { return a.r==b.r && a.g==b.g && a.b==b.b && a.a==b.a; }
bool same_pixels(const FrameBuffer& a,const FrameBuffer& b) {
    return a.width==b.width && a.height==b.height && std::equal(a.pixels.begin(),a.pixels.end(),b.pixels.begin(),same_color);
}

void moved_boxes_refresh_paint_hit_and_retained_damage() {
    auto dom = HtmlParser{}.parse("<div id='p'><div id='c'><div id='g'></div></div></div>");
    StyleResolver resolver(CssParser{}.parse(reset+
        "#p{position:relative;width:200px;height:100px;padding:10px;border:2px solid}"
        "#c{position:absolute;left:10.25%;top:25%;width:20px;height:15px;background:#ff0000}"
        "#g{position:absolute;left:50%;top:50%;width:3px;height:3px;background:#0000ff}"));
    LayoutEngine engine(resolver);
    auto old_render = RenderTreeBuilder(resolver).build(*dom);
    auto old_layout = engine.layout(*old_render,320,200);
    rect(by_id(*old_layout,"c"),25,32,20,15,"paint fixture uses the 220x120 padding containing block");
    rect(by_id(*old_layout,"g"),35,40,3,3,"nested paint fixture uses the child padding containing block");
    LayerTreeBuilder builder;
    auto old_layers = builder.build(*old_layout);
    const auto old_rect = by_id(*old_layout,"c").rect;
    auto* changed = const_cast<Node*>(by_id(*old_layout,"c").node);
    SoftwareCompositor compositor;
    const Color white{255,255,255,255};
    auto retained = compositor.render(*old_layers,320,200,white);
    check(HitTester{}.hit_test(*old_layers,old_rect.x+2,old_rect.y+2).node == changed,
        "old layer hit uses positioned paint origin");
    clear_dirty_flags(*dom);
    changed->set_attribute("style","left:60.25%;top:50%;position:absolute");
    auto next_render = RenderTreeBuilder(resolver).build(*dom);
    check(!style_dirty_can_reuse_layout(*dom,*old_render,*next_render,*old_layout,{}),
        "retained inset mutation refuses layout reuse");
    auto next_layout = engine.layout(*next_render,320,200);
    rect(by_id(*next_layout,"c"),135,62,20,15,"mutated paint fixture resolves its retained percentage basis");
    rect(by_id(*next_layout,"g"),145,70,3,3,"mutated paint fixture resolves nested geometry");
    auto next_layers = builder.build(*next_layout);
    const auto current = by_id(*next_layout,"c").rect;
    check(current.x > old_rect.x+old_rect.width && current.y > old_rect.y,
        "inset mutation moves the measured box");
    check(HitTester{}.hit_test(*next_layers,current.x+2,current.y+2).node == changed &&
        HitTester{}.hit_test(*next_layers,old_rect.x+2,old_rect.y+2).node != changed,
        "new hit snapshot finds moved box and releases prior location");
    DirtyRegionOptions options;
    options.viewport = {0,0,320,200};
    options.previous_layer_tree = old_layers.get();
    options.current_layer_tree = next_layers.get();
    auto damage = compute_dirty_region(*dom,old_layout.get(),next_layout.get(),options);
    check(damage.mode == DirtyRegionMode::DirtyRects && !damage.rects.empty(),
        "moved box produces bounded retained damage");
    const auto covered = [&](int x,int y) {
        for (const auto& r : damage.rects)
            if (x>=r.x && y>=r.y && x<r.x+r.width && y<r.y+r.height) return true;
        return false;
    };
    check(covered(old_rect.x+2,old_rect.y+2) && covered(current.x+2,current.y+2),
        "damage covers previous and current positioned footprints");
    compositor.render_into(*next_layers,retained,white,damage.rects.data(),damage.rects.size());
    auto full = compositor.render(*next_layers,320,200,white);
    check(same_pixels(retained,full),"dirty retained replay equals complete moved-box repaint");
    check(same_color(full.pixel(current.x+2,current.y+2),Color{255,0,0,255}) &&
        same_color(full.pixel(old_rect.x+2,old_rect.y+2),white),"paint moves color and clears old footprint");
    // Moving the containing block also moves absolute descendants in the final
    // snapshot; their former pixels must be covered by ancestor invalidation.
    clear_dirty_flags(*dom);
    auto* parent = const_cast<Node*>(by_id(*next_layout,"p").node);
    parent->set_attribute("style","position:relative;left:13.25px;top:9.25px");
    auto shifted_layout = engine.layout(*dom,320,200);
    rect(by_id(*shifted_layout,"c"),148,71,20,15,"ancestor shift combines with inset before projection");
    rect(by_id(*shifted_layout,"g"),158,79,3,3,"nested paint geometry follows fractional ancestor shift");
    auto shifted_layers = builder.build(*shifted_layout);
    options.previous_layer_tree = next_layers.get(); options.current_layer_tree = shifted_layers.get();
    auto parent_damage = compute_dirty_region(*dom,next_layout.get(),shifted_layout.get(),options);
    check(parent_damage.mode == DirtyRegionMode::DirtyRects,"relative parent shift keeps bounded damage");
    compositor.render_into(*shifted_layers,retained,white,parent_damage.rects.data(),parent_damage.rects.size());
    auto shifted_full = compositor.render(*shifted_layers,320,200,white);
    check(same_pixels(retained,shifted_full),"relative ancestor shift repaints nested absolute descendants");
}

void bounded_work_and_sparse_retention() {
    auto dom=HtmlParser{}.parse("<div id='p'><div id='a'></div><div id='b'></div><div id='c'></div><div id='d'></div></div>");
    StyleResolver resolver(CssParser{}.parse(reset+
        "#p{position:relative;width:200px;height:100px}div{width:20px;height:10px}"
        "#a{position:absolute;left:25%}#b{position:relative;left:-0.25px}"
        "#c{position:absolute;top:calc(-25% + 2px)}#d{position:relative;left:7px}"));
    auto layout=LayoutEngine(resolver).layout(*dom,320,200);
    std::size_t owners=0,boxes=0;
    const auto count=[&](const auto& self,const LayoutBox& box)->void {
        ++boxes; if(!box.style.computed_sizes.empty()) ++owners;
        for(const auto& child:box.children) self(self,*child);
    };
    count(count,*layout);
    check(owners==3,"only percentage, mixed and fractional inset nodes retain sparse payloads");
    LayoutEngineOptions options;
    options.max_layout_boxes=3; options.max_layout_depth=2;
    VectorDiagnosticSink diagnostics; options.diagnostics=&diagnostics;
    auto bounded=LayoutEngine(resolver,{},options).layout(*dom,320,200);
    check(count_layout_boxes(*bounded)<=3,"positioned final pass cannot exceed construction box budget");
    check(!diagnostics.diagnostics().empty(),"bounded scene reports layout resource refusal");
    std::cout << "sparse fixture boxes=" << boxes << " retained-nodes=" << owners << '\n';
}

void nested_opposite_edges_keep_measurement_work_linear() {
    std::size_t measurements_per_node = 0;
    for (unsigned depth : {8U,16U,32U,64U}) {
        std::string html;
        for (unsigned index=0;index<depth;++index) html += "<div class='p'>x";
        for (unsigned index=0;index<depth;++index) html += "</div>";
        auto dom=HtmlParser{}.parse(html);
        StyleResolver resolver(CssParser{}.parse(reset+
            ".p{position:absolute;right:1px;bottom:1px;width:100px;height:100px}"));
        std::size_t measurements = 0;
        TextMeasureProvider provider;
        provider.context = &measurements;
        provider.measure = [](const std::string& text,int,int,TextMetrics* metrics,void* context) {
            ++*static_cast<std::size_t*>(context);
            *metrics = {static_cast<int>(text.size())*5,10};
            return true;
        };
        auto tree=LayoutEngine(resolver,provider).layout(*dom,320,240);
        check(count_layout_boxes(*tree)==2*depth+3,"nested opposite-edge fixture retains every box");
        if (depth==8) measurements_per_node=measurements/depth;
        check(measurements_per_node>0 && measurements==measurements_per_node*depth,
            "opposite-edge remeasurement stays linear across nested out-of-flow children");
        std::cout << "opposite-edge nested boxes=" << depth << " text measurements=" << measurements << '\n';
    }
}

int main() {
    try {
        browser_reference_cases();
        bounded_work_and_sparse_retention();
        nested_opposite_edges_keep_measurement_work_linear();
        moved_boxes_refresh_paint_hit_and_retained_damage();
        signed_edges_and_relative_precedence();
        stretch_constraints_and_nested_containing_blocks();
        resize_mutation_and_initial_containing_block();
        relative_fractional_parent_shift_and_pixel_regressions();
        parsing_preserves_or_rejects_without_viewport_substitution();
        automatic_insets_keep_static_flow_origin();
        std::cout << "inset positioning tests passed; sizeof(Style)=" << sizeof(Style)
            << " sizeof(LayoutBox)=" << sizeof(LayoutBox)
            << " sizeof(LayoutContentGeometry)=" << sizeof(LayoutContentGeometry) << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
