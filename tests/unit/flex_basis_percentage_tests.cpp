#include "render_core/css_parser.h"
#include "render_core/html_parser.h"
#include "render_core/layout.h"
#include "render_core/style_repaint.h"
#include "../../src/computed_size_geometry.h"
#include "flex_basis_percentage_cases.h"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace jellyframe;
void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
const LayoutBox* find(const LayoutBox& b,const char* id){if(b.node&&b.node->attribute("id")==id)return &b;for(const auto& c:b.children)if(auto* p=find(*c,id))return p;return nullptr;}
bool supports(const std::string& d){return !CssParser{}.parse("@supports ("+d+"){div{height:1px}}").empty();}
int main(){try{
    check(supports("flex-basis:33.333%") == bool(JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED),"percentage support follows feature family");
    check(!supports("flex-basis:calc(50% + 1px)"),"mixed percentage math is not advertised as viewport pixels");
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
    StyleResolver empty(CssParser{}.parse(""));auto node=make_element("div");
    for(const char* d:{"flex-basis:33.333%","flex:0 0 33.333%","flex:0 33.333%","flex:33.333% 0 0","flex:33.333% 0","flex:33.333%"}) {
        check(supports(d),"supports accepts the same valid Flex grammar");
        node->set_attribute("style",d);auto s=empty.resolve(*node);
        check(s.flex_basis_kind==FlexBasisKind::Percentage && s.flex_basis==33333 && s.computed_sizes.empty(),"basis grammar retains Q1000 percentage without heap storage");
    }
    node->set_attribute("style","flex-basis:33.3335%");
    check(empty.resolve(*node).flex_basis==33334,"decimal-half coefficients use shared rounding");
    for(const char* bad:{"-1%","-1px","-.0001px","nan%","inf%","5%garbage","calc(50% + 1px)"}) {
        node->set_attribute("style",std::string("flex-basis:12.25px;flex-basis:")+bad);
        auto s=empty.resolve(*node);detail::SizeKind k;std::int64_t u=0;
        check(s.flex_basis_kind==FlexBasisKind::Length && s.computed_sizes.get(detail::SizeProperty::FlexBasis,k,u) && u==12250,"invalid basis override is atomic");
    }
    for(const char* d:{"flex-basis:0px","flex-basis:auto","flex-basis:initial","flex-basis:unset","flex:1","flex:none","flex:initial","flex:unset"}) {
        node->set_attribute("style",std::string("flex-basis:33.333%;")+d);auto s=empty.resolve(*node);
        check(s.flex_basis_kind==FlexBasisKind::Length && s.computed_sizes.empty(),"length/auto/shorthand reset clears percentage kind");
    }
    node->set_attribute("style","flex:0 0 33.333%;flex:2 20px .5");
    auto invalid=empty.resolve(*node);
    check(invalid.flex_basis_kind==FlexBasisKind::Percentage && invalid.flex_basis==33333 && invalid.flex_grow==0 && invalid.flex_shrink==0,"invalid shorthand preserves kind and all factors");
    node->set_attribute("style","flex-basis:1e20%");auto huge=empty.resolve(*node);
    check(huge.flex_basis==std::numeric_limits<int>::max() && detail::computed_size_units(huge,detail::SizeProperty::FlexBasis,1000)==21474836,"huge coefficients saturate before safe axis arithmetic");
    check(detail::computed_size_units(huge,detail::SizeProperty::FlexBasis,-1)==-1,"indefinite percentages are unresolved");
    for(const char* bad:{"flex:2 20px .5","flex:initial 1","flex-basis:-.0001px","flex-basis:content"})
        check(!supports(bad),"supports rejects invalid or not-yet-implemented Flex syntax");
    for(const auto& c:flex_basis_cases::cases) {
        auto dom=HtmlParser{}.parse("<div id='p'><div id='a'><div id='g'></div></div><div id='b'></div></div>");
        StyleResolver styles(CssParser{}.parse(c.css));auto tree=LayoutEngine(styles).layout(*dom,320,240);
        unsigned i=0;for(const char* id:{"a","g","b"}) {
            auto* b=find(*tree,id);const auto* r=c.rects[i++];
            if(!b || b->rect.x!=r[0] || b->rect.y!=r[1] || b->rect.width!=r[2] || b->rect.height!=r[3]) {
                std::cerr<<c.id<<" "<<id<<" expected="<<r[0]<<","<<r[1]<<","<<r[2]<<","<<r[3]<<"\n";
                if(b)std::cerr<<"actual="<<b->rect.x<<","<<b->rect.y<<","<<b->rect.width<<","<<b->rect.height<<"\n";
                throw std::runtime_error("percentage-basis rational geometry differs");
            }
        }
    }
    // This forces the legacy wrapped path; it must use the container, not viewport.
    {
        auto dom=HtmlParser{}.parse("<div id='p'><div id='a'></div><div id='b'></div></div>");
        StyleResolver styles(CssParser{}.parse("*{margin:0;padding:0;border:0}#p{display:flex;flex-wrap:wrap;width:100px}#a,#b{flex:0 0 60%;height:10px}"));
        auto tree=LayoutEngine(styles).layout(*dom,320,240);
        check(find(*tree,"a")->rect.width==60 && find(*tree,"b")->rect.width==60 && find(*tree,"b")->rect.y==10,"legacy wrap resolves percentage before line placement");
    }
    // In a height:auto column, even 0% means content; 0px stays a definite zero.
    for(const char* percent:{"0%","50%","150%"}) {
        auto dom=HtmlParser{}.parse("<div id='p'><div id='a'><div id='g'></div></div></div>");
        StyleResolver styles(CssParser{}.parse(std::string("*{margin:0;padding:0;border:0}#p{display:flex;flex-direction:column;width:100px}#a{height:80px;flex:0 0 ")+percent+"}#g{height:12px}"));
        auto tree=LayoutEngine(styles).layout(*dom,320,240);
        check(find(*tree,"a")->rect.height==12 && find(*tree,"p")->rect.height==12 && find(*tree,"a")->style.height==80,"indefinite percentage basis uses content, ignoring authored preferred height");
    }
    {
        auto dom=HtmlParser{}.parse("<div id='p'><div id='a'><div id='g'></div></div><div id='b'></div></div>");
        StyleResolver styles(CssParser{}.parse("*{margin:0;padding:0;border:0}#p{display:flex;flex-direction:column;width:100px}#a{height:80px;flex:0 0 0px}#b{height:10px}#g{height:12px}"));
        auto tree=LayoutEngine(styles).layout(*dom,320,240);
        check(find(*tree,"a")->rect.height==0 && find(*tree,"p")->rect.height==10,"definite pixel zero remains different from zero percent in auto column");
    }
    {
        auto dom=HtmlParser{}.parse("<div id='p'><div id='a'><div id='g'></div></div></div>");
        StyleResolver styles(CssParser{}.parse("*{margin:0;padding:0;border:0}#p{display:flex;flex-direction:column;width:100px;height:0}#a{height:80px;min-height:0;flex:0 0 0%}#g{height:12px}"));
        auto tree=LayoutEngine(styles).layout(*dom,320,240);
        check(find(*tree,"a")->rect.height==0 && find(*tree,"p")->rect.height==0,"percentage basis resolves against definite zero instead of probing content");
    }
    {
        auto dom=HtmlParser{}.parse("<div id='p'><div id='a'></div></div>");
        StyleResolver styles(CssParser{}.parse("*{margin:0;padding:0;border:0}#p{display:flex;width:100px}#a{flex:0 0 50%;height:10px}"));
        auto narrow=LayoutEngine(styles).layout(*dom,320,240);auto wide=LayoutEngine(styles).layout(*dom,640,480);
        check(find(*narrow,"a")->rect.width==50 && find(*wide,"a")->rect.width==50,"fixed container percentage basis is independent of viewport dimensions");
    }
    node->set_attribute("style","flex-basis:.0004%");auto tiny=empty.resolve(*node);
    check(tiny.flex_basis_kind==FlexBasisKind::Percentage && tiny.flex_basis==0 &&
          detail::computed_size_units(tiny,detail::SizeProperty::FlexBasis,-1)==-1,"subprecision zero keeps percentage dependency");
    // Equal scalar proxies can mean different bases; layout reuse must see kind.
    node->set_attribute("style","display:flex;width:100px;height:10px;flex-basis:100px");
    auto before=RenderTreeBuilder(empty).build(*node);auto layout=LayoutEngine(empty).layout(*before,320,240);
    node->set_attribute("style","display:flex;width:100px;height:10px;flex-basis:.1%");auto after=RenderTreeBuilder(empty).build(*node);
    check(before->style.flex_basis==after->style.flex_basis && !style_dirty_can_reuse_layout(*node,*before,*after,*layout,{}),"basis kind changes invalidate layout even with equal scalars");
    std::cout<<"48 rational percentage scenes and basis contracts passed; Style bytes="<<sizeof(Style)<<"\n";
#else
    auto n=make_element("div");n->set_attribute("style","flex-basis:50%");
    check(StyleResolver(CssParser{}.parse("")).resolve(*n).flex_basis==-1,"cropped parser does not mutate initial basis");
    std::cout<<"cropped Flex basis admission passed\n";
#endif
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
