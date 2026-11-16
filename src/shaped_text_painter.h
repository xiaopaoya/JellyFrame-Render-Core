#pragma once

#include "render_core/geometry.h"
#include "shaped_text_packet.h"
#include "shaping_binding.h"

namespace jellyframe {
struct FrameBuffer;
namespace shaped_painter {

enum class Status { ok, invalid, identity_mismatch, missing_glyph, limit };

// Draws a decoded packet directly into a caller-owned framebuffer. The packet
// and snapshot are borrowed and must remain immutable for the call.
Status paint(const shaped_packet::View &, const shaping_binding::Snapshot &,
             FrameBuffer &, std::int32_t baseline_x_q1000,
             std::int32_t baseline_y_q1000, Rect clip, Color,
             std::uint64_t &work) noexcept;

} // namespace shaped_painter
} // namespace jellyframe
