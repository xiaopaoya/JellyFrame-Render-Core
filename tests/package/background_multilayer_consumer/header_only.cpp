#include "render_core/style.h"

#include <type_traits>

static_assert(sizeof(jellyframe::Style) == 888, "qualified ABI4 candidate Style size");
static_assert(alignof(jellyframe::Style) == 8, "qualified ABI4 candidate alignment");
static_assert(sizeof(jellyframe::BackgroundLayer) == 28, "lossless inline layer");
static_assert(std::is_trivially_copyable<jellyframe::BackgroundLayer>::value,
              "computed layer has no parser lifetime ownership");

int main() {
    jellyframe::BackgroundLayer layer;
    const auto copy = layer;
    return copy.kind != jellyframe::BackgroundLayerKind::None ||
        copy.image.position_x_percent != 0 || copy.image.position_y_percent != 0;
}
