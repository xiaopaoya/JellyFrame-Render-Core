#include "render_core/shaped_packet_v2.h"

#include <iostream>
#include <stdexcept>

using namespace jellyframe::shaped_packet_v2;

namespace {
void check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

std::string hex(std::string_view bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (unsigned char byte : bytes) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 15]);
    }
    return result;
}

void check_codec() {
    Header header{16000, 0, 73, 1};
    for (std::size_t i = 0; i < header.binding_identity.size(); ++i)
        header.binding_identity[i] = static_cast<unsigned char>(i + 1);
    const Cluster cluster{0, 1, 0, 1};
    const Glyph glyph{7, 0, 1, 2, 3, 4, 5, -6};
    std::uint64_t work = 10000;
    std::string packet;
    check(encode(header, "A", &cluster, 1, &glyph, 1, {}, work, packet) == Status::ok,
          "encode fixture");
    check(hex(packet) ==
              "4a4653500200500085000000010000000100000001000000803e000000000000"
              "490000000000000001000000000000000102030405060708090a0b0c0d0e0f"
              "101112131415161718191a1b1c1d1e1f204100000000010000000000000001"
              "00000007000000000000000100000002000000030000000400000005000000"
              "faffffff00000000",
          "experimental golden bytes");
    check(work == 10000 - 2 * packet.size() - 2, "encode work charge");

    const auto retained = packet;
    View previous;
    work = packet.size() + 2;
    check(decode(retained, {}, work, previous) == Status::ok && work == 0 &&
              previous.bytes().data() == retained.data() && previous.source() == "A" &&
              previous.header().binding_identity == header.binding_identity &&
              previous.header().resource_id == 73 && previous.header().binding_generation == 1,
          "decode borrows golden bytes and charges exact work");
    Cluster decoded_cluster;
    Glyph decoded_glyph;
    check(previous.cluster_count() == 1 && previous.cluster(0, decoded_cluster) &&
              previous.glyph_count() == 1 && previous.glyph(0, decoded_glyph) &&
              decoded_cluster.source_end == 1 && decoded_cluster.glyph_end == 1 &&
              decoded_glyph.gid == 7 && decoded_glyph.offset_y == -6,
          "decode fixture records");
    check(!previous.cluster(1, decoded_cluster) && decoded_cluster.source_end == 1 &&
              !previous.glyph(1, decoded_glyph) && decoded_glyph.gid == 7,
          "out-of-range reads preserve destinations");

    const auto rejects_decode = [&](std::string_view bytes, const Limits &limits,
                                    std::uint64_t budget, Status expected) {
        auto view = previous;
        auto available = budget;
        check(decode(bytes, limits, available, view) == expected && available == budget &&
                  view.bytes().data() == previous.bytes().data() && view.bytes() == previous.bytes(),
              "decode refusal preserves view and work");
    };
    const auto rejects_encode = [&](const Header &value, const Limits &limits,
                                    std::uint64_t budget, Status expected) {
        auto available = budget;
        check(encode(value, "A", &cluster, 1, &glyph, 1, limits, available, packet) == expected &&
                  available == budget && packet == retained,
              "encode refusal preserves packet and work");
    };
    auto malformed = packet;
    malformed.back() = 1; // reserved glyph field; fails after visiting the glyph
    rejects_decode(malformed, {}, 10000, Status::malformed);
    auto invalid_header = header;
    invalid_header.selection = 5; // fails inside the draft validation pass
    rejects_encode(invalid_header, {}, 10000, Status::malformed);
    rejects_decode(packet, {}, packet.size() + 1, Status::limit);
    rejects_encode(header, {}, packet.size() * 2 + 1, Status::limit);
    const Limits maxima;
    for (auto field : {&Limits::bytes, &Limits::source_bytes, &Limits::clusters, &Limits::glyphs}) {
        auto limits = maxima;
        limits.*field = 0;
        rejects_decode(packet, limits, 10000, Status::limit);
        rejects_encode(header, limits, 10000, Status::limit);
        limits.*field = (maxima.*field) + 1;
        rejects_decode(packet, limits, 10000, Status::limit);
        rejects_encode(header, limits, 10000, Status::limit);
    }
    for (std::uint32_t ceiling : {0U, 65537U}) {
        auto limits = maxima;
        limits.glyph_ceiling = ceiling;
        rejects_decode(packet, limits, 10000, Status::limit);
        rejects_encode(header, limits, 10000, Status::limit);
    }

    work = 10000;
    const auto aliased_source = std::string_view(packet).substr(header_bytes, 1);
    check(encode(header, aliased_source, &cluster, 1, &glyph, 1, {}, work, packet) == Status::ok &&
              packet == retained,
          "encode from previous output storage");
}
} // namespace

int main() {
    try {
        check_codec();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
