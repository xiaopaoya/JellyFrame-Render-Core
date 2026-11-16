#pragma once
#include "shaped_text_packet.h"
#include "shaping_lookup_executor.h"
#include "shaping_normalization.h"
namespace jellyframe {
namespace shaped_service {
struct Request {
  const shaping_catalog::View *catalog = nullptr;
  const shaping_normalization::Tables *tables = nullptr;
  const shaping_normalization::Eligibility *eligibility = nullptr;
  shaped_packet::Header packet;
};
// Every span is scratch and may change on refusal. All spans must be disjoint.
struct Buffers {
  shaping_normalization::Scalar *normalization_scratch = nullptr,
                                *normalized = nullptr;
  std::size_t normalization_capacity = 0, normalized_capacity = 0;
  shaping_lookup::Glyph *input = nullptr, *scratch = nullptr,
                        *positioned = nullptr;
  std::size_t glyph_capacity = 0;
  shaped_packet::Glyph *glyphs = nullptr;
  shaped_packet::Cluster *clusters = nullptr;
  std::size_t cluster_capacity = 0;
};
struct Limits {
  shaping_normalization::Limits normalization;
  shaped_packet::Limits packet;
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
  limit,
  capacity,
  overlap,
  normalization,
  lookup,
  overflow,
  packet
};
// Raw UTF8 admission, normalization and catalog choices are Core-owned.
// Packet and metrics publish together. No renderer/service registration
// implied.
Status prepare(const Request &, std::string_view, const Buffers &,
               const Limits &, std::uint64_t &, std::string &,
               Metrics &) noexcept;
} // namespace shaped_service
} // namespace jellyframe
