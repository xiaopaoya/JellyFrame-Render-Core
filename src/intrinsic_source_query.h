#pragma once

#include "render_core/layout.h"

#include <atomic>
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

} // namespace jellyframe::detail
