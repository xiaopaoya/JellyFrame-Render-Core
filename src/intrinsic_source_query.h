#pragma once

#include "render_core/layout.h"

#include <atomic>
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace jellyframe::detail {

enum class IntrinsicSourceStatus : std::uint8_t {
    Complete,
    Unsupported,
    ResourceLimit,
    MeasurementFailure,
    InvalidInput,
    Cancelled,
};

// Bounds include source normalization and Core Text measurement work. Dynamic
// scratch counts simultaneously live old/new stream buffers; fixed traversal
// storage is separate (65 frames, 2080 bytes on x64). Values above the internal
// hard ceilings are clamped. Borrowed input/font storage is excluded.
struct IntrinsicSourceQueryLimits {
    std::size_t max_boxes = 256;
    std::size_t max_depth = 32;
    std::size_t max_source_bytes = 8192;
    std::size_t max_measurements = 512;
    std::size_t max_work = 65536;
    std::size_t max_scratch_bytes = 32768;
    // Borrowed for the synchronous query. Cancellation never publishes dimensions.
    const std::atomic_bool* cancellation = nullptr;
};

enum class IntrinsicPercentageBasisKind : std::uint8_t {
    CyclicSameAxis,
    DefiniteIndependent,
    IndefiniteOrUnknown,
};

struct IntrinsicPercentageBasis {
    IntrinsicPercentageBasisKind kind = IntrinsicPercentageBasisKind::IndefiniteOrUnknown;
    std::int64_t basis_units = -1;
};

struct IntrinsicSourceWidthResult {
    IntrinsicSourceStatus status = IntrinsicSourceStatus::Unsupported;
    std::int64_t min_content_border_box_units = -1;
    std::int64_t max_content_border_box_units = -1;
    std::size_t boxes_visited = 0;
    std::size_t source_bytes = 0;
    std::size_t measurements = 0;
    std::size_t work = 0;
    std::size_t peak_scratch_bytes = 0;
};

struct IntrinsicSourceQueryUsage {
    std::size_t boxes = 0;
    std::size_t source_bytes = 0;
    std::size_t measurements = 0;
    std::size_t work = 0;

    template<class Result>
    bool consume(const Result& result, const IntrinsicSourceQueryLimits& limits) noexcept {
        const auto fits = [](std::size_t used, std::size_t amount, std::size_t maximum) {
            return used <= maximum && amount <= maximum - used;
        };
        if (!fits(boxes, result.boxes_visited, limits.max_boxes) ||
            !fits(source_bytes, result.source_bytes, limits.max_source_bytes) ||
            !fits(measurements, result.measurements, limits.max_measurements) ||
            !fits(work, result.work, limits.max_work)) return false;
        boxes += result.boxes_visited;
        source_bytes += result.source_bytes;
        measurements += result.measurements;
        work += result.work;
        return true;
    }
};

// Initial admission is one ordinary horizontal block with an ASCII inline
// source. ASCII overflow-wrap:anywhere adds per-character min-content breaks;
// ASCII uppercase/lowercase transforms are applied before measurement. Non-ASCII
// simple CJK ideographs are also admitted with per-codepoint opportunities;
// mixed scripts, Han segment breaks, punctuation and combining sequences still refuse.
// Same-axis width/min/max percentages on neutral non-replaced Block descendants
// use explicit cyclic intrinsic-contribution semantics. Other basis modes are
// represented but refused until a consumer defines them.
IntrinsicSourceWidthResult query_intrinsic_source_width(
    const LayoutBox& box, const TextSystem& text_system,
    IntrinsicPercentageBasis percentage_basis,
    IntrinsicSourceQueryLimits limits = {}) noexcept;

struct IntrinsicSourceHeightResult;

// One ledger lives on the public layout-call stack. Layout recursion, retries
// and speculative transactions share it and cannot refund consumed work.
class IntrinsicSourceQueryLedger {
public:
    explicit IntrinsicSourceQueryLedger(LayoutIntrinsicQueryLimits limits = {}) noexcept : limits_(limits) {}

    bool cancelled() const noexcept {
        return limits_.cancellation && limits_.cancellation->load(std::memory_order_relaxed);
    }
    IntrinsicSourceQueryLimits remaining_limits(std::size_t live_scratch = 0) const noexcept {
        IntrinsicSourceQueryLimits result;
        const auto remaining = [](std::size_t maximum, std::size_t used) {
            return maximum - std::min(maximum, used);
        };
        result.max_boxes = std::min(result.max_boxes, remaining(limits_.max_boxes, usage_.boxes));
        result.max_depth = std::min(result.max_depth, limits_.max_depth);
        result.max_source_bytes = std::min(result.max_source_bytes,
            remaining(limits_.max_source_bytes, usage_.source_bytes));
        result.max_measurements = std::min(result.max_measurements,
            remaining(limits_.max_measurements, usage_.measurements));
        result.max_work = std::min(result.max_work, remaining(limits_.max_work, usage_.work));
        result.max_scratch_bytes = std::min(result.max_scratch_bytes,
            remaining(remaining(limits_.max_scratch_bytes, scratch_live_), live_scratch));
        result.cancellation = limits_.cancellation;
        return result;
    }
    template<class Result>
    bool consume(const Result& result) noexcept {
        IntrinsicSourceQueryLimits aggregate;
        aggregate.max_boxes = limits_.max_boxes;
        aggregate.max_source_bytes = limits_.max_source_bytes;
        aggregate.max_measurements = limits_.max_measurements;
        aggregate.max_work = limits_.max_work;
        ++queries_;
        peak_scratch_ = std::max(peak_scratch_, scratch_live_ + result.peak_scratch_bytes);
        if (result.status == IntrinsicSourceStatus::ResourceLimit) ++resource_refusals_;
        return usage_.consume(result, aggregate);
    }
    bool charge(std::size_t amount) noexcept {
        if (cancelled()) return false;
        if (usage_.work > limits_.max_work || amount > limits_.max_work - usage_.work) {
            ++resource_refusals_;
            return false;
        }
        usage_.work += amount;
        return true;
    }
    bool reserve_scratch(std::size_t bytes) noexcept {
        if (cancelled()) return false;
        if (scratch_live_ > limits_.max_scratch_bytes || bytes > limits_.max_scratch_bytes - scratch_live_) {
            ++resource_refusals_;
            return false;
        }
        scratch_live_ += bytes;
        peak_scratch_ = std::max(peak_scratch_, scratch_live_);
        return true;
    }
    void release_scratch(std::size_t bytes) noexcept {
        scratch_live_ -= std::min(scratch_live_, bytes);
    }
    LayoutIntrinsicQueryStatistics snapshot() const noexcept {
        return {usage_.boxes, usage_.source_bytes, usage_.measurements, usage_.work,
            queries_, resource_refusals_, peak_scratch_};
    }
    bool can_query(const IntrinsicSourceQueryLimits& limits) const noexcept {
        return limits.max_boxes && limits.max_depth && limits.max_source_bytes &&
            limits.max_measurements && limits.max_work && limits.max_scratch_bytes;
    }
    IntrinsicSourceWidthResult query_width(const LayoutBox& box, const TextSystem& text_system,
        IntrinsicPercentageBasis basis) noexcept;
    IntrinsicSourceHeightResult query_height(const LayoutBox& box, const TextSystem& text_system,
        std::int64_t inline_size) noexcept;

private:
    LayoutIntrinsicQueryLimits limits_;
    IntrinsicSourceQueryUsage usage_;
    std::size_t scratch_live_ = 0, peak_scratch_ = 0, queries_ = 0, resource_refusals_ = 0;
};

class IntrinsicSourceQueryScratch {
public:
    IntrinsicSourceQueryScratch(IntrinsicSourceQueryLedger& ledger, std::size_t bytes) noexcept
        : ledger_(ledger), bytes_(bytes), reserved_(ledger.reserve_scratch(bytes)) {}
    ~IntrinsicSourceQueryScratch() { if (reserved_) ledger_.release_scratch(bytes_); }
    IntrinsicSourceQueryScratch(const IntrinsicSourceQueryScratch&) = delete;
    IntrinsicSourceQueryScratch& operator=(const IntrinsicSourceQueryScratch&) = delete;
    explicit operator bool() const noexcept { return reserved_; }
private:
    IntrinsicSourceQueryLedger& ledger_;
    std::size_t bytes_;
    bool reserved_;
};

} // namespace jellyframe::detail
