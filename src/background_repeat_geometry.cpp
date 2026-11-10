#include "background_repeat_geometry.h"

#include <algorithm>
#include <limits>

namespace jellyframe::detail {
namespace {
struct Axis {
    std::int64_t anchor = 0;
    std::int64_t size = 0;
    std::int64_t step = 0;
    std::int64_t first = 0;
    std::int64_t count = 0;
};
std::int64_t floor_div(std::int64_t n, std::int64_t d) noexcept {
    return n / d - (n % d < 0 ? 1 : 0);
}
bool valid_repeat(BackgroundAxisRepeat repeat) noexcept {
    return repeat == BackgroundAxisRepeat::NoRepeat || repeat == BackgroundAxisRepeat::Repeat ||
        repeat == BackgroundAxisRepeat::Space || repeat == BackgroundAxisRepeat::Round;
}
BackgroundRepeatStatus axis_plan(int positioning_origin, int positioning_size,
    int painting_origin, int painting_size, int tile_size, std::int32_t percent,
    BackgroundAxisRepeat repeat, Axis& axis) noexcept {
    axis.size = tile_size;
    axis.step = tile_size;
    const std::int64_t copies = positioning_size / tile_size;
    const bool spaced = repeat == BackgroundAxisRepeat::Space && copies >= 2;
    const bool periodic = repeat == BackgroundAxisRepeat::Repeat ||
        repeat == BackgroundAxisRepeat::Round || spaced;
    if (repeat == BackgroundAxisRepeat::Round || spaced) {
        axis.anchor = positioning_origin;
        if (spaced) {
            if ((positioning_size - tile_size) % (copies - 1) != 0)
                return BackgroundRepeatStatus::FractionalGeometry;
            axis.step = (positioning_size - tile_size) / (copies - 1);
        }
    } else {
        const auto offset = static_cast<std::int64_t>(positioning_size - tile_size) * percent;
        if (offset % 100000 != 0) return BackgroundRepeatStatus::FractionalGeometry;
        axis.anchor = static_cast<std::int64_t>(positioning_origin) + offset / 100000;
    }
    const auto clip_start = static_cast<std::int64_t>(painting_origin);
    const auto clip_end = clip_start + painting_size;
    if (periodic) {
        axis.first = floor_div(clip_start - axis.anchor - tile_size, axis.step) + 1;
        const auto last = floor_div(clip_end - axis.anchor - 1, axis.step);
        axis.count = std::max<std::int64_t>(0, last - axis.first + 1);
    } else {
        axis.count = axis.anchor < clip_end && axis.anchor + tile_size > clip_start ? 1 : 0;
    }
    return BackgroundRepeatStatus::Ready;
}
BackgroundRepeatStatus round_size(int extent, int& size) noexcept {
    if (extent == 0) return BackgroundRepeatStatus::Empty;
    const auto count = std::max<std::int64_t>(1,
        (2LL * extent + size) / (2LL * size));
    if (extent % count != 0) return BackgroundRepeatStatus::FractionalGeometry;
    size = static_cast<int>(extent / count);
    return BackgroundRepeatStatus::Ready;
}
bool representable_rect(std::int64_t x, std::int64_t y, int w, int h) noexcept {
    return x >= std::numeric_limits<int>::min() && y >= std::numeric_limits<int>::min() &&
        x + w <= std::numeric_limits<int>::max() && y + h <= std::numeric_limits<int>::max();
}
} // namespace

BackgroundRepeatStatus plan_background_image_tiles(
    const BackgroundRepeatRequest& request, BackgroundRepeatPlan& result) noexcept {
    if (!valid_repeat(request.repeat_x) || !valid_repeat(request.repeat_y))
        return BackgroundRepeatStatus::InvalidGeometry;
    BackgroundImageGeometry geometry;
    const auto initial = resolve_background_image_geometry(request.image, geometry);
    if (initial == BackgroundImageGeometryStatus::Empty) return BackgroundRepeatStatus::Empty;
    if (initial != BackgroundImageGeometryStatus::Ready) return BackgroundRepeatStatus::InvalidGeometry;
    const bool repeat = request.repeat_x != BackgroundAxisRepeat::NoRepeat ||
        request.repeat_y != BackgroundAxisRepeat::NoRepeat;
    BackgroundRepeatPlan candidate;
    if (!repeat) {
        if (request.max_tiles == 0) return BackgroundRepeatStatus::TileLimit;
        candidate.tiles[0] = geometry.destination;
        candidate.count = 1;
        result = candidate;
        return BackgroundRepeatStatus::Ready;
    }
    if (!geometry.dimensions_exact) return BackgroundRepeatStatus::FractionalGeometry;
    int width = geometry.destination.width;
    int height = geometry.destination.height;
    const bool round_x = request.repeat_x == BackgroundAxisRepeat::Round;
    const bool round_y = request.repeat_y == BackgroundAxisRepeat::Round;
    if ((round_x || round_y) && request.image.size_mode != BackgroundImageSizeMode::Intrinsic &&
        request.image.size_mode != BackgroundImageSizeMode::Stretch)
        return BackgroundRepeatStatus::UnsupportedCombination;
    if (round_x) {
        const auto status = round_size(request.image.positioning_area.width, width);
        if (status != BackgroundRepeatStatus::Ready) return status;
    }
    if (round_y) {
        const auto status = round_size(request.image.positioning_area.height, height);
        if (status != BackgroundRepeatStatus::Ready) return status;
    }
    if (request.image.size_mode == BackgroundImageSizeMode::Intrinsic && round_x != round_y) {
        const auto numerator = round_x
            ? static_cast<std::int64_t>(width) * request.image.intrinsic_height
            : static_cast<std::int64_t>(height) * request.image.intrinsic_width;
        const int denominator = round_x ? request.image.intrinsic_width : request.image.intrinsic_height;
        if (numerator % denominator != 0) return BackgroundRepeatStatus::FractionalGeometry;
        const auto restored = numerator / denominator;
        if (restored <= 0 || restored > 8191) return BackgroundRepeatStatus::InvalidGeometry;
        if (round_x) height = static_cast<int>(restored);
        else width = static_cast<int>(restored);
    }
    Axis x, y;
    auto status = axis_plan(request.image.positioning_area.x, request.image.positioning_area.width,
        request.image.painting_area.x, request.image.painting_area.width, width,
        request.image.position_x_percent_q1000, request.repeat_x, x);
    if (status != BackgroundRepeatStatus::Ready) return status;
    status = axis_plan(request.image.positioning_area.y, request.image.positioning_area.height,
        request.image.painting_area.y, request.image.painting_area.height, height,
        request.image.position_y_percent_q1000, request.repeat_y, y);
    if (status != BackgroundRepeatStatus::Ready) return status;
    if (x.count == 0 || y.count == 0) return BackgroundRepeatStatus::Empty;
    const auto limit = std::min(request.max_tiles, kMaxBackgroundImageTiles);
    if (x.count > static_cast<std::int64_t>(limit) ||
        y.count > static_cast<std::int64_t>(limit) ||
        x.count * y.count > static_cast<std::int64_t>(limit))
        return BackgroundRepeatStatus::TileLimit;
    for (std::int64_t iy = 0; iy < y.count; ++iy) {
        for (std::int64_t ix = 0; ix < x.count; ++ix) {
            const auto left = x.anchor + (x.first + ix) * x.step;
            const auto top = y.anchor + (y.first + iy) * y.step;
            if (!representable_rect(left, top, width, height))
                return BackgroundRepeatStatus::CoordinateOverflow;
            candidate.tiles[candidate.count++] = {
                static_cast<int>(left), static_cast<int>(top), width, height};
        }
    }
    result = candidate;
    return BackgroundRepeatStatus::Ready;
}
} // namespace jellyframe::detail
