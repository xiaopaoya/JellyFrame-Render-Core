#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
namespace jellyframe {
namespace shaped_packet {
constexpr std::size_t header_bytes = 48, cluster_bytes = 16, glyph_bytes = 36;
struct Header {
  std::int32_t size_q1000 = 0;
  std::uint16_t selection = 0;
  std::uint64_t resource_id = 0, binding_generation = 0;
};
struct Cluster {
  std::uint32_t source_begin = 0, source_end = 0, glyph_begin = 0,
                glyph_end = 0;
};
struct Glyph {
  std::uint32_t gid = 0, cluster = 0;
  std::int32_t x = 0, y = 0, advance_x = 0, advance_y = 0, offset_x = 0,
               offset_y = 0;
};
struct Limits {
  std::size_t bytes = 1024 * 1024, source_bytes = 65536, clusters = 4096,
              glyphs = 4096;
  std::uint32_t glyph_ceiling = 65536;
};
enum class Status { ok, malformed, limit, allocation };
class View {
public:
  bool valid() const noexcept { return !bytes_.empty(); }
  Header header() const noexcept;
  std::string_view source() const noexcept;
  std::size_t cluster_count() const noexcept;
  std::size_t glyph_count() const noexcept;
  bool cluster(std::size_t, Cluster &) const noexcept;
  bool glyph(std::size_t, Glyph &) const noexcept;
  std::string_view bytes() const noexcept { return bytes_; }

private:
  std::string_view bytes_;
  friend Status decode(std::string_view, const Limits &, std::uint64_t &,
                       View &) noexcept;
};
// Borrowed immutable decode; output unchanged on refusal. Work is cumulative.
Status decode(std::string_view, const Limits &, std::uint64_t &,
              View &) noexcept;
// Inputs may alias the old output string. Draft publication preserves output on
// refusal.
Status encode(const Header &, std::string_view, const Cluster *, std::size_t,
              const Glyph *, std::size_t, const Limits &, std::uint64_t &,
              std::string &) noexcept;
} // namespace shaped_packet
} // namespace jellyframe
