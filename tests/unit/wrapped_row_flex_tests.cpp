#include "render_core/css_parser.h"
#include "render_core/html_parser.h"
#include "render_core/layout.h"
#include "render_core/text_backend.h"
#include "../../src/layout_mutation_journal.h"

#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Deterministic cancellation during final child publication: identify the
// journal's exact snapshot allocation, then count subsequent allocations.
// No allocation fails and the hook never allocates or inspects live objects.
static std::size_t snapshot_allocation = 0, after_snapshot = 0, cancel_after = 0;
static bool snapshot_seen = false;
static std::atomic_bool* allocation_cancellation = nullptr;
void* operator new(std::size_t bytes) {
    if (snapshot_allocation != 0 && bytes == snapshot_allocation) {
        snapshot_seen = true; after_snapshot = 0;
    } else if (snapshot_seen && snapshot_allocation != 0) {
        ++after_snapshot;
        if (allocation_cancellation && after_snapshot == cancel_after)
            allocation_cancellation->store(true, std::memory_order_relaxed);
    }
    if (void* result = std::malloc(bytes ? bytes : 1)) return result;
    throw std::bad_alloc();
}
void operator delete(void* data) noexcept { std::free(data); }
void operator delete(void* data, std::size_t) noexcept { std::free(data); }
// Route array allocations through the same deterministic hook, including ASan.
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete[](void* data) noexcept { ::operator delete(data); }
void operator delete[](void* data, std::size_t) noexcept { ::operator delete(data); }

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
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED

        auto exact = layout(pair, "", system, true);
        std::cout << "exact threshold: ";
        expect(*exact.tree, "a", 0, 0, 16, 20);
        expect(*exact.tree, "b", 16, 0, 16, 20);
        expect(*exact.tree, "p", 0, 0, 32, 20); std::cout << '\n';
        check_core_cache(*exact.tree, system);
        auto legacy_pair = layout(pair, "", system, false);
        require(find(*legacy_pair.tree, "a").rect.width == 32 &&
                find(*legacy_pair.tree, "b").rect.y == 20,
                "legacy control must distinguish active intrinsic geometry");

        auto below = layout(pair, "#p{width:31.999px}", system, true);
        std::cout << "one Q1000 unit below: ";
        expect(*below.tree, "a", 0, 0, 16, 20);
        expect(*below.tree, "b", 0, 20, 16, 20);
        expect(*below.tree, "p", 0, 0, 32, 40); std::cout << '\n';

        const std::string cross_rules = "#p{width:36px;column-gap:4px;row-gap:3px}"
            "#b{line-height:24px}#c{line-height:16px}#d{line-height:40px}";
        auto two = layout(four, cross_rules, system, true);
        std::cout << "two rows with different cross sizes: ";
        expect(*two.tree, "a", 0, 0, 16, 20);
        expect(*two.tree, "b", 20, 0, 16, 24);
        expect(*two.tree, "c", 0, 27, 16, 16);
        expect(*two.tree, "d", 20, 27, 16, 40);
        expect(*two.tree, "p", 0, 0, 36, 67); std::cout << '\n';
        check_core_cache(*two.tree, system);

        // A definite basis gives the same line geometry in both engines.
        auto definite = layout(four, cross_rules + "div{flex-basis:16px}", system, true);
        auto definite_legacy = layout(four, cross_rules + "div{flex-basis:16px}", system, false);
        compare_trees(*definite.tree, *definite_legacy.tree);
        expect(*definite.tree, "p", 0, 0, 36, 67);
        std::cout << "definite-basis same-font geometry agrees\n";

        auto three = layout(four + "<div id='e'>IJ</div>",
                            "#p{width:36px;column-gap:4px;row-gap:3px}", system, true);
        std::cout << "three rows: ";
        expect(*three.tree, "a", 0, 0, 16, 20);
        expect(*three.tree, "b", 20, 0, 16, 20);
        expect(*three.tree, "c", 0, 23, 16, 20);
        expect(*three.tree, "d", 20, 23, 16, 20);
        expect(*three.tree, "e", 0, 46, 16, 20);
        expect(*three.tree, "p", 0, 0, 36, 66); std::cout << '\n';

        auto grow = layout(pair + "<div id='c'>EF</div>",
                           "#p{width:40px;column-gap:4px;row-gap:2px}div{flex-grow:1}", system, true);
        std::cout << "independent line growth: ";
        expect(*grow.tree, "a", 0, 0, 18, 20);
        expect(*grow.tree, "b", 22, 0, 18, 20);
        expect(*grow.tree, "c", 0, 22, 40, 20);
        expect(*grow.tree, "p", 0, 0, 40, 42); std::cout << '\n';
        const auto freeze = layout(pair + "<div id='c'>EF</div>",
            "#p{width:40px;column-gap:4px;row-gap:2px}div{flex:1 1 16px;min-width:0}#a{max-width:17px}", system, true);
        expect(*freeze.tree, "a", 0, 0, 17, 20);
        expect(*freeze.tree, "b", 21, 0, 19, 20);
        expect(*freeze.tree, "c", 0, 22, 40, 20);
        const auto decorated = layout(pair,
            "#p{width:48px;column-gap:4px}div{padding:1px;border:2px solid black;align-self:stretch}", system, true);
        expect(*decorated.tree, "a", 0, 0, 22, 26);
        expect(*decorated.tree, "b", 26, 0, 22, 26);
        expect(*decorated.tree, "p", 0, 0, 48, 26);

        auto refused = layout(four, "#d{white-space:nowrap}", system, true);
        auto legacy = layout(four, "#d{white-space:nowrap}", system, false);
        compare_trees(*refused.tree, *legacy.tree);
        std::cout << "unsupported fourth sibling fallback: ";
        expect(*refused.tree, "a", 0, 0, 32, 20);
        expect(*refused.tree, "b", 0, 20, 32, 20);
        expect(*refused.tree, "c", 0, 40, 32, 20);
        expect(*refused.tree, "d", 0, 60, 32, 20);
        expect(*refused.tree, "p", 0, 0, 32, 80); std::cout << '\n';
        check_core_cache(*refused.tree, system);

        // Each source is below 8192 bytes. Three siblings exceed the shared
        // source-byte allowance while collapsed output remains two glyphs.
        const std::string padded = std::string(3000, ' ') + "AB";
        const std::string padded_items = "<div id='a'>" + padded + "</div><div id='b'>" +
            padded + "</div><div id='c'>" + padded + "</div>";
        auto individual = layout("<div id='a'>" + padded + "</div>", "", system, true);
        expect(*individual.tree, "a", 0, 0, 16, 20); std::cout << "single padded item admitted\n";
        auto exhausted = layout(padded_items, "", system, true);
        auto exhausted_legacy = layout(padded_items, "", system, false);
        compare_trees(*exhausted.tree, *exhausted_legacy.tree);
        std::cout << "aggregate source-byte fallback: ";
        expect(*exhausted.tree, "a", 0, 0, 32, 20);
        expect(*exhausted.tree, "b", 0, 20, 32, 20);
        expect(*exhausted.tree, "c", 0, 40, 32, 20);
        expect(*exhausted.tree, "p", 0, 0, 32, 60); std::cout << '\n';
        check_core_cache(*exhausted.tree, system);

        // The final charge follows all child layouts. One unit less therefore
        // exercises rollback of complete geometry and prepared caches, followed
        // by a clean ordinary run over the same tree.
        LayoutIntrinsicQueryStatistics complete_stats;
        const auto complete = layout(four, cross_rules, system, true, {}, &complete_stats);
        require(complete_stats.queries == 8 && complete_stats.work > 1 &&
            complete_stats.peak_scratch_bytes <= 32768, "planner shares bounded width/height query accounting");
        LayoutIntrinsicQueryLimits exact_work;
        exact_work.max_work = complete_stats.work;
        const auto exact_cap = layout(four, cross_rules, system, true, exact_work);
        compare_trees(*complete.tree, *exact_cap.tree);
        exact_work.max_work -= 1;
        LayoutIntrinsicQueryStatistics short_stats;
        const auto late = layout(four, cross_rules, system, true, exact_work, &short_stats);
        const auto ordinary = layout(four, cross_rules, system, false);
        compare_trees(*late.tree, *ordinary.tree);
        check_core_cache(*late.tree, system);
        require(short_stats.queries == 8 && short_stats.work <= exact_work.max_work,
            "late commit refusal retains monotonic query/work ledgers");
        for (std::size_t cap : {std::size_t{1}, complete_stats.peak_scratch_bytes - 1}) {
            LayoutIntrinsicQueryLimits scratch; scratch.max_scratch_bytes = cap;
            LayoutIntrinsicQueryStatistics stats;
            const auto refused_scratch = layout(four, cross_rules, system, true, scratch, &stats);
            compare_trees(*refused_scratch.tree, *ordinary.tree);
            require(stats.peak_scratch_bytes <= cap, "scratch refusal never exceeds configured payload");
        }
        std::atomic_bool cancellation{true};
        LayoutIntrinsicQueryLimits cancel; cancel.cancellation = &cancellation;
        const auto cancelled = layout(four, cross_rules, system, true, cancel);
        compare_trees(*cancelled.tree, *ordinary.tree);
        check_core_cache(*cancelled.tree, system);
        // Calibrate one identical child's publication allocation count, then
        // cancel after that complete child's publication work. A later charged
        // boundary must rollback at least one completely published child.
#if !defined(_MSC_VER) || _ITERATOR_DEBUG_LEVEL == 0
        snapshot_allocation = detail::LayoutMutationJournal::capture_scratch_bytes(3) - 3 * sizeof(const LayoutBox*);
        snapshot_seen = false;
        const auto one_for_calibration = layout("<div>AB</div>", "", system, true);
        require(snapshot_seen && after_snapshot > 0, "single child journal allocation is observable");
        const auto per_child_allocations = after_snapshot;
        snapshot_allocation = detail::LayoutMutationJournal::capture_scratch_bytes(9) - 9 * sizeof(const LayoutBox*);
        snapshot_seen = false;
        const auto four_for_calibration = layout(four, "", system, true);
        require(snapshot_seen && after_snapshot > per_child_allocations + 1,
            "multiple children expose allocations after a complete single child");
        cancellation.store(false);
        allocation_cancellation = &cancellation;
        cancel_after = per_child_allocations + 1;
        snapshot_seen = false;
        const auto during_publication = layout(four, "", system, true, cancel);
        snapshot_allocation = 0; allocation_cancellation = nullptr;
        require(cancellation.load(), "cancellation occurs after one child publication");
        const auto publication_control = layout(four, "", system, false);
        compare_trees(*during_publication.tree, *publication_control.tree);
        check_core_cache(*during_publication.tree, system);
#endif

        // Wrapping uses hypothetical outer sizes before independent line
        // shrink; only an oversized first item may shrink on its own line.
        const auto shrink = layout(pair, "#p{width:10px;row-gap:2px}div{min-width:0;flex:0 1 16px}", system, true);
        expect(*shrink.tree, "a", 0, 0, 10, 20);
        expect(*shrink.tree, "b", 0, 22, 10, 20);
        const auto automatic = layout(pair, "#p{width:10px;row-gap:2px}div{flex:0 1 16px}", system, true);
        expect(*automatic.tree, "a", 0, 0, 16, 20);
        expect(*automatic.tree, "b", 0, 22, 16, 20);
        const auto order = layout(pair, "#b{order:-1}", system, true);
        expect(*order.tree, "b", 0, 0, 16, 20);
        expect(*order.tree, "a", 16, 0, 16, 20);
        for (const auto alignment : {std::string("center"), std::string("flex-end"), std::string("stretch")}) {
            const auto cross = layout(pair, "#p{align-items:" + alignment + "}#b{line-height:24px}", system, true);
            expect(*cross.tree, "a", 0, alignment == "center" ? 2 : alignment == "flex-end" ? 4 : 0,
                16, alignment == "stretch" ? 24 : 20);
            expect(*cross.tree, "b", 16, 0, 16, 24);
        }
        const auto margin = layout(pair,
            "#p{width:54px;column-gap:4px}div{padding:1px;border:2px solid black;margin:1px 2px 2px 1px}", system, true);
        expect(*margin.tree, "a", 1, 1, 22, 26);
        expect(*margin.tree, "b", 30, 1, 22, 26);
        expect(*margin.tree, "p", 0, 0, 54, 29);
        const auto origin = layout(pair, "body{width:100.5px}#p{margin-left:auto;margin-right:auto}", system, true);
        require(find(*origin.tree, "a").content_geometry.x == 34250 &&
            find(*origin.tree, "b").content_geometry.x == 50250,
            "fractional containing origin remains Q1000 through final wrapped publication");
        const auto unsupported = layout(four, cross_rules + "#d{position:relative}", system, true);
        const auto unsupported_control = layout(four, cross_rules + "#d{position:relative}", system, false);
        compare_trees(*unsupported.tree, *unsupported_control.tree);
        const auto fractional_selection = layout(pair,
            "#p{width:33px}div{flex:1 1 16px}", system, true);
        const auto fractional_selection_control = layout(pair,
            "#p{width:33px}div{flex:1 1 16px}", system, false);
        compare_trees(*fractional_selection.tree, *fractional_selection_control.tree);
        LayoutIntrinsicQueryLimits shallow; shallow.max_depth = 2;
        const auto depth_refused = layout(four, cross_rules, system, true, shallow);
        compare_trees(*depth_refused.tree, *ordinary.tree);
        std::string bounded_many;
        for (int index = 0; index < 32; ++index) bounded_many += "<div>AB</div>";
        LayoutIntrinsicQueryStatistics many_stats;
        const auto many = layout(bounded_many, "", system, true, {}, &many_stats);
        require(find(*many.tree, "p").rect.height == 320 && many_stats.queries == 64 &&
            many_stats.peak_scratch_bytes <= 32768, "32 Core items complete under aggregate scratch/query defaults");
        std::string excessive;
        for (int index = 0; index < 129; ++index) excessive += "<div>AB</div>";
        const auto too_many = layout(excessive, "", system, true);
        const auto too_many_control = layout(excessive, "", system, false);
        compare_trees(*too_many.tree, *too_many_control.tree);
#else
        const auto cropped = layout(pair, "", system, true);
        const auto control = layout(pair, "", system, false);
        compare_trees(*cropped.tree, *control.tree);
        check_core_cache(*cropped.tree, system);
#endif
        std::cout << "bounded wrapped-row Flex tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}

