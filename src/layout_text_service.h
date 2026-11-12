#pragma once

#include "render_core/prepared_text.h"
#include "render_core/style.h"

namespace jellyframe::detail {

// Internal, owned request values. No pointers into Style or this object's old
// address survive copies/moves. This is not another host-provider interface.
struct LayoutTextFont {
    std::uint32_t family = 0;
    std::int32_t size = 0, letters = 0, words = 0;
    std::uint16_t weight = 400;
    TextFaceStyle style = TextFaceStyle::Normal;
    TextFontRequest request() const noexcept;
};

// CSS inputs are checked without passing through the old integer-font callback
// boundary. The current service admits the upright zero-spacing profile.
// Refusal preserves output. CSS family hashes are resolved by the collector.
TextSystemStatus layout_text_font(const Style&, LayoutTextFont&) noexcept;


} // namespace jellyframe::detail
