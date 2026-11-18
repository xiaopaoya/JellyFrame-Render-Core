#pragma once

#include "render_core/font_resource.h"
#include "shaping_binding.h"
#include "shaping_catalog.h"
#include "shaping_catalog_sha256.h"
#include "shaping_normalization.h"

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>

namespace jellyframe::shaping_binding_internal {
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
template<class Limits>
inline bool valid_limits(const Limits &limits) noexcept {
  return limits.bytes && limits.bytes <= hard_bytes && limits.work &&
      limits.work <= hard_work && limits.packet_bytes &&
      limits.packet_bytes <= 1024U * 1024U && limits.scratch_bytes &&
      limits.scratch_bytes <= hard_scratch_bytes && limits.source_bytes &&
      limits.source_bytes <= 65536 && limits.input_scalars &&
      limits.input_scalars <= 4096 && limits.expanded_scalars &&
      limits.expanded_scalars <= 16384 && limits.clusters &&
      limits.clusters <= 4096 && limits.glyphs && limits.glyphs <= 4096;
}
inline bool nonzero(const std::array<unsigned char, 32> &digest) noexcept {
  return std::any_of(digest.begin(), digest.end(),
                     [](unsigned char c) { return c != 0; });
}
inline bool add_size(std::size_t value, std::size_t &sum) noexcept {
  if (value > std::numeric_limits<std::size_t>::max() - sum) return false;
  sum += value;
  return true;
}

struct OwnedResources {
  std::string font_bytes, catalog_bytes, companion_bytes, tables_bytes,
      eligibility_bytes;
  FontResource font;
  shaping_catalog::View catalog;
  shaping_normalization::Tables tables;
  shaping_normalization::Eligibility eligibility;
  shaping_binding::Snapshot snapshot;
  std::uint16_t selection = 0;
};

// The contextual API charges for verifying the raster font against its own
// expected digest. The legacy API retains its existing cross-check and budget.
template<class Status, class State, class Inputs, class Limits>
Status create(const Inputs &in, const Limits &limits, std::uint64_t &work,
              const std::array<unsigned char, 32> *font_identity,
              std::shared_ptr<const State> &output) noexcept {
  if (!valid_limits(limits))
    return Status::limit;
  if (work > limits.work) return Status::limit;
  WorkTransaction work_transaction(work);
  if (!in.resource_id || in.selection > 4 || !in.shaping_glyph_count ||
      !in.raster_size || !in.weight || !nonzero(in.source_identity) ||
      !nonzero(in.profile_identity) ||
      (font_identity && !nonzero(*font_identity)) ||
      !nonzero(in.catalog_identity) ||
      !nonzero(in.companion_identity) || !nonzero(in.tables_identity) ||
      !nonzero(in.eligibility_identity))
    return Status::invalid;
  std::size_t total = 0;
  for (auto span : {in.font_resource, in.shaping_catalog, in.shaping_companion,
                    in.normalization_tables, in.normalization_eligibility}) {
    if (font_identity && (span.empty() || !span.data())) return Status::invalid;
    if (!add_size(span.size(), total)) return Status::limit;
  }
  if (total > limits.bytes || total > hard_bytes) return Status::limit;
  if (total > work) return Status::limit;
  work -= total;
  try {
    auto candidate = std::make_shared<State>();
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
    if ((font_identity && !add_size(candidate->font_bytes.size(), font_validation_cost)) ||
        !add_size(declared_bitmap_bytes, font_validation_cost) ||
        !add_size(declared_font_glyphs, font_validation_cost) ||
        font_validation_cost > work)
      return Status::limit;
    work -= font_validation_cost;
    if (font_identity && shaping_catalog::sha256(candidate->font_bytes) != *font_identity)
      return Status::identity_mismatch;
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
    output = std::move(candidate);
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
} // namespace jellyframe::shaping_binding_internal
