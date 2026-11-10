#include "render_core/modern_paint.h"
#include "render_core/raster_primitives.h"

#include <cassert>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace jellyframe;

namespace {
void elliptical_gradient_rows_match_fill_pixels() {
    const int axes[][2] = {
        {encode_corner_radii({4, 8, 6, 3}), encode_corner_radii({32, 25, 28, 30})},
        {encode_corner_radii({32, 25, 28, 30}), encode_corner_radii({4, 8, 6, 3})},
        {encode_corner_radii({80, 0, 0, 0}), encode_corner_radii({80, 0, 0, 0})},
        {encode_corner_radii({80, 0, 80, 0}), encode_corner_radii({80, 0, 80, 0})},
    };
    const Rect rect{4, 4, 100, 100};
    const Rect clips[] = {{0, 0, 112, 112}, {4, 12, 25, 24}};
    for (const auto& axis : axes) {
        for (const Rect clip : clips) {
            for (const int alpha : {180, 255}) {
                const Color color{23, 103, 203, static_cast<std::uint8_t>(alpha)};
                FrameBuffer expected(112, 112, {225, 232, 240, 255});
                DisplayCommand fill(DisplayCommandType::FillRect);
                fill.rect = rect;
                fill.color = color;
                fill.basic_paint().border_radius = axis[0];
                fill.basic_paint().border_radius_y = axis[1];
                SoftwareRasterizer().rasterize(fill, expected, clip);
                const GradientAxis linear_axes[] = {GradientAxis::Vertical, GradientAxis::Horizontal,
                    GradientAxis::DiagonalDownRight, GradientAxis::DiagonalDownLeft};
                for (int kind = 0; kind < 6; ++kind) {
                    FrameBuffer actual(112, 112, {225, 232, 240, 255});
                    if (kind < 4) {
                        modern_paint_fill_linear_gradient(actual, rect, clip, color, color,
                                                           linear_axes[kind], axis[0], axis[1]);
                    } else if (kind == 4) {
                        modern_paint_fill_conic_gradient_region(actual, rect,
                            raster_clip_rect(actual, rect, clip), color, color, 50, axis[0], axis[1]);
                    } else {
                        modern_paint_fill_radial_gradient_region(actual, rect,
                            raster_clip_rect(actual, rect, clip), color, color,
                            GradientAxis::Vertical, 0, axis[0], axis[1]);
                    }
                    for (std::size_t i = 0; i < actual.pixels.size(); ++i) {
                        const Color a = actual.pixels[i], b = expected.pixels[i];
                        if (a.r != b.r || a.g != b.g || a.b != b.b || a.a != b.a) {
                            throw std::runtime_error("ellipse gradient mismatch kind=" + std::to_string(kind) +
                                " rx=" + std::to_string(axis[0]) + " ry=" + std::to_string(axis[1]) +
                                " alpha=" + std::to_string(alpha) + " x=" + std::to_string(i % 112) +
                                " y=" + std::to_string(i / 112) + " actual=" + std::to_string(a.r) +
                                " expected=" + std::to_string(b.r));
                        }
                    }
                }
            }
        }
    }
}
} // namespace

int modern_paint_tests_main() {
    elliptical_gradient_rows_match_fill_pixels();
    const Color midpoint = modern_paint_lerp_color_255(Color{0, 10, 20, 255},
                                                        Color{100, 110, 120, 255},
                                                        128);
    assert(midpoint.r == 50 && midpoint.g == 60 && midpoint.b == 70);
    assert(modern_paint_conic_percent_from_top_clockwise(0, -10) == 0);
    assert(modern_paint_conic_percent_from_top_clockwise(10, 0) == 25);
    assert(modern_paint_conic_percent_from_top_clockwise(0, 10) == 50);
    assert(modern_paint_conic_percent_from_top_clockwise(-10, 0) == 75);
    assert(modern_paint_euclidean_distance_half_px(0, 10) == 10);
    assert(modern_paint_euclidean_distance_half_px(std::numeric_limits<std::int64_t>::min(),
                                                   std::numeric_limits<std::int64_t>::max()) ==
           std::numeric_limits<int>::max());
    assert(modern_paint_rounded_rect_outside_distance_half_px(Rect{0, 0, 20, 20}, 10, 10, 10) == 0);
    const Rect rounded_rect{4, 6, 20, 14};
    const int rounded_radius = encode_corner_radii(CornerRadii{5, 3, 7, 2});
    const RasterRoundedRect prepared = prepare_rounded_rect(rounded_rect, rounded_radius);
    for (int y = 6; y < 20; ++y) {
        for (int x = 4; x < 24; ++x) {
            assert(rounded_rect_coverage(prepared, x, y) ==
                   rounded_rect_coverage(rounded_rect, rounded_radius, x, y));
        }
    }
    assert(safe_add(std::numeric_limits<int>::max(), 1) == std::numeric_limits<int>::max());
    assert(safe_add(std::numeric_limits<int>::min(), -1) == std::numeric_limits<int>::min());
    assert(safe_negate(std::numeric_limits<int>::min()) == std::numeric_limits<int>::max());
    assert(safe_span(std::numeric_limits<int>::min(), std::numeric_limits<int>::max()) ==
           std::numeric_limits<int>::max());
    assert(expand_corner_radii(127, std::numeric_limits<int>::max()) == std::numeric_limits<int>::max());
    assert(expand_corner_radii(1, std::numeric_limits<int>::min()) == 0);
    const RasterRoundedRect extreme = prepare_rounded_rect(
        Rect{std::numeric_limits<int>::min(), std::numeric_limits<int>::min(),
             std::numeric_limits<int>::max(), std::numeric_limits<int>::max()},
        std::numeric_limits<int>::max());
    const int center = safe_add(extreme.left, extreme.radii.top_left);
    assert(rounded_rect_coverage(extreme, center, center) == 255);
    assert(rounded_rect_coverage(extreme, std::numeric_limits<int>::max(),
                                 std::numeric_limits<int>::max()) >= 0);
    RasterRoundedRect externally_constructed{};
    externally_constructed.left = std::numeric_limits<int>::min();
    externally_constructed.top = std::numeric_limits<int>::min();
    externally_constructed.right = std::numeric_limits<int>::max();
    externally_constructed.bottom = std::numeric_limits<int>::max();
    externally_constructed.radii = CornerRadii{std::numeric_limits<int>::max(),
                                                std::numeric_limits<int>::max(),
                                                std::numeric_limits<int>::max(),
                                                std::numeric_limits<int>::max()};
    externally_constructed.rounded = true;
    assert(rounded_rect_coverage(externally_constructed, 0, 0) >= 0);
    (void)modern_paint_rounded_rect_outside_distance_half_px(
        Rect{std::numeric_limits<int>::min(), std::numeric_limits<int>::min(),
             std::numeric_limits<int>::max(), std::numeric_limits<int>::max()},
        127,
        std::numeric_limits<int>::max() - 1,
        std::numeric_limits<int>::max() - 1);
    std::cout << "modern paint math tests passed\n";
    return 0;
}
