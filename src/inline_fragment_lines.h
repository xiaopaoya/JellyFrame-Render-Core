#pragma once

#include "render_core/style.h"

#include <cstddef>
#include <vector>

namespace jellyframe::detail {

enum class TextAlignLast { Auto, Start, Center, End };

enum class InlinePieceKind { Text, CollapsibleSpace, PreservedSpace, Tab, Atomic, Strut, Opportunity, ForcedBreak };

// Prepared by the IFC source/measurement adapter, never by independently
// wrapping each text node. Units must already be legal indivisible clusters;
// Placed geometry uses supplied advances. Height-only contextual mode queries
// whole ranges without publishing glyph positions or assuming additivity.
struct InlineMeasuredPiece {
    InlinePieceKind kind = InlinePieceKind::Text;
    std::size_t owner = 0;
    int advance = 0;
    // Signed maximum contributions of this unit AND each containing inline's
    // own strut, including their accumulated positive upward shifts. They are
    // measured against the block baseline, not the run's displaced baseline.
    int before = 0;
    int after = 0;
    int raise = 0;
    int own_before = 0;
    int own_after = 0;
    int font_ascent = 0;
    int font_descent = 0;
    // Normal opportunities take precedence over emergency anywhere breaks.
    // Style/node boundaries by themselves must set neither flag.
    bool normal_before = false;
    bool normal_after = false;
    bool emergency_before = false;
    bool soft_wrap_allowed = true;
    bool hang_at_end = false;
    // Empty inline struts after a consumed separator stay on its preceding
    // line. Extend an existing opportunity only; never create a word break.
    bool extend_previous_break = false;
    // Tab stops relative to the unindented content start; computed by the
    // source adapter from the applicable CSS tab-size and space measurement.
    int tab_interval = 0;
    int tab_half_ch_twice = 0;
};

enum class InlineLineEnd { SoftWrap, ForcedBreak, End };
struct InlinePlacedPiece {
    std::size_t input_index = 0;
    std::size_t owner = 0;
    std::size_t line = 0;
    Rect logical;
    Rect paint;
    int pen_x = 0;
    int baseline = 0;
};
struct InlineFragmentLine {
    std::size_t first_fragment = 0;
    std::size_t fragment_count = 0;
    Rect rect;
    int baseline = 0;
    int used_width = 0;
    InlineLineEnd end = InlineLineEnd::End;
    // Raw prepared-input ranges remain available in contextual height-only
    // plans, which intentionally contain no placed glyph fragments. A forced
    // marker can be included; next_input skips a consumed collapse boundary.
    std::size_t input_begin = 0;
    std::size_t input_end = 0;
    std::size_t next_input = 0;
};
struct InlineLinePlan {
    std::vector<InlineFragmentLine> lines;
    std::vector<InlinePlacedPiece> pieces;
    int height = 0;
    // MSVC Debug's noexcept default vector constructor can allocate its iterator
    // proxy. Use throwing count constructors so allocation failures can reach
    // the planner/builder transaction instead of terminating the process.
    InlineLinePlan() : lines(0), pieces(0) {}
};
struct InlineLineRangeUsage { std::size_t work = 0, capacity = 0; };
struct InlineLineRangeBudget { std::size_t work = 0, capacity = 0; };
// Whole prepared-source ranges for height measurement without additive glyph
// advances. Query budgets include the planner's own work/storage plus callback
// usage; a callback must not outlive this call or mutate the prepared input.
struct InlineLineRangeMeasure {
    bool (*advance)(std::size_t first, std::size_t end, int origin,
        const InlineLineRangeBudget&, int* output, void* context) = nullptr;
    InlineLineRangeUsage (*usage)(void* context) = nullptr;
    void* context = nullptr;
};
struct InlineLinePlanOptions {
    int x = 0;
    int y = 0;
    int width = 0;
    int first_line_indent = 0;
    int strut_before = 0;
    int strut_after = 0;
    TextAlign align = TextAlign::Start;
    TextAlignLast align_last = TextAlignLast::Auto;
    // Use identical scans/line extents without allocating placed glyph pieces.
    // Lines and height remain available; geometry publication leaves this false.
    bool height_only = false;
    // Private compatibility callers can grow line storage from a small initial
    // reservation. Default placement keeps its up-front capacity behavior.
    bool compact_line_storage = false;
    InlineLineRangeMeasure range;
};
struct InlineLinePlanLimits {
    std::size_t max_input_items = 8192;
    std::size_t max_lines = 512;
    std::size_t max_fragments = 8192;
    std::size_t max_work = 262144;
    // Capacities of the two NEW scratch/output vectors, plus optional range
    // callback acquisitions. Not caller input, old output, allocator metadata
    // or a transitive/process peak-heap guarantee.
    std::size_t max_capacity_bytes = 1048576;
};
enum class InlineLinePlanStatus { Complete, InvalidInput, InputLimit, LineLimit, FragmentLimit, WorkLimit, CapacityLimit, CoordinateLimit, AllocationFailure, MeasurementDeclined };

// work_used reports planner validation/scan/emit work, including failed attempts;
// optional range.usage reports callback work separately under the joint limit.
// Atomic publication: all failures leave output unchanged. Source normalization,
// shaping/measurement, ancestor metrics, slice edges/decoration fragments and
// runtime binding belong to the surrounding IFC builder, not this pure planner.
InlineLinePlanStatus plan_inline_fragment_lines(const InlineMeasuredPiece* input, std::size_t count,
    const InlineLinePlanOptions& options, InlineLinePlan& output, InlineLinePlanLimits limits = {},
    std::size_t* work_used = nullptr, std::size_t* capacity_used = nullptr);

} // namespace jellyframe::detail
