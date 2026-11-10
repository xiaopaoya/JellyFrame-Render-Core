#include "../../src/background_image_geometry.h"

#include <iostream>
#include <limits>
#include <stdexcept>

using namespace jellyframe;
using namespace jellyframe::detail;

namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

BackgroundImageGeometryRequest fixture(BackgroundImageSizeMode mode) {
    BackgroundImageGeometryRequest request;
    request.positioning_area = {100, 200, 96, 56};
    request.painting_area = {92, 192, 112, 72};
    request.intrinsic_size_known = true;
    request.intrinsic_width = 48;
    request.intrinsic_height = 24;
    request.size_mode = mode;
    return request;
}

void expect(const BackgroundImageGeometryRequest& request, Rect destination,
            const char* message) {
    BackgroundImageGeometry result;
    check(resolve_background_image_geometry(request, result) ==
              BackgroundImageGeometryStatus::Ready, message);
    check(result.destination.x == destination.x && result.destination.y == destination.y &&
              result.destination.width == destination.width &&
              result.destination.height == destination.height, message);
    check(result.painting_clip.x == request.painting_area.x &&
              result.painting_clip.y == request.painting_area.y &&
              result.painting_clip.width == request.painting_area.width &&
              result.painting_clip.height == request.painting_area.height,
          "painting clip remains independent of positioned image destination");
}

void expect_box_area(const BackgroundBoxAreasRequest& request, BackgroundBoxArea origin,
                     BackgroundBoxArea clip, Rect positioning, Rect painting, int radius) {
    auto configured = request;
    configured.origin = origin;
    configured.clip = clip;
    BackgroundBoxAreas result;
    check(resolve_background_box_areas(configured, result) == BackgroundBoxAreasStatus::Ready,
          "uniform box area combination is accepted");
    check(result.positioning_area.x == positioning.x && result.positioning_area.y == positioning.y &&
              result.positioning_area.width == positioning.width &&
              result.positioning_area.height == positioning.height,
          "origin selects the independent positioning area");
    check(result.painting_area.x == painting.x && result.painting_area.y == painting.y &&
              result.painting_area.width == painting.width &&
              result.painting_area.height == painting.height,
          "clip selects the independent painting area");
    check(result.painting_radius == radius, "clip radius is inset and clamped at zero");
}

void box_area_cases() {
    BackgroundBoxAreasRequest request;
    request.border_box = {20, 30, 112, 72};
    request.border = {8, 8, 8, 8};
    request.padding = {10, 10, 10, 10};
    request.border_radius = 18;
    const Rect areas[] = {{20, 30, 112, 72}, {28, 38, 96, 56}, {38, 48, 76, 36}};
    const int radii[] = {18, 10, 0};
    const BackgroundBoxArea boxes[] = {BackgroundBoxArea::BorderBox,
        BackgroundBoxArea::PaddingBox, BackgroundBoxArea::ContentBox};
    for (unsigned origin = 0; origin < 3; ++origin) {
        for (unsigned clip = 0; clip < 3; ++clip) {
            expect_box_area(request, boxes[origin], boxes[clip], areas[origin], areas[clip],
                            radii[clip]);
        }
    }

    BackgroundBoxAreas unchanged{{1, 2, 3, 4}, {5, 6, 7, 8}, 9};
    auto invalid = request;
    invalid.border.right = 7;
    check(resolve_background_box_areas(invalid, unchanged) == BackgroundBoxAreasStatus::InvalidInput,
          "nonuniform border is rejected");
    check(unchanged.positioning_area.x == 1 && unchanged.painting_area.x == 5 &&
              unchanged.painting_radius == 9,
          "invalid box area does not publish partial output");
    invalid = request;
    invalid.padding.bottom = 9;
    check(resolve_background_box_areas(invalid, unchanged) == BackgroundBoxAreasStatus::InvalidInput,
          "nonuniform padding is rejected");
    invalid = request;
    invalid.border.left = -1;
    check(resolve_background_box_areas(invalid, unchanged) == BackgroundBoxAreasStatus::InvalidInput,
          "negative border inset is rejected");
    invalid = request;
    invalid.border_radius = -1;
    check(resolve_background_box_areas(invalid, unchanged) == BackgroundBoxAreasStatus::InvalidInput,
          "negative radius is rejected");
    invalid = request;
    invalid.origin = static_cast<BackgroundBoxArea>(99);
    check(resolve_background_box_areas(invalid, unchanged) == BackgroundBoxAreasStatus::InvalidInput,
          "unknown origin enum is rejected");
    invalid = request;
    invalid.border_box.x = std::numeric_limits<int>::max() - 32;
    check(resolve_background_box_areas(invalid, unchanged) ==
              BackgroundBoxAreasStatus::CoordinateOverflow,
          "border-box edge overflow is rejected");
    invalid = request;
    invalid.border = {8191, 8191, 8191, 8191};
    check(resolve_background_box_areas(invalid, unchanged) == BackgroundBoxAreasStatus::Empty,
          "insets collapsing the selected area return empty atomically");

    invalid = {};
    invalid.border_box = {0, 0, 16, 16};
    invalid.border = {8, 8, 8, 8};
    invalid.origin = BackgroundBoxArea::PaddingBox;
    invalid.clip = BackgroundBoxArea::BorderBox;
    check(resolve_background_box_areas(invalid, unchanged) == BackgroundBoxAreasStatus::Ready &&
          unchanged.positioning_area.width == 0 && unchanged.positioning_area.height == 0 &&
          unchanged.painting_area.width == 16,
          "zero positioning area remains distinct from positive painting area");
}

void elliptical_box_area_insets_preserve_packed_axes() {
    BackgroundBoxAreasRequest request;
    request.border_box = {0, 0, 100, 100};
    request.border = {5, 5, 5, 5};
    request.clip = BackgroundBoxArea::PaddingBox;
    request.border_radius = encode_corner_radii({20, 10, 30, 15});
    request.border_radius_y = encode_corner_radii({10, 30, 15, 20});
    BackgroundBoxAreas result;
    check(resolve_background_box_areas(request, result) == BackgroundBoxAreasStatus::Ready,
          "canonical packed corner axes are valid background geometry");
    check(result.painting_radius == encode_corner_radii({15, 5, 25, 10}) &&
          result.painting_radius_y == encode_corner_radii({5, 25, 10, 15}),
          "each packed corner axis is decoded before its clip inset is removed");
    request.border = {20, 20, 20, 20};
    request.border_radius = 10;
    request.border_radius_y = 30;
    check(resolve_background_box_areas(request, result) == BackgroundBoxAreasStatus::Ready &&
          result.painting_radius == 0 && result.painting_radius_y == 10,
          "zero horizontal inset radius retains its independently reduced vertical axis");
    int x = 0, y = 0;
    check(inset_background_radii(30, 20, {2, 12, 6, 4}, x, y) &&
          x == encode_corner_radii({26, 18, 18, 26}) &&
          y == encode_corner_radii({18, 18, 14, 14}),
          "asymmetric physical sides reduce their corresponding corner axis only");
    request.border_radius_y = 8192;
    check(resolve_background_box_areas(request, result) == BackgroundBoxAreasStatus::DimensionLimit,
          "vertical radius shares the bounded background dimension limit");
}

void compact_paint_clip_checks_relative_offset_range() {
    BackgroundPaintClip clip{7, 8, 9, 10};
    check(make_background_paint_clip({100, 200, 20, 20}, {92, 206, 16, 12}, clip),
          "painting area encodes as a relative compact clip");
    check(clip.dx == -8 && clip.dy == 6 && clip.width == 16 && clip.height == 12,
          "compact clip preserves offset and extent");
    const BackgroundPaintClip previous = clip;
    check(!make_background_paint_clip({40000, 0, 10, 10}, {0, 0, 10, 10}, clip),
          "relative offset outside int16 is refused");
    check(clip.dx == previous.dx && clip.dy == previous.dy && clip.width == previous.width &&
              clip.height == previous.height,
          "unrepresentable clip leaves output unchanged");
    check(!make_background_paint_clip({0, 0, 10, 10}, {0, 0, 0, 10}, clip),
          "empty clip is refused");
}
} // namespace

int main() {
    try {
        box_area_cases();
        elliptical_box_area_insets_preserve_packed_axes();
        compact_paint_clip_checks_relative_offset_range();
        auto request = fixture(BackgroundImageSizeMode::Intrinsic);
        expect(request, {124, 216, 48, 24}, "intrinsic auto matches wide Blink oracle dimensions");

        request = fixture(BackgroundImageSizeMode::Cover);
        expect(request, {92, 200, 112, 56}, "cover preserves ratio and may overflow origin area");

        request = fixture(BackgroundImageSizeMode::Contain);
        expect(request, {100, 204, 96, 48}, "contain preserves ratio within origin area");

        request = fixture(BackgroundImageSizeMode::Intrinsic);
        request.position_x_percent_q1000 = 0;
        request.position_y_percent_q1000 = 0;
        expect(request, {100, 200, 48, 24}, "start position aligns to positioning area");
        request.position_x_percent_q1000 = 100000;
        request.position_y_percent_q1000 = 100000;
        expect(request, {148, 232, 48, 24}, "end position aligns to positioning area");

        request = fixture(BackgroundImageSizeMode::Intrinsic);
        request.intrinsic_width = 24;
        request.intrinsic_height = 48;
        expect(request, {136, 204, 24, 48}, "tall intrinsic image is centered with overflow");
        request.size_mode = BackgroundImageSizeMode::Cover;
        expect(request, {100, 132, 96, 192}, "cover uses tall source ratio and clips separately");
        request.size_mode = BackgroundImageSizeMode::Contain;
        expect(request, {134, 200, 28, 56}, "contain uses tall source ratio");

        request = fixture(BackgroundImageSizeMode::Stretch);
        request.intrinsic_size_known = false;
        request.intrinsic_width = request.intrinsic_height = 0;
        expect(request, {100, 200, 96, 56}, "stretch is determinate without natural dimensions");
        request.size_mode = BackgroundImageSizeMode::Cover;
        BackgroundImageGeometry unchanged{{7, 8, 9, 10}, {11, 12, 13, 14}};
        check(resolve_background_image_geometry(request, unchanged) ==
                  BackgroundImageGeometryStatus::MissingIntrinsicSize,
              "unknown dimensions refuse ratio-dependent sizing");
        check(unchanged.destination.x == 7 && unchanged.painting_clip.x == 11,
              "refusal does not publish partial geometry");

        request = fixture(BackgroundImageSizeMode::Intrinsic);
        request.intrinsic_width = 0;
        check(resolve_background_image_geometry(request, unchanged) ==
                  BackgroundImageGeometryStatus::InvalidIntrinsicSize,
              "zero natural dimension is invalid metadata");
        request.intrinsic_width = 8192;
        check(resolve_background_image_geometry(request, unchanged) ==
                  BackgroundImageGeometryStatus::DimensionLimit,
              "oversized natural dimension is refused");
        request.intrinsic_size_known = false;
        request.intrinsic_width = 10;
        check(resolve_background_image_geometry(request, unchanged) ==
                  BackgroundImageGeometryStatus::InvalidInput,
              "unknown dimensions must use canonical zero fields");

        request = fixture(BackgroundImageSizeMode::Intrinsic);
        request.intrinsic_width = 8191;
        request.intrinsic_height = 1;
        request.positioning_area = {0, 0, 8191, 8191};
        request.size_mode = BackgroundImageSizeMode::Cover;
        check(resolve_background_image_geometry(request, unchanged) ==
                  BackgroundImageGeometryStatus::DimensionLimit,
              "output dimension above limit is refused");
        request.positioning_area.width = 0;
        check(resolve_background_image_geometry(request, unchanged) ==
                  BackgroundImageGeometryStatus::Empty,
              "empty positioning area is a no-op");

        request = fixture(BackgroundImageSizeMode::Stretch);
        request.positioning_area = {std::numeric_limits<int>::max() - 8, 0, 16, 20};
        check(resolve_background_image_geometry(request, unchanged) ==
                  BackgroundImageGeometryStatus::InvalidInput,
              "area edge overflow is rejected");
        request = fixture(BackgroundImageSizeMode::Intrinsic);
        request.positioning_area = {std::numeric_limits<int>::max() - 8191, 0, 8191, 56};
        request.position_x_percent_q1000 = std::numeric_limits<std::int32_t>::max();
        check(resolve_background_image_geometry(request, unchanged) ==
                  BackgroundImageGeometryStatus::CoordinateOverflow,
              "position coordinate overflow is rejected");

        request = fixture(BackgroundImageSizeMode::Intrinsic);
        request.positioning_area.width = 2;
        request.positioning_area.height = 1;
        request.intrinsic_width = 1;
        request.intrinsic_height = 2;
        request.size_mode = BackgroundImageSizeMode::Contain;
        expect(request, {101, 200, 1, 1}, "positive fractional scale rounds to nearest pixel");
        std::cout << "background image geometry cases passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
