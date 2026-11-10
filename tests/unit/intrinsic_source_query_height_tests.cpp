#include "../../src/intrinsic_source_query_height.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <initializer_list>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

using namespace jellyframe;
using namespace jellyframe::detail;

namespace { bool fail_query_array_allocation = false; }

void* operator new[](std::size_t size) {
    if (fail_query_array_allocation) throw std::bad_alloc();
    return ::operator new(size);
}
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { ::operator delete(pointer); }

namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void u16(std::vector<std::uint8_t>& b, std::size_t at, unsigned v) {
    b[at] = static_cast<std::uint8_t>(v & 255U);
    b[at + 1] = static_cast<std::uint8_t>((v >> 8U) & 255U);
}
void u32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) b[at + i] = static_cast<std::uint8_t>((v >> (8U * i)) & 255U);
}
struct FontAsset { std::vector<std::uint8_t> bytes; FontResource font; };
FontAsset make_font() {
    constexpr std::uint32_t cps[] = {32, 45, 65, 66, 67, 68};
    constexpr unsigned advance = 600;
    constexpr std::size_t bitmap = 128 + 6 * 40;
    FontAsset asset;
    asset.bytes.resize(bitmap, 0);
    auto& b = asset.bytes;
    const char magic[] = "JFFONT2";
    for (unsigned i = 0; i < 8; ++i) b[i] = static_cast<std::uint8_t>(magic[i]);
    u16(b, 8, 2); u16(b, 10, 128); u16(b, 12, 40); u16(b, 14, 1);
    u32(b, 20, 6); u32(b, 24, 128); u32(b, 28, static_cast<std::uint32_t>(bitmap));
    u16(b, 36, 1000); u16(b, 38, 16); u16(b, 40, 400); b[42] = 4;
    u32(b, 44, 800); u32(b, 48, 200); u32(b, 52, 50); b[64] = 1;
    u32(b, 100, 1); u16(b, 104, 1);
    for (std::size_t i = 0; i < 6; ++i) {
        const auto at = 128 + i * 40;
        u32(b, at, cps[i]); u32(b, at + 4, static_cast<std::uint32_t>(i + 1));
        u32(b, at + 8, cps[i] == 32 ? 300U : advance);
        u32(b, at + 28, static_cast<std::uint32_t>(b.size() - bitmap));
        if (cps[i] != 32) {
            u32(b, at + 12, static_cast<std::uint32_t>(-1));
            u32(b, at + 16, static_cast<std::uint32_t>(-2));
            u16(b, at + 20, 1); u16(b, at + 22, 2);
            u32(b, at + 24, 1); u32(b, at + 32, 2);
            b.push_back(0xf0); b.push_back(0xf0);
        }
    }
    u32(b, 16, static_cast<std::uint32_t>(b.size()));
    u32(b, 32, static_cast<std::uint32_t>(b.size() - bitmap));
    std::uint32_t crc = 0xffffffffU;
    for (auto byte : b) {
        crc ^= byte;
        for (unsigned j = 0; j < 8; ++j) crc = (crc >> 1U) ^ ((crc & 1U) ? 0xedb88320U : 0U);
    }
    u32(b, 96, ~crc);
    check(asset.font.load(b.data(), b.size()) == FontResourceStatus::Loaded, "test font load failed");
    return asset;
}
struct Fixture {
    Node root_node{NodeType::Element};
    LayoutBox root;
    Fixture() {
        root_node.tag_name = "div";
        root.node = &root_node;
        root.style.display = Display::Block;
        root.style.font_size = 16;
        root.style.font_weight = 400;
    }
    LayoutBox& element(LayoutBox& parent, Node& parent_node, std::string tag, Display display) {
        Node& node = parent_node.append_child(make_element(std::move(tag)));
        LayoutBoxPtr child(new LayoutBox, LayoutBoxDeleter{false});
        child->node = &node;
        child->style = parent.style;
        child->style.display = display;
        parent.children.push_back(std::move(child));
        return *parent.children.back();
    }
    void text(LayoutBox& parent, Node& parent_node, std::string value) {
        Node& node = parent_node.append_child(make_text(std::move(value)));
        LayoutBoxPtr child(new LayoutBox, LayoutBoxDeleter{false});
        child->node = &node;
        child->style = parent.style;
        child->style.display = Display::Inline;
        parent.children.push_back(std::move(child));
    }
};
TextSystem bind(FontAsset& asset, TextFontFace& face) {
    face = TextFontFace{1, 0, asset.font};
    TextSystem system;
    check(system.bind(&face, 1) == TextSystemStatus::Ok, "text system bind failed");
    return system;
}
IntrinsicSourceHeightResult query(Fixture& f, const TextSystem& system,
                                  std::int64_t width = 32000,
                                  IntrinsicSourceQueryLimits limits = {}) {
    return query_intrinsic_source_height(f.root, system, width, limits);
}
void verify_oracle_shapes(const TextSystem& system) {
    {
        Fixture f;
        f.text(f.root, f.root_node, "A B C D");
        auto r = query(f, system, 32000);
        check(r.status == IntrinsicSourceStatus::Complete && r.border_box_block_size_units == 33600,
              "narrow normal text should form two Core-natural-height lines");
        r = query(f, system, 96000);
        check(r.status == IntrinsicSourceStatus::Complete && r.border_box_block_size_units == 16800,
              "wide normal text should remain one line");
    }
    {
        Fixture f;
        f.root.style.line_height = 20;
        f.text(f.root, f.root_node, "A B C D");
        const auto r = query(f, system, 32000);
        check(r.status == IntrinsicSourceStatus::Complete && r.border_box_block_size_units == 40000,
              "explicit 20px line-height should produce two 20px lines");
    }
    {
        Fixture f;
        f.root.style.line_height = 0;
        f.text(f.root, f.root_node, "A B C D");
        const auto r = query(f, system, 32000);
        check(r.status == IntrinsicSourceStatus::Complete && r.border_box_block_size_units == 0,
              "explicit zero line-height must not fall back to normal metrics");
    }
    for (const std::int64_t width : {32000, 96000}) {
        Fixture f;
        f.text(f.root, f.root_node, "A B");
        auto& br = f.element(f.root, f.root_node, "br", Display::Inline);
        (void)br;
        f.text(f.root, f.root_node, "C D");
        const auto r = query(f, system, width);
        check(r.status == IntrinsicSourceStatus::Complete && r.border_box_block_size_units == 33600,
              "br must preserve two lines independent of available width");
    }
    {
        Fixture f;
        f.text(f.root, f.root_node, "A");
        (void)f.element(f.root, f.root_node, "br", Display::Inline);
        (void)f.element(f.root, f.root_node, "br", Display::Inline);
        const auto r = query(f, system, 96000);
        check(r.status == IntrinsicSourceStatus::Complete && r.border_box_block_size_units == 33600,
              "two trailing br elements complete two lines without a phantom third line");
    }
    {
        Fixture f;
        auto& span = f.element(f.root, f.root_node, "span", Display::Inline);
        f.text(f.root, f.root_node, "A ");
        f.text(span, *f.root_node.children.back(), "B C");
        const auto r = query(f, system, 32000);
        check(r.status == IntrinsicSourceStatus::Complete && r.border_box_block_size_units == 33600,
              "collapsible spaces across neutral inline partitions wrap as one text run");
    }
    {
        Fixture f;
        f.text(f.root, f.root_node, "A- B");
        const auto r = query(f, system, 32000);
        check(r.status == IntrinsicSourceStatus::Complete && r.border_box_block_size_units == 33600,
              "hyphen break plus following collapsed space retains the space advance");
    }
    {
        Fixture anywhere;
        anywhere.root.style.overflow_wrap_anywhere = true;
        anywhere.text(anywhere.root, anywhere.root_node, "ABCD");
        const auto wrapped = query(anywhere, system, 20000);
        check(wrapped.status == IntrinsicSourceStatus::Complete &&
              wrapped.border_box_block_size_units == 33600,
              "overflow-wrap:anywhere breaks a long ASCII token at scalar boundaries");
        Fixture ordinary;
        ordinary.text(ordinary.root, ordinary.root_node, "ABCD");
        const auto unbroken = query(ordinary, system, 20000);
        check(unbroken.status == IntrinsicSourceStatus::Complete &&
              unbroken.border_box_block_size_units == 16800,
              "ordinary unbreakable token remains on one overflowing line");
    }
    {
        Fixture f;
        f.text(f.root, f.root_node, "A");
        auto& block = f.element(f.root, f.root_node, "section", Display::Block);
        f.text(block, *f.root_node.children.back(), "B");
        f.text(f.root, f.root_node, "C");
        const auto r = query(f, system, 96000);
        check(r.status == IntrinsicSourceStatus::Complete && r.border_box_block_size_units == 50400,
              "simple block child creates separate hard-break lines");
    }
}
void verify_browser_break_heights(const TextSystem& system) {
    // Line counts independently observed at 20px/20px in the localhost fixture
    // docs/evaluation/2026-11-09-empty-break-height-oracle.html.
    struct Case { std::initializer_list<const char*> tokens; int lines; };
    const Case cases[] = {
        {{}, 0}, {{" \t\n "}, 0}, {{"br"}, 1}, {{"br", "br"}, 2},
        {{"br", "br", "br"}, 3}, {{"A"}, 1}, {{"A", "br"}, 1},
        {{"A", "br", "br"}, 2}, {{"br", "A"}, 2},
        {{"A", "br", "B"}, 2}, {{"wbr"}, 1}, {{"wbr", "wbr"}, 1},
        {{"A", "wbr"}, 1}, {{"br", "wbr"}, 2}, {{"wbr", "br"}, 1},
        {{"wbr", "br", "wbr"}, 2}, {{"A", "br", " "}, 1}
    };
    for (int explicit_height : {-1, 0, 20}) {
        for (const auto& test : cases) {
            Fixture f;
            f.root.style.line_height = explicit_height;
            for (const char* token : test.tokens) {
                const std::string value(token);
                if (value == "br" || value == "wbr")
                    (void)f.element(f.root, f.root_node, value, Display::Inline);
                else f.text(f.root, f.root_node, value);
            }
            const auto r = query(f, system, 96000);
            const std::int64_t line_height = explicit_height < 0 ? 16800 : explicit_height * 1000;
            if (r.status != IntrinsicSourceStatus::Complete ||
                r.border_box_block_size_units != test.lines * line_height)
                throw std::runtime_error("browser break-height case mismatch at " +
                    std::to_string(&test - cases) + ", line-height " + std::to_string(explicit_height));
        }
    }
    for (int shape = 0; shape < 4; ++shape) {
        Fixture f;
        f.root.style.line_height = 20;
        if (shape == 0) {
            for (int i = 0; i < 2; ++i) {
                auto& span = f.element(f.root, f.root_node, "span", Display::Inline);
                (void)f.element(span, *f.root_node.children.back(), "br", Display::Inline);
            }
        } else if (shape == 1 || shape == 3) {
            f.text(f.root, f.root_node, "A");
            (void)f.element(f.root, f.root_node, "br", Display::Inline);
            auto& block = f.element(f.root, f.root_node, "section", Display::Block);
            if (shape == 1) f.text(block, *f.root_node.children.back(), "B");
        } else {
            auto& block = f.element(f.root, f.root_node, "section", Display::Block);
            f.text(block, *f.root_node.children.back(), "A");
            (void)f.element(block, *f.root_node.children.back(), "br", Display::Inline);
            f.text(f.root, f.root_node, "B");
        }
        const auto r = query(f, system, 96000);
        check(r.status == IntrinsicSourceStatus::Complete &&
              r.border_box_block_size_units == (shape == 3 ? 20000 : 40000),
              "neutral span/block boundaries must preserve browser hard-break line counts");
    }
}
void verify_decoration_and_refusals(const TextSystem& system) {
    for (int variant = 0; variant < 5; ++variant) {
        Fixture f;
        auto& child = f.element(f.root, f.root_node, variant == 3 ? "li" : "section", Display::Block);
        if (variant == 0) child.style.text_indent = 1;
        if (variant == 1) child.style.list_style_type = ListStyleType::Disc;
        if (variant == 2) child.style.aspect_ratio_auto = false;
        if (variant == 4) f.root.style.aspect_ratio_auto = false;
        const auto r = query(f, system);
        check(r.status == IntrinsicSourceStatus::Unsupported && r.border_box_block_size_units == -1,
              "unmodeled indent, marker and aspect-ratio source must refuse atomically");
    }
    {
        Fixture f;
        f.text(f.root, f.root_node, "A\vB");
        const auto r = query(f, system);
        check(r.status == IntrinsicSourceStatus::Unsupported && r.border_box_block_size_units == -1,
              "vertical tab must not be collapsed as CSS whitespace");
    }
    for (const char* tag : {"br", "wbr"}) {
        for (bool preceding_text : {false, true}) {
            Fixture f;
            if (preceding_text) f.text(f.root, f.root_node, "A");
            (void)f.element(f.root, f.root_node, tag, Display::Block);
            const auto r = query(f, system, 96000);
            check(r.status == IntrinsicSourceStatus::Unsupported &&
                  r.border_box_block_size_units == -1,
                  "block break tags must refuse without partial height");
        }
    }
    {
        Fixture f;
        f.root.style.padding.top = 2;
        f.root.style.padding.bottom = 3;
        f.root.style.border_width.top = 1;
        f.root.style.border_width.bottom = 2;
        f.root.style.margin.top = 99;
        f.text(f.root, f.root_node, "A");
        const auto r = query(f, system);
        check(r.status == IntrinsicSourceStatus::Complete && r.border_box_block_size_units == 24800,
              "result includes vertical padding/border and excludes margin");
    }
    {
        Fixture f;
        f.root.style.height_percent = 50;
        f.text(f.root, f.root_node, "A");
        check(query(f, system).status == IntrinsicSourceStatus::Unsupported,
              "unknown percentage block-size provenance must refuse");
    }
    {
        Fixture f;
        f.root.style.padding.top = 2;
        f.root.style.border_width.bottom = 1;
        const auto empty = query(f, system);
        check(empty.status == IntrinsicSourceStatus::Complete &&
              empty.border_box_block_size_units == 3000 && empty.measurements == 0,
              "empty source contributes only root decoration without font measurement");
        f.text(f.root, f.root_node, " \t\n ");
        const auto collapsed = query(f, system);
        check(collapsed.status == IntrinsicSourceStatus::Complete &&
              collapsed.border_box_block_size_units == 3000 && collapsed.measurements == 0,
              "collapsed whitespace-only source contributes no line box");
    }
    {
        Fixture f;
        (void)f.element(f.root, f.root_node, "br", Display::Inline);
        const auto r = query(f, system);
        check(r.status == IntrinsicSourceStatus::Complete &&
              r.border_box_block_size_units == 16800,
              "a source consisting only of br contributes one empty line");
    }
    {
        Fixture f;
        f.text(f.root, f.root_node, "A");
        auto& block = f.element(f.root, f.root_node, "section", Display::Block);
        block.style.padding.top = 1;
        check(query(f, system).status == IntrinsicSourceStatus::Unsupported,
              "decorated nested Block must refuse");
    }
    {
        Fixture f;
        f.text(f.root, f.root_node, "A");
        (void)f.element(f.root, f.root_node, "section", Display::Flex);
        check(query(f, system).status == IntrinsicSourceStatus::Unsupported,
              "nested Flex must refuse");
    }
    {
        Fixture f;
        f.text(f.root, f.root_node, "A");
        auto& child = f.element(f.root, f.root_node, "section", Display::Block);
        child.style.width_percent = 50;
        check(query(f, system).status == IntrinsicSourceStatus::Unsupported,
              "descendant percentage size with unmodeled provenance must refuse");
    }
    {
        Fixture f;
        f.text(f.root, f.root_node, "A");
        auto& child = f.element(f.root, f.root_node, "section", Display::Block);
        child.style.position_type = PositionType::Absolute;
        check(query(f, system).status == IntrinsicSourceStatus::Unsupported,
              "positioned descendant must refuse rather than disappear from the source");
    }
    {
        Fixture f;
        f.text(f.root, f.root_node, "A");
        auto& span = f.element(f.root, f.root_node, "span", Display::Inline);
        span.style.font_weight = 700;
        f.text(span, *f.root_node.children.back(), "B");
        check(query(f, system).status == IntrinsicSourceStatus::Unsupported,
              "mixed font metrics must refuse");
    }
    {
        Fixture f;
        f.text(f.root, f.root_node, "A");
        check(query(f, system, -1).status == IntrinsicSourceStatus::InvalidInput,
              "negative explicit inline size must be invalid");
        IntrinsicSourceQueryLimits limits; limits.max_boxes = 0;
        const auto r = query(f, system, 32000, limits);
        check(r.status == IntrinsicSourceStatus::InvalidInput &&
              r.border_box_block_size_units == -1,
              "zero budget must be rejected without partial dimensions");
    }
    {
        Fixture f;
        f.text(f.root, f.root_node, "AB");
        IntrinsicSourceQueryLimits limits; limits.max_boxes = 1;
        const auto r = query(f, system, 32000, limits);
        check(r.status == IntrinsicSourceStatus::ResourceLimit &&
              r.border_box_block_size_units == -1,
              "box exhaustion must not expose a partial block size");
    }
    {
        Fixture f;
        f.text(f.root, f.root_node, "AB");
        IntrinsicSourceQueryLimits limits; limits.max_source_bytes = 1;
        const auto r = query(f, system, 32000, limits);
        check(r.status == IntrinsicSourceStatus::ResourceLimit &&
              r.border_box_block_size_units == -1,
              "source-byte exhaustion must not expose a partial block size");
    }
    {
        Fixture f;
        f.text(f.root, f.root_node, "AB");
        IntrinsicSourceQueryLimits limits; limits.max_measurements = 1;
        const auto r = query(f, system, 32000, limits);
        check(r.status == IntrinsicSourceStatus::ResourceLimit &&
              r.border_box_block_size_units == -1,
              "measurement exhaustion must not expose a partial block size");
    }
    {
        Fixture f;
        f.text(f.root, f.root_node, "?");
        const auto r = query(f, system);
        check(r.status == IntrinsicSourceStatus::MeasurementFailure &&
              r.border_box_block_size_units == -1,
              "missing glyph must fail measurement without partial dimensions");
    }
    {
        Fixture f;
        f.root.style.text_transform = TextTransform::Uppercase;
        f.text(f.root, f.root_node, "ab");
        check(query(f, system, 96000).status == IntrinsicSourceStatus::Complete,
              "supported ASCII uppercase transform must be measured by Core Text");
    }
    {
        Fixture f;
        f.text(f.root, f.root_node, "\xE4\xB8\xad");
        check(query(f, system).status == IntrinsicSourceStatus::Unsupported,
              "Han remains outside the initial column source profile");
    }
}
void exact_limits_and_cancellation(const TextSystem& system) {
    Fixture f; f.text(f.root, f.root_node, "AB");
    const auto complete = query(f, system);
    check(complete.status == IntrinsicSourceStatus::Complete, "height threshold reference completes");
    for (auto member : {&IntrinsicSourceQueryLimits::max_boxes,
                        &IntrinsicSourceQueryLimits::max_source_bytes,
                        &IntrinsicSourceQueryLimits::max_measurements,
                        &IntrinsicSourceQueryLimits::max_work,
                        &IntrinsicSourceQueryLimits::max_scratch_bytes}) {
        const std::size_t exact = member == &IntrinsicSourceQueryLimits::max_boxes ? complete.boxes_visited :
            member == &IntrinsicSourceQueryLimits::max_source_bytes ? complete.source_bytes :
            member == &IntrinsicSourceQueryLimits::max_measurements ? complete.measurements :
            member == &IntrinsicSourceQueryLimits::max_work ? complete.work : 2;
        IntrinsicSourceQueryLimits limits; limits.*member = exact;
        check(query(f, system, 32000, limits).status == IntrinsicSourceStatus::Complete,
              "exact height budget threshold must complete");
        limits.*member = exact - 1;
        const auto refused = query(f, system, 32000, limits);
        check(refused.status == IntrinsicSourceStatus::ResourceLimit && refused.border_box_block_size_units == -1,
              "one below height budget threshold must refuse atomically");
    }
    f.root_node.parent = &f.root_node;
    const auto cycle = query(f, system);
    f.root_node.parent = nullptr;
    check(cycle.status == IntrinsicSourceStatus::ResourceLimit && cycle.border_box_block_size_units == -1,
          "cyclic height ancestor context must refuse within depth budget");
    f.root.style.text_decoration_line_through = true;
    check(query(f, system).status == IntrinsicSourceStatus::Unsupported, "height decorated text must refuse");
    f.root.style.text_decoration_line_through = false;
    std::atomic_bool cancel{true};
    IntrinsicSourceQueryLimits limits; limits.cancellation = &cancel;
    const auto cancelled = query(f, system, 32000, limits);
    check(cancelled.status == IntrinsicSourceStatus::Cancelled && cancelled.boxes_visited == 0 &&
          cancelled.border_box_block_size_units == -1, "cancelled height query must refuse atomically");

    fail_query_array_allocation = true;
    const auto failed_height = query(f, system);
    const auto failed_width = query_intrinsic_source_width(f.root, system,
        {IntrinsicPercentageBasisKind::CyclicSameAxis, -1});
    fail_query_array_allocation = false;
    check(failed_height.status == IntrinsicSourceStatus::ResourceLimit &&
          failed_height.border_box_block_size_units == -1 &&
          failed_width.status == IntrinsicSourceStatus::ResourceLimit &&
          failed_width.min_content_border_box_units == -1 &&
          failed_width.max_content_border_box_units == -1,
          "scratch allocation failure must return from both noexcept queries without partial output");

    Fixture markers;
    markers.text(markers.root, markers.root_node, "A");
    for (int i = 0; i < 30; ++i) (void)markers.element(markers.root, markers.root_node, "wbr", Display::Inline);
    markers.text(markers.root, markers.root_node, std::string(30, ' ') + "B");
    for (int i = 0; i < 20; ++i) markers.root_node.set_attribute("a" + std::to_string(i), "value");
    const auto marker_reference = query(markers, system);
    check(marker_reference.status == IntrinsicSourceStatus::Complete, "marker budget reference completes");
    limits = {};
    limits.max_work = marker_reference.work;
    check(query(markers, system, 32000, limits).status == IntrinsicSourceStatus::Complete,
          "marker and ancestor-attribute work completes at the exact budget");
    --limits.max_work;
    const auto marker_refused = query(markers, system, 32000, limits);
    check(marker_refused.status == IntrinsicSourceStatus::ResourceLimit &&
          marker_refused.work <= limits.max_work && marker_refused.border_box_block_size_units == -1,
          "stream and repeated whitespace scans must stop at the shared work limit");
}

void named_family_budget(FontAsset& asset) {
    TextFontFace faces[] = {{1, 0, asset.font}, {2, 7, asset.font}};
    faces[1].css_family_hash = 123;
    TextSystem system;
    check(system.bind(faces, 2) == TextSystemStatus::Ok, "named font budget setup failed");
    Fixture f;
    f.root.style.font_family_hash = 123;
    f.text(f.root, f.root_node, "AB");
    const auto reference = query(f, system);
    check(reference.status == IntrinsicSourceStatus::Complete, "named font query reference completes");
    IntrinsicSourceQueryLimits limits;
    limits.max_work = reference.work;
    check(query(f, system, 32000, limits).status == IntrinsicSourceStatus::Complete,
          "named family resolution shares the exact query-wide budget");
    --limits.max_work;
    const auto refused = query(f, system, 32000, limits);
    check(refused.status == IntrinsicSourceStatus::ResourceLimit &&
          refused.work <= limits.max_work && refused.border_box_block_size_units == -1,
          "named family query refuses atomically below its complete work budget");
}

}

int main() {
    try {
        auto asset = make_font();
        TextFontFace face;
        const auto system = bind(asset, face);
        named_family_budget(asset);
        exact_limits_and_cancellation(system);
        verify_browser_break_heights(system);
        verify_oracle_shapes(system);
        verify_decoration_and_refusals(system);
        std::cout << "intrinsic source height wrapping, decoration, refusals and budgets passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
