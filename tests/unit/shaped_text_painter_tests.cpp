#include "shaped_text_painter.h"
#include "shaping_catalog_sha256.h"
#include "shaping_normalization.h"
#include "render_core/software_renderer.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

using namespace jellyframe;
namespace {
void check(bool value, const std::string &message) {
  if (!value) throw std::runtime_error(message);
}
std::string load(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  check(bool(f), "fixture open");
  return {std::istreambuf_iterator<char>(f), {}};
}
std::vector<std::uint8_t> font_resource(const shaping_catalog::Metrics &m,
                                       std::uint32_t scalar,
                                       const FontGlyphView &glyph) {
  std::vector<std::uint8_t> b(168 + glyph.coverage_bytes, 0);
  std::memcpy(b.data(), "JFFONT2", 8);
  auto put16 = [&](std::size_t p, unsigned v) { b[p] = std::uint8_t(v); b[p + 1] = std::uint8_t(v >> 8); };
  auto put32 = [&](std::size_t p, std::uint32_t v) { for (unsigned i = 0; i < 4; ++i) b[p + i] = std::uint8_t(v >> (i * 8)); };
  put16(8, 2); put16(10, 128); put16(12, 40); put16(14, 1);
  put32(16, static_cast<std::uint32_t>(b.size())); put32(20, 1);
  put32(24, 128); put32(28, 168); put32(32, static_cast<std::uint32_t>(glyph.coverage_bytes)); put16(36, m.units_per_em);
  put16(38, m.raster_pixel_size); put16(40, m.weight); b[42] = m.coverage_bits;
  put32(44, static_cast<std::uint32_t>(m.ascent)); put32(48, static_cast<std::uint32_t>(m.descent));
  put32(52, static_cast<std::uint32_t>(m.line_gap));
  std::memcpy(b.data() + 64, m.source_sha256.data(), 32);
  put32(100, 1); put16(104, 1);
  put32(128, scalar); put32(132, glyph.glyph_id); put32(136, static_cast<std::uint32_t>(glyph.advance));
  put32(140, static_cast<std::uint32_t>(glyph.bearing_x)); put32(144, static_cast<std::uint32_t>(glyph.bearing_y));
  b[148] = static_cast<std::uint8_t>(glyph.width); b[149] = static_cast<std::uint8_t>(glyph.width >> 8);
  b[150] = static_cast<std::uint8_t>(glyph.height); b[151] = static_cast<std::uint8_t>(glyph.height >> 8);
  put32(152, glyph.row_stride); put32(156, 0); put32(160, static_cast<std::uint32_t>(glyph.coverage_bytes));
  std::copy_n(glyph.coverage, glyph.coverage_bytes, b.data() + 168);
  std::uint32_t crc = 0xffffffffU;
  for (std::size_t i = 0; i < b.size(); ++i) {
    crc ^= i >= 96 && i < 100 ? 0U : b[i];
    for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
  }
  put32(96, ~crc);
  return b;
}
struct Fixture {
  std::string catalog_bytes, companion, table_bytes, eligibility_bytes;
  shaping_catalog::View catalog;
  shaping_normalization::Tables tables;
  shaping_normalization::Eligibility eligibility;
  std::vector<std::uint8_t> font_bytes;
  FontResource font;
  jellyframe::shaping_binding::Snapshot binding;
  std::uint32_t gid = 0;
  std::uint32_t scalar = 0;
  explicit Fixture(const std::string &root, const std::string &face = "regular",
                   std::uint16_t weight = 400) {
    catalog_bytes = load(root + "/shaping_executor_catalog/" + face + ".jfcat1");
    companion = load(root + "/shaping_resource/" + face + ".jfshr2");
    table_bytes = load(root + "/shaping_normalization/unicode14.bin");
    eligibility_bytes = load(root + "/shaping_normalization/" + face + ".eligibility");
    shaping_catalog::Identity id;
    std::memcpy(id.shaping.source, catalog_bytes.data() + 16, 32);
    std::memcpy(id.shaping.profile, catalog_bytes.data() + 48, 32);
    id.shaping.glyph_count = 30900; id.weight = weight;
    id.trusted_companion = companion;
    id.companion_sha256 = shaping_catalog::sha256(companion);
    id.catalog_sha256 = shaping_catalog::sha256(catalog_bytes);
    std::uint64_t work = 100000000;
    check(shaping_catalog::bind(catalog_bytes, id, {}, work, catalog) == shaping_catalog::Status::ok, "catalog");
    work = 100000000;
    check(shaping_normalization::validate_tables(table_bytes, shaping_catalog::sha256(table_bytes), {}, work, tables) == shaping_normalization::Status::ok, "tables");
    work = 100000000;
    check(shaping_normalization::validate_eligibility(eligibility_bytes, shaping_catalog::sha256(eligibility_bytes), tables, catalog, work, eligibility) == shaping_normalization::Status::ok, "eligibility");
    FontGlyphView raster;
    for (std::uint32_t cp = 1; cp <= 0x10ffff; ++cp) {
      std::uint16_t mapped = 0;
      if ((catalog.map_scalar(cp, true, mapped) || catalog.map_scalar(cp, false, mapped)) &&
          catalog.find_gid(mapped, raster) && raster.width && raster.height) {
        scalar = cp; gid = mapped; break;
      }
    }
    check(gid != 0, "ink glyph");
    font_bytes = font_resource(catalog.metadata(), scalar, raster);
    check(font.load(font_bytes.data(), font_bytes.size()) == FontResourceStatus::Loaded, "font");
    jellyframe::shaping_binding::BindingState state;
    const auto binding_status = state.bind(catalog, font, tables, eligibility, 73, binding);
    check(binding_status == jellyframe::shaping_binding::Status::ok, "snapshot");
  }
};
std::string packet_for(const shaping_binding::Snapshot &binding,
                       std::uint64_t id, std::uint64_t generation, std::uint32_t gid,
                       std::int32_t x = 0, std::int32_t offset_x = 0,
                       std::int32_t advance_x = 0) {
  shaped_packet::Header h{16000, 0, id, generation};
  h.binding_identity = binding.content_identity();
  shaped_packet::Cluster c{0, 1, 0, 1};
  shaped_packet::Glyph g{gid, 0, x, 0, advance_x, 0, offset_x, 0};
  std::string bytes; std::uint64_t work = 1000000;
  check(shaped_packet::encode(h, "a", &c, 1, &g, 1, {}, work, bytes) == shaped_packet::Status::ok, "packet encode");
  return bytes;
}
bool same_pixels(const std::vector<Color> &a, const std::vector<Color> &b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (a[i].r != b[i].r || a[i].g != b[i].g || a[i].b != b[i].b || a[i].a != b[i].a) return false;
  return true;
}
shaped_packet::View decode(const std::string &bytes) {
  shaped_packet::View v; std::uint64_t work = 1000000;
  check(shaped_packet::decode(bytes, {}, work, v) == shaped_packet::Status::ok, "packet decode"); return v;
}
}
int main(int argc, char **argv) {
  try {
    check(argc == 2, "fixture root"); Fixture f(argv[1]);
    const Color clear{13, 17, 19, 255}, ink{240, 30, 20, 255};
    Fixture alternate(argv[1], "bold", 700);
    check(alternate.binding.resource_id() == f.binding.resource_id() &&
          alternate.binding.generation() == f.binding.generation() &&
          alternate.binding.content_identity() != f.binding.content_identity(),
          "fixture identity collision");
    auto bytes = packet_for(f.binding, 73, 1, f.gid);
    auto v = decode(bytes);
    FrameBuffer base(64, 48, clear), target = base;
    std::uint64_t work = 1000000;
    check(shaped_painter::paint(v, f.binding, target, 8 * 1000, 20 * 1000, {0, 0, 64, 48}, ink, work) == shaped_painter::Status::ok, "paint");
    check(!same_pixels(target.pixels, base.pixels), "paint writes ink");
    auto saved = target.pixels;
    auto refuses_without_writes = [&](const shaped_packet::View &packet, const jellyframe::shaping_binding::Snapshot &binding,
                                      Rect clip, std::uint64_t budget, shaped_painter::Status expected) {
      target.pixels = saved; auto before = target.pixels;
      auto result = shaped_painter::paint(packet, binding, target, 8 * 1000, 20 * 1000, clip, ink, budget);
      check(result == expected && same_pixels(target.pixels, before), "refusal writes framebuffer");
    };
    auto wrong_resource_bytes = packet_for(f.binding, 74, 1, f.gid);
    auto wrong_generation_bytes = packet_for(f.binding, 73, 2, f.gid);
    auto missing_gid_bytes = packet_for(f.binding, 73, 1, 65535);
    refuses_without_writes(decode(wrong_resource_bytes), f.binding, {0, 0, 64, 48}, 1000000, shaped_painter::Status::identity_mismatch);
    refuses_without_writes(decode(wrong_generation_bytes), f.binding, {0, 0, 64, 48}, 1000000, shaped_painter::Status::identity_mismatch);
    refuses_without_writes(decode(missing_gid_bytes), f.binding, {0, 0, 64, 48}, 1000000, shaped_painter::Status::missing_glyph);
    refuses_without_writes(v, alternate.binding, {0, 0, 64, 48}, 1000000, shaped_painter::Status::identity_mismatch);
    refuses_without_writes(v, f.binding, {0, 0, 64, 48}, 0, shaped_painter::Status::limit);
    // A valid packet can be wholly clipped with no writes and no raster cost.
    target.pixels = base.pixels; work = 1000000;
    check(shaped_painter::paint(v, f.binding, target, 8 * 1000, 20 * 1000, {80, 80, 4, 4}, ink, work) == shaped_painter::Status::ok && same_pixels(target.pixels, base.pixels), "fully clipped");
    target = base; work = 1000000;
    check(shaped_painter::paint(v, f.binding, target, 8 * 1000, 20 * 1000, {10, 16, 3, 4}, ink, work) == shaped_painter::Status::ok, "partial clip");
    for (int y = 0; y < base.height; ++y) for (int x = 0; x < base.width; ++x)
      if (x < 10 || x >= 13 || y < 16 || y >= 20)
        check(target.pixel(x, y).r == clear.r && target.pixel(x, y).g == clear.g && target.pixel(x, y).b == clear.b, "clip bounds");
    // Explicit offset moves the bitmap; advance is metadata and does not recur.
    auto advance_bytes = packet_for(f.binding, 73, 1, f.gid, 0, 0, 30000);
    auto advance_only = decode(advance_bytes);
    target = base; work = 1000000;
    auto advance_status = shaped_painter::paint(advance_only, f.binding, target, 8 * 1000, 20 * 1000, {0, 0, 64, 48}, ink, work);
    check(advance_status == shaped_painter::Status::ok, "advance paint status=" + std::to_string(static_cast<int>(advance_status)));
    check(same_pixels(target.pixels, saved), "advance does not alter explicit pen position");
    auto shifted_bytes = packet_for(f.binding, 73, 1, f.gid, 0, 6000, 30000);
    auto shifted = decode(shifted_bytes);
    target = base; work = 1000000;
    check(shaped_painter::paint(shifted, f.binding, target, 8 * 1000, 20 * 1000, {0, 0, 64, 48}, ink, work) == shaped_painter::Status::ok, "offset paint");
    check(!same_pixels(target.pixels, saved), "offset shifts bitmap");
    std::cout << "shaped text painter tests passed\n"; return 0;
  } catch (const std::exception &e) { std::cerr << e.what(); return 1; }
}
