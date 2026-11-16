#include "render_core/shaped_text_binding.h"
#include "shaping_catalog.h"
#include "shaping_catalog_sha256.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

using namespace jellyframe;
using namespace jellyframe::shaped_text;
namespace {
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
  std::uint16_t gid = 0;
  explicit Fixture(const std::string &root, const char *face) {
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
    result.font_resource = font;
    result.shaping_catalog = catalog;
    result.shaping_companion = companion;
    result.normalization_tables = tables;
    result.normalization_eligibility = eligibility;
    return result;
  }
};
} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 2, "fixture root");
    Fixture fixture(argv[1], "regular");
    auto input = fixture.as_inputs();
    Binding binding;
    std::uint64_t work = 32U * 1024U * 1024U;
    check(create_binding(input, {}, work, binding) == Status::ok && binding.valid(), "factory");
    auto retained = binding;
    check(binding.resource_id() == 73 && binding.generation() == 1 && binding.selection() == 0, "binding metadata");
    const auto identity = binding.content_identity();
    check(std::any_of(identity.begin(), identity.end(), [](auto c) { return c != 0; }), "content identity");
    Binding unchanged = retained;
    auto bad_identity = input;
    bad_identity.eligibility_identity[0] ^= 1;
    work = 32U * 1024U * 1024U;
    const auto work_before_failure = work;
    check(create_binding(bad_identity, {}, work, unchanged) == Status::identity_mismatch &&
          work == work_before_failure &&
          unchanged.content_identity() == retained.content_identity(),
          "late factory refusal transaction");

    // Source buffers may disappear or change after creation; the handle owns exact copies.
    std::fill(fixture.font.begin(), fixture.font.end(), '\0');
    std::fill(fixture.catalog.begin(), fixture.catalog.end(), '\0');
    std::fill(fixture.companion.begin(), fixture.companion.end(), '\0');
    std::fill(fixture.tables.begin(), fixture.tables.end(), '\0');
    std::fill(fixture.eligibility.begin(), fixture.eligibility.end(), '\0');
    std::string packet = "retained output";
    Metrics metrics;
    work = 1000000;
    check(prepare(binding, 16000, "A", {}, work, packet, metrics) == Status::ok &&
          metrics.glyph_count == 1 && metrics.cluster_count == 1, "prepare");
    work = 1000000;
    check(validate_packet(binding, packet, {}, work) == Status::ok, "validate");

    auto reject_identity = [&](std::string altered, const char *case_name) {
      work = 1000000;
      if (validate_packet(binding, altered, {}, work) != Status::identity_mismatch)
        throw std::runtime_error(case_name);
    };
    auto altered = packet; altered[32] ^= 1; reject_identity(altered, "resource id");
    altered = packet; altered[40] ^= 2; reject_identity(altered, "generation");
    altered = packet; altered[48] ^= 1; reject_identity(altered, "digest");
    altered = packet; altered[28] = 1; reject_identity(altered, "selection");
    Limits tiny; tiny.packet_bytes = 1;
    work = 1000000;
    check(validate_packet(binding, packet, tiny, work) == Status::limit, "packet limit");
    auto work_before = work;
    Limits zero_limits; zero_limits.glyphs = 0;
    check(validate_packet(binding, packet, zero_limits, work) == Status::limit &&
          work == work_before, "zero validator limits preserve work");
    work = 1000000;
    Metrics retained_metrics = metrics;
    std::string retained_packet = packet;
    Limits no_scratch; no_scratch.scratch_bytes = 0;
    check(prepare(binding, 16000, "A", no_scratch, work, packet, metrics) == Status::limit &&
          work == 1000000 && packet == retained_packet &&
          metrics.glyph_count == retained_metrics.glyph_count,
          "scratch refusal transaction");

    auto bad_input = input;
    bad_input.resource_id = 0;
    work = 1000000;
    work_before = work;
    check(create_binding(bad_input, {}, work, unchanged) == Status::invalid &&
          unchanged.content_identity() == retained.content_identity() &&
          work == work_before, "factory transaction");

    Fixture bold(argv[1], "bold");
    auto bold_input = bold.as_inputs();
    Binding other;
    work = 32U * 1024U * 1024U;
    check(create_binding(bold_input, {}, work, other) == Status::ok &&
          other.resource_id() == binding.resource_id() &&
          other.generation() == binding.generation() &&
          other.content_identity() != binding.content_identity(), "collision fixture");
    work = 1000000;
    check(validate_packet(other, packet, {}, work) == Status::identity_mismatch, "cross-binding identity");
    std::cout << "shaped text binding API tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
