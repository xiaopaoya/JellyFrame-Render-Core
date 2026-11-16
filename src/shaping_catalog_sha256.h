#pragma once
#include <array>
#include <cstdint>
#include <string_view>
namespace jellyframe {
namespace shaping_catalog {
std::array<unsigned char, 32> sha256(std::string_view bytes) noexcept;
}
} // namespace jellyframe
