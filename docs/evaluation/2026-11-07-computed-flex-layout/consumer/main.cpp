#include "render_core/css_parser.h"
#include "render_core/html_parser.h"
#include "render_core/layout.h"
#include <iostream>
using namespace jellyframe;
const LayoutBox* find(const LayoutBox& b,const char* id){if(b.node&&b.node->attribute("id")==id)return &b;for(const auto& c:b.children)if(auto* p=find(*c,id))return p;return nullptr;}
int main(){
    auto dom=HtmlParser{}.parse("<div id='p'><div id='a'></div><div id='b'><div id='g'></div></div></div>");
    StyleResolver styles(CssParser{}.parse("*{margin:0;padding:0;border:0}#p{display:flex;width:100.25px;height:30.25px;align-items:center}#a,#b{flex:1 1 20.25px;width:999.25px;height:10.25px;min-width:0}#a{max-width:25.125px}#g{width:50%;height:100%}"));
    auto tree=LayoutEngine(styles).layout(*dom,320,240);
    const auto* a=find(*tree,"a");const auto* b=find(*tree,"b");const auto* g=find(*tree,"g");
    detail::SizeKind kind;std::int64_t units=0;
    const bool original=b->style.computed_sizes.get(detail::SizeProperty::Width,kind,units)&&units==999250;
    std::cout<<"installed capped Flex widths="<<a->rect.width<<","<<b->rect.width<<"; descendant="<<g->rect.width<<"; original="<<original<<"\n";
    return a->rect.width==25&&b->rect.x==25&&b->rect.width==75&&g->rect.x==25&&g->rect.width==38&&original?0:1;
}
