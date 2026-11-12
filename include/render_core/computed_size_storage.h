#pragma once
#include <cstdint>
#include <cstddef>

namespace jellyframe::detail {
enum class SizeProperty : std::uint8_t {
    Width, Height, MinWidth, MinHeight, MaxWidth, MaxHeight, FlexBasis,
    InsetTop, InsetRight, InsetBottom, InsetLeft
};
enum class SizeKind : std::uint8_t { Pixels, Percent, LengthPercentage };
struct ComputedSizeData;
// One-word shared immutable owner. Fractional values and unresolved mixed units
// occupy slots so copies remain cheap and preserve source-unit provenance.
// Copying does not allocate; mutations publish a replacement only on success.
class ComputedSizeStorage {
public:
    ComputedSizeStorage() noexcept = default;
    ComputedSizeStorage(const ComputedSizeStorage&) noexcept;
    ComputedSizeStorage(ComputedSizeStorage&&) noexcept;
    ComputedSizeStorage& operator=(const ComputedSizeStorage&) noexcept;
    ComputedSizeStorage& operator=(ComputedSizeStorage&&) noexcept;
    ~ComputedSizeStorage();
    bool set(SizeProperty, SizeKind, double);
    // Store a bounded signed length plus percentage pair for any dimension
    // slot. The legacy three-output getter intentionally hides this kind.
    bool set_length_percentage(SizeProperty, double length_pixels, double percentage);
    void clear(SizeProperty);
    void reset() noexcept;
    bool get(SizeProperty, SizeKind&, std::int64_t&) const noexcept;
    bool get(SizeProperty, SizeKind&, std::int64_t& units,
             std::int64_t& percentage_units) const noexcept;
    // Presence-aware typed read. Unlike the legacy getter this reports a
    // LengthPercentage slot and leaves outputs untouched on absence.
    bool read_typed(SizeProperty, SizeKind&, std::int64_t& units,
                    std::int64_t& percentage_units) const noexcept;
    std::size_t retained_payload_bytes() const noexcept;
    bool empty() const noexcept { return data_ == nullptr; }
    bool equals(const ComputedSizeStorage&) const noexcept;
private:
    const ComputedSizeData* data_ = nullptr;
};
} // namespace jellyframe::detail
