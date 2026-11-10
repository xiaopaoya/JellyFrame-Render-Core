#include "background_gradient_geometry.h"

namespace jellyframe::detail {
namespace {
bool valid_size_mode(BackgroundGradientSizeMode mode) noexcept {
    return mode == BackgroundGradientSizeMode::Auto ||
        mode == BackgroundGradientSizeMode::Cover ||
        mode == BackgroundGradientSizeMode::Contain ||
        mode == BackgroundGradientSizeMode::Stretch;
}
BackgroundGradientGeometryStatus box_status(BackgroundBoxAreasStatus status) noexcept {
    switch (status) {
    case BackgroundBoxAreasStatus::Ready: return BackgroundGradientGeometryStatus::Ready;
    case BackgroundBoxAreasStatus::Empty: return BackgroundGradientGeometryStatus::Empty;
    case BackgroundBoxAreasStatus::DimensionLimit: return BackgroundGradientGeometryStatus::DimensionLimit;
    case BackgroundBoxAreasStatus::CoordinateOverflow: return BackgroundGradientGeometryStatus::CoordinateOverflow;
    case BackgroundBoxAreasStatus::InvalidInput: return BackgroundGradientGeometryStatus::InvalidGeometry;
    }
    return BackgroundGradientGeometryStatus::InvalidGeometry;
}
BackgroundGradientGeometryStatus repeat_status(BackgroundRepeatStatus status) noexcept {
    switch (status) {
    case BackgroundRepeatStatus::Ready: return BackgroundGradientGeometryStatus::Ready;
    case BackgroundRepeatStatus::Empty: return BackgroundGradientGeometryStatus::Empty;
    case BackgroundRepeatStatus::InvalidGeometry: return BackgroundGradientGeometryStatus::InvalidGeometry;
    case BackgroundRepeatStatus::FractionalGeometry: return BackgroundGradientGeometryStatus::FractionalGeometry;
    case BackgroundRepeatStatus::UnsupportedCombination: return BackgroundGradientGeometryStatus::UnsupportedCombination;
    case BackgroundRepeatStatus::TileLimit: return BackgroundGradientGeometryStatus::TileLimit;
    case BackgroundRepeatStatus::CoordinateOverflow: return BackgroundGradientGeometryStatus::CoordinateOverflow;
    }
    return BackgroundGradientGeometryStatus::InvalidGeometry;
}
} // namespace

BackgroundGradientGeometryStatus resolve_background_gradient_geometry(
    const BackgroundGradientGeometryRequest& request,
    BackgroundGradientGeometry& result) noexcept {
    if (!valid_size_mode(request.size_mode))
        return BackgroundGradientGeometryStatus::InvalidGeometry;
    BackgroundBoxAreas boxes;
    const auto areas = resolve_background_box_areas(request.boxes, boxes);
    if (areas != BackgroundBoxAreasStatus::Ready) return box_status(areas);
    BackgroundRepeatRequest repeat;
    repeat.image.positioning_area = boxes.positioning_area;
    repeat.image.painting_area = boxes.painting_area;
    // No-natural-size/ratio gradients use the positioning area for all four
    // admitted modes. Stretch also avoids image-only round ratio restoration.
    repeat.image.size_mode = BackgroundImageSizeMode::Stretch;
    repeat.image.position_x_percent_q1000 = request.position_x_percent_q1000;
    repeat.image.position_y_percent_q1000 = request.position_y_percent_q1000;
    repeat.repeat_x = request.repeat_x;
    repeat.repeat_y = request.repeat_y;
    repeat.max_tiles = request.max_tiles;
    BackgroundGradientGeometry candidate;
    const auto planned = plan_background_image_tiles(repeat, candidate.sampling_tiles);
    if (planned != BackgroundRepeatStatus::Ready) return repeat_status(planned);
    candidate.painting_area = boxes.painting_area;
    candidate.painting_radius = boxes.painting_radius;
    candidate.painting_radius_y = boxes.painting_radius_y;
    result = candidate;
    return BackgroundGradientGeometryStatus::Ready;
}
} // namespace jellyframe::detail
