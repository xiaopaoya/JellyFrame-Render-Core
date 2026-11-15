#include "render_core/text_system.h"
#include "render_core/prepared_text.h"
#include "render_core/software_renderer.h"
#include "render_core/css_parser.h"
#include "render_core/diagnostics.h"
#include "render_core/html_parser.h"
#include "render_core/layout.h"
#include "render_core/layer_tree.h"
#include "render_core/text_backend.h"
#include "render_core/form_control.h"

#include <array>
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <new>
#include <utility>
#include <vector>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

// This isolated executable verifies no ordinary new/new[] during service calls.
static std::size_t allocations=0;
void* operator new(std::size_t n) {
    ++allocations;
    if (auto* p=std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void operator delete[](void* p,std::size_t) noexcept { std::free(p); }

using namespace jellyframe;
namespace {
using S=TextSystemStatus;
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
    assert(a.font.load(b.data(),b.size())==FontResourceStatus::Loaded);return a;
}
std::uint64_t selected(const TextSystem& system,TextFontRequest request={}) {
    TextRunMetrics m;assert(system.measure("A",request,m)==S::Ok);return m.primary_resource_id;
}
unsigned expected_weight(const std::vector<unsigned>& weights,unsigned wanted) {
    auto has=[&](unsigned w) { for (auto v:weights) if (v==w) return true;return false; };
    if (wanted<400) {
        for (unsigned w=wanted;w>0;--w) if (has(w)) return w;
        for (unsigned w=wanted+1;w<=1000;++w) if (has(w)) return w;
    } else if (wanted>500) {
        for (unsigned w=wanted;w<=1000;++w) if (has(w)) return w;
        for (unsigned w=wanted-1;w>0;--w) if (has(w)) return w;
    } else {
        for (unsigned w=wanted;w<=500;++w) if (has(w)) return w;
        for (unsigned w=wanted-1;w>0;--w) if (has(w)) return w;
        for (unsigned w=501;w<=1000;++w) if (has(w)) return w;
    }
    assert(false);return 0;
}
void weights() {
    const std::array<unsigned,9> available={100,200,300,400,450,500,600,800,900};
    std::array<Asset,9> assets;
    for (std::size_t i=0;i<assets.size();++i) assets[i]=asset(available[i],16,4,static_cast<unsigned>(i+1));
    for (unsigned mask=1;mask<(1U<<available.size());++mask) {
        std::vector<TextFontFace> faces;std::vector<unsigned> present;
        for (std::size_t i=0;i<available.size();++i) if (mask&(1U<<i)) {
            faces.push_back({available[i],0,assets[i].font});present.push_back(available[i]);
        }
        TextSystem system;assert(system.bind(faces.data(),faces.size())==S::Ok);
        for (auto wanted:{1U,100U,299U,399U,400U,401U,450U,499U,500U,501U,650U,999U,1000U}) {
            TextFontRequest request;request.weight=static_cast<std::uint16_t>(wanted);
            assert(selected(system,request)==expected_weight(present,wanted));
        }
    }
}
void strikes_and_families() {
    auto small=asset(400,16,4),large=asset(400,24,4),precise=asset(400,24,8);
    auto named=asset(500,16,4,2);
    std::array<TextFontFace,4> faces={{{1,0,small.font},{2,0,large.font},{3,0,precise.font},
                                      {4,17,named.font,0x12345678U}}};
    TextSystem system;assert(system.bind(faces.data(),faces.size())==S::Ok);
    std::uint32_t family_id=999;
    assert(system.resolve_family_hash(0,family_id) && family_id==0);
    assert(system.resolve_family_hash(0x12345678U,family_id) && family_id==17);
    family_id=999;assert(!system.resolve_family_hash(0x87654321U,family_id) && family_id==999);
    TextFontRequest r;r.size_q1000=20000;assert(selected(system,r)==3); // Larger on size tie, then precision.
    r.size_q1000=19999;assert(selected(system,r)==1);
    r.size_q1000=24000;assert(selected(system,r)==3);
    std::array<std::uint32_t,3> families={99,17,17};r.families=families.data();r.family_count=families.size();
    assert(selected(system,r)==4); // Family priority beats a closer default weight.
    std::array<std::uint32_t,2> defaults_first={0,17};r.families=defaults_first.data();r.family_count=2;
    assert(selected(system,r)==3);
    auto duplicate=faces;duplicate[2].resource_id=1;
    assert(system.bind(duplicate.data(),duplicate.size())==S::InvalidFontSet);assert(selected(system)==1);
    std::array<TextFontFace,5> conflicting_alias={{{1,0,small.font},{2,0,large.font},{3,0,precise.font},
                                                    {4,17,named.font,0x12345678U},
                                                    {5,18,named.font,0x12345678U}}};
    assert(system.bind(conflicting_alias.data(),conflicting_alias.size())==S::InvalidFontSet);
    TextFontFace no_default{8,17,named.font};
    assert(system.bind(&no_default,1)==S::InvalidFontSet);
    auto inconsistent=asset(400,24,4,1,{{65,500}},0,801);
    std::array<TextFontFace,2> wrong={{{1,0,small.font},{9,0,inconsistent.font}}};
    assert(system.bind(wrong.data(),wrong.size())==S::InvalidFontSet);
    auto stable=faces;stable[1].resource=large.font;stable[1].resource_id=10;stable[1].family_id=5;
    assert(system.bind(stable.data(),stable.size())==S::Ok);
    assert(selected(system)==1); // Out-of-order registrations do not create unnamed fallback.
}
void fallback_and_geometry() {
    auto primary=asset(400,16,4,1,{{63,400},{65,500},{66,600}},63);
    auto default_font=asset(400,16,4,2,{{0x4e2d,1000}},0,900);
    std::array<TextFontFace,2> faces={{{11,17,primary.font},{22,0,default_font.font}}};
    TextSystem system;assert(system.bind(faces.data(),faces.size())==S::Ok);
    const std::uint32_t family=17;TextFontRequest r;r.families=&family;r.family_count=1;
    std::array<PreparedTextGlyph,4> output{};TextRunMetrics measured,prepared;
    const auto before=allocations;
    assert(system.measure("A\xe4\xb8\xad" "B",r,measured)==S::Ok);
    assert(system.prepare("A\xe4\xb8\xad" "B",r,output.data(),output.size(),prepared)==S::Ok);
    assert(allocations==before);
    assert(prepared.primary_resource_id==11 && prepared.glyph_count==3 && prepared.substitutions==0);
    assert(prepared.advance==33600 && prepared.advance==measured.advance);
    assert(prepared.primary_font.ascent==12800 && prepared.ascent==14400 && prepared.descent==3200);
    assert(prepared.natural_height==18400 && prepared.has_ink);
    assert(prepared.ink_left==-1000 && prepared.ink_right==24000 && prepared.ink_top==-2000 && prepared.ink_bottom==0);
    assert(output[0].resource_id==11 && output[1].resource_id==22 && output[2].resource_id==11);
    assert(output[0].source_begin==0 && output[0].source_end==1 && output[0].x==0);
    assert(output[1].source_begin==1 && output[1].source_end==4 && output[1].x==8000);
    assert(output[2].source_begin==4 && output[2].source_end==5 && output[2].x==24000);
    assert(system.prepare("Z",r,output.data(),output.size(),prepared)==S::Ok);
    assert(prepared.substitutions==1 && output[0].codepoint=='?' && output[0].source_codepoint=='Z');
    assert(output[0].resource_id==11 && prepared.advance==6400);
    // A nonselected weight in the first family cannot replace real default coverage.
    auto heavier=asset(500,16,4,3,{{0x4e2d,700}});
    std::array<TextFontFace,3> more={{{11,17,primary.font},{33,17,heavier.font},{22,0,default_font.font}}};
    assert(system.bind(more.data(),more.size())==S::Ok);
    assert(system.prepare("\xe4\xb8\xad",r,output.data(),output.size(),prepared)==S::Ok);
    assert(output[0].resource_id==22);
}
void refusals_and_budgets() {
    auto a=asset();TextFontFace face{1,0,a.font};TextSystem system;TextRunMetrics m;TextFontRequest r;
    std::array<PreparedTextGlyph,4> out{};for (auto& g:out) g.resource_id=999;
    assert(system.measure("A",r,m)==S::NotReady);
    assert(system.bind(&face,1)==S::Ok);
    auto reject=[&](std::string_view text,S wanted) {
        m.advance=123;assert(system.prepare(text,r,out.data(),out.size(),m)==wanted);
        assert(m.advance==0 && m.glyph_count==0 && !m.primary_resource_id);
        for (const auto& g:out) assert(g.resource_id==999);
    };
    reject("ABZ",S::MissingGlyph);
    for (auto invalid:{"\xc0\x80","A\xed\xa0\x80","A\xf4\x90\x80\x80","A\xe4\xb8"}) reject(invalid,S::InvalidUtf8);
    for (auto unsupported:{"A\xcc\x81","A\xe2\x80\x8d","\xd8\xa7","\xf0\x9f\x98\x80","A\xe3\x80\xaa","A\xef\xb8\x8f","A\t","A\n"})
        reject(unsupported,S::UnsupportedText);
    reject(std::string_view("A\0B",3),S::UnsupportedText);
    r.style=TextFaceStyle::Italic;reject("A",S::UnsupportedStyle);r.style=TextFaceStyle::Normal;
    r.size_q1000=-1;reject("A",S::InvalidRequest);r.size_q1000=16000;
    r.weight=0;reject("A",S::InvalidRequest);r.weight=1001;reject("A",S::InvalidRequest);r.weight=400;
    r.family_count=1;reject("A",S::InvalidRequest);r.family_count=0;
    assert(system.prepare("AB",r,out.data(),1,m)==S::OutputTooSmall);
    assert(system.prepare("A",r,nullptr,4,m)==S::OutputTooSmall);
    assert(system.prepare("",r,nullptr,0,m)==S::Ok && m.primary_resource_id==1 && !m.glyph_count);
    assert(system.prepare(" ",r,out.data(),out.size(),m)==S::Ok && !m.has_ink && m.advance==4000);
    for (auto& g:out) g.resource_id=999;
    r.size_q1000=std::numeric_limits<std::int32_t>::max();reject("AA",S::Overflow);r.size_q1000=16000;
    TextSystemLimits limits;limits.max_glyphs=2;assert(system.bind(&face,1,limits)==S::Ok);reject("AAA",S::ResourceLimit);
    limits={};limits.max_text_bytes=2;assert(system.bind(&face,1,limits)==S::Ok);reject("AAA",S::ResourceLimit);
    limits={};limits.max_families=0;assert(system.bind(&face,1,limits)==S::Ok);
    const std::uint32_t family=0;r.families=&family;r.family_count=1;reject("A",S::ResourceLimit);r={};
    limits={};limits.max_work=10;assert(system.bind(&face,1,limits)==S::Ok);
    assert(system.measure("A",r,m)==S::Ok);reject("A",S::ResourceLimit); // Two-pass cost is reserved before writing.
    limits={};limits.max_work=0;assert(system.bind(&face,1,limits)==S::ResourceLimit);
    assert(system.measure("A",r,m)==S::Ok); // Failed rebind preserved previous budget/set.
    assert(system.bind(&face,65)==S::ResourceLimit);
    assert(system.bind(nullptr,0)==S::InvalidFontSet);
    assert(system.bind(&face,1)==S::Ok);r.size_q1000=0;
    assert(system.prepare("A",r,out.data(),out.size(),m)==S::Ok && !m.has_ink && !m.advance);
    auto zero=asset(400,16,4,4,{{65,0}});TextFontFace zero_face{7,0,zero.font};
    assert(system.bind(&zero_face,1)==S::Ok);r={};
    assert(system.prepare("AA",r,out.data(),out.size(),m)==S::Ok && m.has_ink && !m.advance && out[1].x==0);
    system.clear();assert(!system.ready() && system.measure("A",r,m)==S::NotReady);
}
void prepared_packet_paint() {
    auto a=asset();TextFontFace face{0x123400000056ULL,0,a.font};TextSystem system;
    assert(system.bind(&face,1)==S::Ok);
    std::array<PreparedTextGlyph,4> glyphs{};TextRunMetrics metrics;TextFontRequest request;
    assert(system.prepare("AB",request,glyphs.data(),glyphs.size(),metrics)==S::Ok);
    std::string packet;
    assert(encode_prepared_text(glyphs.data(),metrics.glyph_count,request.size_q1000,2,packet));
    PreparedTextView view;
    assert(decode_prepared_text(packet,view) && view.size()==2 && view.source_bytes()==2);

    const Color background{39,59,83,117},ink{213,91,37,255};
    FrameBuffer full(32,20,background);
    const auto before=allocations;
    assert(system.paint(view,full,{0,0,32,20},7000,12000,ink)==S::Ok);
    assert(allocations==before);
    assert(full.pixel(6,10).r==ink.r && full.pixel(6,10).g==ink.g && full.pixel(6,10).b==ink.b);
    assert(full.pixel(14,10).r==ink.r && full.pixel(14,10).g==ink.g && full.pixel(14,10).b==ink.b);
    assert(full.pixel(0,0).r==background.r && full.pixel(0,0).a==background.a);

    FrameBuffer tiled(32,20,background);
    for (int y=0;y<20;y+=3) for (int x=0;x<32;x+=5)
        assert(system.paint(view,tiled,{x,y,5,3},7000,12000,ink)==S::Ok);
    assert(tiled.pixels.size()==full.pixels.size());
    for (std::size_t i=0;i<full.pixels.size();++i) {
        const auto left=full.pixels[i],right=tiled.pixels[i];
        assert(left.r==right.r && left.g==right.g && left.b==right.b && left.a==right.a);
    }

    FrameBuffer unchanged(32,20,background);
    const auto reject_without_write=[&](TextPaintLimits limits) {
        assert(system.paint(view,unchanged,{0,0,32,20},7000,12000,ink,limits)==TextSystemStatus::ResourceLimit);
        for (const auto& pixel:unchanged.pixels)
            assert(pixel.r==background.r && pixel.g==background.g && pixel.b==background.b && pixel.a==background.a);
    };
    TextPaintLimits limits;limits.max_pixels=1;reject_without_write(limits);
    limits={};limits.max_work=1;reject_without_write(limits);
    auto malformed=packet;malformed[4]=4;PreparedTextView preserved=view;
    assert(!decode_prepared_text(malformed,preserved) && preserved.size()==view.size());
    assert(system.paint(preserved,unchanged,{0,0,32,20},7000,12000,ink)==TextSystemStatus::Ok);

    DisplayCommand command;
    command.set_type(DisplayCommandType::PreparedText);command.rect={0,0,32,20};
    command.text=packet;command.color=ink;
    set_prepared_text_origin(command,0,0);
    SoftwareRasterizerOptions raster_options;raster_options.text_system=&system;
    FrameBuffer rendered(32,20,background);
    SoftwareRasterizer({},nullptr,raster_options).rasterize(command,rendered,{0,0,32,20});
    FrameBuffer reference(32,20,background);
    assert(system.paint(view,reference,{0,0,32,20},0,0,ink)==TextSystemStatus::Ok);
    assert(rendered.pixels.size()==reference.pixels.size());
    for (std::size_t i=0;i<reference.pixels.size();++i) {
        const auto left=reference.pixels[i],right=rendered.pixels[i];
        assert(left.r==right.r && left.g==right.g && left.b==right.b && left.a==right.a);
    }
    LayerNode root;root.bounds={0,0,32,20};root.display_list.push_back(command);
    SoftwareCompositor::Options compositor_options;compositor_options.text_system=&system;
    const auto composited=SoftwareCompositor(ImagePainter{},compositor_options).render(root,32,20,background);
    for (std::size_t i=0;i<reference.pixels.size();++i) {
        const auto left=reference.pixels[i],right=composited.pixels[i];
        assert(left.r==right.r && left.g==right.g && left.b==right.b && left.a==right.a);
    }
}
const LayoutBox* find_text_box(const LayoutBox& box);
void offset_prepared_packets() {
    auto a=asset();TextFontFace face{1,0,a.font};TextSystem system;
    assert(system.bind(&face,1)==S::Ok);
    std::array<PreparedTextGlyph,2> glyphs;TextRunMetrics metrics;
    assert(system.prepare("BA",{},glyphs.data(),glyphs.size(),metrics)==S::Ok);
    std::string packet;
    auto shifted=glyphs;
    for(auto& glyph:shifted) glyph.x+=1500;
    assert(encode_prepared_text(shifted.data(),shifted.size(),16000,2,packet,1500));
    assert(packet[4]==2);
    PreparedTextView offset;
    assert(decode_prepared_text(packet,offset));
    FrameBuffer actual(40,30,{255,255,255,255}),expected=actual;
    assert(system.paint(offset,actual,{0,0,40,30},7000,12000,{0,0,0,255})==S::Ok);
    assert(encode_prepared_text(glyphs.data(),glyphs.size(),16000,2,packet));
    PreparedTextView zero;
    assert(packet[4]==1 && decode_prepared_text(packet,zero));
    assert(system.paint(zero,expected,{0,0,40,30},8500,12000,{0,0,0,255})==S::Ok);
    for(std::size_t i=0;i<actual.pixels.size();++i) assert(actual.pixels[i].r==expected.pixels[i].r);
    auto malformed=packet;malformed[24]=1;
    assert(!decode_prepared_text(malformed,zero));
    TextLayoutCache cache;
    TextLayoutCache copied=cache;
    assert(!copied.shared_inline);
}
void installed_shared_paragraph_path() {
    auto font=asset(); TextFontFace face{1,0,font.font}; TextSystem system;
    assert(system.bind(&face,1)==S::Ok);
    StyleResolver styles(CssParser{}.parse(
        "*{margin:0;padding:0;border:0}div{width:25px;font-size:16px;line-height:20px;text-align:center}"));
    auto plain=HtmlParser{}.parse("<div><span>AB AB AB</span></div>");
    auto split=HtmlParser{}.parse("<div><span>A<span>B A</span>B </span><span>AB</span></div>");
    LayoutEngineOptions options; options.text_system=&system;
    auto a=LayoutEngine(styles, options).layout(*plain,80,80);
    auto b=LayoutEngine(styles, options).layout(*split,80,80);
    TextLayoutCache copied;
    bool has_binding=false;
    const auto bindings=[&](const auto& self,const LayoutBox& box)->void {
        if(box.text_layout_cache.shared_inline) {copied=box.text_layout_cache;has_binding=true;}
        for(const auto& child:box.children) self(self,*child);
    };
    bindings(bindings,*b);
    assert(has_binding && copied.shared_inline);
    LayerTreeBuilderOptions layers; layers.text_system=&system;
    auto la=LayerTreeBuilder(layers).build(*a), lb=LayerTreeBuilder(layers).build(*b);
    bool has_v2=false;
    const auto packets=[&](const auto& self,const LayerNode& layer)->void {
        for(const auto& command:layer.display_list)
            if(command.type()==DisplayCommandType::PreparedText && command.text.size()>4 && command.text[4]==2)
                has_v2=true;
        for(const auto& child:layer.children) self(self,*child);
    };
    packets(packets,*lb); assert(has_v2);
    SoftwareCompositor::Options paint; paint.text_system=&system;
    SoftwareCompositor compositor(ImagePainter{},paint);
    const auto expected=compositor.render(*la,80,80,{255,255,255,255});
    const auto actual=compositor.render(*lb,80,80,{255,255,255,255});
    assert(expected.pixels.size()==actual.pixels.size());
    for(std::size_t i=0;i<actual.pixels.size();++i) {
        const auto x=expected.pixels[i],y=actual.pixels[i];
        assert(x.r==y.r && x.g==y.g && x.b==y.b && x.a==y.a);
    }
    b.reset(); split.reset();
    assert(copied.shared_inline); // Destruction needs only the installed incomplete type.
}
void html_layout_to_core_paint() {
    auto a=asset();
    const auto family_hash=normalized_font_family_hash("Core Test");
    std::array<TextFontFace,2> faces={{{101,17,a.font,family_hash},{102,0,a.font,0}}};
    TextSystem system;
    assert(system.bind(faces.data(),faces.size())==S::Ok);
    HtmlParser html;CssParser css;
    auto document=html.parse("<body><div>AB</div></body>");
    StyleResolver resolver(css.parse("div { font-family: 'Core Test'; font-size: 16px; white-space: nowrap; }"));
    LayoutEngineOptions layout_options;layout_options.text_system=&system;
    auto layout=LayoutEngine(resolver, layout_options).layout(*document,80,40);
    const LayoutBox* text_box=nullptr;
    const auto find_text=[&](const auto& self,const LayoutBox& box)->void {
        if (box.node && box.node->type==NodeType::Text) { text_box=&box;return; }
        for (const auto& child:box.children) if (!text_box) self(self,*child);
    };
    find_text(find_text,*layout);
    assert(text_box && text_box->text_layout_cache.prepared_text);
    assert(text_box->text_layout_cache.prepared_packet.substr(0,4)=="JFTP");
    assert(text_box->text_layout_cache.prepared_origin_x==text_box->rect.x);
    assert(text_box->text_layout_cache.prepared_baseline_y>text_box->rect.y);

    LayerTreeBuilderOptions layer_options;layer_options.text_system=&system;
    auto layers=LayerTreeBuilder(layer_options).build(*layout);
    bool found_prepared=false;
    for (const auto& command:layers->display_list)
        found_prepared|=command.type()==DisplayCommandType::PreparedText;
    assert(found_prepared);
    SoftwareCompositor::Options compositor_options;compositor_options.text_system=&system;
    const auto frame=SoftwareCompositor(ImagePainter{},compositor_options).render(*layers,80,40,Color{255,255,255,255});
    std::size_t ink=0;
    for (const auto pixel:frame.pixels) ink+=pixel.r<255 || pixel.g<255 || pixel.b<255;
    assert(ink>0);

    auto positioned_document=html.parse("<body><div>AB</div></body>");
    StyleResolver positioned_resolver(css.parse(
        "div { position: absolute; left: 20px; top: 10px; font-family: 'Core Test'; white-space: nowrap; }"));
    auto positioned_layout=LayoutEngine(positioned_resolver, layout_options).layout(*positioned_document,80,40);
    const LayoutBox* positioned_text=find_text_box(*positioned_layout);
    assert(positioned_text && positioned_text->text_layout_cache.prepared_text);
    assert(positioned_text->rect.x>=20 && positioned_text->rect.y>=10);
    assert(positioned_text->text_layout_cache.prepared_origin_x==positioned_text->rect.x);
    LayerTreeBuilder positioned_builder(layer_options);
    auto positioned_layers=positioned_builder.build(*positioned_layout);
    bool found_positioned_text=false;
    const auto check_positioned_commands=[&](const auto& self,const LayerNode& layer)->void {
        for (const auto& command:layer.display_list) {
            if (command.type()!=DisplayCommandType::PreparedText) continue;
            found_positioned_text=true;
            assert(prepared_text_origin_x(command)==positioned_text->rect.x);
            assert(prepared_text_baseline_y(command)==positioned_text->text_layout_cache.prepared_baseline_y);
        }
        for (const auto& child:layer.children) self(self,*child);
    };
    check_positioned_commands(check_positioned_commands,*positioned_layers);
    assert(found_positioned_text);

    auto wrapped_document=html.parse("<body><div>A B</div></body>");
    StyleResolver wrapped_resolver(css.parse(
        "div { width: 10px; font-family: 'Core Test'; font-size: 16px; }"));
    auto wrapped_layout=LayoutEngine(wrapped_resolver, layout_options).layout(*wrapped_document,80,40);
    const LayoutBox* wrapped_text=find_text_box(*wrapped_layout);
    assert(wrapped_text && wrapped_text->text_layout_cache.prepared_text);
    assert((wrapped_text->text_layout_cache.lines==std::vector<std::string>{"A","B"}));
    assert(wrapped_text->text_layout_cache.prepared_lines.size()==2);
    auto wrapped_layers=LayerTreeBuilder(layer_options).build(*wrapped_layout);
    std::size_t wrapped_commands=0;
    const auto count_wrapped_commands=[&](const auto& self,const LayerNode& layer)->void {
        for (const auto& command:layer.display_list)
            wrapped_commands+=command.type()==DisplayCommandType::PreparedText;
        for (const auto& child:layer.children) self(self,*child);
    };
    count_wrapped_commands(count_wrapped_commands,*wrapped_layers);
    assert(wrapped_commands==2);

    auto anywhere_document=html.parse("<body><div>AB</div></body>");
    StyleResolver anywhere_resolver(css.parse(
        "div { width: 4px; font-family: 'Core Test'; overflow-wrap: anywhere; }"));
    auto anywhere_layout=LayoutEngine(anywhere_resolver, layout_options).layout(*anywhere_document,80,40);
    const LayoutBox* anywhere_text=find_text_box(*anywhere_layout);
    assert(anywhere_text && anywhere_text->text_layout_cache.prepared_text);
    assert((anywhere_text->text_layout_cache.lines==std::vector<std::string>{"A","B"}));

    const auto cached_generation=wrapped_text->text_layout_cache.text_system_generation;
    assert(system.bind(faces.data(),faces.size())==S::Ok);
    assert(system.generation()>cached_generation);
    auto stale_layers=LayerTreeBuilder(layer_options).build(*wrapped_layout);
    std::size_t stale_prepared_commands=0;
    const auto count_stale_commands=[&](const auto& self,const LayerNode& layer)->void {
        for (const auto& command:layer.display_list)
            stale_prepared_commands+=command.type()==DisplayCommandType::PreparedText;
        for (const auto& child:layer.children) self(self,*child);
    };
    count_stale_commands(count_stale_commands,*stale_layers);
    assert(stale_prepared_commands==0);
}
void route_relayout_keeps_prepared_text_while_dom_is_dirty() {
    auto font=asset();
    TextFontFace face{1,0,font.font};
    TextSystem system;
    assert(system.bind(&face,1)==S::Ok);

    HtmlParser html;
    CssParser css;
    auto document=html.parse(
        "<body><section id='home' class='active'><p>AB</p></section>"
        "<section id='activity'><p>BA</p></section></body>");
    StyleResolver resolver(css.parse(
        "body { margin: 0; } section { display: none; } "
        "section.active { display: block; } "
        "p { margin: 0; font-size: 16px; white-space: nowrap; }"));
    LayoutEngineOptions layout_options;
    layout_options.text_system=&system;
    LayerTreeBuilderOptions layer_options;
    layer_options.text_system=&system;
    clear_dirty_flags(*document);

    const auto build_frame=[&]() {
        auto render=RenderTreeBuilder(resolver).build(*document);
        auto layout=LayoutEngine(resolver, layout_options).layout(*render,80,40);
        auto layers=LayerTreeBuilder(layer_options).build(*layout);
        return std::pair{std::move(layout),std::move(layers)};
    };
    const auto count_prepared=[&](const auto& self,const LayerNode& layer)->std::size_t {
        std::size_t count=0;
        for (const auto& command:layer.display_list)
            count+=command.type()==DisplayCommandType::PreparedText;
        for (const auto& child:layer.children) count+=self(self,*child);
        return count;
    };

    auto [initial_layout,initial_layers]=build_frame();
    assert(count_prepared(count_prepared,*initial_layers)==1);
    clear_dirty_flags(*document);

    Node* home=nullptr;
    Node* activity=nullptr;
    const auto find_views=[&](const auto& self,Node& node)->void {
        if (node.attribute("id")=="home") home=&node;
        if (node.attribute("id")=="activity") activity=&node;
        for (auto& child:node.children) self(self,*child);
    };
    find_views(find_views,*document);
    assert(home && activity);
    home->set_attribute("class","inactive");
    activity->set_attribute("class","active");

    auto [routed_layout,routed_layers]=build_frame();
    assert((subtree_dirty_flags(*document)&(DomDirtyStyle|DomDirtyLayout|DomDirtyAttributes))!=0U);
    assert(count_prepared(count_prepared,*routed_layers)==1);
    const Node* activity_text=activity->children.front()->children.front().get();
    const auto find_layout_box=[&](const auto& self,const LayoutBox& box)->const LayoutBox* {
        if (box.node==activity_text) return &box;
        for (const auto& child:box.children) {
            if (const LayoutBox* found=self(self,*child)) return found;
        }
        return nullptr;
    };
    const LayoutBox* active_text_box=find_layout_box(find_layout_box,*routed_layout);
    assert(active_text_box!=nullptr);
    assert(active_text_box->text_layout_cache.valid);
    assert(active_text_box->text_layout_cache.prepared_text);
    assert(active_text_box->text_layout_cache.source_text=="BA");
}
void unrelated_input_mutation_preserves_static_prepared_text() {
    auto font=asset();
    TextFontFace face{1,0,font.font};
    TextSystem system;
    assert(system.bind(&face,1)==S::Ok);

    auto document=HtmlParser().parse(
        "<body><input id='label' value='AB'><p>AB</p></body>");
    StyleResolver resolver(CssParser().parse(
        "body { margin: 0; } p { margin: 0; font-size: 16px; white-space: nowrap; }"));
    LayoutEngineOptions layout_options;
    layout_options.text_system=&system;
    auto layout=LayoutEngine(resolver, layout_options).layout(*document,80,40);
    const LayoutBox* text=find_text_box(*layout);
    assert(text!=nullptr && text->text_layout_cache.prepared_text);

    Node* input=nullptr;
    const auto find_input=[&](const auto& self,Node& node)->void {
        if (node.attribute("id")=="label") input=&node;
        for (auto& child:node.children) self(self,*child);
    };
    find_input(find_input,*document);
    assert(input!=nullptr);
    input->set_attribute("value","CDE");

    LayerTreeBuilderOptions layer_options;
    layer_options.text_system=&system;
    auto layers=LayerTreeBuilder(layer_options).build(*layout);
    std::size_t prepared_commands=0;
    const auto count_prepared=[&](const auto& self,const LayerNode& layer)->void {
        for (const auto& command:layer.display_list)
            prepared_commands+=command.type()==DisplayCommandType::PreparedText;
        for (const auto& child:layer.children) self(self,*child);
    };
    count_prepared(count_prepared,*layers);
    assert(prepared_commands==1);
}
const LayoutBox* find_text_box(const LayoutBox& box) {
    if (box.node && box.node->type==NodeType::Text) return &box;
    for (const auto& child:box.children) {
        if (const auto* found=find_text_box(*child)) return found;
    }
    return nullptr;
}
void aligned_prepared_text_stays_inside_its_layout_box() {
    auto font = asset();
    TextFontFace face{101, 0, font.font};
    TextSystem system;
    assert(system.bind(&face, 1) == S::Ok);
    LayoutEngineOptions layout_options;
    layout_options.text_system = &system;
    LayerTreeBuilderOptions layer_options;
    layer_options.text_system = &system;
    SoftwareCompositor::Options compositor_options;
    compositor_options.text_system = &system;

    for (const std::string tag : {"div", "button"}) {
        for (const std::string alignment : {"left", "center", "right"}) {
            for (const bool wrapped : {false, true}) {
                const int width = wrapped ? 32 : 80;
                const std::string text = wrapped ? "AB ABA" : "AB";
                auto document = HtmlParser().parse("<body><" + tag + ">" + text +
                    "</" + tag + "></body>");
                StyleResolver resolver(CssParser().parse(
                    "body { margin: 0; } " + tag + " { margin: 0; padding: 0; border: 0; "
                    "font-size: 16px; line-height: 20px; width: " + std::to_string(width) +
                    "px; text-align: " + alignment + "; color: #000000; background: #ffffff; }"));
                auto layout = LayoutEngine(resolver, layout_options).layout(*document, 120, 80);
                const LayoutBox* text_box = find_text_box(*layout);
                assert(text_box != nullptr);
                const auto& cache = text_box->text_layout_cache;
                assert(cache.valid && cache.prepared_text);
                assert(cache.prepared_lines.size() == (wrapped ? 2U : 1U));
                assert(text_box->rect.width == (wrapped ? 32 : 18));
                const int outer_offset = alignment == "center" ? (width - text_box->rect.width) / 2
                    : alignment == "right" ? width - text_box->rect.width : 0;
                assert(text_box->rect.x == outer_offset);
                for (const auto& line : cache.prepared_lines) {
                    const int inner_offset = alignment == "center" ? (text_box->rect.width - line.width) / 2
                        : alignment == "right" ? text_box->rect.width - line.width : 0;
                    assert(line.origin_x == text_box->rect.x + inner_offset);
                }
                auto layers = LayerTreeBuilder(layer_options).build(*layout);
                auto flattened = LayerTreeBuilder(layer_options).flatten(*layers);
                std::size_t line_index = 0;
                for (const auto& command : flattened) {
                    if (command.type() != DisplayCommandType::PreparedText) continue;
                    assert(line_index < cache.prepared_lines.size());
                    assert(command.rect.x == text_box->rect.x && command.rect.y == text_box->rect.y);
                    assert(command.rect.width == text_box->rect.width && command.rect.height == text_box->rect.height);
                    assert(prepared_text_origin_x(command) == cache.prepared_lines[line_index].origin_x);
                    assert(prepared_text_baseline_y(command) == cache.prepared_lines[line_index].baseline_y);
                    ++line_index;
                }
                assert(line_index == cache.prepared_lines.size());
                const auto frame = SoftwareCompositor(ImagePainter{}, compositor_options).render(
                    *layers, 120, 80, Color{255, 255, 255, 255});
                for (const auto& line : cache.prepared_lines) {
                    std::size_t ink = 0;
                    for (int y = line.baseline_y - 2; y < line.baseline_y; ++y) {
                        for (int x = text_box->rect.x; x < text_box->rect.x + text_box->rect.width; ++x) {
                            ink += frame.pixel(x, y).r < 255;
                        }
                    }
                    assert(ink > 0);
                }
            }
        }
    }
}
void core_native_control_prepared_labels() {
    std::vector<std::pair<unsigned,unsigned>> coverage;
    for(unsigned cp=32;cp<127;++cp) coverage.push_back({cp,500});
    auto font=asset(400,16,4,1,coverage);
    const auto hash=normalized_font_family_hash("Controls");
    std::array<TextFontFace,2> faces={{{1,0,font.font},{2,7,font.font,hash}}};
    TextSystem system;assert(system.bind(faces.data(),faces.size())==S::Ok);
    const auto packets=[&](const std::string& markup,const char* extra="",std::size_t max_commands=8192) {
        auto document=HtmlParser().parse("<body>"+markup+"</body>");
        StyleResolver resolver(CssParser().parse(std::string(
            "body {margin:0} input,textarea,select {margin:0;width:100px;height:24px;padding:0;"
            "border:0;font-size:16px;color:#000000;")+extra+"}"));
        LayoutEngineOptions lo;lo.text_system=&system;
        auto layout=LayoutEngine(resolver,lo).layout(*document,160,80);
        LayerTreeBuilderOptions po;po.text_system=&system;po.max_display_commands=max_commands;
        auto layers=LayerTreeBuilder(po).build(*layout);
        auto commands=LayerTreeBuilder(po).flatten(*layers);
        std::vector<DisplayCommand> output;
        for(auto& command:commands) {
            assert(command.type()!=DisplayCommandType::Text);
            if(command.type()==DisplayCommandType::PreparedText) output.push_back(std::move(command));
        }
        return output;
    };
    const auto text_of=[](const DisplayCommand& command) {
        PreparedTextView view;assert(decode_prepared_text(command.text,view));
        std::string text;
        for(std::size_t i=0;i<view.size();++i) {PreparedTextGlyph glyph;assert(view.read(i,glyph));text.push_back(static_cast<char>(glyph.source_codepoint));}
        return text;
    };
    SoftwareCompositor::Options paint_options;paint_options.text_system=&system;
    SoftwareCompositor compositor(ImagePainter{},paint_options);
    const auto replay=[&](const std::vector<DisplayCommand>& commands) {
        LayerNode layer;layer.type=LayerType::Root;layer.bounds={0,0,160,80};layer.display_list=commands;
        return compositor.render(layer,160,80,Color{255,255,255,255});
    };
    const auto assert_clipped_ink=[&](const std::vector<DisplayCommand>& commands) {
        for(const auto& command:commands) {
            // Exercise the real painterless compositor dispatch, one retained
            // native packet at a time so each label/arrow clip is verified.
            const auto frame=replay({command});std::size_t ink=0;
            for(int y=0;y<frame.height;++y) for(int x=0;x<frame.width;++x) {
                const auto& pixel=frame.pixels[static_cast<std::size_t>(y)*frame.width+x];
                if(pixel.r==255 && pixel.g==255 && pixel.b==255) continue;
                ++ink;assert(x>=command.rect.x && x<command.rect.x+command.rect.width &&
                    y>=command.rect.y && y<command.rect.y+command.rect.height);
            }
            assert(ink>0);
        }
    };
    for(const auto& fixture:std::vector<std::pair<std::string,std::string>>{
        {"<input value='AB'>","AB"},{"<textarea>AB</textarea>","AB"},
        {"<input placeholder='AB'>","AB"},{"<input type='date' value='2026-11-15'>","2026-11-15"},
        {"<input type='time' value='12:34'>","12:34"},{"<input type='color' value='#112233'>","#112233"},
        {"<input type='file'>","Choose file"},{"<input type='file' value='A.txt'>","A.txt"}}) {
        const auto commands=packets(fixture.first);assert(commands.size()==1 && text_of(commands[0])==fixture.second);
        const auto& command=commands[0];
        assert(prepared_text_baseline_y(command)>command.rect.y);
        assert(prepared_text_baseline_y(command)<=command.rect.y+command.rect.height);
        assert_clipped_ink(commands);
    }
    auto select=packets("<select><option>AB</option></select>");
    assert(select.size()==2 && text_of(select[0])=="AB" && text_of(select[1])=="v");
    assert_clipped_ink(select);
    assert_clipped_ink(packets("<input value='"+std::string(100,'A')+"'>"));
    assert_clipped_ink(packets("<select><option>"+std::string(100,'A')+"</option></select>"));
    auto centered=packets("<input value='AB'>","text-align:center;");
    auto right=packets("<input value='AB'>","text-align:right;font-family:Controls;");
    assert(prepared_text_origin_x(centered[0])>centered[0].rect.x);
    assert(prepared_text_origin_x(right[0])>prepared_text_origin_x(centered[0]));
    PreparedTextView named;PreparedTextGlyph named_glyph;
    assert(decode_prepared_text(right[0].text,named) && named.read(0,named_glyph) && named_glyph.resource_id==2);
    assert(packets("<input value='AB'>","font-family:Absent;").empty());
    assert(packets("<input value='A&#x301;'>").empty());
    assert(packets("<textarea>A&#10;B</textarea>").empty());
    assert(packets("<input value='"+std::string(129,'A')+"'>").empty());
    assert(packets("<select><option>"+std::string(128,'A')+"</option></select>").empty());
    assert(packets("<select><option>"+std::string(127,'A')+"</option></select>").size()==2);
    const auto placeholder=packets("<input placeholder='AB'>");
    assert(placeholder[0].color.r==100 && placeholder[0].color.g==116 && placeholder[0].color.b==139);
    const auto spaced=packets("<input value='AB'>","letter-spacing:2px;");
    assert(spaced.empty());
    TextSystemLimits limits;limits.max_glyphs=1;assert(system.bind(faces.data(),faces.size(),limits)==S::Ok);
    assert(packets("<select><option>AB</option></select>").empty());
    assert(system.bind(faces.data(),faces.size())==S::Ok);
    auto no_arrow_font=asset();TextFontFace no_arrow{3,0,no_arrow_font.font};assert(system.bind(&no_arrow,1)==S::Ok);
    assert(packets("<select><option>AB</option></select>").empty());
    assert(packets("<input value='C'>").empty());
    assert(system.bind(faces.data(),faces.size())==S::Ok);
    assert(packets("<select><option>AB</option></select>","",1).empty());
    auto live=HtmlParser().parse("<input value='AB'>");
    StyleResolver resolver(CssParser().parse("input {width:100px;height:24px;font-size:16px;color:#000000}"));
    LayoutEngineOptions lo;lo.text_system=&system;auto layout=LayoutEngine(resolver,lo).layout(*live,160,80);
    LayerTreeBuilderOptions po;po.text_system=&system;
    auto before=LayerTreeBuilder(po).build(*layout);const auto retained=LayerTreeBuilder(po).flatten(*before);
    const auto before_pixels=replay(retained);
    Node* control=nullptr;
    const auto find=[&](const auto& self,Node& node)->void {if(node.tag_name=="input")control=&node;for(auto& c:node.children)self(self,*c);};
    find(find,*live);assert(control && set_form_control_value(*control,"BA"));
    auto after=LayerTreeBuilder(po).build(*layout);
    bool changed=false;
    for(const auto& command:LayerTreeBuilder(po).flatten(*after)) if(command.type()==DisplayCommandType::PreparedText) changed=text_of(command)=="BA";
    assert(changed);
    for(const auto& command:retained) if(command.type()==DisplayCommandType::PreparedText) assert(text_of(command)=="AB");
    const auto replay_pixels=replay(retained);
    for(std::size_t i=0;i<before_pixels.pixels.size();++i) {
        const auto& a=before_pixels.pixels[i];const auto& b=replay_pixels.pixels[i];
        assert(a.r==b.r && a.g==b.g && a.b==b.b && a.a==b.a);
    }
    auto live_select=HtmlParser().parse("<select><option>AB</option><option>BA</option></select>");
    auto select_layout=LayoutEngine(resolver,lo).layout(*live_select,160,80);
    auto old_select=LayerTreeBuilder(po).build(*select_layout);
    const auto old_select_commands=LayerTreeBuilder(po).flatten(*old_select);
    const auto old_select_pixels=replay(old_select_commands);
    Node* select_node=nullptr;
    const auto find_select=[&](const auto& self,Node& node)->void {if(node.tag_name=="select")select_node=&node;for(auto& c:node.children)self(self,*c);};
    find_select(find_select,*live_select);assert(select_node && set_form_control_selected_index(*select_node,1));
    auto new_select=LayerTreeBuilder(po).build(*select_layout);bool selected_changed=false;
    for(const auto& command:LayerTreeBuilder(po).flatten(*new_select))
        if(command.type()==DisplayCommandType::PreparedText && text_of(command)=="BA") selected_changed=true;
    assert(selected_changed);
    const auto old_select_replayed=replay(old_select_commands);
    for(std::size_t i=0;i<old_select_pixels.pixels.size();++i) {
        const auto& a=old_select_pixels.pixels[i];const auto& b=old_select_replayed.pixels[i];
        assert(a.r==b.r && a.g==b.g && a.b==b.b && a.a==b.a);
    }
}

void core_prepared_text_ellipsis() {
    auto font = asset(400,16,4,1,{{32,250},{46,200},{65,500},{66,600},{0x2026,700},{0x4e00,1000}});
    TextFontFace face{1,0,font.font}; TextSystem system;
    assert(system.bind(&face,1)==S::Ok);
    LayoutEngineOptions options; options.text_system=&system;

    std::vector<std::unique_ptr<Node>> documents;
    const auto run = [&](std::string source, int width, const char* extra, std::size_t budget=128) {
        auto document=HtmlParser().parse("<body><div>"+source+"</div></body>");
        StyleResolver resolver(CssParser().parse("body { margin:0 } div { margin:0; color:#000000; font-size:16px; line-height:20px; width:"+
            std::to_string(width)+"px; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; "+extra+" }"));
        auto local=options; local.max_prepared_text_glyphs=budget;
        auto result=LayoutEngine(resolver,local).layout(*document,100,40);
        documents.push_back(std::move(document));
        return result;
    };
    auto truncated=run("ABAB",24,"");
    const auto* text=find_text_box(*truncated); assert(text && text->text_layout_cache.prepared_text);
    assert(text->text_layout_cache.rendered_text=="ABAB");
    assert(text->text_layout_cache.lines==std::vector<std::string>{"A\xE2\x80\xA6"});
    PreparedTextView view; PreparedTextGlyph first,last;
    assert(decode_prepared_text(text->text_layout_cache.prepared_packet,view) && view.size()==2);
    assert(view.read(0,first) && view.read(1,last));
    assert(first.source_codepoint=='A' && last.source_codepoint==0x2026 && last.x==8000);
    assert(first.source_begin==0 && first.source_end==1 && last.source_begin==1 && last.source_end==4);
    LayerTreeBuilderOptions layer_options; layer_options.text_system=&system;
    auto layers=LayerTreeBuilder(layer_options).build(*truncated);
    SoftwareCompositor::Options paint_options; paint_options.text_system=&system;
    const auto frame=SoftwareCompositor(ImagePainter{},paint_options).render(*layers,100,40,Color{255,255,255,255});
    std::size_t ink=0;
    for (const auto& pixel:frame.pixels) ink += pixel.r<255 || pixel.g<255 || pixel.b<255;
    assert(ink>0);
    auto fitting=run("AB",24,""); text=find_text_box(*fitting);
    assert(text->text_layout_cache.lines==std::vector<std::string>{"AB"});
    auto fractional=run("ABAB",28,"width:28.9px;"); text=find_text_box(*fractional);
    assert(text->text_layout_cache.lines==std::vector<std::string>{"AB\xE2\x80\xA6"});
    auto multiline=run("A A A",10,"white-space:normal;"); text=find_text_box(*multiline);
    assert(text->text_layout_cache.prepared_text && text->text_layout_cache.prepared_lines.size()==3);
    auto indented_multiline=run("A AB AB",20,"white-space:normal;text-indent:8px;"); text=find_text_box(*indented_multiline);
    assert(text->text_layout_cache.prepared_text && text->text_layout_cache.prepared_lines.size()==3);
    auto narrow=run("A",1,""); text=find_text_box(*narrow);
    assert(text->text_layout_cache.lines==std::vector<std::string>{"A"});
    auto cjk=run("\xE4\xB8\x80\xE4\xB8\x80\xE4\xB8\x80",30,""); text=find_text_box(*cjk);
    assert(text->text_layout_cache.lines==std::vector<std::string>{"\xE4\xB8\x80\xE2\x80\xA6"});
    auto spaced=run("ABAB",25,"letter-spacing:2px;"); text=find_text_box(*spaced);
    assert(decode_prepared_text(text->text_layout_cache.prepared_packet,view) && view.size()==2);
    assert(view.read(1,last) && last.x==10000);
    auto negative=run("ABAB",20,"letter-spacing:-2px;"); text=find_text_box(*negative);
    assert(decode_prepared_text(text->text_layout_cache.prepared_packet,view) && view.size()==2);
    assert(view.read(1,last) && last.x==6000);
    auto zero=run("AB",0,""); text=find_text_box(*zero);
    assert(text->text_layout_cache.lines==std::vector<std::string>{"A"});
    auto indent=run("ABAB",30,"text-indent:8px;text-align:right;"); text=find_text_box(*indent);
    assert(text->text_layout_cache.lines==std::vector<std::string>{"A\xE2\x80\xA6"});
    assert(text->text_layout_cache.prepared_lines.front().origin_x==8);
    auto negative_indent=run("ABAB",24,"text-indent:-2px;"); text=find_text_box(*negative_indent);
    assert(!text->text_layout_cache.prepared_text && text->text_layout_cache.prepared_lines.empty());
    auto refused=run("ABAB",24,"",3); text=find_text_box(*refused);
    assert(!text->text_layout_cache.prepared_text && text->text_layout_cache.prepared_lines.empty());
    auto first_budget=run("A",1,"",1); text=find_text_box(*first_budget);
    assert(text->text_layout_cache.prepared_text && text->text_layout_cache.lines==std::vector<std::string>{"A"});
    auto mixed=run("A<span>B</span>A",10,"");
    const auto assert_refused = [&](const auto& self,const LayoutBox& box) -> void {
        if (box.node && box.node->type==NodeType::Text)
            assert(!box.text_layout_cache.prepared_text && box.text_layout_cache.prepared_lines.empty());
        for (const auto& child:box.children) self(self,*child);
    };
    assert_refused(assert_refused,*mixed);
    auto generated_document=HtmlParser().parse("<body><div>ABAB</div></body>");
    StyleResolver generated_resolver(CssParser().parse(
        "div {width:24px;font-size:16px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}"
        "div::before {content:'A'} div::after {content:'B'}"));
    auto generated=LayoutEngine(generated_resolver,options).layout(*generated_document,100,40);
    assert_refused(assert_refused,*generated);
    auto generated_layers=LayerTreeBuilder(layer_options).build(*generated);
    const auto generated_commands=LayerTreeBuilder(layer_options).flatten(*generated_layers);
    for (const auto& command:generated_commands)
        assert(command.type()!=DisplayCommandType::Text && command.type()!=DisplayCommandType::PreparedText);
    auto list_document=HtmlParser().parse("<body><ul><li>ABAB</li></ul></body>");
    StyleResolver list_resolver(CssParser().parse(
        "li {width:24px;font-size:16px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;list-style-type:disc}"));
    auto list=LayoutEngine(list_resolver,options).layout(*list_document,100,60);
    assert_refused(assert_refused,*list);
    auto list_layers=LayerTreeBuilder(layer_options).build(*list);
    for (const auto& command:LayerTreeBuilder(layer_options).flatten(*list_layers))
        assert(command.type()!=DisplayCommandType::Text && command.type()!=DisplayCommandType::PreparedText);
    auto nested_document=HtmlParser().parse("<body><div>A<span>B</span>A</div></body>");
    StyleResolver nested_resolver(CssParser().parse(
        "div {width:24px;font-size:16px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}"
        "span::before {content:'A'} span::after {content:'B'}"));
    auto nested=LayoutEngine(nested_resolver,options).layout(*nested_document,100,40);
    assert_refused(assert_refused,*nested);
    auto nested_layers=LayerTreeBuilder(layer_options).build(*nested);
    for (const auto& command:LayerTreeBuilder(layer_options).flatten(*nested_layers))
        assert(command.type()!=DisplayCommandType::Text && command.type()!=DisplayCommandType::PreparedText);
    auto block_document=HtmlParser().parse("<body><div>A<span>B</span><p>AB</p></div></body>");
    auto block=LayoutEngine(nested_resolver,options).layout(*block_document,100,60);
    auto block_layers=LayerTreeBuilder(layer_options).build(*block);
    std::size_t independent_text=0;
    for (const auto& command:LayerTreeBuilder(layer_options).flatten(*block_layers)) {
        assert(command.type()!=DisplayCommandType::Text);
        independent_text+=command.type()==DisplayCommandType::PreparedText;
    }
    assert(independent_text==1);
    auto visible=run("ABAB",10,"overflow:visible;white-space:normal;"); text=find_text_box(*visible);
    assert(text->text_layout_cache.prepared_text && text->text_layout_cache.lines==std::vector<std::string>{"ABAB"});
    auto unbreakable=run("ABAB",24,"white-space:normal;"); text=find_text_box(*unbreakable);
    assert(text->text_layout_cache.prepared_text && text->text_layout_cache.lines==std::vector<std::string>{"A\xE2\x80\xA6"});
    // Reuse the same engine across source and width changes, and compare source
    // geometry with overflow:visible so truncation cannot change layout metrics.
    auto live=HtmlParser().parse("<body><div id='live'>ABAB</div></body>");
    StyleResolver live_resolver(CssParser().parse(
        "body { margin:0 } div { margin:0; width:24px; font-size:16px;line-height:20px;"
        "white-space:nowrap;overflow:hidden;text-overflow:ellipsis;color:#000000 }"));
    LayoutEngine live_engine(live_resolver,options);
    Node* live_owner=nullptr;
    const auto find_live=[&](const auto& self,Node& node)->void {
        if (node.attribute("id")=="live") live_owner=&node;
        for (auto& child:node.children) self(self,*child);
    };
    find_live(find_live,*live); assert(live_owner);
    auto before=live_engine.layout(*live,100,40);
    const Rect source_rect=find_text_box(*before)->rect;
    live_owner->set_attribute("style","overflow:visible");
    auto normal=live_engine.layout(*live,100,40);
    const Rect normal_rect=find_text_box(*normal)->rect;
    assert(source_rect.x==normal_rect.x && source_rect.width==normal_rect.width && source_rect.height==normal_rect.height);
    live_owner->set_attribute("style","width:60px");
    auto wider=live_engine.layout(*live,100,40);
    assert(find_text_box(*wider)->text_layout_cache.lines==std::vector<std::string>{"ABAB"});
    live_owner->set_text_content("ABABAB");
    live_owner->set_attribute("style","width:24px");
    auto mutated=live_engine.layout(*live,100,40);
    assert(find_text_box(*mutated)->text_layout_cache.rendered_text=="ABABAB");
    assert(find_text_box(*mutated)->text_layout_cache.lines==std::vector<std::string>{"A\xE2\x80\xA6"});
    // Relayout must not mutate the prior frame's retained source snapshot.
    assert(find_text_box(*before)->text_layout_cache.rendered_text=="ABAB");
    auto dotfont=asset(400,16,4,2,{{46,200},{65,500},{66,600}},65);
    face={2,0,dotfont.font}; assert(system.bind(&face,1)==S::Ok);
    auto dots=run("AB",9,""); text=find_text_box(*dots);
    assert(text->text_layout_cache.lines==std::vector<std::string>{"A..."});
    assert(decode_prepared_text(text->text_layout_cache.prepared_packet,view) && view.size()==4);
    auto marker_budget=run("AB",9,"",3); text=find_text_box(*marker_budget);
    assert(!text->text_layout_cache.prepared_text && text->text_layout_cache.prepared_lines.empty());
    auto unsupported=run("AB\xCC\x81",10,""); text=find_text_box(*unsupported);
    assert(!text->text_layout_cache.prepared_text && text->text_layout_cache.prepared_lines.empty());
}

void core_prepared_text_applies_bounded_letter_spacing() {
    auto font = asset();
    const auto family_hash = normalized_font_family_hash("Core Test");
    std::array<TextFontFace, 2> faces = {{{101, 102, font.font, family_hash},
                                          {102, 0, font.font, 0}}};
    TextSystem system;
    assert(system.bind(faces.data(), faces.size()) == S::Ok);
    LayoutEngineOptions layout_options;
    layout_options.text_system = &system;
    std::vector<std::unique_ptr<Node>> documents;

    const auto layout_text = [&](const char* css_text) {
        auto document = HtmlParser().parse("<body><div>AB</div></body>");
        StyleResolver resolver(CssParser().parse(css_text));
        auto layout = LayoutEngine(resolver, layout_options).layout(*document, 80, 40);
        documents.push_back(std::move(document));
        return layout;
    };
    const auto assert_positions = [&](const LayoutBox& box, std::int32_t expected_spacing) {
        assert(box.text_layout_cache.prepared_text);
        assert(box.text_layout_cache.prepared_lines.size() == 1);
        PreparedTextView view;
        assert(decode_prepared_text(box.text_layout_cache.prepared_lines[0].packet, view));
        assert(view.size() == 2);
        PreparedTextGlyph first, second;
        assert(view.read(0, first) && view.read(1, second));
        assert(first.x == 0 && second.x == first.geometry.advance + expected_spacing * 1000);
        assert(second.geometry.bearing_x == -1000);
    };

    auto positive = layout_text(
        "div { margin: 0; width: 40px; font-family: 'Core Test'; font-size: 16px; "
        "line-height: 20px; color: #000000; white-space: nowrap; text-align: right; letter-spacing: 2px; }");
    const LayoutBox* positive_text = find_text_box(*positive);
    assert(positive_text != nullptr);
    assert(positive_text->rect.width >= 20);
    assert(positive_text->text_layout_cache.prepared_lines[0].width == 20);
    assert(positive_text->text_layout_cache.prepared_lines[0].origin_x ==
           positive_text->rect.x + positive_text->rect.width - 20);
    assert_positions(*positive_text, 2);
    auto negative = layout_text(
        "div { margin: 0; width: 40px; font-family: 'Core Test'; font-size: 16px; "
        "line-height: 20px; white-space: nowrap; letter-spacing: -2px; }");
    const LayoutBox* negative_text = find_text_box(*negative);
    assert(negative_text != nullptr);
    assert(negative_text->rect.width >= 16);
    assert(negative_text->text_layout_cache.prepared_lines[0].width == 16);
    assert_positions(*negative_text, -2);

    auto bounded = layout_text(
        "div { margin: 0; width: 80px; font-family: 'Core Test'; font-size: 16px; "
        "line-height: 20px; white-space: nowrap; letter-spacing: 100px; }");
    const LayoutBox* bounded_text = find_text_box(*bounded);
    assert(bounded_text != nullptr);
    TextFontRequest request;
    request.families = &faces[0].family_id;
    request.family_count = 1;
    request.size_q1000 = 16000;
    TextRunMetrics base_metrics;
    assert(system.measure("AB", request, base_metrics) == S::Ok);
    const int bounded_spacing = bounded_letter_spacing(16, bounded_text->style.letter_spacing);
    const std::int64_t expected_advance = static_cast<std::int64_t>(base_metrics.advance) +
        static_cast<std::int64_t>(bounded_spacing) * 1000;
    assert(bounded_text->text_layout_cache.prepared_lines[0].width ==
           (expected_advance + 999) / 1000);
    assert_positions(*bounded_text, bounded_spacing);

    auto wrapped_document = HtmlParser().parse("<body><div>AB</div></body>");
    StyleResolver wrapped_resolver(CssParser().parse(
        "div { margin: 0; width: 18px; font-family: 'Core Test'; font-size: 16px; "
        "line-height: 20px; overflow-wrap: anywhere; letter-spacing: 2px; }"));
    auto wrapped = LayoutEngine(wrapped_resolver, layout_options).layout(*wrapped_document, 80, 40);
    const LayoutBox* wrapped_text = find_text_box(*wrapped);
    assert(wrapped_text != nullptr && wrapped_text->text_layout_cache.prepared_text);
    assert((wrapped_text->text_layout_cache.lines == std::vector<std::string>{"A", "B"}));
    assert(wrapped_text->text_layout_cache.prepared_lines.size() == 2);
    for (const auto& line : wrapped_text->text_layout_cache.prepared_lines) {
        PreparedTextView view;
        PreparedTextGlyph glyph;
        assert(decode_prepared_text(line.packet, view) && view.size() == 1);
        assert(view.read(0, glyph) && glyph.x == 0);
    }

    auto document = HtmlParser().parse("<body><p id='label'>AB</p></body>");
    StyleResolver resolver(CssParser().parse(
        "body { margin: 0; } p { margin: 0; width: 40px; height: 24px; padding: 0; "
        "font-family: 'Core Test'; font-size: 16px; line-height: 20px; color: #000000; "
        "white-space: nowrap; letter-spacing: 0px; } "
        "p.spaced { letter-spacing: 2px; }"));
    LayoutEngineOptions route_layout_options;
    route_layout_options.text_system = &system;
    LayerTreeBuilderOptions route_layer_options;
    route_layer_options.text_system = &system;
    const auto capture = [&]() {
        auto render = RenderTreeBuilder(resolver).build(*document);
        auto layout = LayoutEngine(resolver, route_layout_options).layout(*render, 80, 40);
        auto layers = LayerTreeBuilder(route_layer_options).build(*layout);
        auto commands = LayerTreeBuilder(route_layer_options).flatten(*layers);
        const auto command = std::find_if(commands.begin(), commands.end(), [](const auto& item) {
            return item.type() == DisplayCommandType::PreparedText;
        });
        assert(command != commands.end());
        PreparedTextView packet;
        assert(decode_prepared_text(command->text, packet));
        FrameBuffer frame(80, 40, Color{255, 255, 255, 255});
        assert(system.paint(packet, frame, command->rect,
                            static_cast<std::int64_t>(prepared_text_origin_x(*command)) * 1000,
                            static_cast<std::int64_t>(prepared_text_baseline_y(*command)) * 1000,
                            command->color) == S::Ok);
        std::size_t ink = 0;
        for (const auto& pixel : frame.pixels) ink += pixel.r < 255 || pixel.g < 255 || pixel.b < 255;
        assert(ink > 0);
        return frame;
    };
    clear_dirty_flags(*document);
    const FrameBuffer normal_frame = capture();
    Node* label = nullptr;
    const auto find_label = [&](const auto& self, Node& node) -> void {
        if (node.attribute("id") == "label") label = &node;
        for (auto& child : node.children) self(self, *child);
    };
    find_label(find_label, *document);
    assert(label != nullptr);
    label->set_attribute("class", "spaced");
    assert((subtree_dirty_flags(*document) & (DomDirtyStyle | DomDirtyLayout | DomDirtyAttributes)) != 0U);
    const FrameBuffer spaced_frame = capture();
    assert(normal_frame.pixels.size() == spaced_frame.pixels.size());
    bool changed = false;
    for (std::size_t i = 0; i < normal_frame.pixels.size(); ++i) {
        const auto left = normal_frame.pixels[i];
        const auto right = spaced_frame.pixels[i];
        changed |= left.r != right.r || left.g != right.g || left.b != right.b || left.a != right.a;
    }
    assert(changed);
}
void core_layout_refusals_are_explicit() {
    auto a=asset();
    std::array<TextFontFace,2> faces={{{101,17,a.font,normalized_font_family_hash("Core Test")},
                                      {102,0,a.font,0}}};
    TextSystem system;assert(system.bind(faces.data(),faces.size())==S::Ok);
    HtmlParser html;CssParser css;
    const auto run_refusal=[&](const char* markup,const char* rules,LayoutEngineOptions options) {
        VectorDiagnosticSink diagnostics;
        options.text_system=&system;
        options.diagnostics=&diagnostics;
        auto document=html.parse(markup);
        StyleResolver resolver(css.parse(rules));
        auto layout=LayoutEngine(resolver,options).layout(*document,80,40);
        const auto* text=find_text_box(*layout);
        assert(text && text->text_layout_cache.valid && !text->text_layout_cache.prepared_text);
        assert(text->rect.width==0 && text->rect.height==0);
        assert(diagnostics.size()==1);
        assert(diagnostics.diagnostics()[0].code=="layout-core-text-refused");
        LayerTreeBuilderOptions layer_options;layer_options.text_system=&system;
        auto layers=LayerTreeBuilder(layer_options).build(*layout);
        for (const auto& command:layers->display_list)
            assert(command.type()!=DisplayCommandType::Text && command.type()!=DisplayCommandType::PreparedText);
    };

    run_refusal("<body><div>AB</div></body>",
                "div { font-family: 'Unregistered'; white-space: nowrap; }",{});
    LayoutEngineOptions glyph_limit;glyph_limit.max_prepared_text_glyphs=1;
    run_refusal("<body><div>AB</div></body>",
                "div { font-family: 'Core Test'; white-space: nowrap; }",glyph_limit);
}
int inspect(int argc,char** argv) {
    constexpr char probe_text[]="A jg\xe2\x80\xa6\xe4\xb8\xad 10/26 12/29 14/31 16/30";
    std::vector<Asset> assets(static_cast<std::size_t>(argc-1));
    std::vector<TextFontFace> faces(assets.size());
    for (std::size_t i=0;i<assets.size();++i) {
        std::ifstream stream(argv[i+1],std::ios::binary);if (!stream) return 2;
        assets[i].bytes.assign(std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>());
        if (assets[i].font.load(assets[i].bytes.data(),assets[i].bytes.size())!=FontResourceStatus::Loaded) return 3;
        faces[i]={i+1,0,assets[i].font};
    }
    TextSystem system;if (system.bind(faces.data(),faces.size())!=S::Ok) return 4;
    {
        auto document=HtmlParser().parse("<body><div>ABABAB</div></body>");
        StyleResolver resolver(CssParser().parse(
            "body { margin:0 } div { margin:0;width:40px;font-size:24px;line-height:30px;"
            "white-space:nowrap;overflow:hidden;text-overflow:ellipsis;color:#000000 }"));
        LayoutEngineOptions layout_options;layout_options.text_system=&system;
        auto layout=LayoutEngine(resolver, layout_options).layout(*document,80,40);
        const auto* text=find_text_box(*layout);
        if (!text || !text->text_layout_cache.prepared_text) return 15;
        PreparedTextView packet;PreparedTextGlyph marker;
        if (!decode_prepared_text(text->text_layout_cache.prepared_packet,packet) || packet.size()<2 ||
            !packet.read(packet.size()-1,marker) || marker.codepoint!=0x2026) return 16;
        LayerTreeBuilderOptions layer_options;layer_options.text_system=&system;
        auto layers=LayerTreeBuilder(layer_options).build(*layout);
        SoftwareCompositor::Options paint_options;paint_options.text_system=&system;
        const auto frame=SoftwareCompositor(ImagePainter{},paint_options).render(*layers,80,40,Color{255,255,255,255});
        std::size_t visible=0;
        for (const auto& pixel:frame.pixels) visible+=pixel.r<255 || pixel.g<255 || pixel.b<255;
        if (!visible) return 17;
    }
    std::cout<<"{\"rows\":[";bool first=true;
    for (auto size:{7000,8000,15500,24000,28000}) for (auto weight:{350,400,450,500,550,700,900}) {
        TextFontRequest r;r.size_q1000=size;r.weight=static_cast<std::uint16_t>(weight);
        std::array<PreparedTextGlyph,64> glyphs;TextRunMetrics m;
        if (system.prepare(probe_text,r,glyphs.data(),glyphs.size(),m)!=S::Ok) return 5;
        if (!first) std::cout<<',';first=false;
        std::cout<<"{\"size\":"<<size<<",\"weight\":"<<weight<<",\"primary\":"<<m.primary_resource_id
            <<",\"font\":["<<m.primary_font.ascent<<','<<m.primary_font.descent<<','<<m.primary_font.line_gap
            <<','<<m.primary_font.line_height<<"],\"advance\":"<<m.advance<<",\"glyphs\":[";
        for (std::size_t i=0;i<m.glyph_count;++i) {
            if (i) std::cout<<',';const auto& g=glyphs[i];
            std::cout<<'['<<g.resource_id<<','<<g.codepoint<<','<<g.glyph_id<<','<<g.source_begin<<','<<g.source_end
                <<','<<g.x<<','<<g.geometry.advance<<','<<g.geometry.bearing_x<<','<<g.geometry.bearing_y
                <<','<<g.geometry.width<<','<<g.geometry.height<<']';
        }
        std::cout<<"]}";
    }
    constexpr std::array<const char*,4> tiny_labels={"10/26","12/29","14/31","16/30"};
    const Color background{17,29,43,255},ink{235,241,249,255};
    std::cout<<"],\"tinyPaints\":[";first=true;
    for (const auto* label:tiny_labels) for (const auto size:{7000,8000}) {
        TextFontRequest request;request.size_q1000=size;
        std::array<PreparedTextGlyph,16> glyphs{};TextRunMetrics measured,prepared;
        if (system.measure(label,request,measured)!=S::Ok ||
            system.prepare(label,request,glyphs.data(),glyphs.size(),prepared)!=S::Ok ||
            measured.advance!=prepared.advance || measured.glyph_count!=prepared.glyph_count ||
            prepared.glyph_count!=std::char_traits<char>::length(label)) return 6;
        std::size_t source_offset=0;std::int32_t expected_x=0;
        for (std::size_t i=0;i<prepared.glyph_count;++i) {
            if (glyphs[i].source_begin!=source_offset || glyphs[i].source_end!=source_offset+1 ||
                glyphs[i].x!=expected_x) return 7;
            expected_x+=glyphs[i].geometry.advance;source_offset+=1;
        }
        if (expected_x!=prepared.advance) return 8;
        std::string packet;
        if (!encode_prepared_text(glyphs.data(),prepared.glyph_count,size,
                                  static_cast<std::uint32_t>(std::char_traits<char>::length(label)),packet)) return 9;
        PreparedTextView view;
        if (!decode_prepared_text(packet,view)) return 10;
        FrameBuffer rendered(64,20,background),repeat(64,20,background);
        if (system.paint(view,rendered,{0,0,64,20},1000,13000,ink)!=S::Ok ||
            system.paint(view,repeat,{0,0,64,20},1000,13000,ink)!=S::Ok) return 11;
        if (rendered.pixels.size()!=repeat.pixels.size()) return 12;
        std::size_t visible=0;std::uint32_t hash=2166136261U;
        for (std::size_t i=0;i<rendered.pixels.size();++i) {
            const auto& a=rendered.pixels[i];const auto& b=repeat.pixels[i];
            if (a.r!=b.r || a.g!=b.g || a.b!=b.b || a.a!=b.a) return 13;
            if (a.r!=background.r || a.g!=background.g || a.b!=background.b || a.a!=background.a) ++visible;
            for (const auto component:{a.r,a.g,a.b,a.a}) hash=(hash^component)*16777619U;
        }
        if (visible==0) return 14;
        if (!first) std::cout<<',';first=false;
        std::cout<<"{\"size\":"<<size<<",\"text\":\""<<label
                 <<"\",\"advance\":"<<prepared.advance<<",\"glyphs\":"<<prepared.glyph_count
                 <<",\"visiblePixels\":"<<visible<<",\"pixelHash\":"<<hash<<"}";
    }
    std::cout<<"]}\n";return 0;
}
} // namespace
int main(int argc,char** argv) {
#ifdef _MSC_VER
    _set_error_mode(_OUT_TO_STDERR);_CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
#endif
    if (argc>1) return inspect(argc,argv);
    weights();strikes_and_families();fallback_and_geometry();refusals_and_budgets();
    prepared_packet_paint();offset_prepared_packets();installed_shared_paragraph_path();html_layout_to_core_paint();
    route_relayout_keeps_prepared_text_while_dom_is_dirty();aligned_prepared_text_stays_inside_its_layout_box();
    core_prepared_text_applies_bounded_letter_spacing();
    core_prepared_text_ellipsis();
    core_native_control_prepared_labels();
    unrelated_input_mutation_preserves_static_prepared_text();
    core_layout_refusals_are_explicit();
    std::cout<<"text system selection/geometry/transaction/budget tests passed\n";
}
