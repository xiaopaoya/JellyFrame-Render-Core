#include <render_core/css_parser.h>
#include <render_core/computed_size_storage.h>
#include <render_core/diagnostics.h>
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

RenderObject* find_object(RenderObject& root, const char* id) {
    if (root.node && root.node->attribute("id") == std::string(id)) return &root;
    for (const auto& child : root.children)
        if (auto* found = find_object(*child, id)) return found;
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
                    TextSystem* text_system, bool enable_intrinsic = true,
                    LayoutIntrinsicQueryLimits limits = {}, LayoutIntrinsicQueryStatistics* statistics = nullptr) {
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
    options.intrinsic_query_limits = limits;
    options.intrinsic_query_statistics = statistics;
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

void check_pass_budget(TextSystem& text_system) {
    const std::string html = "<body><main><div id='p'><div id='item'>AB CD</div></div></main></body>";
    const std::string css = "main{display:grid;grid-template-columns:100px}"
        "#p{display:flex;width:5px;align-items:flex-start}"
        "#item{display:block;flex:0 1 0px;font-family:'Core Test';font-size:16px}";
    LayoutIntrinsicQueryStatistics statistics;
    LayoutIntrinsicQueryLimits limits;
    limits.max_work = 0;
    auto refused = layout(html, css, &text_system, true, limits, &statistics);
    require(find_box(*refused.boxes, "item")->rect.width == 0 && statistics.work == 0 &&
            statistics.resource_refusals > 0,
            "Installed zero-work pass budget must preserve legacy Flex allocation");
    limits.max_work = 65536;
    auto normal = layout(html, css, &text_system, true, limits, &statistics);
    require(find_box(*normal.boxes, "item")->rect.width == 18 && statistics.work > 0 &&
            statistics.work <= limits.max_work && statistics.resource_refusals == 0,
            "Installed next public pass must replace statistics and restore intrinsic allocation");
}

void check_cropped_pass_budget(TextSystem& text_system) {
    const std::string html = "<body><div id='item'>AB CD</div></body>";
    const std::string css = "#item{display:block;width:40px;font-family:'Core Test';font-size:16px}";
    LayoutIntrinsicQueryStatistics statistics;
    statistics.work = 123;
    statistics.queries = 456;
    LayoutIntrinsicQueryLimits limits;
    limits.max_work = 0;
    auto bounded = layout(html, css, &text_system, true, limits, &statistics);
    require(statistics.work == 0 && statistics.queries == 0 && statistics.resource_refusals == 0,
            "Installed cropped public layout must replace stale query statistics");
    const auto normal = layout(html, css, &text_system);
    const auto* bounded_item = find_box(*bounded.boxes, "item");
    const auto* normal_item = find_box(*normal.boxes, "item");
    require(bounded_item && normal_item && bounded_item->rect.x == normal_item->rect.x &&
            bounded_item->rect.y == normal_item->rect.y &&
            bounded_item->rect.width == normal_item->rect.width &&
            bounded_item->rect.height == normal_item->rect.height && bounded_item->rect.height > 0,
            "Installed cropped zero-query allowance must preserve ordinary Block text layout");
}

void check_installed_block_mixed_dimensions() {
    const std::string html = "<div id='parent'><div id='mixed'></div></div>";
    const std::string base_css = "*{margin:0;padding:0;border:0}"
        "#parent{width:200px;height:100px}";

    StyleResolver calc_resolver(CssParser{}.parse(base_css +
        "#mixed{width:calc(20px + 50%)}"));
    auto calc_dom = HtmlParser{}.parse(html);
    auto calc_tree = RenderTreeBuilder(calc_resolver).build(*calc_dom);
    const auto* calc_item = find_object(*calc_tree, "mixed");
    detail::SizeKind calc_kind = detail::SizeKind::Pixels;
    std::int64_t calc_length = 0;
    std::int64_t calc_percentage = 0;
    require(calc_item && !calc_item->style.computed_sizes.read_typed(
                detail::SizeProperty::Width, calc_kind, calc_length, calc_percentage) &&
            calc_kind != detail::SizeKind::LengthPercentage,
            "Installed CSS calc declaration must not publish typed mixed-dimension storage");

    StyleResolver mixed_resolver(CssParser{}.parse(base_css));
    auto mixed_dom = HtmlParser{}.parse(html);
    auto mixed_tree = RenderTreeBuilder(mixed_resolver).build(*mixed_dom);
    auto* mixed_item = find_object(*mixed_tree, "mixed");
    require(mixed_item != nullptr, "Installed mixed Block fixture item exists");
    auto publish = [&](detail::SizeProperty property, double length, double percentage,
                       int& scalar, int& percentage_proxy) {
        if (!mixed_item->style.computed_sizes.set_length_percentage(property, length, percentage))
            return false;
        scalar = -1;
        percentage_proxy = -1;
        return true;
    };
    require(publish(detail::SizeProperty::Width, 50, 50,
                    mixed_item->style.width, mixed_item->style.width_percent) &&
            publish(detail::SizeProperty::MinWidth, 80, -50,
                    mixed_item->style.min_width, mixed_item->style.min_width_percent) &&
            publish(detail::SizeProperty::MaxWidth, 30, 50,
                    mixed_item->style.max_width, mixed_item->style.max_width_percent) &&
            publish(detail::SizeProperty::Height, 20, 50,
                    mixed_item->style.height, mixed_item->style.height_percent) &&
            publish(detail::SizeProperty::MinHeight, 80, -50,
                    mixed_item->style.min_height, mixed_item->style.min_height_percent) &&
            publish(detail::SizeProperty::MaxHeight, 50, 50,
                    mixed_item->style.max_height, mixed_item->style.max_height_percent),
            "Installed package publishes all six mixed Block dimensions through typed storage");
    const auto mixed_layout = LayoutEngine(mixed_resolver).layout(*mixed_tree, 320, 240);
    const auto* mixed_box = find_box(*mixed_layout, "mixed");
    require(mixed_box && mixed_box->rect.width == 130 && mixed_box->rect.height == 70,
            "Installed Block consumer resolves signed mixed ranges in the cropped-capable profile");
}

void check_installed_grid_vertical_refusal() {
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
    const std::string html =
        "<main id='grid'><div id='mixed'></div><div id='other'></div></main>";
    const std::string css = "*{margin:0;padding:0;border:0}"
        "#grid{display:grid;width:200px;grid-template-columns:100px 100px}"
        "#mixed,#other{height:0px}";
    const auto layout_fixture = [&](bool inject_mixed, VectorDiagnosticSink* diagnostics) {
        auto dom = HtmlParser{}.parse(html);
        StyleResolver resolver(CssParser{}.parse(css));
        auto tree = RenderTreeBuilder(resolver).build(*dom);
        auto* mixed = find_object(*tree, "mixed");
        require(mixed != nullptr, "Installed Grid refusal fixture item exists");
        if (inject_mixed) {
            require(mixed->style.computed_sizes.set_length_percentage(
                        detail::SizeProperty::Height, 20, 50),
                    "Installed Grid mixed height publishes through typed storage");
            mixed->style.height = -1;
            mixed->style.height_percent = -1;
        }
        LayoutEngineOptions options;
        options.diagnostics = diagnostics;
        auto boxes = LayoutEngine(resolver, {}, options).layout(*tree, 320, 240);
        return std::make_pair(std::move(dom), std::move(boxes));
    };

    auto reference = layout_fixture(false, nullptr);
    VectorDiagnosticSink diagnostics;
    auto refused = layout_fixture(true, &diagnostics);
    const auto* reference_mixed = find_box(*reference.second, "mixed");
    const auto* refused_mixed = find_box(*refused.second, "mixed");
    const auto same_vertical_geometry = [](const Rect& left, const Rect& right) {
        return left.y == right.y && left.height == right.height;
    };
    require(reference_mixed && refused_mixed &&
            same_vertical_geometry(reference_mixed->rect, refused_mixed->rect),
            "Installed Grid vertical refusal preserves the item's equivalent pixel geometry");
    const auto* refused_grid = find_box(*refused.second, "grid");
    const auto* refused_other = find_box(*refused.second, "other");
    require(refused_grid && refused_other &&
            refused_grid->rect.x == 0 && refused_grid->rect.y == 0 &&
            refused_grid->rect.width == 0 && refused_grid->rect.height == 0 &&
            refused_mixed->rect.x == 0 && refused_mixed->rect.y == 0 &&
            refused_mixed->rect.width == 0 && refused_mixed->rect.height == 0 &&
            refused_other->rect.x == 0 && refused_other->rect.y == 0 &&
            refused_other->rect.width == 0 && refused_other->rect.height == 0,
            "Installed Grid refusal leaves the container, item and sibling at the documented zero-geometry boundary");
    detail::SizeKind kind = detail::SizeKind::Pixels;
    std::int64_t length = 0;
    std::int64_t percentage = 0;
    require(refused_mixed->style.computed_sizes.read_typed(
                detail::SizeProperty::Height, kind, length, percentage) &&
            kind == detail::SizeKind::LengthPercentage &&
            refused_mixed->style.height == -1 && refused_mixed->style.height_percent == -1,
            "Installed Grid refusal keeps the authored typed height and scalar proxies intact");
    require(diagnostics.size() == 1 &&
            diagnostics.diagnostics().front().stage == DiagnosticStage::Layout &&
            diagnostics.diagnostics().front().code == "unsupported-mixed-dimension-layout",
            "Installed Grid refusal exposes the exact public layout diagnostic");
#endif
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
        check_installed_block_mixed_dimensions();

#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
        check_installed_grid_vertical_refusal();
        check_auto_min(text_system);
        check_percentage_minimum(text_system);
        check_grid_max_content(text_system);
        check_calc_refusal(text_system);
        check_pass_budget(text_system);
        std::cout << "Installed ABI4 mixed Block, Flex auto-min, percentage min, Grid max-content and calc refusal passed\n";
#else
        check_cropped_pass_budget(text_system);
        std::cout << "Installed ABI4 cropped profile: mixed Block, package identity, Block text layout and fresh query statistics passed\n";
#endif
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
