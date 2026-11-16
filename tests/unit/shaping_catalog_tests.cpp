#include "../../src/shaping_catalog.h"
#include "../../src/shaping_catalog_sha256.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
using namespace jellyframe;
namespace {
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
std::string load(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  check(bool(f), "open");
  return {std::istreambuf_iterator<char>(f), {}};
}
unsigned read(const std::string &b, std::size_t p, unsigned n) {
  unsigned v = 0;
  for (unsigned i = 0; i < n; ++i)
    v |= unsigned(static_cast<unsigned char>(b[p + i])) << (8 * i);
  return v;
}
void put(std::string &b, std::size_t p, unsigned v, unsigned n) {
  for (unsigned i = 0; i < n; ++i)
    b[p + i] = char(v >> (8 * i));
}
void checksum(std::string &b) {
  unsigned v = ~0U;
  for (std::size_t p = 172; p < b.size(); ++p) {
    v ^= static_cast<unsigned char>(b[p]);
    for (unsigned i = 0; i < 8; ++i)
      v = (v >> 1) ^ (0xedb88320U & (0U - (v & 1U)));
  }
  put(b, 156, ~v, 4);
}
} // namespace
int main(int argc, char **argv) {
  try {
    check(argc == 3, "arguments");
    auto digest_hex = [](std::string_view value) {
      auto hash = shaping_catalog::sha256(value);
      std::string text;
      for (auto c : hash) {
        text += "0123456789abcdef"[c >> 4];
        text += "0123456789abcdef"[c & 15];
      }
      return text;
    };
    check(
        digest_hex("") ==
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "sha empty");
    check(
        digest_hex("abc") ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "sha abc");
    check(
        digest_hex(std::string(1000000, 'a')) ==
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
        "sha million");
    check(
        digest_hex(std::string(55, 'a')) ==
            "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318",
        "sha boundary 55");
    check(
        digest_hex(std::string(56, 'a')) ==
            "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a",
        "sha boundary 56");
    check(
        digest_hex(std::string(63, 'a')) ==
            "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34",
        "sha boundary 63");
    check(
        digest_hex(std::string(64, 'a')) ==
            "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb",
        "sha boundary 64");
    for (const char *face : {"regular", "medium", "bold"}) {
      auto bytes = load(std::string(argv[1]) + "/" + face + ".jfcat1");
      auto companion = load(std::string(argv[2]) + "/" + face + ".jfshr2");
      shaping_catalog::Identity id;
      std::memcpy(id.shaping.source, bytes.data() + 16, 32);
      std::memcpy(id.shaping.profile, bytes.data() + 48, 32);
      std::memcpy(id.companion_sha256.data(), bytes.data() + 80, 32);
      id.shaping.glyph_count = 30900;
      id.weight = static_cast<std::uint16_t>(read(bytes, 132, 2));
      id.trusted_companion = companion;
      id.catalog_sha256 = shaping_catalog::sha256(bytes);
      shaping_catalog::View view;
      shaping_catalog::Limits limits;
      std::uint64_t work = 10000000;
      check(shaping_catalog::bind(bytes, id, limits, work, view) ==
                shaping_catalog::Status::ok,
            "valid bind");
      std::uint16_t gid = 0;
      check(view.map_scalar('A', true, gid), "raw A");
      FontGlyphView glyph;
      check(view.find_gid(gid, glyph), "original gid");
      check(glyph.advance > 0, "advance");
      check(!view.map_scalar(0x0302, false, gid), "unmapped intermediate");
      auto retained = view.bytes().data();
      const auto retained_metrics = view.metadata();
      auto reject_geometry = [&](std::string altered, const char *message) {
        checksum(altered);
        auto altered_id = id;
        altered_id.catalog_sha256 = shaping_catalog::sha256(altered);
        work = 10000000;
        check(shaping_catalog::bind(altered, altered_id, limits, work, view) ==
                  shaping_catalog::Status::malformed,
              message);
        const auto current = view.metadata();
        check(view.bytes() == bytes && view.bytes().data() == retained &&
                  view.companion_bytes() == companion &&
                  view.companion_bytes().data() == companion.data() &&
                  current.units_per_em == retained_metrics.units_per_em &&
                  current.raster_pixel_size ==
                      retained_metrics.raster_pixel_size &&
                  current.weight == retained_metrics.weight &&
                  current.ascent == retained_metrics.ascent &&
                  current.descent == retained_metrics.descent &&
                  current.line_gap == retained_metrics.line_gap &&
                  current.coverage_bits == retained_metrics.coverage_bits &&
                  current.source_sha256 == retained_metrics.source_sha256,
              "geometry refusal preserves binding");
      };
      const auto raster_start = 172 + read(bytes, 116, 4) * 8;
      const auto pool_start = raster_start + read(bytes, 120, 4) * 32;
      auto ink = raster_start;
      while (!read(bytes, ink + 16, 2))
        ink += 32;
      auto altered = bytes;
      put(altered, ink + 20, read(bytes, ink + 20, 4) + 1, 4);
      reject_geometry(altered, "stride mismatch");
      altered = bytes;
      put(altered, ink + 24, 0xffffffffU, 4);
      reject_geometry(altered, "coverage offset overflow");
      auto later_ink = ink + 32;
      while (!read(bytes, later_ink + 28, 4))
        later_ink += 32;
      altered = bytes;
      put(altered, later_ink + 24, 0, 4);
      reject_geometry(altered, "coverage overlap");
      auto odd = raster_start;
      while (!(read(bytes, odd + 16, 2) & 1U))
        odd += 32;
      altered = bytes;
      altered[pool_start + read(bytes, odd + 24, 4) + read(bytes, odd + 20, 4) -
              1] |= 1;
      reject_geometry(altered, "odd width padding");
      altered = bytes;
      put(altered, 176, 0, 2);
      reject_geometry(altered, "missing mapping target");
      altered = bytes;
      put(altered, 180, read(bytes, 172, 4), 4);
      reject_geometry(altered, "duplicate scalar order");

      for (std::size_t length :
           {std::size_t(0), std::size_t(171), bytes.size() - 1}) {
        work = 10000000;
        check(shaping_catalog::bind(std::string_view(bytes).substr(0, length),
                                    id, limits, work,
                                    view) != shaping_catalog::Status::ok,
              "truncation");
        check(view.bytes().data() == retained, "transaction");
      }
      auto bad = bytes;
      bad[16] ^= 1;
      work = 10000000;
      check(shaping_catalog::bind(bad, id, limits, work, view) ==
                shaping_catalog::Status::identity_mismatch,
            "identity");
      bad = bytes;
      bad[178] = char(4);
      checksum(bad);
      id.catalog_sha256 = shaping_catalog::sha256(bad);
      work = 10000000;
      check(shaping_catalog::bind(bad, id, limits, work, view) ==
                shaping_catalog::Status::malformed,
            "flags");
      id.catalog_sha256 = shaping_catalog::sha256(bytes);
      work = 0;
      check(shaping_catalog::bind(bytes, id, limits, work, view) ==
                shaping_catalog::Status::limit,
            "work");
      bad = bytes;
      const auto base = 172 + read(bytes, 116, 4) * 8;
      const auto count = read(bytes, 120, 4);
      put(bad, base + (count - 1) * 32, 30900, 2);
      checksum(bad);
      id.catalog_sha256 = shaping_catalog::sha256(bad);
      work = 10000000;
      check(shaping_catalog::bind(bad, id, limits, work, view) ==
                shaping_catalog::Status::malformed,
            "gid ceiling");
      id.catalog_sha256 = shaping_catalog::sha256(bytes);
      auto corrupt_companion = companion;
      corrupt_companion.back() ^= 1;
      id.trusted_companion = corrupt_companion;
      work = 10000000;
      check(shaping_catalog::bind(bytes, id, limits, work, view) ==
                shaping_catalog::Status::identity_mismatch,
            "whole companion digest");
      check(view.bytes().data() == retained, "digest refusal transaction");
      id.trusted_companion = companion;
      work = bytes.size() * 2 + companion.size() - 1;
      check(shaping_catalog::bind(bytes, id, limits, work, view) ==
                shaping_catalog::Status::limit,
            "companion digest budget boundary");
      // Repair both checksums and independently expected hashes to exercise
      // semantic closure admission, beyond accidental corruption and identity
      // checks.
      auto changed_companion = companion;
      bool changed = false;
      std::uint16_t absent = 1;
      while (view.find_gid(absent, glyph))
        ++absent;
      const auto dirs =
          112 + read(companion, 80, 4) * 16 + read(companion, 84, 4) * 8;
      for (unsigned i = 0; i < read(companion, 88, 4) && !changed; ++i) {
        const auto d = dirs + i * 16;
        if (read(companion, d, 1) != 1 || read(companion, d + 1, 1) != 1)
          continue;
        auto p = read(companion, d + 8, 4);
        for (unsigned st = 0; st < read(companion, d + 2, 2) && !changed;
             ++st) {
          const auto end = p + 6 + read(companion, p + 2, 4);
          p += 6;
          auto rows = read(companion, p, 4);
          p += 4;
          for (unsigned row = 0; row < rows; ++row) {
            if (view.find_gid(static_cast<std::uint16_t>(read(companion, p, 2)),
                              glyph)) {
              put(changed_companion, p + 2, absent, 2);
              changed = true;
              break;
            }
            p += 4;
          }
          p = end;
        }
      }
      check(changed, "closure mutation source");
      unsigned companion_crc = ~0U;
      for (std::size_t p = 112; p < changed_companion.size(); ++p) {
        companion_crc ^= static_cast<unsigned char>(changed_companion[p]);
        for (unsigned i = 0; i < 8; ++i)
          companion_crc = (companion_crc >> 1) ^
                          (0xedb88320U & (0U - (companion_crc & 1U)));
      }
      put(changed_companion, 96, ~companion_crc, 4);
      id.trusted_companion = changed_companion;
      id.companion_sha256 = shaping_catalog::sha256(changed_companion);
      bad = bytes;
      std::memcpy(bad.data() + 80, id.companion_sha256.data(), 32);
      id.catalog_sha256 = shaping_catalog::sha256(bad);
      work = 10000000;
      check(shaping_catalog::bind(bad, id, limits, work, view) ==
                shaping_catalog::Status::incomplete_closure,
            "complete closure");
      id.trusted_companion = companion;
      std::memcpy(id.companion_sha256.data(), bytes.data() + 80, 32);
      id.catalog_sha256 = shaping_catalog::sha256(bytes);
      auto unaligned = std::string("x") + bytes;
      work = 10000000;
      check(shaping_catalog::bind(std::string_view(unaligned).substr(1), id,
                                  limits, work,
                                  view) == shaping_catalog::Status::ok,
            "unaligned");
    }
    std::cout << "shaping catalog tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
