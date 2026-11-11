#pragma once

#include "render_core/layout.h"
#include "render_core/raster_primitives.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace jellyframe::detail {

// Shared used overflow-clip geometry for layer painting and layout hit traversal.
inline bool has_overflow_clip(const Style& style) {
    return style.overflow == "hidden" || style.overflow == "scroll" ||
        style.overflow == "auto" || style.overflow == "clip";
}

inline EllipticalCornerRadii resolve_border_radii(const LayoutBox& box) {
    if (box.style.border_radius_axes_specified) {
        const auto resolve_axis = [&](const std::array<int, 4>& values, std::uint8_t percent_mask,
                                      bool horizontal) {
            const int basis = std::max(0, horizontal ? box.rect.width : box.rect.height);
            const auto used = [&](std::size_t index) {
                const std::int64_t value = (percent_mask & (1U << index)) != 0U
                    ? (static_cast<std::int64_t>(basis) * values[index] + 50) / 100
                    : values[index];
                return static_cast<int>(std::clamp<std::int64_t>(value, 0, std::numeric_limits<int>::max()));
            };
            return CornerRadii{used(0), used(1), used(2), used(3)};
        };
        CornerRadii x = resolve_axis(box.style.border_radius_x_values,
                                     box.style.border_radius_x_percent_mask, true);
        CornerRadii y = resolve_axis(box.style.border_radius_y_values,
                                     box.style.border_radius_y_percent_mask, false);
        const double scale = std::min({
            1.0,
            static_cast<double>(x.top_left) + x.top_right == 0 ? 1.0 : static_cast<double>(box.rect.width) / (static_cast<double>(x.top_left) + x.top_right),
            static_cast<double>(x.bottom_left) + x.bottom_right == 0 ? 1.0 : static_cast<double>(box.rect.width) / (static_cast<double>(x.bottom_left) + x.bottom_right),
            static_cast<double>(y.top_left) + y.bottom_left == 0 ? 1.0 : static_cast<double>(box.rect.height) / (static_cast<double>(y.top_left) + y.bottom_left),
            static_cast<double>(y.top_right) + y.bottom_right == 0 ? 1.0 : static_cast<double>(box.rect.height) / (static_cast<double>(y.top_right) + y.bottom_right),
        });
        const auto scaled = [scale](int value) { return std::max(0, static_cast<int>(value * scale)); };
        x = {scaled(x.top_left), scaled(x.top_right), scaled(x.bottom_right), scaled(x.bottom_left)};
        y = {scaled(y.top_left), scaled(y.top_right), scaled(y.bottom_right), scaled(y.bottom_left)};
        return {x, y};
    }
    const int max_radius = std::max(0, std::min(box.rect.width, box.rect.height) / 2);
    if (box.style.border_radius_percent >= 0) {
        const std::int64_t min_dimension = std::max(0, std::min(box.rect.width, box.rect.height));
        const std::int64_t percent = box.style.border_radius_percent;
        const std::int64_t radius = (min_dimension * percent + 50) / 100;
        const int resolved = static_cast<int>(std::min<std::int64_t>(max_radius, std::max<std::int64_t>(0, radius)));
        const CornerRadii same{resolved, resolved, resolved, resolved};
        return {same, same};
    }
    CornerRadii radii = decode_corner_radii(box.style.border_radius);
    const auto edge_scale = [](int dimension, int first, int second) {
        const std::int64_t sum = static_cast<std::int64_t>(first) + second;
        return sum == 0 ? 1.0 : static_cast<double>(std::max(0, dimension)) / sum;
    };
    const double scale = std::min({1.0,
        edge_scale(box.rect.width, radii.top_left, radii.top_right),
        edge_scale(box.rect.width, radii.bottom_left, radii.bottom_right),
        edge_scale(box.rect.height, radii.top_left, radii.bottom_left),
        edge_scale(box.rect.height, radii.top_right, radii.bottom_right)});
    const auto scaled = [scale](int value) { return static_cast<int>(value * scale); };
    radii = {scaled(radii.top_left), scaled(radii.top_right),
             scaled(radii.bottom_right), scaled(radii.bottom_left)};
    return {radii, radii};
}

inline int resolved_border_radius(const LayoutBox& box) {
    if (!box.style.border_radius_axes_specified && box.style.border_radius_percent >= 0) {
        const std::int64_t min_dimension = std::max(0, std::min(box.rect.width, box.rect.height));
        const std::int64_t percent = box.style.border_radius_percent;
        const std::int64_t radius = (min_dimension * percent + 50) / 100;
        const int max_radius = std::max(0, std::min(box.rect.width, box.rect.height) / 2);
        return static_cast<int>(std::min<std::int64_t>(max_radius, std::max<std::int64_t>(0, radius)));
    }
    const CornerRadii radii = resolve_border_radii(box).horizontal;
    const auto pair = resolve_border_radii(box);
    const auto representable = [](const CornerRadii& axis) {
        return std::max({axis.top_left, axis.top_right, axis.bottom_right, axis.bottom_left}) <= 127 ||
            (axis.top_left == axis.top_right && axis.top_left == axis.bottom_right &&
             axis.top_left == axis.bottom_left);
    };
    if (!representable(pair.horizontal) || !representable(pair.vertical)) return 0;
    return encode_corner_radii(radii);
}

inline int resolved_border_radius_y(const LayoutBox& box) {
    if (!box.style.border_radius_axes_specified && box.style.border_radius_percent >= 0)
        return resolved_border_radius(box);
    const auto pair = resolve_border_radii(box);
    const auto representable = [](const CornerRadii& axis) {
        return std::max({axis.top_left, axis.top_right, axis.bottom_right, axis.bottom_left}) <= 127 ||
            (axis.top_left == axis.top_right && axis.top_left == axis.bottom_right &&
             axis.top_left == axis.bottom_left);
    };
    if (!representable(pair.horizontal) || !representable(pair.vertical)) return 0;
    return encode_corner_radii(pair.vertical);
}

} // namespace jellyframe::detail
