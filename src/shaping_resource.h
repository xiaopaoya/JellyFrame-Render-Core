#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace jellyframe {
namespace shaping_resource {
// Private experimental resource boundary. Bytes must remain immutable while
// borrowed.
struct Identity {
    unsigned char source[32];
    unsigned char profile[32];
    std::uint32_t glyph_count = 0;
};
struct Limits {
    std::uint32_t bytes = 262144, selections = 32, references = 256;
    std::uint32_t lookups = 64, subtables = 256, records = 131072;
};
enum class Status { ok, malformed, identity_mismatch, limit };
struct View {
    std::string_view bytes;
    std::uint32_t selections = 0, references = 0, lookups = 0, glyphs = 0;
};
// Failure preserves output. Work includes checksum bytes and semantic
// traversal.
Status validate(std::string_view bytes, const Identity &expected,
                const Limits &limits, std::uint64_t &remaining_work,
                View &output) noexcept;
} // namespace shaping_resource
} // namespace jellyframe
