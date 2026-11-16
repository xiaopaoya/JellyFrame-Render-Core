#include "render_core/shaped_text_binding.h"

#include "render_core/font_resource.h"
#include "shaped_text_packet.h"
#include "shaped_text_service.h"
#include "shaping_binding.h"
#include "shaping_catalog.h"
#include "shaping_catalog_sha256.h"
#include "shaping_normalization.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

namespace jellyframe::shaped_text {
namespace {
constexpr std::size_t hard_bytes = 16U * 1024U * 1024U;
constexpr std::uint64_t hard_work = 32U * 1024U * 1024U;
constexpr std::size_t hard_scratch_bytes = 8U * 1024U * 1024U;
class WorkTransaction {
public:
  explicit WorkTransaction(std::uint64_t &work) noexcept
      : work_(work), initial_(work) {}
  ~WorkTransaction() { if (!committed_) work_ = initial_; }
  void commit() noexcept { committed_ = true; }
private:
  std::uint64_t &work_;
  std::uint64_t initial_;
  bool committed_ = false;
};
bool valid_limits(const Limits &limits) noexcept {
  return limits.bytes && limits.bytes <= hard_bytes && limits.work &&
      limits.work <= hard_work && limits.packet_bytes &&
      limits.packet_bytes <= 1024U * 1024U && limits.scratch_bytes &&
      limits.scratch_bytes <= hard_scratch_bytes && limits.source_bytes &&
      limits.source_bytes <= 65536 && limits.input_scalars &&
      limits.input_scalars <= 4096 && limits.expanded_scalars &&
      limits.expanded_scalars <= 16384 && limits.clusters &&
      limits.clusters <= 4096 && limits.glyphs && limits.glyphs <= 4096;
}
bool nonzero(const Digest &digest) noexcept {
  return std::any_of(digest.begin(), digest.end(),
                     [](unsigned char c) { return c != 0; });
}
bool add_size(std::size_t value, std::size_t &sum) noexcept {
  if (value > std::numeric_limits<std::size_t>::max() - sum) return false;
  sum += value;
  return true;
}
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

struct Binding::State {
  std::string font_bytes, catalog_bytes, companion_bytes, tables_bytes,
      eligibility_bytes;
  FontResource font;
  shaping_catalog::View catalog;
  shaping_normalization::Tables tables;
  shaping_normalization::Eligibility eligibility;
  shaping_binding::Snapshot snapshot;
  std::uint16_t selection = 0;
};

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
  if (!valid_limits(limits))
    return Status::limit;
  if (work > limits.work) return Status::limit;
  WorkTransaction work_transaction(work);
  if (!in.resource_id || in.selection > 4 || !in.shaping_glyph_count ||
      !in.raster_size || !in.weight || !nonzero(in.source_identity) ||
      !nonzero(in.profile_identity) || !nonzero(in.catalog_identity) ||
      !nonzero(in.companion_identity) || !nonzero(in.tables_identity) ||
      !nonzero(in.eligibility_identity))
    return Status::invalid;
  std::size_t total = 0;
  for (auto size : {in.font_resource.size(), in.shaping_catalog.size(),
                    in.shaping_companion.size(), in.normalization_tables.size(),
                    in.normalization_eligibility.size()})
    if (!add_size(size, total)) return Status::limit;
  if (total > limits.bytes || total > hard_bytes) return Status::limit;
  if (total > work) return Status::limit;
  work -= total;
  try {
    auto candidate = std::make_shared<Binding::State>();
    candidate->font_bytes.assign(in.font_resource);
    candidate->catalog_bytes.assign(in.shaping_catalog);
    candidate->companion_bytes.assign(in.shaping_companion);
    candidate->tables_bytes.assign(in.normalization_tables);
    candidate->eligibility_bytes.assign(in.normalization_eligibility);

    FontResourceLimits font_limits;
    font_limits.max_bytes = std::min(font_limits.max_bytes, limits.bytes);
    const auto declared_font_glyphs = candidate->font_bytes.size() >= 24
        ? std::size_t(static_cast<unsigned char>(candidate->font_bytes[20])) |
              (std::size_t(static_cast<unsigned char>(candidate->font_bytes[21])) << 8) |
              (std::size_t(static_cast<unsigned char>(candidate->font_bytes[22])) << 16) |
              (std::size_t(static_cast<unsigned char>(candidate->font_bytes[23])) << 24)
        : 0;
    const auto declared_bitmap_bytes = candidate->font_bytes.size() >= 36
        ? std::size_t(static_cast<unsigned char>(candidate->font_bytes[32])) |
              (std::size_t(static_cast<unsigned char>(candidate->font_bytes[33])) << 8) |
              (std::size_t(static_cast<unsigned char>(candidate->font_bytes[34])) << 16) |
              (std::size_t(static_cast<unsigned char>(candidate->font_bytes[35])) << 24)
        : 0;
    std::size_t font_validation_cost = candidate->font_bytes.size();
    if (!add_size(declared_bitmap_bytes, font_validation_cost) ||
        !add_size(declared_font_glyphs, font_validation_cost) ||
        font_validation_cost > work)
      return Status::limit;
    work -= font_validation_cost;
    font_limits.max_work = std::min(font_limits.max_work, font_validation_cost);
    const auto font_status = candidate->font.load(
        reinterpret_cast<const std::uint8_t *>(candidate->font_bytes.data()),
        candidate->font_bytes.size(), font_limits);
    if (font_status != FontResourceStatus::Loaded)
      return font_status == FontResourceStatus::ResourceLimit ? Status::limit
                                                               : Status::invalid;

    shaping_catalog::Identity catalog_identity;
    std::copy(in.source_identity.begin(), in.source_identity.end(),
              catalog_identity.shaping.source);
    std::copy(in.profile_identity.begin(), in.profile_identity.end(),
              catalog_identity.shaping.profile);
    catalog_identity.shaping.glyph_count = in.shaping_glyph_count;
    catalog_identity.companion_sha256 = in.companion_identity;
    catalog_identity.trusted_companion = candidate->companion_bytes;
    catalog_identity.catalog_sha256 = in.catalog_identity;
    catalog_identity.raster_size = in.raster_size;
    catalog_identity.weight = in.weight;
    shaping_catalog::Limits catalog_limits;
    catalog_limits.bytes = std::min(catalog_limits.bytes, limits.bytes);
    std::uint64_t remaining_work = work;
    auto status = shaping_catalog::bind(candidate->catalog_bytes,
        catalog_identity, catalog_limits, remaining_work, candidate->catalog);
    if (status != shaping_catalog::Status::ok)
      return status == shaping_catalog::Status::limit ? Status::limit
                                                       : Status::identity_mismatch;
    work = remaining_work;

    remaining_work = work;
    const auto table_status = shaping_normalization::validate_tables(
        candidate->tables_bytes, in.tables_identity, {}, remaining_work,
        candidate->tables);
    if (table_status != shaping_normalization::Status::ok)
      return table_status == shaping_normalization::Status::limit
                 ? Status::limit : Status::identity_mismatch;
    work = remaining_work;
    remaining_work = work;
    const auto eligibility_status = shaping_normalization::validate_eligibility(
        candidate->eligibility_bytes, in.eligibility_identity,
        candidate->tables, candidate->catalog, remaining_work,
        candidate->eligibility);
    if (eligibility_status != shaping_normalization::Status::ok)
      return eligibility_status == shaping_normalization::Status::limit
                 ? Status::limit : Status::identity_mismatch;
    work = remaining_work;

    std::size_t cross_check_cost = candidate->font.glyph_count();
    if (candidate->font.glyph_count() >
        (std::numeric_limits<std::size_t>::max() - cross_check_cost) / 33U)
      return Status::limit;
    cross_check_cost += candidate->font.glyph_count() * 33U;
    if (!add_size(candidate->font_bytes.size(), cross_check_cost) ||
        cross_check_cost > work)
      return Status::limit;
    work -= cross_check_cost;
    shaping_binding::BindingState binding_state;
    shaping_binding::Snapshot snapshot;
    if (binding_state.bind(candidate->catalog, candidate->font,
                           candidate->tables, candidate->eligibility,
                           in.resource_id, snapshot) != shaping_binding::Status::ok)
      return Status::invalid;
    candidate->snapshot = snapshot;
    candidate->selection = in.selection;
    output = Binding(std::move(candidate));
    work_transaction.commit();
    return Status::ok;
  } catch (const std::bad_alloc &) {
    return Status::allocation;
  } catch (const std::length_error &) {
    return Status::allocation;
  } catch (...) {
    return Status::invalid;
  }
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
