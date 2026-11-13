#include "render_core/css_parser.h"
#include "render_core/html_parser.h"
#include "render_core/layout.h"
#include "render_core/hit_test.h"
#include "render_core/layer_tree.h"
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
#include <limits>
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
Node& find_node(Node& node, const char* id) {
    if (node.attribute("id") == id) return node;
    for (auto& child : node.children) {
        try { return find_node(*child, id); } catch (const std::out_of_range&) {}
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
struct InlineImageState {
    std::uint32_t handle = 91;
    std::size_t resolve_calls = 0;
    bool fail_emission = false;
    bool resolved = true;
};
struct NaturalInlineImageState {
    std::size_t calls = 0;
    ImageIntrinsicSize result{ImageIntrinsicSizeState::Known, 15, 10};
};
ImageIntrinsicSize natural_inline_size(const Node& node, void* context) {
    auto& state = *static_cast<NaturalInlineImageState*>(context);
    ++state.calls;
    if (node.tag_name != "img" || node.attribute("src") != "tile") return {};
    return state.result;
}
bool resolve_inline_image(const Node& node, ImageResolveKind kind, std::uint16_t,
    ResolvedImageResource& result, void* context) {
    auto& state = *static_cast<InlineImageState*>(context);
    ++state.resolve_calls;
    if (kind != ImageResolveKind::Content || node.tag_name != "img" || node.attribute("src") != "tile" ||
        !state.resolved) return false;
    result.image_handle = state.handle;
    return true;
}
bool emit_inline_image(const LayoutBox& box, DisplayList& output, void* context) {
    auto& state = *static_cast<InlineImageState*>(context);
    if (state.fail_emission) return false;
    ResolvedImageResource resource;
    if (!resolve_inline_image(*box.node, ImageResolveKind::Content, 0, resource, &state) ||
        resource.image_handle == 0) return true;
    DisplayCommand command;
    command.set_type(DisplayCommandType::Image);
    command.rect = box.rect;
    command.image_paint().image_handle = resource.image_handle;
    command.image_paint().object_fit = box.style.object_fit;
    command.image_paint().object_position = box.style.object_position;
    command.image_paint().image_rendering = box.style.image_rendering;
    output.push_back(std::move(command));
    return true;
}
bool paint_inline_image(FrameBuffer& target, const ImagePaintRequest& request, void* context) {
    auto& painted = *static_cast<std::size_t*>(context);
    if (request.image_handle != 91) return false;
    ++painted;
    const auto rect = request.destination;
    for (int y = std::max(0, rect.y); y < std::min(target.height, rect.y+rect.height); ++y)
        for (int x = std::max(0, rect.x); x < std::min(target.width, rect.x+rect.width); ++x)
            target.pixel(x,y) = {220,30,40,255};
    return true;
}
bool equal_pixels(const FrameBuffer& a, const FrameBuffer& b) {
    if (a.pixels.size() != b.pixels.size()) return false;
    for (std::size_t i = 0; i < a.pixels.size(); ++i) {
        const auto x=a.pixels[i], y=b.pixels[i];
        if (x.r!=y.r || x.g!=y.g || x.b!=y.b || x.a!=y.a) return false;
    }
    return true;
}
void definite_inline_image_atomic_path() {
    Fixture f;
    StyleResolver styles(CssParser{}.parse(
        "*{margin:0;padding:0;border:0}div{display:block;width:40px;font-size:16px;line-height:18px}"
        "span{display:inline}img{display:inline;width:7.5px;height:18px}"));
    auto dom = HtmlParser{}.parse("<div id='p'><span>A<img id='i' src='tile'>B</span></div>");
    NaturalImageSizingStatistics natural_stats;
    f.options.natural_image_sizing.statistics = &natural_stats;
    auto layout = LayoutEngine(styles, {}, f.options).layout(*dom, 80, 50);
    const auto& host = find(*layout, "p");
    const auto& image = find(*layout, "i");
    const auto binding = host.text_layout_cache.shared_inline;
    check(binding && detail::shared_inline_current(*binding, &f.system),
        "definite inline image joins a live shared paragraph");
    check(binding->snapshot->atomic_placements.size() == 1 && natural_stats.queries == 0,
        "definite image uses a planner atom without intrinsic metadata queries");
    check(image.content_geometry.width == 7500 && image.content_geometry.height() == 18000 &&
        image.content_geometry.height_definite() && image.rect.width == 8 && image.rect.height == 18,
        "fractional Q1000 width projects around an integer-height atomic box");
    check(binding->snapshot->atomic_placements.front().extent == 18 &&
        image.content_geometry.y + image.content_geometry.height() ==
            static_cast<std::int64_t>(binding->snapshot->paragraph.lines.front().baseline +
                host.rect.y + binding->snapshot->content_offset_y)*1000,
        "atomic bottom shares the line text baseline");
    const auto hit = HitTester{}.hit_test(*layout, image.rect.x+image.rect.width/2,
        image.rect.y+image.rect.height/2);
    check(hit && hit.node == image.node, "atomic image rectangle remains an inline hit target");

    InlineImageState state;
    f.layers.image_resolver = {resolve_inline_image, &state};
    LayerTreeBuilder layer_builder(f.layers);
    auto layer_tree = layer_builder.build(*layout);
    const auto layer_commands = layer_builder.flatten(*layer_tree);
    std::size_t layer_text_commands = 0, layer_image_commands = 0;
    std::size_t layer_text_before_image = 0, layer_text_after_image = 0;
    bool image_seen = false;
    for (const auto& command : layer_commands) {
        if (command.type() == DisplayCommandType::Image && command.image_paint().image_handle == state.handle) {
            ++layer_image_commands;
            image_seen = true;
        } else if (command.type() == DisplayCommandType::PreparedText) {
            ++layer_text_commands;
            if (image_seen) ++layer_text_after_image;
            else ++layer_text_before_image;
        }
    }
    check(layer_image_commands == 1 && layer_text_commands == 2 &&
        layer_text_before_image == 1 && layer_text_after_image == 1,
        "LayerTree resolves and interleaves typed inline image between prepared text commands");
    state.resolved = false;
    auto unresolved_layer = layer_builder.build(*layout);
    const auto unresolved_commands = layer_builder.flatten(*unresolved_layer);
    std::size_t unresolved_text_commands = 0, unresolved_image_commands = 0;
    for (const auto& command : unresolved_commands) {
        if (command.type() == DisplayCommandType::PreparedText) ++unresolved_text_commands;
        if (command.type() == DisplayCommandType::Image && command.image_paint().image_handle == state.handle)
            ++unresolved_image_commands;
    }
    check(unresolved_text_commands == 2 && unresolved_image_commands == 0,
        "unresolved inline image is omitted while its surrounding text remains painted");
    state.resolved = true;
    state.resolve_calls = 0;
    DisplayList display;
    check(!detail::emit_shared_inline(host, display) && display.empty(),
        "atomic paragraph refuses emission without an image emitter");
    state.fail_emission = true;
    check(!detail::emit_shared_inline(host, display, emit_inline_image, &state) && display.empty(),
        "image callback refusal discards all prepared text and image commands");
    state.fail_emission = false;
    check(detail::emit_shared_inline(host, display, emit_inline_image, &state),
        "image callback commits the mixed paragraph draft");
    check(display.size() == 3 && display[0].type() == DisplayCommandType::PreparedText &&
        display[1].type() == DisplayCommandType::Image && display[2].type() == DisplayCommandType::PreparedText &&
        display[1].rect.x == image.rect.x && display[1].rect.y == image.rect.y &&
        display[1].image_paint().image_handle == state.handle,
        "typed image command is interleaved in source order with text");
    check(prepared_text_baseline_y(display[0]) == display[0].text_paint().baseline_y &&
        static_cast<std::int64_t>(image.rect.y+image.rect.height)*1000 ==
            static_cast<std::int64_t>(prepared_text_baseline_y(display[0]))*1000,
        "emitted image and prepared text preserve a common baseline");
    for (const auto& command : display) {
        if (command.type() != DisplayCommandType::PreparedText) continue;
        PreparedTextView packet;
        check(decode_prepared_text(command.text, packet), "mixed packet decodes");
        for (std::size_t i = 0; i < packet.size(); ++i) {
            PreparedTextGlyph glyph;
            packet.read(i, glyph);
            check(glyph.source_codepoint == 'A' || glyph.source_codepoint == 'B',
                "atomic marker never enters a text glyph packet");
        }
    }
    LayerNode layer;
    layer.type = LayerType::Root;
    layer.bounds = {0,0,80,50};
    layer.display_list = std::move(display);
    std::size_t painted = 0;
    SoftwareCompositor::Options paint_options;
    paint_options.text_system = &f.system;
    SoftwareCompositor compositor({}, ImagePainter{paint_inline_image, &painted, true}, paint_options);
    const auto pixels = compositor.render(layer, 80, 50, {255,255,255,255});
    check(painted == 1 && pixels.pixel(image.rect.x+image.rect.width/2, image.rect.y+image.rect.height/2).r == 220,
        "mixed shared paragraph rasterizes the callback-backed image pixels");

    const auto original_binding = binding;
    auto& mutable_image = const_cast<LayoutBox&>(image);
    const auto original_rect = image.rect;
    detail::LayoutMutationJournal transaction;
    auto& mutable_host = const_cast<LayoutBox&>(host);
    check(transaction.capture(mutable_host, 128) == detail::LayoutMutationJournalStatus::Ready,
        "journal captures shared image members");
    int moved_height = 0;
    check(detail::layout_shared_inline(mutable_host, 9, 11, 40, f.options,
        moved_height), "second atomic paragraph publication succeeds");
    transaction.rollback();
    check(mutable_image.rect.x == original_rect.x && mutable_image.rect.y == original_rect.y &&
        mutable_image.content_geometry.width == 7500 && mutable_host.text_layout_cache.shared_inline == original_binding,
        "journal rollback restores atom geometry and owner binding");
    auto tiny = f.options;
    tiny.shared_inline.max_total_capacity_bytes = 1;
    int height = 0;
    check(!detail::layout_shared_inline(mutable_host, 9, 11, 40, tiny, height) &&
        mutable_image.rect.x == original_rect.x && mutable_image.rect.y == original_rect.y &&
        mutable_host.text_layout_cache.shared_inline == original_binding,
        "resource refusal leaves published image geometry and text untouched");
    const_cast<Node*>(image.node)->set_attribute("src", "changed");
    check(!detail::shared_inline_current(*original_binding, &f.system),
        "source mutation invalidates the image-containing paragraph as a whole");

    StyleResolver changed_styles(CssParser{}.parse(
        "*{margin:0;padding:0;border:0}div{display:block;width:40px;font-size:16px;line-height:18px}"
        "span{display:inline}img{display:inline;width:7.5px;height:18px;object-fit:contain}"));
    auto changed_dom = HtmlParser{}.parse("<div id='p'><span>A<img id='i' src='tile'>B</span></div>");
    auto changed_layout = LayoutEngine(changed_styles, {}, f.options).layout(*changed_dom, 80, 50);
    check(!find(*changed_layout, "p").text_layout_cache.shared_inline,
        "non-Fill object-fit stays on ordinary fallback");
}

void definite_inline_image_refusal_and_style_proof() {
    Fixture f;
    const std::array<const char*, 5> cases{{
        "<img id='i' width='8' height='18'>",
        "<img id='i' src='tile' srcset='other 2x'>",
        "<img id='i' src='tile' sizes='20px'>",
        "<img id='i' src='tile' style='width:8px;height:50%'>",
        "<img id='i' src='tile' style='width:8px;height:18.5px'>"
    }};
    for (const auto* image_markup : cases) {
        std::string html = "<div id='p'><span>A";
        html += image_markup;
        html += "B</span></div>";
        auto dom = HtmlParser{}.parse(html);
        StyleResolver styles(CssParser{}.parse(
            "*{margin:0;padding:0;border:0}div{display:block;width:40px;font-size:16px;line-height:18px}"
            "span{display:inline}img{display:inline;width:8px;height:18px}"));
        auto layout = LayoutEngine(styles, {}, f.options).layout(*dom, 80, 50);
        check(!find(*layout, "p").text_layout_cache.shared_inline,
            "unsupported image source or size contract declines the whole context");
    }
    {
        auto dom = HtmlParser{}.parse("<div id='p'><span>A<img id='i' src='tile'>B</span></div>");
        StyleResolver styles(CssParser{}.parse(
            "*{margin:0;padding:0;border:0}div{display:block;width:40px;font-size:16px;line-height:18px}"
            "span{display:inline}img{display:inline}"));
        auto layout = LayoutEngine(styles, {}, f.options).layout(*dom, 80, 50);
        check(!find(*layout, "p").text_layout_cache.shared_inline,
            "intrinsic or unknown image dimensions remain on the ordinary fallback");
    }
    {
        auto dom = HtmlParser{}.parse("<div id='p'><span>A<img id='i' src='tile'>B</span></div>");
        const auto append_image_child = [&](auto&& self, Node& node) -> Node* {
            if (node.attribute("id") == "i") {
                node.append_child(std::make_unique<Node>(NodeType::Text));
                node.children.back()->set_text("unexpected");
                return &node;
            }
            for (auto& child : node.children) if (auto* found = self(self, *child)) return found;
            return nullptr;
        };
        check(append_image_child(append_image_child, *dom) != nullptr, "image fixture receives a child");
        StyleResolver styles(CssParser{}.parse(
            "*{margin:0;padding:0;border:0}div{display:block;width:40px;font-size:16px;line-height:18px}"
            "span{display:inline}img{display:inline;width:8px;height:18px}"));
        auto layout = LayoutEngine(styles, {}, f.options).layout(*dom, 80, 50);
        check(!find(*layout, "p").text_layout_cache.shared_inline,
            "image children remain outside the atomic subset");
    }
    auto dom = HtmlParser{}.parse("<div id='p'><span>A<img id='i' src='tile'>B</span></div>");
    StyleResolver styles(CssParser{}.parse(
        "*{margin:0;padding:0;border:0}div{display:block;width:40px;font-size:16px;line-height:18px}"
        "span{display:inline}img{display:inline;width:8px;height:18px}"));
    auto layout = LayoutEngine(styles, {}, f.options).layout(*dom, 80, 50);
    const auto& host = find(*layout, "p");
    const auto binding = host.text_layout_cache.shared_inline;
    check(binding != nullptr, "eligible image publishes a proof for mutation checks");
    auto& image = const_cast<LayoutBox&>(find(*layout, "i"));
    image.style.width = 9;
    check(!detail::shared_inline_current(*binding, &f.system),
        "resolved width mutation invalidates atomic image proof");
}

void definite_inline_image_percent_width() {
    Fixture f;
    for (const auto& value : {std::pair<const char*,std::int64_t>{"50%",20000}, {"50.5%",20200}}) {
        StyleResolver styles(CssParser{}.parse(std::string(
            "*{margin:0;padding:0;border:0}div{display:block;width:40px;font-size:16px;line-height:18px}"
            "span{display:inline}img{display:inline;width:")+value.first+";height:18px}"));
        auto dom = HtmlParser{}.parse("<div id='p'><span>A<img id='i' src='tile'>B</span></div>");
        auto layout = LayoutEngine(styles, {}, f.options).layout(*dom, 80, 50);
        const auto& host = find(*layout, "p");
        const auto& image = find(*layout, "i");
        check(host.text_layout_cache.shared_inline && image.content_geometry.width == value.second,
            "definite percentage width resolves against the paragraph containing width");
    }
    auto dom = HtmlParser{}.parse("<div id='p'><span>A<img id='i' src='tile'>B</span></div>");
    StyleResolver styles(CssParser{}.parse(
        "*{margin:0;padding:0;border:0}div{display:block;width:40px;font-size:16px;line-height:18px}"
        "span{display:inline}img{display:inline;width:50%;height:18px}"));
    auto layout = LayoutEngine(styles, {}, f.options).layout(*dom, 80, 50);
    auto& host = const_cast<LayoutBox&>(find(*layout, "p"));
    const auto precise_max = std::numeric_limits<std::int64_t>::max();
    int height = 0;
    check(!detail::layout_shared_inline(host, 0, 0, 40, f.options, height, precise_max),
        "out-of-range percentage basis refuses before multiplication can overflow");
}
void natural_inline_image_metadata_path() {
    const auto run = [](const char* image_style, ImageIntrinsicSize intrinsic, bool expect_shared,
        std::int64_t expected_width, int expected_height) {
        Fixture f;
        NaturalInlineImageState state;
        state.result = intrinsic;
        f.options.natural_image_sizing.provider = {natural_inline_size, &state};
        NaturalImageSizingStatistics stats;
        f.options.natural_image_sizing.statistics = &stats;
        StyleResolver styles(CssParser{}.parse(std::string(
            "*{margin:0;padding:0;border:0}div{display:block;width:40px;font-size:16px;line-height:18px}"
            "span{display:inline}img{display:inline;") + image_style + "}"));
        auto dom = HtmlParser{}.parse("<div id='p'><span>A<img id='i' src='tile'>B</span></div>");
        auto layout = LayoutEngine(styles, {}, f.options).layout(*dom, 80, 50);
        const auto& host = find(*layout, "p");
        const auto& image = find(*layout, "i");
        check(static_cast<bool>(host.text_layout_cache.shared_inline) == expect_shared,
            "natural inline image admission follows bounded sizing result");
        check(state.calls == 1 && stats.queries == 1,
            "natural inline metadata is queried once through the layout operation memo");
        if (expect_shared) {
            check(image.content_geometry.width == expected_width && image.rect.height == expected_height &&
                image.content_geometry.height() == static_cast<std::int64_t>(expected_height) * 1000,
                "natural inline image publishes Q1000 width and integral pixel height");
        }
        auto layers = LayerTreeBuilder(f.layers).build(*layout);
        const auto painted = glyphs(*layers);
        check(painted.size() == 2 && std::get<0>(painted[0]) == 'A' && std::get<0>(painted[1]) == 'B',
            "natural image admission or refusal preserves surrounding source text");
    };
    run("", {ImageIntrinsicSizeState::Known, 15, 10}, true, 15000, 10);
    run("width:30px", {ImageIntrinsicSizeState::Known, 15, 10}, true, 30000, 20);
    run("height:12px", {ImageIntrinsicSizeState::Known, 15, 10}, true, 18000, 12);
    run("min-width:18px", {ImageIntrinsicSizeState::Known, 15, 10}, true, 18000, 12);
    run("max-width:9px", {ImageIntrinsicSizeState::Known, 15, 10}, true, 9000, 6);
    run("width:20px", {ImageIntrinsicSizeState::Known, 15, 10}, false, 0, 0);
    run("", {ImageIntrinsicSizeState::Unknown, 0, 0}, false, 0, 0);
}
void sharp_text_shadow_parity_and_bounds() {
    Fixture fixture;
    for (const char* shadow : {"-15px 4px 0 red", "3px -2px 0"}) {
        const int offset_x = std::string(shadow).find("-15px") == 0 ? -15 : 3;
        const int offset_y = std::string(shadow).find("4px") != std::string::npos ? 4 : -2;
        const std::string css = std::string(
            "*{margin:0;padding:0;border:0}#p{display:block;width:100px;margin-left:20px;"
            "font-size:16px;line-height:20px;color:blue;text-shadow:") + shadow +
            "}span{display:inline}";
        StyleResolver styles(CssParser{}.parse(css));
        auto shared_dom = HtmlParser{}.parse("<div id='p'><span id='a'>A</span><span>B</span></div>");
        auto shared_layout = LayoutEngine(styles, {}, fixture.options).layout(*shared_dom, 140, 60);
        const auto& host = find(*shared_layout, "p");
        check(host.text_layout_cache.shared_inline != nullptr,
              "zero-blur shadow remains eligible for complete shared paragraph");
        const auto& owner = find(*shared_layout, "a");
        const auto& binding = *host.text_layout_cache.shared_inline;
        check(detail::shared_inline_command_count(binding, &fixture.system) == 4,
              "each shared text fragment reserves one shadow and one foreground command");
        auto shared_layers = LayerTreeBuilder(fixture.layers).build(*shared_layout);
        check(shared_layers->display_list.size() == 4,
              "sharp shadow commands are included in bounded shared display output");
        for (std::size_t i = 0; i < shared_layers->display_list.size(); i += 2) {
            const auto& shadow_command = shared_layers->display_list[i];
            const auto& text_command = shared_layers->display_list[i + 1];
            check(shadow_command.type() == DisplayCommandType::PreparedText &&
                text_command.type() == DisplayCommandType::PreparedText &&
                shadow_command.text == text_command.text,
                "shadow reuses the exact frozen prepared packet before foreground text");
            check(shadow_command.text_paint().origin_x == text_command.text_paint().origin_x + offset_x &&
                shadow_command.text_paint().baseline_y == text_command.text_paint().baseline_y + offset_y,
                "prepared glyph origin follows signed shadow offsets");
            check(shadow_command.rect.x == text_command.rect.x + offset_x &&
                shadow_command.rect.y == text_command.rect.y + offset_y,
                "shadow paint bounds follow signed shadow offsets");
        }
        const auto shadow_color = shared_layers->display_list.front().color;
        if (std::string(shadow).find("red") != std::string::npos)
            check(shadow_color.r > shadow_color.g && shadow_color.r > shadow_color.b,
                  "explicit shadow color is retained");
        else
            check(shadow_color.r == owner.style.color.r && shadow_color.g == owner.style.color.g &&
                shadow_color.b == owner.style.color.b,
                "currentColor shadow resolves from the owner text color");

        auto ordinary_options = fixture.options;
        ordinary_options.shared_inline.max_attempts = 0;
        auto ordinary_dom = HtmlParser{}.parse("<div id='p'><span id='a'>A</span><span>B</span></div>");
        auto ordinary_layout = LayoutEngine(styles, {}, ordinary_options).layout(*ordinary_dom, 140, 60);
        check(find(*ordinary_layout, "p").text_layout_cache.shared_inline == nullptr,
              "ordinary shadow path is a control with shared optimization disabled");
        auto ordinary_layers = LayerTreeBuilder(fixture.layers).build(*ordinary_layout);
        SoftwareCompositor::Options paint;
        paint.text_system = &fixture.system;
        SoftwareCompositor compositor({}, paint);
        const auto shared_pixels = compositor.render(*shared_layers, 140, 60, {255,255,255,255});
        LayerNode foreground_only;
        LayerNode shadows_only;
        for (std::size_t i = 0; i < shared_layers->display_list.size(); i += 2) {
            shadows_only.display_list.push_back(shared_layers->display_list[i]);
            foreground_only.display_list.push_back(shared_layers->display_list[i + 1]);
        }
        const auto foreground_pixels = compositor.render(foreground_only, 140, 60, {255,255,255,255});
        const auto shadow_pixels = compositor.render(shadows_only, 140, 60, {255,255,255,255});
        check(!equal_pixels(shared_pixels, foreground_pixels) &&
            !equal_pixels(shadow_pixels, FrameBuffer(140, 60, {255,255,255,255})),
            "offset shadow packets contribute visible pixels before foreground text");
        FrameBuffer tiled(140, 60, {255,255,255,255});
        for (int y = 0; y < 60; y += 7) {
            Rect clip{0,y,140,std::min(7,60-y)};
            compositor.render_into(*shared_layers, tiled, {255,255,255,255}, &clip, 1);
        }
        check(equal_pixels(shared_pixels, tiled), "shadow packet clips compose to the full frame");

        const auto shadow_rect = shared_layers->display_list.front().rect;
        const auto damage = detail::shared_inline_damage_bounds(host);
        bool shadow_ink_covered = true;
        for (int y = 0; y < shadow_pixels.height; ++y) {
            for (int x = 0; x < shadow_pixels.width; ++x) {
                const auto pixel = shadow_pixels.pixel(x, y);
                if (pixel.r == 255 && pixel.g == 255 && pixel.b == 255) continue;
                if (x < damage.x || y < damage.y || x >= safe_edge(damage.x, damage.width) ||
                    y >= safe_edge(damage.y, damage.height)) shadow_ink_covered = false;
            }
        }
        check(shadow_ink_covered, "paragraph dirty bounds cover every painted shadow ink pixel");
        const auto& shadow_owner = find(*shared_layout, "a");
        const auto hit = HitTester{}.hit_test(*shared_layout,
            shadow_rect.x, shadow_rect.y + shadow_rect.height / 2);
        const auto& owner_binding = *shadow_owner.text_layout_cache.shared_inline;
        const auto& owner_member = owner_binding.snapshot->members[owner_binding.member];
        check(shadow_owner.rect.width == owner_member.bounds.width &&
            shadow_owner.rect.height == owner_member.bounds.height,
            "shadow ink does not expand the owner's logical geometry");
        if (offset_x < 0) {
            check(!detail::shared_inline_contains(shadow_owner, shadow_rect.x,
                    shadow_rect.y + shadow_rect.height / 2) && (!hit || hit.node != shadow_owner.node),
                  "negative shadow-only pixels do not expand the logical hit target");
        }

        auto low_budget = fixture.layers;
        low_budget.max_display_commands = 3;
        auto limited = LayerTreeBuilder(low_budget).build(*shared_layout);
        check(glyphs(*limited).empty(),
              "display budget below the complete shadow paragraph emits no partial text");
    }

    StyleResolver blurred(CssParser{}.parse(
        "*{margin:0;padding:0;border:0}#p{width:100px;font-size:16px;text-shadow:1px 1px 2px red}span{display:inline}"));
    auto blurred_dom = HtmlParser{}.parse("<div id='p'><span>A</span><span>B</span></div>");
    auto blurred_layout = LayoutEngine(blurred, {}, fixture.options).layout(*blurred_dom, 140, 60);
    check(find(*blurred_layout, "p").text_layout_cache.shared_inline == nullptr,
          "blurred shadows remain on the established whole-paragraph fallback");
}
void inline_scope_backgrounds() {
    Fixture fixture;
    const std::string css =
        "*{margin:0;padding:0;border:0}#p{display:block;width:44px;font-size:16px;line-height:20px}"
        "#outer{background-color:rgba(255,0,0,.5)}#inner{background-color:blue}span{display:inline}";
    StyleResolver styles(CssParser{}.parse(css));
    auto dom = HtmlParser{}.parse(
        "<div id='p'><span id='outer'>AB AB <span id='inner'>AB AB AB AB</span> AB</span></div>");
    auto layout = LayoutEngine(styles, {}, fixture.options).layout(*dom, 80, 100);
    const auto& host = find(*layout, "p");
    check(host.text_layout_cache.shared_inline != nullptr,
          "solid inline scope backgrounds stay eligible for shared paragraph layout");
    const auto& outer = find(*layout, "outer");
    auto& inner = const_cast<LayoutBox&>(find(*layout, "inner"));
    const auto& outer_binding = *outer.text_layout_cache.shared_inline;
    const auto& inner_binding = *inner.text_layout_cache.shared_inline;
    const auto& outer_member = outer_binding.snapshot->members[outer_binding.member];
    const auto& inner_member = inner_binding.snapshot->members[inner_binding.member];
    check(outer_member.fragments.size() >= 2,
          "outer background geometry retains separate line fragments when text wraps");
    check(inner_member.fragments.size() >= 2,
          "nested background geometry retains separate line fragments when text wraps");

    auto layers = LayerTreeBuilder(fixture.layers).build(*layout);
    const auto& commands = layers->display_list;
    const auto expected_backgrounds = outer_member.fragments.size() + inner_member.fragments.size();
    check(commands.size() == expected_backgrounds + outer_binding.snapshot->paragraph.fragments.size(),
          "per-line inline backgrounds are included in exact display command accounting");
    std::size_t fill_count = 0;
    while (fill_count < commands.size() && commands[fill_count].type() == DisplayCommandType::FillRect)
        ++fill_count;
    check(fill_count == expected_backgrounds,
          "all inline background fragments paint before text content");
    for (std::size_t i = 0; i < outer_member.fragments.size(); ++i) {
        const auto& command = commands[i];
        const auto expected = outer_member.fragments[i];
        check(command.rect.x == expected.x + host.rect.x + outer_binding.snapshot->content_offset_x &&
            command.rect.y == expected.y + host.rect.y + outer_binding.snapshot->content_offset_y &&
            command.rect.width == expected.width && command.rect.height == expected.height &&
            command.color.r == 255 && command.color.g == 0 && command.color.b == 0 &&
            command.color.a >= 127 && command.color.a <= 128,
            "outer scope emits translucent red backgrounds for each measured line fragment");
    }
    for (std::size_t i = 0; i < inner_member.fragments.size(); ++i) {
        const auto& command = commands[outer_member.fragments.size()+i];
        check(command.color.r == inner_member.proof.background_color.r &&
            command.color.g == inner_member.proof.background_color.g &&
            command.color.b == inner_member.proof.background_color.b &&
            command.color.a == inner_member.proof.background_color.a,
            "nested scope background follows the parent background in source nesting order");
    }
    LayerNode backgrounds_only;
    for (std::size_t i = 0; i < expected_backgrounds; ++i)
        backgrounds_only.display_list.push_back(commands[i]);
    SoftwareCompositor::Options paint;
    paint.text_system = &fixture.system;
    SoftwareCompositor compositor({}, paint);
    const auto background_pixels = compositor.render(backgrounds_only, 80, 100, {255,255,255,255});
    const auto red_pixel = background_pixels.pixel(39, 10);
    check(red_pixel.r == 255 && red_pixel.g >= 127 && red_pixel.g <= 128 &&
        red_pixel.b >= 127 && red_pixel.b <= 128,
        "translucent inline background is composited once over the underlying surface");

    const auto pixels = compositor.render(*layers, 80, 100, {255,255,255,255});
    check(!equal_pixels(pixels, FrameBuffer(80, 100, {255,255,255,255})),
          "inline scope backgrounds contribute visible pixels");
    FrameBuffer tiled(80, 100, {255,255,255,255});
    for (int y = 0; y < 100; y += 13) {
        Rect clip{0, y, 80, std::min(13, 100-y)};
        compositor.render_into(*layers, tiled, {255,255,255,255}, &clip, 1);
    }
    check(equal_pixels(pixels, tiled),
          "inline background and foreground packets compose identically across tiled clips");
    const auto damage = detail::shared_inline_damage_bounds(host);
    for (std::size_t i = 0; i < expected_backgrounds; ++i) {
        const auto& rect = commands[i].rect;
        check(rect.x >= damage.x && rect.y >= damage.y &&
            safe_edge(rect.x, rect.width) <= safe_edge(damage.x, damage.width) &&
            safe_edge(rect.y, rect.height) <= safe_edge(damage.y, damage.height),
            "shared paragraph damage covers every per-line inline background");
    }
    auto low_budget = fixture.layers;
    low_budget.max_display_commands = expected_backgrounds;
    auto limited = LayerTreeBuilder(low_budget).build(*layout);
    check(limited->display_list.empty(),
          "command budget below the complete background and text paragraph emits no partial output");
    inner.style.background_color = {0, 255, 0, 255};
    check(!detail::shared_inline_current(inner_binding, &fixture.system),
          "live inline background changes invalidate the frozen shared paragraph proof");
}
void propagated_inline_text_decorations() {
    Fixture fixture;
    const std::string css =
        "*{margin:0;padding:0;border:0}#p{display:block;width:32px;font-size:16px;line-height:20px;"
        "text-decoration:underline line-through;color:red}span{display:inline}";
    StyleResolver styles(CssParser{}.parse(css));
    auto dom = HtmlParser{}.parse(
        "<div id='p'><span>A A A A A A</span><span> B B</span></div>");
    auto layout = LayoutEngine(styles, {}, fixture.options).layout(*dom, 64, 100);
    const auto& host = find(*layout, "p");
    check(host.text_layout_cache.shared_inline != nullptr,
          "existing inherited underline and strike styles remain eligible for shared paragraphs");
    const auto& snapshot = *host.text_layout_cache.shared_inline->snapshot;
    std::size_t decorated_fragments = 0;
    for (const auto& fragment : snapshot.paragraph.fragments) {
        const auto owner = snapshot.owners[fragment.owner];
        const auto& proof = snapshot.members[owner].proof;
        if (proof.text_decoration_underline || proof.text_decoration_line_through)
            ++decorated_fragments;
    }
    check(decorated_fragments >= 2,
          "wrapping produces multiple text fragments carrying propagated decorations");
    auto layers = LayerTreeBuilder(fixture.layers).build(*layout);
    const auto& commands = layers->display_list;
    check(commands.size() == snapshot.paragraph.fragments.size() + decorated_fragments * 2,
          "one strike and one underline command are budgeted per decorated line fragment");
    std::size_t prepared = 0, fill = 0;
    std::vector<Rect> underline_rects;
    bool sequence_valid = true;
    bool underline_after_strike = true;
    for (std::size_t i = 0; i < commands.size(); ++i) {
        if (commands[i].type() == DisplayCommandType::PreparedText) {
            ++prepared;
            if (i + 2 >= commands.size() || commands[i + 1].type() != DisplayCommandType::FillRect ||
                commands[i + 2].type() != DisplayCommandType::FillRect) sequence_valid = false;
            else {
                ++fill;
                ++fill;
                underline_rects.push_back(commands[i + 2].rect);
                if (commands[i + 1].rect.y >= commands[i + 2].rect.y) underline_after_strike = false;
            }
        }
    }
    check(sequence_valid && prepared == decorated_fragments && fill == 2 * decorated_fragments,
          "foreground text is followed by strike and underline commands on each line");
    check(underline_after_strike,
          "underline is positioned below line-through within each line fragment");
    const auto damage = detail::shared_inline_damage_bounds(host);
    for (const auto& rect : underline_rects)
        check(rect.y >= damage.y && safe_edge(rect.y, rect.height) <= safe_edge(damage.y, damage.height),
              "paragraph damage includes propagated underline pixels below the glyph ink");

    SoftwareCompositor::Options paint;
    paint.text_system = &fixture.system;
    SoftwareCompositor compositor({}, paint);
    const auto pixels = compositor.render(*layers, 64, 100, {255,255,255,255});
    FrameBuffer tiled(64, 100, {255,255,255,255});
    for (int y = 0; y < 100; y += 9) {
        Rect clip{0, y, 64, std::min(9, 100-y)};
        compositor.render_into(*layers, tiled, {255,255,255,255}, &clip, 1);
    }
    check(equal_pixels(pixels, tiled),
          "decoration strokes render identically in full-frame and tiled passes");

    auto low_budget = fixture.layers;
    low_budget.max_display_commands = commands.size() - 1;
    auto limited = LayerTreeBuilder(low_budget).build(*layout);
    check(glyphs(*limited).empty(),
          "insufficient command budget refuses the complete decorated paragraph");
}
void shared_mixed_font_and_line_height_owners() {
    Fixture fixture;
    StyleResolver styles(CssParser{}.parse(
        "*{margin:0;padding:0;border:0}#p{display:block;width:100px;font-size:16px;line-height:20px;color:blue}"
        "span{display:inline}#large{font-size:24px;line-height:30px;font-weight:700;color:red}"
        "#small{font-size:12px;line-height:16px;color:green}"));
    auto dom = HtmlParser{}.parse(
        "<div id='p'><span id='large'>A</span><span> B</span><span id='small'>A</span><span> B</span></div>");
    auto layout = LayoutEngine(styles, {}, fixture.options).layout(*dom, 140, 80);
    const auto& host = find(*layout, "p");
    check(host.text_layout_cache.shared_inline != nullptr,
          "same-family mixed-size and mixed-line-height owners share a complete paragraph");
    const auto& large = find(*layout, "large");
    const auto& small = find(*layout, "small");
    const auto& large_proof = large.text_layout_cache.shared_inline->snapshot->members[
        large.text_layout_cache.shared_inline->member].proof;
    const auto& small_proof = small.text_layout_cache.shared_inline->snapshot->members[
        small.text_layout_cache.shared_inline->member].proof;
    check(large_proof.font.size == 24000 && large_proof.font.weight == 700 &&
        large_proof.line_height == 30 && large_proof.color.r == large.style.color.r &&
        small_proof.font.size == 12000 && small_proof.line_height == 16 &&
        small_proof.color.g == small.style.color.g,
        "each source owner retains its authored size, weight, line height and color proof");
    const auto& paragraph = *host.text_layout_cache.shared_inline->snapshot;
    check(paragraph.paragraph.fragments.size() >= 3 &&
        paragraph.paragraph.lines.size() == 1 && paragraph.paragraph.lines[0].height >= 30,
        "line strut accommodates the largest participating font and line height");
    const auto layers = LayerTreeBuilder(fixture.layers).build(*layout);
    std::size_t text_commands = 0;
    for (const auto& command : layers->display_list) {
        if (command.type() != DisplayCommandType::PreparedText) continue;
        ++text_commands;
        check(command.text_paint().baseline_y == paragraph.paragraph.lines[0].baseline,
              "mixed-font owner packets use the shared line baseline");
    }
    check(text_commands == paragraph.paragraph.fragments.size(),
          "all mixed-font source fragments are emitted once");
    check(detail::shared_inline_current(*host.text_layout_cache.shared_inline, &fixture.system),
          "the complete mixed-font source and style proof is current");

    auto ordinary_options = fixture.options;
    ordinary_options.shared_inline.max_attempts = 0;
    auto ordinary_dom = HtmlParser{}.parse(
        "<div id='p'><span id='large'>A</span><span> B</span><span id='small'>A</span><span> B</span></div>");
    auto ordinary = LayoutEngine(styles, {}, ordinary_options).layout(*ordinary_dom, 140, 80);
    check(find(*ordinary, "p").text_layout_cache.shared_inline == nullptr,
          "optimization-disabled tree remains an independent ordinary-layout control");
}
void preserved_whitespace_shared_paragraph() {
    Asset font = asset(400, 16, 4, 1, {{32,250},{48,500},{65,500},{66,600}});
    TextFontFace face{1, 0, font.font};
    TextSystem system;
    LayoutEngineOptions options;
    LayerTreeBuilderOptions layers;
    check(system.bind(&face, 1) == TextSystemStatus::Ok, "bind preserved-text font fixture");
    options.text_system = &system;
    layers.text_system = &system;
    StyleResolver styles(CssParser{}.parse(
        "*{margin:0;padding:0;border:0}pre{display:block;width:120px;font-size:16px;line-height:20px}"
        "span{display:inline}"));
    auto dom = HtmlParser{}.parse(
        "<pre id='p'>A\tB\r<span>\nA\n</span>\nB  </pre>");
    auto& pre = find_node(*dom, "p");
    check(pre.children.size() == 3 && pre.children[1]->tag_name == "span" &&
        pre.children[1]->children.size() == 1,
        "parser fixture has text owners on both sides of the inline span");
    pre.children[0]->set_text("A\tB\r");
    pre.children[1]->children[0]->set_text("\nA\n");
    pre.children[2]->set_text("\nB  ");
    auto layout = LayoutEngine(styles, {}, options).layout(*dom, 140, 120);
    const auto& host = find(*layout, "p");
    check(host.text_layout_cache.shared_inline != nullptr,
          "uniformly preserved ASCII whitespace uses the shared paragraph path");
    const auto& snapshot = *host.text_layout_cache.shared_inline->snapshot;
    check(snapshot.preserves_whitespace &&
        snapshot.paragraph.normalized == "A\tB\nA\n\nB  ",
        "tab, cross-owner CRLF, blank lines and trailing spaces retain their source policy");
    check(snapshot.paragraph.lines.size() == 4,
          "CRLF forms one hard break while consecutive LF preserves an empty line");
    check(snapshot.paragraph.height == 80,
          "preserved hard breaks contribute one line strut each");
    check(detail::shared_inline_current(*host.text_layout_cache.shared_inline, &system),
          "preserved source ownership and policy proof remain current");

    auto painted = LayerTreeBuilder(layers).build(*layout);
    SoftwareCompositor::Options paint;
    paint.text_system = &system;
    SoftwareCompositor compositor({}, paint);
    const auto pixels = compositor.render(*painted, 140, 120, {255,255,255,255});
    FrameBuffer tiled(140, 120, {255,255,255,255});
    for (int y = 0; y < 120; y += 11) {
        Rect clip{0, y, 140, std::min(11, 120-y)};
        compositor.render_into(*painted, tiled, {255,255,255,255}, &clip, 1);
    }
    check(equal_pixels(pixels, tiled),
          "preserved whitespace packets replay identically across tiled clips");

    auto unsupported_dom = HtmlParser{}.parse("<pre id='p'>A\fB</pre>");
    auto unsupported = LayoutEngine(styles, {}, options).layout(*unsupported_dom, 140, 120);
    check(find(*unsupported, "p").text_layout_cache.shared_inline == nullptr,
          "unsupported preserved form-feed text falls back as a complete paragraph");

    auto markers_dom = HtmlParser{}.parse("<pre id='p'>A<wbr>B</pre>");
    auto markers = LayoutEngine(styles, {}, options).layout(*markers_dom, 140, 120);
    const auto& marker_host = find(*markers, "p");
    check(marker_host.text_layout_cache.shared_inline != nullptr,
          "wbr does not conflict with preserved text mode");
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
    int failures=0;
#if defined(NDEBUG)
    bool success=false;
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
#else
    int height=0;
    check(detail::layout_shared_inline(host,host.rect.x,host.rect.y,25,f.options,height),
        "debug build publishes shared paragraph without allocator fault injection");
#endif
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
        definite_inline_image_atomic_path();
        definite_inline_image_refusal_and_style_proof();
        definite_inline_image_percent_width();
        natural_inline_image_metadata_path();
        sharp_text_shadow_parity_and_bounds();
        inline_scope_backgrounds();
        propagated_inline_text_decorations();
        shared_mixed_font_and_line_height_owners();
        preserved_whitespace_shared_paragraph();
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
