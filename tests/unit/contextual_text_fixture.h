#pragma once
#include "render_core/contextual_text.h"
#include "shaping_catalog.h"
#include "shaping_catalog_sha256.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
using namespace jellyframe;
using namespace jellyframe::contextual_text;
namespace shaped_binding_fixture {
using Bytes = std::vector<std::uint8_t>;
void check(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
std::string load(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  check(bool(f), "fixture open");
  return {std::istreambuf_iterator<char>(f), {}};
}
void put16(Bytes &b, std::size_t p, unsigned v) {
  b[p] = static_cast<std::uint8_t>(v);
  b[p + 1] = static_cast<std::uint8_t>(v >> 8);
}
void put32(Bytes &b, std::size_t p, std::uint32_t v) {
  for (unsigned i = 0; i < 4; ++i) b[p + i] = std::uint8_t(v >> (8 * i));
}
std::uint32_t crc(const Bytes &b) {
  std::uint32_t c = ~0U;
  for (std::size_t p = 0; p < b.size(); ++p) {
    c ^= p >= 96 && p < 100 ? 0U : b[p];
    for (unsigned i = 0; i < 8; ++i) c = (c >> 1) ^ (0xedb88320U & (0U - (c & 1U)));
  }
  return ~c;
}
Bytes make_font(const shaping_catalog::Metrics &m, std::uint16_t gid,
                const FontGlyphView &g) {
  Bytes b(168 + g.coverage_bytes, 0);
  std::memcpy(b.data(), "JFFONT2", 8);
  put16(b, 8, 2); put16(b, 10, 128); put16(b, 12, 40); put16(b, 14, 1);
  put32(b, 16, static_cast<std::uint32_t>(b.size())); put32(b, 20, 1);
  put32(b, 24, 128); put32(b, 28, 168); put32(b, 32, static_cast<std::uint32_t>(g.coverage_bytes));
  put16(b, 36, m.units_per_em); put16(b, 38, m.raster_pixel_size);
  put16(b, 40, m.weight); b[42] = 4;
  put32(b, 44, static_cast<std::uint32_t>(m.ascent));
  put32(b, 48, static_cast<std::uint32_t>(m.descent));
  put32(b, 52, static_cast<std::uint32_t>(m.line_gap));
  std::memcpy(b.data() + 64, m.source_sha256.data(), 32);
  put32(b, 100, 1); put16(b, 104, 1);
  put32(b, 128, 'A'); put32(b, 132, gid); put32(b, 136, g.advance);
  put32(b, 140, static_cast<std::uint32_t>(g.bearing_x));
  put32(b, 144, static_cast<std::uint32_t>(g.bearing_y));
  put16(b, 148, g.width); put16(b, 150, g.height); put32(b, 152, g.row_stride);
  put32(b, 160, static_cast<std::uint32_t>(g.coverage_bytes));
  std::copy_n(g.coverage, g.coverage_bytes, b.data() + 168);
  put32(b, 96, crc(b));
  return b;
}
struct Fixture {
  std::string font, catalog, companion, tables, eligibility;
  Inputs inputs;
  Digest font_identity{};
  std::uint16_t gid = 0;
  explicit Fixture(const std::string &root, const char *face, bool complete_font = false) {
    const std::string name = face;
    catalog = load(root + "/shaping_executor_catalog/" + name + ".jfcat1");
    companion = load(root + "/shaping_resource/" + name + ".jfshr2");
    tables = load(root + "/shaping_normalization/unicode14.bin");
    eligibility = load(root + "/shaping_normalization/" + name + ".eligibility");
    const auto metadata_source = std::string_view(catalog).substr(16, 32);
    const auto metadata_profile = std::string_view(catalog).substr(48, 32);
    shaping_catalog::Identity identity;
    std::memcpy(identity.shaping.source, metadata_source.data(), 32);
    std::memcpy(identity.shaping.profile, metadata_profile.data(), 32);
    identity.shaping.glyph_count = 30900;
    identity.trusted_companion = companion;
    identity.companion_sha256 = shaping_catalog::sha256(companion);
    identity.catalog_sha256 = shaping_catalog::sha256(catalog);
    identity.weight = name == "bold" ? 700 : name == "medium" ? 500 : 400;
    shaping_catalog::View view;
    std::uint64_t work = 100000000;
    check(shaping_catalog::bind(catalog, identity, {}, work, view) == shaping_catalog::Status::ok, "catalog bind");
    check(view.map_scalar('A', true, gid), "A map");
    FontGlyphView glyph;
    check(view.find_gid(gid, glyph), "A raster");
    const auto font_data = make_font(view.metadata(), gid, glyph);
    font.assign(reinterpret_cast<const char *>(font_data.data()), font_data.size());
    if (complete_font) font = load(root + "/contextual_font/" + name + ".jffont2");
    font_identity = shaping_catalog::sha256(font);
    std::memcpy(inputs.source_identity.data(), metadata_source.data(), 32);
    std::memcpy(inputs.profile_identity.data(), metadata_profile.data(), 32);
    inputs.catalog_identity = shaping_catalog::sha256(catalog);
    inputs.companion_identity = shaping_catalog::sha256(companion);
    inputs.tables_identity = shaping_catalog::sha256(tables);
    inputs.eligibility_identity = shaping_catalog::sha256(eligibility);
    inputs.shaping_glyph_count = 30900;
    inputs.resource_id = 73;
    inputs.raster_size = 16;
    inputs.weight = identity.weight;
    inputs.selection = 0;
  }
  Inputs as_inputs() const {
    auto result = inputs;
    result.font_resource_identity = font_identity;
    result.font_resource = font;
    result.shaping_catalog = catalog;
    result.shaping_companion = companion;
    result.normalization_tables = tables;
    result.normalization_eligibility = eligibility;
    return result;
  }
};
} // namespace
