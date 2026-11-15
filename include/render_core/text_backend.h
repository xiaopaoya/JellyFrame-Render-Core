#pragma once

#include <cstdint>
#include <string_view>

namespace jellyframe {

std::uint32_t normalized_font_family_hash(std::string_view family);
bool is_generic_font_family(std::string_view family);
int bounded_letter_spacing(int font_size, int letter_spacing);

} // namespace jellyframe
