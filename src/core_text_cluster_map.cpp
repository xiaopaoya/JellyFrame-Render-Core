#include "core_text_cluster_map.h"

#include "utf8_scalar.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace jellyframe::detail {
namespace {
using Status = CoreTextClusterMapStatus;

struct Builder {
    std::string_view text;
    const CoreTextClusterMapToken* tokens;
    std::size_t token_count;
    const CoreTextClusterMapGlyph* input;
    std::size_t glyph_count;
    CoreTextClusterMapLimits limits;
    CoreTextClusterMapUsage usage{};
    CoreTextClusterMap draft;
    std::size_t capacity = 0;
    std::size_t token_cursor = 0;
    std::size_t glyph_cursor = 0;

    Builder(std::string_view input_text, const CoreTextClusterMapToken* input_tokens,
        std::size_t input_token_count, const CoreTextClusterMapGlyph* input_glyphs,
        std::size_t input_glyph_count, CoreTextClusterMapLimits input_limits)
        : text(input_text), tokens(input_tokens), token_count(input_token_count),
          input(input_glyphs), glyph_count(input_glyph_count), limits(input_limits), draft() {}

    void work(std::size_t amount = 1) {
        if (amount > limits.max_work - std::min(usage.work, limits.max_work))
            throw Status::WorkLimit;
        usage.work += amount;
    }

    template<class T>
    void reserve(std::vector<T>& values, std::size_t count, std::size_t maximum, Status limit_status) {
        if (count > maximum) throw limit_status;
        if (count <= values.capacity()) return;
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(T))
            throw Status::CapacityLimit;
        const auto requested = count * sizeof(T);
        if (requested > limits.max_capacity_bytes - std::min(capacity, limits.max_capacity_bytes))
            throw Status::CapacityLimit;
        usage.capacity_bytes = std::max(usage.capacity_bytes, capacity + requested);
        values.reserve(count);
        if (values.capacity() > std::numeric_limits<std::size_t>::max() / sizeof(T))
            throw Status::CapacityLimit;
        const auto acquired = values.capacity() * sizeof(T);
        if (acquired > limits.max_capacity_bytes - std::min(capacity, limits.max_capacity_bytes)) {
            usage.capacity_bytes = std::max(usage.capacity_bytes, capacity + acquired);
            throw Status::CapacityLimit;
        }
        capacity += acquired;
        usage.capacity_bytes = std::max(usage.capacity_bytes, capacity);
    }

    void validate_tokens() {
        if (token_count && !tokens) throw Status::InvalidInput;
        std::size_t text_cursor = 0;
        for (std::size_t i = 0; i < token_count; ++i) {
            work();
            const auto& token = tokens[i];
            if (token.text_begin != text_cursor || token.text_end < token.text_begin ||
                token.text_end > text.size() || token.source_end <= token.source_begin)
                throw Status::InvalidInput;
            if (token.omitted) {
                if (token.text_begin != token.text_end) throw Status::InvalidInput;
            } else {
                if (token.text_begin == token.text_end) throw Status::InvalidInput;
                text_cursor = token.text_end;
            }
        }
        if (text_cursor != text.size()) throw Status::InvalidInput;
    }

    void owner_spans(const CoreTextClusterRecord& cluster) {
        const auto cluster_begin = static_cast<std::size_t>(cluster.text_begin);
        const auto cluster_end = static_cast<std::size_t>(cluster.text_end);
        const auto span_begin = draft.owner_spans.size();
        std::size_t covered = cluster_begin;
        while (token_cursor < token_count) {
            work();
            const auto token_index = token_cursor;
            const auto& token = tokens[token_index];
            if (token.omitted) {
                if (token.text_begin <= cluster_begin) {
                    ++token_cursor;
                    continue;
                }
                if (token.text_begin < cluster_end) {
                    // An omitted source event inside a normalized cluster is
                    // retained in source order but contributes no owner span.
                    ++token_cursor;
                    continue;
                }
                break;
            }
            if (token.text_end <= cluster_begin) {
                ++token_cursor;
                continue;
            }
            if (token.text_begin >= cluster_end) break;
            if (token.text_begin != covered || token.text_end > cluster_end)
                throw Status::UnsupportedCluster;
            if (token_index > std::numeric_limits<std::uint32_t>::max())
                throw Status::CoordinateLimit;

            if (draft.owner_spans.size() > span_begin) {
                auto& previous = draft.owner_spans.back();
                if (previous.owner == token.owner &&
                    previous.source_end == token.source_begin &&
                    static_cast<std::size_t>(previous.first_token) + previous.token_count == token_index) {
                    if (previous.token_count == std::numeric_limits<std::uint32_t>::max())
                        throw Status::CoordinateLimit;
                    previous.source_end = token.source_end;
                    ++previous.token_count;
                    covered = token.text_end;
                    ++token_cursor;
                    continue;
                }
            }
            reserve(draft.owner_spans, draft.owner_spans.size() + 1,
                limits.max_owner_spans, Status::OwnerSpanLimit);
            draft.owner_spans.push_back({token.owner, static_cast<std::uint32_t>(token_index), 1,
                token.source_begin, token.source_end});
            covered = token.text_end;
            ++token_cursor;
        }
        if (covered != cluster_end) throw Status::UnsupportedCluster;
        const auto count = draft.owner_spans.size() - span_begin;
        if (count > std::numeric_limits<std::uint32_t>::max()) throw Status::CoordinateLimit;
        // The explicit omitted records are source-proof entries, not cluster
        // owners. A cluster still needs at least one glyph-bearing owner.
        if (!count) throw Status::UnsupportedCluster;
    }

    void run() {
        if (text.size() > std::numeric_limits<std::uint32_t>::max() ||
            token_count > std::numeric_limits<std::uint32_t>::max() ||
            glyph_count > std::numeric_limits<std::uint32_t>::max())
            throw Status::CoordinateLimit;
        if (glyph_count && !input) throw Status::InvalidInput;
        if (glyph_count > limits.max_glyphs) throw Status::GlyphLimit;
        validate_tokens();
        std::size_t position = 0;
        while (position < text.size()) {
            work();
            std::uint32_t scalar = 0;
            if (!read_utf8_scalar(text, position, scalar)) throw Status::InvalidUtf8;
        }
        for (std::size_t i = 0; i < token_count; ++i) {
            work();
            if (!is_scalar_boundary(text, tokens[i].text_begin) ||
                !is_scalar_boundary(text, tokens[i].text_end)) throw Status::InvalidInput;
        }

        draft.text_length = static_cast<std::uint32_t>(text.size());
        if (text.empty()) {
            if (glyph_count) throw Status::UnsupportedCluster;
            return;
        }
        if (!glyph_count) throw Status::UnsupportedCluster;
        if (!limits.max_clusters) throw Status::ClusterLimit;
        if (!limits.max_owner_spans) throw Status::OwnerSpanLimit;
        reserve(draft.glyphs, glyph_count, limits.max_glyphs, Status::GlyphLimit);

        std::size_t covered = 0;
        while (glyph_cursor < glyph_count) {
            work();
            const auto first_glyph = glyph_cursor;
            const auto begin = input[glyph_cursor].cluster_begin;
            const auto end = input[glyph_cursor].cluster_end;
            if (begin >= end || end > text.size() || begin != covered)
                throw Status::UnsupportedCluster;
            if (!is_scalar_boundary(text, begin) || !is_scalar_boundary(text, end))
                throw Status::UnsupportedCluster;

            while (glyph_cursor < glyph_count && input[glyph_cursor].cluster_begin == begin &&
                   input[glyph_cursor].cluster_end == end) {
                work();
                draft.glyphs.push_back(input[glyph_cursor]);
                ++glyph_cursor;
            }
            if (glyph_cursor < glyph_count && input[glyph_cursor].cluster_begin < end)
                throw Status::UnsupportedCluster;

            CoreTextClusterRecord cluster{};
            cluster.text_begin = begin;
            cluster.text_end = end;
            cluster.first_glyph = static_cast<std::uint32_t>(first_glyph);
            cluster.glyph_count = static_cast<std::uint32_t>(glyph_cursor - first_glyph);
            cluster.first_owner_span = static_cast<std::uint32_t>(draft.owner_spans.size());
            owner_spans(cluster);
            cluster.owner_span_count = static_cast<std::uint32_t>(draft.owner_spans.size()) -
                cluster.first_owner_span;
            reserve(draft.clusters, draft.clusters.size() + 1,
                limits.max_clusters, Status::ClusterLimit);
            draft.clusters.push_back(cluster);
            covered = end;
        }
        if (covered != text.size()) throw Status::UnsupportedCluster;
    }

    bool is_scalar_boundary(std::string_view value, std::size_t offset) {
        if (offset == 0 || offset == value.size()) return true;
        return (static_cast<unsigned char>(value[offset]) & 0xc0U) != 0x80U;
    }
};
} // namespace

CoreTextClusterMapStatus map_core_text_clusters(std::string_view normalized_text,
    const CoreTextClusterMapToken* tokens, std::size_t token_count,
    const CoreTextClusterMapGlyph* glyphs, std::size_t glyph_count,
    CoreTextClusterMap& output, CoreTextClusterMapUsage* usage,
    CoreTextClusterMapLimits limits) {
    Builder builder(normalized_text, tokens, token_count, glyphs, glyph_count, limits);
    Status status = Status::Complete;
    try {
        builder.run();
        output = std::move(builder.draft);
    } catch (Status failure) {
        status = failure;
    } catch (const std::bad_alloc&) {
        status = Status::AllocationFailure;
    } catch (const std::length_error&) {
        status = Status::AllocationFailure;
    }
    if (usage) *usage = builder.usage;
    return status;
}

} // namespace jellyframe::detail
