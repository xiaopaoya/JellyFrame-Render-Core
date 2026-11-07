#include "render_core/css_parser.h"
#include "render_core/html_parser.h"
#include "render_core/layout.h"
#include <iostream>
using namespace jellyframe;
const LayoutBox* find(const LayoutBox& b,const char* id){if(b.node&&b.node->attribute("id")==id)return &b;for(const auto& c:b.children)if(auto* p=find(*c,id))return p;return nullptr;}
int main(){
    auto dom=HtmlParser{}.parse("<div id='p'><div id='a'><div id='g'></div></div></div>");
    StyleResolver styles(CssParser{}.parse("*{margin:0;padding:0;border:0}#p{display:flex;width:100.25px}#a{width:80px;height:10px;min-width:0;flex:37.5% 0 0}#g{width:50%;height:5px}"));
    auto tree=LayoutEngine(styles).layout(*dom,320,240);
    const auto* a=find(*tree,"a");const auto* g=find(*tree,"g");
    const bool tagged=a->style.flex_basis_kind==FlexBasisKind::Percentage && a->style.flex_basis==37500;
    if(a->rect.width!=38 || g->rect.width!=19 || a->style.width!=80 || !tagged || !a->style.computed_sizes.empty())return 1;
    StyleResolver column(CssParser{}.parse("*{margin:0;padding:0;border:0}#p{display:flex;flex-direction:column;width:100px}#a{height:80px;flex:0 0 0%}#g{height:12px}"));
    auto natural=LayoutEngine(column).layout(*dom,320,240);
    if(find(*natural,"a")->rect.height!=12 || find(*natural,"p")->rect.height!=12)return 2;
    std::cout<<"installed percentage basis: item=38, descendant=19, indefinite column=12; Style bytes="<<sizeof(Style)<<"\n";
}
