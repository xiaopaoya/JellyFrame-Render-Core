#pragma once

#include "utf8_scalar.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <string_view>
#include <utility>
#include <vector>

namespace jellyframe::detail {

struct CoreTextShapedWrapCluster {
    std::size_t begin = 0, end = 0;
};

struct CoreTextShapedWrapBoundary {
    std::size_t offset = 0;
    bool normal = false;
    bool emergency = false;
};

struct CoreTextShapedWrapLine {
    std::size_t begin = 0, end = 0;
    std::int32_t advance = 0;
    bool soft = false;
};

struct CoreTextShapedWrapResult {
    std::vector<CoreTextShapedWrapLine> lines;
    CoreTextShapedWrapResult() : lines(0) {}
};

struct CoreTextShapedWrapLimits {
    std::size_t max_source_bytes = 8192;
    std::size_t max_clusters = 4096;
    std::size_t max_boundaries = 4096;
    std::size_t max_lines = 128;
    std::size_t max_shape_calls = 2048;
    std::size_t max_work = 262144;
};

struct CoreTextShapedWrapUsage {
    std::size_t work = 0, shape_calls = 0;
};

enum class CoreTextShapedWrapStatus {
    Complete, InvalidInput, InvalidUtf8, UnsupportedBoundary, ShapeFailure,
    Overflow, WorkLimit, ShapeCallLimit, LineLimit, AllocationFailure
};

// Boundaries encode the caller's line-break policy, but must coincide with
// packet cluster ends. The callback shapes the exact candidate source slice.
// Output is replaced only after a complete plan has been built.
template<class Measure>
CoreTextShapedWrapStatus plan_core_text_shaped_wrap(
    std::string_view source,
    const CoreTextShapedWrapCluster* clusters, std::size_t cluster_count,
    const CoreTextShapedWrapBoundary* boundaries, std::size_t boundary_count,
    std::int32_t width, Measure&& measure,
    CoreTextShapedWrapResult& output,
    CoreTextShapedWrapUsage* reported_usage = nullptr,
    CoreTextShapedWrapLimits limits = {}) noexcept {
    using Status = CoreTextShapedWrapStatus;
    CoreTextShapedWrapUsage usage{};
    struct Report {
        CoreTextShapedWrapUsage* output;
        CoreTextShapedWrapUsage& value;
        ~Report() { if (output) *output = value; }
    } report{reported_usage, usage};
    try {
        if (width < 0 || source.size() > limits.max_source_bytes ||
            cluster_count > limits.max_clusters || boundary_count > limits.max_boundaries ||
            (source.size() && (!clusters || !cluster_count)) ||
            (boundary_count && !boundaries)) return Status::InvalidInput;
        std::size_t cursor = 0;
        for (std::size_t i = 0; i < source.size();) {
            if (usage.work++ >= limits.max_work) return Status::WorkLimit;
            std::uint32_t scalar = 0;
            if (!read_utf8_scalar(source, i, scalar)) return Status::InvalidUtf8;
        }
        for (std::size_t i = 0; i < cluster_count; ++i) {
            if (usage.work++ >= limits.max_work) return Status::WorkLimit;
            if (clusters[i].begin != cursor || clusters[i].end <= cursor ||
                clusters[i].end > source.size()) return Status::InvalidInput;
            if (clusters[i].end < source.size() &&
                (static_cast<unsigned char>(source[clusters[i].end]) & 0xc0U) == 0x80U)
                return Status::UnsupportedBoundary;
            cursor = clusters[i].end;
        }
        if (cursor != source.size()) return Status::InvalidInput;
        for (std::size_t i = 0; i < boundary_count; ++i) {
            if (usage.work++ >= limits.max_work) return Status::WorkLimit;
            if ((!boundaries[i].normal && !boundaries[i].emergency) ||
                boundaries[i].offset <= 0 || boundaries[i].offset >= source.size() ||
                (i && boundaries[i - 1].offset >= boundaries[i].offset))
                return Status::InvalidInput;
            bool cluster_end = false;
            for (std::size_t j = 0; j < cluster_count; ++j) {
                if (usage.work++ >= limits.max_work) return Status::WorkLimit;
                if (clusters[j].end == boundaries[i].offset) { cluster_end = true; break; }
                if (clusters[j].end > boundaries[i].offset) break;
            }
            if (!cluster_end) return Status::UnsupportedBoundary;
        }

        CoreTextShapedWrapResult draft;
        if (source.empty()) { output = std::move(draft); return Status::Complete; }
        draft.lines.reserve(std::min(limits.max_lines, cluster_count));
        std::size_t line_begin = 0, first_cluster = 0;
        while (line_begin < source.size()) {
            if (draft.lines.size() >= limits.max_lines) return Status::LineLimit;
            while (first_cluster < cluster_count && clusters[first_cluster].end <= line_begin) {
                if (usage.work++ >= limits.max_work) return Status::WorkLimit;
                ++first_cluster;
            }
            std::size_t chosen_end = source.size();
            std::int32_t chosen_advance = 0;
            bool found_fit = false;
            std::size_t first_normal = 0;
            std::size_t first_emergency = 0;
            auto test_candidate = [&](std::size_t end, bool force = false) -> Status {
                if (usage.shape_calls >= limits.max_shape_calls) return Status::ShapeCallLimit;
                if (usage.work++ >= limits.max_work) return Status::WorkLimit;
                ++usage.shape_calls;
                std::int32_t advance = 0;
                if (!measure(source.substr(line_begin, end - line_begin), advance))
                    return Status::ShapeFailure;
                if (advance < 0) return Status::Overflow;
                if (advance <= width || force) {
                    chosen_end = end;
                    chosen_advance = advance;
                    found_fit = advance <= width;
                }
                return Status::Complete;
            };
            std::size_t boundary_at = 0;
            while (boundary_at < boundary_count && boundaries[boundary_at].offset <= line_begin) {
                if (usage.work++ >= limits.max_work) return Status::WorkLimit;
                ++boundary_at;
            }
            for (auto i = first_cluster; i < cluster_count; ++i) {
                if (usage.work++ >= limits.max_work) return Status::WorkLimit;
                const auto end = clusters[i].end;
                const bool final = end == source.size();
                bool allowed = final;
                bool emergency = false;
                while (boundary_at < boundary_count && boundaries[boundary_at].offset < end) {
                    if (usage.work++ >= limits.max_work) return Status::WorkLimit;
                    ++boundary_at;
                }
                if (boundary_at < boundary_count && boundaries[boundary_at].offset == end) {
                    allowed = allowed || boundaries[boundary_at].normal || boundaries[boundary_at].emergency;
                    emergency = boundaries[boundary_at].emergency;
                    if (boundaries[boundary_at].normal && !first_normal)
                        first_normal = end;
                }
                if (!allowed) continue;
                if (emergency && !first_emergency) first_emergency = end;
                const auto status = test_candidate(end);
                if (status != Status::Complete) return status;
            }
            if (!found_fit) {
                chosen_end = first_emergency ? first_emergency :
                    first_normal ? first_normal : source.size();
                if (!chosen_end) {
                    if (first_cluster >= cluster_count) return Status::InvalidInput;
                    chosen_end = clusters[first_cluster].end;
                }
                const auto status = test_candidate(chosen_end, true);
                if (status != Status::Complete) return status;
            }
            if (chosen_end <= line_begin) return Status::InvalidInput;
            draft.lines.push_back({line_begin, chosen_end, chosen_advance,
                chosen_end < source.size()});
            line_begin = chosen_end;
        }
        output = std::move(draft);
        return Status::Complete;
    } catch (const std::bad_alloc&) {
        return Status::AllocationFailure;
    } catch (...) {
        return Status::ShapeFailure;
    }
}

} // namespace jellyframe::detail
