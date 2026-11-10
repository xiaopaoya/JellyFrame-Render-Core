#include "../../src/background_repeat_geometry.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace jellyframe;
namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool same(Rect left, Rect right) {
    return left.x == right.x && left.y == right.y && left.width == right.width && left.height == right.height;
}
void planner_cases() {
    using namespace jellyframe::detail;
    BackgroundRepeatRequest request;
    request.image.positioning_area = {8, 8, 72, 48};
    request.image.painting_area = {0, 0, 88, 64};
    request.image.intrinsic_size_known = true;
    request.image.intrinsic_width = 20;
    request.image.intrinsic_height = 12;
    request.image.position_x_percent_q1000 = 0;
    request.image.position_y_percent_q1000 = 0;
    BackgroundRepeatPlan plan;
    check(plan_background_image_tiles(request, plan) == BackgroundRepeatStatus::Ready && plan.count == 30,
          "repeat directly enumerates only visible indices through all four clip edges");
    check(same(plan.tiles.front(), {-12, -4, 20, 12}) && same(plan.tiles[29], {68, 56, 20, 12}),
          "repeat preserves negative-phase tile destinations and full sampling extents");
    request.repeat_x = BackgroundAxisRepeat::Space;
    request.repeat_y = BackgroundAxisRepeat::NoRepeat;
    check(plan_background_image_tiles(request, plan) == BackgroundRepeatStatus::Ready && plan.count == 5,
          "space pattern extends beyond positioning area into painting area");
    const int spaced_x[] = {-18, 8, 34, 60, 86};
    for (std::size_t i = 0; i < 5; ++i) check(plan.tiles[i].x == spaced_x[i], "space uses edge anchored gap");
    request.image.positioning_area.width = 71;
    const auto previous = plan;
    check(plan_background_image_tiles(request, plan) == BackgroundRepeatStatus::FractionalGeometry &&
          plan.count == previous.count && same(plan.tiles[0], previous.tiles[0]),
          "fractional gap refuses atomically instead of rounding step");
    request.image.positioning_area.width = 10;
    request.image.position_x_percent_q1000 = 100000;
    check(plan_background_image_tiles(request, plan) == BackgroundRepeatStatus::Ready && plan.count == 1 &&
          plan.tiles[0].x == -2, "space singleton uses authored position");
    request.image.positioning_area.width = 0;
    check(plan_background_image_tiles(request, plan) == BackgroundRepeatStatus::Ready && plan.count == 1 &&
          plan.tiles[0].x == -12, "zero positioning axis permits positive intrinsic singleton");
    request.repeat_x = BackgroundAxisRepeat::Repeat;
    check(plan_background_image_tiles(request, plan) == BackgroundRepeatStatus::Ready && plan.count == 5,
          "zero positioning axis permits bounded repeat under positive painting area");
    request.repeat_x = BackgroundAxisRepeat::Round;
    check(plan_background_image_tiles(request, plan) == BackgroundRepeatStatus::Empty,
          "zero round positioning extent is explicitly empty");
    request.image.positioning_area.width = 72;
    request.image.intrinsic_height = 10;
    request.image.position_x_percent_q1000 = 0;
    check(plan_background_image_tiles(request, plan) == BackgroundRepeatStatus::Ready && plan.tiles[0].width == 18 &&
          plan.tiles[0].height == 9, "integral round restores opposite intrinsic auto axis ratio");
    request.repeat_y = BackgroundAxisRepeat::Round;
    check(plan_background_image_tiles(request, plan) == BackgroundRepeatStatus::FractionalGeometry,
          "fractional round axis refuses without integer sampling equivalence");
    request.image.positioning_area.height = 50;
    check(plan_background_image_tiles(request, plan) == BackgroundRepeatStatus::Ready &&
          plan.tiles[0].width == 18 && plan.tiles[0].height == 10,
          "both round axes resize independently without auto ratio correction");
    request.repeat_x = request.repeat_y = BackgroundAxisRepeat::Repeat;
    request.image.positioning_area = {0, 0, 64, 64};
    request.image.painting_area = request.image.positioning_area;
    request.image.intrinsic_width = request.image.intrinsic_height = 8;
    check(plan_background_image_tiles(request, plan) == BackgroundRepeatStatus::Ready && plan.count == 64,
          "exact 64-tile boundary is admitted");
    request.max_tiles = 63;
    check(plan_background_image_tiles(request, plan) == BackgroundRepeatStatus::TileLimit && plan.count == 64,
          "one-command-short budget refuses complete layer atomically");
    request.max_tiles = std::numeric_limits<std::size_t>::max();
    request.image.intrinsic_width = request.image.intrinsic_height = 1;
    request.image.painting_area = {0, 0, 8191, 8191};
    check(plan_background_image_tiles(request, plan) == BackgroundRepeatStatus::TileLimit,
          "hard cap cannot be overridden and bounds dense work before enumeration");
    request.image.painting_area = {0, 0, 64, 64};
    request.image.position_x_percent_q1000 = 50000;
    check(plan_background_image_tiles(request, plan) == BackgroundRepeatStatus::FractionalGeometry,
          "half pixel repeat phase is refused");
    request.repeat_x = static_cast<BackgroundAxisRepeat>(99);
    check(plan_background_image_tiles(request, plan) == BackgroundRepeatStatus::InvalidGeometry,
          "malformed repeat enum refuses");

    // Independent exhaustive occupancy oracle: enumerate a deliberately larger
    // fixed index range, rather than reproducing the planner's index arithmetic.
    std::size_t queries = 0;
    for (int extent = 0; extent <= 20; ++extent) for (int tile = 1; tile <= 8; ++tile)
        for (int shift = -3; shift <= 3; ++shift) for (int mode = 0; mode <= 3; ++mode) {
            request = {};
            request.image.positioning_area = {shift, 0, extent, 1};
            request.image.painting_area = {-5, 0, 30, 1};
            request.image.intrinsic_size_known = true;
            request.image.intrinsic_width = static_cast<std::uint16_t>(tile);
            request.image.intrinsic_height = 1;
            request.image.position_x_percent_q1000 = request.image.position_y_percent_q1000 = 0;
            request.repeat_x = static_cast<BackgroundAxisRepeat>(mode);
            request.repeat_y = BackgroundAxisRepeat::NoRepeat;
            const auto status = plan_background_image_tiles(request, plan);
            if (status != BackgroundRepeatStatus::Ready) continue;
            for (int pixel = -5; pixel < 25; ++pixel) {
                bool observed = false;
                for (std::size_t i = 0; i < plan.count; ++i)
                    observed |= pixel >= plan.tiles[i].x && pixel < plan.tiles[i].x + plan.tiles[i].width;
                bool expected = false;
                int size = tile, step = tile;
                bool periodic = mode == 1 || mode == 3;
                if (mode == 2 && extent / tile >= 2) {
                    step = (extent - tile) / (extent / tile - 1);
                    periodic = true;
                }
                if (mode == 3) {
                    const int count = std::max(1, (2 * extent + tile) / (2 * tile));
                    size = step = extent / count;
                }
                for (int i = periodic ? -64 : 0; i <= (periodic ? 64 : 0); ++i)
                    expected |= pixel >= shift + i * step && pixel < shift + i * step + size;
                check(observed == expected, "integer planner matches independent brute occupancy");
            }
            ++queries;
        }
    check(queries > 3000, "independent occupancy corpus is nonempty and broad across integer boundaries");
    std::cout << "integer occupancy queries " << queries << '\n';
}
} // namespace
int main() {
    try {
        planner_cases();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
