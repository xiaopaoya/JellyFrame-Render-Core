#include "render_core/contextual_text_paragraph.h"
#include "shaped_text_packet.h"
#include "utf8_scalar.h"
#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace jellyframe::contextual_text {
struct Paragraph::State {
  Binding binding;
  std::string source, text;
  std::vector<ParagraphLine> lines;
  std::int32_t width = 0, height = 0;
};
namespace {
const Binding empty_binding;
const std::vector<ParagraphLine> empty_lines;
bool whitespace(unsigned char c) noexcept {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}
bool take(std::uint64_t& work, std::uint64_t amount) noexcept {
  if (amount > work) { work = 0; return false; }
  work -= amount; return true;
}
bool add(std::size_t& value, std::size_t amount, std::size_t ceiling) noexcept {
  if (value > ceiling || amount > ceiling - value) return false;
  value += amount; return true;
}
std::size_t trimmed_end(std::string_view text, std::size_t end) noexcept {
  return end && text[end - 1] == ' ' ? end - 1 : end;
}
}
const Binding& Paragraph::binding() const noexcept { return state_ ? state_->binding : empty_binding; }
std::string_view Paragraph::source() const noexcept { return state_ ? state_->source : std::string_view{}; }
std::string_view Paragraph::text() const noexcept { return state_ ? state_->text : std::string_view{}; }
const std::vector<ParagraphLine>& Paragraph::lines() const noexcept { return state_ ? state_->lines : empty_lines; }
std::int32_t Paragraph::width_q1000() const noexcept { return state_ ? state_->width : 0; }
std::int32_t Paragraph::height_q1000() const noexcept { return state_ ? state_->height : 0; }

Status layout_paragraph(const Binding& binding, std::string_view source,
    const ParagraphOptions& options, const ParagraphLimits& limits,
    std::uint64_t& work, Paragraph& output, ParagraphUsage* reported) noexcept {
  ParagraphUsage usage;
  const auto initial_work = work;
  struct Report {
    ParagraphUsage* out; ParagraphUsage& usage; std::uint64_t initial;
    std::uint64_t& work;
    ~Report() { usage.work = initial - work; if (out) *out = usage; }
  } report{reported, usage, initial_work, work};
  if (!binding.valid() || options.size_q1000 <= 0 || options.width_q1000 < 0 ||
      options.line_height_q1000 < 0) return Status::invalid;
  if (!limits.lines || limits.lines > 4096 || !limits.shape_calls ||
      limits.shape_calls > 16384 || !limits.result_bytes ||
      limits.result_bytes > 8U * 1024U * 1024U ||
      source.size() > limits.shaping.source_bytes || source.size() > 65536 ||
      !limits.shaping.work || limits.shaping.work > 32U * 1024U * 1024U ||
      work > limits.shaping.work) return Status::limit;
  try {
    // Source scan/copy is charged before allocation; retained output is never
    // modified until every line has succeeded.
    if (!take(work, source.size() + 1)) return Status::limit;
    auto draft = std::make_shared<Paragraph::State>();
    draft->binding = binding;
    std::size_t bytes = sizeof(Paragraph::State);
    if (!add(bytes, source.size(), limits.result_bytes)) return Status::limit;
    draft->source.assign(source);
    draft->text.reserve(source.size());
    bool pending_space = false;
    for (unsigned char c : source) {
      if (whitespace(c)) { pending_space = !draft->text.empty(); continue; }
      if (c < 0x20 || c == 0x7f) return Status::invalid;
      if (pending_space) draft->text.push_back(' ');
      pending_space = false;
      draft->text.push_back(static_cast<char>(c));
    }
    // This entry point has no bidi/reordering or special separator policy.
    for (std::size_t at = 0; at < draft->text.size();) {
      if (!take(work, 1)) return Status::limit;
      std::uint32_t scalar = 0;
      if (!detail::read_utf8_scalar(draft->text, at, scalar)) return Status::invalid;
      if (scalar == 0x85 || scalar == 0xad || scalar == 0x200b ||
          (scalar >= 0x200e && scalar <= 0x200f) ||
          (scalar >= 0x2028 && scalar <= 0x202e) ||
          (scalar >= 0x2066 && scalar <= 0x2069) || scalar == 0xfeff ||
          (scalar >= 0x590 && scalar <= 0x8ff) ||
          (scalar >= 0xfb1d && scalar <= 0xfdff) ||
          (scalar >= 0xfe70 && scalar <= 0xfefc) ||
          (scalar >= 0x10800 && scalar <= 0x10fff) ||
          (scalar >= 0x1e800 && scalar <= 0x1efff)) return Status::invalid;
    }
    // Capacity accounting includes retained original + collapsed strings.
    bytes = sizeof(Paragraph::State);
    if (!add(bytes, draft->source.capacity(), limits.result_bytes) ||
        !add(bytes, draft->text.capacity(), limits.result_bytes)) return Status::limit;
    usage.result_bytes = bytes;
    const auto& text = draft->text;
    auto effective_shaping = limits.shaping;
    auto shape = [&](std::string_view part, std::string& packet, Metrics& metrics) -> Status {
      if (usage.shape_calls >= limits.shape_calls) return Status::limit;
      ++usage.shape_calls;
      if (!take(work, part.size() + 1)) return Status::limit;
      const auto status = prepare(binding, options.size_q1000, part, effective_shaping, work, packet, metrics);
      if (status != Status::ok) {
        // Primitive prepare rolls back refusal work. Do not offer an attacker
        // unlimited failed attempts through the paragraph ledger.
        work = 0;
        return status;
      }
      const auto checked = validate_packet(binding, packet, limits.shaping, work);
      if (checked != Status::ok) { work = 0; return checked; }
      if (metrics.advance_x < 0 || metrics.advance_y != 0) return Status::invalid;
      return Status::ok;
    };
    std::string whole_packet;
    Metrics whole_metrics;
    auto status = shape(text, whole_packet, whole_metrics);
    if (status != Status::ok) return status;
    shaped_packet::Limits packet_limits;
    packet_limits.bytes = limits.shaping.packet_bytes;
    packet_limits.source_bytes = limits.shaping.source_bytes;
    packet_limits.clusters = limits.shaping.clusters;
    packet_limits.glyphs = limits.shaping.glyphs;
    shaped_packet::View whole;
    if (shaped_packet::decode(whole_packet, packet_limits, work, whole) != shaped_packet::Status::ok) {
      work = 0; return Status::limit;
    }
    if (whole.source() != text) return Status::invalid;
    const auto natural_height = std::int64_t(whole_metrics.ascent) - whole_metrics.descent + whole_metrics.line_gap;
    const auto line_height = options.line_height_q1000 ? options.line_height_q1000 : natural_height;
    if (line_height <= 0 || line_height > INT32_MAX) return Status::invalid;
    // The packet remains live during candidate/final preparation, so reserve
    // its storage against the explicit scratch ceiling in addition to prepare.
    if (whole_packet.capacity() >= effective_shaping.scratch_bytes) return Status::limit;
    effective_shaping.scratch_bytes -= whole_packet.capacity();
    auto line_reserve = std::min(limits.lines, whole.cluster_count());
    if (line_reserve > (limits.result_bytes - bytes) / sizeof(ParagraphLine)) return Status::limit;
    draft->lines.reserve(line_reserve);
    if (!add(bytes, draft->lines.capacity() * sizeof(ParagraphLine), limits.result_bytes)) return Status::limit;
    usage.result_bytes = bytes;
    std::size_t begin = 0, first_cluster = 0;
    while (begin < text.size()) {
      if (draft->lines.size() >= limits.lines) return Status::limit;
      std::size_t normal_end = 0, emergency_end = 0, first_normal = 0, first_end = 0;
      std::int32_t normal_width = 0, emergency_width = 0;
      auto candidate = [&](std::size_t end, std::int32_t& width) -> Status {
        const auto visible_end = trimmed_end(text, end);
        if (visible_end <= begin) return Status::invalid;
        std::string packet; Metrics metrics;
        const auto result = shape(std::string_view(text).substr(begin, visible_end - begin), packet, metrics);
        width = metrics.advance_x;
        return result;
      };
      if (options.nowrap) {
        normal_end = text.size();
        status = candidate(normal_end, normal_width);
        if (status != Status::ok) return status;
      } else {
        // Examine every permitted candidate; shaping advances need not be
        // monotonic when contextual substitutions or positioning are active.
        for (auto i = first_cluster; i < whole.cluster_count(); ++i) {
          if (!take(work, 1)) return Status::limit;
          shaped_packet::Cluster cluster;
          if (!whole.cluster(i, cluster)) return Status::malformed;
          const auto end = std::size_t(cluster.source_end);
          if (end <= begin) continue;
          // Trimming a line-ending space must not cut a shaping cluster.
          // If context joined it to preceding source, refuse that boundary.
          if (text[end - 1] == ' ' && cluster.source_begin + 1 != cluster.source_end)
            continue;
          if (!first_end) first_end = end;
          const bool normal = end == text.size() || text[end - 1] == ' ' ||
              (text[end - 1] == '-' && text[end] != '/');
          if (normal && !first_normal) first_normal = end;
          if (!normal && (!options.emergency_wrap || first_normal)) continue;
          std::int32_t width = 0;
          status = candidate(end, width);
          if (status != Status::ok) return status;
          if (width <= options.width_q1000) {
            if (normal) { normal_end = end; normal_width = width; }
            else { emergency_end = end; emergency_width = width; }
          }
        }
      }
      std::size_t chosen = normal_end ? normal_end : emergency_end;
      std::int32_t chosen_width = normal_end ? normal_width : emergency_width;
      if (!chosen) {
        chosen = options.emergency_wrap ? first_end : first_normal;
        if (!chosen) return Status::invalid;
        status = candidate(chosen, chosen_width);
        if (status != Status::ok) return status;
      }
      std::size_t consumed = chosen;
      if (chosen < text.size() && text[chosen] == ' ') {
        bool singleton_space = false;
        for (auto i = first_cluster; i < whole.cluster_count(); ++i) {
          if (!take(work, 1)) return Status::limit;
          shaped_packet::Cluster cluster;
          if (!whole.cluster(i, cluster)) return Status::malformed;
          if (cluster.source_begin == chosen && cluster.source_end == chosen + 1) {
            singleton_space = true; break;
          }
          if (cluster.source_end > chosen) break;
        }
        if (!singleton_space) return Status::invalid;
        ++consumed;
      }
      ParagraphLine line;
      line.source_begin = begin;
      line.source_end = trimmed_end(text, chosen);
      line.consumed_end = consumed;
      status = shape(std::string_view(text).substr(begin, line.source_end - begin), line.packet, line.metrics);
      if (status != Status::ok) return status;
      if (line.metrics.advance_x != chosen_width) return Status::invalid;
      const auto half_leading = (line_height -
          (std::int64_t(whole_metrics.ascent) - whole_metrics.descent)) / 2;
      const auto baseline = half_leading + whole_metrics.ascent +
          std::int64_t(draft->lines.size()) * line_height;
      const auto height = (std::int64_t(draft->lines.size()) + 1) * line_height;
      if (baseline < INT32_MIN || baseline > INT32_MAX || height > INT32_MAX) return Status::invalid;
      line.baseline_y_q1000 = static_cast<std::int32_t>(baseline);
      if (!add(bytes, line.packet.capacity(), limits.result_bytes)) return Status::limit;
      draft->width = std::max(draft->width, line.metrics.advance_x);
      draft->height = static_cast<std::int32_t>(height);
      draft->lines.push_back(std::move(line));
      usage.result_bytes = bytes;
      begin = consumed;
      while (first_cluster < whole.cluster_count()) {
        shaped_packet::Cluster cluster;
        if (!take(work, 1) || !whole.cluster(first_cluster, cluster)) return Status::limit;
        if (cluster.source_end > begin) break;
        ++first_cluster;
      }
    }
    output.state_ = std::move(draft);
    return Status::ok;
  } catch (const std::bad_alloc&) { return Status::allocation; }
    catch (const std::length_error&) { return Status::allocation; }
    catch (...) { return Status::invalid; }
}
} // namespace jellyframe::contextual_text
