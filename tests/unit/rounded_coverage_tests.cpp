#include "render_core/raster_primitives.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace jellyframe;

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// Integrate a unit square against a circle independently of the raster grid.
// Horizontal and vertical coordinates measure distance from the circle center.
double circle_square_area(int radius, int horizontal, int vertical) {
    const double radius_squared = static_cast<double>(radius) * radius;
    const auto root = [radius_squared](double coordinate) {
        return std::sqrt(std::max(0.0, radius_squared - coordinate * coordinate));
    };
    const auto clamp_to_square = [horizontal](double coordinate) {
        return std::clamp(coordinate, static_cast<double>(horizontal),
                          static_cast<double>(horizontal + 1));
    };
    const auto primitive = [radius_squared, radius](double coordinate) {
        return 0.5 * (coordinate * std::sqrt(std::max(0.0, radius_squared - coordinate * coordinate)) +
                      radius_squared * std::asin(coordinate / radius));
    };
    const double begin = clamp_to_square(root(vertical + 1));
    const double end = clamp_to_square(root(vertical));
    return begin - horizontal + primitive(end) - primitive(begin) - vertical * (end - begin);
}

void check_translation(const RasterRoundedRect& geometry, Rect rect, int radius) {
    for (int origin : {std::numeric_limits<int>::min(), std::numeric_limits<int>::max() - 200}) {
        const auto translated = prepare_rounded_rect({origin, origin, rect.width, rect.height}, radius);
        for (int y = 0; y < radius; ++y) {
            for (int x = 0; x < radius; ++x) {
                check(rounded_rect_coverage(geometry, rect.x + x, rect.y + y) ==
                          rounded_rect_coverage(translated, origin + x, origin + y),
                      "translation near integer limits preserves coverage");
            }
        }
    }
}

void elliptical_corner_coverage_uses_both_axes() {
    const Rect rect{0, 0, 20, 10};
    const int x_radius = encode_corner_radii(CornerRadii{8, 0, 0, 0});
    const int y_radius = encode_corner_radii(CornerRadii{2, 0, 0, 0});
    const RasterRoundedRect ellipse = prepare_rounded_rect(rect, x_radius, y_radius);
    check(ellipse.radii.top_left == 8 && ellipse.vertical_radii.top_left == 2,
          "elliptical raster geometry retains horizontal and vertical radii");
    check(effective_vertical_radius(x_radius, -1) == x_radius,
          "only the omitted-axis sentinel inherits horizontal radii");
    check(effective_vertical_radius(x_radius, y_radius) == y_radius,
          "a negative packed vertical axis is an actual corner value");
    // Independent 4x4 equation for this one-corner ellipse. In particular,
    // (0,0) is uncovered for both shapes and cannot distinguish the axes.
    for (int py = 0; py < rect.height; ++py) {
        for (int px = 0; px < rect.width; ++px) {
            int count = 16;
            if (px < 8 && py < 2) {
                count = 0;
                for (int sy = 0; sy < 4; ++sy) for (int sx = 0; sx < 4; ++sx) {
                    const double nx = (px + (sx + 0.5) / 4 - 8) / 8;
                    const double ny = (py + (sy + 0.5) / 4 - 2) / 2;
                    if (nx * nx + ny * ny <= 1) ++count;
                }
            }
            check(rounded_rect_coverage(ellipse, px, py) == (count * 255 + 8) / 16,
                  "every ellipse pixel agrees with independent area samples");
        }
    }
    check(rounded_rect_coverage(ellipse, 9, 2) == 255,
          "ellipse retains full coverage in the flat interior");
    const auto common = prepare_rounded_rect({0, 0, 100, 100}, 200, 20);
    check(common.radii.top_left == 50 && common.vertical_radii.top_left == 5,
          "overlap scaling precedes any per-axis box clamp");
    const int single = encode_corner_radii(CornerRadii{80, 0, 0, 0});
    const auto asymmetric = prepare_rounded_rect({0, 0, 100, 100}, single, single);
    check(asymmetric.radii.top_left == 80 && asymmetric.vertical_radii.top_left == 80,
          "equal axes do not clamp independent corners to half the box");
    const auto odd = prepare_rounded_rect({0, 0, 81, 41}, 41, 21);
    check(odd.radii.top_left + odd.radii.top_right <= 81 &&
          odd.vertical_radii.top_left + odd.vertical_radii.bottom_left <= 41,
          "integer overlap projection never reintroduces overlapping edges");
    check(expand_corner_radii(150, -1) == 149 && expand_corner_radii(150, 2) == 152,
          "uniform large axes remain lossless through inset and outline expansion");
}
} // namespace

int run_coverage_tests() {
    double maximum_error = 0;
    double absolute_error = 0;
    double signed_error = 0;
    int pixel_count = 0;
    for (int radius = 1; radius <= 64; ++radius) {
        const Rect rect{7, -11, 2 * radius + 5, 2 * radius + 9};
        const auto geometry = prepare_rounded_rect(rect, radius);
        double reference_sum = 0;
        for (int y = 0; y < radius; ++y) {
            for (int x = 0; x < radius; ++x) {
                const int coverage = rounded_rect_coverage(geometry, rect.x + x, rect.y + y);
                check(coverage == rounded_rect_coverage(geometry, rect.x + rect.width - 1 - x, rect.y + y),
                      "horizontal reflection preserves coverage");
                check(coverage == rounded_rect_coverage(geometry, rect.x + x, rect.y + rect.height - 1 - y),
                      "vertical reflection preserves coverage");
                check(coverage == rounded_rect_coverage(geometry, rect.x + y, rect.y + x),
                      "axis interchange preserves coverage");
                const double reference = circle_square_area(radius, radius - 1 - x, radius - 1 - y);
                reference_sum += reference;
                const double error = coverage / 255.0 - reference;
                maximum_error = std::max(maximum_error, std::abs(error));
                absolute_error += std::abs(error);
                signed_error += error;
                ++pixel_count;
                if (radius == 6 || radius == 10) {
                    check(std::abs(error) <= 0.06, "fixture coverage stays within measured area bound");
                }
                check(std::abs(error) <= 0.125, "four-by-four area error exceeds one eighth pixel");
            }
        }
        const double quarter_circle_area = std::acos(-1.0) * radius * radius / 4;
        check(std::abs(reference_sum - quarter_circle_area) < 1e-8, "oracle integrates to quarter-circle area");
        check_translation(geometry, rect, radius);
        check(rounded_rect_coverage(geometry, rect.x - 1, rect.y) == 0, "outside rounded box is uncovered");
        check(rounded_rect_coverage(geometry, rect.x + radius, rect.y + radius) == 255, "flat interior is covered");
    }
    elliptical_corner_coverage_uses_both_axes();
    constexpr int huge_radius = 1000000000;
    const auto huge = prepare_rounded_rect({-huge_radius, -huge_radius, 2 * huge_radius, 2 * huge_radius}, huge_radius);
    check(rounded_rect_coverage(huge, -huge_radius, -huge_radius) == 0, "huge corner is outside circle");
    check(rounded_rect_coverage(huge, -1, -huge_radius) == 255, "huge tangent is inside circle");
    std::cout << "{\"radii\":[1,64],\"corner_pixels\":" << pixel_count
              << ",\"max_area_error\":" << maximum_error
              << ",\"sum_absolute_area_error\":" << absolute_error
              << ",\"sum_signed_area_error\":" << signed_error << "}\n";
    return 0;
}

int main() {
    try {
        return run_coverage_tests();
    } catch (const std::exception& error) {
        std::cerr << "Rounded coverage test failed: " << error.what() << '\n';
        return 1;
    }
}
