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

// Publish a mixed dimension together with its scalar compatibility state.
// Work on a value copy so allocation or validation failure cannot expose a
// partially updated Style. A zero percentage remains a typed mixed value.
inline bool apply_computed_length_percentage(Style& style, SizeProperty property,
                                             double length_pixels, double percentage) {
    const auto index = static_cast<unsigned>(property);
    if (index >= 7 || !std::isfinite(length_pixels) || !std::isfinite(percentage) ||
        std::abs(length_pixels) > std::numeric_limits<int>::max() ||
        std::abs(percentage) > 1000.0) return false;

    Style candidate = style;
    if (!candidate.computed_sizes.set_length_percentage(property, length_pixels, percentage)) return false;
    constexpr int Style::* scalars[] = {&Style::width,&Style::height,&Style::min_width,&Style::min_height,
        &Style::max_width,&Style::max_height,&Style::flex_basis};
    constexpr int Style::* percentages[] = {&Style::width_percent,&Style::height_percent,&Style::min_width_percent,
        &Style::min_height_percent,&Style::max_width_percent,&Style::max_height_percent};
    candidate.*scalars[index] = -1;
    if (index != 6) {
        candidate.*percentages[index] = -1;
        candidate.table_properties |= table_sizing_provenance_bit(property);
    } else {
        candidate.flex_basis_kind = FlexBasisKind::Length;
    }
    style = std::move(candidate);
    return true;
}
} // namespace jellyframe::detail
