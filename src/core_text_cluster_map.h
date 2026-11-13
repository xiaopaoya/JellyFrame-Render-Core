#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace jellyframe::detail {

// Mapped tokens partition normalized UTF-8 bytes. Omitted tokens retain source
// order and ownership without contributing bytes to the shaped run.
struct CoreTextClusterMapToken {
    std::uint32_t text_begin = 0, text_end = 0;
    std::uint32_t owner = 0;
    std::uint32_t source_begin = 0, source_end = 0;
    bool omitted = false;
};

// Input glyph intervals are relative to the normalized run. Equal adjacent
// intervals form one cluster and their glyph order is retained.
struct CoreTextClusterMapGlyph {
    std::uint32_t glyph_id = 0;
    std::uint32_t cluster_begin = 0, cluster_end = 0;
};

struct CoreTextClusterOwnerSpan {
    std::uint32_t owner = 0;
    std::uint32_t first_token = 0, token_count = 0;
    std::uint32_t source_begin = 0, source_end = 0;
};

struct CoreTextClusterRecord {
    std::uint32_t text_begin = 0, text_end = 0;
    std::uint32_t first_glyph = 0, glyph_count = 0;
    std::uint32_t first_owner_span = 0, owner_span_count = 0;
};

struct CoreTextClusterMap {
    std::uint32_t text_length = 0;
    std::vector<CoreTextClusterMapGlyph> glyphs;
    std::vector<CoreTextClusterRecord> clusters;
    std::vector<CoreTextClusterOwnerSpan> owner_spans;
    CoreTextClusterMap() : glyphs(0), clusters(0), owner_spans(0) {}
};

struct CoreTextClusterMapLimits {
    std::size_t max_glyphs = 4096;
    std::size_t max_clusters = 4096;
    std::size_t max_owner_spans = 8192;
    std::size_t max_work = 262144;
    std::size_t max_capacity_bytes = 1048576;
};

struct CoreTextClusterMapUsage {
    std::size_t work = 0;
    std::size_t capacity_bytes = 0;
};

enum class CoreTextClusterMapStatus {
    Complete, InvalidInput, InvalidUtf8, UnsupportedCluster, GlyphLimit,
    ClusterLimit, OwnerSpanLimit, WorkLimit, CapacityLimit, CoordinateLimit,
    AllocationFailure
};

// Builds a value-only cluster and owner map. Tokens must cover the complete
// normalized run in order; omitted tokens have an empty text interval and a
// nonempty original source interval. Output publishes only on success; usage
// reports attempted work and capacity on both success and refusal.
CoreTextClusterMapStatus map_core_text_clusters(
    std::string_view normalized_text,
    const CoreTextClusterMapToken* tokens, std::size_t token_count,
    const CoreTextClusterMapGlyph* glyphs, std::size_t glyph_count,
    CoreTextClusterMap& output,
    CoreTextClusterMapUsage* usage = nullptr,
    CoreTextClusterMapLimits limits = {});

} // namespace jellyframe::detail
