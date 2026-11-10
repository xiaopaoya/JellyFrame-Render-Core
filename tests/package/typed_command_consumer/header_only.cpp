#include "render_core/geometry.h"

#include <iostream>
#include <type_traits>

using namespace jellyframe;

static_assert(std::is_trivially_copyable<BasicPaint>::value, "Basic payload must support union value copies");
static_assert(std::is_trivially_copyable<TextPaint>::value, "Text payload must support union value copies");
static_assert(std::is_trivially_copyable<ImagePaint>::value, "Image payload must support union value copies");
static_assert(sizeof(BackgroundPaintClip) == 8, "Relative clip remains compact");
// Explicit vertical axis adds one int to every active payload in ABI4.
static_assert(sizeof(BasicPaint) == 28, "Basic payload size contract with radius axes");
static_assert(sizeof(TextPaint) == 36, "Text payload size contract with radius axes");
static_assert(sizeof(ImagePaint) == 36, "Image payload size contract with radius axes");
#if defined(_WIN64) && defined(_MSC_VER) && !defined(_DEBUG)
static_assert(sizeof(DisplayCommand) == 136, "Windows x64 release command size contract with radius axes");
#endif

int main() {
    std::cout << "DisplayCommand=" << sizeof(DisplayCommand) << '\n';
}
