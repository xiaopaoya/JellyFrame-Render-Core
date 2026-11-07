#pragma once
#include <string_view>

namespace jellyframe {
struct Style;
namespace detail {
// Reduced exact integer ratio. Degenerate ratios have no preferred ratio.
// -2/-2 carries explicit inheritance until parent-first tree finalization.
struct CssAspectRatio {
    int width = 0;
    int height = 0;
    bool automatic = true;
};
// Literal-number profile: 4096 source bytes, 63 per numeric token, uint64
// significant coefficients, absolute explicit exponent <=10000, reduced
// nonzero terms <=INT_MAX. Refusal never changes output or saturates a term.
bool parse_css_aspect_ratio(std::string_view, CssAspectRatio&) noexcept;
void finalize_inherited_aspect_ratio(Style&, const Style*) noexcept;
} // namespace detail
} // namespace jellyframe
