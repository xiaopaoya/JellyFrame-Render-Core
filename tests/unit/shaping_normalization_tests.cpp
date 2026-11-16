#include "../../src/shaping_catalog_sha256.h"
#include "../../src/shaping_normalization.h"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <vector>
#include <stdexcept>
using namespace jellyframe;
using namespace shaping_normalization;
std::string load(std::string p) {
  std::ifstream f(p, std::ios::binary);
  if (!f)
    throw std::runtime_error(p);
  return {std::istreambuf_iterator<char>(f), {}};
}
void ck(bool v) {
  static int index = 0;
  ++index;
  if (!v) {
    std::cerr << index << " ";
    throw std::runtime_error("check failed");
  }
}
unsigned rd(std::string_view b, size_t p, unsigned n = 4) {
  unsigned v = 0;
  for (unsigned i = 0; i < n; ++i)
    v |= unsigned((unsigned char)b[p + i]) << (i * 8);
  return v;
}
std::string utf8(const std::vector<unsigned> &scalars) {
  std::string bytes;
  for (auto cp : scalars) {
    if (cp < 0x80) bytes += char(cp);
    else if (cp < 0x800) {
      bytes += char(0xc0 | (cp >> 6)); bytes += char(0x80 | (cp & 63));
    } else if (cp < 0x10000) {
      bytes += char(0xe0 | (cp >> 12)); bytes += char(0x80 | ((cp >> 6) & 63));
      bytes += char(0x80 | (cp & 63));
    } else {
      bytes += char(0xf0 | (cp >> 18)); bytes += char(0x80 | ((cp >> 12) & 63));
      bytes += char(0x80 | ((cp >> 6) & 63)); bytes += char(0x80 | (cp & 63));
    }
  }
  return bytes;
}
void put(std::string &b, size_t p, unsigned v) {
  for (int i = 0; i < 4; ++i)
    b[p + i] = char(v >> (i * 8));
}
void crc(std::string &b) {
  unsigned x = ~0U;
  for (size_t i = 172; i < b.size(); ++i) {
    x ^= (unsigned char)b[i];
    for (int j = 0; j < 8; ++j)
      x = (x >> 1) ^ (0xedb88320U & (0U - (x & 1)));
  }
  put(b, 156, ~x);
}
int main(int argc, char **argv) {
  try {
    ck(argc == 2);
    std::string root = argv[1];
    auto bytes = load(root + "/shaping_normalization/unicode14.bin");
    auto hash = shaping_catalog::sha256(bytes);
    Tables tables;
    std::uint64_t work = 10000000;
    ck(validate_tables(bytes, hash, {}, work, tables) == Status::ok);
    auto corrupt = bytes;
    corrupt[8] = 15;
    Tables sentinel = tables;
    work = 10000000;
    ck(validate_tables(corrupt, shaping_catalog::sha256(corrupt), {}, work,
                       sentinel) == Status::malformed &&
       sentinel.bytes().data() == bytes.data());
    for (auto face : {"regular", "medium", "bold"}) {
      auto cb = load(root + "/shaping_catalog/" + face + ".jfcat1"),
           comp = load(root + "/shaping_resource/" + face + ".jfshr2"),
           eb = load(root + "/shaping_normalization/" + face + ".eligibility");
      shaping_catalog::Identity id;
      std::memcpy(id.shaping.source, cb.data() + 16, 32);
      std::memcpy(id.shaping.profile, cb.data() + 48, 32);
      id.shaping.glyph_count = rd(cb, 112);
      id.weight = rd(cb, 132, 2);
      id.trusted_companion = comp;
      id.companion_sha256 = shaping_catalog::sha256(comp);
      id.catalog_sha256 = shaping_catalog::sha256(cb);
      shaping_catalog::View cat;
      work = 10000000;
      ck(shaping_catalog::bind(cb, id, {}, work, cat) ==
         shaping_catalog::Status::ok);
      Eligibility e;
      ck(validate_eligibility(eb, shaping_catalog::sha256(eb), tables, cat,
                              work, e) == Status::ok);
      Scalar scratch[16384], out[4096];
      size_t count = 77;
      Limits limits;
      auto run = [&](std::string_view src) {
        work = 10000000;
        return normalize(tables, e, cat, src, scratch, 16384, out, 4096, count,
                         limits, work);
      };
      ck(run("ABC") == Status::ok && count == 3 && out[2].source_begin == 2);
      ck(run("\xc3\xa9") == Status::ok && count == 1 && out[0].scalar == 0xe9 &&
         out[0].source_begin == 0 && out[0].source_end == 2);
      auto saved = out[0];
      count = 77;
      ck(run("A\xcc\x81") == Status::unsupported && count == 77 &&
         !std::memcmp(&saved, out, sizeof(saved)));
      ck(run("\xc0\x80") == Status::malformed);
      ck(run("\n") == Status::unsupported);
      ck(run("\xc2\xad") == Status::unsupported);
      ck(run("\xe1\x84\x80\xe1\x85\xa1") == Status::unsupported);
      // Actual expanded application catalog, independently generated from
      // declared raw alphabet.
      cb = load(root + "/shaping_executor_catalog/" + face + ".jfcat1");
      id.catalog_sha256 = shaping_catalog::sha256(cb);
      work = 10000000;
      ck(shaping_catalog::bind(cb, id, {}, work, cat) ==
         shaping_catalog::Status::ok);
      ck(run("A\xcc\x81") == Status::ok && count == 1 &&
         out[0].scalar == 0xc1 && out[0].source_end == 3);
      ck(run("A\xcc\x81\xcc\x81") == Status::ok && count == 2 &&
         out[0].source_begin == 0 && out[0].source_end == 5 &&
         out[1].source_begin == 0 && out[1].source_end == 5);
      ck(run("I\xcc\x87") == Status::ok && count == 2);
      count = 77;
      work = 0;
      ck(normalize(tables, e, cat, "A", scratch, 16384, out, 4096, count,
                   limits, work) == Status::limit &&
         count == 77);
      work = 10000000;
      ck(normalize(tables, e, cat, "A", scratch, 0, out, 4096, count, limits,
                   work) == Status::capacity &&
         count == 77);
      work = 10000000;
      ck(normalize(tables, e, cat, "A", scratch, 16384, scratch, 4096, count,
                   limits, work) == Status::overlap);
      work = 10000000;
      ck(normalize(tables, e, cat, "A", scratch, 16384, out, 0, count, limits,
                   work) == Status::capacity &&
         count == 77);
      ck(run("") == Status::ok && count == 0);
      auto bad = eb;
      bad[8] ^= 1;
      work = 10000000;
      ck(validate_eligibility(bad, shaping_catalog::sha256(bad), tables, cat,
                              work, e) == Status::identity);
    }

    // Algorithm cases use explicitly synthetic membership, separately from
    // Noto.
    auto cb = load(root + "/shaping_normalization/synthetic.jfcat1");
    auto comp = load(root + "/shaping_normalization/synthetic.jfshr2");
    auto eb = load(root + "/shaping_normalization/synthetic.eligibility");
    shaping_catalog::Identity id;
    std::memcpy(id.shaping.source, cb.data() + 16, 32);
    std::memcpy(id.shaping.profile, cb.data() + 48, 32);
    id.shaping.glyph_count = rd(cb, 112);
    id.weight = rd(cb, 132, 2);
    id.trusted_companion = comp;
    id.companion_sha256 = shaping_catalog::sha256(comp);
    id.catalog_sha256 = shaping_catalog::sha256(cb);
    shaping_catalog::View cat;
    work = 10000000;
    ck(shaping_catalog::bind(cb, id, {}, work, cat) ==
       shaping_catalog::Status::ok);
    Eligibility eligibility;
    ck(validate_eligibility(eb, shaping_catalog::sha256(eb), tables, cat, work,
                            eligibility) == Status::ok);
    Scalar scratch[16384], output[4096];
    std::size_t count = 0;
    std::ifstream oracle(root + "/shaping_normalization/oracle.txt");
    std::string encoded, expected;
    while (oracle >> encoded >> expected) {
      std::string input;
      for (std::size_t i = 0; i < encoded.size(); i += 2)
        input.push_back(static_cast<char>(
            std::strtoul(encoded.substr(i, 2).c_str(), nullptr, 16)));
      work = 10000000;
      ck(normalize(tables, eligibility, cat, input, scratch, 16384, output,
                   4096, count, {}, work) == Status::ok);
      std::size_t begin = 0, index = 0;
      do {
        auto end = expected.find(',', begin);
        ck(index < count &&
           output[index++].scalar ==
               std::strtoul(expected.substr(begin, end - begin).c_str(),
                            nullptr, 10));
        begin = end == std::string::npos ? end : end + 1;
      } while (begin != std::string::npos);
      ck(index == count);
      for (std::size_t i = 0; i < count; ++i)
        ck(output[i].source_begin == 0 && output[i].source_end == input.size());
    }
    // Official Unicode14 NFC invariants, using explicitly artificial complete
    // mapping and unrestricted composition. Real-font policy is tested above.
    std::ifstream ucd(root + "/shaping_normalization/NormalizationTest-14.0.0.txt");
    ck(bool(ucd));
    std::string line;
    std::size_t ucd_cases = 0, refused_profile = 0;
    while (std::getline(ucd, line)) {
      line = line.substr(0, line.find('#'));
      if (line.empty() || line[0] == '@') continue;
      std::istringstream row(line);
      std::vector<unsigned> columns[5];
      bool complete = true;
      for (auto &column : columns) {
        std::string field;
        if (!std::getline(row, field, ';')) { complete = false; break; }
        std::istringstream values(field);
        unsigned cp;
        while (values >> std::hex >> cp) column.push_back(cp);
        if (column.empty()) complete = false;
      }
      if (!complete) continue;
      for (unsigned c = 0; c < 5; ++c) {
        auto text = utf8(columns[c]);
        work = 10000000;
        auto result = normalize(tables, eligibility, cat, text, scratch, 16384,
                                output, 4096, count, {}, work);
        if (result == Status::unsupported) { ++refused_profile; continue; }
        ck(result == Status::ok);
        const auto &target = columns[c < 3 ? 1 : 3];
        ck(count == target.size());
        for (std::size_t i = 0; i < count; ++i) ck(output[i].scalar == target[i]);
        ++ucd_cases;
      }
    }
    ck(ucd_cases == 94950);
    std::cout << ucd_cases << " official Unicode14 NFC cases; "
              << refused_profile << " explicit profile refusals\n";
    // Every work refusal leaves the entire output and published count intact.
    const std::string input = "B\xcc\x81\xcc\xa7";
    work = 10000000;
    ck(normalize(tables, eligibility, cat, input, scratch, 16384, output, 4096,
                 count, {}, work) == Status::ok);
    const auto cost = 10000000 - work;
    for (std::uint64_t budget = 0; budget < cost; ++budget) {
      std::memset(output, 0x5a, sizeof(output));
      Scalar saved[4096];
      std::memcpy(saved, output, sizeof(output));
      count = 99;
      work = budget;
      ck(normalize(tables, eligibility, cat, input, scratch, 16384, output,
                   4096, count, {}, work) == Status::limit);
      ck(count == 99 && !std::memcmp(saved, output, sizeof(output)));
    }
    for (auto malformed :
         {std::string("\xed\xa0\x80"), std::string("\xf4\x90\x80\x80"),
          std::string("\xe2\x82"), std::string("\x80")}) {
      work = 10000000;
      ck(normalize(tables, eligibility, cat, malformed, scratch, 16384, output,
                   4096, count, {}, work) == Status::malformed);
    }
    for (auto offset :
         {std::size_t(0), std::size_t(8), std::size_t(10), std::size_t(12),
          std::size_t(16), std::size_t(20), std::size_t(24)}) {
      auto bad = bytes;
      bad[offset] ^= 0x40;
      work = 10000000;
      auto preserved = tables.bytes().data();
      ck(validate_tables(bad, shaping_catalog::sha256(bad), {}, work, tables) ==
         Status::malformed);
      ck(tables.bytes().data() == preserved);
    }
    for (auto offset :
         {std::size_t(0), std::size_t(72), std::size_t(76), std::size_t(197)}) {
      auto bad = eb;
      bad[offset] ^= 0x80;
      work = 10000000;
      auto preserved = eligibility.bytes().data();
      ck(validate_eligibility(bad, shaping_catalog::sha256(bad), tables, cat,
                              work, eligibility) == Status::malformed);
      ck(eligibility.bytes().data() == preserved);
    }
    work = 10000000;
    ck(normalize(tables, eligibility, cat, "A", scratch, 16384, output, 4096,
                 *reinterpret_cast<std::size_t *>(scratch), {},
                 work) == Status::overlap);
    work = 10000000;
    Limits limited;
    limited.input_scalars = 0;
    ck(normalize(tables, eligibility, cat, "A", scratch, 16384, output, 4096,
                 count, limited, work) == Status::limit);
    limited = {};
    limited.expanded_scalars = 1;
    work = 10000000;
    ck(normalize(tables, eligibility, cat, "\xc3\xa9", scratch, 16384, output,
                 4096, count, limited, work) == Status::limit);
    // Independent starters have no interacting provenance: their cost must
    // scale linearly rather than rescanning the full run per starter.
    auto ascii_cost = [&](std::size_t length) {
      std::string text(length, 'A');
      work = 10000000;
      ck(normalize(tables, eligibility, cat, text, scratch, 16384, output,
                   4096, count, {}, work) == Status::ok && count == length);
      for (std::size_t i = 0; i < count; ++i)
        ck(output[i].source_begin == i && output[i].source_end == i + 1);
      return 10000000 - work;
    };
    const auto short_cost = ascii_cost(128);
    const auto long_cost = ascii_cost(256);
    ck(long_cost < short_cost * 3);
    std::cout << "normalization tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what();
    return 1;
  }
}
