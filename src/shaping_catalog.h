#pragma once
#include "render_core/font_resource.h"
#include "shaping_resource.h"
#include <array>
namespace jellyframe {
namespace shaping_catalog {
struct Identity {
  shaping_resource::Identity shaping{};
  std::array<unsigned char, 32> companion_sha256{};
  // Exact independently registered companion bytes, retained and immutable.
  std::string_view trusted_companion;
  std::array<unsigned char, 32> catalog_sha256{};
  std::uint16_t raster_size = 16, weight = 400;
};
struct Limits {
  std::size_t bytes = 8U * 1024U * 1024U;
  std::uint32_t mappings = 65536, rasters = 65535;
  shaping_resource::Limits shaping;
};
enum class Status {
  ok,
  malformed,
  identity_mismatch,
  limit,
  incomplete_closure
};
struct Metrics {
  std::uint16_t units_per_em = 0, raster_pixel_size = 0, weight = 0;
  std::uint8_t coverage_bits = 4;
  std::int32_t ascent = 0, descent = 0, line_gap = 0;
  std::array<std::uint8_t, 32> source_sha256{};
};
class View {
public:
  Metrics metadata() const noexcept;
  bool find_gid(std::uint16_t gid, FontGlyphView &output) const noexcept;
  bool map_scalar(std::uint32_t scalar, bool raw_input,
                  std::uint16_t &gid) const noexcept;
  std::string_view bytes() const noexcept { return bytes_; }
  std::string_view companion_bytes() const noexcept { return companion_; }

private:
  friend Status bind(std::string_view, const Identity &, const Limits &,
                     std::uint64_t &, View &) noexcept;
  std::string_view bytes_;
  std::string_view companion_;
  std::uint32_t mappings_ = 0, rasters_ = 0;
};
// Revalidates companion bytes, never consumes caller-forged shaping View.
// On refusal output is unchanged. All bytes and trusted registration survive
// every view, prepared result and frame replay that refers to this binding.
Status bind(std::string_view, const Identity &, const Limits &, std::uint64_t &,
            View &) noexcept;
} // namespace shaping_catalog
} // namespace jellyframe
