#include "../../src/shaped_text_service.h"
#include "../../src/shaping_catalog_sha256.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>
using namespace jellyframe;
namespace {
using FontBytes = std::vector<std::uint8_t>;
void put16(FontBytes &b, std::size_t at, unsigned v) {
  b[at] = static_cast<std::uint8_t>(v);
  b[at + 1] = static_cast<std::uint8_t>(v >> 8);
}
void put32(FontBytes &b, std::size_t at, std::uint32_t v) {
  for (unsigned i = 0; i < 4; ++i)
    b[at + i] = static_cast<std::uint8_t>(v >> (i * 8));
}
FontBytes font_resource(const shaping_catalog::Metrics &m) {
  FontBytes b(168, 0);
  const char magic[] = "JFFONT2";
  std::memcpy(b.data(), magic, 8);
  put16(b, 8, 2); put16(b, 10, 128); put16(b, 12, 40); put16(b, 14, 1);
  put32(b, 16, static_cast<std::uint32_t>(b.size()));
  put32(b, 20, 1); put32(b, 24, 128); put32(b, 28, 168); put32(b, 32, 0);
  put16(b, 36, m.units_per_em); put16(b, 38, m.raster_pixel_size);
  put16(b, 40, m.weight); b[42] = m.coverage_bits;
  put32(b, 44, static_cast<std::uint32_t>(m.ascent));
  put32(b, 48, static_cast<std::uint32_t>(m.descent));
  put32(b, 52, static_cast<std::uint32_t>(m.line_gap));
  std::memcpy(b.data() + 64, m.source_sha256.data(), 32);
  put32(b, 100, 1); put16(b, 104, 1);
  put32(b, 128, 32); put32(b, 132, 1); put32(b, 136, 250);
  std::uint32_t crc = 0xffffffffU;
  for (auto value : b) {
    crc ^= value;
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
  }
  put32(b, 96, ~crc);
  return b;
}
void check(bool value, const std::string &message) {
  if (!value) throw std::runtime_error(message);
}
std::string load(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  check(bool(file), path);
  return {std::istreambuf_iterator<char>(file), {}};
}
std::int32_t scaled(std::int64_t value, int pixels, unsigned divisor) {
  // Independent reference uses quotient/remainder, avoiding production's bias.
  auto product = value * pixels;
  auto quotient = product / divisor;
  auto remainder = product % divisor;
  if (remainder * 2 >= divisor) ++quotient;
  if (-remainder * 2 >= divisor) --quotient;
  return static_cast<std::int32_t>(quotient);
}
std::string unhex(const std::string &text) {
  std::string result;
  for (std::size_t i = 0; i < text.size(); i += 2)
    result += static_cast<char>(std::stoul(text.substr(i, 2), nullptr, 16));
  return result;
}
}
int main(int argc, char **argv) {
  try {
    check(argc == 2, "fixture root argument");
    const std::string root = argv[1];
    auto table_bytes = load(root + "/shaping_normalization/unicode14.bin");
    shaping_normalization::Tables tables;
    std::uint64_t work = 100000000;
    check(shaping_normalization::validate_tables(table_bytes,
        shaping_catalog::sha256(table_bytes), {}, work, tables) ==
        shaping_normalization::Status::ok, "tables");
    unsigned cases = 0;
    std::ifstream raw(root + "/shaped_text_service/raw-inputs.txt");
    std::string name, hex;
    while (raw >> name >> hex) {
      auto face = name.substr(0, name.find('-'));
      auto source = unhex(hex);
      auto catalog_bytes = load(root + "/shaping_executor_catalog/" + face + ".jfcat1");
      auto companion = load(root + "/shaping_resource/" + face + ".jfshr2");
      auto eligibility_bytes = load(root + "/shaping_normalization/" + face + ".eligibility");
      shaping_catalog::Identity identity;
      std::memcpy(identity.shaping.source, catalog_bytes.data() + 16, 32);
      std::memcpy(identity.shaping.profile, catalog_bytes.data() + 48, 32);
      identity.shaping.glyph_count = 30900;
      identity.weight = face == "bold" ? 700 : face == "medium" ? 500 : 400;
      identity.trusted_companion = companion;
      identity.companion_sha256 = shaping_catalog::sha256(companion);
      identity.catalog_sha256 = shaping_catalog::sha256(catalog_bytes);
      shaping_catalog::View catalog;
      work = 100000000;
      check(shaping_catalog::bind(catalog_bytes, identity, {}, work, catalog) ==
          shaping_catalog::Status::ok, name + " catalog");
      shaping_normalization::Eligibility eligibility;
      check(shaping_normalization::validate_eligibility(eligibility_bytes,
          shaping_catalog::sha256(eligibility_bytes), tables, catalog, work,
          eligibility) == shaping_normalization::Status::ok, name + " eligibility");
      auto font_bytes = font_resource(catalog.metadata());
      FontResource font;
      check(font.load(font_bytes.data(), font_bytes.size()) ==
          FontResourceStatus::Loaded, name + " font resource");
      shaping_binding::BindingState binding_state;
      shaping_binding::Snapshot binding;
      check(binding_state.bind(catalog, font, tables, eligibility, 42, binding) ==
          shaping_binding::Status::ok && binding.valid() &&
          binding.resource_id() == 42 && binding.generation() == 1,
          name + " binding");
      auto bad_font_metadata = catalog.metadata();
      ++bad_font_metadata.weight;
      auto bad_font_bytes = font_resource(bad_font_metadata);
      FontResource bad_font;
      check(bad_font.load(bad_font_bytes.data(), bad_font_bytes.size()) ==
          FontResourceStatus::Loaded, name + " mismatched font setup");
      shaping_binding::Snapshot mismatched_font;
      check(binding_state.bind(catalog, bad_font, tables, eligibility, 45,
                               mismatched_font) == shaping_binding::Status::invalid &&
          binding_state.generation() == 1 && !mismatched_font.valid(),
          name + " reject mismatched font metadata");
      auto retained_binding = binding;
      shaping_binding::Snapshot refused_binding;
      check(binding_state.bind(catalog, font, tables, eligibility, 0,
                               refused_binding) == shaping_binding::Status::invalid &&
          binding_state.generation() == 1 && binding.generation() == 1 &&
          !refused_binding.valid(), name + " binding refusal rollback");
      shaping_binding::Snapshot replacement_binding;
      check(binding_state.bind(catalog, font, tables, eligibility, 43,
                               replacement_binding) == shaping_binding::Status::ok &&
          replacement_binding.generation() == 2 &&
          retained_binding.generation() == 1 &&
          retained_binding.resource_id() == 42,
          name + " immutable retained binding");
      if (face == "regular") {
        auto other_bytes = load(root + "/shaping_executor_catalog/bold.jfcat1");
        auto other_companion = load(root + "/shaping_resource/bold.jfshr2");
        shaping_catalog::Identity other_identity;
        std::memcpy(other_identity.shaping.source, other_bytes.data() + 16, 32);
        std::memcpy(other_identity.shaping.profile, other_bytes.data() + 48, 32);
        other_identity.shaping.glyph_count = 30900;
        other_identity.weight = 700;
        other_identity.trusted_companion = other_companion;
        other_identity.companion_sha256 = shaping_catalog::sha256(other_companion);
        other_identity.catalog_sha256 = shaping_catalog::sha256(other_bytes);
        shaping_catalog::View other_catalog;
        work = 100000000;
        check(shaping_catalog::bind(other_bytes, other_identity, {}, work,
                                    other_catalog) == shaping_catalog::Status::ok,
              "alternate catalog setup");
        auto other_font_bytes = font_resource(other_catalog.metadata());
        FontResource other_font;
        check(other_font.load(other_font_bytes.data(), other_font_bytes.size()) ==
              FontResourceStatus::Loaded, "alternate font setup");
        shaping_binding::Snapshot mismatched;
        check(binding_state.bind(other_catalog, other_font, tables, eligibility, 44,
                                 mismatched) == shaping_binding::Status::invalid &&
              !mismatched.valid() && binding_state.generation() == 2,
              "reject eligibility from another catalog");
      }
      std::ifstream oracle(root + "/shaping_executor/" + name);
      unsigned selection, inputs, outputs;
      oracle >> selection >> inputs >> outputs;
      unsigned ignored;
      for (unsigned i = 0; i < inputs; ++i) oracle >> ignored >> ignored >> ignored;
      std::vector<shaping_lookup::Glyph> expected(outputs);
      for (auto &g : expected)
        oracle >> g.gid >> g.advance_x >> g.advance_y >> g.offset_x >> g.offset_y
               >> g.source_begin >> g.source_end;
      std::vector<shaping_normalization::Scalar> ns(16384), normalized(16384);
      std::vector<shaping_lookup::Glyph> input(16384), scratch(16384), positioned(16384);
      std::vector<shaped_packet::Glyph> glyphs(16384);
      std::vector<shaped_packet::Cluster> clusters(16384);
      shaped_service::Buffers buffers{ns.data(), normalized.data(), ns.size(),
          normalized.size(), input.data(), scratch.data(), positioned.data(),
          input.size(), glyphs.data(), clusters.data(), clusters.size()};
      shaped_service::Request request{&binding, 16000,
          static_cast<std::uint16_t>(selection)};
      for (int pixels : {16000, 20000, 24000, 17750}) {
        request.size_q1000 = pixels;
        std::string packet = "previous packet";
        shaped_service::Metrics metrics;
        work = 100000000;
        auto status = shaped_service::prepare(request, source, buffers, {}, work,
                                              packet, metrics);
        check(status == shaped_service::Status::ok,
              name + " prepare status " + std::to_string(int(status)));
        shaped_packet::View decoded;
        check(shaped_packet::decode(packet, {}, work, decoded) ==
            shaped_packet::Status::ok, name + " decode");
        check(decoded.source() == source && decoded.glyph_count() == outputs,
              name + " raw source and glyph count");
        check(decoded.header().resource_id == binding.resource_id() &&
              decoded.header().binding_generation == binding.generation(),
              name + " packet identity comes from binding");
        auto metadata = catalog.metadata();
        std::int64_t pen_x = 0, pen_y = 0;
        for (unsigned i = 0; i < outputs; ++i) {
          shaped_packet::Glyph g;
          check(decoded.glyph(i, g), "glyph");
          auto &e = expected[i];
          check(g.gid == e.gid &&
              g.x == scaled(pen_x, pixels, metadata.units_per_em) &&
              g.y == scaled(-pen_y, pixels, metadata.units_per_em) &&
              g.advance_x == scaled(e.advance_x, pixels, metadata.units_per_em) &&
              g.advance_y == scaled(-std::int64_t(e.advance_y), pixels, metadata.units_per_em) &&
              g.offset_x == scaled(e.offset_x, pixels, metadata.units_per_em) &&
              g.offset_y == scaled(-std::int64_t(e.offset_y), pixels, metadata.units_per_em),
              name + " independently scaled HB geometry glyph " + std::to_string(i));
          shaped_packet::Cluster cluster;
          check(decoded.cluster(g.cluster, cluster) &&
              cluster.source_begin == e.source_begin && cluster.source_end == e.source_end,
              name + " original source cluster glyph " + std::to_string(i));
          pen_x += e.advance_x;
          pen_y += e.advance_y;
        }
        check(metrics.advance_x == scaled(pen_x, pixels, metadata.units_per_em) &&
            metrics.advance_y == scaled(-pen_y, pixels, metadata.units_per_em) &&
            metrics.ascent == scaled(metadata.ascent, pixels, metadata.units_per_em) &&
            metrics.descent == scaled(metadata.descent, pixels, metadata.units_per_em) &&
            metrics.line_gap == scaled(metadata.line_gap, pixels, metadata.units_per_em),
            name + " metrics");
        const auto prior = packet;
        const auto prior_metrics = metrics;
        auto refusal = [&](std::string_view text, shaped_service::Buffers spans,
                           shaped_service::Limits limits, std::uint64_t budget) {
          work = budget;
          check(shaped_service::prepare(request, text, spans, limits, work, packet,
              metrics) != shaped_service::Status::ok, name + " refusal");
          check(packet == prior && std::memcmp(&metrics, &prior_metrics, sizeof(metrics)) == 0,
                name + " atomic rollback");
        };
        refusal(source, buffers, {}, 0);
        refusal(std::string_view("\xc0\x80", 2), buffers, {}, 100000000);
        auto alias = buffers;
        alias.scratch = alias.input;
        refusal(source, alias, {}, 100000000);
        // The mutable budget must never be stored inside a scratch record:
        // later stages write these records while continuing to spend work.
        input[0].advance_x = 100000000;
        check(shaped_service::prepare(request, source, buffers, {},
              *reinterpret_cast<std::uint64_t *>(input.data()), packet, metrics) ==
              shaped_service::Status::overlap, name + " budget/scratch overlap");
        check(packet == prior && std::memcmp(&metrics, &prior_metrics, sizeof(metrics)) == 0,
              name + " budget overlap rollback");
        auto packet_alias = buffers;
        packet_alias.input = reinterpret_cast<shaping_lookup::Glyph *>(&packet);
        refusal(source, packet_alias, {}, 100000000);
        auto request_alias = buffers;
        request_alias.input = reinterpret_cast<shaping_lookup::Glyph *>(&request);
        refusal(source, request_alias, {}, 100000000);
        auto small = buffers;
        small.cluster_capacity = 0;
        refusal(source, small, {}, 100000000);
        shaped_service::Limits bounded;
        bounded.packet.bytes = 1;
        refusal(source, buffers, bounded, 100000000);
      }
      std::string empty_packet = "old";
      shaped_service::Metrics empty_metrics;
      empty_metrics.glyph_count = 37;
      work = 100000000;
      check(shaped_service::prepare(request, {}, buffers, {}, work,
            empty_packet, empty_metrics) == shaped_service::Status::ok,
            name + " empty run");
      shaped_packet::View empty_view;
      check(shaped_packet::decode(empty_packet, {}, work, empty_view) ==
            shaped_packet::Status::ok && empty_view.source().empty() &&
            empty_view.glyph_count() == 0 && empty_metrics.glyph_count == 0 &&
            empty_metrics.cluster_count == 0, name + " empty output");
      ++cases;
    }
    check(cases == 30, "all thirty raw HB cases");
    std::cout << cases << " raw HB cases x four sizes, atomic rollback passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
