#include "../../src/core_text_cluster_map.h"
#include "../../src/core_text_shaped_wrap.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace jellyframe::detail;

void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void maps_ordered_clusters_and_cross_owner_spans() {
    const std::string text = "office";
    const CoreTextClusterMapToken tokens[] = {
        {0, 1, 10, 0, 1, false},
        {1, 2, 10, 1, 2, false},
        {2, 3, 11, 5, 6, false},
        {3, 4, 12, 0, 1, false},
        {4, 5, 12, 1, 2, false},
        {5, 6, 12, 2, 3, false},
    };
    const CoreTextClusterMapGlyph glyphs[] = {
        {1, 0, 1}, {2, 1, 2}, {30, 2, 4}, {4, 4, 5}, {5, 5, 6},
    };
    CoreTextClusterMap result;
    check(map_core_text_clusters(text, tokens, 6, glyphs, 5, result) ==
        CoreTextClusterMapStatus::Complete, "ordered clusters map");
    check(result.clusters.size() == 5 && result.glyphs.size() == 5,
        "glyph and cluster counts retained");
    const auto& ligature = result.clusters[2];
    check(ligature.text_begin == 2 && ligature.text_end == 4 &&
        ligature.first_glyph == 2 && ligature.glyph_count == 1 &&
        ligature.owner_span_count == 2 &&
        result.owner_spans[ligature.first_owner_span].owner == 11 &&
        result.owner_spans[ligature.first_owner_span + 1].owner == 12,
        "one ligature cluster preserves its two source owners");
}

void maps_multibyte_cluster_across_inline_owners() {
    const std::string text = "\xc3\xa9\xe4\xb8\xad"; // é中
    const CoreTextClusterMapToken tokens[] = {
        {0, 2, 21, 3, 5, false},
        {2, 5, 22, 9, 12, false},
    };
    const CoreTextClusterMapGlyph glyphs[] = {{41, 0, 5}};
    CoreTextClusterMap result;
    check(map_core_text_clusters(text, tokens, 2, glyphs, 1, result) ==
        CoreTextClusterMapStatus::Complete, "multibyte cross-owner cluster maps");
    check(result.clusters.size() == 1 && result.clusters[0].text_begin == 0 &&
        result.clusters[0].text_end == 5 && result.clusters[0].owner_span_count == 2,
        "shaping cluster retains complete UTF-8 byte interval and both owners");
    const auto& first = result.owner_spans[result.clusters[0].first_owner_span];
    const auto& second = result.owner_spans[result.clusters[0].first_owner_span + 1];
    check(first.owner == 21 && first.source_begin == 3 && first.source_end == 5 &&
        second.owner == 22 && second.source_begin == 9 && second.source_end == 12,
        "owner source ranges retain source order across independent byte offsets");
}

void coalesces_only_contiguous_same_owner_sources() {
    const std::string text = "abcd";
    const CoreTextClusterMapToken tokens[] = {
        {0, 1, 7, 2, 3, false},
        {1, 2, 7, 3, 4, false},
        {2, 3, 7, 8, 9, false},
        {3, 4, 8, 0, 1, false},
    };
    const CoreTextClusterMapGlyph glyphs[] = {{9, 0, 4}};
    CoreTextClusterMap result;
    check(map_core_text_clusters(text, tokens, 4, glyphs, 1, result) ==
        CoreTextClusterMapStatus::Complete, "cross-owner cluster maps");
    check(result.owner_spans.size() == 3, "same-owner source gap remains a boundary");
    check(result.owner_spans[0].owner == 7 && result.owner_spans[0].first_token == 0 &&
        result.owner_spans[0].token_count == 2 && result.owner_spans[0].source_begin == 2 &&
        result.owner_spans[0].source_end == 4, "contiguous same-owner tokens coalesce");
    check(result.owner_spans[1].owner == 7 && result.owner_spans[1].first_token == 2 &&
        result.owner_spans[1].source_begin == 8 && result.owner_spans[1].source_end == 9,
        "same owner after source gap remains a separate span");
    check(result.owner_spans[2].owner == 8, "owner order follows source order");
}

void omitted_source_stays_out_of_glyph_owner_spans() {
    const std::string text = "ab";
    const CoreTextClusterMapToken tokens[] = {
        {0, 1, 3, 0, 1, false},
        {1, 1, 99, 4, 7, true},
        {1, 2, 3, 7, 8, false},
    };
    const CoreTextClusterMapGlyph glyphs[] = {{20, 0, 2}};
    CoreTextClusterMap result;
    check(map_core_text_clusters(text, tokens, 3, glyphs, 1, result) ==
        CoreTextClusterMapStatus::Complete, "omitted source event is accepted");
    check(result.owner_spans.size() == 2 && result.owner_spans[0].owner == 3 &&
        result.owner_spans[1].owner == 3 && result.owner_spans[0].token_count == 1 &&
        result.owner_spans[1].token_count == 1,
        "omitted whitespace contributes no glyph owner span and breaks coalescing");
}

void rejects_invalid_utf8_and_non_scalar_boundaries_transactionally() {
    CoreTextClusterMap result;
    const std::string good = "A";
    const CoreTextClusterMapToken good_tokens[] = {{0, 1, 4, 0, 1, false}};
    const CoreTextClusterMapGlyph good_glyphs[] = {{1, 0, 1}};
    check(map_core_text_clusters(good, good_tokens, 1, good_glyphs, 1, result) ==
        CoreTextClusterMapStatus::Complete, "transaction baseline publishes");
    const auto* old_clusters = result.clusters.data();
    const auto* old_spans = result.owner_spans.data();

    const std::string malformed("\xc0\xaf", 2);
    const CoreTextClusterMapToken malformed_tokens[] = {{0, 2, 4, 0, 2, false}};
    const CoreTextClusterMapGlyph malformed_glyphs[] = {{2, 0, 2}};
    check(map_core_text_clusters(malformed, malformed_tokens, 1, malformed_glyphs, 1, result) ==
        CoreTextClusterMapStatus::InvalidUtf8 && result.clusters.data() == old_clusters &&
        result.owner_spans.data() == old_spans, "malformed UTF-8 leaves prior map untouched");

    const std::string multibyte("\xc3\xa9", 2);
    const CoreTextClusterMapToken unicode_tokens[] = {{0, 2, 4, 0, 2, false}};
    const CoreTextClusterMapGlyph split_glyphs[] = {{3, 0, 1}, {4, 1, 2}};
    check(map_core_text_clusters(multibyte, unicode_tokens, 1, split_glyphs, 2, result) ==
        CoreTextClusterMapStatus::UnsupportedCluster && result.clusters.data() == old_clusters,
        "cluster boundary cannot split a UTF-8 scalar");
    const CoreTextClusterMapToken split_token[] = {
        {0, 1, 4, 0, 1, false}, {1, 2, 4, 1, 2, false},
    };
    const CoreTextClusterMapGlyph whole_glyph[] = {{5, 0, 2}};
    check(map_core_text_clusters(multibyte, split_token, 2, whole_glyph, 1, result) ==
        CoreTextClusterMapStatus::InvalidInput && result.clusters.data() == old_clusters,
        "token boundary cannot split a UTF-8 scalar");
}

void rejects_gaps_overlaps_and_bad_token_partitions() {
    const std::string text = "abc";
    const CoreTextClusterMapToken tokens[] = {{0, 3, 1, 0, 3, false}};
    CoreTextClusterMap result;
    const CoreTextClusterMapGlyph gap[] = {{1, 0, 1}, {2, 2, 3}};
    check(map_core_text_clusters(text, tokens, 1, gap, 2, result) ==
        CoreTextClusterMapStatus::UnsupportedCluster, "cluster coverage gap refused");
    const CoreTextClusterMapGlyph overlap[] = {{1, 0, 2}, {2, 1, 3}};
    check(map_core_text_clusters(text, tokens, 1, overlap, 2, result) ==
        CoreTextClusterMapStatus::UnsupportedCluster, "overlapping cluster intervals refused");
    const CoreTextClusterMapToken bad_tokens[] = {
        {0, 1, 1, 0, 1, false}, {2, 3, 1, 1, 2, false},
    };
    const CoreTextClusterMapGlyph whole[] = {{3, 0, 3}};
    check(map_core_text_clusters(text, bad_tokens, 2, whole, 1, result) ==
        CoreTextClusterMapStatus::InvalidInput, "token partition gap refused");
}

void omitted_only_run_and_limits_are_bounded() {
    const CoreTextClusterMapToken omitted[] = {{0, 0, 2, 4, 5, true}};
    CoreTextClusterMap result;
    check(map_core_text_clusters({}, omitted, 1, nullptr, 0, result) ==
        CoreTextClusterMapStatus::Complete && result.clusters.empty() && result.owner_spans.empty(),
        "omitted-only source proof does not create a glyph cluster");

    const std::string text = "ab";
    const CoreTextClusterMapToken tokens[] = {
        {0, 1, 1, 0, 1, false}, {1, 2, 1, 1, 2, false},
    };
    const CoreTextClusterMapGlyph glyphs[] = {{4, 0, 1}, {5, 1, 2}};
    check(map_core_text_clusters(text, tokens, 2, glyphs, 2, result) ==
        CoreTextClusterMapStatus::Complete, "limit baseline maps");
    const auto* prior = result.clusters.data();
    CoreTextClusterMapLimits limits;
    limits.max_glyphs = 1;
    check(map_core_text_clusters(text, tokens, 2, glyphs, 2, result, nullptr, limits) ==
        CoreTextClusterMapStatus::GlyphLimit && result.clusters.data() == prior,
        "glyph count limit is transactional");
    limits = {};
    limits.max_clusters = 1;
    check(map_core_text_clusters(text, tokens, 2, glyphs, 2, result, nullptr, limits) ==
        CoreTextClusterMapStatus::ClusterLimit && result.clusters.data() == prior,
        "cluster count limit is transactional");
    limits = {};
    limits.max_owner_spans = 0;
    check(map_core_text_clusters(text, tokens, 2, glyphs, 2, result, nullptr, limits) ==
        CoreTextClusterMapStatus::OwnerSpanLimit && result.clusters.data() == prior,
        "owner span limit is transactional");
    limits = {};
    limits.max_work = 1;
    check(map_core_text_clusters(text, tokens, 2, glyphs, 2, result, nullptr, limits) ==
        CoreTextClusterMapStatus::WorkLimit && result.clusters.data() == prior,
        "work limit is transactional");
    limits = {};
    limits.max_capacity_bytes = 1;
    check(map_core_text_clusters(text, tokens, 2, glyphs, 2, result, nullptr, limits) ==
        CoreTextClusterMapStatus::CapacityLimit && result.clusters.data() == prior,
        "capacity limit is transactional");
}

void shaped_wrap_candidates_stay_on_cluster_edges_and_measure_final_slices() {
    using namespace jellyframe::detail;
    const std::string text = "a\xc3\xa9" "bc";
    const CoreTextShapedWrapCluster clusters[] = {
        {0, 1}, {1, 3}, {3, 4}, {4, 5},
    };
    const CoreTextShapedWrapBoundary boundaries[] = {
        {3, true, false}, {4, false, true},
    };
    CoreTextShapedWrapResult result;
    std::vector<std::string> measured;
    auto measure = [&](std::string_view run, std::int32_t& advance) {
        measured.emplace_back(run);
        advance = static_cast<std::int32_t>(run.size() * 1000);
        return true;
    };
    check(plan_core_text_shaped_wrap(text, clusters, 4, boundaries, 2,
        3000, measure, result) == CoreTextShapedWrapStatus::Complete,
        "shaped wrap plan completes");
    check(result.lines.size() == 2 && result.lines[0].begin == 0 &&
        result.lines[0].end == 3 && result.lines[0].advance == 3000 &&
        result.lines[0].soft && result.lines[1].begin == 3 &&
        result.lines[1].end == 5 && result.lines[1].advance == 2000 &&
        !result.lines[1].soft, "widest fitting source-cluster candidate selected");
    check(!measured.empty() && measured.front() == text.substr(0, 3),
        "candidate callback receives the exact UTF-8 line source");

    const CoreTextShapedWrapBoundary inside_cluster[] = {{2, true, false}};
    const auto* prior = result.lines.data();
    check(plan_core_text_shaped_wrap(text, clusters, 4, inside_cluster, 1,
        3000, measure, result) == CoreTextShapedWrapStatus::UnsupportedBoundary &&
        result.lines.data() == prior, "cluster interior is rejected transactionally");
}

void shaped_wrap_enforces_progress_and_resource_bounds() {
    using namespace jellyframe::detail;
    const std::string text = "ab";
    const CoreTextShapedWrapCluster clusters[] = {{0, 1}, {1, 2}};
    CoreTextShapedWrapResult result;
    auto measure = [](std::string_view run, std::int32_t& advance) {
        advance = static_cast<std::int32_t>(run.size() * 2000);
        return true;
    };
    const CoreTextShapedWrapBoundary emergency[] = {{1, false, true}};
    check(plan_core_text_shaped_wrap(text, clusters, 2, emergency, 1,
        1000, measure, result) == CoreTextShapedWrapStatus::Complete &&
        result.lines.size() == 2 && result.lines[0].end == 1 &&
        result.lines[0].advance == 2000, "over-wide first cluster guarantees progress");

    check(plan_core_text_shaped_wrap(text, clusters, 2, nullptr, 0,
        1000, measure, result) == CoreTextShapedWrapStatus::Complete &&
        result.lines.size() == 1 && result.lines[0].end == 2 &&
        result.lines[0].advance == 4000,
        "ordinary break fallback does not invent an emergency split");

    CoreTextShapedWrapLimits limits;
    limits.max_shape_calls = 1;
    const auto* prior = result.lines.data();
    check(plan_core_text_shaped_wrap(text, clusters, 2, nullptr, 0,
        1000, measure, result, nullptr, limits) ==
        CoreTextShapedWrapStatus::ShapeCallLimit && result.lines.data() == prior,
        "shape call cap preserves published output");
}
} // namespace

int main() {
    maps_ordered_clusters_and_cross_owner_spans();
    maps_multibyte_cluster_across_inline_owners();
    coalesces_only_contiguous_same_owner_sources();
    omitted_source_stays_out_of_glyph_owner_spans();
    rejects_invalid_utf8_and_non_scalar_boundaries_transactionally();
    rejects_gaps_overlaps_and_bad_token_partitions();
    omitted_only_run_and_limits_are_bounded();
    shaped_wrap_candidates_stay_on_cluster_edges_and_measure_final_slices();
    shaped_wrap_enforces_progress_and_resource_bounds();
}
