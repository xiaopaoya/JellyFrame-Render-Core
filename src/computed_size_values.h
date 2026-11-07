#pragma once
#include "render_core/style.h"
#include "css_dimension_precision.h"

namespace jellyframe::detail {
// Publish retained precision before changing scalar proxies. Allocation failure
// therefore leaves both representations unchanged.
inline bool apply_computed_size(Style& style, SizeProperty property, SizeKind kind, double value) {
    const auto index = static_cast<unsigned>(property);
    if (index >= 7 || (kind != SizeKind::Pixels && kind != SizeKind::Percent) ||
        (index == 6 && kind == SizeKind::Percent)) return false;
    if (kind == SizeKind::Percent && std::isfinite(value)) value = std::min(1000.0, value);
    std::int64_t units = 0;
    if (!encode_dimension_units(value, units)) return false;
    if (!style.computed_sizes.set(property, kind, value)) return false;
    constexpr int Style::* scalars[] = {&Style::width,&Style::height,&Style::min_width,&Style::min_height,
        &Style::max_width,&Style::max_height,&Style::flex_basis};
    constexpr int Style::* percentages[] = {&Style::width_percent,&Style::height_percent,&Style::min_width_percent,
        &Style::min_height_percent,&Style::max_width_percent,&Style::max_height_percent};
    const auto rounded = static_cast<int>(std::min<std::int64_t>(
        std::numeric_limits<int>::max(), (units + 500) / 1000));
    style.*scalars[index] = kind == SizeKind::Pixels ? rounded : -1;
    if (index != 6) style.*percentages[index] = kind == SizeKind::Percent ? rounded : -1;
    return true;
}
} // namespace jellyframe::detail
