#pragma once
#include "inline_fragment_lines.h"
#include "inline_text_alignment_geometry.h"
#include "inline_tab_geometry.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace jellyframe::detail {
namespace inline_piece_reader_detail {
inline bool fits(std::int64_t value) {
    return value >= std::numeric_limits<int>::min() && value <= std::numeric_limits<int>::max();
}
inline bool content(const InlineMeasuredPiece& piece) {
    return piece.kind == InlinePieceKind::Text || piece.kind == InlinePieceKind::Atomic ||
        piece.kind == InlinePieceKind::PreservedSpace || piece.kind == InlinePieceKind::Tab;
}
inline bool trailing_space(const InlineMeasuredPiece& piece) {
    return piece.kind == InlinePieceKind::CollapsibleSpace ||
        ((piece.kind == InlinePieceKind::PreservedSpace || piece.kind == InlinePieceKind::Tab) && piece.hang_at_end);
}
template<class Reader> struct Planner {
    Reader input;
    std::size_t count;
    const InlineLinePlanOptions& options;
    InlineLinePlanLimits limits;
    InlineLinePlan result;
    std::size_t work = 0;
    std::size_t* work_output = nullptr;
    std::int64_t line_y;
    std::size_t acquired_capacity = 0;
    std::size_t* capacity_output = nullptr;
    InlineLinePlanStatus status = InlineLinePlanStatus::Complete;

    bool charge() {
        const auto external = options.range.usage ? options.range.usage(options.range.context).work : 0;
        if (work >= limits.max_work || external >= limits.max_work - work) {
            status = InlineLinePlanStatus::WorkLimit; return false;
        }
        ++work; if (work_output != nullptr) *work_output = work; return true;
    }
    bool coordinate(std::int64_t value) {
        if (fits(value)) return true;
        status = InlineLinePlanStatus::CoordinateLimit; return false;
    }
    bool capacity() {
        const auto external = options.range.usage ? options.range.usage(options.range.context).capacity : 0;
        if (acquired_capacity > limits.max_capacity_bytes ||
            external > limits.max_capacity_bytes - acquired_capacity) {
            status = InlineLinePlanStatus::CapacityLimit; return false;
        }
        return true;
    }
    bool acquire_capacity(std::size_t count, std::size_t item_bytes) {
        if (!capacity()) return false;
        const auto external = options.range.usage ? options.range.usage(options.range.context).capacity : 0;
        const auto remaining = limits.max_capacity_bytes - acquired_capacity - external;
        if (count > remaining / item_bytes) {
            status = InlineLinePlanStatus::CapacityLimit;
            return false;
        }
        acquired_capacity += count * item_bytes;
        if (capacity_output) *capacity_output = acquired_capacity;
        return true;
    }
    template<class Item>
    bool reserve(std::vector<Item>& storage, std::size_t requested) {
        if (requested <= storage.capacity()) return true;
        if (!acquire_capacity(requested, sizeof(Item))) return false;
        storage.reserve(requested);
        if (storage.capacity() > requested &&
            !acquire_capacity(storage.capacity() - requested, sizeof(Item))) return false;
        return capacity();
    }
    std::int64_t indent() const { return result.lines.empty() ? options.first_line_indent : 0; }
    std::int64_t advance(const InlineMeasuredPiece& piece, std::int64_t x) const {
        if (piece.kind == InlinePieceKind::Tab) {
            // Euclidean remainder gives the next stop even with negative indent.
            return inline_tab_advance_twice(x * 2, piece.tab_interval, piece.tab_half_ch_twice) / 2;
        }
        return piece.advance;
    }
    bool range_advance(std::size_t first, std::size_t end, std::int64_t& x) {
        if (!capacity()) return false;
        const auto used = options.range.usage(options.range.context);
        if (work > limits.max_work || used.work > limits.max_work - work) {
            status = InlineLinePlanStatus::WorkLimit; return false;
        }
        InlineLineRangeBudget budget;
        budget.work = limits.max_work - work - used.work;
        budget.capacity = limits.max_capacity_bytes - acquired_capacity - used.capacity;
        int measured = 0;
        if (!options.range.advance(first, end, static_cast<int>(indent()), budget, &measured, options.range.context)) {
            status = InlineLinePlanStatus::MeasurementDeclined; return false;
        }
        if (measured < 0) { status = InlineLinePlanStatus::MeasurementDeclined; return false; }
        x = indent() + measured;
        if (!capacity()) return false;
        return coordinate(x);
    }
    bool emit(std::size_t start, std::size_t end, InlineLineEnd ending, std::size_t next) {
        std::size_t first = end, last = start;
        bool has_content = false, has_hanging = false;
        for (std::size_t index = start; index < end; ++index) {
            if (!charge()) return false;
            InlineMeasuredPiece piece; if (!input.read(index,piece)) { status=InlineLinePlanStatus::InvalidInput; return false; }
            if (content(piece)) has_content = true;
            if ((piece.kind == InlinePieceKind::PreservedSpace || piece.kind == InlinePieceKind::Tab) &&
                piece.hang_at_end) has_hanging = true;
            // A trailing hanging preserved space still creates a real line.
            if (content(piece) && !trailing_space(piece)) {
                first = std::min(first, index); last = index + 1;
            }
        }
        if (!has_content && ending != InlineLineEnd::ForcedBreak) return true;
        if (result.lines.size() >= limits.max_lines) { status = InlineLinePlanStatus::LineLimit; return false; }
        // Preserve leading spaces/tabs. Only collapsible spaces use first/last
        // text positions; a pre-wrap all-space line is real with hanging tail.
        std::int64_t x = indent();
        std::int64_t before = options.strut_before, after = options.strut_after;
        std::size_t gaps = 0;
        for (std::size_t index = start; index < end; ++index) {
            if (!charge()) return false;
            InlineMeasuredPiece piece; if (!input.read(index,piece)) { status=InlineLinePlanStatus::InvalidInput; return false; }
            const bool collapsed = piece.kind == InlinePieceKind::CollapsibleSpace && (index < first || index >= last);
            const bool hanging = piece.kind != InlinePieceKind::CollapsibleSpace && trailing_space(piece) && index >= last;
            if (collapsed) continue;
            before = std::max(before, static_cast<std::int64_t>(piece.before));
            after = std::max(after, static_cast<std::int64_t>(piece.after));
            if (hanging) continue;
            if (piece.kind == InlinePieceKind::CollapsibleSpace) ++gaps;
            x += advance(piece, x);
            if (!coordinate(x)) return false;
        }
        if (options.range.advance && !range_advance(start, end, x)) return false;
        // Preserved tails always hang at soft wraps. At a forced break or
        // paragraph end they affect alignment when the natural line fits.
        // Retain their actual positions below even when they hang outside.
        if (!options.height_only && has_hanging && ending != InlineLineEnd::SoftWrap) {
            auto natural = x;
            for (auto index = last; index < end; ++index) {
                if (!charge()) return false;
                InlineMeasuredPiece piece; if (!input.read(index,piece)) { status=InlineLinePlanStatus::InvalidInput; return false; }
                if (piece.kind == InlinePieceKind::CollapsibleSpace) continue;
                natural += advance(piece, natural);
                if (!coordinate(natural)) return false;
            }
            if (natural <= options.width) x = natural;
        }
        const std::int64_t line_height = before + after;
        const std::int64_t baseline = line_y + before;
        if (!coordinate(static_cast<std::int64_t>(options.x) + options.width) ||
            !coordinate(line_height) || !coordinate(baseline) || !coordinate(line_y + line_height) ||
            !coordinate(line_y + line_height - options.y)) return false;
        // `justify` expands only natural soft-wrap lines. `justify-all` also
        // expands the final line when it follows a natural soft wrap; this
        // keeps forced breaks and a single unwrapped line unchanged without
        // adding per-line state to the compact plan representation.
        const bool final_natural_wrap = ending == InlineLineEnd::End &&
            !result.lines.empty() && result.lines.back().end == InlineLineEnd::SoftWrap;
        const auto alignment_geometry = inline_text_alignment_geometry(options.align, options.align_last,
            ending, final_natural_wrap, options.width, x, gaps);
        const bool justify = alignment_geometry.justified;
        const std::int64_t alignment = alignment_geometry.offset;
        const std::int64_t free = std::max<std::int64_t>(0, static_cast<std::int64_t>(options.width) - x);
        const std::size_t first_fragment = result.pieces.size();
        std::size_t gap_index = 0;
        const std::int64_t used_width = alignment_geometry.used_width;
        x = indent();
        for (std::size_t index = start; !options.height_only && index < end; ++index) {
            if (!charge()) return false;
            InlineMeasuredPiece piece; if (!input.read(index,piece)) { status=InlineLinePlanStatus::InvalidInput; return false; }
            const bool collapsed = piece.kind == InlinePieceKind::CollapsibleSpace && (index < first || index >= last);
            if (collapsed || piece.kind == InlinePieceKind::Strut ||
                piece.kind == InlinePieceKind::Opportunity || piece.kind == InlinePieceKind::ForcedBreak) continue;
            std::int64_t width = advance(piece, x);
            if (justify && piece.kind == InlinePieceKind::CollapsibleSpace) {
                width += free / static_cast<std::int64_t>(gaps) + (gap_index < static_cast<std::size_t>(free % gaps) ? 1 : 0);
                ++gap_index;
            }
            const std::int64_t pen = static_cast<std::int64_t>(options.x) + alignment + x;
            const std::int64_t own_baseline = baseline - piece.raise;
            const std::int64_t top = own_baseline - piece.own_before;
            const std::int64_t own_height = static_cast<std::int64_t>(piece.own_before) + piece.own_after;
            const std::int64_t paint_top = std::min(top, own_baseline - piece.font_ascent);
            const std::int64_t paint_bottom = std::max(top + own_height, own_baseline + piece.font_descent);
            if (!coordinate(pen) || !coordinate(pen + width) || !coordinate(width) || !coordinate(own_baseline) ||
                !coordinate(top) || !coordinate(top + own_height) || !coordinate(own_height) ||
                !coordinate(paint_top) || !coordinate(paint_bottom) || !coordinate(paint_bottom - paint_top)) return false;
            if (result.pieces.size() >= limits.max_fragments) { status = InlineLinePlanStatus::FragmentLimit; return false; }
            result.pieces.push_back({index, piece.owner, result.lines.size(),
                {static_cast<int>(pen), static_cast<int>(top), static_cast<int>(width), static_cast<int>(own_height)},
                {static_cast<int>(pen), static_cast<int>(paint_top), static_cast<int>(width), static_cast<int>(paint_bottom - paint_top)},
                static_cast<int>(pen), static_cast<int>(own_baseline)});
            if (!capacity()) return false;
            x += width;
        }
        if (options.compact_line_storage && result.lines.size() == result.lines.capacity()) {
            const auto external = options.range.usage ? options.range.usage(options.range.context).capacity : 0;
            if (acquired_capacity > limits.max_capacity_bytes ||
                external > limits.max_capacity_bytes - acquired_capacity) {
                status = InlineLinePlanStatus::CapacityLimit; return false;
            }
            const auto ceiling = std::min(limits.max_lines,
                (limits.max_capacity_bytes - acquired_capacity - external) / sizeof(InlineFragmentLine));
            if (result.lines.size() >= ceiling) { status = InlineLinePlanStatus::CapacityLimit; return false; }
            const auto old = result.lines.capacity();
            const auto next = old > ceiling / 2 ? ceiling : std::max<std::size_t>(1,old * 2);
            if (!reserve(result.lines, next)) return false;
        }
        result.lines.push_back({first_fragment, result.pieces.size() - first_fragment,
            {options.x, static_cast<int>(line_y), options.width, static_cast<int>(line_height)},
            static_cast<int>(baseline), static_cast<int>(used_width), ending, start, end, next});
        if (!capacity()) return false;
        line_y += line_height;
        result.height = static_cast<int>(line_y - options.y);
        return true;
    }
    bool run_ranges() {
        struct Candidate { std::size_t end = 0, next = 0; bool valid = false; };
        std::size_t start = 0, index = 0;
        Candidate fitting, first_normal, overflowing;
        bool has_content = false;
        const auto suffix_content = [&](std::size_t first,std::size_t end) {
            for (auto at=first;at<end;++at) { if (!charge()) return false; InlineMeasuredPiece p; if (!input.read(at,p)) {status=InlineLinePlanStatus::InvalidInput;return false;} if (content(p)) return true; }
            return false;
        };
        const auto endpoint = [&](std::size_t end, std::size_t next, InlineLineEnd ending, bool within_word = false) {
            if (within_word && !first_normal.valid && end>start) first_normal={end,next,true};
            std::int64_t x = 0;
            if (!range_advance(start, end, x)) return -1;
            if (x <= options.width) {
                fitting = {end, next, true};
                if (ending == InlineLineEnd::SoftWrap) return 0;
                if (!emit(start, end, ending, next)) return -1;
            } else {
                // A later prefix of the same word can be narrower after shaping.
                // Delay an interior overflow decision until the whole word is
                // known; keep its last genuinely fitting normal opportunity.
                if (within_word) return 0;
                if (fitting.valid && fitting.end > start) {
                    if (!emit(start, fitting.end, InlineLineEnd::SoftWrap, fitting.next)) return -1;
                    next = fitting.next;
                } else {
                    const bool prior_overflow=overflowing.valid && suffix_content(overflowing.next,end);
                    if (status!=InlineLinePlanStatus::Complete) return -1;
                    if (prior_overflow) {
                        if (!emit(start,overflowing.end,InlineLineEnd::SoftWrap,overflowing.next)) return -1;
                        start=index=overflowing.next; fitting={}; first_normal={}; overflowing={}; has_content=false;
                        return 1;
                    }
                    Candidate emergency, first_emergency;
                    bool prior_content = false;
                    for (std::size_t at = start; at < end; ++at) {
                        if (!charge()) return -1;
                        InlineMeasuredPiece p; if (!input.read(at,p)) { status=InlineLinePlanStatus::InvalidInput; return -1; }
                        if (prior_content && p.soft_wrap_allowed && p.emergency_before) {
                            if (!first_emergency.valid) first_emergency = {at, at, true};
                            if (!range_advance(start, at, x)) return -1;
                            if (x <= options.width) emergency = {at, at, true};
                        }
                        prior_content = prior_content || content(p);
                    }
                    // An overwide indivisible cluster still has to make progress.
                    // Prefix widths need not be monotone: retain the last actual
                    // fitting emergency boundary instead of stopping at a scalar.
                    if (!emergency.valid) {
                        emergency=first_emergency;
                        const bool can_normal=first_normal.valid && suffix_content(first_normal.next,end);
                        if (status!=InlineLinePlanStatus::Complete) return -1;
                        if (can_normal && (!emergency.valid || first_normal.end<emergency.end)) emergency=first_normal;
                    }
                    if (emergency.valid) {
                        if (!emit(start, emergency.end, InlineLineEnd::SoftWrap, emergency.next)) return -1;
                        next = emergency.next;
                    } else {
                        // Keep an indivisible overwide word until following
                        // content actually needs its trailing opportunity. A
                        // trailing separator before newline/end is trimmed,
                        // and cannot create a second empty forced-break line.
                        if (ending==InlineLineEnd::SoftWrap) { overflowing={end,next,true}; return 0; }
                        if (!emit(start,end,ending,next)) return -1;
                    }
                }
            }
            start = index = next; fitting = {}; first_normal = {}; overflowing = {}; has_content = false;
            return 1;
        };
        while (start < count) {
            if (index == count) {
                if (endpoint(count, count, InlineLineEnd::End) < 0) return false;
                continue;
            }
            if (!charge()) return false;
            InlineMeasuredPiece p; if (!input.read(index,p)) { status=InlineLinePlanStatus::InvalidInput; return false; }
            if (p.kind == InlinePieceKind::ForcedBreak) {
                if (endpoint(index + 1, index + 1, InlineLineEnd::ForcedBreak) < 0) return false;
                continue;
            }
            if (has_content && p.soft_wrap_allowed &&
                (p.kind == InlinePieceKind::CollapsibleSpace || p.kind == InlinePieceKind::Opportunity || p.normal_before)) {
                const bool boundary = p.kind == InlinePieceKind::CollapsibleSpace || p.kind == InlinePieceKind::Opportunity;
                const auto action = endpoint(index, boundary ? index + 1 : index, InlineLineEnd::SoftWrap,
                    p.kind!=InlinePieceKind::CollapsibleSpace);
                if (action < 0) return false;
                if (action > 0) continue;
            }
            has_content = has_content || content(p);
            ++index;
            if (has_content && p.soft_wrap_allowed && p.normal_after) {
                const auto action = endpoint(index, index, InlineLineEnd::SoftWrap,
                    p.kind!=InlinePieceKind::PreservedSpace && p.kind!=InlinePieceKind::Tab);
                if (action < 0) return false;
                if (action > 0) continue;
            }
        }
        return true;
    }
    bool run() {
        if (options.range.advance) return run_ranges();
        struct Candidate { std::size_t end = 0, next = 0; bool valid = false; };
        std::size_t start = 0, index = 0;
        std::int64_t x = indent();
        bool has_content = false;
        Candidate normal, emergency;
        while (index < count) {
            if (!charge()) return false;
            InlineMeasuredPiece piece; if (!input.read(index,piece)) { status=InlineLinePlanStatus::InvalidInput; return false; }
            if (piece.kind == InlinePieceKind::ForcedBreak) {
                if (!emit(start, index + 1, InlineLineEnd::ForcedBreak, index + 1)) return false;
                start = ++index; x = indent(); has_content = false; normal = {}; emergency = {}; continue;
            }
            if (has_content && piece.soft_wrap_allowed) {
                if (piece.kind == InlinePieceKind::CollapsibleSpace || piece.kind == InlinePieceKind::Opportunity)
                    normal = {index, index + 1, true};
                else if (piece.normal_before) normal = {index, index, true};
                if (piece.emergency_before) emergency = {index, index, true};
            }
            if(piece.extend_previous_break && normal.valid && normal.next==index)
                normal={index+1,index+1,true};
            const bool leading_space = piece.kind == InlinePieceKind::CollapsibleSpace && !has_content;
            if (!leading_space) x += advance(piece, x);
            if (!coordinate(x)) return false;
            has_content = has_content || content(piece);
            // Trailing pre-wrap spaces may hang until the next real unit. Their
            // end opportunity is still remembered for that next unit's wrap.
            const bool hanging = piece.kind != InlinePieceKind::CollapsibleSpace && trailing_space(piece);
            // A collapsible separator can disappear at this line's end. Do
            // not wrap on it (or a zero-width scope/opportunity) before knowing
            // the following content; a forced break must not acquire an extra
            // blank line merely because the trailing space made x overwide.
            if (x > options.width && !hanging && content(piece)) {
                const Candidate chosen = normal.valid ? normal : emergency;
                if (chosen.valid) {
                    if (!emit(start, chosen.end, InlineLineEnd::SoftWrap, chosen.next)) return false;
                    start = index = chosen.next; x = indent(); has_content = false; normal = {}; emergency = {}; continue;
                }
            }
            if (has_content && piece.soft_wrap_allowed && piece.normal_after) normal = {index + 1, index + 1, true};
            ++index;
        }
        return emit(start, count, InlineLineEnd::End, count);
    }
};
} // namespace inline_piece_reader_detail

template<class Reader> InlineLinePlanStatus plan_inline_fragment_lines_read(Reader input, std::size_t count,
    const InlineLinePlanOptions& options, InlineLinePlan& output, InlineLinePlanLimits limits,
    std::size_t* work_used, std::size_t* capacity_used = nullptr) {
    if (work_used != nullptr) *work_used = 0;
    if (capacity_used != nullptr) *capacity_used = 0;
    using namespace inline_piece_reader_detail;
    if (count > limits.max_input_items) return InlineLinePlanStatus::InputLimit;
    if (work_used != nullptr) *work_used = std::min(count, limits.max_work);
    if (count > limits.max_work) return InlineLinePlanStatus::WorkLimit;
    if ((options.range.advance && (!options.height_only || !options.range.usage)) || options.align < TextAlign::Start || options.align > TextAlign::End || options.width < 0 || static_cast<std::int64_t>(options.strut_before) + options.strut_after < 0 ||
        !fits(static_cast<std::int64_t>(options.strut_before) + options.strut_after)) return InlineLinePlanStatus::InvalidInput;
    std::size_t potential_pieces = 0;
    const auto line_ceiling = std::min(count, limits.max_lines);
    std::size_t potential_lines = 0;
    bool trailing_content = false;
    bool could_line = false;
    const auto line_candidate = [&] {
        // Saturate before addition, even with caller-supplied SIZE_MAX limits.
        if (potential_lines < line_ceiling) ++potential_lines;
    };
    for (std::size_t index = 0; index < count; ++index) {
        InlineMeasuredPiece piece; if (!input.read(index,piece)) return InlineLinePlanStatus::InvalidInput;
        if (piece.kind < InlinePieceKind::Text || piece.kind > InlinePieceKind::ForcedBreak ||
            piece.advance < 0 || piece.font_ascent < 0 || piece.font_descent < 0 ||
            (piece.kind == InlinePieceKind::Tab && (piece.tab_interval < 0 || piece.tab_half_ch_twice < 0)) ||
            static_cast<std::int64_t>(piece.own_before) + piece.own_after < 0 ||
            !fits(static_cast<std::int64_t>(piece.own_before) + piece.own_after) ||
            static_cast<std::int64_t>(piece.before) + piece.after < 0 ||
            !fits(static_cast<std::int64_t>(piece.before) + piece.after) ||
            !fits(static_cast<std::int64_t>(piece.font_ascent) + piece.font_descent) ||
            ((!content(piece) && piece.kind != InlinePieceKind::CollapsibleSpace) && piece.advance != 0) ||
            ((content(piece) || piece.kind == InlinePieceKind::CollapsibleSpace) &&
                (piece.before < static_cast<std::int64_t>(piece.own_before) + piece.raise ||
                 piece.after < static_cast<std::int64_t>(piece.own_after) - piece.raise))) return InlineLinePlanStatus::InvalidInput;
        if (content(piece) || piece.kind == InlinePieceKind::CollapsibleSpace) ++potential_pieces;
        could_line = could_line || content(piece) || piece.kind == InlinePieceKind::ForcedBreak;
        // Every soft line consumes a distinct legal cut; moving an existing
        // cut across empty struts cannot add one. Forced markers each emit at
        // most one further line. Only content after the last forced marker
        // can create the final End line. Count in this existing validation
        // traversal, without executing contextual measurement callbacks.
        if (piece.kind == InlinePieceKind::ForcedBreak) {
            line_candidate();
            // Unlike the outer forced-marker branch, contextual emergency
            // search scans the marker itself before emitting its blank line.
            if (options.range.advance && piece.soft_wrap_allowed && piece.emergency_before) line_candidate();
            trailing_content = false;
        } else {
            if (piece.soft_wrap_allowed) {
                if (piece.kind == InlinePieceKind::CollapsibleSpace || piece.kind == InlinePieceKind::Opportunity ||
                    piece.normal_before || piece.emergency_before) line_candidate();
                if (piece.normal_after) line_candidate();
            }
            trailing_content = trailing_content || content(piece);
        }
    }
    if (trailing_content) line_candidate();
    try {
        Planner<Reader> planner{input, count, options, limits, {}, count, work_used,
            options.y, 0, capacity_used};
        if (could_line) {
            const std::size_t fragments = options.height_only ? 0 : std::min(potential_pieces, limits.max_fragments);
            const std::size_t lines = options.compact_line_storage ? std::min<std::size_t>(potential_lines,4) : potential_lines;
            if (fragments > limits.max_capacity_bytes / sizeof(InlinePlacedPiece) ||
                lines > (limits.max_capacity_bytes - fragments * sizeof(InlinePlacedPiece)) / sizeof(InlineFragmentLine))
                return InlineLinePlanStatus::CapacityLimit;
            if (!planner.reserve(planner.result.pieces, fragments) ||
                !planner.reserve(planner.result.lines, lines)) return planner.status;
            if (!planner.capacity()) return planner.status;
        }
        if (!planner.run()) return planner.status;
        output = std::move(planner.result);
        return InlineLinePlanStatus::Complete;
    } catch (const std::bad_alloc&) { return InlineLinePlanStatus::AllocationFailure; }
      catch (const std::length_error&) { return InlineLinePlanStatus::AllocationFailure; }
}
} // namespace jellyframe::detail
