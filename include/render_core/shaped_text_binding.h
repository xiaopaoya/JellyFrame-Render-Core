#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#if !defined(JELLYFRAME_RENDER_CORE_TEXT_SHAPING_ENABLED) || \
    !JELLYFRAME_RENDER_CORE_TEXT_SHAPING_ENABLED
#error "JellyFrame shaped-text binding requires JELLYFRAME_ENABLE_TEXT_SHAPING"
#endif

namespace jellyframe::shaped_text {

using Digest = std::array<unsigned char, 32>;

struct Inputs {
  std::string_view font_resource;
  std::string_view shaping_catalog;
  std::string_view shaping_companion;
  std::string_view normalization_tables;
  std::string_view normalization_eligibility;
  Digest source_identity{};
  Digest profile_identity{};
  Digest catalog_identity{};
  Digest companion_identity{};
  Digest tables_identity{};
  Digest eligibility_identity{};
  std::uint32_t shaping_glyph_count = 0;
  std::uint64_t resource_id = 0;
  std::uint16_t raster_size = 0;
  std::uint16_t weight = 0;
  std::uint16_t selection = 0;
};

struct Limits {
  std::size_t bytes = 16U * 1024U * 1024U;
  std::uint64_t work = 32U * 1024U * 1024U;
  std::size_t packet_bytes = 1024U * 1024U;
  std::size_t scratch_bytes = 8U * 1024U * 1024U;
  std::size_t source_bytes = 65536;
  std::size_t input_scalars = 4096;
  std::size_t expanded_scalars = 16384;
  std::size_t clusters = 4096;
  std::size_t glyphs = 4096;
};

struct Metrics {
  std::int32_t advance_x = 0, advance_y = 0, ascent = 0, descent = 0,
               line_gap = 0;
  std::int32_t ink_left = 0, ink_top = 0, ink_right = 0, ink_bottom = 0;
  std::size_t glyph_count = 0, cluster_count = 0;
  bool has_ink = false;
};

enum class Status {
  ok,
  invalid,
  identity_mismatch,
  malformed,
  limit,
  allocation
};

// Copyable immutable owner of the exact validated bytes used for shaping.
// Each factory-created binding starts at generation 1; replacement assets need
// a distinct resource ID and are represented by a new immutable handle.
class Binding {
public:
  Binding() noexcept = default;
  bool valid() const noexcept;
  std::uint64_t resource_id() const noexcept;
  std::uint64_t generation() const noexcept;
  std::uint16_t selection() const noexcept;
  Digest content_identity() const noexcept;

private:
  struct State;
  explicit Binding(std::shared_ptr<const State>) noexcept;
  std::shared_ptr<const State> state_;
  friend Status create_binding(const Inputs &, const Limits &, std::uint64_t &,
                               Binding &) noexcept;
  friend Status prepare(const Binding &, std::int32_t, std::string_view,
                        const Limits &, std::uint64_t &, std::string &,
                        Metrics &) noexcept;
  friend Status validate_packet(const Binding &, std::string_view,
                                const Limits &, std::uint64_t &) noexcept;
};

// On refusal, output and work remain unchanged; existing handles are immutable.
Status create_binding(const Inputs &, const Limits &, std::uint64_t &work,
                     Binding &output) noexcept;

// Core owns normalization, lookup execution, positioning, clustering and packet
// identity. Scratch and packet are per-call, bounded by Limits. Selection comes
// from the handle; packet, metrics and work remain unchanged on refusal.
Status prepare(const Binding &, std::int32_t size_q1000,
               std::string_view utf8_source, const Limits &,
               std::uint64_t &work, std::string &packet,
               Metrics &metrics) noexcept;

// Validates JFSP v2 framing, budget, selection, ID, generation, full digest,
// and catalog membership for every emitted GID before accepting the packet.
// Work is restored on refusal.
Status validate_packet(const Binding &, std::string_view packet,
                       const Limits &, std::uint64_t &work) noexcept;

} // namespace jellyframe::shaped_text
