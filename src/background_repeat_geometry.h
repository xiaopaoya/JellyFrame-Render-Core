#pragma once

#include "background_image_geometry.h"

#include <array>
#include <cstddef>

namespace jellyframe::detail {

enum class BackgroundAxisRepeat : std::uint8_t { NoRepeat, Repeat, Space, Round };
enum class BackgroundRepeatStatus : std::uint8_t {
    Ready, Empty, InvalidGeometry, FractionalGeometry, UnsupportedCombination,
    TileLimit, CoordinateOverflow
};
inline constexpr std::size_t kMaxBackgroundImageTiles = 64;

struct BackgroundRepeatRequest {
    BackgroundImageGeometryRequest image;
    BackgroundAxisRepeat repeat_x = BackgroundAxisRepeat::Repeat;
    BackgroundAxisRepeat repeat_y = BackgroundAxisRepeat::Repeat;
    std::size_t max_tiles = kMaxBackgroundImageTiles;
};
struct BackgroundRepeatPlan {
    std::array<Rect, kMaxBackgroundImageTiles> tiles{};
    std::size_t count = 0;
};

// Allocation-free integer command planner. Computes visible indices directly;
// no offscreen walk. All output is atomic, including Empty and refusal.
// Fractional periods/gaps/active positions refuse rather than accumulate drift.
// Round admits integral Stretch or Intrinsic sizing; one intrinsic round axis
// restores the natural ratio on the other auto axis. Cover/Contain + Round is
// unqualified and explicitly refused. Zero round positioning extent is Empty.
BackgroundRepeatStatus plan_background_image_tiles(
    const BackgroundRepeatRequest& request, BackgroundRepeatPlan& result) noexcept;

} // namespace jellyframe::detail
