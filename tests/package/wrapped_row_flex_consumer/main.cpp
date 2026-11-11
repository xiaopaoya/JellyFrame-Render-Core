#include "render_core/css_parser.h"
#include "render_core/html_parser.h"
#include "render_core/layout.h"
#include "render_core/text_backend.h"


#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace jellyframe;
namespace {
using Bytes = std::vector<std::uint8_t>;
void put16(Bytes& bytes, std::size_t at, unsigned value) {
    bytes[at] = static_cast<std::uint8_t>(value);
    bytes[at + 1] = static_cast<std::uint8_t>(value >> 8);
}
void put32(Bytes& bytes, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
}

struct FontAsset {
    Bytes bytes;
    FontResource font;
};

FontAsset make_font() {
    std::array<std::pair<unsigned, unsigned>, 95> glyphs{};
    for (unsigned i = 0; i < glyphs.size(); ++i)
        glyphs[i] = {32 + i, i == 0 ? 250U : 500U};
    FontAsset asset;
    const auto bitmap = 128 + glyphs.size() * 40;
    asset.bytes.resize(bitmap, 0);
    auto& bytes = asset.bytes;
    const char magic[] = "JFFONT2";
    for (unsigned i = 0; i < 8; ++i) bytes[i] = static_cast<std::uint8_t>(magic[i]);
    put16(bytes, 8, 2); put16(bytes, 10, 128); put16(bytes, 12, 40); put16(bytes, 14, 1);
    put32(bytes, 20, static_cast<std::uint32_t>(glyphs.size()));
    put32(bytes, 24, 128); put32(bytes, 28, static_cast<std::uint32_t>(bitmap));
    put16(bytes, 36, 1000); put16(bytes, 38, 16); put16(bytes, 40, 400); bytes[42] = 4;
    put32(bytes, 44, 800); put32(bytes, 48, 200); put32(bytes, 52, 50); bytes[64] = 1;
    put32(bytes, 100, 1); put16(bytes, 104, 1);
    for (std::size_t i = 0; i < glyphs.size(); ++i) {
        const auto at = 128 + i * 40;
        put32(bytes, at, glyphs[i].first); put32(bytes, at + 4, static_cast<std::uint32_t>(i + 1));
        put32(bytes, at + 8, glyphs[i].second); put32(bytes, at + 28, static_cast<std::uint32_t>(bytes.size() - bitmap));
        if (glyphs[i].first != 32) {
            put32(bytes, at + 12, static_cast<std::uint32_t>(-1));
            put32(bytes, at + 16, static_cast<std::uint32_t>(-2));
            put16(bytes, at + 20, 1); put16(bytes, at + 22, 2); put32(bytes, at + 24, 1);
            put32(bytes, at + 32, 2); bytes.push_back(0xf0); bytes.push_back(0xf0);
        }
    }
    put32(bytes, 16, static_cast<std::uint32_t>(bytes.size()));
    put32(bytes, 32, static_cast<std::uint32_t>(bytes.size() - bitmap));
    std::uint32_t crc = 0xffffffffU;
    for (auto value : bytes) {
        crc ^= value;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xedb88320U : 0U);
    }
    put32(bytes, 96, ~crc);
    if (asset.font.load(bytes.data(), bytes.size()) != FontResourceStatus::Loaded) throw std::runtime_error("font load failed");
    return asset;
}


void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}
const LayoutBox& find(const LayoutBox& box, const std::string& id) {
    if (box.node && box.node->attribute("id") == id) return box;
    for (const auto& child : box.children) {
        try { return find(*child, id); } catch (const std::out_of_range&) {}
    }
    throw std::out_of_range("missing box " + id);
}
void expect(const LayoutBox& root, const char* id, int x, int y, int width, int height) {
    const auto& r = find(root, id).rect;
    std::cout << id << '=' << r.x << ',' << r.y << ',' << r.width << ',' << r.height << ' ';
    require(r.x == x && r.y == y && r.width == width && r.height == height,
            std::string("unexpected geometry for ") + id);
}
bool measure_with_core(const std::string& text, int size, int weight,
                       TextMetrics* metrics, void* context) {
    TextFontRequest request;
    request.size_q1000 = size * 1000;
    request.weight = static_cast<std::uint16_t>(weight);
    TextRunMetrics run;
    if (static_cast<TextSystem*>(context)->measure(text, request, run) != TextSystemStatus::Ok)
        return false;
    metrics->width = static_cast<int>((run.advance + 999) / 1000);
    metrics->line_height = static_cast<int>((run.natural_height + 999) / 1000);
    return true;
}
struct Scene {
    std::unique_ptr<Node> document;
    LayoutBoxPtr tree;
};
Scene layout(const std::string& items, const std::string& rules, TextSystem& system, bool core, LayoutIntrinsicQueryLimits limits = {}, LayoutIntrinsicQueryStatistics* statistics = nullptr) {
    Scene scene;
    scene.document = HtmlParser{}.parse("<body><main id='p'>" + items + "</main></body>");
    require(scene.document != nullptr, "HTML parses");
    StyleResolver styles(CssParser{}.parse(
        "*{margin:0;padding:0;border:0}"
        "#p{display:flex;flex-direction:row;flex-wrap:wrap;width:32px;align-items:flex-start}"
        "div{font-family:'Core Test';font-size:16px;line-height:20px;flex:0 1 auto}" + rules));
    LayoutEngineOptions options;
    options.text_system = &system;
    options.intrinsic_query_limits = limits;
    if (!core) options.intrinsic_query_limits.max_work = 0;
    options.intrinsic_query_statistics = statistics;
    TextMeasureProvider host;
    host.measure = measure_with_core;
    host.context = &system;
    scene.tree = LayoutEngine(styles, host, options).layout(*scene.document, 200, 300);
    require(scene.tree != nullptr, "layout returns a tree");
    return scene;
}
void compare_trees(const LayoutBox& core, const LayoutBox& legacy) {
    const auto& a = core.rect; const auto& b = legacy.rect;
    require(a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height,
            "whole-tree same-font fallback geometry differs");
    require(core.children.size() == legacy.children.size(), "fallback tree shape differs");
    for (std::size_t i = 0; i < core.children.size(); ++i)
        compare_trees(*core.children[i], *legacy.children[i]);
}
void check_core_cache(const LayoutBox& box, const TextSystem& system) {
    if (box.node && box.node->type == NodeType::Text) {
        require(box.text_layout_cache.valid && box.text_layout_cache.prepared_text &&
                    box.text_layout_cache.text_system == &system &&
                    box.text_layout_cache.text_system_generation == system.generation(),
                "Core text cache is not usable after layout");
    }
    for (const auto& child : box.children) check_core_cache(*child, system);
}
const std::string pair = "<div id='a'>AB</div><div id='b'>CD</div>";
const std::string four = pair + "<div id='c'>EF</div><div id='d'>GH</div>";
} // namespace

int main() {
    try {
        auto asset = make_font();
        std::array<TextFontFace, 2> faces = {{{1, 17, asset.font,
            normalized_font_family_hash("Core Test")}, {2, 0, asset.font, 0}}};
        TextSystem system;
        require(system.bind(faces.data(), faces.size()) == TextSystemStatus::Ok, "font set binds");
        LayoutIntrinsicQueryStatistics stats;
        const auto exact = layout(pair, "", system, true, {}, &stats);
        const auto legacy = layout(pair, "", system, false);
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
        expect(*exact.tree, "a", 0, 0, 16, 20);
        expect(*exact.tree, "b", 16, 0, 16, 20);
        require(find(*legacy.tree, "a").rect.width == 32 && stats.queries == 4,
            "installed package actually executes the new wrapped planner");
        const auto below = layout(pair, "#p{width:31.999px}", system, true);
        expect(*below.tree, "b", 0, 20, 16, 20);
        const auto grow = layout(pair + "<div id='c'>EF</div>",
            "#p{width:40px;column-gap:4px;row-gap:2px}div{flex-grow:1}", system, true);
        expect(*grow.tree, "a", 0, 0, 18, 20);
        expect(*grow.tree, "b", 22, 0, 18, 20);
        expect(*grow.tree, "c", 0, 22, 40, 20);
        require(stats.peak_scratch_bytes <= 32768 && stats.work > 1, "installed limits bound planner scratch/work");
        LayoutIntrinsicQueryLimits short_work; short_work.max_work = stats.work - 1;
        const auto refused = layout(pair, "", system, true, short_work);
        compare_trees(*refused.tree, *legacy.tree);
        check_core_cache(*refused.tree, system);
#else
        compare_trees(*exact.tree, *legacy.tree);
        require(stats.queries == 0, "cropped package performs no Flex source queries");
#endif
        check_core_cache(*exact.tree, system);
        std::cout << "installed wrapped-row Flex contract passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}


