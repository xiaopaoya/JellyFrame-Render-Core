#include "core_text_inline_source_map.h"
#include "shaped_text_packet.h"
#include "render_core/contextual_text.h"
#include "utf8_scalar.h"

#include <algorithm>
#include <new>
#include <stdexcept>

namespace jellyframe::detail {
namespace {
bool ascii_whitespace(unsigned char value) noexcept {
    return value == ' ' || value == '\t' || value == '\n' || value == '\r' || value == '\f';
}

bool take_work(std::uint64_t& work, std::uint64_t units) noexcept {
    if (units > work) return false;
    work -= units;
    return true;
}

bool valid_utf8_source(std::string_view text) noexcept {
    for (std::size_t at = 0; at < text.size();) {
        std::uint32_t scalar = 0;
        if (!read_utf8_scalar(text, at, scalar)) return false;
        if ((scalar < 0x20 && !ascii_whitespace(static_cast<unsigned char>(scalar))) ||
            scalar == 0x7f) return false;
    }
    return true;
}

bool scalar_boundary(std::string_view text, std::size_t offset) noexcept {
    return offset == 0 || offset == text.size() ||
        (static_cast<unsigned char>(text[offset]) & 0xc0U) != 0x80U;
}
}

CoreTextInlineSourceMapStatus build_core_text_inline_source_tokens(
    const CoreTextParagraph& paragraph,
    const std::string_view* owner_sources, std::size_t owner_count,
    std::vector<CoreTextClusterMapToken>& output, std::uint64_t* remaining_work) noexcept {
    using Status = CoreTextInlineSourceMapStatus;
    constexpr std::size_t max_source_bytes = 65536;
    constexpr std::size_t max_pieces = 8192;
    constexpr std::size_t max_owners = 128;
    if ((owner_count && !owner_sources) || owner_count > max_owners ||
        paragraph.sources.size() > max_pieces || paragraph.normalized.size() > max_source_bytes ||
        paragraph.sources.empty() != paragraph.normalized.empty() ||
        (!paragraph.byte_offsets.empty() && paragraph.byte_offsets.size() != paragraph.sources.size()))
        return Status::InvalidInput;

    std::uint64_t local_work = UINT64_MAX;
    auto& work = remaining_work ? *remaining_work : local_work;
    if (!take_work(work, paragraph.normalized.size() + paragraph.sources.size() + owner_count))
        return Status::WorkLimit;
    std::size_t source_bytes = 0;
    for (std::size_t owner = 0; owner < owner_count; ++owner) {
        const auto text = owner_sources[owner];
        if (text.size() > max_source_bytes - source_bytes) return Status::InvalidInput;
        if (!take_work(work, text.size() * 2)) return Status::WorkLimit;
        if (!valid_utf8_source(text)) return Status::InvalidInput;
        source_bytes += text.size();
    }

    try {
        std::vector<CoreTextClusterMapToken> draft;
        draft.reserve(paragraph.sources.size() + owner_count);
        std::vector<std::size_t> source_cursors(owner_count, 0);
        const auto omit = [&](std::size_t owner, std::size_t end, std::size_t offset) {
            auto& begin = source_cursors[owner];
            if (begin == end) return true;
            const auto gap = owner_sources[owner].substr(begin, end - begin);
            if (!std::all_of(gap.begin(), gap.end(), ascii_whitespace)) return false;
            draft.push_back({static_cast<std::uint32_t>(offset), static_cast<std::uint32_t>(offset),
                static_cast<std::uint32_t>(owner), static_cast<std::uint32_t>(begin),
                static_cast<std::uint32_t>(end), true});
            begin = end;
            return true;
        };
        std::size_t previous_owner = 0;
        std::size_t normalized_cursor = 0;
        bool has_owner = false;

        for (std::size_t piece = 0; piece < paragraph.sources.size(); ++piece) {
            const auto& source = paragraph.sources[piece];
            if (source.owner >= owner_count || source.begin >= source.end ||
                (has_owner && source.owner < previous_owner))
                return Status::InvalidInput;

            const auto text_begin = paragraph.byte_offset(piece);
            const auto text_end = paragraph.byte_offset(piece + 1);
            if (text_begin != normalized_cursor || text_end <= text_begin ||
                text_end > paragraph.normalized.size())
                return Status::InvalidInput;
            const auto source_text = owner_sources[source.owner];
            if (source.end > source_text.size() || source.begin < source_cursors[source.owner])
                return Status::InvalidInput;
            auto scalar_end = source.begin;
            std::uint32_t scalar = 0;
            if (!scalar_boundary(source_text, source.begin) ||
                !read_utf8_scalar(source_text, scalar_end, scalar) || scalar_end != source.end)
                return Status::UnsupportedNormalization;

            // Owners skipped between pieces can only contribute omitted whitespace.
            if (!has_owner || source.owner != previous_owner) {
                const auto first = has_owner ? previous_owner : 0;
                for (std::size_t owner = first; owner < source.owner; ++owner) {
                    if (!omit(owner, owner_sources[owner].size(), text_begin))
                        return Status::UnsupportedNormalization;
                }
            }
            if (!omit(source.owner, source.begin, text_begin))
                return Status::UnsupportedNormalization;

            const auto original = source_text.substr(source.begin, source.end - source.begin);
            const auto normalized = std::string_view(paragraph.normalized).substr(
                text_begin, text_end - text_begin);
            const bool collapsed_whitespace = original.size() == 1 && normalized.size() == 1 &&
                ascii_whitespace(static_cast<unsigned char>(original.front())) && normalized.front() == ' ';
            if (!collapsed_whitespace && (ascii_whitespace(static_cast<unsigned char>(original.front())) ||
                original != normalized))
                return Status::UnsupportedNormalization;

            draft.push_back({static_cast<std::uint32_t>(text_begin),
                static_cast<std::uint32_t>(text_end),
                static_cast<std::uint32_t>(source.owner),
                static_cast<std::uint32_t>(source.begin),
                static_cast<std::uint32_t>(source.end), false});
            source_cursors[source.owner] = source.end;
            previous_owner = source.owner;
            has_owner = true;
            normalized_cursor = text_end;
        }

        if (normalized_cursor != paragraph.normalized.size()) return Status::InvalidInput;
        const auto trailing_start = has_owner ? previous_owner : 0;
        for (std::size_t owner = trailing_start; owner < owner_count; ++owner) {
            if (!omit(owner, owner_sources[owner].size(), normalized_cursor))
                return Status::UnsupportedNormalization;
        }
        output.swap(draft);
        return Status::Complete;
    } catch (const std::bad_alloc&) {
        return Status::AllocationFailure;
    } catch (const std::length_error&) {
        return Status::AllocationFailure;
    }
}

CoreTextInlineSourceMapStatus map_experimental_contextual_inline_line(
    const CoreTextParagraph& source_paragraph,
    const std::string_view* owner_sources, std::size_t owner_count,
    const contextual_text::Paragraph& shaped_paragraph, std::size_t line_index,
    CoreTextClusterMap& output, CoreTextClusterMapLimits limits,
    std::uint64_t* remaining_work) noexcept {
    using Status = CoreTextInlineSourceMapStatus;
    if (!shaped_paragraph.valid() || line_index >= shaped_paragraph.lines().size() ||
        shaped_paragraph.text().size() != source_paragraph.normalized.size())
        return Status::UnsupportedNormalization;

    auto local_work = static_cast<std::uint64_t>(limits.max_work);
    auto& work = remaining_work ? *remaining_work : local_work;
    if (!take_work(work, source_paragraph.normalized.size())) return Status::WorkLimit;
    if (shaped_paragraph.text() != source_paragraph.normalized) return Status::UnsupportedNormalization;

    try {
        std::vector<CoreTextClusterMapToken> source_tokens;
        auto status = build_core_text_inline_source_tokens(source_paragraph,
            owner_sources, owner_count, source_tokens, &work);
        if (status != Status::Complete) return status;

        const auto& line = shaped_paragraph.lines()[line_index];
        if (line.source_begin > line.source_end || line.source_end > shaped_paragraph.text().size() ||
            line.consumed_end < line.source_end || line.consumed_end > shaped_paragraph.text().size())
            return Status::InvalidInput;
        const auto line_text = shaped_paragraph.text().substr(
            line.source_begin, line.source_end - line.source_begin);
        if (!take_work(work, source_tokens.size() + line.consumed_end - line.source_end))
            return Status::WorkLimit;
        std::vector<CoreTextClusterMapToken> line_tokens;
        status = project_core_text_inline_line_tokens(source_paragraph, source_tokens,
            line.source_begin, line.source_end, line.consumed_end, line_tokens);
        if (status != Status::Complete) return status;

        const auto checked = contextual_text::validate_packet(shaped_paragraph.binding(),
            line.packet, {}, work);
        if (checked == contextual_text::Status::limit) return Status::WorkLimit;
        if (checked != contextual_text::Status::ok) return Status::InvalidInput;

        shaped_packet::Limits packet_limits;
        shaped_packet::View packet;
        const auto decoded = shaped_packet::decode(line.packet, packet_limits, work, packet);
        if (decoded == shaped_packet::Status::limit) return Status::WorkLimit;
        if (decoded != shaped_packet::Status::ok || packet.source() != line_text)
            return Status::InvalidInput;
        if (packet.glyph_count() > limits.max_glyphs || packet.cluster_count() > limits.max_clusters)
            return Status::InvalidInput;
        if (!take_work(work, packet.cluster_count() + packet.glyph_count() + line_text.size()))
            return Status::WorkLimit;

        std::vector<CoreTextClusterMapGlyph> glyphs;
        glyphs.reserve(packet.glyph_count());
        for (std::size_t cluster_index = 0; cluster_index < packet.cluster_count(); ++cluster_index) {
            shaped_packet::Cluster cluster;
            if (!packet.cluster(cluster_index, cluster) || cluster.glyph_begin >= cluster.glyph_end ||
                cluster.glyph_end > packet.glyph_count())
                return Status::InvalidInput;
            for (std::size_t glyph_index = cluster.glyph_begin; glyph_index < cluster.glyph_end; ++glyph_index) {
                shaped_packet::Glyph glyph;
                if (!packet.glyph(glyph_index, glyph)) return Status::InvalidInput;
                glyphs.push_back({glyph.gid, cluster.source_begin, cluster.source_end});
            }
        }

        CoreTextClusterMap draft;
        CoreTextClusterMapUsage usage;
        limits.max_work = std::min<std::size_t>(limits.max_work,
            static_cast<std::size_t>(std::min<std::uint64_t>(work, SIZE_MAX)));
        const auto mapped = map_core_text_clusters(line_text, line_tokens.data(),
            line_tokens.size(), glyphs.data(), glyphs.size(), draft, &usage, limits);
        work -= std::min<std::uint64_t>(work, usage.work);
        if (mapped == CoreTextClusterMapStatus::WorkLimit) return Status::WorkLimit;
        if (mapped == CoreTextClusterMapStatus::AllocationFailure) return Status::AllocationFailure;
        if (mapped != CoreTextClusterMapStatus::Complete) return Status::InvalidInput;
        output = std::move(draft);
        return Status::Complete;
    } catch (const std::bad_alloc&) {
        return Status::AllocationFailure;
    } catch (const std::length_error&) {
        return Status::AllocationFailure;
    }
}

CoreTextInlineSourceMapStatus project_core_text_inline_line_tokens(
    const CoreTextParagraph& paragraph,
    const std::vector<CoreTextClusterMapToken>& source_tokens,
    std::size_t line_begin, std::size_t line_end, std::size_t consumed_end,
    std::vector<CoreTextClusterMapToken>& output) noexcept {
    using Status = CoreTextInlineSourceMapStatus;
    if (line_begin > line_end || line_end > consumed_end ||
        consumed_end > paragraph.normalized.size() ||
        !scalar_boundary(paragraph.normalized, line_begin) ||
        !scalar_boundary(paragraph.normalized, line_end) ||
        !scalar_boundary(paragraph.normalized, consumed_end))
        return Status::InvalidInput;
    const auto consumed = std::string_view(paragraph.normalized).substr(line_end, consumed_end - line_end);
    if (!std::all_of(consumed.begin(), consumed.end(), ascii_whitespace))
        return Status::UnsupportedNormalization;

    try {
        std::vector<CoreTextClusterMapToken> draft;
        draft.reserve(source_tokens.size());
        std::size_t text_cursor = 0;
        for (const auto& token : source_tokens) {
            if (token.text_begin > token.text_end || token.text_end > paragraph.normalized.size() ||
                token.source_begin >= token.source_end)
                return Status::InvalidInput;
            if (token.omitted) {
                if (token.text_begin != token.text_end) return Status::InvalidInput;
                const auto position = static_cast<std::size_t>(token.text_begin);
                // A gap at the next line's start belongs to that line.
                if (position < line_begin || position > consumed_end ||
                    (position == consumed_end && consumed_end != paragraph.normalized.size()))
                    continue;
                const auto local = static_cast<std::uint32_t>(
                    std::min(position, line_end) - line_begin);
                draft.push_back({local, local, token.owner, token.source_begin,
                    token.source_end, true});
                continue;
            }

            if (token.text_begin == token.text_end ||
                token.text_end - token.text_begin != token.source_end - token.source_begin)
                return Status::UnsupportedNormalization;

            const auto visible_begin = std::max<std::size_t>(token.text_begin, line_begin);
            const auto visible_end = std::min<std::size_t>(token.text_end, line_end);
            if (visible_begin < visible_end) {
                if (visible_begin != text_cursor + line_begin) return Status::InvalidInput;
                const auto source_begin = token.source_begin +
                    static_cast<std::uint32_t>(visible_begin - token.text_begin);
                const auto source_end = source_begin +
                    static_cast<std::uint32_t>(visible_end - visible_begin);
                draft.push_back({static_cast<std::uint32_t>(visible_begin - line_begin),
                    static_cast<std::uint32_t>(visible_end - line_begin), token.owner,
                    source_begin, source_end, false});
                text_cursor += visible_end - visible_begin;
            }

            const auto consumed_begin = std::max<std::size_t>(token.text_begin, line_end);
            const auto consumed_finish = std::min<std::size_t>(token.text_end, consumed_end);
            if (consumed_begin < consumed_finish) {
                const auto source_begin = token.source_begin +
                    static_cast<std::uint32_t>(consumed_begin - token.text_begin);
                const auto source_end = source_begin +
                    static_cast<std::uint32_t>(consumed_finish - consumed_begin);
                const auto local_end = static_cast<std::uint32_t>(line_end - line_begin);
                draft.push_back({local_end, local_end, token.owner, source_begin,
                    source_end, true});
            }
        }
        if (text_cursor != line_end - line_begin) return Status::InvalidInput;
        output.swap(draft);
        return Status::Complete;
    } catch (const std::bad_alloc&) {
        return Status::AllocationFailure;
    } catch (const std::length_error&) {
        return Status::AllocationFailure;
    }
}

} // namespace jellyframe::detail
