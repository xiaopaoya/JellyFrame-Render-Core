#pragma once

#include "render_core/style.h"

namespace jellyframe::detail {

inline bool background_colors_equal(Color a, Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

inline Color computed_background_color(const Style& style) {
    return style.background_color;
}

inline bool computed_background_images_equal(const Style& left, const Style& right) {
    if (left.background_layer_count != right.background_layer_count ||
        left.background_layer_count > kMaxBackgroundLayers) return false;
    for (std::size_t i = 0; i < left.background_layer_count; ++i) {
        const auto& a = left.background_layers[i];
        const auto& b = right.background_layers[i];
        if (a.kind != b.kind || a.image.resource_id != b.image.resource_id ||
            a.image.size != b.image.size ||
            a.image.position_x_percent != b.image.position_x_percent ||
            a.image.position_y_percent != b.image.position_y_percent ||
            a.image.origin != b.image.origin || a.image.clip != b.image.clip ||
            a.image.repeat_x != b.image.repeat_x || a.image.repeat_y != b.image.repeat_y ||
            a.paint.kind != b.paint.kind || a.paint.axis != b.paint.axis ||
            a.paint.stop_percent != b.paint.stop_percent ||
            !background_colors_equal(a.paint.color, b.paint.color) ||
            !background_colors_equal(a.paint.color2, b.paint.color2)) return false;
    }
    return left.image_rendering == right.image_rendering;
}

} // namespace jellyframe::detail
