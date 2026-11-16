#include "../../src/shaped_text_packet.h"
#include <iostream>
#include <stdexcept>
using namespace jellyframe::shaped_packet;
void check(bool v) {
  static int n = 0;
  ++n;
  if (!v)
    throw std::runtime_error("packet check " + std::to_string(n));
}
int main() {
  try {
    Limits limits;
    Header h{16000, 0, 1, 1};
    Cluster c{0, 2, 0, 1};
    Glyph g{42, 0, 0, 0, 16000, 0, 0, -1000};
    std::string packet = "old";
    std::uint64_t work = 1000000;
    check(encode(h, "fi", &c, 1, &g, 1, limits, work, packet) == Status::ok);
    View v;
    check(decode(packet, limits, work, v) == Status::ok);
    check(v.source() == "fi");
    Glyph got;
    check(v.glyph(0, got) && got.offset_y == -1000);
    auto original = packet;
    auto ptr = v.bytes().data();
    auto refuse = [&](std::string bad) {
      work = 1000000;
      check(decode(bad, limits, work, v) != Status::ok);
      check(v.bytes().data() == ptr && v.bytes() == original);
    };
    for (std::size_t n = 0; n < packet.size(); ++n)
      refuse(packet.substr(0, n));
    for (auto offset : {4, 6, 8, 12, 16, 20, 28, 30, 50, 54, 58, 62, 70, 98}) {
      auto zero_field = [&](std::size_t offset, unsigned width) {
        auto bad = packet;
        for (unsigned j = 0; j < width; ++j)
          bad[offset + j] = 0;
        refuse(bad);
      };
      zero_field(32, 8);
      zero_field(40, 8);
      zero_field(66, 4);
      auto limited = limits;
      limited.glyph_ceiling = 42;
      work = 1000000;
      check(decode(packet, limited, work, v) == Status::malformed);
      limited = limits;
      limited.source_bytes = 1;
      work = 1000000;
      check(decode(packet, limited, work, v) == Status::limit);
      work = packet.size() + 1;
      check(decode(packet, limits, work, v) == Status::limit);
      auto b = packet;
      b[offset] ^= char(0x80);
      refuse(b);
    }
    auto b = packet;
    b[48] = char(0xc0);
    refuse(b);
    b = packet;
    b[66] = 0;
    refuse(b);
    work = 0;
    check(decode(packet, limits, work, v) == Status::limit);
    auto unaligned = "x" + packet;
    work = 1000000;
    View u;
    check(decode(std::string_view(unaligned).substr(1), limits, work, u) ==
          Status::ok);
    Cluster multi{0, 2, 0, 2};
    Glyph gs[2] = {{42, 0, 0, 0, 0, 0, 0, 0}, {43, 0, 0, 0, 0, 0, 0, 0}};
    work = 1000000;
    check(encode(h, "\xc3\xa9", &multi, 1, gs, 2, limits, work, packet) ==
          Status::ok);
    Cluster split[2] = {{0, 1, 0, 1}, {1, 2, 1, 2}};
    gs[1].cluster = 1;
    auto saved = packet;
    work = 1000000;
    check(encode(h, "\xc3\xa9", split, 2, gs, 2, limits, work, packet) ==
              Status::malformed &&
          packet == saved);
    work = 1000000;
    View alias;
    check(decode(packet, limits, work, alias) == Status::ok);
    gs[1].cluster = 0;
    check(encode(h, alias.source(), &multi, 1, gs, 2, limits, work, packet) ==
              Status::ok &&
          packet == saved);
    gs[0].x = INT32_MAX;
    gs[0].offset_x = 1;
    work = 1000000;
    check(encode(h, "ab", &multi, 1, gs, 2, limits, work, packet) ==
              Status::malformed &&
          packet == saved);
    h.resource_id = 0;
    work = 1000000;
    check(encode(h, "ab", &multi, 1, gs, 2, limits, work, packet) ==
          Status::malformed);
    h.resource_id = 1;
    work = 1000000;
    check(encode(h, "", nullptr, 0, nullptr, 0, limits, work, packet) ==
          Status::ok);
    View empty;
    check(decode(packet, limits, work, empty) == Status::ok && empty.valid() &&
          empty.source().empty());
    work = 1000000;
    c = {0, 1, 0, 1};
    g = {42, 0, 0, 0, 0, 0, 0, 0};
    check(encode(h, std::string_view("\0", 1), &c, 1, &g, 1, limits, work,
                 packet) == Status::ok);
    std::cout << "shaped packet tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what();
    return 1;
  }
}
