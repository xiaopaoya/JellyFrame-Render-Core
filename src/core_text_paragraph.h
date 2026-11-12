#pragma once
#include "layout_text_service.h"
#include "inline_fragment_lines.h"
#include "render_core/layout.h"

namespace jellyframe::detail {

struct CoreTextLayoutLimits {
    std::size_t max_queries = 2048, max_query_bytes = 262144;
    std::size_t max_source_bytes = 8192, max_prepared_glyphs = 128;
    std::size_t max_lines = 128, max_line_work = 262144;
    std::size_t max_capacity_bytes = 1048576;
};

// Private paragraph input: source order, explicit structural events and resolved fonts.
// Atomic uses separate intrinsic endpoints or selected geometry. Full building
// returns placements separately from text-only glyph packets.
// No DOM, CSS callback, paint policy or host measurement provider is borrowed.
enum class CoreTextParagraphSpanKind : unsigned char {Text, ForcedBreak, Opportunity, Atomic};
struct CoreTextParagraphSpan {
    std::string_view source;
    LayoutTextFont font;
    int line_height = -1;
    CoreTextParagraphSpanKind kind = CoreTextParagraphSpanKind::Text;
};
enum class CoreTextWhiteSpace : unsigned char { Collapse, PreserveLines, Preserve, PreserveSpaces };
struct CoreTextParagraphOptions {
    LayoutTextFont strut{0,16000};
    int line_height = -1;
    // Selected content width and spare alignment are Q1000 throughout planning.
    // The public indent value and final box origins retain integer pixels.
    std::int64_t width_units = 0;
    int indent = 0;
    TextAlign align = TextAlign::Start;
    TextAlignLast align_last = TextAlignLast::Auto;
    bool nowrap = false, break_all = false, anywhere = false;
    CoreTextWhiteSpace whitespace = CoreTextWhiteSpace::Collapse;
};
struct CoreTextParagraphSource {
    std::size_t owner = 0, begin = 0, end = 0;
};
struct CoreTextParagraphLine {
    std::size_t begin = 0, end = 0;
    int top = 0, height = 0, baseline = 0;
    // Preserved advance is the complete natural advance, including hanging
    // whitespace. origin includes alignment; Tab grids precede that shift.
    std::int32_t origin = 0, advance = 0;
    bool soft = false;
};
struct CoreTextParagraphFragment {
    std::size_t owner = 0, line = 0, begin = 0, end = 0;
    // Packet X is Q1000 relative to the paragraph content origin. Its source
    // byte ranges are local to this fragment's normalized ASCII substring.
    std::string packet;
    CoreTextParagraphFragment() : packet(0,'\0') {}
};
struct CoreTextParagraph {
    // Private sentinels: LF ForcedBreak, VT Opportunity, BS Atomic.
    // Explicit events have begin==end==0; pre-line source LF/CR/CRLF retains
    // its source range (cross-owner CRLF belongs to CR). None become glyphs.
    std::string normalized;
    std::vector<CoreTextParagraphSource> sources;
    std::vector<CoreTextParagraphLine> lines;
    std::vector<CoreTextParagraphFragment> fragments;
    std::uint64_t revision = 0;
    int height = 0;
    CoreTextParagraph() : normalized(0,'\0'), sources(0), lines(0), fragments(0) {}
};
struct CoreTextParagraphUsage {
    std::size_t queries = 0, query_bytes = 0, work = 0, capacity = 0;
};
struct CoreTextPreservedOptions {
    // Owned containing-block Tab context, shared by intrinsic and selected
    // metric queries. Text spans retain their own complete font requests.
    LayoutTextFont tab_font{0,16000};
    bool break_spaces = false;
    std::uint8_t tab_size = 8;
};
struct CoreTextIntrinsicOptions {
    bool nowrap = false, break_all = false, anywhere = false;
    CoreTextWhiteSpace whitespace = CoreTextWhiteSpace::Collapse;
    CoreTextPreservedOptions preserved;
};
struct CoreTextIntrinsicSizes {
    std::int32_t min_content = 0, max_content = 0; // Q1000 advances, not ink
};
// Borrowed sorted intrinsic-only endpoints. Exactly one entry per Atomic span;
// owner addresses the original span array, never a glyph or normalized byte.
// Separate storage leaves ordinary paragraph spans unchanged.
struct CoreTextAtomicWidths {
    std::size_t owner;
    std::int64_t min_content, max_content;
};
// Selected outer advance in Q1000 and integer-pixel outer vertical extent.
// Baseline is the outer bottom (before=extent, after=0). Exactly one sorted
// record per Atomic span; never substitute intrinsic MIN/MAX endpoints.
struct CoreTextAtomicGeometry {
    std::size_t owner;
    std::int64_t advance;
    int extent;
};
// Private final outer placement: x/advance are Q1000; top/extent are pixels.
// Text fragments contain only actual glyphs. Separate output keeps ordinary
// paragraph/result holder footprints fixed and acquires storage only for atoms.
struct CoreTextAtomicPlacement {
    std::size_t owner, line;
    std::int32_t x, advance;
    int top, extent;
};
struct CoreTextParagraphAtomics {
    std::vector<CoreTextAtomicPlacement> placements;
    CoreTextParagraphAtomics() : placements(0) {}
};

// Same normalization, participating struts and line plan as the packet builder,
// without preparing glyphs or allocating final lines/packets. Baselines are
// relative to the content origin and may be signed, including zero-height lines.
struct CoreTextParagraphMetrics {
    int height = 0, first_baseline = 0, last_baseline = 0;
    std::size_t line_count = 0;
};
TextSystemStatus measure_core_text_paragraph_metrics(const TextSystem&,
    const CoreTextParagraphSpan*,std::size_t,const CoreTextParagraphOptions&,
    CoreTextParagraphMetrics&,CoreTextLayoutLimits = {},CoreTextParagraphUsage* = nullptr,
    const CoreTextAtomicGeometry* = nullptr,std::size_t atomic_count = 0,
    const CoreTextPreservedOptions* = nullptr) noexcept;

// Private ordinary ASCII width prerequisite. Shares paragraph normalization,
// font-run grouping and exact range advances, without glyph packets or lines.
// Uniform requests may use signed spacing; mixed requests require zero spacing.
// Owner boundaries never add breaks. Forced events divide natural lines; optional
// events add min-content cuts only when wrapping, and remain transparent to
// font-run tracking when unused. Atoms have separate borrowed MIN/MAX endpoints,
// split shaping runs and retain half-sum exterior tracking (none between atoms).
// Wrapping cuts on each side; nowrap still uses distinct atomic endpoints.
// max_content is floored at min_content.
// No containing width/indent/alignment, host callback or integer-font projection.
// Failure preserves output and reports attempted work/capacity. This does not
// yet admit production Flex/Grid/table layout or contextual Unicode inputs.
// PreserveLines collapses spaces/Tab/FF and retains CR/LF.
// Preserve retains SPACE/Tab/CR/LF; PreserveSpaces converts Tab/CR/LF to
// retained SPACE. CRLF coalesces across owners. Wrapping Preserve modes hang
// trailing whitespace for MIN unless break_spaces; MAX retains natural width.
// Tab stops use the resolved containing context at each candidate origin.
// Preserved modes refuse atoms/FF. Selected metrics and final packets require
// an explicit preserved context; packet output contains actual text glyphs only.
TextSystemStatus measure_core_text_intrinsic(const TextSystem&,
    const CoreTextParagraphSpan*,std::size_t count,const CoreTextIntrinsicOptions&,
    CoreTextIntrinsicSizes&,CoreTextLayoutLimits = {},CoreTextParagraphUsage* = nullptr,
    const CoreTextAtomicWidths* = nullptr,std::size_t atomic_count = 0) noexcept;

// Private builder, also used by the bounded LayoutEngine collector. Collapsing ASCII text
// with explicit forced/optional events; source/style boundaries never add cuts. Equal font
// requests share measurement/preparation even across owner boundaries. Different
// font requests start separate scalar runs (no cross-font spacing/shaping).
// Common-baseline line metrics include glyph struts, atomic extents and the block
// strut. Atomic spans require selected geometry plus a separate placement output;
// neither events nor atoms enter glyph packets. Both outputs are transactional.
// Usage includes failed attempts. Resources, inputs and output holders must stay
// immutable/disjoint throughout the call (outputs publish only on success).
// Preserved modes require the owned context below, use the same exact-width
// metric plan, retain real SPACE glyphs and omit Tab/break markers from packets.
// Alignment uses natural tails when fitting at end/forced lines, trimmed tails
// at soft wraps/overflow. Tabs always use the pre-alignment content-origin grid.
TextSystemStatus build_core_text_paragraph(const TextSystem&,
    const CoreTextParagraphSpan*, std::size_t count, const CoreTextParagraphOptions&,
    CoreTextParagraph&, CoreTextLayoutLimits = {}, CoreTextParagraphUsage* = nullptr,
    const CoreTextAtomicGeometry* = nullptr,std::size_t atomic_count = 0,
    CoreTextParagraphAtomics* = nullptr,
    const CoreTextPreservedOptions* = nullptr) noexcept;

} // namespace jellyframe::detail
