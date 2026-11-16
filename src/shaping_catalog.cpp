#include "shaping_catalog.h"
#include "shaping_catalog_sha256.h"
#include <cstring>
namespace jellyframe {
namespace shaping_catalog {
namespace {
std::uint32_t read(std::string_view b, std::size_t p, unsigned n) noexcept {
  std::uint32_t v = 0;
  for (unsigned i = 0; i < n; ++i)
    v |= std::uint32_t(static_cast<unsigned char>(b[p + i])) << (8 * i);
  return v;
}
bool charge(std::uint64_t &work, std::uint64_t amount) noexcept {
  if (amount > work)
    return false;
  work -= amount;
  return true;
}
std::uint32_t crc(std::string_view b) noexcept {
  std::uint32_t value = ~0U;
  for (unsigned char c : b) {
    value ^= c;
    for (unsigned i = 0; i < 8; ++i)
      value = (value >> 1) ^ (0xedb88320U & (0U - (value & 1U)));
  }
  return ~value;
}
} // namespace
Metrics View::metadata() const noexcept {
  Metrics result{};
  if (bytes_.empty())
    return result;
  result.units_per_em = static_cast<std::uint16_t>(read(bytes_, 128, 2));
  result.raster_pixel_size = static_cast<std::uint16_t>(read(bytes_, 130, 2));
  result.weight = static_cast<std::uint16_t>(read(bytes_, 132, 2));
  result.coverage_bits = 4;
  result.ascent = static_cast<std::int32_t>(read(bytes_, 136, 4));
  result.descent = static_cast<std::int32_t>(read(bytes_, 140, 4));
  result.line_gap = static_cast<std::int32_t>(read(bytes_, 144, 4));
  std::memcpy(result.source_sha256.data(), bytes_.data() + 16, 32);
  return result;
}
bool View::map_scalar(std::uint32_t scalar, bool raw,
                      std::uint16_t &gid) const noexcept {
  std::size_t lo = 0, hi = mappings_;
  while (lo < hi) {
    auto mid = lo + (hi - lo) / 2;
    auto p = 172 + mid * 8;
    auto cp = read(bytes_, p, 4);
    if (cp < scalar)
      lo = mid + 1;
    else
      hi = mid;
  }
  if (lo == mappings_)
    return false;
  auto p = 172 + lo * 8;
  if (read(bytes_, p, 4) != scalar ||
      !(read(bytes_, p + 6, 2) & (raw ? 1U : 2U)))
    return false;
  gid = static_cast<std::uint16_t>(read(bytes_, p + 4, 2));
  return true;
}
bool View::find_gid(std::uint16_t gid, FontGlyphView &output) const noexcept {
  std::size_t lo = 0, hi = rasters_, base = 172 + std::size_t(mappings_) * 8;
  while (lo < hi) {
    auto mid = lo + (hi - lo) / 2;
    if (read(bytes_, base + mid * 32, 2) < gid)
      lo = mid + 1;
    else
      hi = mid;
  }
  if (lo == rasters_)
    return false;
  auto p = base + lo * 32;
  if (read(bytes_, p, 2) != gid)
    return false;
  FontGlyphView result{};
  result.glyph_id = gid;
  result.advance = static_cast<std::int32_t>(read(bytes_, p + 4, 4));
  result.bearing_x = static_cast<std::int32_t>(read(bytes_, p + 8, 4));
  result.bearing_y = static_cast<std::int32_t>(read(bytes_, p + 12, 4));
  result.width = static_cast<std::uint16_t>(read(bytes_, p + 16, 2));
  result.height = static_cast<std::uint16_t>(read(bytes_, p + 18, 2));
  result.row_stride = read(bytes_, p + 20, 4);
  result.coverage_bytes = read(bytes_, p + 28, 4);
  result.coverage = reinterpret_cast<const std::uint8_t *>(
      bytes_.data() + base + std::size_t(rasters_) * 32 +
      read(bytes_, p + 24, 4));
  output = result;
  return true;
}
Status bind(std::string_view b, const Identity &expected, const Limits &limits,
            std::uint64_t &work, View &output) noexcept {
  if (b.size() > limits.bytes)
    return Status::limit;
  if (!charge(work, b.size()))
    return Status::limit;
  if (!charge(work, b.size()))
    return Status::limit;
  if (sha256(b) != expected.catalog_sha256)
    return Status::identity_mismatch;
  if (expected.trusted_companion.size() > limits.shaping.bytes ||
      !charge(work, expected.trusted_companion.size()))
    return Status::limit;
  if (sha256(expected.trusted_companion) != expected.companion_sha256)
    return Status::identity_mismatch;
  if (b.size() < 172 || b.substr(0, 8) != std::string_view("JFCAT1\0\0", 8) ||
      read(b, 8, 2) != 1 || read(b, 10, 2) != 172 || read(b, 12, 4) != b.size())
    return Status::malformed;
  if (std::memcmp(b.data() + 16, expected.shaping.source, 32) ||
      std::memcmp(b.data() + 48, expected.shaping.profile, 32) ||
      std::memcmp(b.data() + 80, expected.companion_sha256.data(), 32) ||
      read(b, 112, 4) != expected.shaping.glyph_count ||
      read(b, 130, 2) != expected.raster_size ||
      read(b, 132, 2) != expected.weight)
    return Status::identity_mismatch;
  if (read(b, 128, 2) == 0 || read(b, 128, 2) > 16384 || read(b, 134, 1) != 4 ||
      read(b, 135, 1) || read(b, 148, 2) != 14 || read(b, 150, 2) != 0 ||
      read(b, 152, 2) != 1 || read(b, 154, 2))
    return Status::malformed;
  const auto ascent = static_cast<std::int32_t>(read(b, 136, 4));
  const auto descent = static_cast<std::int32_t>(read(b, 140, 4));
  const auto gap = static_cast<std::int32_t>(read(b, 144, 4));
  const auto natural = std::int64_t(ascent) + descent + gap;
  if (!expected.raster_size || expected.raster_size > 4096 ||
      !expected.weight || expected.weight > 1000 || ascent < 0 || descent < 0 ||
      ascent > (1 << 20) || descent > (1 << 20) || gap < -(1 << 20) ||
      gap > (1 << 20) || natural < 0 || natural > (1 << 20))
    return Status::malformed;
  for (std::size_t i = 160; i < 172; ++i)
    if (b[i])
      return Status::malformed;
  if (crc(b.substr(172)) != read(b, 156, 4))
    return Status::malformed;
  View candidate;
  candidate.bytes_ = b;
  candidate.companion_ = expected.trusted_companion;
  candidate.mappings_ = read(b, 116, 4);
  candidate.rasters_ = read(b, 120, 4);
  if (candidate.mappings_ > limits.mappings ||
      candidate.rasters_ > limits.rasters)
    return Status::limit;
  const std::uint64_t raster_base = 172ULL + candidate.mappings_ * 8ULL;
  const std::uint64_t bitmap_base = raster_base + candidate.rasters_ * 32ULL;
  if (bitmap_base > b.size() || read(b, 124, 4) != b.size() - bitmap_base ||
      !candidate.mappings_ || !candidate.rasters_)
    return Status::malformed;
  std::uint32_t previous = 0, next_bitmap = 0;
  for (std::uint32_t i = 0; i < candidate.rasters_; ++i) {
    if (!charge(work, 1))
      return Status::limit;
    auto p = static_cast<std::size_t>(raster_base + i * 32ULL);
    auto gid = read(b, p, 2), width = read(b, p + 16, 2),
         height = read(b, p + 18, 2), stride = read(b, p + 20, 4),
         offset = read(b, p + 24, 4), size = read(b, p + 28, 4);
    if (!gid || gid >= expected.shaping.glyph_count || (i && gid <= previous) ||
        read(b, p + 2, 2) || read(b, p + 4, 4) > (1U << 20) || width > 4096 ||
        height > 4096 || offset != next_bitmap ||
        size > b.size() - bitmap_base - next_bitmap)
      return Status::malformed;
    const auto bearing_x = static_cast<std::int32_t>(read(b, p + 8, 4));
    const auto bearing_y = static_cast<std::int32_t>(read(b, p + 12, 4));
    if (bearing_x < -(1 << 20) || bearing_x > (1 << 20) ||
        bearing_y < -(1 << 20) || bearing_y > (1 << 20))
      return Status::malformed;
    if (!width || !height) {
      if (width || height || stride || size || read(b, p + 8, 4) ||
          read(b, p + 12, 4))
        return Status::malformed;
    } else {
      if (stride != (width + 1) / 2 || std::uint64_t(stride) * height != size)
        return Status::malformed;
      if (width & 1U)
        for (std::uint32_t row = 0; row < height; ++row) {
          if (!charge(work, 1))
            return Status::limit;
          if (read(b,
                   static_cast<std::size_t>(bitmap_base + offset +
                                            row * stride + stride - 1),
                   1) &
              15U)
            return Status::malformed;
        }
    }
    next_bitmap += size;
    previous = gid;
  }
  if (next_bitmap != read(b, 124, 4))
    return Status::malformed;
  previous = 0;
  const auto lookup_cost = std::uint64_t(17);
  for (std::uint32_t i = 0; i < candidate.mappings_; ++i) {
    if (!charge(work, lookup_cost))
      return Status::limit;
    auto p = 172 + std::size_t(i) * 8;
    auto cp = read(b, p, 4), flags = read(b, p + 6, 2);
    FontGlyphView glyph;
    if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) ||
        (i && cp <= previous) || !flags || (flags & ~3U) ||
        !candidate.find_gid(static_cast<std::uint16_t>(read(b, p + 4, 2)),
                            glyph))
      return Status::malformed;
    previous = cp;
  }
  shaping_resource::View companion;
  const auto status =
      shaping_resource::validate(expected.trusted_companion, expected.shaping,
                                 limits.shaping, work, companion);
  if (status != shaping_resource::Status::ok)
    return status == shaping_resource::Status::limit
               ? Status::limit
               : Status::identity_mismatch;
  auto s = companion.bytes;
  const auto directories = 112 + std::size_t(companion.selections) * 16 +
                           std::size_t(companion.references) * 8;
  for (std::uint32_t i = 0; i < companion.lookups; ++i) {
    auto d = directories + i * 16;
    if (read(s, d, 1) != 1)
      continue;
    auto type = read(s, d + 1, 1);
    std::size_t p = read(s, d + 8, 4);
    for (std::uint32_t sub = 0; sub < read(s, d + 2, 2); ++sub) {
      auto end = p + 6 + read(s, p + 2, 4);
      p += 6;
      auto rows = read(s, p, 4);
      p += 4;
      for (std::uint32_t row = 0; row < rows; ++row) {
        FontGlyphView glyph;
        bool inputs = true;
        std::uint16_t target = 0;
        if (type == 1) {
          if (!charge(work, lookup_cost * 2))
            return Status::limit;
          inputs = candidate.find_gid(static_cast<std::uint16_t>(read(s, p, 2)),
                                      glyph);
          target = static_cast<std::uint16_t>(read(s, p + 2, 2));
          p += 4;
        } else {
          auto components = read(s, p, 1);
          target = static_cast<std::uint16_t>(read(s, p + 1, 2));
          p += 3;
          if (!charge(work, lookup_cost * (components + 1)))
            return Status::limit;
          for (std::uint32_t c = 0; c < components; ++c) {
            inputs = candidate.find_gid(
                         static_cast<std::uint16_t>(read(s, p, 2)), glyph) &&
                     inputs;
            p += 2;
          }
        }
        if (inputs && !candidate.find_gid(target, glyph))
          return Status::incomplete_closure;
      }
      if (p != end)
        return Status::malformed;
    }
  }
  output = candidate;
  return Status::ok;
}
} // namespace shaping_catalog
} // namespace jellyframe
