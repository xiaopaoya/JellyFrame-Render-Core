#include "shaping_binding.h"
#include <limits>

namespace jellyframe {
namespace shaping_binding {

Status BindingState::bind(const shaping_catalog::View &catalog,
                          const FontResource &font,
                          const shaping_normalization::Tables &tables,
                          const shaping_normalization::Eligibility &eligibility,
                          std::uint64_t resource_id,
                          Snapshot &output) noexcept {
  if (!resource_id || !font.valid() || catalog.bytes().empty() ||
      catalog.companion_bytes().empty() || tables.bytes().empty() ||
      eligibility.bytes().empty() || !eligibility.matches(tables, catalog))
    return Status::invalid;
  const auto &font_metadata = font.metadata();
  const auto catalog_metadata = catalog.metadata();
  if (font_metadata.profile != FontResourceProfile::ScalarBitmap ||
      font_metadata.units_per_em != catalog_metadata.units_per_em ||
      font_metadata.raster_pixel_size != catalog_metadata.raster_pixel_size ||
      font_metadata.weight != catalog_metadata.weight ||
      font_metadata.coverage_bits != catalog_metadata.coverage_bits ||
      font_metadata.ascent != catalog_metadata.ascent ||
      font_metadata.descent != catalog_metadata.descent ||
      font_metadata.line_gap != catalog_metadata.line_gap ||
      font_metadata.source_sha256 != catalog_metadata.source_sha256)
    return Status::invalid;
  if (generation_ == std::numeric_limits<std::uint64_t>::max())
    return Status::exhausted;
  Snapshot candidate;
  candidate.catalog_ = catalog;
  candidate.font_ = font;
  candidate.tables_ = tables;
  candidate.eligibility_ = eligibility;
  candidate.resource_id_ = resource_id;
  candidate.generation_ = generation_ + 1;
  output = candidate;
  generation_ = candidate.generation_;
  return Status::ok;
}

} // namespace shaping_binding
} // namespace jellyframe
