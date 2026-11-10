#pragma once

#include "background_repeat_geometry.h"

namespace jellyframe::detail {

// Gradients have no natural dimensions or aspect ratio. In this slice auto,
// cover, contain and 100% 100% all use the positioning area's two dimensions.
// Explicit length/percentage axes, attachment and per-layer lists are deferred.
enum class BackgroundGradientSizeMode : std::uint8_t { Auto, Cover, Contain, Stretch };
enum class BackgroundGradientGeometryStatus : std::uint8_t {
    Ready, Empty, InvalidGeometry, DimensionLimit, FractionalGeometry,
    UnsupportedCombination, TileLimit, CoordinateOverflow
};

struct BackgroundGradientGeometryRequest {
    BackgroundBoxAreasRequest boxes;
    BackgroundGradientSizeMode size_mode = BackgroundGradientSizeMode::Auto;
    std::int32_t position_x_percent_q1000 = 50000;
    std::int32_t position_y_percent_q1000 = 50000;
    BackgroundAxisRepeat repeat_x = BackgroundAxisRepeat::Repeat;
    BackgroundAxisRepeat repeat_y = BackgroundAxisRepeat::Repeat;
    std::size_t max_tiles = kMaxBackgroundImageTiles;
};

struct BackgroundGradientGeometry {
    // Full rectangles for gradient interpolation, never cropped to painting.
    BackgroundRepeatPlan sampling_tiles;
    // Every tile is clipped by this one box/radius, independently of its own
    // sampling rectangle. A tile must not acquire rounded tile corners.
    Rect painting_area;
    int painting_radius = 0;
};

// Private geometry foundation only: no production caller or new CSS admission.
// Composes the image-independent box resolver and bounded integer tile planner;
// no image resource metadata, heap allocation or raster surface is involved.
// All non-Ready returns, including Empty, leave result unchanged. This inherits
// the box resolver's uniform-inset/circular-radius and 8191px area boundaries.
BackgroundGradientGeometryStatus resolve_background_gradient_geometry(
    const BackgroundGradientGeometryRequest& request,
    BackgroundGradientGeometry& result) noexcept;

} // namespace jellyframe::detail
