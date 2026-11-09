#pragma once
#include <cstdint>

namespace jellyframe::detail {
enum class SizeProperty : std::uint8_t { Width, Height, MinWidth, MinHeight, MaxWidth, MaxHeight, FlexBasis };
enum class SizeKind : std::uint8_t { Pixels, Percent };
struct ComputedSizeData;
// One-word shared immutable owner. Only fractional values occupy a slot.
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
    void clear(SizeProperty);
    void reset() noexcept;
    bool get(SizeProperty, SizeKind&, std::int64_t&) const noexcept;
    bool empty() const noexcept { return data_ == nullptr; }
    bool equals(const ComputedSizeStorage&) const noexcept;
private:
    const ComputedSizeData* data_ = nullptr;
};
} // namespace jellyframe::detail
