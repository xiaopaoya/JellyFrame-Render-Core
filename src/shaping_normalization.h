#pragma once
#include "shaping_catalog.h"
namespace jellyframe {
namespace shaping_normalization {
using Hash = std::array<unsigned char, 32>;
enum class Status {
  ok,
  malformed,
  identity,
  limit,
  capacity,
  unsupported,
  overlap
};
struct Limits {
  std::size_t bytes = 131072, source_bytes = 65536, input_scalars = 4096,
              expanded_scalars = 16384;
};
class Tables {
public:
  std::string_view bytes() const noexcept { return bytes_; }
  Hash identity() const noexcept { return hash_; }

private:
  std::string_view bytes_;
  Hash hash_{};
  friend Status validate_tables(std::string_view, const Hash &, const Limits &,
                                std::uint64_t &, Tables &) noexcept;
};
class Eligibility {
public:
  std::string_view bytes() const noexcept { return bytes_; }
  bool matches(const Tables &tables,
               const shaping_catalog::View &catalog) const noexcept;

private:
  std::string_view bytes_;
  Hash tables_identity_{};
  Hash catalog_identity_{};
  friend Status validate_eligibility(std::string_view, const Hash &,
                                     const Tables &,
                                     const shaping_catalog::View &,
                                     std::uint64_t &, Eligibility &) noexcept;
};
struct Scalar {
  std::uint32_t scalar = 0, source_begin = 0, source_end = 0;
  std::uint16_t gid = 0;
};
Status validate_tables(std::string_view, const Hash &, const Limits &,
                       std::uint64_t &, Tables &) noexcept;
Status validate_eligibility(std::string_view, const Hash &, const Tables &,
                            const shaping_catalog::View &, std::uint64_t &,
                            Eligibility &) noexcept;
// Strict selected-profile run; no line/control/default-ignorable processing.
// Borrowed resources must remain immutable. Scratch may change on refusal;
// output and published count do not. All writable spans must be disjoint.
Status normalize(const Tables &, const Eligibility &,
                 const shaping_catalog::View &, std::string_view, Scalar *,
                 std::size_t, Scalar *, std::size_t, std::size_t &,
                 const Limits &, std::uint64_t &) noexcept;
} // namespace shaping_normalization
} // namespace jellyframe
