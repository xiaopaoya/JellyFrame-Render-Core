#pragma once

#include "render_core/text_system.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>

namespace jellyframe::test {

class CoreTextTestFixture {
public:
    static constexpr std::uint32_t default_family_id = 0;
    static constexpr std::uint32_t named_family_id = 17;
    static constexpr std::uint32_t named_family_css_hash = 0xd573a91aU;
    static constexpr std::uint16_t default_weight = 400;
    static constexpr std::uint16_t named_weight = 700;

    CoreTextTestFixture() {
        bytes_[0] = make_font(default_weight, 0x31);
        bytes_[1] = make_font(named_weight, 0x32);
        for (std::size_t i = 0; i < resources_.size(); ++i) {
            if (resources_[i].load(bytes_[i].data(), bytes_[i].size()) != FontResourceStatus::Loaded) {
                status_ = TextSystemStatus::InvalidFontSet;
                return;
            }
        }

        faces_[0] = TextFontFace{1, default_family_id, resources_[0], 0};
        faces_[1] = TextFontFace{2, named_family_id, resources_[1], named_family_css_hash};
        status_ = system_.bind(faces_.data(), faces_.size());
    }

    CoreTextTestFixture(const CoreTextTestFixture&) = delete;
    CoreTextTestFixture& operator=(const CoreTextTestFixture&) = delete;
    CoreTextTestFixture(CoreTextTestFixture&&) = delete;
    CoreTextTestFixture& operator=(CoreTextTestFixture&&) = delete;

    bool ready() const noexcept { return status_ == TextSystemStatus::Ok; }
    TextSystemStatus status() const noexcept { return status_; }
    TextSystem& system() noexcept { return system_; }
    const TextSystem& system() const noexcept { return system_; }

private:
    static void put16(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint16_t value) {
        bytes[at] = static_cast<std::uint8_t>(value);
        bytes[at + 1] = static_cast<std::uint8_t>(value >> 8U);
    }

    static void put32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) {
        for (unsigned i = 0; i < 4; ++i)
            bytes[at + i] = static_cast<std::uint8_t>(value >> (8U * i));
    }

    static std::vector<std::uint32_t> codepoints() {
        std::vector<std::uint32_t> result;
        result.reserve(128);
        for (std::uint32_t cp = 0x20; cp <= 0x7e; ++cp) result.push_back(cp);
        constexpr std::uint32_t additional[] = {
            0x00a0, 0x2013, 0x2014, 0x2018, 0x2019, 0x201c, 0x201d, 0x2026,
            0x2039, 0x203a, 0x3001, 0x3002, 0x300c, 0x300d, 0x3042, 0x30a2,
            0x4e00, 0x4e2d, 0x56fd, 0x65e5, 0x672c, 0x6587, 0x8bed, 0x97e9,
            0xac00, 0xfffd,
        };
        result.insert(result.end(), std::begin(additional), std::end(additional));
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }

    static std::uint32_t checksum(const std::vector<std::uint8_t>& bytes) noexcept {
        std::uint32_t crc = 0xffffffffU;
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            crc ^= (i >= 96 && i < 100) ? 0U : bytes[i];
            for (unsigned bit = 0; bit < 8; ++bit)
                crc = (crc >> 1U) ^ (0xedb88320U & (0U - (crc & 1U)));
        }
        return ~crc;
    }

    static std::vector<std::uint8_t> make_font(std::uint16_t weight, std::uint8_t source_tag) {
        constexpr std::size_t header_bytes = 128;
        constexpr std::size_t glyph_bytes = 40;
        constexpr std::size_t coverage_bytes_per_glyph = 2;
        const auto chars = codepoints();
        const auto bitmap_offset = header_bytes + chars.size() * glyph_bytes;
        std::vector<std::uint8_t> bytes(bitmap_offset + chars.size() * coverage_bytes_per_glyph, 0);
        const char magic[] = "JFFONT2";
        std::copy_n(reinterpret_cast<const std::uint8_t*>(magic), 8, bytes.begin());
        put16(bytes, 8, 2);
        put16(bytes, 10, static_cast<std::uint16_t>(header_bytes));
        put16(bytes, 12, static_cast<std::uint16_t>(glyph_bytes));
        put16(bytes, 14, 1);
        put32(bytes, 16, static_cast<std::uint32_t>(bytes.size()));
        put32(bytes, 20, static_cast<std::uint32_t>(chars.size()));
        put32(bytes, 24, static_cast<std::uint32_t>(header_bytes));
        put32(bytes, 28, static_cast<std::uint32_t>(bitmap_offset));
        put32(bytes, 32, static_cast<std::uint32_t>(chars.size() * coverage_bytes_per_glyph));
        put16(bytes, 36, 1000);
        put16(bytes, 38, 16);
        put16(bytes, 40, weight);
        bytes[42] = 4;
        put32(bytes, 44, 800);
        put32(bytes, 48, 200);
        put32(bytes, 52, 50);
        put32(bytes, 100, 1);
        put16(bytes, 104, 1);
        for (std::size_t i = 0; i < 32; ++i)
            bytes[64 + i] = static_cast<std::uint8_t>(source_tag + i);

        for (std::size_t i = 0; i < chars.size(); ++i) {
            const std::size_t at = header_bytes + i * glyph_bytes;
            const auto cp = chars[i];
            const bool wide = cp >= 0x2e80;
            const auto advance = cp == 0x20 ? 250U : (wide ? 1000U : 600U);
            put32(bytes, at, cp);
            put32(bytes, at + 4, static_cast<std::uint32_t>(i + 1));
            put32(bytes, at + 8, advance);
            put32(bytes, at + 16, static_cast<std::uint32_t>(-2));
            put16(bytes, at + 20, 2);
            put16(bytes, at + 22, 2);
            put32(bytes, at + 24, 1);
            put32(bytes, at + 28, static_cast<std::uint32_t>(i * coverage_bytes_per_glyph));
            put32(bytes, at + 32, static_cast<std::uint32_t>(coverage_bytes_per_glyph));
            bytes[bitmap_offset + i * coverage_bytes_per_glyph] = 0xff;
            bytes[bitmap_offset + i * coverage_bytes_per_glyph + 1] = 0xff;
        }
        put32(bytes, 96, checksum(bytes));
        return bytes;
    }

    std::array<std::vector<std::uint8_t>, 2> bytes_;
    std::array<FontResource, 2> resources_;
    std::array<TextFontFace, 2> faces_;
    TextSystem system_;
    TextSystemStatus status_ = TextSystemStatus::NotReady;
};

} // namespace jellyframe::test
