#include "shaping_resource.h"
#include <cstring>
#include <limits>
namespace jellyframe {
namespace shaping_resource {
namespace {
std::uint32_t read_le(std::string_view bytes, std::size_t offset,
                      unsigned n) noexcept {
    std::uint32_t result = 0;
    for (unsigned i = 0; i < n; ++i)
        result |= std::uint32_t(static_cast<unsigned char>(bytes[offset + i]))
                  << (8 * i);
    return result;
}
struct Cursor {
    std::string_view bytes;
    std::size_t offset = 0;
    std::uint64_t &work;
    bool exhausted = false;
    bool charge(std::uint64_t n) noexcept {
        if (n > work) {
            exhausted = true;
            return false;
        }
        work -= n;
        return true;
    }
    bool records(std::uint64_t count, std::uint64_t &remaining) noexcept {
        if (count > remaining) {
            exhausted = true;
            return false;
        }
        remaining -= count;
        return true;
    }
    bool read_integer(unsigned n, std::uint32_t &result) noexcept {
        if (!charge(1) || n > bytes.size() - offset)
            return false;
        result = read_le(bytes, offset, n);
        offset += n;
        return true;
    }
    bool read_glyph(std::uint32_t ceiling, std::uint32_t &result) noexcept {
        return read_integer(2, result) && result < ceiling;
    }
    bool read_value_record() noexcept {
        std::uint32_t mask, component;
        if (!read_integer(1, mask) || mask > 15)
            return false;
        for (unsigned bit = 0; bit < 4; ++bit)
            if ((mask & (1U << bit)) && !read_integer(2, component))
                return false;
        return true;
    }
    bool read_anchor(bool required) noexcept {
        std::uint32_t present, coordinate;
        if (!read_integer(1, present) || present > 1 || (required && !present))
            return false;
        return !present ||
               (read_integer(2, coordinate) && read_integer(2, coordinate));
    }
};
bool validate_sorted_glyphs(Cursor &cursor, std::uint32_t count,
                            std::uint32_t glyphs) noexcept {
    std::uint32_t previous = 0, glyph_id;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (!cursor.read_glyph(glyphs, glyph_id) || (i && glyph_id <= previous))
            return false;
        previous = glyph_id;
    }
    return true;
}
bool validate_subtable(Cursor &cursor, unsigned table, unsigned type,
                       unsigned format, std::uint32_t glyphs,
                       std::uint64_t &remaining_records) noexcept {
    std::uint32_t count, glyph_id, other, previous = 0;
    if (table == 1 && type == 1 && format == 1) {
        if (!cursor.read_integer(4, count) ||
            !cursor.records(count, remaining_records))
            return false;
        for (std::uint32_t i = 0; i < count; ++i) {
            if (!cursor.read_glyph(glyphs, glyph_id) ||
                !cursor.read_glyph(glyphs, other) ||
                (i && glyph_id <= previous))
                return false;
            previous = glyph_id;
        }
    } else if (table == 1 && type == 4 && format == 4) {
        if (!cursor.read_integer(4, count) ||
            !cursor.records(count, remaining_records))
            return false;
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t components;
            if (!cursor.read_integer(1, components) || components < 2 ||
                !cursor.read_glyph(glyphs, other) ||
                !cursor.read_glyph(glyphs, glyph_id) ||
                (i && glyph_id < previous))
                return false;
            previous = glyph_id;
            for (std::uint32_t j = 1; j < components; ++j)
                if (!cursor.read_glyph(glyphs, other))
                    return false;
        }
    } else if (table == 2 && type == 2 && format == 1) {
        if (!cursor.read_integer(4, count) ||
            !cursor.records(count, remaining_records))
            return false;
        std::uint64_t previous_pair = 0;
        for (std::uint32_t i = 0; i < count; ++i) {
            if (!cursor.read_glyph(glyphs, glyph_id) ||
                !cursor.read_glyph(glyphs, other))
                return false;
            const std::uint64_t pair = (std::uint64_t(glyph_id) << 16) | other;
            if ((i && pair <= previous_pair) || !cursor.read_value_record() ||
                !cursor.read_value_record())
                return false;
            previous_pair = pair;
        }
    } else if (table == 2 && type == 2 && format == 2) {
        std::uint32_t classes[2];
        if (!cursor.read_integer(2, count) ||
            !cursor.records(count, remaining_records) ||
            !validate_sorted_glyphs(cursor, count, glyphs) ||
            !cursor.read_integer(2, classes[0]) ||
            !cursor.read_integer(2, classes[1]) || !classes[0] || !classes[1])
            return false;
        for (unsigned side = 0; side < 2; ++side) {
            if (!cursor.read_integer(2, count) ||
                !cursor.records(count, remaining_records))
                return false;
            previous = 0;
            for (std::uint32_t i = 0; i < count; ++i) {
                if (!cursor.read_glyph(glyphs, glyph_id) ||
                    !cursor.read_integer(2, other) || other >= classes[side] ||
                    (i && glyph_id <= previous))
                    return false;
                previous = glyph_id;
            }
        }
        const std::uint64_t cells = std::uint64_t(classes[0]) * classes[1];
        if (!cursor.records(cells, remaining_records))
            return false;
        for (std::uint64_t i = 0; i < cells; ++i)
            if (!cursor.read_value_record() || !cursor.read_value_record())
                return false;
    } else if (table == 2 && type == 4 && format == 1) {
        std::uint32_t classes;
        if (!cursor.read_integer(2, classes) || !classes ||
            !cursor.read_integer(2, count) ||
            !cursor.records(count, remaining_records))
            return false;
        previous = 0;
        for (std::uint32_t i = 0; i < count; ++i) {
            if (!cursor.read_glyph(glyphs, glyph_id) ||
                !cursor.read_integer(2, other) || other >= classes ||
                (i && glyph_id <= previous) || !cursor.read_anchor(true))
                return false;
            previous = glyph_id;
        }
        if (!cursor.read_integer(2, count) ||
            !cursor.records(std::uint64_t(count) * classes, remaining_records))
            return false;
        previous = 0;
        for (std::uint32_t i = 0; i < count; ++i) {
            if (!cursor.read_glyph(glyphs, glyph_id) ||
                (i && glyph_id <= previous))
                return false;
            previous = glyph_id;
            for (std::uint32_t j = 0; j < classes; ++j)
                if (!cursor.read_anchor(false))
                    return false;
        }
    } else
        return false;
    return cursor.offset == cursor.bytes.size();
}
} // namespace
Status validate(std::string_view resource_bytes, const Identity &expected,
                const Limits &limits, std::uint64_t &work,
                View &output) noexcept {
    Cursor budget{resource_bytes, 0, work};
    if (!budget.charge(resource_bytes.size() < 112 ? resource_bytes.size() + 1
                                                   : 112))
        return Status::limit;
    if (resource_bytes.size() > limits.bytes)
        return Status::limit;
    if (resource_bytes.size() < 112 ||
        resource_bytes.substr(0, 8) != std::string_view("JFSHR2\0\0", 8) ||
        read_le(resource_bytes, 8, 2) != 2 ||
        read_le(resource_bytes, 10, 2) != 112 ||
        read_le(resource_bytes, 12, 4) != resource_bytes.size())
        return Status::malformed;
    if (std::memcmp(resource_bytes.data() + 16, expected.source, 32) ||
        std::memcmp(resource_bytes.data() + 48, expected.profile, 32))
        return Status::identity_mismatch;
    const auto selection_count = read_le(resource_bytes, 80, 4),
               reference_count = read_le(resource_bytes, 84, 4),
               lookup_count = read_le(resource_bytes, 88, 4),
               glyph_count = read_le(resource_bytes, 92, 4);
    if (glyph_count != expected.glyph_count)
        return Status::identity_mismatch;
    if (!glyph_count || glyph_count > 65536 || selection_count != 5 ||
        !lookup_count || read_le(resource_bytes, 100, 4) ||
        read_le(resource_bytes, 104, 4) || read_le(resource_bytes, 108, 4))
        return Status::malformed;
    if (selection_count > limits.selections ||
        reference_count > limits.references || lookup_count > limits.lookups)
        return Status::limit;
    if (!budget.charge(resource_bytes.size() - 112))
        return Status::limit;
    std::uint32_t crc = ~0U;
    for (std::size_t i = 112; i < resource_bytes.size(); ++i) {
        crc ^= static_cast<unsigned char>(resource_bytes[i]);
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    if (~crc != read_le(resource_bytes, 96, 4))
        return Status::malformed;
    const std::uint64_t refs = 112 + std::uint64_t(selection_count) * 16,
                        dirs = refs + std::uint64_t(reference_count) * 8,
                        payload = dirs + std::uint64_t(lookup_count) * 16;
    if (payload > resource_bytes.size())
        return Status::malformed;
    std::uint32_t next_ref = 0;
    for (std::uint32_t i = 0; i < selection_count; ++i) {
        if (!budget.charge(1))
            return Status::limit;
        const std::size_t offset = 112 + std::size_t(i) * 16;
        static constexpr const char *selections[] = {
            "latn    ", "latnZHS ", "hani    ", "haniZHS ", "bopoZHT "};
        if (resource_bytes.substr(offset, 8) !=
            std::string_view(selections[i], 8))
            return Status::malformed;
        if (read_le(resource_bytes, offset + 8, 2) != next_ref ||
            read_le(resource_bytes, offset + 12, 4))
            return Status::malformed;
        const auto count = read_le(resource_bytes, offset + 10, 2);
        if (count > reference_count - next_ref)
            return Status::malformed;
        for (std::uint32_t j = 0; j < i; ++j) {
            if (!budget.charge(1))
                return Status::limit;
            if (resource_bytes.substr(offset, 8) ==
                resource_bytes.substr(112 + std::size_t(j) * 16, 8))
                return Status::malformed;
        }
        unsigned previous_table = 0;
        for (std::uint32_t j = 0; j < count; ++j) {
            if (!budget.charge(1))
                return Status::limit;
            const std::size_t r = std::size_t(refs) + (next_ref + j) * 8;
            const auto index = read_le(resource_bytes, r + 4, 2);
            if (index >= lookup_count || read_le(resource_bytes, r + 6, 2))
                return Status::malformed;
            const auto table =
                read_le(resource_bytes, std::size_t(dirs) + index * 16, 1);
            const auto tag = resource_bytes.substr(r, 4);
            if ((table == 1 && tag != "ccmp" && tag != "locl" &&
                 tag != "liga") ||
                (table == 2 && tag != "kern" && tag != "mark") ||
                table < previous_table)
                return Status::malformed;
            const auto kind =
                read_le(resource_bytes, std::size_t(dirs) + index * 16 + 1, 1);
            if ((tag == "liga" && kind != 4) || (tag == "locl" && kind != 1) ||
                (tag == "ccmp" && kind != 1 && kind != 4) ||
                (tag == "kern" && kind != 2) || (tag == "mark" && kind != 4))
                return Status::malformed;
            previous_table = table;
            for (std::uint32_t k = 0; k < j; ++k) {
                if (!budget.charge(1))
                    return Status::limit;
                if (read_le(resource_bytes,
                            std::size_t(refs) + (next_ref + k) * 8 + 4,
                            2) == index)
                    return Status::malformed;
            }
        }
        next_ref += count;
    }
    if (next_ref != reference_count)
        return Status::malformed;
    std::size_t next_payload = std::size_t(payload);
    std::uint64_t subtables = 0;
    std::uint64_t remaining_records = limits.records;
    for (std::uint32_t i = 0; i < lookup_count; ++i) {
        if (!budget.charge(1))
            return Status::limit;
        const std::size_t offset = std::size_t(dirs) + i * 16;
        const auto table = read_le(resource_bytes, offset, 1),
                   type = read_le(resource_bytes, offset + 1, 1),
                   count = read_le(resource_bytes, offset + 2, 2),
                   payload_offset = read_le(resource_bytes, offset + 8, 4),
                   length = read_le(resource_bytes, offset + 12, 4);
        if (!count || read_le(resource_bytes, offset + 6, 2) ||
            payload_offset != next_payload ||
            length > resource_bytes.size() - next_payload)
            return Status::malformed;
        subtables += count;
        if (subtables > limits.subtables)
            return Status::limit;
        for (std::uint32_t j = 0; j < i; ++j) {
            if (!budget.charge(1))
                return Status::limit;
            const std::size_t q = std::size_t(dirs) + j * 16;
            if (read_le(resource_bytes, q, 1) == table &&
                read_le(resource_bytes, q + 4, 2) ==
                    read_le(resource_bytes, offset + 4, 2))
                return Status::malformed;
        }
        Cursor lookup{resource_bytes.substr(next_payload, length), 0, work};
        for (std::uint32_t j = 0; j < count; ++j) {
            std::uint32_t subtype, format, size;
            if (!lookup.read_integer(1, subtype) ||
                !lookup.read_integer(1, format) ||
                !lookup.read_integer(4, size) ||
                size > lookup.bytes.size() - lookup.offset || subtype != type)
                return lookup.exhausted ? Status::limit : Status::malformed;
            Cursor data{lookup.bytes.substr(lookup.offset, size), 0, work};
            if (!validate_subtable(data, table, type, format, glyph_count,
                                   remaining_records))
                return data.exhausted ? Status::limit : Status::malformed;
            lookup.offset += size;
        }
        if (lookup.offset != lookup.bytes.size())
            return Status::malformed;
        next_payload += length;
    }
    if (next_payload != resource_bytes.size())
        return Status::malformed;
    output = View{resource_bytes, selection_count, reference_count,
                  lookup_count, glyph_count};
    return Status::ok;
}
} // namespace shaping_resource
} // namespace jellyframe
