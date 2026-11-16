#include "../../src/shaping_binding.h"
#include "../../src/shaping_catalog_sha256.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

using namespace jellyframe;
namespace {
using Bytes = std::vector<std::uint8_t>;
void check(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
std::string load(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  check(bool(f), "open fixture");
  return {std::istreambuf_iterator<char>(f), {}};
}
void put16(Bytes &b, std::size_t p, unsigned v) {
  b[p] = static_cast<std::uint8_t>(v);
  b[p + 1] = static_cast<std::uint8_t>(v >> 8);
}
void put32(Bytes &b, std::size_t p, std::uint32_t v) {
  for (unsigned i = 0; i < 4; ++i)
    b[p + i] = static_cast<std::uint8_t>(v >> (i * 8));
}
std::uint32_t crc(const Bytes &b) {
  std::uint32_t v = ~0U;
  for (std::size_t p = 0; p < b.size(); ++p) {
    const auto c = p >= 96 && p < 100 ? 0U : b[p];
    v ^= c;
    for (unsigned i = 0; i < 8; ++i)
      v = (v >> 1) ^ (0xedb88320U & (0U - (v & 1U)));
  }
  return ~v;
}
Bytes font_bytes(const shaping_catalog::Metrics &m, std::uint32_t cp,
                 const FontGlyphView &g, unsigned mutation = 0) {
  const unsigned width = g.width + (mutation == 7 ? 1U : 0U);
  const unsigned height = mutation == 6 && g.height ? g.height - 1 : g.height;
  const unsigned bits = mutation == 11 ? 8U : 4U;
  const unsigned stride = (width * bits + 7) / 8;
  Bytes coverage(stride * height, 0);
  if (g.coverage_bytes) {
    const unsigned original_stride = g.row_stride;
    for (unsigned y = 0; y < std::min<unsigned>(height, g.height); ++y)
      std::copy_n(g.coverage + y * original_stride,
                  std::min<unsigned>(stride, original_stride),
                  coverage.begin() + y * stride);
  }
  if (bits == 4 && (width & 1U))
    for (unsigned y = 0; y < height; ++y) coverage[(y + 1) * stride - 1] &= 0xf0;
  if (mutation == 10 && !coverage.empty()) coverage[0] ^= 0x10;
  Bytes b(168 + coverage.size(), 0);
  std::memcpy(b.data(), "JFFONT2", 8);
  put16(b, 8, 2); put16(b, 10, 128); put16(b, 12, 40); put16(b, 14, 1);
  put32(b, 16, static_cast<std::uint32_t>(b.size())); put32(b, 20, 1);
  put32(b, 24, 128); put32(b, 28, 168); put32(b, 32, static_cast<std::uint32_t>(coverage.size()));
  put16(b, 36, m.units_per_em); put16(b, 38, m.raster_pixel_size);
  put16(b, 40, m.weight); b[42] = static_cast<std::uint8_t>(bits);
  put32(b, 44, static_cast<std::uint32_t>(m.ascent));
  put32(b, 48, static_cast<std::uint32_t>(m.descent));
  put32(b, 52, static_cast<std::uint32_t>(m.line_gap));
  std::memcpy(b.data() + 64, m.source_sha256.data(), 32);
  put32(b, 100, 1); put16(b, 104, 1);
  put32(b, 128, cp);
  put32(b, 132, mutation == 2 ? g.glyph_id + 1 : g.glyph_id);
  put32(b, 136, static_cast<std::uint32_t>(g.advance + (mutation == 3 ? 1 : 0)));
  put32(b, 140, static_cast<std::uint32_t>(g.bearing_x + (mutation == 4 ? 1 : 0)));
  put32(b, 144, static_cast<std::uint32_t>(g.bearing_y + (mutation == 5 ? 1 : 0)));
  put16(b, 148, width); put16(b, 150, height);
  put32(b, 152, stride); put32(b, 156, 0); put32(b, 160, static_cast<std::uint32_t>(coverage.size()));
  std::copy(coverage.begin(), coverage.end(), b.begin() + 168);
  if (mutation == 1) put32(b, 128, cp + 1);
  put32(b, 96, crc(b));
  return b;
}
} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 2, "fixture root argument");
    const std::string root = argv[1];
    auto cat_bytes = load(root + "/shaping_executor_catalog/regular.jfcat1");
    auto companion = load(root + "/shaping_resource/regular.jfshr2");
    auto table_bytes = load(root + "/shaping_normalization/unicode14.bin");
    auto eligibility_bytes = load(root + "/shaping_normalization/regular.eligibility");
    shaping_catalog::Identity id;
    std::memcpy(id.shaping.source, cat_bytes.data() + 16, 32);
    std::memcpy(id.shaping.profile, cat_bytes.data() + 48, 32);
    id.shaping.glyph_count = 30900; id.weight = 400;
    id.trusted_companion = companion;
    id.companion_sha256 = shaping_catalog::sha256(companion);
    id.catalog_sha256 = shaping_catalog::sha256(cat_bytes);
    shaping_catalog::View catalog;
    std::uint64_t work = 100000000;
    check(shaping_catalog::bind(cat_bytes, id, {}, work, catalog) == shaping_catalog::Status::ok, "catalog");
    shaping_normalization::Tables tables;
    work = 100000000;
    check(shaping_normalization::validate_tables(table_bytes, shaping_catalog::sha256(table_bytes), {}, work, tables) == shaping_normalization::Status::ok, "tables");
    shaping_normalization::Eligibility eligibility;
    work = 100000000;
    check(shaping_normalization::validate_eligibility(eligibility_bytes, shaping_catalog::sha256(eligibility_bytes), tables, catalog, work, eligibility) == shaping_normalization::Status::ok, "eligibility");
    std::uint16_t gid = 0;
    check(catalog.map_scalar('A', true, gid), "A mapping");
    FontGlyphView raster;
    check(catalog.find_gid(gid, raster), "A raster");
    auto valid_bytes = font_bytes(catalog.metadata(), 'A', raster);
    FontResource font;
    check(font.load(valid_bytes.data(), valid_bytes.size()) == FontResourceStatus::Loaded, "font fixture");
    shaping_binding::BindingState state;
    shaping_binding::Snapshot accepted;
    check(state.bind(catalog, font, tables, eligibility, 1, accepted) == shaping_binding::Status::ok, "matching cross-bind");
    // The catalog includes hundreds of closure GIDs not referenced by this
    // one-scalar JFFONT2; their independent presence must remain admissible.
    check(catalog.bytes().size() > 0 && accepted.valid(), "closure GID boundary");
    auto retained = accepted;
    for (unsigned mutation : {1U, 2U, 3U, 4U, 5U, 6U, 7U, 10U, 11U}) {
      auto bad_bytes = font_bytes(catalog.metadata(), 'A', raster, mutation);
      FontResource bad;
      check(bad.load(bad_bytes.data(), bad_bytes.size()) == FontResourceStatus::Loaded, "mutated font remains valid");
      shaping_binding::Snapshot refused;
      check(state.bind(catalog, bad, tables, eligibility, 2, refused) == shaping_binding::Status::invalid &&
            state.generation() == 1 && accepted.generation() == retained.generation() && !refused.valid(),
            "field mismatch preserves snapshot");
    }
    std::cout << "shaping binding tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
