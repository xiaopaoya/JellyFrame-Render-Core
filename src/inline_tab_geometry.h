#pragma once

#include <cstdint>

namespace jellyframe::detail {
// Integer CSS pixels, with a twice-pixel pen retaining the half-ch threshold.
// Callers validate nonnegative interval/ch and their own coordinate budgets.
inline std::int64_t inline_tab_advance_twice(std::int64_t pen, int interval, int half_ch_twice) {
    if (!interval) return 0;
    const auto stop = static_cast<std::int64_t>(interval) * 2;
    const auto remainder = (pen % stop + stop) % stop;
    auto advance = stop - remainder;
    if (advance < half_ch_twice) advance += stop;
    return advance;
}
} // namespace jellyframe::detail
