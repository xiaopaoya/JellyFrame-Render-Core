#include "contextual_text_painter.h"

#include "render_core/software_renderer.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>

namespace jellyframe {
namespace contextual_painter {
namespace {
std::int64_t round_q1000(std::int64_t value) noexcept {
  return value >= 0 ? (value + 500) / 1000 : -((-value + 500) / 1000);
}

std::int64_t round_ratio(std::int64_t value, std::int32_t size,
                         std::uint16_t raster_size) noexcept {
  const auto product = value * size;
  const auto divisor = std::int64_t(raster_size) * 1000;
  return product >= 0 ? (product + divisor / 2) / divisor
                      : -((-product + divisor / 2) / divisor);
}

std::uint8_t coverage_at(const FontGlyphView &glyph, std::size_t x,
                         std::size_t y) noexcept {
  const auto packed = glyph.coverage[y * glyph.row_stride + x / 2];
  return static_cast<std::uint8_t>((x & 1) ? packed & 15U : packed >> 4);
}

void blend(Color &dst, Color src, std::uint8_t coverage) noexcept {
  const unsigned alpha = (unsigned(src.a) * coverage + 7U) / 15U;
  const unsigned inverse = 255U - alpha;
  dst.r = static_cast<std::uint8_t>((unsigned(src.r) * alpha + unsigned(dst.r) * inverse + 127U) / 255U);
  dst.g = static_cast<std::uint8_t>((unsigned(src.g) * alpha + unsigned(dst.g) * inverse + 127U) / 255U);
  dst.b = static_cast<std::uint8_t>((unsigned(src.b) * alpha + unsigned(dst.b) * inverse + 127U) / 255U);
  dst.a = static_cast<std::uint8_t>(alpha + (unsigned(dst.a) * inverse + 127U) / 255U);
}

struct Positioned {
  FontGlyphView glyph{};
  std::int64_t x = 0, y = 0;
  std::int64_t width = 0, height = 0;
};

bool position(const shaped_packet::Glyph &pg, std::int32_t size,
              std::uint16_t raster_size, const FontGlyphView &glyph,
              std::int32_t baseline_x, std::int32_t baseline_y,
              Positioned &out) noexcept {
  out.glyph = glyph;
  const auto xq = std::int64_t(baseline_x) + pg.x + pg.offset_x;
  const auto yq = std::int64_t(baseline_y) + pg.y + pg.offset_y;
  out.x = round_q1000(xq) + round_ratio(glyph.bearing_x, size, raster_size);
  out.y = round_q1000(yq) + round_ratio(glyph.bearing_y, size, raster_size);
  out.width = round_ratio(glyph.width, size, raster_size);
  out.height = round_ratio(glyph.height, size, raster_size);
  if (glyph.width && out.width == 0) out.width = 1;
  if (glyph.height && out.height == 0) out.height = 1;
  return out.width >= 0 && out.height >= 0;
}
} // namespace

Status glyph_ink_bounds(const shaped_packet::View &packet,
                        const shaping_binding::Snapshot &binding,
                        std::int32_t baseline_x, std::int32_t baseline_y,
                        std::vector<Rect> &output,
                        std::uint64_t &work) noexcept {
  if (!packet.valid() || !binding.valid()) return Status::invalid;
  const auto header = packet.header();
  if (header.resource_id != binding.resource_id() ||
      header.binding_generation != binding.generation() ||
      header.binding_identity != binding.content_identity())
    return Status::identity_mismatch;
  const auto raster_size = binding.catalog().metadata().raster_pixel_size;
  if (header.size_q1000 <= 0 || !raster_size) return Status::invalid;
  const auto count = packet.glyph_count();
  if (count > 4096) return Status::limit;
  const std::uint64_t cost = count * 18U;
  if (cost > work) return Status::limit;

  try {
    std::vector<Rect> draft;
    draft.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      shaped_packet::Glyph pg;
      if (!packet.glyph(i, pg)) return Status::invalid;
      FontGlyphView glyph;
      if (pg.gid > std::numeric_limits<std::uint16_t>::max() ||
          !binding.catalog().find_gid(static_cast<std::uint16_t>(pg.gid), glyph))
        return Status::missing_glyph;
      Positioned placed;
      if (!position(pg, header.size_q1000, raster_size, glyph,
                    baseline_x, baseline_y, placed))
        return Status::invalid;
      if (placed.x < INT32_MIN || placed.x > INT32_MAX ||
          placed.y < INT32_MIN || placed.y > INT32_MAX ||
          placed.width > INT32_MAX || placed.height > INT32_MAX ||
          placed.x + placed.width > INT32_MAX ||
          placed.y + placed.height > INT32_MAX)
        return Status::limit;
      draft.push_back({static_cast<int>(placed.x), static_cast<int>(placed.y),
                       static_cast<int>(placed.width), static_cast<int>(placed.height)});
    }
    output.swap(draft);
    work -= cost;
    return Status::ok;
  } catch (const std::bad_alloc &) {
    return Status::allocation;
  } catch (const std::length_error &) {
    return Status::allocation;
  }
}

Status paint(const shaped_packet::View &packet,
             const shaping_binding::Snapshot &binding, FrameBuffer &target,
             std::int32_t baseline_x, std::int32_t baseline_y, Rect clip,
             Color color, std::uint64_t &work) noexcept {
  if (!packet.valid() || !binding.valid() || target.width <= 0 ||
      target.height <= 0 || std::size_t(target.width) > SIZE_MAX / std::size_t(target.height) ||
      target.pixels.size() != std::size_t(target.width) * std::size_t(target.height) ||
      clip.width < 0 || clip.height < 0)
    return Status::invalid;
  const auto header = packet.header();
  if (header.resource_id != binding.resource_id() ||
      header.binding_generation != binding.generation() ||
      header.binding_identity != binding.content_identity())
    return Status::identity_mismatch;
  const auto metadata = binding.catalog().metadata();
  if (header.size_q1000 <= 0 || !metadata.raster_pixel_size)
    return Status::invalid;
  const auto right = std::clamp<std::int64_t>(std::int64_t(clip.x) + clip.width, 0, target.width);
  const auto bottom = std::clamp<std::int64_t>(std::int64_t(clip.y) + clip.height, 0, target.height);
  const int left = static_cast<int>(std::clamp<std::int64_t>(clip.x, 0, target.width));
  const int top = static_cast<int>(std::clamp<std::int64_t>(clip.y, 0, target.height));
  const auto count = packet.glyph_count();
  if (count > 4096)
    return Status::limit;
  // Each of two passes does at most16 catalog comparisons plus record access.
  // Charge both passes before writing, including fully clipped glyphs.
  std::uint64_t cost = count * 36U;
  for (std::size_t i = 0; i < count; ++i) {
    shaped_packet::Glyph pg;
    if (!packet.glyph(i, pg)) return Status::invalid;
    FontGlyphView glyph;
    if (pg.gid > std::numeric_limits<std::uint16_t>::max() ||
        !binding.catalog().find_gid(static_cast<std::uint16_t>(pg.gid), glyph))
      return Status::missing_glyph;
    Positioned p;
    if (!position(pg, header.size_q1000, metadata.raster_pixel_size, glyph,
                  baseline_x, baseline_y, p)) return Status::invalid;
    const auto x0 = std::max<std::int64_t>(p.x, left), y0 = std::max<std::int64_t>(p.y, top);
    const auto x1 = std::min<std::int64_t>(p.x + p.width, right), y1 = std::min<std::int64_t>(p.y + p.height, bottom);
    const auto area = x1 > x0 && y1 > y0 ? std::uint64_t(x1 - x0) * std::uint64_t(y1 - y0) : 0;
    if (area > std::numeric_limits<std::uint64_t>::max() - cost) return Status::limit;
    cost += area;
  }
  if (cost > work) return Status::limit;
  work -= cost;

  for (std::size_t i = 0; i < count; ++i) {
    shaped_packet::Glyph pg;
    FontGlyphView glyph;
    packet.glyph(i, pg);
    binding.catalog().find_gid(static_cast<std::uint16_t>(pg.gid), glyph);
    Positioned p;
    position(pg, header.size_q1000, metadata.raster_pixel_size, glyph,
             baseline_x, baseline_y, p);
    if (!p.glyph.width || !p.glyph.height || p.width <= 0 || p.height <= 0) continue;
    if (p.x >= right || p.y >= bottom || p.x + p.width <= left || p.y + p.height <= top)
      continue;
    const int x0 = static_cast<int>(std::max<std::int64_t>(p.x, left));
    const int y0 = static_cast<int>(std::max<std::int64_t>(p.y, top));
    const int x1 = static_cast<int>(std::min<std::int64_t>(p.x + p.width, right));
    const int y1 = static_cast<int>(std::min<std::int64_t>(p.y + p.height, bottom));
    for (int y = y0; y < y1; ++y) {
      const auto sy = std::min<std::size_t>(p.glyph.height - 1,
          std::uint64_t(y - p.y) * p.glyph.height / p.height);
      for (int x = x0; x < x1; ++x) {
        const auto sx = std::min<std::size_t>(p.glyph.width - 1,
            std::uint64_t(x - p.x) * p.glyph.width / p.width);
        blend(target.pixel(x, y), color, coverage_at(p.glyph, sx, sy));
      }
    }
  }
  return Status::ok;
}

} // namespace contextual_painter
} // namespace jellyframe
