#include "../../src/flex_auto_min.h"
#include "../../src/css_dimension_precision.h"
#include "render_core/css_parser.h"
#include "render_core/html_parser.h"
#include "render_core/layout.h"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace jellyframe;
using namespace jellyframe::detail;
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
const LayoutBox* find(const LayoutBox& b,const char* id){if(b.node&&b.node->attribute("id")==id)return &b;for(const auto& c:b.children)if(auto* p=find(*c,id))return p;return nullptr;}
unsigned policy_cases=0;
void expect(const FlexAutoMinInput& in,std::int64_t expected){auto r=flex_auto_minimum(in);++policy_cases;if(r.status!=FlexAutoMinStatus::Complete || r.minimum_border_box_units!=expected){std::cerr<<"policy expected="<<expected<<" actual="<<r.minimum_border_box_units<<"\n";throw std::runtime_error("automatic minimum policy differs");}}
int main(){try{
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
    FlexAutoMinInput in;
    check(flex_auto_minimum(in).status==FlexAutoMinStatus::Incomplete,"missing content is not measured zero");
    in.content_size_units=0;
    check(flex_auto_minimum(in).status==FlexAutoMinStatus::Incomplete,"incomplete content flag is honored even with zero suggestion");
    in.specified_main_units=0;expect(in,0);
    in.main_decoration_units=4000;expect(in,4000);
    in.specified_main_units=-1;in.max_main_units=4000;expect(in,4000);
    in.max_main_units=-1;in.specified_main_units=5000;
    check(flex_auto_minimum(in).status==FlexAutoMinStatus::Incomplete,"a positive cap does not prove unknown content size");
    in={};in.content_complete=true;in.content_size_units=30250;expect(in,30250);
    in.ratio_main=2;in.ratio_cross=1;in.preferred_cross_units=20125;expect(in,40250);
    in.replaced=true;expect(in,30250);
    in.replaced=false;in.specified_main_units=25125;expect(in,25125);
    in.max_main_units=20750;expect(in,20750);
    in.specified_main_units=in.max_main_units=in.preferred_cross_units=-1;
    in.min_cross_units=18500;expect(in,37000);
    in.min_cross_units=-1;in.max_cross_units=8750;expect(in,17500);
    in.min_cross_units=18500;in.preferred_cross_units=12000;expect(in,37000);
    in={};in.content_complete=true;in.content_size_units=5250;
    in.main_decoration_units=4000;in.cross_decoration_units=6000;
    in.ratio_main=2;in.ratio_cross=1;in.preferred_cross_units=20000;expect(in,32000);
    in.ratio_uses_border_box=true;expect(in,40000);
    in={};in.content_complete=true;in.content_size_units=0;in.ratio_main=1;in.ratio_cross=3;in.preferred_cross_units=1001;expect(in,334);
    in.ratio_main=std::numeric_limits<int>::max();in.ratio_cross=1;in.preferred_cross_units=kDimensionPrecisionMaximum;expect(in,kDimensionPrecisionMaximum);
    in.main_decoration_units=kDimensionPrecisionMaximum;in.cross_decoration_units=kDimensionPrecisionMaximum;expect(in,kDimensionPrecisionMaximum);
    in.content_size_units=kDimensionPrecisionMaximum+1;
    check(flex_auto_minimum(in).status==FlexAutoMinStatus::InvalidInput,"oversized suggestions refuse before proof shortcut");
    in={};in.content_size_units=-2;
    check(flex_auto_minimum(in).status==FlexAutoMinStatus::InvalidInput,"invalid sentinel refuses");
    in={};in.ratio_main=1;
    check(flex_auto_minimum(in).status==FlexAutoMinStatus::InvalidInput,"half-present ratio refuses");
    Style style;
    for(auto axis:{FlexMainAxis::Horizontal,FlexMainAxis::Vertical}) {
        for(const char* overflow:{"","visible","clip"}){style.overflow=overflow;check(flex_auto_min_kind(style,axis)==FlexAutoMinKind::Content,"non-scrollable overflow requests content minimum");}
        for(const char* overflow:{"hidden","auto","scroll"}){style.overflow=overflow;check(flex_auto_min_kind(style,axis)==FlexAutoMinKind::Zero,"scrollable overflow has zero auto minimum");}
    }
    style.overflow="inherit";
    check(flex_auto_min_kind(style,FlexMainAxis::Horizontal)==FlexAutoMinKind::Unresolved,"unknown overflow is not guessed");
    style.overflow.clear();style.min_width=0;
    check(flex_auto_min_kind(style,FlexMainAxis::Horizontal)==FlexAutoMinKind::NotAuto && flex_auto_min_kind(style,FlexMainAxis::Vertical)==FlexAutoMinKind::Content,"explicit zero stays distinct from auto on each axis");
    style.min_width=-1;style.min_height_percent=0;
    check(flex_auto_min_kind(style,FlexMainAxis::Vertical)==FlexAutoMinKind::NotAuto,"unresolved percentage minimum is not auto keyword");
    style.min_height_percent=-1;style.min_width=-2;
    check(flex_auto_min_kind(style,FlexMainAxis::Horizontal)==FlexAutoMinKind::Unresolved,"malformed minimum refuses");
    for(bool column:{false,true})for(const char* overflow:{"hidden","auto","scroll"}) {
        auto dom=HtmlParser{}.parse("<div id='p'><div id='a'><div id='ga'></div></div><div id='b'><div id='gb'></div></div></div>");
        const std::string main=column?"height":"width",cross=column?"width":"height";
        auto css=std::string("*{margin:0;padding:0;border:0}#p{display:flex;align-items:flex-start;flex-direction:")+(column?"column":"row")+";"+main+":100.75px;"+cross+":60.25px}#a,#b{box-sizing:border-box;flex:0 1 80.5px;"+main+":80.5px;"+cross+":30.25px;padding:1px;border:2px solid black;overflow:"+overflow+"}#ga,#gb{width:100%;height:100%}";
        StyleResolver styles(CssParser{}.parse(css));auto tree=LayoutEngine(styles).layout(*dom,320,240);
        auto size=[&](const char* id){auto* b=find(*tree,id);return column?b->rect.height:b->rect.width;};
        check(size("a")==50 && size("b")==51 && size("ga")==44 && size("gb")==45,"zero auto minima allow decorated fractional shrink and descendants in both axes");
        check(find(*tree,"a")->style.min_width==-1 && find(*tree,"a")->style.min_height==-1,"allocation leaves automatic author minima intact");
    }
    for(bool maximum:{false,true}) {
        auto dom=HtmlParser{}.parse("<div id='p'><div id='a'><div id='g'></div></div></div>");
        auto css=std::string("*{margin:0;padding:0;border:0}#p{display:flex;width:50.25px}#a{flex:0 1 80.5px;height:20px;")+(maximum?"max-width:0":"width:0")+"}#g{width:200px;height:10px}";
        StyleResolver styles(CssParser{}.parse(css));auto tree=LayoutEngine(styles).layout(*dom,320,240);
        check(find(*tree,"a")->rect.width==(maximum?0:50) && find(*tree,"g")->rect.width==200,"definite main cap can prove auto minimum without content measurement");
    }
    {
        auto dom=HtmlParser{}.parse("<div id='p'><div id='a'></div></div>");
        StyleResolver styles(CssParser{}.parse("*{margin:0;padding:0;border:0}#p{display:flex;align-items:flex-start;width:25.25px;height:30.25px}#a{flex:0 1 80.5px;height:20.25px;aspect-ratio:2/1}"));
        auto tree=LayoutEngine(styles).layout(*dom,320,240);
        check(find(*tree,"a")->rect.width==41,"empty non-replaced ratio item retains transferred automatic minimum");
    }
    {
        auto dom=HtmlParser{}.parse("<div id='p'><div id='a'></div></div>");
        StyleResolver styles(CssParser{}.parse("*{margin:0;padding:0;border:0}#p{display:flex;flex-direction:column;align-items:flex-start;width:60.25px;height:25.25px}#a{flex:0 1 80.5px;width:80.25px;aspect-ratio:2/1}"));
        auto tree=LayoutEngine(styles).layout(*dom,320,240);
        check(find(*tree,"a")->rect.height==40,"column automatic minimum orients ratio as main over cross");
    }
    std::cout<<policy_cases<<" policy cases, classifier and 10 layout scenes passed; Style bytes="<<sizeof(Style)<<"\n";
#else
    check(flex_auto_minimum({}).status==FlexAutoMinStatus::Unsupported && flex_auto_min_kind({},FlexMainAxis::Horizontal)==FlexAutoMinKind::NotAuto,"cropped policy has no admitted solver");
    std::cout<<"cropped auto-min policy passed\n";
#endif
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
