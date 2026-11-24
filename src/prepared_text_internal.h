#pragma once

#include "render_core/prepared_text.h"

namespace jellyframe::detail {

// Invalid packet data returns false. Storage allocation errors propagate.
bool encode_positioned_prepared_text_or_throw(const PreparedTextGlyph* glyphs,
    std::size_t count, std::int32_t size, std::uint32_t source_bytes, std::string& output);

} // namespace jellyframe::detail
