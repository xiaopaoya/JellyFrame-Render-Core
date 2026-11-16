#include "../../src/shaping_catalog_sha256.h"
#include "../../src/shaping_lookup_executor.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <vector>
using namespace jellyframe;
shaping_catalog::View bind_synthetic(const std::string &bytes,
                                     const std::string &cat) {
  shaping_catalog::Identity trusted{};
  trusted.shaping.glyph_count = 30900;
  std::memcpy(trusted.shaping.source, bytes.data() + 16, 32);
  std::memcpy(trusted.shaping.profile, bytes.data() + 48, 32);
  trusted.trusted_companion = bytes;
  trusted.companion_sha256 = shaping_catalog::sha256(bytes);
  trusted.catalog_sha256 = shaping_catalog::sha256(cat);
  shaping_catalog::View view;
  std::uint64_t work = 100000000;
  if (shaping_catalog::bind(cat, trusted, {}, work, view) !=
      shaping_catalog::Status::ok)
    std::abort();
  return view;
}
int main(int argc, char **argv) {
  if (argc != 4)
    return 2;
  unsigned cases = 0;
  for (auto &entry : std::filesystem::directory_iterator(argv[2])) {
    auto name = entry.path().filename().string();
    if (entry.path().extension() != ".txt" ||
        name.find("advances") != std::string::npos)
      continue;
    auto face = name.substr(0, name.find('-'));
    std::ifstream f(std::string(argv[1]) + "/" + face + ".jfshr2",
                    std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(f)), {});
    shaping_resource::Identity id{};
    std::memcpy(id.source, bytes.data() + 16, 32);
    std::memcpy(id.profile, bytes.data() + 48, 32);
    id.glyph_count = 30900;
    std::ifstream cf(std::string(argv[3]) + "/" + face + ".jfcat1",
                     std::ios::binary);
    std::string catalogBytes((std::istreambuf_iterator<char>(cf)), {});
    shaping_catalog::Identity trusted{};
    trusted.shaping = id;
    trusted.trusted_companion = bytes;
    trusted.companion_sha256 = shaping_catalog::sha256(bytes);
    trusted.catalog_sha256 = shaping_catalog::sha256(catalogBytes);
    trusted.weight = face == "bold" ? 700 : face == "medium" ? 500 : 400;
    shaping_catalog::View catalog;
    std::uint64_t bindWork = 100000000;
    if (shaping_catalog::bind(catalogBytes, trusted, {}, bindWork, catalog) !=
        shaping_catalog::Status::ok) {
      std::cerr << "catalog bind failed " << face << "\n";
      return 8;
    }
    std::ifstream v(entry.path());
    unsigned selection, n, m;
    v >> selection >> n >> m;
    std::vector<shaping_lookup::Glyph> in(n), scratch(n), sub(n), out(n),
        expected(m);
    for (auto &g : in)
      v >> g.gid >> g.source_begin >> g.source_end;
    for (auto &g : expected)
      v >> g.gid >> g.advance_x >> g.advance_y >> g.offset_x >> g.offset_y >>
          g.source_begin >> g.source_end;
    shaping_lookup::Ledger ledger{100000000, 100000};
    std::size_t count = 999;
    auto s = shaping_lookup::substitute(catalog, selection, in.data(), n,
                                        scratch.data(), n, sub.data(), n, count,
                                        ledger);
    if (s != shaping_lookup::Status::ok) {
      std::cerr << name << " substitute " << int(s) << "\n";
      return 1;
    }
    std::ifstream advances(std::string(argv[2]) + "/" + face + "-advances.txt");
    std::map<unsigned, int> a;
    unsigned gid;
    int advance;
    while (advances >> gid >> advance) {
      if (!a.emplace(gid, advance).second)
        return 31;
    }
    for (unsigned i = 0; i < count; ++i) {
      FontGlyphView metric;
      if (!catalog.find_gid(sub[i].gid, metric) ||
          a.find(sub[i].gid) == a.end() || metric.advance != a.at(sub[i].gid))
        return 10;
      sub[i].advance_x = metric.advance;
    }
    std::size_t output = 999;
    s = shaping_lookup::position(catalog, selection, sub.data(), count,
                                 scratch.data(), n, out.data(), n, output,
                                 ledger);
    if (s != shaping_lookup::Status::ok || output != m) {
      std::cerr << name << " position " << int(s) << "\n";
      return 1;
    }
    for (unsigned i = 0; i < m; ++i) {
      auto &g = out[i], &e = expected[i];
      if (g.gid != e.gid || g.advance_x != e.advance_x ||
          g.advance_y != e.advance_y || g.offset_x != e.offset_x ||
          g.offset_y != e.offset_y || g.source_begin != e.source_begin ||
          g.source_end != e.source_end) {
        std::cerr << name << " glyph " << i << " mismatch\n";
        return 1;
      }
    }
    // Refusal must preserve both output bytes and published count.
    auto saved = out;
    auto published = output;
    shaping_lookup::Ledger exhausted{0, 100};
    auto refused = shaping_lookup::position(catalog, selection, sub.data(),
                                            count, scratch.data(), n,
                                            out.data(), n, output, exhausted);
    if (refused != shaping_lookup::Status::limit || output != published ||
        std::memcmp(saved.data(), out.data(), out.size() * sizeof(out[0])))
      return 3;
    shaping_lookup::Ledger budget{100000000, 100};
    refused =
        shaping_lookup::position(catalog, selection, sub.data(), count,
                                 sub.data(), n, out.data(), n, output, budget);
    if (count && refused != shaping_lookup::Status::overlap)
      return 4;
    // A budget exhausted after resource validation must also roll back.
    for (std::uint64_t work :
         {std::uint64_t(bytes.size()), std::uint64_t(bytes.size() * 2),
          std::uint64_t(bytes.size() * 3)}) {
      shaping_lookup::Ledger limited{work, 100};
      output = published;
      refused = shaping_lookup::position(catalog, selection, sub.data(), count,
                                         scratch.data(), n, out.data(), n,
                                         output, limited);
      if (refused != shaping_lookup::Status::ok &&
          (output != published ||
           std::memcmp(saved.data(), out.data(), out.size() * sizeof(out[0]))))
        return 5;
    }
    budget = {100000000, 100};
    output = published;
    refused = shaping_lookup::position(catalog, selection, sub.data(), count,
                                       scratch.data(), n, out.data(), 0, output,
                                       budget);
    if (count &&
        (refused != shaping_lookup::Status::capacity || output != published ||
         std::memcmp(saved.data(), out.data(), out.size() * sizeof(out[0]))))
      return 6;
    budget = {100000000, 100};
    refused = shaping_lookup::position(catalog, selection, sub.data(), count,
                                       scratch.data(), n, sub.data(), n, output,
                                       budget);
    if (count && refused != shaping_lookup::Status::overlap)
      return 7;
    budget = {100000000, 100};
    refused = shaping_lookup::position(catalog, selection, sub.data(), count,
                                       scratch.data(), n, scratch.data(), n,
                                       output, budget);
    if (count && refused != shaping_lookup::Status::overlap)
      return 9;
    // A valid original GID outside the admitted catalog cannot reach
    // positioning.
    unsigned missing = 1;
    FontGlyphView absent;
    while (missing < id.glyph_count && catalog.find_gid(missing, absent))
      ++missing;
    if (missing < id.glyph_count && count) {
      auto invalid = sub;
      invalid[0].gid = missing;
      budget = {100000000, 100};
      output = published;
      refused = shaping_lookup::position(catalog, selection, invalid.data(),
                                         count, scratch.data(), n, out.data(),
                                         n, output, budget);
      if (refused != shaping_lookup::Status::invalid_input ||
          output != published ||
          std::memcmp(saved.data(), out.data(), out.size() * sizeof(out[0])))
        return 11;
    }
    for (auto *borrowed : {&catalogBytes, &bytes}) {
      auto original = *borrowed;
      budget = {100000000, 100};
      output = published;
      auto *alias = reinterpret_cast<shaping_lookup::Glyph *>(borrowed->data());
      refused =
          shaping_lookup::position(catalog, selection, sub.data(), count, alias,
                                   n, out.data(), n, output, budget);
      if (count && (refused != shaping_lookup::Status::overlap ||
                    *borrowed != original || output != published))
        return 12;
      budget = {100000000, 100};
      refused =
          shaping_lookup::position(catalog, selection, sub.data(), count,
                                   scratch.data(), n, alias, n, output, budget);
      if (count && (refused != shaping_lookup::Status::overlap ||
                    *borrowed != original || output != published))
        return 13;
    }
    for (unsigned i = 0; i < count; ++i) {
      auto delta = std::int64_t(expected[i].advance_x) - sub[i].advance_x;
      if (!delta)
        continue;
      auto extreme = sub;
      extreme[i].advance_x = delta < 0
                                 ? std::numeric_limits<std::int32_t>::min()
                                 : std::numeric_limits<std::int32_t>::max();
      budget = {100000000, 100};
      output = published;
      refused = shaping_lookup::position(catalog, selection, extreme.data(),
                                         count, scratch.data(), n, out.data(),
                                         n, output, budget);
      if (refused != shaping_lookup::Status::coordinate_overflow ||
          output != published ||
          std::memcmp(saved.data(), out.data(), out.size() * sizeof(out[0])))
        return 14;
      break;
    }
    budget = {100000000, 0};
    output = published;
    refused = shaping_lookup::position(catalog, selection, sub.data(), count,
                                       scratch.data(), n, out.data(), n, output,
                                       budget);
    if (count &&
        (refused != shaping_lookup::Status::limit || output != published ||
         std::memcmp(saved.data(), out.data(), out.size() * sizeof(out[0]))))
      return 15;
    budget = {100000000, 0};
    output = published;
    refused = shaping_lookup::position(catalog, selection, nullptr, 0, nullptr,
                                       0, nullptr, 0, output, budget);
    if (refused != shaping_lookup::Status::ok || output != 0)
      return 16;
    if (count) {
      shaping_lookup::Glyph malformed[2] = {sub[0], sub[0]}, tmp[2], dst[2]{};
      malformed[0].source_begin = 0;
      malformed[0].source_end = 1;
      malformed[1].source_begin = 0;
      malformed[1].source_end = 2;
      budget = {100000000, 100};
      output = 77;
      refused = shaping_lookup::position(catalog, selection, malformed, 2, tmp,
                                         2, dst, 2, output, budget);
      if (refused != shaping_lookup::Status::invalid_input || output != 77)
        return 17;
    }
    {
      shaping_catalog::View empty;
      budget = {100000000, 100};
      output = published;
      refused = shaping_lookup::substitute(empty, selection, in.data(), n,
                                           scratch.data(), n, out.data(), n,
                                           output, budget);
      if (refused != shaping_lookup::Status::invalid_resource ||
          output != published ||
          std::memcmp(saved.data(), out.data(), out.size() * sizeof(out[0])))
        return 26;
      if (missing < id.glyph_count && n) {
        auto invalid = in;
        invalid[0].gid = missing;
        budget = {100000000, 100};
        refused = shaping_lookup::substitute(catalog, selection, invalid.data(),
                                             n, scratch.data(), n, out.data(),
                                             n, output, budget);
        if (refused != shaping_lookup::Status::invalid_input ||
            output != published ||
            std::memcmp(saved.data(), out.data(), out.size() * sizeof(out[0])))
          return 27;
      }
      auto mismatch = trusted;
      mismatch.companion_sha256[0] ^= 1;
      auto retained = catalog;
      auto oldCompanion = catalog.companion_bytes();
      std::uint64_t work = 100000000;
      if (shaping_catalog::bind(catalogBytes, mismatch, {}, work, retained) !=
              shaping_catalog::Status::identity_mismatch ||
          retained.companion_bytes() != oldCompanion)
        return 28;
      for (auto *borrowed : {&catalogBytes, &bytes}) {
        auto original = *borrowed;
        auto *alias =
            reinterpret_cast<shaping_lookup::Glyph *>(borrowed->data());
        budget = {100000000, 100};
        refused =
            shaping_lookup::substitute(catalog, selection, in.data(), n, alias,
                                       n, out.data(), n, output, budget);
        if (n && (refused != shaping_lookup::Status::overlap ||
                  *borrowed != original || output != published))
          return 29;
        budget = {100000000, 100};
        refused = shaping_lookup::substitute(catalog, selection, in.data(), n,
                                             scratch.data(), n, alias, n,
                                             output, budget);
        if (n && (refused != shaping_lookup::Status::overlap ||
                  *borrowed != original || output != published))
          return 30;
      }
    }
    ++cases;
  }
  {
    auto load = [](const std::string &path) {
      std::ifstream f(path, std::ios::binary);
      return std::string(std::istreambuf_iterator<char>(f), {});
    };
    auto companion = load(std::string(argv[2]) + "/late-vf2.jfshr2");
    auto cat = load(std::string(argv[2]) + "/late-vf2.jfcat1");
    shaping_catalog::Identity trusted{};
    trusted.shaping.glyph_count = 30900;
    std::memcpy(trusted.shaping.source, companion.data() + 16, 32);
    std::memcpy(trusted.shaping.profile, companion.data() + 48, 32);
    trusted.trusted_companion = companion;
    trusted.companion_sha256 = shaping_catalog::sha256(companion);
    trusted.catalog_sha256 = shaping_catalog::sha256(cat);
    shaping_catalog::View catalog;
    std::uint64_t work = 100000000;
    if (shaping_catalog::bind(cat, trusted, {}, work, catalog) !=
        shaping_catalog::Status::ok)
      return 20;
    shaping_lookup::Glyph input{34, 0, 1, 1000}, scratch{},
        output{99, 9, 10, 123};
    auto saved = output;
    std::size_t count = 77;
    shaping_lookup::Ledger ledger{100000000, 100};
    auto status = shaping_lookup::position(catalog, 0, &input, 1, &scratch, 1,
                                           &output, 1, count, ledger);
    if (status != shaping_lookup::Status::unsupported || count != 77 ||
        std::memcmp(&saved, &output, sizeof(output)))
      return 21;
  }
  {
    std::ifstream f(std::string(argv[2]) + "/nested-ligature.jfshr2",
                    std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(f)), {});
    std::ifstream cf(std::string(argv[2]) + "/nested-ligature.jfcat1",
                     std::ios::binary);
    std::string syntheticCat((std::istreambuf_iterator<char>(cf)), {});
    auto catalog = bind_synthetic(bytes, syntheticCat);
    shaping_resource::Identity id{};
    id.glyph_count = 30900;
    std::memcpy(id.source, bytes.data() + 16, 32);
    std::memcpy(id.profile, bytes.data() + 48, 32);
    shaping_lookup::Glyph input[3] = {{34, 0, 1, 11},
                                      {35, 1, 2, 22},
                                      {37, 2, 3, 33}},
                          scratch[3], output[3];
    std::size_t count = 99;
    shaping_lookup::Ledger ledger{1000000, 100};
    if (shaping_lookup::substitute(catalog, 0, input, 3, scratch, 3, output, 3,
                                   count,
                                   ledger) != shaping_lookup::Status::ok ||
        count != 1 || output[0].gid != 38 || output[0].source_begin != 0 ||
        output[0].source_end != 3 || output[0].advance_x != 0)
      return 22;
  }
  {
    std::ifstream f(std::string(argv[2]) + "/nested-ligature.jfshr2",
                    std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(f)), {});
    std::ifstream cf(std::string(argv[2]) + "/nested-ligature.jfcat1",
                     std::ios::binary);
    std::string syntheticCat((std::istreambuf_iterator<char>(cf)), {});
    auto catalog = bind_synthetic(bytes, syntheticCat);
    shaping_resource::Identity id{};
    id.glyph_count = 30900;
    std::memcpy(id.source, bytes.data() + 16, 32);
    std::memcpy(id.profile, bytes.data() + 48, 32);
    // Retained glyphs on both sides share the first/last consumed clusters.
    shaping_lookup::Glyph input[4] = {{39, 0, 1},
                                      {34, 0, 1},
                                      {35, 1, 2},
                                      {40, 1, 2}},
                          scratch[4], output[4], scratch2[4], output2[4];
    std::size_t count = 99;
    shaping_lookup::Ledger ledger{1000000, 100};
    if (shaping_lookup::substitute(catalog, 0, input, 4, scratch, 4, output, 4,
                                   count,
                                   ledger) != shaping_lookup::Status::ok ||
        count != 3)
      return 23;
    for (unsigned i = 0; i < count; ++i)
      if (output[i].source_begin != 0 || output[i].source_end != 2)
        return 24;
    // Re-admission proves a valid identical-cluster partition after GSUB.
    std::size_t count2 = 99;
    if (shaping_lookup::substitute(catalog, 0, output, count, scratch2, 4,
                                   output2, 4, count2,
                                   ledger) != shaping_lookup::Status::ok ||
        count2 != 3)
      return 25;
  }
  std::cout << cases << " pinned HarfBuzz geometry cases passed\n";
  return cases ? 0 : 1;
}
