#include "shaping_binding.h"
#include "shaping_catalog_sha256.h"
#include <array>
#include <cstring>
#include <limits>

namespace jellyframe {
namespace shaping_binding {
namespace {
std::array<unsigned char, 32> identity(
    const shaping_catalog::View &catalog,
    const shaping_normalization::Tables &tables,
    const shaping_normalization::Eligibility &eligibility,
    std::string_view font_bytes) noexcept {
  std::array<unsigned char, 200> material{};
  constexpr char domain[] = "JellyFrame shaping binding v1";
  std::memcpy(material.data(), domain, sizeof(domain) - 1);
  std::size_t offset = 40;
  const auto catalog_hash = shaping_catalog::sha256(catalog.bytes());
  const auto companion_hash = shaping_catalog::sha256(catalog.companion_bytes());
  const auto tables_hash = tables.identity();
  const auto eligibility_hash = shaping_catalog::sha256(eligibility.bytes());
  const auto font_hash = shaping_catalog::sha256(font_bytes);
  for (const auto *hash : {&catalog_hash, &companion_hash, &tables_hash,
                           &eligibility_hash, &font_hash}) {
    std::memcpy(material.data() + offset, hash->data(), hash->size());
    offset += hash->size();
  }
  return shaping_catalog::sha256(std::string_view(
      reinterpret_cast<const char *>(material.data()), offset));
}
} // namespace

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
  // JFFONT2 proves correspondence only for its declared scalar coverage. The
  // catalog may additionally contain shaping-closure GIDs absent from it.
  if (font_metadata.coverage_bits != 4 || catalog_metadata.coverage_bits != 4)
    return Status::invalid;
  for (std::size_t i = 0; i < font.glyph_count(); ++i) {
    const auto declared = font.glyph_at_with_coverage(i);
    std::uint16_t mapped_gid = 0;
    FontGlyphView raster;
    if (!catalog.map_scalar_declared(declared.codepoint, mapped_gid) ||
        mapped_gid != declared.glyph_id || !catalog.find_gid(mapped_gid, raster) ||
        declared.advance != raster.advance ||
        declared.bearing_x != raster.bearing_x ||
        declared.bearing_y != raster.bearing_y ||
        declared.width != raster.width || declared.height != raster.height ||
        declared.row_stride != raster.row_stride ||
        declared.coverage_bytes != raster.coverage_bytes ||
        (declared.coverage_bytes &&
         std::memcmp(declared.coverage, raster.coverage,
                     declared.coverage_bytes) != 0))
      return Status::invalid;
  }
  if (generation_ == std::numeric_limits<std::uint64_t>::max())
    return Status::exhausted;
  Snapshot candidate;
  candidate.catalog_ = catalog;
  candidate.font_ = font;
  candidate.tables_ = tables;
  candidate.eligibility_ = eligibility;
  candidate.resource_id_ = resource_id;
  candidate.generation_ = generation_ + 1;
  const auto *font_data = reinterpret_cast<const char *>(font.data_);
  const auto font_size = std::uint32_t(font.data_[16]) |
      (std::uint32_t(font.data_[17]) << 8) |
      (std::uint32_t(font.data_[18]) << 16) |
      (std::uint32_t(font.data_[19]) << 24);
  candidate.content_identity_ = identity(catalog, tables, eligibility,
                                         {font_data, font_size});
  output = candidate;
  generation_ = candidate.generation_;
  return Status::ok;
}

} // namespace shaping_binding
} // namespace jellyframe
