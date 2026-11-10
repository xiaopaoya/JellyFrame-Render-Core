#include <render_core/css_parser.h>
#include <render_core/feature_config.h>
#include <render_core/font_resource.h>
#include <render_core/html_parser.h>
#include <render_core/layout.h>
#include <render_core/text_backend.h>
#include <render_core/text_system.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace jellyframe;

namespace {
using Bytes = std::vector<std::uint8_t>;

void put16(Bytes& bytes, std::size_t at, unsigned value) {
    bytes[at] = static_cast<std::uint8_t>(value & 255U);
    bytes[at + 1] = static_cast<std::uint8_t>((value >> 8U) & 255U);
}

void put32(Bytes& bytes, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes[at + i] = static_cast<std::uint8_t>(value >> (8U * i));
}

struct FontAsset {
    Bytes bytes;
    FontResource font;
};

FontAsset make_font() {
    constexpr std::pair<unsigned, unsigned> glyphs[] = {{32, 250}, {65, 500}, {66, 600},
        {67, 400}, {68, 300}};
    FontAsset asset;
    const std::size_t bitmap_offset = 128 + std::size(glyphs) * 40;
    asset.bytes.resize(bitmap_offset, 0);
    auto& bytes = asset.bytes;
    const char magic[] = "JFFONT2";
    for (unsigned i = 0; i < 8; ++i) bytes[i] = static_cast<std::uint8_t>(magic[i]);
    put16(bytes, 8, 2); put16(bytes, 10, 128); put16(bytes, 12, 40); put16(bytes, 14, 1);
    put32(bytes, 20, static_cast<std::uint32_t>(std::size(glyphs)));
    put32(bytes, 24, 128); put32(bytes, 28, static_cast<std::uint32_t>(bitmap_offset));
    put16(bytes, 36, 1000); put16(bytes, 38, 16); put16(bytes, 40, 400); bytes[42] = 4;
    put32(bytes, 44, 800); put32(bytes, 48, 200); put32(bytes, 52, 50); bytes[64] = 1;
    put32(bytes, 100, 1); put16(bytes, 104, 1);
    for (std::size_t i = 0; i < std::size(glyphs); ++i) {
        const auto at = 128 + i * 40;
        put32(bytes, at, glyphs[i].first);
        put32(bytes, at + 4, static_cast<std::uint32_t>(i + 1));
        put32(bytes, at + 8, glyphs[i].second);
        put32(bytes, at + 28, static_cast<std::uint32_t>(bytes.size() - bitmap_offset));
        if (glyphs[i].first != 32) {
            put32(bytes, at + 12, static_cast<std::uint32_t>(-1));
            put32(bytes, at + 16, static_cast<std::uint32_t>(-2));
            put16(bytes, at + 20, 1); put16(bytes, at + 22, 2); put32(bytes, at + 24, 1);
            put32(bytes, at + 32, 2); bytes.push_back(0xf0); bytes.push_back(0xf0);
        }
    }
    put32(bytes, 16, static_cast<std::uint32_t>(bytes.size()));
    put32(bytes, 32, static_cast<std::uint32_t>(bytes.size() - bitmap_offset));
    std::uint32_t crc = 0xffffffffU;
    for (const auto value : bytes) {
        crc ^= value;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1U) ^ ((crc & 1U) ? 0xedb88320U : 0U);
    }
    put32(bytes, 96, ~crc);
    if (asset.font.load(bytes.data(), bytes.size()) != FontResourceStatus::Loaded)
        throw std::runtime_error("Deterministic package font failed to load");
    return asset;
}

const LayoutBox* find_box(const LayoutBox& root, const char* id) {
    if (root.node && root.node->attribute("id") == std::string(id)) return &root;
    for (const auto& child : root.children)
        if (const auto* found = find_box(*child, id)) return found;
    return nullptr;
}

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

bool same_font_measure(const std::string& text, int size, int weight,
                      std::uint32_t family_hash, TextMetrics* output, void* context) {
    auto& system = *static_cast<TextSystem*>(context);
    std::uint32_t family = 0;
    if (family_hash != 0 && !system.resolve_family_hash(family_hash, family)) return false;
    const TextFontRequest request{&family, 1, size * 1000, static_cast<std::uint16_t>(weight)};
    TextRunMetrics metrics;
    if (system.measure(text, request, metrics) != TextSystemStatus::Ok) return false;
    output->width = (metrics.advance + 999) / 1000;
    output->line_height = (metrics.primary_font.line_height + 999) / 1000;
    return true;
}

struct LayoutResult {
    std::unique_ptr<Node> document;
    LayoutBoxPtr boxes;
};

LayoutResult layout(const std::string& html, const std::string& css,
                    TextSystem* text_system, bool enable_intrinsic = true) {
    LayoutResult result;
    result.document = HtmlParser{}.parse(html);
    require(static_cast<bool>(result.document), "HTML fixture parse failed");
    StyleResolver styles(CssParser{}.parse(
        "*{margin:0;padding:0;border:0}body{margin:0}" + css));
    TextMeasureProvider provider;
    provider.measure_family = same_font_measure;
    provider.context = text_system;
    LayoutEngineOptions options;
    options.text_system = enable_intrinsic ? text_system : nullptr;
    result.boxes = LayoutEngine(styles, provider, options).layout(*result.document, 120, 80);
    return result;
}

void check_auto_min(TextSystem& text_system) {
    const auto result = layout("<body><div id='p'><div id='item'>AB CD</div></div></body>",
        "#p{display:flex;width:5px;align-items:flex-start}"
        "#item{display:block;flex:0 1 0px;font-family:'Core Test';font-size:16px}", &text_system);
    const auto* item = find_box(*result.boxes, "item");
    require(item && item->rect.width == 18,
            (std::string("Installed Flex auto minimum expected 18px, got ") +
                (item ? std::to_string(item->rect.width) : "missing item")).c_str());
}

void check_percentage_minimum(TextSystem& text_system) {
    const auto result = layout("<body><div id='p'><div id='item'>AB</div></div></body>",
        "#p{display:flex;width:5px;align-items:flex-start}"
        "#item{display:block;flex:0 1 0px;min-width:50%;font-family:'Core Test';font-size:16px}",
        &text_system);
    const auto* item = find_box(*result.boxes, "item");
    require(item && item->rect.width == 3,
            "Installed Flex min-width percentage must resolve against the content width");
}

void check_grid_max_content(TextSystem& text_system) {
    const auto result = layout("<body><main id='grid'><div id='item'>AB</div><span id='fill'></span></main></body>",
        "#grid{display:grid;width:100px;grid-template-columns:auto 1fr}"
        "#item{display:block;font-family:'Core Test';font-size:16px}", &text_system);
    const auto* item = find_box(*result.boxes, "item");
    const auto* fill = find_box(*result.boxes, "fill");
    require(item && fill && item->rect.width == 18 && fill->rect.x >= item->rect.x + 18,
            "Installed Grid auto track must consume the complete Core max-content width");
}

void check_calc_refusal(TextSystem& text_system) {
    const std::string html = "<body><div id='p'><div id='item'>AB CD</div></div></body>";
    const std::string css = "#p{display:flex;width:5px;align-items:flex-start}"
        "#item{display:block;flex:0 1 0px;min-width:calc(20px + 50%);"
        "font-family:'Core Test';font-size:16px}";
    const auto queried = layout(html, css, &text_system);
    const auto legacy = layout(html, css, &text_system, false);
    const auto* queried_parent = find_box(*queried.boxes, "p");
    const auto* legacy_parent = find_box(*legacy.boxes, "p");
    const auto* queried_item = find_box(*queried.boxes, "item");
    const auto* legacy_item = find_box(*legacy.boxes, "item");
    require(queried_parent && legacy_parent && queried_item && legacy_item &&
            queried_parent->rect.x == legacy_parent->rect.x &&
            queried_parent->rect.y == legacy_parent->rect.y &&
            queried_parent->rect.width == legacy_parent->rect.width &&
            queried_parent->rect.height == legacy_parent->rect.height &&
            queried_item->rect.x == legacy_item->rect.x &&
            queried_item->rect.y == legacy_item->rect.y &&
            queried_item->rect.width == legacy_item->rect.width &&
            queried_item->rect.height == legacy_item->rect.height,
            "Unsupported computed calc min-width must retain whole Flex legacy geometry");
}
} // namespace

int main() {
    try {
        auto asset = make_font();
        TextFontFace faces[] = {
            {1, 17, asset.font, normalized_font_family_hash("Core Test")},
            {2, 0, asset.font, 0},
        };
        TextSystem text_system;
        require(text_system.bind(faces, std::size(faces)) == TextSystemStatus::Ok,
                "Installed consumer TextSystem bind failed");

#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
        check_auto_min(text_system);
        check_percentage_minimum(text_system);
        check_grid_max_content(text_system);
        check_calc_refusal(text_system);
        std::cout << "Installed ABI4 Flex auto-min, percentage min, Grid max-content and calc refusal passed\n";
#else
        std::cout << "Installed ABI4 cropped profile has Flex/Grid disabled; package and font binding passed\n";
#endif
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
