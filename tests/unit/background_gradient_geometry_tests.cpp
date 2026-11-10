#include "../../src/background_gradient_geometry.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>

static std::size_t allocations = 0;
void* operator new(std::size_t size) {
    if (auto* value = std::malloc(size ? size : 1)) { ++allocations; return value; }
    throw std::bad_alloc();
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }

using namespace jellyframe;
using namespace jellyframe::detail;
namespace {
void check(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
bool rect_is(Rect value, int x, int y, int width, int height) {
    return value.x == x && value.y == y && value.width == width && value.height == height;
}
void untouched(const BackgroundGradientGeometry& value) {
    check(value.sampling_tiles.count == 7 &&
          rect_is(value.sampling_tiles.tiles[0],1,2,3,4) &&
          rect_is(value.painting_area,5,6,7,8) && value.painting_radius == 9,
          "non-Ready result must preserve output");
}
BackgroundGradientGeometry sentinel() {
    BackgroundGradientGeometry value;
    value.sampling_tiles.count = 7;
    value.sampling_tiles.tiles[0] = {1,2,3,4};
    value.painting_area = {5,6,7,8};
    value.painting_radius = 9;
    return value;
}
void independent_cases() {
    // Deliberately simple bounded enumeration reference: no division-based
    // visible-index formula and no calls to either production box/planner.
    std::uint32_t seed = 0x14725U;
    const auto next = [&seed]() { seed = seed*1664525U+1013904223U; return seed; };
    for (unsigned trial=0; trial<3000; ++trial) {
        BackgroundGradientGeometryRequest request;
        const int bx = static_cast<int>(next()%401)-200;
        const int by = static_cast<int>(next()%401)-200;
        const int width = 12+static_cast<int>(next()%30);
        const int height = 12+static_cast<int>(next()%30);
        const int border = static_cast<int>(next()%3);
        const int padding = static_cast<int>(next()%3);
        request.boxes.border_box = {bx,by,width,height};
        request.boxes.border = {border,border,border,border};
        request.boxes.padding = {padding,padding,padding,padding};
        request.boxes.border_radius = static_cast<int>(next()%20);
        request.boxes.origin = static_cast<BackgroundBoxArea>(next()%3);
        request.boxes.clip = static_cast<BackgroundBoxArea>(next()%3);
        request.repeat_x = static_cast<BackgroundAxisRepeat>(next()%4);
        request.repeat_y = static_cast<BackgroundAxisRepeat>(next()%4);
        request.size_mode = static_cast<BackgroundGradientSizeMode>(next()%4);
        request.position_x_percent_q1000 = static_cast<std::int32_t>(next()%400001)-200000;
        request.position_y_percent_q1000 = static_cast<std::int32_t>(next()%400001)-200000;
        const auto inset_for = [&](BackgroundBoxArea kind) {
            return kind == BackgroundBoxArea::BorderBox ? 0 :
                kind == BackgroundBoxArea::PaddingBox ? border : border+padding;
        };
        const int origin_inset = inset_for(request.boxes.origin);
        const int clip_inset = inset_for(request.boxes.clip);
        const int tile_width = width-2*origin_inset;
        const int tile_height = height-2*origin_inset;
        const int clip_width = width-2*clip_inset;
        const int clip_height = height-2*clip_inset;
        Rect expected[64];
        std::size_t count = 0;
        for (int iy=-16; iy<=16; ++iy) for (int ix=-16; ix<=16; ++ix) {
            if ((request.repeat_x == BackgroundAxisRepeat::NoRepeat || request.repeat_x == BackgroundAxisRepeat::Space) && ix)
                continue;
            if ((request.repeat_y == BackgroundAxisRepeat::NoRepeat || request.repeat_y == BackgroundAxisRepeat::Space) && iy)
                continue;
            const int left = bx+origin_inset+ix*tile_width;
            const int top = by+origin_inset+iy*tile_height;
            if (left >= bx+clip_inset+clip_width || left+tile_width <= bx+clip_inset ||
                top >= by+clip_inset+clip_height || top+tile_height <= by+clip_inset) continue;
            check(count<64,"reference fixture must fit shared tile cap");
            expected[count++] = {left,top,tile_width,tile_height};
        }
        BackgroundGradientGeometry actual;
        check(resolve_background_gradient_geometry(request,actual) == BackgroundGradientGeometryStatus::Ready &&
              actual.sampling_tiles.count == count, "seeded reference tile count agrees");
        check(rect_is(actual.painting_area,bx+clip_inset,by+clip_inset,clip_width,clip_height) &&
              actual.painting_radius == std::max(0,request.boxes.border_radius-clip_inset),
              "seeded reference painting area and radius agree");
        for (std::size_t index=0; index<count; ++index) {
            const auto tile = expected[index];
            check(rect_is(actual.sampling_tiles.tiles[index],tile.x,tile.y,tile.width,tile.height),
                  "seeded bounded enumeration agrees with every full sampling tile");
        }
    }
}
}
int main() {
    BackgroundGradientGeometryRequest request;
    request.boxes.border_box = {0,0,100,80};
    request.boxes.border = {10,10,10,10};
    request.boxes.padding = {5,5,5,5};
    request.boxes.border_radius = 18;
    BackgroundGradientGeometry geometry;
    const auto initial_allocations = allocations;
    check(resolve_background_gradient_geometry(request,geometry) == BackgroundGradientGeometryStatus::Ready,
          "default gradient repeat succeeds");
    check(geometry.sampling_tiles.count == 9 && rect_is(geometry.painting_area,0,0,100,80) &&
          geometry.painting_radius == 18, "painting area remains the original rounded border box");
    const int x_positions[] = {-70,10,90};
    const int y_positions[] = {-50,10,70};
    for (int y=0; y<3; ++y) for (int x=0; x<3; ++x)
        check(rect_is(geometry.sampling_tiles.tiles[y*3+x],x_positions[x],y_positions[y],80,60),
              "repeat tiles extend the positioning box through the larger painting box");
    for (const auto mode : {BackgroundGradientSizeMode::Auto, BackgroundGradientSizeMode::Cover,
                            BackgroundGradientSizeMode::Contain, BackgroundGradientSizeMode::Stretch}) {
        request.size_mode = mode;
        request.repeat_x = request.repeat_y = BackgroundAxisRepeat::NoRepeat;
        request.boxes.origin = BackgroundBoxArea::ContentBox;
        request.boxes.clip = BackgroundBoxArea::PaddingBox;
        check(resolve_background_gradient_geometry(request,geometry) == BackgroundGradientGeometryStatus::Ready &&
              geometry.sampling_tiles.count == 1 &&
              rect_is(geometry.sampling_tiles.tiles[0],15,15,70,50) &&
              rect_is(geometry.painting_area,10,10,80,60) && geometry.painting_radius == 8,
              "all keyword sizes use positioning area; clipping remains separate");
    }
    request.boxes.origin = BackgroundBoxArea::BorderBox;
    request.boxes.clip = BackgroundBoxArea::ContentBox;
    request.position_x_percent_q1000 = -50000;
    request.position_y_percent_q1000 = 150000;
    check(resolve_background_gradient_geometry(request,geometry) == BackgroundGradientGeometryStatus::Ready &&
          rect_is(geometry.sampling_tiles.tiles[0],0,0,100,80) &&
          rect_is(geometry.painting_area,15,15,70,50) && geometry.painting_radius == 3,
          "position does not shift a gradient with zero remaining space");
    request.boxes.origin = BackgroundBoxArea::PaddingBox;
    request.boxes.clip = BackgroundBoxArea::BorderBox;
    request.repeat_x = BackgroundAxisRepeat::Space;
    request.repeat_y = BackgroundAxisRepeat::Round;
    check(resolve_background_gradient_geometry(request,geometry) == BackgroundGradientGeometryStatus::Ready &&
          geometry.sampling_tiles.count == 3 &&
          rect_is(geometry.sampling_tiles.tiles[0],10,-50,80,60) &&
          rect_is(geometry.sampling_tiles.tiles[1],10,10,80,60) &&
          rect_is(geometry.sampling_tiles.tiles[2],10,70,80,60),
          "space singleton and round share the bounded planner semantics");
    request.repeat_x = request.repeat_y = BackgroundAxisRepeat::Repeat;
    request.max_tiles = 8;
    geometry = sentinel();
    check(resolve_background_gradient_geometry(request,geometry) == BackgroundGradientGeometryStatus::TileLimit,
          "whole gradient refuses when tile budget is exceeded");
    untouched(geometry);
    request.max_tiles = 9;
    check(resolve_background_gradient_geometry(request,geometry) == BackgroundGradientGeometryStatus::Ready &&
          geometry.sampling_tiles.count == 9, "exact tile budget succeeds");
    request.boxes.border_box.width = 20;
    request.boxes.origin = BackgroundBoxArea::PaddingBox;
    geometry = sentinel();
    check(resolve_background_gradient_geometry(request,geometry) == BackgroundGradientGeometryStatus::Empty,
          "zero positioning dimension gives empty auto-sized gradient");
    untouched(geometry);
    request.boxes.border_box.width = 100;
    request.boxes.padding.left = -1;
    check(resolve_background_gradient_geometry(request,geometry) == BackgroundGradientGeometryStatus::InvalidGeometry,
          "invalid insets refuse");
    untouched(geometry);
    request.boxes.padding.left = 5;
    request.size_mode = static_cast<BackgroundGradientSizeMode>(99);
    check(resolve_background_gradient_geometry(request,geometry) == BackgroundGradientGeometryStatus::InvalidGeometry,
          "invalid size mode refuses");
    untouched(geometry);
    request.size_mode = BackgroundGradientSizeMode::Auto;
    request.boxes.border_box.x = std::numeric_limits<int>::max()-5;
    check(resolve_background_gradient_geometry(request,geometry) == BackgroundGradientGeometryStatus::CoordinateOverflow,
          "overflowing box refuses");
    untouched(geometry);
    request.boxes.border_box = {0,0,8192,80};
    check(resolve_background_gradient_geometry(request,geometry) == BackgroundGradientGeometryStatus::DimensionLimit,
          "dimension bound refuses");
    untouched(geometry);
    request.boxes.border_box = {0,0,100,80};
    request.repeat_x = static_cast<BackgroundAxisRepeat>(99);
    check(resolve_background_gradient_geometry(request,geometry) == BackgroundGradientGeometryStatus::InvalidGeometry,
          "invalid repeat mode refuses");
    untouched(geometry);
    request.repeat_x = BackgroundAxisRepeat::Repeat;
    request.boxes.border_box.x = std::numeric_limits<int>::min();
    check(resolve_background_gradient_geometry(request,geometry) == BackgroundGradientGeometryStatus::CoordinateOverflow,
          "offscreen repeated tile cannot exceed representable coordinates");
    untouched(geometry);
    independent_cases();
    check(allocations == initial_allocations, "geometry and all failure paths allocate nothing");
    std::cout << "gradient geometry foundation tests passed\n";
}
