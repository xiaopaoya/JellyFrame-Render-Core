#include "contextual_text_inline_geometry.h"

#include "contextual_text_painter.h"
#include "shaped_text_packet.h"

#include <algorithm>
#include <new>
#include <stdexcept>

namespace jellyframe::detail {
namespace {
bool take(std::uint64_t& work, std::uint64_t amount) noexcept {
    if (amount > work) return false;
    work -= amount;
    return true;
}
}

ContextualTextInkStatus contextual_text_line_glyph_ink(
    const contextual_text::Paragraph& paragraph, std::size_t line_index,
    std::int32_t baseline_x_q1000, std::int32_t baseline_y_q1000,
    std::vector<ContextualTextGlyphInk>& output,
    std::uint64_t& work) noexcept {
    using Status = ContextualTextInkStatus;
    if (!paragraph.valid() || line_index >= paragraph.lines().size())
        return Status::InvalidInput;
    const auto& line = paragraph.lines()[line_index];
    const auto text = paragraph.text();
    if (line.source_begin > line.source_end || line.source_end > text.size())
        return Status::InvalidInput;

    auto remaining = work;
    const auto packet_status = contextual_text::validate_packet(
        paragraph.binding(), line.packet, {}, remaining);
    if (packet_status == contextual_text::Status::identity_mismatch)
        return Status::BindingMismatch;
    if (packet_status == contextual_text::Status::allocation)
        return Status::AllocationFailure;
    if (packet_status != contextual_text::Status::ok)
        return Status::ResourceLimit;

    shaped_packet::Limits packet_limits;
    shaped_packet::View packet;
    const auto decoded = shaped_packet::decode(line.packet, packet_limits, remaining, packet);
    if (decoded == shaped_packet::Status::allocation)
        return Status::AllocationFailure;
    if (decoded != shaped_packet::Status::ok ||
        packet.source() != text.substr(line.source_begin, line.source_end - line.source_begin))
        return Status::InvalidInput;
    if (!take(remaining, packet.cluster_count() + packet.glyph_count()))
        return Status::ResourceLimit;

    std::vector<Rect> glyph_bounds;
    const auto positioned = contextual_painter::glyph_ink_bounds(
        packet, ContextualTextInkBindingAccess::snapshot(paragraph.binding()),
        baseline_x_q1000, baseline_y_q1000, glyph_bounds, remaining);
    if (positioned == contextual_painter::Status::identity_mismatch)
        return Status::BindingMismatch;
    if (positioned == contextual_painter::Status::missing_glyph ||
        positioned == contextual_painter::Status::invalid)
        return Status::InvalidInput;
    if (positioned == contextual_painter::Status::allocation)
        return Status::AllocationFailure;
    if (positioned != contextual_painter::Status::ok)
        return Status::ResourceLimit;

    try {
        std::vector<ContextualTextGlyphInk> draft;
        draft.reserve(glyph_bounds.size());
        std::size_t expected_glyph = 0;
        for (std::size_t cluster_index = 0;
             cluster_index < packet.cluster_count(); ++cluster_index) {
            shaped_packet::Cluster cluster;
            if (!packet.cluster(cluster_index, cluster) ||
                cluster.glyph_begin != expected_glyph ||
                cluster.glyph_end <= cluster.glyph_begin ||
                cluster.glyph_end > glyph_bounds.size())
                return Status::InvalidInput;
            for (std::size_t glyph_index = cluster.glyph_begin;
                 glyph_index < cluster.glyph_end; ++glyph_index) {
                draft.push_back({static_cast<std::uint32_t>(cluster_index),
                    static_cast<std::uint32_t>(glyph_index), glyph_bounds[glyph_index]});
            }
            expected_glyph = cluster.glyph_end;
        }
        if (expected_glyph != glyph_bounds.size()) return Status::InvalidInput;
        output.swap(draft);
        work = remaining;
        return Status::Complete;
    } catch (const std::bad_alloc&) {
        return Status::AllocationFailure;
    } catch (const std::length_error&) {
        return Status::AllocationFailure;
    }
}

} // namespace jellyframe::detail
