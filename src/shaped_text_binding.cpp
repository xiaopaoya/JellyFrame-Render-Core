#include "render_core/shaped_text_binding.h"

#include "render_core/font_resource.h"
#include "shaped_text_packet.h"
#include "shaped_text_service.h"
#include "shaping_binding.h"
#include "shaping_catalog.h"
#include "shaping_binding_owned.h"
#include "shaping_normalization.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

namespace jellyframe::shaped_text {
namespace {
using shaping_binding_internal::WorkTransaction;
using shaping_binding_internal::add_size;
using shaping_binding_internal::valid_limits;
Status packet_status(shaped_packet::Status status) noexcept {
  switch (status) {
  case shaped_packet::Status::ok: return Status::ok;
  case shaped_packet::Status::limit: return Status::limit;
  case shaped_packet::Status::allocation: return Status::allocation;
  case shaped_packet::Status::malformed: return Status::malformed;
  }
  return Status::invalid;
}
Status service_status(shaped_service::Status status) noexcept {
  switch (status) {
  case shaped_service::Status::ok: return Status::ok;
  case shaped_service::Status::limit: return Status::limit;
  case shaped_service::Status::capacity: return Status::limit;
  case shaped_service::Status::packet: return Status::malformed;
  case shaped_service::Status::invalid:
  case shaped_service::Status::normalization:
  case shaped_service::Status::lookup:
  case shaped_service::Status::overflow:
  case shaped_service::Status::overlap: return Status::invalid;
  }
  return Status::invalid;
}
} // namespace

struct Binding::State : shaping_binding_internal::OwnedResources {};

Binding::Binding(std::shared_ptr<const State> state) noexcept
    : state_(std::move(state)) {}
bool Binding::valid() const noexcept { return state_ && state_->snapshot.valid(); }
std::uint64_t Binding::resource_id() const noexcept {
  return valid() ? state_->snapshot.resource_id() : 0;
}
std::uint64_t Binding::generation() const noexcept {
  return valid() ? state_->snapshot.generation() : 0;
}
std::uint16_t Binding::selection() const noexcept {
  return valid() ? state_->selection : 0;
}
Digest Binding::content_identity() const noexcept {
  return valid() ? state_->snapshot.content_identity() : Digest{};
}

Status create_binding(const Inputs &in, const Limits &limits,
                      std::uint64_t &work, Binding &output) noexcept {
  std::shared_ptr<const Binding::State> candidate;
  const auto status = shaping_binding_internal::create<Status>(
      in, limits, work, nullptr, candidate);
  if (status == Status::ok) output = Binding(std::move(candidate));
  return status;
}

Status prepare(const Binding &binding, std::int32_t size_q1000,
               std::string_view source, const Limits &limits,
               std::uint64_t &work, std::string &output,
               Metrics &metrics) noexcept {
  if (!binding.valid()) return Status::invalid;
  if (!valid_limits(limits))
    return Status::limit;
  if (work > limits.work) return Status::limit;
  WorkTransaction work_transaction(work);
  if (size_q1000 <= 0 || source.size() > limits.source_bytes)
    return Status::invalid;
  std::size_t scratch_bytes = 0;
  const auto add_array = [&](std::size_t count, std::size_t width) {
    if (count && width > std::numeric_limits<std::size_t>::max() / count)
      return false;
    return add_size(count * width, scratch_bytes);
  };
  if (!add_array(limits.input_scalars, sizeof(shaping_normalization::Scalar)) ||
      !add_array(limits.expanded_scalars, sizeof(shaping_normalization::Scalar)) ||
      !add_array(limits.glyphs, sizeof(shaping_lookup::Glyph) * 3U) ||
      !add_array(limits.glyphs, sizeof(shaped_packet::Glyph)) ||
      !add_array(limits.clusters, sizeof(shaped_packet::Cluster)) ||
      scratch_bytes > limits.scratch_bytes)
    return Status::limit;
  try {
    std::vector<shaping_normalization::Scalar> norm(limits.input_scalars),
        normalized(limits.expanded_scalars);
    std::vector<shaping_lookup::Glyph> input(limits.glyphs),
        scratch(limits.glyphs), positioned(limits.glyphs);
    std::vector<shaped_packet::Glyph> glyphs(limits.glyphs);
    std::vector<shaped_packet::Cluster> clusters(limits.clusters);
    shaped_service::Buffers buffers{norm.data(), normalized.data(), norm.size(),
        normalized.size(), input.data(), scratch.data(), positioned.data(),
        input.size(), glyphs.data(), clusters.data(), clusters.size()};
    shaped_service::Limits service_limits;
    service_limits.normalization.source_bytes = limits.source_bytes;
    service_limits.normalization.input_scalars = limits.input_scalars;
    service_limits.normalization.expanded_scalars = limits.expanded_scalars;
    service_limits.packet.bytes = limits.packet_bytes;
    service_limits.packet.source_bytes = limits.source_bytes;
    service_limits.packet.clusters = limits.clusters;
    service_limits.packet.glyphs = limits.glyphs;
    shaped_service::Request request{&binding.state_->snapshot, size_q1000,
                                    binding.selection()};
    std::string draft;
    shaped_service::Metrics service_metrics;
    const auto status = shaped_service::prepare(request, source, buffers,
        service_limits, work, draft, service_metrics);
    if (status != shaped_service::Status::ok) return service_status(status);
    Metrics draft_metrics{service_metrics.advance_x, service_metrics.advance_y,
        service_metrics.ascent, service_metrics.descent, service_metrics.line_gap,
        service_metrics.ink_left, service_metrics.ink_top,
        service_metrics.ink_right, service_metrics.ink_bottom,
        service_metrics.glyph_count, service_metrics.cluster_count,
        service_metrics.has_ink};
    output.swap(draft);
    metrics = draft_metrics;
    work_transaction.commit();
    return Status::ok;
  } catch (const std::bad_alloc &) {
    return Status::allocation;
  } catch (...) {
    return Status::invalid;
  }
}

Status validate_packet(const Binding &binding, std::string_view bytes,
                       const Limits &limits, std::uint64_t &work) noexcept {
  if (!binding.valid()) return Status::invalid;
  if (!valid_limits(limits) || bytes.size() > limits.packet_bytes)
    return Status::limit;
  if (work > limits.work) return Status::limit;
  WorkTransaction work_transaction(work);
  shaped_packet::Limits packet_limits;
  packet_limits.bytes = std::min(limits.packet_bytes, packet_limits.bytes);
  packet_limits.source_bytes = std::min(limits.source_bytes, packet_limits.source_bytes);
  packet_limits.clusters = std::min(limits.clusters, packet_limits.clusters);
  packet_limits.glyphs = std::min(limits.glyphs, packet_limits.glyphs);
  shaped_packet::View packet;
  auto status = shaped_packet::decode(bytes, packet_limits, work, packet);
  if (status != shaped_packet::Status::ok) return packet_status(status);
  const auto header = packet.header();
  if (header.resource_id != binding.resource_id() ||
      header.binding_generation != binding.generation() ||
      header.binding_identity != binding.content_identity() ||
      header.selection != binding.selection())
    return Status::identity_mismatch;
  for (std::size_t i = 0; i < packet.glyph_count(); ++i) {
    if (!work) return Status::limit;
    --work;
    shaped_packet::Glyph glyph;
    FontGlyphView raster;
    if (!packet.glyph(i, glyph) || glyph.gid > UINT16_MAX ||
        !binding.state_->catalog.find_gid(static_cast<std::uint16_t>(glyph.gid), raster))
      return Status::malformed;
  }
  work_transaction.commit();
  return Status::ok;
}

} // namespace jellyframe::shaped_text
