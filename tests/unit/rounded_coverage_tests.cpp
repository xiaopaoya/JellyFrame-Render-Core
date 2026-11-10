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
} // namespace

int main() {
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
    constexpr int huge_radius = 1000000000;
    const auto huge = prepare_rounded_rect({-huge_radius, -huge_radius, 2 * huge_radius, 2 * huge_radius}, huge_radius);
    check(rounded_rect_coverage(huge, -huge_radius, -huge_radius) == 0, "huge corner is outside circle");
    check(rounded_rect_coverage(huge, -1, -huge_radius) == 255, "huge tangent is inside circle");
    std::cout << "{\"radii\":[1,64],\"corner_pixels\":" << pixel_count
              << ",\"max_area_error\":" << maximum_error
              << ",\"sum_absolute_area_error\":" << absolute_error
              << ",\"sum_signed_area_error\":" << signed_error << "}\n";
}
