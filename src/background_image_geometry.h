#pragma once

#include "render_core/geometry.h"

#include <cstdint>

namespace jellyframe::detail {

enum class BackgroundImageSizeMode : std::uint8_t {
    Intrinsic,
    Cover,
    Contain,
    Stretch
};

enum class BackgroundImageGeometryStatus : std::uint8_t {
    Ready,
    Empty,
    MissingIntrinsicSize,
    InvalidInput,
    InvalidIntrinsicSize,
    DimensionLimit,
    CoordinateOverflow
};

struct BackgroundImageGeometryRequest {
    Rect positioning_area;
    Rect painting_area;
    bool intrinsic_size_known = false;
    std::uint16_t intrinsic_width = 0;
    std::uint16_t intrinsic_height = 0;
    BackgroundImageSizeMode size_mode = BackgroundImageSizeMode::Intrinsic;
    // CSS percentages in thousandths of one percent; 100000 is 100%.
    std::int32_t position_x_percent_q1000 = 50000;
    std::int32_t position_y_percent_q1000 = 50000;
};

struct BackgroundImageGeometry {
    Rect destination;
    Rect painting_clip;
    // Tiling must not turn a rounded F1a size into an incorrect repeat period.
    bool dimensions_exact = true;
};

// Encodes the painting rectangle relative to the positioned destination for a
// compact display command. The result is unchanged when offsets cannot fit.
bool make_background_paint_clip(Rect destination, Rect painting_area,
                                BackgroundPaintClip& result) noexcept;

enum class BackgroundBoxArea : std::uint8_t {
    BorderBox,
    PaddingBox,
    ContentBox
};

struct BackgroundImageInsets {
    int top = 0;
    int right = 0;
    int bottom = 0;
    int left = 0;
};

enum class BackgroundBoxAreasStatus : std::uint8_t {
    Ready,
    Empty,
    InvalidInput,
    DimensionLimit,
    CoordinateOverflow
};

struct BackgroundBoxAreasRequest {
    Rect border_box;
    BackgroundImageInsets border;
    BackgroundImageInsets padding;
    BackgroundBoxArea origin = BackgroundBoxArea::PaddingBox;
    BackgroundBoxArea clip = BackgroundBoxArea::BorderBox;
    int border_radius = 0;
};

struct BackgroundBoxAreas {
    Rect positioning_area;
    Rect painting_area;
    int painting_radius = 0;
};

// Derives independent CSS background origin/clip rectangles for the initial
// uniform-edge, circular-radius slice. Nonuniform or negative insets are
// rejected. Radius is reduced by the inset to the selected clip box and
// clamped at zero. Output is atomic; failures leave `result` unchanged.
BackgroundBoxAreasStatus resolve_background_box_areas(
    const BackgroundBoxAreasRequest& request,
    BackgroundBoxAreas& result) noexcept;

// Computes the initial image destination without allocation or mutation.
// Rect is integer CSS-pixel geometry, so every rational result is rounded to the
// nearest integer with exact half ties away from zero. CSS's fractional layout
// coordinates are quantized by the caller before this function is called; this
// helper does not claim subpixel browser parity. The output size and both input
// areas are bounded to 8191 CSS pixels per axis. Coordinates are range-checked.
// Unknown natural dimensions are accepted only for Stretch, whose two output
// axes are determined by the positioning area. Known dimensions must be both
// positive and within the same bound, even when Stretch is selected.
// Zero positioning extents admit a positive intrinsic image, but ratio sizing
// or Stretch with a zero resolved extent is Empty. The painting area must be
// positive. `dimensions_exact` distinguishes rounded sizes for repeat callers.
BackgroundImageGeometryStatus resolve_background_image_geometry(
    const BackgroundImageGeometryRequest& request,
    BackgroundImageGeometry& result) noexcept;

} // namespace jellyframe::detail
