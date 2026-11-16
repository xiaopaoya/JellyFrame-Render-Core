#include "../../src/shaping_resource.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <new>
#include <stdexcept>
#include <string>
using namespace jellyframe::shaping_resource;
namespace {
bool forbid_allocation = false;
}
void *operator new(std::size_t size) {
    if (forbid_allocation)
        throw std::bad_alloc();
    if (void *memory = std::malloc(size ? size : 1))
        return memory;
    throw std::bad_alloc();
}
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete[](void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void *memory, std::size_t) noexcept {
    std::free(memory);
}

namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::string load(const std::string &path) {
    std::ifstream file(path, std::ios::binary);
    check(bool(file), "fixture open");
    return std::string(std::istreambuf_iterator<char>(file), {});
}
unsigned digit(char c) {
    return c <= '9' ? unsigned(c - '0') : unsigned(c - 'a' + 10);
}
void hex(const char *s, unsigned char *out) {
    for (unsigned i = 0; i < 32; ++i)
        out[i] = static_cast<unsigned char>((digit(s[2 * i]) << 4) |
                                            digit(s[2 * i + 1]));
}
void put(std::string &s, std::size_t p, std::uint32_t v) {
    for (unsigned i = 0; i < 4; ++i)
        s[p + i] = static_cast<char>(v >> (8 * i));
}
std::uint32_t get(const std::string &s, std::size_t p, unsigned n) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < n; ++i)
        value |= std::uint32_t(static_cast<unsigned char>(s[p + i])) << (8 * i);
    return value;
}
void checksum(std::string &s) {
    std::uint32_t crc = ~0U;
    for (std::size_t i = 112; i < s.size(); ++i) {
        crc ^= static_cast<unsigned char>(s[i]);
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    put(s, 96, ~crc);
}
void reject(std::string_view bytes, const Identity &identity) {
    View out{"sentinel", 9, 8, 7, 6};
    std::uint64_t work = 4194304;
    check(validate(bytes, identity, Limits{}, work, out) != Status::ok,
          "malformed accepted");
    check(out.bytes == "sentinel" && out.selections == 9 &&
              out.references == 8 && out.lookups == 7 && out.glyphs == 6,
          "failure changed view");
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 2, "fixture directory argument");
        const char *names[] = {"regular", "medium", "bold"};
        const char *hashes[] = {
            "a2b93e6c2db05d6bbbf6f27d413ec73269735b7b679019c8a5aa9670ff0ffbf2",
            "9c62ceb174d7529ae4a7f2071f6531991cfadbc2f1897910b48ba951a580ac57",
            "d1961be1161ea1be08496c920862d06ea5c23a757628f4fd69368de1d9f51bed"};
        for (unsigned face = 0; face < 3; ++face) {
            std::string bytes =
                load(std::string(argv[1]) + "/" + names[face] + ".jfshr2");
            Identity identity{};
            identity.glyph_count = 30900;
            hex(hashes[face], identity.source);
            // Independently pinned profile descriptor digest.
            hex("7efac7babdc2da34475ffcb8ca985e5fb1e7da6544c7e58c401fed3679d58a"
                "a7",
                identity.profile);
            View out;
            std::uint64_t work = 4194304;
            forbid_allocation = true;
            auto status = validate(bytes, identity, Limits{}, work, out);
            forbid_allocation = false;
            if (status != Status::ok)
                std::cerr << names[face] << " status " << int(status) << '\n';
            check(status == Status::ok, "true encoded fixture rejected");
            check(out.selections == 5 && out.lookups == 7 &&
                      out.glyphs == 30900,
                  "fixture counts");
            check(work < 4194304, "work uncharged");
            std::string unaligned = "x" + bytes;
            work = 4194304;
            check(validate(std::string_view(unaligned).substr(1), identity,
                           Limits{}, work, out) == Status::ok,
                  "unaligned");
            for (std::size_t n = 0; n < bytes.size(); ++n)
                reject(std::string_view(bytes).substr(0, n), identity);
            for (std::size_t p = 0; p < 112; ++p) {
                auto corrupt = bytes;
                corrupt[p] ^= 1;
                reject(corrupt, identity);
            }
            auto corrupt = bytes;
            corrupt[112] = 'x';
            checksum(corrupt);
            reject(corrupt, identity);
            corrupt = bytes;
            corrupt.push_back('x');
            put(corrupt, 12, static_cast<std::uint32_t>(corrupt.size()));
            checksum(corrupt);
            reject(corrupt, identity);
            const std::size_t directory = 112 + 5 * 16 + 28 * 8;
            unsigned tested_forms = 0;
            for (unsigned lookup = 0; lookup < 7; ++lookup) {
                const std::size_t entry = directory + lookup * 16;
                for (unsigned field : {0U, 1U, 6U}) {
                    corrupt = bytes;
                    corrupt[entry + field] = static_cast<char>(255);
                    checksum(corrupt);
                    reject(corrupt, identity);
                }
                for (unsigned field : {8U, 12U}) {
                    corrupt = bytes;
                    put(corrupt, entry + field, 0xffffffffU);
                    checksum(corrupt);
                    reject(corrupt, identity);
                }
                auto payload = get(bytes, entry + 8, 4);
                const auto table = get(bytes, entry, 1);
                const auto type = get(bytes, entry + 1, 1);
                const auto subtable_count = get(bytes, entry + 2, 2);
                const auto payload_end = payload + get(bytes, entry + 12, 4);
                for (unsigned subtable = 0; subtable < subtable_count;
                     ++subtable) {
                    check(payload + 6 <= payload_end,
                          "fixture subtable header bounds");
                    const auto body_size = get(bytes, payload + 2, 4);
                    check(body_size <= payload_end - payload - 6,
                          "fixture subtable body bounds");
                    const auto format = get(bytes, payload + 1, 1);
                    const std::size_t body = payload + 6;
                    auto reject_u16 = [&](std::size_t position,
                                          std::uint32_t value) {
                        auto malformed = bytes;
                        malformed[position] = static_cast<char>(value);
                        malformed[position + 1] = static_cast<char>(value >> 8);
                        checksum(malformed);
                        reject(malformed, identity);
                    };
                    if (table == 1 && type == 1) {
                        tested_forms |= 1U << 0;
                        reject_u16(body + 4,
                                   65535); // Source GID outside font namespace.
                        reject_u16(body + 6,
                                   65535); // Output GID outside font namespace.
                        if (get(bytes, body, 4) > 1)
                            reject_u16(body + 8,
                                       get(bytes, body + 4,
                                           2)); // Duplicate source key.
                    }
                    if (table == 1 && type == 4) {
                        tested_forms |= 1U << 1;
                        auto malformed = bytes;
                        malformed[body + 4] = 1; // Too few ligature components.
                        checksum(malformed);
                        reject(malformed, identity);
                        reject_u16(body + 5, 65535); // Ligature output GID.
                        reject_u16(body + 7, 65535); // First component GID.
                        if (get(bytes, body, 4) > 1) {
                            const auto next_row =
                                body + 7 + get(bytes, body + 4, 1) * 2;
                            const auto next_first = get(bytes, next_row + 3, 2);
                            if (next_first + 1 < 30900)
                                reject_u16(body + 7,
                                           next_first +
                                               1); // Descending first GID.
                        }
                    }
                    if (table == 2 && type == 2 && format == 1) {
                        tested_forms |= 1U << 2;
                        reject_u16(body + 4, 65535);
                        reject_u16(body + 6, 65535);
                        if (get(bytes, body, 4) > 1) {
                            std::size_t next_pair = body + 8;
                            for (unsigned value = 0; value < 2; ++value) {
                                auto mask = get(bytes, next_pair++, 1);
                                for (unsigned bit = 0; bit < 4; ++bit)
                                    if (mask & (1U << bit))
                                        next_pair += 2;
                            }
                            auto duplicate = bytes;
                            for (unsigned byte = 0; byte < 4; ++byte)
                                duplicate[next_pair + byte] =
                                    bytes[body + 4 + byte];
                            checksum(duplicate);
                            reject(duplicate, identity);
                        }
                        auto malformed = bytes;
                        malformed[body + 8] =
                            static_cast<char>(0x80); // Unknown value mask bit.
                        checksum(malformed);
                        reject(malformed, identity);
                    }
                    if (table == 2 && type == 2 && format == 2) {
                        tested_forms |= 1U << 3;
                        const auto coverage = get(bytes, body, 2);
                        reject_u16(body + 2, 65535);
                        if (coverage > 1)
                            reject_u16(body + 4, get(bytes, body + 2, 2));
                        const auto dimensions = body + 2 + coverage * 2;
                        reject_u16(dimensions, 0);
                        reject_u16(dimensions + 2, 0);
                        const auto assignment_count =
                            get(bytes, dimensions + 4, 2);
                        if (assignment_count) {
                            reject_u16(dimensions + 6, 65535);
                            reject_u16(dimensions + 8,
                                       get(bytes, dimensions, 2));
                            if (assignment_count > 1)
                                reject_u16(dimensions + 10,
                                           get(bytes, dimensions + 6, 2));
                        }
                    }
                    if (table == 2 && type == 4) {
                        tested_forms |= 1U << 4;
                        reject_u16(body, 0);         // Empty mark class domain.
                        reject_u16(body + 4, 65535); // Mark GID.
                        reject_u16(body + 6,
                                   get(bytes, body, 2)); // Class out of range.
                        auto malformed = bytes;
                        malformed[body + 8] = 0; // Required mark anchor absent.
                        checksum(malformed);
                        reject(malformed, identity);
                        malformed = bytes;
                        malformed[body + 8] = 2; // Unknown anchor presence.
                        checksum(malformed);
                        reject(malformed, identity);
                    }

                    corrupt = bytes;
                    corrupt[payload] = static_cast<char>(255);
                    checksum(corrupt);
                    reject(corrupt, identity);
                    corrupt = bytes;
                    corrupt[payload + 1] = static_cast<char>(255);
                    checksum(corrupt);
                    reject(corrupt, identity);
                    corrupt = bytes;
                    put(corrupt, payload + 2, 0xffffffffU);
                    checksum(corrupt);
                    reject(corrupt, identity);
                    corrupt = bytes;
                    put(corrupt, payload + 6, 0xffffffffU);
                    checksum(corrupt);
                    reject(corrupt, identity);
                    payload += 6 + body_size;
                }
                check(payload == payload_end,
                      "fixture exact subtable traversal");
            }
            check(tested_forms == 31,
                  "fixture must exercise all five supported forms");
            // Selection zero has ccmp, liga, locl, kern, mark, mark references.
            corrupt = bytes;
            for (unsigned byte = 0; byte < 8; ++byte)
                corrupt[192 + 8 + byte] =
                    bytes[192 + byte]; // Duplicate eligible reference.
            checksum(corrupt);
            reject(corrupt, identity);
            corrupt = bytes;
            for (unsigned byte = 0; byte < 8; ++byte) {
                corrupt[192 + byte] = bytes[192 + 3 * 8 + byte];
                corrupt[192 + 3 * 8 + byte] = bytes[192 + byte];
            }
            checksum(corrupt);
            reject(corrupt, identity); // GPOS before GSUB.
            corrupt = bytes;
            corrupt.replace(192 + 3 * 8, 4, "mark");
            checksum(corrupt);
            reject(corrupt, identity); // mark on pair lookup.
            corrupt = bytes;
            corrupt[directory + 16 + 4] = corrupt[directory + 4];
            corrupt[directory + 16 + 5] = corrupt[directory + 5];
            checksum(corrupt);
            reject(corrupt, identity); // Duplicate original lookup ID.
            Limits small;
            small.records = 1;
            work = 4194304;
            check(validate(bytes, identity, small, work, out) == Status::limit,
                  "record budget");
            small = Limits{};
            small.subtables = 1;
            work = 4194304;
            check(validate(bytes, identity, small, work, out) == Status::limit,
                  "subtable budget");
            work = 0;
            View previous{"sentinel"};
            check(validate(bytes, identity, Limits{}, work, previous) ==
                          Status::limit &&
                      previous.bytes == "sentinel",
                  "budget rollback");
            work = 4194304;
            check(validate(bytes, identity, Limits{}, work, out) == Status::ok,
                  "first cumulative");
            const auto used = 4194304 - work;
            work = used - 1;
            check(validate(bytes, identity, Limits{}, work, out) ==
                      Status::limit,
                  "cumulative work");
        }
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
