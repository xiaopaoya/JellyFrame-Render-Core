#pragma once

#include "render_core/feature_config.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#if !JELLYFRAME_RENDER_CORE_SHAPED_PACKET_V2_ENABLED
#error "JFSP v2 requires JELLYFRAME_ENABLE_SHAPED_PACKET_V2"
#endif

namespace jellyframe::shaped_packet_v2 {

inline constexpr std::uint16_t version = 2;
inline constexpr std::size_t header_bytes = 80;

struct Header {
    std::int32_t size_q1000 = 0;
    std::uint16_t selection = 0;
    std::uint64_t resource_id = 0;
    std::uint64_t binding_generation = 0;
    std::array<unsigned char, 32> binding_identity{};
};

struct Cluster {
    std::uint32_t source_begin = 0, source_end = 0;
    std::uint32_t glyph_begin = 0, glyph_end = 0;
};

struct Glyph {
    std::uint32_t gid = 0, cluster = 0;
    std::int32_t x = 0, y = 0, advance_x = 0, advance_y = 0;
    std::int32_t offset_x = 0, offset_y = 0;
};

struct Limits {
    std::size_t bytes = 1024U * 1024U;
    std::size_t source_bytes = 65536;
    std::size_t clusters = 4096;
    std::size_t glyphs = 4096;
    std::uint32_t glyph_ceiling = 65536;
};

enum class Status { ok, malformed, limit, allocation };

// A view borrows immutable packet bytes; keep them alive for its entire use.
class View {
public:
    bool valid() const noexcept { return !bytes_.empty(); }
    Header header() const noexcept;
    std::string_view source() const noexcept;
    std::size_t cluster_count() const noexcept;
    std::size_t glyph_count() const noexcept;
    bool cluster(std::size_t index, Cluster &output) const noexcept;
    bool glyph(std::size_t index, Glyph &output) const noexcept;
    std::string_view bytes() const noexcept { return bytes_; }

private:
    std::string_view bytes_;
    friend Status decode(std::string_view, const Limits &, std::uint64_t &, View &) noexcept;
};

// Both functions preserve work and output on refusal. Limits may only lower
// the default maxima and must be nonzero. Decode performs no allocation.
Status decode(std::string_view bytes, const Limits &limits,
              std::uint64_t &work, View &output) noexcept;
Status encode(const Header &header, std::string_view source,
              const Cluster *clusters, std::size_t cluster_count,
              const Glyph *glyphs, std::size_t glyph_count,
              const Limits &limits, std::uint64_t &work,
              std::string &output) noexcept;

} // namespace jellyframe::shaped_packet_v2
