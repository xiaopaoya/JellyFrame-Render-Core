#pragma once

#include "render_core/font_resource.h"
#include "shaping_catalog.h"
#include "shaping_normalization.h"

namespace jellyframe {
namespace shaping_binding {

enum class Status { ok, invalid, exhausted };

// Borrowed immutable inputs must outlive every snapshot that uses them.
class Snapshot {
public:
  bool valid() const noexcept { return resource_id_ != 0 && generation_ != 0; }
  std::uint64_t resource_id() const noexcept { return resource_id_; }
  std::uint64_t generation() const noexcept { return generation_; }
  const shaping_catalog::View &catalog() const noexcept { return catalog_; }
  const FontResource &font() const noexcept { return font_; }
  const shaping_normalization::Tables &tables() const noexcept { return tables_; }
  const shaping_normalization::Eligibility &eligibility() const noexcept {
    return eligibility_;
  }

private:
  friend class BindingState;
  shaping_catalog::View catalog_;
  FontResource font_;
  shaping_normalization::Tables tables_;
  shaping_normalization::Eligibility eligibility_;
  std::uint64_t resource_id_ = 0, generation_ = 0;
};

// Refusal preserves both output and the current generation.
class BindingState {
public:
  Status bind(const shaping_catalog::View &, const FontResource &,
              const shaping_normalization::Tables &,
              const shaping_normalization::Eligibility &,
              std::uint64_t resource_id, Snapshot &output) noexcept;
  std::uint64_t generation() const noexcept { return generation_; }

private:
  std::uint64_t generation_ = 0;
};

} // namespace shaping_binding
} // namespace jellyframe
