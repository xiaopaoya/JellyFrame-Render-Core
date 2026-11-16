#include "shaped_text_packet.h"
#include <limits>
#include <new>
#include <stdexcept>
namespace jellyframe {
namespace shaped_packet {
namespace {
std::uint64_t read(std::string_view b, std::size_t p, unsigned n) noexcept {
  std::uint64_t v = 0;
  for (unsigned i = 0; i < n; ++i)
    v |= std::uint64_t(static_cast<unsigned char>(b[p + i])) << (8 * i);
  return v;
}
std::int32_t signed32(std::string_view b, std::size_t p) noexcept {
  auto v = read(b, p, 4);
  return static_cast<std::int32_t>(
      v <= 0x7fffffff ? static_cast<std::int64_t>(v)
                      : static_cast<std::int64_t>(v) - 4294967296LL);
}
void put(std::string &b, std::size_t p, std::uint64_t v, unsigned n = 4) {
  for (unsigned i = 0; i < n; ++i)
    b[p + i] = static_cast<char>(v >> (8 * i));
}
bool take(std::uint64_t &w, std::uint64_t n) noexcept {
  if (n > w)
    return false;
  w -= n;
  return true;
}
bool boundary(std::string_view s, std::size_t p) noexcept {
  return p == s.size() || (static_cast<unsigned char>(s[p]) & 0xc0) != 0x80;
}
bool utf8(std::string_view s) noexcept {
  for (std::size_t p = 0; p < s.size();) {
    auto c = static_cast<unsigned char>(s[p++]);
    if (c < 128)
      continue;
    unsigned n;
    std::uint32_t cp, min;
    if (c >= 0xc2 && c <= 0xdf) {
      n = 1;
      cp = c & 31;
      min = 128;
    } else if (c >= 0xe0 && c <= 0xef) {
      n = 2;
      cp = c & 15;
      min = 2048;
    } else if (c >= 0xf0 && c <= 0xf4) {
      n = 3;
      cp = c & 7;
      min = 65536;
    } else
      return false;
    if (n > s.size() - p)
      return false;
    while (n--) {
      auto d = static_cast<unsigned char>(s[p++]);
      if ((d & 0xc0) != 0x80)
        return false;
      cp = (cp << 6) | (d & 63);
    }
    if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
      return false;
  }
  return true;
}
bool sum(std::int32_t a, std::int32_t b) noexcept {
  auto s = std::int64_t(a) + b;
  return s >= INT32_MIN && s <= INT32_MAX;
}
} // namespace
Header View::header() const noexcept {
  if (!valid())
    return {};
  return {signed32(bytes_, 24), static_cast<std::uint16_t>(read(bytes_, 28, 2)),
          read(bytes_, 32, 8), read(bytes_, 40, 8)};
}
std::size_t View::cluster_count() const noexcept {
  return valid() ? read(bytes_, 16, 4) : 0;
}
std::size_t View::glyph_count() const noexcept {
  return valid() ? read(bytes_, 12, 4) : 0;
}
std::string_view View::source() const noexcept {
  return valid() ? bytes_.substr(48, read(bytes_, 20, 4)) : std::string_view{};
}
bool View::cluster(std::size_t i, Cluster &out) const noexcept {
  if (i >= cluster_count())
    return false;
  auto p = 48 + source().size() + i * 16;
  out = {static_cast<std::uint32_t>(read(bytes_, p, 4)),
         static_cast<std::uint32_t>(read(bytes_, p + 4, 4)),
         static_cast<std::uint32_t>(read(bytes_, p + 8, 4)),
         static_cast<std::uint32_t>(read(bytes_, p + 12, 4))};
  return true;
}
bool View::glyph(std::size_t i, Glyph &out) const noexcept {
  if (i >= glyph_count())
    return false;
  auto p = 48 + source().size() + cluster_count() * 16 + i * 36;
  out = {static_cast<std::uint32_t>(read(bytes_, p, 4)),
         static_cast<std::uint32_t>(read(bytes_, p + 4, 4)),
         signed32(bytes_, p + 8),
         signed32(bytes_, p + 12),
         signed32(bytes_, p + 16),
         signed32(bytes_, p + 20),
         signed32(bytes_, p + 24),
         signed32(bytes_, p + 28)};
  return true;
}
Status decode(std::string_view b, const Limits &l, std::uint64_t &work,
              View &out) noexcept {
  if (b.size() > l.bytes || !take(work, b.size()))
    return Status::limit;
  if (b.size() < 48 || b.substr(0, 4) != "JFSP" || read(b, 4, 2) != 1 ||
      read(b, 6, 2) != 48 || read(b, 8, 4) != b.size() || read(b, 30, 2))
    return Status::malformed;
  auto ng = read(b, 12, 4), nc = read(b, 16, 4), ns = read(b, 20, 4);
  if (ng > l.glyphs || nc > l.clusters || ns > l.source_bytes)
    return Status::limit;
  if (48 + ns + nc * 16 + ng * 36 != b.size())
    return Status::malformed;
  View v;
  v.bytes_ = b;
  auto h = v.header();
  if (h.size_q1000 <= 0 || h.selection > 4 || !h.resource_id ||
      !h.binding_generation || !l.glyph_ceiling || l.glyph_ceiling > 65536)
    return Status::malformed;
  auto s = v.source();
  if (!utf8(s))
    return Status::malformed;
  if ((!ns && (nc || ng)) || (ns && (!nc || !ng)))
    return Status::malformed;
  std::uint32_t sb = 0, gb = 0;
  for (std::size_t i = 0; i < nc; ++i) {
    if (!take(work, 1))
      return Status::limit;
    Cluster c;
    v.cluster(i, c);
    if (c.source_begin != sb || c.glyph_begin != gb || c.source_end <= sb ||
        c.source_end > ns || c.glyph_end <= gb || c.glyph_end > ng ||
        !boundary(s, c.source_end))
      return Status::malformed;
    for (auto j = gb; j < c.glyph_end; ++j) {
      if (!take(work, 1))
        return Status::limit;
      Glyph g;
      v.glyph(j, g);
      auto p = 48 + ns + nc * 16 + std::uint64_t(j) * 36;
      if (!g.gid || g.gid >= l.glyph_ceiling || g.cluster != i ||
          read(b, p + 32, 4) || !sum(g.x, g.offset_x) ||
          !sum(g.y, g.offset_y) || !sum(g.x, g.advance_x) ||
          !sum(g.y, g.advance_y))
        return Status::malformed;
    }
    sb = c.source_end;
    gb = c.glyph_end;
  }
  if (sb != ns || gb != ng)
    return Status::malformed;
  out = v;
  return Status::ok;
}
Status encode(const Header &h, std::string_view s, const Cluster *c,
              std::size_t nc, const Glyph *g, std::size_t ng, const Limits &l,
              std::uint64_t &work, std::string &out) noexcept {
  if ((nc && !c) || (ng && !g))
    return Status::malformed;
  if (s.size() > l.source_bytes || nc > l.clusters || ng > l.glyphs ||
      s.size() > UINT32_MAX || nc > UINT32_MAX || ng > UINT32_MAX)
    return Status::limit;
  auto total =
      48ULL + s.size() + std::uint64_t(nc) * 16 + std::uint64_t(ng) * 36;
  if (total > l.bytes || total > UINT32_MAX || !take(work, total))
    return Status::limit;
  try {
    std::string b(static_cast<std::size_t>(total), '\0');
    b.replace(0, 4, "JFSP");
    put(b, 4, 1, 2);
    put(b, 6, 48, 2);
    put(b, 8, total);
    put(b, 12, ng);
    put(b, 16, nc);
    put(b, 20, s.size());
    put(b, 24, h.size_q1000);
    put(b, 28, h.selection, 2);
    put(b, 32, h.resource_id, 8);
    put(b, 40, h.binding_generation, 8);
    b.replace(48, s.size(), s);
    auto p = 48 + s.size();
    for (std::size_t i = 0; i < nc; ++i, p += 16) {
      put(b, p, c[i].source_begin);
      put(b, p + 4, c[i].source_end);
      put(b, p + 8, c[i].glyph_begin);
      put(b, p + 12, c[i].glyph_end);
    }
    for (std::size_t i = 0; i < ng; ++i, p += 36) {
      put(b, p, g[i].gid);
      put(b, p + 4, g[i].cluster);
      put(b, p + 8, g[i].x);
      put(b, p + 12, g[i].y);
      put(b, p + 16, g[i].advance_x);
      put(b, p + 20, g[i].advance_y);
      put(b, p + 24, g[i].offset_x);
      put(b, p + 28, g[i].offset_y);
    }
    View v;
    auto status = decode(b, l, work, v);
    if (status != Status::ok)
      return status;
    out.swap(b);
    return Status::ok;
  } catch (const std::bad_alloc &) {
    return Status::allocation;
  } catch (const std::length_error &) {
    return Status::allocation;
  }
}
} // namespace shaped_packet
} // namespace jellyframe
