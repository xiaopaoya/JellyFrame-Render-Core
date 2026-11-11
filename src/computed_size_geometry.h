#pragma once
#include "render_core/style.h"
#include "css_dimension_precision.h"

namespace jellyframe::detail {
// Q1000 axis math. -1 is auto/indefinite; -2 is malformed input.
inline std::int64_t scale_size_units(std::int64_t value, std::int64_t numerator, std::int64_t denominator) {
    if (value < 0 || value > kDimensionPrecisionMaximum || numerator < 0 || denominator <= 0 ||
        numerator > static_cast<std::int64_t>(std::numeric_limits<int>::max()) * 1000 ||
        denominator > std::numeric_limits<int>::max()) return -2;
    const auto whole = value / denominator, rest = value % denominator;
    if (numerator && whole > kDimensionPrecisionMaximum / numerator) return kDimensionPrecisionMaximum;
    const auto major = whole * numerator;
    // Callers use int32 ratio components or denominator=100000 percentages.
    if (numerator && rest > (std::numeric_limits<std::int64_t>::max()-denominator/2)/numerator) return -2;
    const auto minor = (rest * numerator + denominator / 2) / denominator;
    return minor > kDimensionPrecisionMaximum - major ? kDimensionPrecisionMaximum : major + minor;
}
inline std::int64_t computed_size_units(const Style& style, SizeProperty property, std::int64_t basis) {
    const auto p = static_cast<unsigned>(property);
    if (p >= 7 || basis < -1 || basis > kDimensionPrecisionMaximum) return -2;
    SizeKind kind;
    std::int64_t value;
    std::int64_t percentage = 0;
    if (!style.computed_sizes.read_typed(property,kind,value,percentage)) {
        if (property==SizeProperty::FlexBasis && style.flex_basis_kind==FlexBasisKind::Percentage) {
            if (style.flex_basis<0) return -2;
            return basis<0 ? -1 : scale_size_units(basis,style.flex_basis,100000);
        }
        constexpr int Style::* scalars[] = {&Style::width,&Style::height,&Style::min_width,&Style::min_height,
            &Style::max_width,&Style::max_height,&Style::flex_basis};
        constexpr int Style::* percentages[] = {&Style::width_percent,&Style::height_percent,&Style::min_width_percent,
            &Style::min_height_percent,&Style::max_width_percent,&Style::max_height_percent};
        if (p < 6 && style.*percentages[p] >= 0) {
            kind = SizeKind::Percent;
            value = static_cast<std::int64_t>(style.*percentages[p]) * 1000;
        } else {
            const auto scalar = style.*scalars[p];
            return scalar < -1 ? -2 : scalar < 0 ? -1 : static_cast<std::int64_t>(scalar) * 1000;
        }
    }
    if (kind == SizeKind::LengthPercentage) {
        if (basis < 0) return -1;
        // Keep signed components through the combination. __int128 prevents
        // overflow for the bounded INT_MAX/Q1000 representation.
        const auto whole = basis / 100000;
        const auto remainder = basis % 100000;
        const auto major = whole * percentage;
        const auto minor_product = remainder * percentage;
        const auto minor = minor_product >= 0 ? (minor_product + 50000) / 100000
                                              : -((-minor_product + 50000) / 100000);
        if ((minor > 0 && major > std::numeric_limits<std::int64_t>::max() - minor) ||
            (minor < 0 && major < std::numeric_limits<std::int64_t>::min() - minor)) return -2;
        const auto combined = value + major + minor;
        if (combined <= 0) return 0;
        if (combined > kDimensionPrecisionMaximum) return kDimensionPrecisionMaximum;
        return combined;
    }
    if (kind == SizeKind::Pixels) return value;
    return basis < 0 ? -1 : scale_size_units(basis,value,100000);
}
inline std::int64_t computed_content_axis(const Style& style, bool rows, std::int64_t basis,
    std::int64_t fallback, std::int64_t decoration) {
    if (fallback < -1 || fallback > kDimensionPrecisionMaximum || decoration < 0) return -2;
    const auto content = [&](SizeProperty property) {
        const auto units = computed_size_units(style,property,basis);
        return units < 0 || !style.box_sizing_border_box ? units : std::max<std::int64_t>(0,units-decoration);
    };
    auto preferred = content(rows ? SizeProperty::Height : SizeProperty::Width);
    const auto minimum = content(rows ? SizeProperty::MinHeight : SizeProperty::MinWidth);
    const auto maximum = content(rows ? SizeProperty::MaxHeight : SizeProperty::MaxWidth);
    if (preferred < -1 || minimum < -1 || maximum < -1) return -2;
    if (preferred < 0) preferred = fallback;
    if (preferred < 0) return -1;
    if (maximum >= 0) preferred = std::min(preferred,maximum);
    if (minimum >= 0) preferred = std::max(preferred,minimum);
    return preferred;
}
inline int project_size_edge(std::int64_t units) {
    if (units >= static_cast<std::int64_t>(std::numeric_limits<int>::max())*1000) return std::numeric_limits<int>::max();
    if (units <= static_cast<std::int64_t>(std::numeric_limits<int>::min())*1000) return std::numeric_limits<int>::min();
    const auto rounded = units >= 0 ? (units + 500) / 1000 : -((-units + 500) / 1000);
    return clamp_int64_to_int(rounded);
}
inline int project_size_extent(std::int64_t origin, std::int64_t size) {
    if (size <= 0) return 0;
    const auto end = origin > std::numeric_limits<std::int64_t>::max()-size ?
        std::numeric_limits<std::int64_t>::max() : origin+size;
    return clamp_int64_to_int(std::max<std::int64_t>(0,
        static_cast<std::int64_t>(project_size_edge(end))-project_size_edge(origin)));
}
} // namespace jellyframe::detail
