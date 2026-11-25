#pragma once
#include "render_core/feature_config.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#if !defined(JELLYFRAME_RENDER_CORE_CONTEXTUAL_TEXT_ENABLED) || !JELLYFRAME_RENDER_CORE_CONTEXTUAL_TEXT_ENABLED
#error "Contextual text requires JELLYFRAME_ENABLE_CONTEXTUAL_TEXT"
#endif
#define JELLYFRAME_CONTEXTUAL_TEXT_API_VERSION 1

namespace jellyframe { struct FrameBuffer; struct Rect; struct Color; }
namespace jellyframe::detail { struct ContextualTextInkBindingAccess; }
namespace jellyframe::contextual_text {
using Digest = std::array<unsigned char, 32>;
// Exact independently authenticated resource identities. Providing digests alone
// is not authentication; the caller owns the package/trust decision. All five
// byte spans are copied and validated before publication.
struct Inputs {
  std::string_view font_resource, shaping_catalog, shaping_companion;
  std::string_view normalization_tables, normalization_eligibility;
  Digest font_resource_identity{}, source_identity{}, profile_identity{};
  Digest catalog_identity{}, companion_identity{}, tables_identity{}, eligibility_identity{};
  std::uint32_t shaping_glyph_count = 0;
  std::uint64_t resource_id = 0;
  std::uint16_t raster_size = 0, weight = 0, selection = 0;
};
struct Limits {
  std::size_t bytes = 16U * 1024U * 1024U;
  std::uint64_t work = 32U * 1024U * 1024U;
  std::size_t packet_bytes = 1024U * 1024U;
  std::size_t scratch_bytes = 8U * 1024U * 1024U;
  std::size_t source_bytes = 65536, input_scalars = 4096, expanded_scalars = 16384;
  std::size_t clusters = 4096, glyphs = 4096;
};
struct Metrics {
  std::int32_t advance_x = 0, advance_y = 0, ascent = 0, descent = 0, line_gap = 0;
  std::int32_t ink_left = 0, ink_top = 0, ink_right = 0, ink_bottom = 0;
  std::size_t glyph_count = 0, cluster_count = 0;
  bool has_ink = false;
};
enum class Status { ok, invalid, identity_mismatch, malformed, limit, allocation };
// Immutable copyable owner, separate from the scalar-profile packet Binding.
// Factory publications start at generation1; exact content identity pins all
// five resources. Distinct replacement resources require distinct resource IDs.
class Binding {
public:
  Binding() noexcept = default;
  bool valid() const noexcept;
  std::uint64_t resource_id() const noexcept;
  std::uint64_t generation() const noexcept;
  std::uint16_t selection() const noexcept;
  std::uint16_t weight() const noexcept;
  Digest content_identity() const noexcept;
private:
  struct State;
  explicit Binding(std::shared_ptr<const State>) noexcept;
  static Status create_impl(const Inputs&, const Limits&, std::uint64_t&, Binding&) noexcept;
  std::shared_ptr<const State> state_;
  friend Status create_binding(const Inputs&, const Limits&, std::uint64_t&, Binding&) noexcept;
  friend Status prepare(const Binding&, std::int32_t, std::string_view, const Limits&,
                        std::uint64_t&, std::string&, Metrics&) noexcept;
  friend Status validate_packet(const Binding&, std::string_view, const Limits&, std::uint64_t&) noexcept;
  friend Status paint(const Binding&, std::string_view, ::jellyframe::FrameBuffer&,
                      std::int32_t, std::int32_t, const ::jellyframe::Rect&,
                      const ::jellyframe::Color&, const Limits&, std::uint64_t&) noexcept;
  friend struct ::jellyframe::detail::ContextualTextInkBindingAccess;
};
// On refusal output/work remain unchanged; previous immutable owners survive.
Status create_binding(const Inputs&, const Limits&, std::uint64_t& work, Binding& output) noexcept;
// Strict bounded horizontal selected-profile shaping: Core owns Unicode14 NFC,
// GSUB/GPOS execution, cluster intervals, final GIDs and Q1000 geometry. Unsupported
// source refuses; no scalar fallback, CSS layout, JFTP cache or paint integration.
// Packet, metrics and work publish transactionally; scratch is per-call/bounded.
Status prepare(const Binding&, std::int32_t size_q1000, std::string_view utf8_source,
               const Limits&, std::uint64_t& work, std::string& packet, Metrics&) noexcept;
// Structural framing, exact binding/selection identity, and final GID membership.
// This does not re-shape source to prove arbitrary packet geometry authenticity.
Status validate_packet(const Binding&, std::string_view, const Limits&, std::uint64_t&) noexcept;
// Typed JFSP2 paint with complete preflight before any pixel write. Refusal
// preserves pixels and work. Resources and packet stay immutable for the call;
// packet bytes must not overlap target storage. Baseline uses Q1000 CSS pixels.
Status paint(const Binding&, std::string_view packet, ::jellyframe::FrameBuffer&,
             std::int32_t baseline_x_q1000, std::int32_t baseline_y_q1000,
             const ::jellyframe::Rect& clip, const ::jellyframe::Color&,
             const Limits&, std::uint64_t& work) noexcept;
} // namespace jellyframe::contextual_text
