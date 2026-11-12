#include "render_core/css_parser.h"
#include "render_core/html_parser.h"
#include "render_core/layout.h"
#include "render_core/hit_test.h"
#include "render_core/software_renderer.h"
#include "render_core/prepared_text.h"
#include "render_core/dirty_region.h"
#include "render_core/text_repaint.h"
#include "../../src/shared_inline.h"
#include "../../src/core_text_paragraph.h"
#include "../../src/layout_mutation_journal.h"
#include <iostream>
#include <array>
#include <stdexcept>
#include <tuple>
#include <cstdlib>
#include <new>
static int allocations_until_failure = -1;
void* operator new(std::size_t bytes) {
    if (allocations_until_failure == 0) throw std::bad_alloc();
    if (allocations_until_failure > 0) --allocations_until_failure;
    if (auto* result = std::malloc(bytes ? bytes : 1)) return result;
    throw std::bad_alloc();
}
void operator delete(void* data) noexcept { std::free(data); }
void operator delete(void* data, std::size_t) noexcept { std::free(data); }
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete[](void* data) noexcept { ::operator delete(data); }
void operator delete[](void* data, std::size_t) noexcept { ::operator delete(data); }
using namespace jellyframe;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
using Bytes=std::vector<std::uint8_t>;
void u16(Bytes& b,std::size_t at,unsigned v) { b[at]=v&255;b[at+1]=(v>>8)&255; }
void u32(Bytes& b,std::size_t at,std::uint32_t v) {
    for (unsigned i=0;i<4;++i) b[at+i]=(v>>(8*i))&255;
}
struct Asset { Bytes bytes;FontResource font; };
Asset asset(unsigned weight=400,unsigned size=16,unsigned bits=4,unsigned tag=1,
            std::vector<std::pair<unsigned,unsigned>> glyphs={{32,250},{65,500},{66,600}},
            unsigned fallback=0,unsigned ascent=800) {
    Asset a;
    const auto bitmap=128+glyphs.size()*40;
    a.bytes.resize(bitmap,0);auto& b=a.bytes;
    const char magic[]="JFFONT2";for (unsigned i=0;i<8;++i) b[i]=magic[i];
    u16(b,8,2);u16(b,10,128);u16(b,12,40);u16(b,14,1);
    u32(b,20,static_cast<unsigned>(glyphs.size()));u32(b,24,128);u32(b,28,static_cast<unsigned>(bitmap));
    u16(b,36,1000);u16(b,38,size);u16(b,40,weight);b[42]=static_cast<std::uint8_t>(bits);
    u32(b,44,ascent);u32(b,48,200);u32(b,52,50);u32(b,56,fallback);b[64]=static_cast<std::uint8_t>(tag);
    u32(b,100,1);u16(b,104,1);
    for (std::size_t i=0;i<glyphs.size();++i) {
        auto at=128+i*40;u32(b,at,glyphs[i].first);u32(b,at+4,static_cast<unsigned>(i+1));u32(b,at+8,glyphs[i].second);
        u32(b,at+28,static_cast<unsigned>(b.size()-bitmap));
        if (glyphs[i].first!=32) {
            u32(b,at+12,static_cast<unsigned>(-1));u32(b,at+16,static_cast<unsigned>(-2));
            u16(b,at+20,1);u16(b,at+22,2);u32(b,at+24,1);u32(b,at+32,2);
            b.push_back(static_cast<std::uint8_t>(((1U<<bits)-1)<<(8-bits)));b.push_back(b.back());
        }
    }
    u32(b,16,static_cast<unsigned>(b.size()));u32(b,32,static_cast<unsigned>(b.size()-bitmap));
    std::uint32_t crc=0xffffffffU;
    for (auto v:b) { crc^=v;for (unsigned j=0;j<8;++j) crc=(crc>>1)^((crc&1) ? 0xedb88320U : 0); }
    u32(b,96,~crc);
    check(a.font.load(b.data(),b.size())==FontResourceStatus::Loaded, "font fixture load");return a;
}

const LayoutBox& find(const LayoutBox& box, const char* id) {
    if (box.node && box.node->attribute("id") == id) return box;
    for (const auto& child : box.children) {
        try { return find(*child, id); } catch (const std::out_of_range&) {}
    }
    throw std::out_of_range(id);
}
using Glyph = std::tuple<unsigned, int, int>;
std::vector<Glyph> glyphs(const LayerNode& layer) {
    std::vector<Glyph> result;
    for (const auto& command : layer.display_list) {
        if (command.type() != DisplayCommandType::PreparedText) continue;
        PreparedTextView packet;
        check(decode_prepared_text(command.text, packet), "decode typed packet");
        for (std::size_t i = 0; i < packet.size(); ++i) {
            PreparedTextGlyph glyph;
            packet.read(i, glyph);
            result.emplace_back(glyph.source_codepoint, glyph.x+command.text_paint().origin_x*1000,
                command.text_paint().baseline_y);
        }
    }
    for (const auto& child : layer.children) {
        auto next = glyphs(*child);
        result.insert(result.end(), next.begin(), next.end());
    }
    return result;
}
struct Fixture {
    Asset font = asset();
    TextFontFace face{1, 0, font.font};
    TextSystem system;
    LayoutEngineOptions options;
    LayerTreeBuilderOptions layers;
    Fixture() {
        check(system.bind(&face, 1) == TextSystemStatus::Ok, "bind fixture font");
        options.text_system = &system;
        layers.text_system = &system;
    }
};
bool equal_pixels(const FrameBuffer& a, const FrameBuffer& b) {
    if (a.pixels.size() != b.pixels.size()) return false;
    for (std::size_t i = 0; i < a.pixels.size(); ++i) {
        const auto x=a.pixels[i], y=b.pixels[i];
        if (x.r!=y.r || x.g!=y.g || x.b!=y.b || x.a!=y.a) return false;
    }
    return true;
}
void partition_and_live_proof() {
    Fixture fixture;
    for (const char* align : {"start", "center", "end"}) {
        const auto css = std::string("*{margin:0;padding:0;border:0}div{width:25px;font-size:16px;line-height:20px;text-align:")+align+"}";
        StyleResolver styles(CssParser{}.parse(css));
        auto continuous=HtmlParser{}.parse("<div id='p'><span>AB AB AB</span></div>");
        auto nested=HtmlParser{}.parse("<div id='p'><span id='s'>A<span id='n'>B A</span>B </span><span>AB</span></div>");
        auto a=LayoutEngine(styles,{},fixture.options).layout(*continuous,100,100);
        auto b=LayoutEngine(styles,{},fixture.options).layout(*nested,100,100);
        const auto& host=find(*b,"p");
        check(host.text_layout_cache.shared_inline != nullptr, "normal entrypoint publishes shared owner");
        auto la=LayerTreeBuilder(fixture.layers).build(*a);
        auto lb=LayerTreeBuilder(fixture.layers).build(*b);
        check(glyphs(*la)==glyphs(*lb), "owner boundaries preserve decoded glyph geometry");
        check(glyphs(*lb).size()==6, "all six text glyphs emitted");
        SoftwareCompositor::Options paint;paint.text_system=&fixture.system;
        SoftwareCompositor compositor({},paint);
        const auto before=compositor.render(*lb,100,100,{255,255,255,255});
        check(equal_pixels(compositor.render(*la,100,100,{255,255,255,255}),before), "partition invariant pixels");
        const auto& inner=find(*b,"n");
        const auto binding=inner.text_layout_cache.shared_inline;
        check(binding && detail::shared_inline_current(*binding,&fixture.system), "whole snapshot proof current");
        const auto& member=binding->snapshot->members[binding->member];
        check(member.fragments.size()==2, "nested owner participates in two lines");
        for (const auto& fragment:member.fragments) {
            const auto hit=HitTester{}.hit_test(*b,fragment.x+1,fragment.y+1);
            check(hit && hit.box->node->type==NodeType::Text && hit.node==inner.node, "fragment selects deepest text owner");
        }
        check(!detail::shared_inline_contains(inner,0,1) || member.fragments.front().x==0, "scope union gap excluded");
        auto* text=const_cast<Node*>(inner.children[0]->node);
        text->text="AA";
        check(!detail::shared_inline_current(*binding,&fixture.system), "direct source mutation invalidates whole context");
        auto stale=LayerTreeBuilder(fixture.layers).build(*b);
        check(glyphs(*stale).empty(), "stale live build emits no partial shared leaves");
        check(equal_pixels(before,compositor.render(*lb,100,100,{255,255,255,255})), "old frame remains reproducible after source mutation");
        std::cout << "snapshot_bytes=" << binding->snapshot->retained_bytes << " planner_capacity="
            << binding->snapshot->usage.capacity << " work=" << binding->snapshot->usage.work << '\n';
    }
}
void refusal_and_rollback() {
    Fixture fixture;
    StyleResolver styles(CssParser{}.parse("*{margin:0;padding:0;border:0}div{font-size:16px;width:25px}"));
    auto dom=HtmlParser{}.parse("<div id='p'><span>A</span><span>B</span></div>");
    auto layout=LayoutEngine(styles,{},fixture.options).layout(*dom,100,100);
    auto& host=const_cast<LayoutBox&>(find(*layout,"p"));
    const auto binding=host.text_layout_cache.shared_inline;
    check(binding!=nullptr,"shared before transaction");
    const auto old=host.children[0]->rect;
    detail::LayoutMutationJournal transaction;
    check(transaction.capture(host,128)==detail::LayoutMutationJournalStatus::Ready,"capture shared bindings");
    int height=0;
    check(detail::layout_shared_inline(host,20,30,25,fixture.options,height),"speculative shared paragraph succeeds");
    transaction.rollback();
    check(host.children[0]->rect.x==old.x && host.children[0]->rect.y==old.y &&
        host.text_layout_cache.shared_inline==binding,"journal restores shared owner and geometry");
    auto tiny=fixture.options;tiny.max_prepared_text_glyphs=1;
    check(!detail::layout_shared_inline(host,20,30,25,tiny,height),"glyph limit refuses whole paragraph");
    check(host.text_layout_cache.shared_inline==binding && host.children[0]->rect.x==old.x,"refusal preserves complete prior state");
    const auto& child=*host.children[0];
    auto& changed=const_cast<LayoutBox&>(child);
    changed.style.font_weight=700;
    check(!detail::shared_inline_current(*binding,&fixture.system),"direct style mutation invalidates snapshot");
    changed.style.font_weight=400;
    check(fixture.system.bind(&fixture.face,1)==TextSystemStatus::Ok,"rebind fixture");
    check(!detail::shared_inline_current(*binding,&fixture.system),"font generation invalidates shared paragraph");
}
void mutation_damage_and_tiling() {
    Fixture f;
    auto dom=HtmlParser{}.parse("<div id='p'><span id='a'>AB </span><span>AB AB AB AB</span></div>");
    StyleResolver styles(CssParser{}.parse("*{margin:0;padding:0;border:0}div{font-size:16px;line-height:20px;width:25px;margin-left:7px;margin-top:4px}"));
    auto old=LayoutEngine(styles,{},f.options).layout(*dom,80,100);
    auto old_layers=LayerTreeBuilder(f.layers).build(*old);
    SoftwareCompositor::Options po;po.text_system=&f.system;
    SoftwareCompositor compositor({},po);
    auto old_pixels=compositor.render(*old_layers,80,100,{255,255,255,255});
    const auto& host=find(*old,"p");
    check(detail::shared_inline_current(*host.text_layout_cache.shared_inline,&f.system),"nonzero containing origin keeps proof current");
    auto* text=const_cast<Node*>(find(*old,"a").children[0]->node);
    text->set_text("A ");
    check(!text_dirty_can_reuse_layout(*dom,*old,{}),"shared text mutation requires paragraph layout");
    auto fresh=LayoutEngine(styles,{},f.options).layout(*dom,80,100);
    auto fresh_layers=LayerTreeBuilder(f.layers).build(*fresh);
    check(!glyphs(*fresh_layers).empty(),"fresh layout captures current DOM generation despite dirty flags");
    const auto full=compositor.render(*fresh_layers,80,100,{255,255,255,255});
    DirtyRegionOptions dirty_options;dirty_options.viewport={0,0,80,100};
    dirty_options.previous_layer_tree=old_layers.get();dirty_options.current_layer_tree=fresh_layers.get();
    auto dirty=compute_dirty_region(*dom,old.get(),fresh.get(),dirty_options);
    check(!dirty.rects.empty(),"mutation produces dirty coverage");
    auto replay=old_pixels;
    compositor.render_into(*fresh_layers,replay,{255,255,255,255},dirty.rects.data(),dirty.rects.size());
    check(equal_pixels(full,replay),"old/new paragraph damage replay equals fresh full render");
    check(equal_pixels(old_pixels,compositor.render(*old_layers,80,100,{255,255,255,255})),"old frame replay after marked mutation");
    FrameBuffer tiled(80,100,{255,255,255,255});
    for(int y=0;y<100;y+=7) {
        Rect clip{0,y,80,std::min(7,100-y)};
        compositor.render_into(*fresh_layers,tiled,{255,255,255,255},&clip,1);
    }
    check(equal_pixels(full,tiled),"tiled clips equal full prepared paragraph composition");
}
void partition_matrix_and_events() {
    Fixture f;
    const std::string source="A  B A";
    std::size_t cases=0;
    for (int width : {0,7,25,100}) {
        for (const char* align : {"start","center","end"}) {
            StyleResolver styles(CssParser{}.parse(std::string("*{margin:0;padding:0;border:0}div{font-size:16px;line-height:9px;width:")+
                std::to_string(width)+"px;text-indent:2px;text-align:"+align+"}"));
            auto continuous=HtmlParser{}.parse("<div><span>"+source+"</span></div>");
            auto continuous_layout=LayoutEngine(styles,{},f.options).layout(*continuous,100,100);
            auto continuous_layers=LayerTreeBuilder(f.layers).build(*continuous_layout);
            const auto expected=glyphs(*continuous_layers);
            check(expected.size()>=3 && expected.size()<=5,"whitespace collapses before line emission");
            for (unsigned mask=0;mask<32;++mask) {
                std::string html="<div><span>";
                for(std::size_t i=0;i<source.size();++i) {
                    html+=source[i];
                    if (i+1<source.size() && (mask&(1U<<i))) html+="</span><span>";
                }
                html+="</span></div>";
                auto dom=HtmlParser{}.parse(html);
                auto layout=LayoutEngine(styles,{},f.options).layout(*dom,100,100);
                auto layers=LayerTreeBuilder(f.layers).build(*layout);
                check(glyphs(*layers)==expected,"partition matrix whitespace/indent/width/negative-leading invariance");
                ++cases;
            }
        }
    }
    StyleResolver styles(CssParser{}.parse("*{margin:0;padding:0;border:0}div{font-size:16px;width:100px}"));
    auto dom=HtmlParser{}.parse("<div><span>B</span><span>A</span><wbr><br><span>A</span></div>");
    auto layout=LayoutEngine(styles,{},f.options).layout(*dom,100,100);
    auto layers=LayerTreeBuilder(f.layers).build(*layout);
    const auto values=glyphs(*layers);
    check(values.size()==3 && std::get<1>(values[1])==9600 && std::get<2>(values[2])>std::get<2>(values[1]),
        "v2 preserves fractional cross-owner pen and structural breaks emit no glyph");
    std::cout << "partition_matrix_cases=" << cases << '\n';
}
void scrolling_topology_and_packet_refusal() {
    Fixture f;
    auto dom=HtmlParser{}.parse("<div id='p'><span id='a'>AB AB </span><span>AB AB</span></div>");
    StyleResolver styles(CssParser{}.parse("*{margin:0;padding:0;border:0}div{width:25px;height:20px;overflow:auto;font-size:16px;line-height:20px}"));
    auto layout=LayoutEngine(styles,{},f.options).layout(*dom,80,100);
    const auto& host=find(*layout,"p");
    check(host.text_layout_cache.shared_inline!=nullptr,"scrollable paragraph uses shared owner");
    f.layers.scroll_resolver={[](const Node&,int maximum,void*) {return std::min(20,maximum);},nullptr};
    f.layers.paint_scroll_indicators=false;
    auto layers=LayerTreeBuilder(f.layers).build(*layout);
    const auto find_layer=[&](const auto& self,const LayerNode& layer)->const LayerNode* {
        if (layer.box==&host) return &layer;
        for(const auto& child:layer.children) if(auto* result=self(self,*child)) return result;
        return nullptr;
    };
    const auto* scrolling=find_layer(find_layer,*layers);
    check(scrolling && scrolling->max_scroll_y==60 && scrolling->scroll_y==20,"per-line extents produce scroll range");
    const auto hit=HitTester{}.hit_test(*layers,1,1);
    check(hit && hit.box->node->type==NodeType::Text,"scrolled paragraph hit uses translated line fragments");
    DisplayList painted(0);
    painted.emplace_back();
    const auto marker=painted.front();
    bool complete=false;
    int failed=0;
#if defined(_MSC_VER) && defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL != 0
    // MSVC debug string constructors may allocate iterator proxies inside a
    // noexcept constructor. Injecting global OOM there terminates in the STL,
    // before the emitter can catch anything. Release and sanitizer builds run
    // the complete failure sweep; keep normal emission checked here as well.
    complete=detail::emit_shared_inline(host,painted);
    check(complete && painted.size()>1,"debug ordinary shared paint succeeds");
#else
    for(int allocation=0;allocation<200;++allocation) {
        allocations_until_failure=allocation;
        const bool accepted=detail::emit_shared_inline(host,painted);
        allocations_until_failure=-1;
        if(accepted) {complete=true;break;}
        ++failed;
        check(painted.size()==1 && painted.front().type()==marker.type(),"paint allocation refusal keeps complete old display prefix");
    }
    check(complete && failed>2,"paint transactional packet sweep reaches success");
#endif
    auto low=f.layers;low.max_display_commands=1;
    check(glyphs(*LayerTreeBuilder(low).build(*layout)).empty(),"command budget refuses complete shared paragraph atomically");
    // Replace a same-tag/same-count child and free the old Node. Whole membership
    // proof must fail before it can touch the freed descendant source pointer.
    auto* span=const_cast<Node*>(find(*layout,"a").node);
    auto replacement=std::make_unique<Node>(NodeType::Text);
    replacement->parent=span;
    replacement->text="AB AB ";
    span->children[0]=std::move(replacement);
    check(!detail::shared_inline_current(*host.text_layout_cache.shared_inline,&f.system),"same-count replacement refuses before freed source dereference");
}
void layout_topology_replacement() {
    Fixture f;
    auto dom=HtmlParser{}.parse("<div id='p'><span>A</span><span>B</span></div>");
    StyleResolver styles(CssParser{}.parse("*{margin:0;padding:0;border:0}div{font-size:16px;width:25px}"));
    auto layout=LayoutEngine(styles,{},f.options).layout(*dom,100,100);
    auto& host=const_cast<LayoutBox&>(find(*layout,"p"));
    const auto binding=host.text_layout_cache.shared_inline;
    LayoutBoxPtr replacement(new LayoutBox);
    replacement->node=host.children[0]->node;
    replacement->style=host.children[0]->style;
    host.children[0]=std::move(replacement);
    check(!detail::shared_inline_current(*binding,&f.system),
        "same-count layout child replacement refuses before freed box dereference");
}

void flex_override_and_budget_fallback() {
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
    for (std::size_t attempts : {std::size_t{1}, std::size_t{128}}) {
        Fixture f;
        SharedInlineLayoutStatistics stats;
        f.options.shared_inline.statistics=&stats;
        f.options.shared_inline.max_attempts=attempts;
        auto dom=HtmlParser{}.parse("<div id='flex'><div id='p'><span id='a'>A</span><span>B</span></div></div>");
        StyleResolver styles(CssParser{}.parse(
            "*{margin:0;padding:0;border:0}#flex{display:flex;flex-direction:column;width:40px;height:60px}"
            "#p{font-size:16px;line-height:20px;}"));
        auto layout=LayoutEngine(styles,{},f.options).layout(*dom,100,100);
        const auto& host=find(*layout,"p");
        check(stats.accepted>=1,"unmodified flex probe accepts shared paragraph");
        check(!host.text_layout_cache.shared_inline,"temporary flex sizing retires shared probe");
        const auto& span=find(*layout,"a");
        check(!span.text_layout_cache.shared_inline,"fallback retires descendant bindings");
        auto layers=LayerTreeBuilder(f.layers).build(*layout);
        check(glyphs(*layers).size()==2,"flex stretch and constrained attempts retain actual glyph paint");
        const auto hit=HitTester{}.hit_test(*layers,span.rect.x+1,span.rect.y+1);
        check(hit && hit.node==span.node,"flex fallback keeps live text hit target");
    }
    Fixture f;
    SharedInlineLayoutStatistics stats;
    f.options.shared_inline.statistics=&stats;
    f.options.shared_inline.max_attempts=1;
    auto dom=HtmlParser{}.parse("<div id='flex'><div id='item'><div id='p'><span id='a'>A</span><span>B</span></div></div></div>");
    StyleResolver styles(CssParser{}.parse(
        "*{margin:0;padding:0;border:0}#flex{display:flex;flex-direction:column;width:40px;height:60px}"
        "#item{flex-grow:1}#p{font-size:16px;line-height:20px}"));
    auto layout=LayoutEngine(styles,{},f.options).layout(*dom,100,100);
    check(stats.accepted==1 && stats.refusals>0,"nested final layout exhausts shared attempts after successful probe");
    const auto& host=find(*layout,"p");
    check(!host.text_layout_cache.shared_inline,"budget refusal retires probe snapshot before ordinary layout");
    const auto& span=find(*layout,"a");
    auto layers=LayerTreeBuilder(f.layers).build(*layout);
    check(glyphs(*layers).size()==2,"exhausted nested paragraph still paints glyphs");
    const auto hit=HitTester{}.hit_test(*layers,span.rect.x+1,span.rect.y+1);
    check(hit && hit.node==span.node,"exhausted nested paragraph retains live hit");
#endif
}

void fractional_alignment() {
    Fixture f;
    for (const char* alignment : {"center", "end"}) {
        auto dom=HtmlParser{}.parse("<div id='p'><span>B</span></div>");
        StyleResolver styles(CssParser{}.parse(std::string(
            "*{margin:0;padding:0;border:0}#p{font-size:16px;width:25.5px;text-align:")+alignment+"}"));
        auto layout=LayoutEngine(styles,{},f.options).layout(*dom,100,100);
        auto layers=LayerTreeBuilder(f.layers).build(*layout);
        const auto values=glyphs(*layers);
        const int expected=std::string(alignment)=="center" ? 7950 : 15900;
        check(values.size()==1 && std::get<1>(values[0])==expected,
            "fractional containing width and advance retain exact aligned Q1000 glyph origin");
    }
}

void cumulative_scratch_acquisitions() {
    Fixture fixture;
    using namespace jellyframe::detail;
    const std::string ten(10,'A'), twenty(20,'A');
    const LayoutTextFont font{0,16000};
    const CoreTextParagraphSpan spans[] = {
        {ten,font}, {{},font,-1,CoreTextParagraphSpanKind::Opportunity}, {ten,font},
        {{},font,-1,CoreTextParagraphSpanKind::ForcedBreak},
        {twenty,font}, {{},font,-1,CoreTextParagraphSpanKind::Opportunity}, {twenty,font}
    };
    CoreTextIntrinsicSizes output;
    CoreTextParagraphUsage usage;
    check(measure_core_text_intrinsic(fixture.system,spans,7,{},output,{},&usage)==TextSystemStatus::Ok,
          "opportunities across two growing natural runs must measure");
    // Mirror only the standard library's reserve rounding, not the builder's
    // budget arithmetic. Both replaced scratch allocations must remain charged.
    std::string normalized(0,'\0'); normalized.reserve(63);
    std::string scratch(0,'\0');
    std::size_t scratch_bytes=16;
    scratch.reserve(21); scratch_bytes+=scratch.capacity()+1;
    scratch.reserve(41); scratch_bytes+=scratch.capacity()+1;
    const auto expected=normalized.capacity()+1+63*sizeof(CoreTextParagraphSource)+scratch_bytes;
    check(usage.capacity==expected,"scratch growth retains full superseded acquisitions");
    CoreTextLayoutLimits limits;
    limits.max_capacity_bytes=expected-1;
    output.min_content=123; output.max_content=456;
    check(measure_core_text_intrinsic(fixture.system,spans,7,{},output,limits,&usage)==TextSystemStatus::ResourceLimit &&
          output.min_content==123 && output.max_content==456 && usage.capacity<=limits.max_capacity_bytes,
          "cumulative scratch limit refuses transactionally before final reservation");
}

void limits_and_allocation_failures() {
    Fixture f;
    auto dom=HtmlParser{}.parse("<div id='p'><span>A</span><span>B</span></div>");
    StyleResolver styles(CssParser{}.parse("*{margin:0;padding:0;border:0}div{font-size:16px;width:25px}"));
    SharedInlineLayoutStatistics stats;
    f.options.shared_inline.statistics=&stats;
    auto layout=LayoutEngine(styles,{},f.options).layout(*dom,100,100);
    auto& host=const_cast<LayoutBox&>(find(*layout,"p"));
    check(stats.accepted==1 && stats.acquired_bytes<=f.options.shared_inline.max_total_capacity_bytes,"operation acquisition accounting bounded");
    const auto binding=host.text_layout_cache.shared_inline;
    const auto old=host.children[0]->rect;
    bool success=false;
    int failures=0;
    for(int failure=0;failure<200;++failure) {
        auto options=f.options;
        SharedInlineLayoutStatistics attempt;
        options.shared_inline.statistics=&attempt;
        allocations_until_failure=failure;
        int height=0;
        const bool accepted=detail::layout_shared_inline(host,host.rect.x,host.rect.y,25,options,height);
        allocations_until_failure=-1;
        if(accepted) { success=true;break; }
        ++failures;
        check(host.text_layout_cache.shared_inline==binding && host.children[0]->rect.x==old.x &&
            host.children[0]->rect.y==old.y,"every allocation failure preserves prior snapshot and geometry");
        check(attempt.refusals==1 && attempt.acquired_bytes>0,"failed acquisition remains accounted");
    }
    check(success && failures>10,"allocation failure sweep reaches successful publication");
    auto tiny=f.options;
    tiny.shared_inline.max_total_capacity_bytes=1;
    auto refused=LayoutEngine(styles,{},tiny).layout(*dom,100,100);
    check(!find(*refused,"p").text_layout_cache.shared_inline && stats.refusals>0 && stats.accepted==0,
        "normal entrypoint preserves whole ordinary fallback on operation budget refusal");
    std::cout << "allocation_refusals=" << failures << " sizeof_snapshot=" << sizeof(detail::SharedInlineSnapshot)
        << " sizeof_member=" << sizeof(detail::SharedInlineMember) << '\n';
}

}
int main() {
    try {
        partition_and_live_proof();
        refusal_and_rollback();
        mutation_damage_and_tiling();
        limits_and_allocation_failures();
        cumulative_scratch_acquisitions();
        layout_topology_replacement();
        flex_override_and_budget_fallback();
        fractional_alignment();
        partition_matrix_and_events();
        scrolling_topology_and_packet_refusal();
        std::cout << "sizeof_LayoutBox=" << sizeof(LayoutBox) << " sizeof_TextLayoutCache=" << sizeof(TextLayoutCache) << '\n';
    } catch (const std::exception& e) { std::cerr << e.what() << '\n';return 1; }
}
