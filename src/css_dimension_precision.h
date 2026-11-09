#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace jellyframe::detail {
// Shared computed CSS dimension lattice; no parser or layout family dependency.
inline constexpr std::int64_t kDimensionPrecisionScale = 1000;
inline constexpr std::int64_t kDimensionPrecisionMaximum =
    static_cast<std::int64_t>(std::numeric_limits<int>::max()) * kDimensionPrecisionScale;
inline bool encode_dimension_units(double pixels, std::int64_t& out) {
    if (!std::isfinite(pixels) || pixels < 0 || pixels > std::numeric_limits<int>::max()) return false;
    const auto whole = static_cast<std::int64_t>(pixels);
    if (pixels == static_cast<double>(whole)) { out = whole * kDimensionPrecisionScale; return true; }
    // Quantize the complete dimension. Subtracting the integer part first can
    // move exact decimal halves below their tie through cancellation.
    double scaled = pixels * kDimensionPrecisionScale;
    const double half = std::floor(scaled) + .5;
    const double error = 4 * std::numeric_limits<double>::epsilon() * std::max(1.0, scaled);
    if (std::abs(scaled - half) <= error) scaled = half;
    const auto units = static_cast<std::int64_t>(std::floor(scaled + .5));
    if (units > kDimensionPrecisionMaximum) return false;
    out = units; return true;
}
} // namespace jellyframe::detail
