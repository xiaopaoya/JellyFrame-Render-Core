#include "flex_precision_cases.h"
#include "render_core/css_parser.h"
#include "render_core/html_parser.h"
#include "render_core/layout.h"
#include "render_core/hit_test.h"
#include "render_core/software_renderer.h"
#include "../../src/flex_length_resolution.h"
#include <iostream>
#include <stdexcept>

using namespace jellyframe;
void check(bool success,const char* message){if(!success)throw std::runtime_error(message);}
const LayoutBox* find(const LayoutBox& box,const std::string& id){
    if(box.node && box.node->attribute("id")==id)return &box;
    for(const auto& child:box.children)if(auto* b=find(*child,id))return b;
    return nullptr;
}
int main(){
    try{
        for(const auto& c:flex_precision_cases::cases){
            auto dom=HtmlParser{}.parse("<div id='p'><div id='i0'><div id='g0'></div></div><div id='i1'><div id='g1'></div></div><div id='i2'><div id='g2'></div></div></div>");
            StyleResolver resolver(CssParser{}.parse(c.css));
            auto layout=LayoutEngine(resolver).layout(*dom,320,240);
            for(unsigned i=0;i<6;++i){
                const auto id=std::string(i%2?"g":"i")+std::to_string(i/2);
                const auto* box=find(*layout,id);const auto* r=c.rects[i];
                if(!box || box->rect.x!=r[0] || box->rect.y!=r[1] || box->rect.width!=r[2] || box->rect.height!=r[3]){
                    std::cerr<<c.id<<" "<<id<<" expected="<<r[0]<<','<<r[1]<<','<<r[2]<<','<<r[3]<<'\n';
                    if(box)std::cerr<<"actual="<<box->rect.x<<','<<box->rect.y<<','<<box->rect.width<<','<<box->rect.height<<'\n';
                    throw std::runtime_error("closed-form Flex allocation differs");
                }
            }
        }
        auto node=make_element("div");StyleResolver resolver(CssParser{}.parse(""));
        for(auto declaration:{"flex-basis:12.25px","flex:1 0 12.25px","flex:1 12.25px"}){
            node->set_attribute("style",declaration);auto s=resolver.resolve(*node);
            detail::SizeKind kind;std::int64_t units=0;
            check(s.computed_sizes.get(detail::SizeProperty::FlexBasis,kind,units)&&units==12250,
                  "basis longhand and shorthand retain the same fractional value");
        }
        node->set_attribute("style","flex-basis:12.25px;flex:1");
        check(resolver.resolve(*node).computed_sizes.empty(),"shorthand reset clears stale basis precision");
        node->set_attribute("style","flex-basis:12.25px;flex-basis:bad");
        check(!resolver.resolve(*node).computed_sizes.empty(),"invalid basis preserves retained value");
        {
            auto dom=HtmlParser{}.parse("<div id='p'><div id='a'></div><div id='b'></div></div>");
            StyleResolver styles(CssParser{}.parse("*{margin:0;padding:0;border:0}#p{display:flex;width:100.25px;height:30.25px}#a{width:20.25px;height:10.25px;margin-left:auto;order:2;background:#ff0000}#b{width:20.25px;height:10.25px;order:1;background:#00ff00}"));
            auto tree=LayoutEngine(styles).layout(*dom,320,240);
            check(find(*tree,"b")->rect.x==0 && find(*tree,"a")->rect.x==80 && find(*tree,"a")->rect.width==20,
                  "source order and main auto margin use exact free space");
            auto layers=LayerTreeBuilder{}.build(*tree);
            auto frame=SoftwareCompositor{}.render(*layers,320,240,Color{255,255,255,255});
            check(frame.pixel(99,2).r==255 && frame.pixel(99,2).g==0 &&
                  HitTester{}.hit_test(*tree,99,2).node==find(*tree,"a")->node,
                  "allocated item paint and hit use the same projected rectangle");
            check(find(*tree,"a")->style.width==20 && find(*tree,"a")->style.margin_left_auto,
                  "allocation leaves authored scalar dimensions and auto flags intact");
        }
        {
            auto dom=HtmlParser{}.parse("<div id='p'><div id='a'></div><div id='b'></div></div>");
            StyleResolver styles(CssParser{}.parse("*{margin:0;padding:0;border:0}#p{display:flex;width:100.25px}#a{width:20.25px;height:50%}#b{width:20.25px;height:20.25px}"));
            auto tree=LayoutEngine(styles).layout(*dom,320,240);
            check(find(*tree,"a")->rect.height==20 && find(*tree,"b")->rect.height==20,
                  "indefinite percentage cross size behaves as auto for stretch");
        }
        {
            auto dom=HtmlParser{}.parse("<div id='p'><div id='a'></div></div>");
            StyleResolver styles(CssParser{}.parse("*{margin:0;padding:0;border:0}#p{display:flex;width:40.25px;height:12px;overflow:hidden}#a{flex:none;width:60.25px;height:10px;background:#ff0000}"));
            auto tree=LayoutEngine(styles).layout(*dom,320,240);
            auto layers=LayerTreeBuilder{}.build(*tree);
            auto frame=SoftwareCompositor{}.render(*layers,320,240,Color{255,255,255,255});
            check(find(*tree,"a")->rect.width==60 && frame.pixel(39,2).g==0 && frame.pixel(40,2).g==255,
                  "fractional allocated overflow clips at the projected parent edge");
            check(HitTester{}.hit_test(*tree,40,2).node!=find(*tree,"a")->node &&
                  HitTester{}.hit_test(*layers,40,2).node!=find(*tree,"a")->node,
                  "layout and layer hit testing honor allocated overflow clipping");
        }
        for(bool column:{false,true}) {
            auto dom=HtmlParser{}.parse("<div id='p'><div id='a'><div id='g'></div></div></div>");
            const auto css=std::string("*{margin:0;padding:0;border:0}#p{display:flex;width:100.25px;height:100.25px;flex-direction:")+
                (column?"column":"row")+"}#a{width:37.5%;height:37.5%;min-width:0;min-height:0;flex-shrink:0}#g{width:50%;height:50%}";
            StyleResolver styles(CssParser{}.parse(css));auto tree=LayoutEngine(styles).layout(*dom,320,240);
            check(find(*tree,"a")->rect.width==38 && find(*tree,"a")->rect.height==38 &&
                  find(*tree,"g")->rect.width==19 && find(*tree,"g")->rect.height==19,
                  "percentage preferred main sizes retain the containing block basis in both directions");
        }
        {
            auto dom=HtmlParser{}.parse("<div id='p'><div id='a'></div><div id='b'></div></div>");
            StyleResolver styles(CssParser{}.parse("*{margin:0;padding:0;border:0}#p{display:flex;width:100.25px;align-items:flex-start}#a{width:40.25px;min-width:0;aspect-ratio:2/1}#b{width:30.25px;height:10.25px}"));
            auto tree=LayoutEngine(styles).layout(*dom,320,240);
            check(find(*tree,"p")->rect.height==20 && find(*tree,"a")->rect.height==20 && find(*tree,"b")->rect.height==10,
                  "auto cross size probes retain the allocated main size for ratio-derived height");
        }
        {
            LayoutBox a,b;
            std::vector<detail::FlexLengthPrecisionItem> items(2);
            for(unsigned i=0;i<2;++i){items[i].child=i?&b:&a;items[i].base=100000;items[i].inner_base=100000;
                items[i].hypothetical=100000;items[i].target=123;}
            items[0].base=-1;
            check(detail::resolve_flex_lengths_precise(items,100000,0,[](std::size_t){return detail::FlexLengthChargeDecision::Continue;})==detail::FlexLengthPrecisionStatus::InvalidInput &&
                  items[0].target==123,"solver refuses invalid inputs before changing targets");
            detail::FlexLengthRange<detail::FlexLengthPrecisionItem> oversized{nullptr,std::numeric_limits<std::size_t>::max()};
            const detail::FlexLengthPrecisionCharge charge{nullptr,[](const void*,std::size_t){return detail::FlexLengthChargeDecision::Continue;}};
            check(detail::resolve_flex_lengths_precise_typed(oversized,100000,0,charge)==
                  detail::FlexLengthPrecisionStatus::CoordinateOverflow,"oversized solver ranges refuse before visiting records");
            items[0].base=100000;
            a.style.flex_shrink=b.style.flex_shrink=std::numeric_limits<int>::max();
            items[0].base=items[0].inner_base=items[0].hypothetical=detail::kDimensionPrecisionMaximum;
            items[1].base=items[1].inner_base=items[1].hypothetical=detail::kDimensionPrecisionMaximum;
            check(detail::resolve_flex_lengths_precise(items,detail::kDimensionPrecisionMaximum,0,[](std::size_t){return detail::FlexLengthChargeDecision::Continue;})==
                  detail::FlexLengthPrecisionStatus::Success && items[0].target+items[1].target==detail::kDimensionPrecisionMaximum,
                  "two-limb shrink weights stay exact beyond 64-bit products");
        }
        {
            using Item=detail::FlexLengthPrecisionItem;
            LayoutBox children[2];
            std::vector<Item> items(2);
            for(std::size_t i=0;i<items.size();++i){
                children[i].style.flex_grow=children[i].style.flex_shrink=1000;
                items[i].child=&children[i];items[i].base=items[i].inner_base=items[i].hypothetical=30000;
                items[i].minimum=0;items[i].maximum=-1;items[i].target=90000+i;
            }
            std::size_t exact_work=0;
            check(detail::resolve_flex_lengths_precise(items,41560,0,[&](std::size_t amount){exact_work+=amount;return detail::FlexLengthChargeDecision::Continue;})==
                detail::FlexLengthPrecisionStatus::Success,"unlimited checked solver caller remains compatible");
            const auto exact_result=items;
            for(std::size_t cancelled_at=0;cancelled_at<exact_work/items.size();++cancelled_at){
                items=exact_result;
                std::size_t charged=0,charge_calls=0;
                std::vector<Item> at_refusal;
                const auto status=detail::resolve_flex_lengths_precise(items,41560,0,[&](std::size_t amount){
                    ++charge_calls;
                    if(charge_calls==cancelled_at+1){at_refusal=items;return detail::FlexLengthChargeDecision::ResourceLimit;}
                    charged+=amount;return detail::FlexLengthChargeDecision::Continue;
                });
                check(status==detail::FlexLengthPrecisionStatus::ResourceLimit && charge_calls==cancelled_at+1 &&
                    items.size()==at_refusal.size() && charged<=exact_work,
                    "cancellation is returned at each solver scan boundary");
                for(std::size_t i=0;i<items.size();++i)
                    check(items[i].target==at_refusal[i].target && items[i].frozen==at_refusal[i].frozen &&
                        items[i].violation==at_refusal[i].violation,"solver performs no mutation after a refused charge");
            }
            items=exact_result;
            std::size_t exact_charged=0;
            check(detail::resolve_flex_lengths_precise(items,41560,0,[&](std::size_t amount){
                if(amount>exact_work-exact_charged)return detail::FlexLengthChargeDecision::ResourceLimit;
                exact_charged+=amount;return detail::FlexLengthChargeDecision::Continue;
            })==detail::FlexLengthPrecisionStatus::Success && exact_charged==exact_work,
                "exact work cap preserves deterministic solver completion");
            for(std::size_t i=0;i<items.size();++i)
                check(items[i].target==exact_result[i].target && items[i].frozen==exact_result[i].frozen &&
                    items[i].violation==exact_result[i].violation,"exact-cap output matches unlimited solver output");
            items=exact_result;
            std::size_t over_charged=0;
            check(detail::resolve_flex_lengths_precise(items,41560,0,[&](std::size_t amount){
                if(amount>exact_work-1-over_charged)return detail::FlexLengthChargeDecision::ResourceLimit;
                over_charged+=amount;return detail::FlexLengthChargeDecision::Continue;
            })==detail::FlexLengthPrecisionStatus::ResourceLimit && over_charged<=exact_work-1,
                "one-unit-short work cap refuses before exceeding its budget");
            check(exact_result[0].target==exact_result[1].target,
                "unlimited solve retains deterministic exact-fit results");
        }
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
    std::cout<<"closed-form Flex geometry and basis tests passed; solver item bytes="<<sizeof(detail::FlexLengthPrecisionItem)<<"\n";
}
