#include "render_core/geometry.h"

#include <iostream>
#include <type_traits>

using namespace jellyframe;

static_assert(std::is_trivially_copyable<BasicPaint>::value, "Basic payload must support union value copies");
static_assert(std::is_trivially_copyable<TextPaint>::value, "Text payload must support union value copies");
static_assert(std::is_trivially_copyable<ImagePaint>::value, "Image payload must support union value copies");
static_assert(sizeof(BackgroundPaintClip) == 8, "Relative clip remains compact");
static_assert(sizeof(BasicPaint) == 16, "Basic payload size contract");
static_assert(sizeof(TextPaint) == 32, "Text payload size contract");
static_assert(sizeof(ImagePaint) == 24, "Image payload size contract");
#if defined(_WIN64) && defined(_MSC_VER) && !defined(_DEBUG)
static_assert(sizeof(DisplayCommand) == 128, "Windows x64 release command size contract");
#endif

int main() {
    std::cout << "DisplayCommand=" << sizeof(DisplayCommand) << '\n';
}
