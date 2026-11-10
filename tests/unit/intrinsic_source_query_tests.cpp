#include "../../src/intrinsic_source_query.h"
#include "render_core/css_parser.h"
#include "render_core/html_parser.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace jellyframe;
using namespace jellyframe::detail;

namespace {

void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void u16(std::vector<std::uint8_t>& bytes, std::size_t at, unsigned value) {
    bytes[at] = static_cast<std::uint8_t>(value & 255U);
    bytes[at + 1] = static_cast<std::uint8_t>((value >> 8U) & 255U);
}

void u32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes[at + i] = static_cast<std::uint8_t>((value >> (8U * i)) & 255U);
}

struct FontAsset {
    std::vector<std::uint8_t> bytes;
    FontResource font;
};

FontAsset make_font() {
    std::vector<std::pair<unsigned, unsigned>> glyphs = {
        {32, 250}, {33, 300}, {45, 300}, {47, 300}, {63, 400},
        {65, 500}, {66, 600}, {67, 400}, {0x4e2d, 1000}, {0x6587, 1000},
        {97, 700}, {98, 800}
    };
    for (const auto range : {std::pair<unsigned, unsigned>{33, 47}, {58, 64},
                             {91, 96}, {123, 126}}) {
        for (unsigned codepoint = range.first; codepoint <= range.second; ++codepoint) {
            const auto existing = std::find_if(glyphs.begin(), glyphs.end(),
                [codepoint](const auto& glyph) { return glyph.first == codepoint; });
            if (existing == glyphs.end()) glyphs.emplace_back(codepoint, 300);
        }
    }
    std::sort(glyphs.begin(), glyphs.end(),
        [](const auto& left, const auto& right) { return left.first < right.first; });
    FontAsset asset;
    const std::size_t bitmap_offset = 128 + glyphs.size() * 40;
    asset.bytes.resize(bitmap_offset, 0);
    auto& bytes = asset.bytes;
    const char magic[] = "JFFONT2";
    for (unsigned i = 0; i < 8; ++i) bytes[i] = static_cast<std::uint8_t>(magic[i]);
    u16(bytes, 8, 2);
    u16(bytes, 10, 128);
    u16(bytes, 12, 40);
    u16(bytes, 14, 1);
    u32(bytes, 20, static_cast<std::uint32_t>(glyphs.size()));
    u32(bytes, 24, 128);
    u32(bytes, 28, static_cast<std::uint32_t>(bitmap_offset));
    u16(bytes, 36, 1000);
    u16(bytes, 38, 16);
    u16(bytes, 40, 400);
    bytes[42] = 4;
    u32(bytes, 44, 800);
    u32(bytes, 48, 200);
    u32(bytes, 52, 50);
    bytes[64] = 1;
    u32(bytes, 100, 1);
    u16(bytes, 104, 1);
    for (std::size_t i = 0; i < glyphs.size(); ++i) {
        const auto at = 128 + i * 40;
        u32(bytes, at, glyphs[i].first);
        u32(bytes, at + 4, static_cast<std::uint32_t>(i + 1));
        u32(bytes, at + 8, glyphs[i].second);
        u32(bytes, at + 28, static_cast<std::uint32_t>(bytes.size() - bitmap_offset));
        if (glyphs[i].first != 32) {
            u32(bytes, at + 12, static_cast<std::uint32_t>(-1));
            u32(bytes, at + 16, static_cast<std::uint32_t>(-2));
            u16(bytes, at + 20, 1);
            u16(bytes, at + 22, 2);
            u32(bytes, at + 24, 1);
            u32(bytes, at + 32, 2);
            bytes.push_back(0xf0);
            bytes.push_back(0xf0);
        }
    }
    u32(bytes, 16, static_cast<std::uint32_t>(bytes.size()));
    u32(bytes, 32, static_cast<std::uint32_t>(bytes.size() - bitmap_offset));
    std::uint32_t crc = 0xffffffffU;
    for (std::uint8_t value : bytes) {
        crc ^= value;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1U) ^ ((crc & 1U) ? 0xedb88320U : 0U);
    }
    u32(bytes, 96, ~crc);
    check(asset.font.load(bytes.data(), bytes.size()) == FontResourceStatus::Loaded,
          "synthetic JFFONT2 font did not load");
    return asset;
}

struct Fixture {
    Node root_node{NodeType::Element};
    LayoutBox root_box;

    Fixture() {
        root_node.tag_name = "div";
        root_box.node = &root_node;
        root_box.style.display = Display::Block;
        root_box.style.font_size = 16;
        root_box.style.font_weight = 400;
    }

    std::pair<LayoutBox&, Node&> append_element(LayoutBox& parent_box, Node& parent_node,
                                               const std::string& tag, Display display) {
        Node& node = parent_node.append_child(make_element(tag));
        LayoutBoxPtr child(new LayoutBox, LayoutBoxDeleter{false});
        child->node = &node;
        child->style = parent_box.style;
        child->style.display = display;
        LayoutBox& result = *child;
        parent_box.children.push_back(std::move(child));
        return {result, node};
    }

    void append_text(LayoutBox& parent_box, Node& parent_node, std::string text) {
        Node& node = parent_node.append_child(make_text(std::move(text)));
        LayoutBoxPtr child(new LayoutBox, LayoutBoxDeleter{false});
        child->node = &node;
        child->style = parent_box.style;
        child->style.display = Display::Inline;
        parent_box.children.push_back(std::move(child));
    }

    void append_break(LayoutBox& parent_box, Node& parent_node, const std::string& tag) {
        (void)append_element(parent_box, parent_node, tag, Display::Inline);
    }
};

TextSystem bind_font(FontAsset& asset, TextFontFace& face) {
    face = TextFontFace{1, 0, asset.font};
    TextSystem text_system;
    check(text_system.bind(&face, 1) == TextSystemStatus::Ok, "font registration failed");
    return text_system;
}

IntrinsicSourceWidthResult query(const Fixture& fixture, const TextSystem& text_system,
                                 IntrinsicSourceQueryLimits limits = {}) {
    return query_intrinsic_source_width(fixture.root_box, text_system,
        IntrinsicPercentageBasis{IntrinsicPercentageBasisKind::CyclicSameAxis, -1}, limits);
}

void percentage_basis_contract(const TextSystem& text_system) {
    Fixture fixture;
    fixture.append_text(fixture.root_box, fixture.root_node, "A");

    const auto cyclic = query_intrinsic_source_width(fixture.root_box, text_system,
        {IntrinsicPercentageBasisKind::CyclicSameAxis, -1});
    check(cyclic.status == IntrinsicSourceStatus::Complete &&
          cyclic.min_content_border_box_units == 8000 &&
          cyclic.max_content_border_box_units == 8000,
          "cyclic same-axis basis should retain the admitted source query");

    const auto definite = query_intrinsic_source_width(fixture.root_box, text_system,
        {IntrinsicPercentageBasisKind::DefiniteIndependent, 50000});
    check(definite.status == IntrinsicSourceStatus::Unsupported &&
          definite.min_content_border_box_units == -1 &&
          definite.max_content_border_box_units == -1,
          "valid independent definite basis must be refused without partial dimensions");

    const auto unknown = query_intrinsic_source_width(fixture.root_box, text_system,
        {IntrinsicPercentageBasisKind::IndefiniteOrUnknown, -1});
    check(unknown.status == IntrinsicSourceStatus::Unsupported &&
          unknown.min_content_border_box_units == -1 &&
          unknown.max_content_border_box_units == -1,
          "unknown basis must be refused without partial dimensions");

    for (const auto basis : {
             IntrinsicPercentageBasis{IntrinsicPercentageBasisKind::CyclicSameAxis, 0},
             IntrinsicPercentageBasis{IntrinsicPercentageBasisKind::DefiniteIndependent, -1},
             IntrinsicPercentageBasis{IntrinsicPercentageBasisKind::IndefiniteOrUnknown, 0},
             IntrinsicPercentageBasis{static_cast<IntrinsicPercentageBasisKind>(255), -1}}) {
        const auto invalid = query_intrinsic_source_width(fixture.root_box, text_system, basis);
        check(invalid.status == IntrinsicSourceStatus::InvalidInput &&
              invalid.min_content_border_box_units == -1 &&
              invalid.max_content_border_box_units == -1,
              "malformed percentage basis must be rejected without partial dimensions");
    }
}

LayoutBox* find_box(LayoutBox& box, std::string_view id) {
    if (box.node && box.node->attribute("id") == id) return &box;
    for (auto& child : box.children) {
        if (auto* found = find_box(*child, id)) return found;
    }
    return nullptr;
}

void fractional_percentage_pipeline(const TextSystem& text_system) {
    struct Property {
        const char* name;
        SizeProperty property;
        int Style::* scalar;
        int Style::* percentage;
    };
    const Property properties[] = {
        {"width", SizeProperty::Width, &Style::width, &Style::width_percent},
        {"min-width", SizeProperty::MinWidth, &Style::min_width, &Style::min_width_percent},
        {"max-width", SizeProperty::MaxWidth, &Style::max_width, &Style::max_width_percent}
    };
    for (const auto& property : properties) {
        for (bool block_partition : {false, true}) {
            const std::string content = block_partition
                ? "<div>A<span>B</span></div><div>C</div>" : "A<span>B</span> C";
            auto dom = HtmlParser{}.parse("<div id='query'><div id='percent'>" + content + "</div></div>");
            StyleResolver styles(CssParser{}.parse(
                std::string("*{margin:0;padding:0;border:0;font-size:16px;font-weight:400;list-style-type:none}") +
                "#percent{" + property.name + ":37.5%}"));
            LayoutEngineOptions options;
            options.text_system = &text_system;
            auto layout = LayoutEngine(styles, {}, options).layout(*dom, 320, 240);
            auto* root = find_box(*layout, "query");
            auto* descendant = find_box(*layout, "percent");
            check(root && descendant, "fractional percentage pipeline boxes missing");
            SizeKind kind = SizeKind::Pixels;
            std::int64_t units = 0;
            check(descendant->style.computed_sizes.get(property.property, kind, units) &&
                  kind == SizeKind::Percent && units == 37500 &&
                  descendant->style.*property.scalar == -1 &&
                  descendant->style.*property.percentage == 38,
                  "normal style resolution must retain fractional percentage and rounded proxy");
            const auto run = [&] {
                return query_intrinsic_source_width(*root, text_system,
                    {IntrinsicPercentageBasisKind::CyclicSameAxis, -1});
            };
            const auto result = run();
            if (result.status != IntrinsicSourceStatus::Complete)
                throw std::runtime_error(std::string("normal pipeline fractional ") + property.name +
                    " refused with status " + std::to_string(static_cast<unsigned>(result.status)));
            check(result.min_content_border_box_units == 17600 &&
                  result.max_content_border_box_units == (block_partition ? 17600 : 28000),
                  "fractional cyclic percentage must preserve source widths across inline/block partitions");
            for (int malformed_proxy : {-1, 37, 39}) {
                descendant->style.*property.percentage = malformed_proxy;
                const auto refused = run();
                check(refused.status == IntrinsicSourceStatus::Unsupported &&
                      refused.min_content_border_box_units == -1 && refused.max_content_border_box_units == -1,
                      "missing/conflicting retained percentage proxies must refuse atomically");
            }
            descendant->style.*property.percentage = 38;
            descendant->style.*property.scalar = 1;
            const auto refused_scalar = run();
            check(refused_scalar.status == IntrinsicSourceStatus::Unsupported &&
                  refused_scalar.min_content_border_box_units == -1 &&
                  refused_scalar.max_content_border_box_units == -1,
                  "retained percentage with a conflicting pixel scalar must refuse atomically");
        }
    }
}

void unsupported_original_size_kinds() {
    for (SizeProperty property : {SizeProperty::Width, SizeProperty::Height,
             SizeProperty::MinWidth, SizeProperty::MinHeight, SizeProperty::MaxWidth,
             SizeProperty::MaxHeight, SizeProperty::FlexBasis}) {
        ComputedSizeStorage storage;
        check(storage.set(property, SizeKind::Pixels, 12.25), "original size precision setup failed");
        check(!storage.set(property, static_cast<SizeKind>(2), 37.5),
              "unsupported third size kind must not publish into an original dimension slot");
        SizeKind kind = SizeKind::Percent;
        std::int64_t units = 0;
        check(storage.get(property, kind, units) && kind == SizeKind::Pixels && units == 12250,
              "refused size kind must preserve the original dimension value");
    }
}

void expect_widths(const Fixture& fixture, const TextSystem& text_system,
                   std::int64_t min_width, std::int64_t max_width) {
    const auto result = query(fixture, text_system);
    if (result.status != IntrinsicSourceStatus::Complete)
        throw std::runtime_error("source width query status was " +
            std::to_string(static_cast<unsigned>(result.status)) +
            " for admitted input");
    if (result.min_content_border_box_units != min_width)
        throw std::runtime_error("min-content width mismatch: expected " + std::to_string(min_width) +
                                 ", got " + std::to_string(result.min_content_border_box_units));
    if (result.max_content_border_box_units != max_width)
        throw std::runtime_error("max-content width mismatch: expected " + std::to_string(max_width) +
                                 ", got " + std::to_string(result.max_content_border_box_units));
    check(result.boxes_visited > 0 && result.source_bytes > 0 &&
          result.measurements > 0 && result.work >= result.source_bytes,
          "complete source query reports aggregate resource usage");
}

void text_runs_and_breaks(const TextSystem& text_system) {
    {
        Fixture fixture;
        fixture.root_box.style.text_transform = TextTransform::Uppercase;
        fixture.append_text(fixture.root_box, fixture.root_node, "ab");
        expect_widths(fixture, text_system, 17600, 17600);
    }
    {
        Fixture fixture;
        fixture.root_box.style.text_transform = TextTransform::Lowercase;
        fixture.append_text(fixture.root_box, fixture.root_node, "AB");
        expect_widths(fixture, text_system, 24000, 24000);
    }
    {
        Fixture fixture;
        fixture.root_box.style.text_transform = TextTransform::Uppercase;
        fixture.append_text(fixture.root_box, fixture.root_node, "a");
        auto [span, span_node] = fixture.append_element(
            fixture.root_box, fixture.root_node, "span", Display::Inline);
        fixture.append_text(span, span_node, "b");
        expect_widths(fixture, text_system, 17600, 17600);
    }
    {
        Fixture fixture;
        fixture.root_box.style.overflow_wrap_anywhere = true;
        fixture.append_text(fixture.root_box, fixture.root_node, "ABC");
        expect_widths(fixture, text_system, 9600, 24000);
    }
    {
        Fixture fixture;
        fixture.root_box.style.overflow_wrap_anywhere = true;
        fixture.append_text(fixture.root_box, fixture.root_node, "A");
        auto [span, span_node] = fixture.append_element(
            fixture.root_box, fixture.root_node, "span", Display::Inline);
        fixture.append_text(span, span_node, "BC");
        expect_widths(fixture, text_system, 9600, 24000);
    }
    {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, "A");
        auto [span, span_node] = fixture.append_element(
            fixture.root_box, fixture.root_node, "span", Display::Inline);
        fixture.append_text(span, span_node, "B");
        expect_widths(fixture, text_system, 17600, 17600);
    }
    {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, "A ");
        auto [span, span_node] = fixture.append_element(
            fixture.root_box, fixture.root_node, "span", Display::Inline);
        fixture.append_text(span, span_node, "  B");
        expect_widths(fixture, text_system, 9600, 21600);
    }
    {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, "A-B");
        expect_widths(fixture, text_system, 12800, 22400);
    }
    {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, "A-B/C");
        expect_widths(fixture, text_system, 20800, 33600);
    }
    {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, "A?B");
        expect_widths(fixture, text_system, 14400, 24000);
    }
    {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, "A??B");
        expect_widths(fixture, text_system, 20800, 30400);
    }
    {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, "A?");
        auto [span, span_node] = fixture.append_element(
            fixture.root_box, fixture.root_node, "span", Display::Inline);
        fixture.append_text(span, span_node, "B");
        expect_widths(fixture, text_system, 14400, 24000);
    }
    {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, "A?/B");
        expect_widths(fixture, text_system, 28800, 28800);
    }
    {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, "A?!B");
        expect_widths(fixture, text_system, 28800, 28800);
    }
    {
        for (char whitespace : {' ', '\t', '\n', '\r', '\f'}) {
            Fixture fixture;
            fixture.append_text(fixture.root_box, fixture.root_node,
                                std::string("A") + whitespace);
            auto [span, span_node] = fixture.append_element(
                fixture.root_box, fixture.root_node, "span", Display::Inline);
            fixture.append_text(span, span_node, std::string(1, whitespace) + "B");
            expect_widths(fixture, text_system, 9600, 21600);
        }
    }
    {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, "A-");
        auto [span, span_node] = fixture.append_element(
            fixture.root_box, fixture.root_node, "span", Display::Inline);
        fixture.append_text(span, span_node, "B");
        expect_widths(fixture, text_system, 12800, 22400);
    }
    {
        for (const auto range : {std::pair<unsigned, unsigned>{33, 47}, {58, 64},
                                 {91, 96}, {123, 126}}) {
            for (unsigned codepoint = range.first; codepoint <= range.second; ++codepoint) {
                const char punctuation = static_cast<char>(codepoint);
                const bool breaks_after = punctuation == '-' || punctuation == '?';
                const std::int64_t expected_max = punctuation == '?' ? 24000 : 22400;
                const std::int64_t expected_min = breaks_after
                    ? (punctuation == '-' ? 12800 : 14400) : 22400;
                Fixture contiguous;
                contiguous.append_text(contiguous.root_box, contiguous.root_node,
                    std::string("A") + punctuation + "B");
                expect_widths(contiguous, text_system, expected_min, expected_max);

                Fixture partitioned;
                partitioned.append_text(partitioned.root_box, partitioned.root_node,
                    std::string("A") + punctuation);
                auto [span, span_node] = partitioned.append_element(
                    partitioned.root_box, partitioned.root_node, "span", Display::Inline);
                partitioned.append_text(span, span_node, "B");
                expect_widths(partitioned, text_system, expected_min, expected_max);
            }
        }
    }
    {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, "A/B");
        expect_widths(fixture, text_system, 22400, 22400);
    }
    {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, "A-");
        auto [span, span_node] = fixture.append_element(
            fixture.root_box, fixture.root_node, "span", Display::Inline);
        fixture.append_text(span, span_node, "/B");
        expect_widths(fixture, text_system, 27200, 27200);
    }
    {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, "A");
        fixture.append_break(fixture.root_box, fixture.root_node, "br");
        fixture.append_break(fixture.root_box, fixture.root_node, "br");
        fixture.append_text(fixture.root_box, fixture.root_node, "B");
        expect_widths(fixture, text_system, 9600, 9600);
    }
    {
        Fixture fixture;
        fixture.root_box.children.emplace_back(nullptr, LayoutBoxDeleter{false});
        check(query(fixture, text_system).status == IntrinsicSourceStatus::InvalidInput,
              "null layout child must be rejected without dereferencing it");
    }
    {
        TextSystem unbound;
        for (const char* content : {"", "\t \n"}) {
            Fixture fixture;
            fixture.append_text(fixture.root_box, fixture.root_node, content);
            fixture.root_box.style.padding.left = 1;
            fixture.root_box.style.padding.right = 2;
            const auto result = query(fixture, unbound);
            check(result.status == IntrinsicSourceStatus::Complete &&
                  result.min_content_border_box_units == 3000 &&
                  result.max_content_border_box_units == 3000,
                  "empty or whitespace-only source should measure decoration without a font system");
        }
    }
    {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, "A");
        fixture.append_break(fixture.root_box, fixture.root_node, "wbr");
        fixture.append_text(fixture.root_box, fixture.root_node, "B");
        expect_widths(fixture, text_system, 9600, 17600);
    }
    {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, "A");
        fixture.append_break(fixture.root_box, fixture.root_node, "br");
        fixture.append_text(fixture.root_box, fixture.root_node, "B");
        expect_widths(fixture, text_system, 9600, 9600);
    }
    {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, " A  ");
        fixture.root_box.style.padding.left = 1;
        fixture.root_box.style.padding.right = 2;
        fixture.root_box.style.border_width.left = 1;
        expect_widths(fixture, text_system, 12000, 12000);
    }
}

void refusals_and_budgets(const TextSystem& text_system) {
    {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, "A");
        fixture.root_box.style.white_space_nowrap = true;
        check(query(fixture, text_system).status == IntrinsicSourceStatus::Unsupported,
              "nowrap source must refuse");
    }
    {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, "A");
        fixture.root_box.style.letter_spacing = 1;
        check(query(fixture, text_system).status == IntrinsicSourceStatus::Unsupported,
              "letter spacing must refuse");
    }
    {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, "A");
        fixture.root_box.style.text_indent = 2;
        check(query(fixture, text_system).status == IntrinsicSourceStatus::Unsupported,
              "text indent must refuse until first-line contribution is modeled");
    }
    {
        Fixture fixture;
        fixture.root_box.style.text_transform = TextTransform::Capitalize;
        fixture.append_text(fixture.root_box, fixture.root_node, "a");
        check(query(fixture, text_system).status == IntrinsicSourceStatus::Unsupported,
              "capitalize must refuse until word boundaries are modeled across source partitions");
    }
    {
        Fixture fixture;
        fixture.root_box.style.text_transform = TextTransform::Uppercase;
        auto [span, span_node] = fixture.append_element(
            fixture.root_box, fixture.root_node, "span", Display::Inline);
        span.style.text_transform = TextTransform::Lowercase;
        fixture.append_text(span, span_node, "a");
        check(query(fixture, text_system).status == IntrinsicSourceStatus::Unsupported,
              "mixed text transforms must refuse instead of merging incompatible source runs");
    }
    for (PositionType position : {PositionType::Relative, PositionType::Absolute,
                                  PositionType::Fixed, PositionType::Sticky}) {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, "A");
        fixture.root_box.style.position_type = position;
        check(query(fixture, text_system).status == IntrinsicSourceStatus::Unsupported,
              "positioned root must refuse");
    }
    {
        Fixture fixture;
        fixture.root_node.tag_name = "li";
        fixture.root_box.style.list_style_type = ListStyleType::Disc;
        fixture.append_text(fixture.root_box, fixture.root_node, "A");
        check(query(fixture, text_system).status == IntrinsicSourceStatus::Unsupported,
              "list marker contribution must refuse until modeled");
    }
    {
        Fixture fixture;
        auto [span, span_node] = fixture.append_element(
            fixture.root_box, fixture.root_node, "span", Display::Inline);
        span.style.padding.left = 1;
        fixture.append_text(span, span_node, "A");
        check(query(fixture, text_system).status == IntrinsicSourceStatus::Unsupported,
              "decorated inline wrapper must refuse");
    }
    {
        Fixture fixture;
        auto [block, block_node] = fixture.append_element(
            fixture.root_box, fixture.root_node, "div", Display::Block);
        fixture.append_text(block, block_node, "A");
        expect_widths(fixture, text_system, 8000, 8000);
    }
    {
        Fixture fixture;
        auto [first, first_node] = fixture.append_element(
            fixture.root_box, fixture.root_node, "div", Display::Block);
        fixture.append_text(first, first_node, "A");
        auto [second, second_node] = fixture.append_element(
            fixture.root_box, fixture.root_node, "div", Display::Block);
        fixture.append_text(second, second_node, "BC");
        expect_widths(fixture, text_system, 16000, 16000);
    }
    {
        Fixture fixture;
        auto [outer, outer_node] = fixture.append_element(
            fixture.root_box, fixture.root_node, "div", Display::Block);
        fixture.append_text(outer, outer_node, "A");
        auto [inner, inner_node] = fixture.append_element(outer, outer_node, "div", Display::Block);
        fixture.append_text(inner, inner_node, "BC");
        fixture.append_text(outer, outer_node, "C");
        expect_widths(fixture, text_system, 16000, 16000);
    }
    {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, "C ");
        auto [block, block_node] = fixture.append_element(
            fixture.root_box, fixture.root_node, "div", Display::Block);
        fixture.append_text(block, block_node, "A ");
        auto [span, span_node] = fixture.append_element(block, block_node, "span", Display::Inline);
        fixture.append_text(span, span_node, "B");
        fixture.append_text(fixture.root_box, fixture.root_node, "A");
        expect_widths(fixture, text_system, 9600, 21600);
    }
    {
        Fixture fixture;
        auto [block, block_node] = fixture.append_element(
            fixture.root_box, fixture.root_node, "div", Display::Block);
        fixture.append_text(block, block_node, "A");
        fixture.append_break(block, block_node, "br");
        fixture.append_text(block, block_node, "B");
        expect_widths(fixture, text_system, 9600, 9600);
    }
    {
        Fixture fixture;
        auto [block, block_node] = fixture.append_element(
            fixture.root_box, fixture.root_node, "div", Display::Block);
        block.style.padding.left = 1;
        fixture.append_text(block, block_node, "A");
        check(query(fixture, text_system).status == IntrinsicSourceStatus::Unsupported,
              "decorated nested block must refuse");
    }
    {
        Fixture fixture;
        auto [block, block_node] = fixture.append_element(
            fixture.root_box, fixture.root_node, "div", Display::Block);
        block.style.width = 10;
        fixture.append_text(block, block_node, "A");
        check(query(fixture, text_system).status == IntrinsicSourceStatus::Unsupported,
              "sized nested block must refuse");
    }
    for (const auto& [percentage, name] : {
             std::pair{&Style::width_percent, "width"},
             std::pair{&Style::min_width_percent, "min-width"},
             std::pair{&Style::max_width_percent, "max-width"}}) {
        Fixture fixture;
        auto [block, block_node] = fixture.append_element(
            fixture.root_box, fixture.root_node, "div", Display::Block);
        block.style.*percentage = 50000;
        fixture.append_text(block, block_node, "A");
        const auto result = query(fixture, text_system);
        if (result.status != IntrinsicSourceStatus::Complete)
            throw std::runtime_error(std::string("cyclic ") + name +
                " query status was " +
                std::to_string(static_cast<unsigned>(result.status)));
        check(result.min_content_border_box_units == 8000 &&
              result.max_content_border_box_units == 8000,
              "cyclic width percentage must preserve the source contribution");
    }
    for (int Style::* percentage : {
             &Style::height_percent, &Style::min_height_percent, &Style::max_height_percent}) {
        Fixture fixture;
        auto [block, block_node] = fixture.append_element(
            fixture.root_box, fixture.root_node, "div", Display::Block);
        block.style.*percentage = 50000;
        fixture.append_text(block, block_node, "A");
        check(query(fixture, text_system).status == IntrinsicSourceStatus::Unsupported,
              "cross-axis percentage sizing must refuse until it is modeled");
    }
    {
        Fixture fixture;
        auto [block, block_node] = fixture.append_element(
            fixture.root_box, fixture.root_node, "div", Display::Block);
        check(block.style.computed_sizes.set(SizeProperty::Width, SizeKind::Percent, 50.0),
              "computed cyclic width percentage setup failed");
        fixture.append_text(block, block_node, "A");
        expect_widths(fixture, text_system, 8000, 8000);
    }
    {
        Fixture fixture;
        auto [block, block_node] = fixture.append_element(
            fixture.root_box, fixture.root_node, "div", Display::Block);
        block.style.width = -2;
        fixture.append_text(block, block_node, "A");
        check(query(fixture, text_system).status == IntrinsicSourceStatus::Unsupported,
              "malformed size sentinel must refuse intrinsic source admission");
    }
    {
        Fixture fixture;
        auto [flex, flex_node] = fixture.append_element(
            fixture.root_box, fixture.root_node, "div", Display::Flex);
        fixture.append_text(flex, flex_node, "A");
        check(query(fixture, text_system).status == IntrinsicSourceStatus::Unsupported,
              "nested flex formatting context must refuse");
    }
    {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, "A");
        fixture.root_box.children[0]->style.font_size = 20;
        check(query(fixture, text_system).status == IntrinsicSourceStatus::Unsupported,
              "mixed font run must refuse");
    }
    for (const char* tag : {"img", "picture", "video", "audio", "canvas",
                            "object", "embed", "iframe", "hr",
                            "input", "select", "textarea", "button"}) {
        Fixture fixture;
        (void)fixture.append_element(fixture.root_box, fixture.root_node, tag, Display::Inline);
        check(query(fixture, text_system).status == IntrinsicSourceStatus::Unsupported,
              "replaced/native source must refuse");
    }
    for (PositionType position : {PositionType::Absolute, PositionType::Fixed}) {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, "A");
        auto [out_of_flow, out_of_flow_node] = fixture.append_element(
            fixture.root_box, fixture.root_node, "span", Display::Inline);
        (void)out_of_flow_node;
        out_of_flow.style.position_type = position;
        fixture.append_text(out_of_flow, out_of_flow_node, "BC");
        expect_widths(fixture, text_system, 8000, 8000);
    }
    for (const char* tag : {"br", "wbr"}) {
        Fixture fixture;
        auto [line_break, line_break_node] = fixture.append_element(
            fixture.root_box, fixture.root_node, tag, Display::Inline);
        (void)line_break_node;
        line_break.style.padding.left = 1;
        check(query(fixture, text_system).status == IntrinsicSourceStatus::Unsupported,
              "decorated br/wbr must refuse");
    }
    {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, "A\xC3\xA9");
        check(query(fixture, text_system).status == IntrinsicSourceStatus::Unsupported,
              "non-Han non-ASCII source must refuse");
    }
    {
        Fixture fixture;
        fixture.root_box.style.overflow_wrap_anywhere = true;
        fixture.append_text(fixture.root_box, fixture.root_node, "A\xC3\xA9");
        check(query(fixture, text_system).status == IntrinsicSourceStatus::Unsupported,
              "anywhere wrapping must not treat UTF-8 bytes as character boundaries");
    }
    {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, "Z");
        const auto result = query(fixture, text_system);
        check(result.status == IntrinsicSourceStatus::MeasurementFailure &&
              result.min_content_border_box_units == -1 &&
              result.max_content_border_box_units == -1,
              "missing glyph must not publish a zero or partial measurement");
    }
    {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, "A");
        TextSystem unbound;
        check(query(fixture, unbound).status == IntrinsicSourceStatus::MeasurementFailure,
              "unbound TextSystem must refuse");
    }
    {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, "A");
        IntrinsicSourceQueryLimits limits;
        limits.max_source_bytes = 0;
        check(query(fixture, text_system, limits).status == IntrinsicSourceStatus::InvalidInput,
              "zero query capacity must be invalid");
    }
    {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, "A B");
        IntrinsicSourceQueryLimits limits;
        limits.max_source_bytes = 1;
        check(query(fixture, text_system, limits).status == IntrinsicSourceStatus::ResourceLimit,
              "source-byte limit must refuse before publishing");
        limits = {};
        limits.max_boxes = 1;
        check(query(fixture, text_system, limits).status == IntrinsicSourceStatus::ResourceLimit,
              "box limit must refuse before publishing");
        limits = {};
        limits.max_measurements = 1;
        check(query(fixture, text_system, limits).status == IntrinsicSourceStatus::ResourceLimit,
              "measurement limit must refuse before publishing");
        limits = {};
        limits.max_work = 2;
        check(query(fixture, text_system, limits).status == IntrinsicSourceStatus::ResourceLimit,
              "work limit must refuse before publishing");
        limits = {};
        limits.max_work = 30;
        const auto repeated_measurements = query(fixture, text_system, limits);
        check(repeated_measurements.status == IntrinsicSourceStatus::ResourceLimit &&
              repeated_measurements.min_content_border_box_units == -1 &&
              repeated_measurements.max_content_border_box_units == -1 &&
              repeated_measurements.boxes_visited > 0 &&
              repeated_measurements.source_bytes > 0 &&
              repeated_measurements.measurements > 0 &&
              repeated_measurements.work > 0 &&
              repeated_measurements.work <= limits.max_work,
              "repeated font measurements must obey the query-wide work budget without partial results");
        limits = {};
        limits.max_scratch_bytes = 1;
        check(query(fixture, text_system, limits).status == IntrinsicSourceStatus::ResourceLimit,
              "scratch capacity must refuse before publishing");
        limits = {};
        limits.max_depth = 1;
        auto [span, span_node] = fixture.append_element(
            fixture.root_box, fixture.root_node, "span", Display::Inline);
        fixture.append_text(span, span_node, "A");
        check(query(fixture, text_system, limits).status == IntrinsicSourceStatus::ResourceLimit,
              "depth limit must refuse before publishing");
    }
    {
        Fixture fixture;
        fixture.root_box.style.overflow_wrap_anywhere = true;
        fixture.append_text(fixture.root_box, fixture.root_node, std::string(513, 'A'));
        IntrinsicSourceQueryLimits limits;
        limits.max_measurements = 512;
        const auto result = query(fixture, text_system, limits);
        check(result.status == IntrinsicSourceStatus::ResourceLimit &&
              result.min_content_border_box_units == -1 &&
              result.max_content_border_box_units == -1 &&
              result.measurements == limits.max_measurements,
              "per-character anywhere measurements must obey query-wide limits without partial results");
    }
    {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, "A");
        const auto before_text = fixture.root_node.children[0]->text;
        const auto before_style = fixture.root_box.children[0]->style;
        const auto result = query(fixture, text_system);
        check(result.status == IntrinsicSourceStatus::Complete, "source immutability case failed");
        check(fixture.root_node.children[0]->text == before_text &&
              fixture.root_box.children[0]->style.font_size == before_style.font_size,
              "source query mutated its input");
    }
}

void han_runs_and_refusals(const TextSystem& text_system) {
    {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, "\xE4\xB8\xAD\xE6\x96\x87");
        expect_widths(fixture, text_system, 16000, 32000);
    }
    {
        Fixture fixture;
        auto [span, span_node] = fixture.append_element(
            fixture.root_box, fixture.root_node, "span", Display::Inline);
        fixture.append_text(fixture.root_box, fixture.root_node, "\xE4\xB8\xAD");
        fixture.append_text(span, span_node, "\xE6\x96\x87");
        expect_widths(fixture, text_system, 16000, 32000);
    }
    {
        Fixture fixture;
        fixture.root_box.style.overflow_wrap_anywhere = true;
        fixture.append_text(fixture.root_box, fixture.root_node, "\xE4\xB8\xAD\xE6\x96\x87");
        expect_widths(fixture, text_system, 16000, 32000);
    }
    {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, "\xE4\xB8\xAD \xE6\x96\x87");
        expect_widths(fixture, text_system, 16000, 36000);
    }
    {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, "\xE4\xB8\xAD");
        fixture.append_break(fixture.root_box, fixture.root_node, "br");
        fixture.append_text(fixture.root_box, fixture.root_node, "\xE6\x96\x87");
        expect_widths(fixture, text_system, 16000, 16000);
    }
    for (const std::string text : {
             std::string("A\xE4\xB8\xAD"), std::string("\xE4\xB8\xAD A"),
             std::string("\xE4\xB8\xAD\xE3\x80\x82"),
             std::string("\xE4\xB8\xAD\xCC\x81"), std::string("\xF0\x9F\x98\x80")}) {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, text);
        check(query(fixture, text_system).status == IntrinsicSourceStatus::Unsupported,
              "mixed scripts, punctuation, combining marks and emoji must refuse");
    }
    for (const std::string text : {std::string("A\vB"),
             std::string("\xE4\xB8\xAD\n\xE6\x96\x87"),
             std::string("\n\xE4\xB8\xAD")}) {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, text);
        const auto result = query(fixture, text_system);
        check(result.status == IntrinsicSourceStatus::Unsupported &&
              result.min_content_border_box_units == -1 && result.max_content_border_box_units == -1,
              "vertical tab and unmodeled Han segment breaks must refuse atomically");
    }
    {
        Fixture fixture;
        fixture.append_text(fixture.root_box, fixture.root_node, std::string("\xE4\xB8", 2));
        check(query(fixture, text_system).status == IntrinsicSourceStatus::InvalidInput,
              "malformed UTF-8 must be reported as invalid input");
    }
    {
        Fixture fixture;
        fixture.root_box.style.text_transform = TextTransform::Uppercase;
        fixture.append_text(fixture.root_box, fixture.root_node, "\xE4\xB8\xAD");
        check(query(fixture, text_system).status == IntrinsicSourceStatus::Unsupported,
              "Han text with a text transform must refuse");
    }
    {
        Fixture fixture;
        auto [span, span_node] = fixture.append_element(
            fixture.root_box, fixture.root_node, "span", Display::Inline);
        fixture.append_text(fixture.root_box, fixture.root_node, "\xE4\xB8\xAD");
        fixture.append_text(span, span_node, "A");
        check(query(fixture, text_system).status == IntrinsicSourceStatus::Unsupported,
              "mixed script refusal must persist across inline partitions");
    }
}

void exact_limits_and_cancellation(const TextSystem& system) {
    Fixture f;
    f.append_text(f.root_box, f.root_node, "AB");
    const auto complete = query(f, system);
    check(complete.status == IntrinsicSourceStatus::Complete, "threshold reference completes");
    for (auto member : {&IntrinsicSourceQueryLimits::max_boxes,
                        &IntrinsicSourceQueryLimits::max_source_bytes,
                        &IntrinsicSourceQueryLimits::max_measurements,
                        &IntrinsicSourceQueryLimits::max_work,
                        &IntrinsicSourceQueryLimits::max_scratch_bytes}) {
        const std::size_t exact = member == &IntrinsicSourceQueryLimits::max_boxes ? complete.boxes_visited :
            member == &IntrinsicSourceQueryLimits::max_source_bytes ? complete.source_bytes :
            member == &IntrinsicSourceQueryLimits::max_measurements ? complete.measurements :
            member == &IntrinsicSourceQueryLimits::max_work ? complete.work : 2;
        IntrinsicSourceQueryLimits limits;
        limits.*member = exact;
        check(query(f, system, limits).status == IntrinsicSourceStatus::Complete,
              "exact width budget threshold must complete");
        limits.*member = exact - 1;
        const auto refused = query(f, system, limits);
        check(refused.status == (exact == 1 ? IntrinsicSourceStatus::InvalidInput : IntrinsicSourceStatus::ResourceLimit) &&
              refused.min_content_border_box_units == -1 && refused.max_content_border_box_units == -1,
              "one below width budget threshold must refuse atomically");
    }
    f.root_node.parent = &f.root_node;
    const auto cycle = query(f, system);
    f.root_node.parent = nullptr;
    check(cycle.status == IntrinsicSourceStatus::ResourceLimit &&
          cycle.min_content_border_box_units == -1, "cyclic ancestor context must refuse within depth budget");
    f.root_node.set_attribute("dir", std::string(100000, 'x'));
    const auto direction = query(f, system);
    f.root_node.remove_attribute("dir");
    check(direction.status == IntrinsicSourceStatus::Unsupported && direction.max_content_border_box_units == -1,
          "large direction value must refuse without a query-owned copy");
    f.root_box.style.text_decoration_underline = true;
    check(query(f, system).status == IntrinsicSourceStatus::Unsupported, "decorated text must refuse");
    f.root_box.style.text_decoration_underline = false;
    std::atomic_bool cancel{true};
    IntrinsicSourceQueryLimits limits; limits.cancellation = &cancel;
    const auto cancelled = query(f, system, limits);
    check(cancelled.status == IntrinsicSourceStatus::Cancelled && cancelled.boxes_visited == 0 &&
          cancelled.min_content_border_box_units == -1 && cancelled.max_content_border_box_units == -1,
          "cancelled width query must not begin or publish dimensions");
    cancel.store(false);
    check(query(f, system, limits).status == IntrinsicSourceStatus::Complete, "uncancelled width completes");
}

} // namespace

int main() {
    try {
        auto font = make_font();
        TextFontFace face;
        auto text_system = bind_font(font, face);
        exact_limits_and_cancellation(text_system);
        percentage_basis_contract(text_system);
        fractional_percentage_pipeline(text_system);
        unsupported_original_size_kinds();
        text_runs_and_breaks(text_system);
        han_runs_and_refusals(text_system);
        refusals_and_budgets(text_system);
        std::cout << "intrinsic source query ASCII/Han runs, breaks, refusals and budgets passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}

