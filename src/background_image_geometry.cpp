#include "background_image_geometry.h"

#include <algorithm>
#include <limits>

namespace jellyframe::detail {
namespace {
constexpr std::int64_t kPercentScale = 100000;
constexpr int kMaxDimension = 8191;

bool valid_area(const Rect& rect, bool allow_zero = false) noexcept {
    if (rect.width < (allow_zero ? 0 : 1) || rect.height < (allow_zero ? 0 : 1) || rect.width > kMaxDimension ||
        rect.height > kMaxDimension) return false;
    const auto right = static_cast<std::int64_t>(rect.x) + rect.width;
    const auto bottom = static_cast<std::int64_t>(rect.y) + rect.height;
    return right <= std::numeric_limits<int>::max() &&
        right >= std::numeric_limits<int>::min() &&
        bottom <= std::numeric_limits<int>::max() &&
        bottom >= std::numeric_limits<int>::min();
}

bool valid_area_kind(BackgroundBoxArea area) noexcept {
    return area == BackgroundBoxArea::BorderBox ||
        area == BackgroundBoxArea::PaddingBox || area == BackgroundBoxArea::ContentBox;
}

bool uniform_nonnegative(const BackgroundImageInsets& insets) noexcept {
    return insets.top >= 0 && insets.top == insets.right &&
        insets.top == insets.bottom && insets.top == insets.left &&
        insets.top <= kMaxDimension;
}

BackgroundImageInsets selected_insets(BackgroundBoxArea area,
                                      const BackgroundImageInsets& border,
                                      const BackgroundImageInsets& padding) noexcept {
    if (area == BackgroundBoxArea::BorderBox) return {};
    if (area == BackgroundBoxArea::PaddingBox) return border;
    return {border.top + padding.top, border.right + padding.right,
            border.bottom + padding.bottom, border.left + padding.left};
}

BackgroundBoxAreasStatus inset_area(const Rect& box, const BackgroundImageInsets& insets,
                                    Rect& result, bool allow_zero = false) noexcept {
    const auto left = static_cast<std::int64_t>(box.x) + insets.left;
    const auto top = static_cast<std::int64_t>(box.y) + insets.top;
    const auto right = static_cast<std::int64_t>(box.x) + box.width - insets.right;
    const auto bottom = static_cast<std::int64_t>(box.y) + box.height - insets.bottom;
    if (left < std::numeric_limits<int>::min() || left > std::numeric_limits<int>::max() ||
        top < std::numeric_limits<int>::min() || top > std::numeric_limits<int>::max() ||
        right < std::numeric_limits<int>::min() || right > std::numeric_limits<int>::max() ||
        bottom < std::numeric_limits<int>::min() || bottom > std::numeric_limits<int>::max())
        return BackgroundBoxAreasStatus::CoordinateOverflow;
    const auto width = right - left;
    const auto height = bottom - top;
    if (width < 0 || height < 0) return BackgroundBoxAreasStatus::Empty;
    if (!allow_zero && (width == 0 || height == 0)) return BackgroundBoxAreasStatus::Empty;
    if (width > kMaxDimension || height > kMaxDimension)
        return BackgroundBoxAreasStatus::DimensionLimit;
    result = {static_cast<int>(left), static_cast<int>(top),
              static_cast<int>(width), static_cast<int>(height)};
    return BackgroundBoxAreasStatus::Ready;
}

std::int64_t rounded_ratio(std::int64_t numerator, std::int64_t denominator) noexcept {
    const bool negative = numerator < 0;
    const auto magnitude = negative ? -numerator : numerator;
    const auto rounded = (magnitude + denominator / 2) / denominator;
    return negative ? -rounded : rounded;
}

bool positioned_origin(int origin, int free_space, std::int32_t percent_q1000,
                       int& result) noexcept {
    const auto offset = rounded_ratio(
        static_cast<std::int64_t>(free_space) * percent_q1000, kPercentScale);
    const auto coordinate = static_cast<std::int64_t>(origin) + offset;
    if (coordinate < std::numeric_limits<int>::min() ||
        coordinate > std::numeric_limits<int>::max()) return false;
    result = static_cast<int>(coordinate);
    return true;
}

bool scaled_dimensions(const Rect& area, const BackgroundImageGeometryRequest& request,
                       int& width, int& height) noexcept {
    const auto iw = static_cast<std::int64_t>(request.intrinsic_width);
    const auto ih = static_cast<std::int64_t>(request.intrinsic_height);
    const auto aw = static_cast<std::int64_t>(area.width);
    const auto ah = static_cast<std::int64_t>(area.height);
    const bool width_scale_is_smaller = aw * ih <= ah * iw;
    const bool use_width = request.size_mode == BackgroundImageSizeMode::Contain
        ? width_scale_is_smaller : !width_scale_is_smaller;
    const auto scaled_width = use_width ? aw : rounded_ratio(ah * iw, ih);
    const auto scaled_height = use_width ? rounded_ratio(aw * ih, iw) : ah;
    if (scaled_width <= 0 || scaled_height <= 0 || scaled_width > kMaxDimension ||
        scaled_height > kMaxDimension) return false;
    width = static_cast<int>(scaled_width);
    height = static_cast<int>(scaled_height);
    return true;
}
} // namespace

bool make_background_paint_clip(Rect destination, Rect painting_area,
                                BackgroundPaintClip& result) noexcept {
    if (destination.width <= 0 || destination.height <= 0 ||
        painting_area.width <= 0 || painting_area.height <= 0 ||
        painting_area.width > std::numeric_limits<std::uint16_t>::max() ||
        painting_area.height > std::numeric_limits<std::uint16_t>::max()) return false;
    const auto dx = static_cast<std::int64_t>(painting_area.x) - destination.x;
    const auto dy = static_cast<std::int64_t>(painting_area.y) - destination.y;
    if (dx < std::numeric_limits<std::int16_t>::min() ||
        dx > std::numeric_limits<std::int16_t>::max() ||
        dy < std::numeric_limits<std::int16_t>::min() ||
        dy > std::numeric_limits<std::int16_t>::max()) return false;
    result = {static_cast<std::int16_t>(dx), static_cast<std::int16_t>(dy),
              static_cast<std::uint16_t>(painting_area.width),
              static_cast<std::uint16_t>(painting_area.height)};
    return true;
}

BackgroundBoxAreasStatus resolve_background_box_areas(
    const BackgroundBoxAreasRequest& request,
    BackgroundBoxAreas& result) noexcept {
    if (!valid_area_kind(request.origin) || !valid_area_kind(request.clip) ||
        request.border_radius < 0) return BackgroundBoxAreasStatus::InvalidInput;
    if (!uniform_nonnegative(request.border) || !uniform_nonnegative(request.padding))
        return BackgroundBoxAreasStatus::InvalidInput;
    if (request.border_radius > kMaxDimension)
        return BackgroundBoxAreasStatus::DimensionLimit;
    if (request.border_box.width <= 0 || request.border_box.height <= 0)
        return BackgroundBoxAreasStatus::Empty;
    if (request.border_box.width > kMaxDimension || request.border_box.height > kMaxDimension)
        return BackgroundBoxAreasStatus::DimensionLimit;
    if (!valid_area(request.border_box)) return BackgroundBoxAreasStatus::CoordinateOverflow;

    const auto origin_insets = selected_insets(request.origin, request.border, request.padding);
    const auto clip_insets = selected_insets(request.clip, request.border, request.padding);
    if (origin_insets.top > kMaxDimension || clip_insets.top > kMaxDimension)
        return BackgroundBoxAreasStatus::DimensionLimit;

    BackgroundBoxAreas candidate;
    auto status = inset_area(request.border_box, origin_insets, candidate.positioning_area, true);
    if (status != BackgroundBoxAreasStatus::Ready) return status;
    status = inset_area(request.border_box, clip_insets, candidate.painting_area);
    if (status != BackgroundBoxAreasStatus::Ready) return status;
    const auto radius_inset = static_cast<std::int64_t>(clip_insets.top);
    candidate.painting_radius = static_cast<int>(std::max<std::int64_t>(
        0, static_cast<std::int64_t>(request.border_radius) - radius_inset));
    result = candidate;
    return BackgroundBoxAreasStatus::Ready;
}

BackgroundImageGeometryStatus resolve_background_image_geometry(
    const BackgroundImageGeometryRequest& request,
    BackgroundImageGeometry& result) noexcept {
    if (request.painting_area.width == 0 || request.painting_area.height == 0)
        return BackgroundImageGeometryStatus::Empty;
    if (!valid_area(request.positioning_area, true) || !valid_area(request.painting_area))
        return BackgroundImageGeometryStatus::InvalidInput;
    if (request.size_mode != BackgroundImageSizeMode::Intrinsic &&
        request.size_mode != BackgroundImageSizeMode::Cover &&
        request.size_mode != BackgroundImageSizeMode::Contain &&
        request.size_mode != BackgroundImageSizeMode::Stretch)
        return BackgroundImageGeometryStatus::InvalidInput;

    if (request.intrinsic_size_known) {
        if (request.intrinsic_width == 0 || request.intrinsic_height == 0)
            return BackgroundImageGeometryStatus::InvalidIntrinsicSize;
        if (request.intrinsic_width > kMaxDimension ||
            request.intrinsic_height > kMaxDimension)
            return BackgroundImageGeometryStatus::DimensionLimit;
    } else if (request.intrinsic_width != 0 || request.intrinsic_height != 0) {
        return BackgroundImageGeometryStatus::InvalidInput;
    }

    int width = request.positioning_area.width;
    int height = request.positioning_area.height;
    bool dimensions_exact = true;
    if (request.size_mode == BackgroundImageSizeMode::Intrinsic ||
        request.size_mode == BackgroundImageSizeMode::Cover ||
        request.size_mode == BackgroundImageSizeMode::Contain) {
        if (!request.intrinsic_size_known)
            return BackgroundImageGeometryStatus::MissingIntrinsicSize;
        if (request.size_mode == BackgroundImageSizeMode::Intrinsic) {
            width = request.intrinsic_width;
            height = request.intrinsic_height;
        } else if (width == 0 || height == 0) {
            return BackgroundImageGeometryStatus::Empty;
        } else if (!scaled_dimensions(request.positioning_area, request, width, height)) {
            return BackgroundImageGeometryStatus::DimensionLimit;
        } else {
            const auto iw = static_cast<std::int64_t>(request.intrinsic_width);
            const auto ih = static_cast<std::int64_t>(request.intrinsic_height);
            dimensions_exact = static_cast<std::int64_t>(width) * ih ==
                static_cast<std::int64_t>(height) * iw;
        }
    }
    if (width == 0 || height == 0) return BackgroundImageGeometryStatus::Empty;

    int x = 0;
    int y = 0;
    if (!positioned_origin(request.positioning_area.x, request.positioning_area.width - width,
                           request.position_x_percent_q1000, x) ||
        !positioned_origin(request.positioning_area.y, request.positioning_area.height - height,
                           request.position_y_percent_q1000, y))
        return BackgroundImageGeometryStatus::CoordinateOverflow;

    result = {{x, y, width, height}, request.painting_area, dimensions_exact};
    return BackgroundImageGeometryStatus::Ready;
}

} // namespace jellyframe::detail
