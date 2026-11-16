#pragma once
#include "shaping_catalog.h"
#include "shaping_resource.h"
#include <cstddef>
#include <cstdint>
namespace jellyframe {
namespace shaping_lookup {
struct Glyph {
  std::uint16_t gid = 0;
  std::uint32_t source_begin = 0, source_end = 0;
  std::int32_t advance_x = 0, advance_y = 0, offset_x = 0, offset_y = 0;
};
struct Ledger {
  std::uint64_t work = 0, output_glyphs = 0;
};
enum class Status {
  ok,
  invalid_resource,
  invalid_input,
  capacity,
  overlap,
  limit,
  coordinate_overflow,
  unsupported
};
// Source intervals are nonempty and contiguous, with identical intervals for
// canonical expansions. Partial overlap, even at a shared start, refuses.
// Input, scratch and output spans must be disjoint. Only scratch and ledger
// change on failure. Output count and output records publish together after all
// work. Substitute clears all metric fields; resolve final GID advances before
// position. Both operations retain the catalog-bound companion identity.
Status substitute(const shaping_catalog::View &catalog, unsigned selection,
                  const Glyph *input, std::size_t count, Glyph *scratch,
                  std::size_t scratch_capacity, Glyph *output,
                  std::size_t output_capacity, std::size_t &output_count,
                  Ledger &ledger) noexcept;
// Catalog identity and companion are inseparable for both operations.
Status position(const shaping_catalog::View &catalog, unsigned selection,
                const Glyph *input, std::size_t count, Glyph *scratch,
                std::size_t scratch_capacity, Glyph *output,
                std::size_t output_capacity, std::size_t &output_count,
                Ledger &ledger) noexcept;
} // namespace shaping_lookup
} // namespace jellyframe
