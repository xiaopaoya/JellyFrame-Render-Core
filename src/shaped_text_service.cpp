#include "shaped_text_service.h"
#include <algorithm>
#include <limits>
namespace jellyframe {
namespace shaped_service {
namespace {
bool take(std::uint64_t &w, std::uint64_t n) {
  if (n > w)
    return false;
  w -= n;
  return true;
}
bool scale(std::int64_t value, std::int32_t size, std::uint16_t divisor,
           std::int32_t &out) {
  if (!divisor || size <= 0 || value > INT64_MAX / size || value < INT64_MIN / size)
    return false;
  auto product = value * size;
  if (product > INT64_MAX - divisor / 2 || product < INT64_MIN + divisor / 2)
    return false;
  auto rounded = product < 0 ? -((-product + divisor / 2) / divisor)
                             : (product + divisor / 2) / divisor;
  if (rounded < INT32_MIN || rounded > INT32_MAX)
    return false;
  out = static_cast<std::int32_t>(rounded);
  return true;
}
struct Span {
  std::uintptr_t begin, end;
};
bool span(const void *p, std::size_t n, std::size_t width, Span &s) {
  if (n && (!p || n > SIZE_MAX / width))
    return false;
  auto b = reinterpret_cast<std::uintptr_t>(p);
  auto bytes = n * width;
  if (bytes > UINTPTR_MAX - b)
    return false;
  s = {b, b + bytes};
  return true;
}
} // namespace
Status prepare(const Request &r, std::string_view source, const Buffers &b,
               const Limits &limits, std::uint64_t &work, std::string &packet,
               Metrics &metrics) noexcept {
  if (!r.catalog || !r.tables || !r.eligibility || r.catalog->bytes().empty() ||
      r.packet.size_q1000 <= 0 || !r.packet.resource_id ||
      !r.packet.binding_generation || r.packet.selection > 4)
    return Status::invalid;
  Span spans[10];
  if (!span(b.normalization_scratch, b.normalization_capacity,
            sizeof(*b.normalization_scratch), spans[0]) ||
      !span(b.normalized, b.normalized_capacity, sizeof(*b.normalized),
            spans[1]) ||
      !span(b.input, b.glyph_capacity, sizeof(*b.input), spans[2]) ||
      !span(b.scratch, b.glyph_capacity, sizeof(*b.scratch), spans[3]) ||
      !span(b.positioned, b.glyph_capacity, sizeof(*b.positioned), spans[4]) ||
      !span(b.glyphs, b.glyph_capacity, sizeof(*b.glyphs), spans[5]) ||
      !span(b.clusters, b.cluster_capacity, sizeof(*b.clusters), spans[6]) ||
      !span(&metrics, 1, sizeof(metrics), spans[7]) ||
      !span(&work, 1, sizeof(work), spans[8]) ||
      !span(&packet, 1, sizeof(packet), spans[9]))
    return Status::capacity;
  std::string_view reads[] = {source,
                              r.catalog->bytes(),
                              r.catalog->companion_bytes(),
                              r.tables->bytes(),
                              r.eligibility->bytes(),
                              {reinterpret_cast<const char *>(&r), sizeof(r)},
                              {reinterpret_cast<const char *>(&b), sizeof(b)},
                              {reinterpret_cast<const char *>(&limits), sizeof(limits)},
                              {reinterpret_cast<const char *>(r.catalog), sizeof(*r.catalog)},
                              {reinterpret_cast<const char *>(r.tables), sizeof(*r.tables)},
                              {reinterpret_cast<const char *>(r.eligibility), sizeof(*r.eligibility)}};
  for (unsigned i = 0; i < 10; ++i) {
    for (unsigned j = 0; j < i; ++j)
      if (spans[i].begin < spans[j].end && spans[j].begin < spans[i].end)
        return Status::overlap;
    if (i < 9) {
      Span previous;
      if (!span(packet.data(), packet.size(), 1, previous))
        return Status::invalid;
      if (spans[i].begin < previous.end && previous.begin < spans[i].end)
        return Status::overlap;
    }
    for (auto read : reads) {
      Span s;
      if (!span(read.data(), read.size(), 1, s))
        return Status::invalid;
      if (spans[i].begin < s.end && s.begin < spans[i].end)
        return Status::overlap;
    }
  }
  std::size_t count = 0;
  auto norm = shaping_normalization::normalize(
      *r.tables, *r.eligibility, *r.catalog, source, b.normalization_scratch,
      b.normalization_capacity, b.normalized, b.normalized_capacity, count,
      limits.normalization, work);
  if (norm != shaping_normalization::Status::ok)
    return Status::normalization;
  if (count > b.glyph_capacity)
    return Status::capacity;
  for (std::size_t i = 0; i < count; ++i) {
    if (!take(work, 1))
      return Status::limit;
    auto s = b.normalized[i];
    b.input[i] = {s.gid, s.source_begin, s.source_end, 0, 0, 0, 0};
  }
  // The executor ledger covers both intermediate and final records. Packet
  // limits independently bound the final published glyph count.
  shaping_lookup::Ledger ledger{work, std::uint64_t(limits.packet.glyphs) * 2};
  std::size_t substituted = 0;
  auto status = shaping_lookup::substitute(
      *r.catalog, r.packet.selection, b.input, count, b.scratch,
      b.glyph_capacity, b.positioned, b.glyph_capacity, substituted, ledger);
  work = ledger.work;
  if (status != shaping_lookup::Status::ok)
    return Status::lookup;
  for (std::size_t i = 0; i < substituted; ++i) {
    if (!take(work, 17))
      return Status::limit;
    FontGlyphView glyph;
    if (!r.catalog->find_gid(b.positioned[i].gid, glyph))
      return Status::invalid;
    b.positioned[i].advance_x = glyph.advance;
  }
  ledger.work = work;
  count = 0;
  status = shaping_lookup::position(
      *r.catalog, r.packet.selection, b.positioned, substituted, b.scratch,
      b.glyph_capacity, b.input, b.glyph_capacity, count, ledger);
  work = ledger.work;
  if (status != shaping_lookup::Status::ok)
    return Status::lookup;
  Metrics draft;
  auto m = r.catalog->metadata();
  auto size = r.packet.size_q1000;
  if (!scale(m.ascent, size, m.units_per_em, draft.ascent) ||
      !scale(m.descent, size, m.units_per_em, draft.descent) ||
      !scale(m.line_gap, size, m.units_per_em, draft.line_gap))
    return Status::overflow;
  std::int64_t pen_x = 0, pen_y = 0;
  std::size_t clusters = 0;
  for (std::size_t i = 0; i < count; ++i) {
    if (!take(work, 1))
      return Status::limit;
    const auto &g = b.input[i];
    if (!i || g.source_begin != b.input[i - 1].source_begin ||
        g.source_end != b.input[i - 1].source_end) {
      if (clusters >= b.cluster_capacity)
        return Status::capacity;
      b.clusters[clusters++] = {g.source_begin, g.source_end,
                                static_cast<std::uint32_t>(i),
                                static_cast<std::uint32_t>(i + 1)};
    } else
      b.clusters[clusters - 1].glyph_end = static_cast<std::uint32_t>(i + 1);
    auto &out = b.glyphs[i];
    out = {};
    out.gid = g.gid;
    out.cluster = static_cast<std::uint32_t>(clusters - 1);
    if (!scale(pen_x, size, m.units_per_em, out.x) ||
        !scale(-pen_y, size, m.units_per_em, out.y) ||
        !scale(g.advance_x, size, m.units_per_em, out.advance_x) ||
        !scale(-std::int64_t(g.advance_y), size, m.units_per_em,
               out.advance_y) ||
        !scale(g.offset_x, size, m.units_per_em, out.offset_x) ||
        !scale(-std::int64_t(g.offset_y), size, m.units_per_em, out.offset_y))
      return Status::overflow;
    pen_x += g.advance_x;
    pen_y += g.advance_y;
    if (!take(work, 17))
      return Status::limit;
    FontGlyphView raster;
    if (!r.catalog->find_gid(g.gid, raster))
      return Status::invalid;
    std::int32_t bx, by, width, height;
    if (!scale(raster.bearing_x, size, m.raster_pixel_size, bx) ||
        !scale(raster.bearing_y, size, m.raster_pixel_size, by) ||
        !scale(raster.width, size, m.raster_pixel_size, width) ||
        !scale(raster.height, size, m.raster_pixel_size, height))
      return Status::overflow;
    auto left = std::int64_t(out.x) + out.offset_x + bx,
         top = std::int64_t(out.y) + out.offset_y + by, right = left + width,
         bottom = top + height;
    if (left < INT32_MIN || right > INT32_MAX || top < INT32_MIN ||
        bottom > INT32_MAX)
      return Status::overflow;
    if (width && height) {
      if (!draft.has_ink) {
        draft.ink_left = static_cast<std::int32_t>(left);
        draft.ink_top = static_cast<std::int32_t>(top);
        draft.ink_right = static_cast<std::int32_t>(right);
        draft.ink_bottom = static_cast<std::int32_t>(bottom);
        draft.has_ink = true;
      } else {
        draft.ink_left =
            std::min(draft.ink_left, static_cast<std::int32_t>(left));
        draft.ink_top = std::min(draft.ink_top, static_cast<std::int32_t>(top));
        draft.ink_right =
            std::max(draft.ink_right, static_cast<std::int32_t>(right));
        draft.ink_bottom =
            std::max(draft.ink_bottom, static_cast<std::int32_t>(bottom));
      }
    }
  }
  if (!scale(pen_x, size, m.units_per_em, draft.advance_x) ||
      !scale(-pen_y, size, m.units_per_em, draft.advance_y))
    return Status::overflow;
  draft.glyph_count = count;
  draft.cluster_count = clusters;
  auto encoded =
      shaped_packet::encode(r.packet, source, b.clusters, clusters, b.glyphs,
                            count, limits.packet, work, packet);
  if (encoded != shaped_packet::Status::ok)
    return Status::packet;
  metrics = draft;
  return Status::ok;
}
} // namespace shaped_service
} // namespace jellyframe
