#include "core_text_inline_source_map.h"
#include "contextual_text_inline_geometry.h"
#include "contextual_text_fixture.h"
#include "render_core/software_renderer.h"

#include <iostream>

using namespace jellyframe;
using namespace jellyframe::detail;
using namespace jellyframe::contextual_text;
using namespace shaped_binding_fixture;

namespace {
constexpr std::uint64_t budget = 32U * 1024U * 1024U;

void check_cross_owner_ligature(const std::string& fixture_root) {
    Fixture fixture(fixture_root, "regular", true);
    auto work = budget;
    Binding binding;
    check(create_binding(fixture.as_inputs(), {}, work, binding) == Status::ok,
        "create contextual binding");

    ParagraphOptions options;
    options.size_q1000 = 24000;
    options.width_q1000 = 240000;
    Paragraph shaped;
    work = budget;
    check(layout_paragraph(binding, "office", options, {}, work, shaped) == Status::ok &&
        shaped.lines().size() == 1, "shape a real JFSP2 line");

    CoreTextParagraph source;
    source.normalized = "office";
    source.sources = {
        {0, 0, 1}, {0, 1, 2},
        {1, 0, 1}, {1, 1, 2}, {1, 2, 3}, {1, 3, 4}
    };
    const std::string_view owners[] = {"of", "fice"};
    CoreTextClusterMap map;
    work = budget;
    check(map_experimental_contextual_inline_line(source, owners, 2, shaped, 0,
        map, {}, &work) == CoreTextInlineSourceMapStatus::Complete,
        "map decoded JFSP2 clusters to paragraph owner ranges");

    bool found_cross_owner_cluster = false;
    for (const auto& cluster : map.clusters) {
        if (cluster.owner_span_count < 2) continue;
        const auto& first = map.owner_spans[cluster.first_owner_span];
        const auto& second = map.owner_spans[cluster.first_owner_span + 1];
        if (first.owner == 0 && second.owner == 1) {
            check(first.source_begin == 1 && first.source_end == 2 &&
                second.source_begin == 0 && second.source_end == 2,
                "cross-owner ligature keeps owner-local byte ranges");
            found_cross_owner_cluster = true;
        }
    }
    check(found_cross_owner_cluster, "font fixture produced a cluster spanning owners");
}

void maps_collapsed_and_consumed_whitespace_as_omitted(const std::string& fixture_root) {
    Fixture fixture(fixture_root, "regular", true);
    auto work = budget;
    Binding binding;
    check(create_binding(fixture.as_inputs(), {}, work, binding) == Status::ok,
        "create whitespace contextual binding");

    ParagraphOptions options;
    options.size_q1000 = 16000;
    options.width_q1000 = 240000;
    Paragraph shaped;
    work = budget;
    check(layout_paragraph(binding, "A  B ", options, {}, work, shaped) == Status::ok &&
        shaped.text() == "A B", "contextual layout collapses and trims whitespace");

    CoreTextParagraph source;
    source.normalized = "A B";
    source.sources = {{0, 0, 1}, {0, 1, 2}, {1, 1, 2}};
    const std::string_view owners[] = {"A  ", " B "};
    CoreTextClusterMap map;
    work = budget;
    check(map_experimental_contextual_inline_line(source, owners, 2, shaped, 0,
        map, {}, &work) == CoreTextInlineSourceMapStatus::Complete,
        "map contextual line with whitespace source records");

    std::vector<CoreTextClusterMapToken> tokens;
    check(build_core_text_inline_source_tokens(source, owners, 2, tokens) ==
        CoreTextInlineSourceMapStatus::Complete, "build whitespace source tokens");
    bool saw_omitted = false;
    for (const auto& token : tokens) {
        if (token.omitted) saw_omitted = true;
    }
    check(saw_omitted, "collapsed and trimmed bytes are explicit omitted tokens");
}

void maps_utf8_owner_ranges_on_scalar_boundaries(const std::string& fixture_root) {
    Fixture fixture(fixture_root, "regular", true);
    auto work = budget;
    Binding binding;
    check(create_binding(fixture.as_inputs(), {}, work, binding) == Status::ok,
        "create UTF-8 contextual binding");
    ParagraphOptions options;
    options.size_q1000 = 24000;
    options.width_q1000 = 240000;
    Paragraph shaped;
    work = budget;
    check(layout_paragraph(binding, "A\xc3\xa9", options, {}, work, shaped) == Status::ok &&
        shaped.lines().size() == 1, "shape multibyte UTF-8 line");

    CoreTextParagraph source;
    source.normalized = "A\xc3\xa9";
    source.sources = {{0, 0, 1}, {1, 0, 2}};
    source.byte_offsets = {0, 1};
    const std::string_view owners[] = {"A", "\xc3\xa9"};
    CoreTextClusterMap map;
    work = budget;
    check(map_experimental_contextual_inline_line(source, owners, 2, shaped, 0,
        map, {}, &work) == CoreTextInlineSourceMapStatus::Complete,
        "map UTF-8 glyph intervals across scalar-boundary owners");
    bool found_accent_owner = false;
    for (const auto& span : map.owner_spans) {
        if (span.owner == 1) {
            check(span.source_begin == 0 && span.source_end == 2,
                "multibyte owner span uses original UTF-8 byte offsets");
            found_accent_owner = true;
        }
    }
    check(found_accent_owner, "multibyte owner survives contextual mapping");
}

void refuses_text_mismatch_transactionally(const std::string& fixture_root) {
    Fixture fixture(fixture_root, "regular", true);
    auto work = budget;
    Binding binding;
    check(create_binding(fixture.as_inputs(), {}, work, binding) == Status::ok,
        "create mismatch contextual binding");
    Paragraph shaped;
    ParagraphOptions options;
    work = budget;
    check(layout_paragraph(binding, "different", options, {}, work, shaped) == Status::ok,
        "shape mismatch baseline");

    CoreTextParagraph source;
    source.normalized = "source";
    source.sources = {{0, 0, 6}};
    source.byte_offsets = {0};
    const std::string_view owners[] = {"source"};
    CoreTextClusterMap output;
    const std::string baseline_text = "A";
    const CoreTextClusterMapToken baseline_token[] = {{0, 1, 0, 0, 1, false}};
    const CoreTextClusterMapGlyph baseline_glyph[] = {{1, 0, 1}};
    check(map_core_text_clusters(baseline_text, baseline_token, 1, baseline_glyph, 1,
        output) == CoreTextClusterMapStatus::Complete, "publish prior map baseline");
    const auto* prior = output.clusters.data();
    const auto prior_size = output.clusters.size();
    check(map_experimental_contextual_inline_line(source, owners, 1, shaped, 0,
        output) == CoreTextInlineSourceMapStatus::UnsupportedNormalization &&
        output.clusters.data() == prior && output.clusters.size() == prior_size,
        "normalized text mismatch refuses without publishing");
}

void refuses_malformed_source_tables_and_work_exhaustion(const std::string& fixture_root) {
    CoreTextParagraph malformed;
    malformed.normalized = "A";
    malformed.sources = {{0, 0, 2}};
    const std::string_view short_owner[] = {"A"};
    std::vector<CoreTextClusterMapToken> tokens{{0, 1, 42, 4, 5, false}};
    const auto* prior_tokens = tokens.data();
    check(build_core_text_inline_source_tokens(malformed, short_owner, 1, tokens) ==
        CoreTextInlineSourceMapStatus::InvalidInput && tokens.data() == prior_tokens &&
        tokens.size() == 1 && tokens[0].owner == 42,
        "malformed owner source range preserves token output");

    malformed.normalized = "AB";
    malformed.sources = {{0, 0, 1}, {0, 1, 2}};
    malformed.byte_offsets = {0};
    const std::string_view two_chars[] = {"AB"};
    check(build_core_text_inline_source_tokens(malformed, two_chars, 1, tokens) ==
        CoreTextInlineSourceMapStatus::InvalidInput && tokens.data() == prior_tokens,
        "truncated byte-offset table refuses without indexing or publication");

    malformed.normalized = "AB";
    malformed.sources = {{0, 0, 1}, {0, 2, 3}};
    malformed.byte_offsets.clear();
    const std::string_view nonwhitespace_gap[] = {"AXB"};
    check(build_core_text_inline_source_tokens(malformed, nonwhitespace_gap, 1, tokens) ==
        CoreTextInlineSourceMapStatus::UnsupportedNormalization && tokens.data() == prior_tokens,
        "omitted source includes only collapsible whitespace");

    auto tiny_work = std::uint64_t{1};
    check(build_core_text_inline_source_tokens(malformed, nonwhitespace_gap, 1, tokens,
        &tiny_work) == CoreTextInlineSourceMapStatus::WorkLimit && tokens.data() == prior_tokens,
        "token construction respects the shared work budget");

    Fixture fixture(fixture_root, "regular", true);
    auto work = budget;
    Binding binding;
    check(create_binding(fixture.as_inputs(), {}, work, binding) == Status::ok,
        "create over-budget contextual binding");
    Paragraph shaped;
    ParagraphOptions options;
    options.size_q1000 = 24000;
    options.width_q1000 = 240000;
    work = budget;
    check(layout_paragraph(binding, "office", options, {}, work, shaped) == Status::ok,
        "shape over-budget mapping baseline");
    CoreTextParagraph source;
    source.normalized = "office";
    source.sources = {{0, 0, 1}, {0, 1, 2}, {1, 0, 1}, {1, 1, 2}, {1, 2, 3}, {1, 3, 4}};
    const std::string_view owners[] = {"of", "fice"};
    CoreTextClusterMap output;
    const std::string baseline_text = "A";
    const CoreTextClusterMapToken baseline_token[] = {{0, 1, 0, 0, 1, false}};
    const CoreTextClusterMapGlyph baseline_glyph[] = {{1, 0, 1}};
    check(map_core_text_clusters(baseline_text, baseline_token, 1, baseline_glyph, 1,
        output) == CoreTextClusterMapStatus::Complete, "publish over-budget output baseline");
    const auto* prior_clusters = output.clusters.data();
    CoreTextClusterMapLimits limits;
    limits.max_work = 1;
    work = budget;
    check(map_experimental_contextual_inline_line(source, owners, 2, shaped, 0,
        output, limits, &work) == CoreTextInlineSourceMapStatus::WorkLimit &&
        output.clusters.data() == prior_clusters && output.clusters.size() == 1,
        "over-budget cluster mapping preserves prior result");
}

void ink_bounds_cover_pixels_painted_by_the_same_packet(const std::string& fixture_root) {
    Fixture fixture(fixture_root, "regular", true);
    auto work = budget;
    Binding binding;
    check(create_binding(fixture.as_inputs(), {}, work, binding) == Status::ok,
        "create ink geometry binding");

    ParagraphOptions options;
    options.size_q1000 = 24000;
    options.width_q1000 = 240000;
    Paragraph paragraph;
    work = budget;
    check(layout_paragraph(binding, "office", options, {}, work, paragraph) == Status::ok,
        "shape ink geometry packet");

    std::vector<ContextualTextGlyphInk> glyph_ink;
    work = budget;
    check(contextual_text_line_glyph_ink(paragraph, 0, 12000, 24000,
        glyph_ink, work) == ContextualTextInkStatus::Complete,
        "compute packet glyph ink bounds");
    check(glyph_ink.size() == paragraph.lines()[0].metrics.glyph_count,
        "one ink rectangle is retained for each packet glyph");

    FrameBuffer target(120, 48, {0, 0, 0, 0});
    work = budget;
    check(paint(binding, paragraph.lines()[0].packet, target, 12000, 24000,
        Rect{0, 0, target.width, target.height}, {255, 255, 255, 255}, {}, work) == Status::ok,
        "paint geometry packet into transparent framebuffer");
    std::size_t ink_pixels = 0;
    for (int y = 0; y < target.height; ++y) {
        for (int x = 0; x < target.width; ++x) {
            if (!target.pixel(x, y).a) continue;
            ++ink_pixels;
            const bool covered = std::any_of(glyph_ink.begin(), glyph_ink.end(),
                [x, y](const ContextualTextGlyphInk& glyph) {
                    const auto& bounds = glyph.bounds;
                    return x >= bounds.x && y >= bounds.y &&
                        x < bounds.x + bounds.width && y < bounds.y + bounds.height;
                });
            check(covered, "painted ink lies inside its glyph bounds");
        }
    }
    check(ink_pixels > 0, "the packet painted visible ink");

    const auto* prior = glyph_ink.data();
    const auto prior_size = glyph_ink.size();
    auto insufficient_work = std::uint64_t{1};
    check(contextual_text_line_glyph_ink(paragraph, 0, 12000, 24000,
        glyph_ink, insufficient_work) == ContextualTextInkStatus::ResourceLimit &&
        glyph_ink.data() == prior && glyph_ink.size() == prior_size && insufficient_work == 1,
        "over-budget ink geometry preserves output and caller work");
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("fixture directory argument required");
        const std::string root = argv[1];
        check_cross_owner_ligature(root);
        maps_collapsed_and_consumed_whitespace_as_omitted(root);
        maps_utf8_owner_ranges_on_scalar_boundaries(root);
        refuses_text_mismatch_transactionally(root);
        refuses_malformed_source_tables_and_work_exhaustion(root);
        ink_bounds_cover_pixels_painted_by_the_same_packet(root);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
