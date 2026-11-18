#pragma once
#include "render_core/contextual_text.h"
#include <vector>

#define JELLYFRAME_CONTEXTUAL_TEXT_PARAGRAPH_API_VERSION 1

namespace jellyframe::contextual_text {

// Bounded horizontal LTR paragraph, normal collapsed ASCII whitespace.
// Soft breaks follow spaces and ASCII hyphens (except before a slash).
// Unicode emergency breaks are permitted only at validated shaping clusters.
// Bidi source and special separator/soft-hyphen policies refuse. This is not
// a full Unicode line-break algorithm or a DOM Layout result.
struct ParagraphOptions {
  std::int32_t size_q1000 = 16000;
  std::int32_t width_q1000 = 0;
  std::int32_t line_height_q1000 = 0; // zero selects natural font height
  bool nowrap = false;
  bool emergency_wrap = false;
};
struct ParagraphLimits {
  Limits shaping;
  std::size_t lines = 128;
  std::size_t shape_calls = 2048;
  std::size_t result_bytes = 1024U * 1024U;
};
struct ParagraphUsage {
  std::size_t shape_calls = 0;
  std::uint64_t work = 0;
  // Retained explicit capacities only; excludes Binding assets, allocator
  // metadata, prior results and transient allocations. Not a peak-memory bound.
  std::size_t result_bytes = 0;
};
struct ParagraphLine {
  // Half-open intervals in Paragraph::text(). Consumed trailing spaces are
  // outside source_end, inside consumed_end. Packets cover source_begin..end.
  std::size_t source_begin = 0, source_end = 0, consumed_end = 0;
  std::int32_t baseline_y_q1000 = 0;
  Metrics metrics;
  std::string packet;
};
class Paragraph {
public:
  Paragraph() noexcept = default;
  bool valid() const noexcept { return bool(state_); }
  const Binding& binding() const noexcept;
  std::string_view source() const noexcept;
  std::string_view text() const noexcept;
  const std::vector<ParagraphLine>& lines() const noexcept;
  std::int32_t width_q1000() const noexcept;
  std::int32_t height_q1000() const noexcept;
private:
  struct State;
  std::shared_ptr<const State> state_;
  friend Status layout_paragraph(const Binding&, std::string_view,
      const ParagraphOptions&, const ParagraphLimits&, std::uint64_t&,
      Paragraph&, ParagraphUsage*) noexcept;
};

// Publishes only a completely prepared/validated immutable paragraph. Failure
// preserves output; attempts and work remain charged (unlike primitive prepare).
// A failed primitive attempt conservatively consumes its remaining allowance.
// Usage is replaced on every call. Input may alias the previous result.
Status layout_paragraph(const Binding&, std::string_view source,
    const ParagraphOptions&, const ParagraphLimits&, std::uint64_t& work,
    Paragraph& output, ParagraphUsage* usage = nullptr) noexcept;

} // namespace jellyframe::contextual_text
