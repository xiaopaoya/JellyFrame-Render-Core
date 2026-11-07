#include "render_core/css_parser.h"
#include "render_core/html_parser.h"
#include "render_core/layout.h"
#include <iostream>
using namespace jellyframe;
const LayoutBox* find(const LayoutBox& b,const char* id){if(b.node&&b.node->attribute("id")==id)return &b;for(const auto& c:b.children)if(auto* p=find(*c,id))return p;return nullptr;}
int main(){
    auto dom=HtmlParser{}.parse("<div id='p'><div id='a'><div id='ga'></div></div><div id='b'><div id='gb'></div></div></div>");
    StyleResolver styles(CssParser{}.parse("*{margin:0;padding:0;border:0}#p{display:flex;align-items:flex-start;width:100.75px;height:60.25px}#a,#b{box-sizing:border-box;flex:0 1 80.5px;width:80.5px;height:30.25px;padding:1px;border:2px solid black;overflow:hidden}#ga,#gb{width:100%;height:100%}"));
    auto tree=LayoutEngine(styles).layout(*dom,320,240);
    if(find(*tree,"a")->rect.width!=50 || find(*tree,"b")->rect.width!=51 || find(*tree,"ga")->rect.width!=44 || find(*tree,"gb")->rect.width!=45 || find(*tree,"a")->style.min_width!=-1)return 1;
    auto ratio_dom=HtmlParser{}.parse("<div id='p'><div id='a'></div></div>");
    StyleResolver ratio(CssParser{}.parse("*{margin:0;padding:0;border:0}#p{display:flex;align-items:flex-start;width:25.25px;height:30.25px}#a{flex:0 1 80.5px;height:20.25px;aspect-ratio:2/1}"));
    auto ratio_tree=LayoutEngine(ratio).layout(*ratio_dom,320,240);
    if(find(*ratio_tree,"a")->rect.width!=41)return 2;
    std::cout<<"installed auto minimum: widths 50/51, content 44/45, ratio minimum 41; Style bytes="<<sizeof(Style)<<"\n";
}
