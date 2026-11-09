#include "render_core/computed_size_storage.h"
#include "css_dimension_precision.h"
#include <array>
#include <atomic>
#include <memory>
#include <utility>

namespace jellyframe::detail {
struct ComputedSizeData {
    struct Slot { std::int64_t units = 0; SizeKind kind = SizeKind::Pixels; };
    mutable std::atomic<std::size_t> references{1};
    std::array<Slot, 7> slots{};
    std::uint8_t mask = 0;
    ComputedSizeData() = default;
    ComputedSizeData(const ComputedSizeData& other) : slots(other.slots), mask(other.mask) {}
};
ComputedSizeStorage::ComputedSizeStorage(const ComputedSizeStorage& other) noexcept : data_(other.data_) {
    if (data_) data_->references.fetch_add(1, std::memory_order_relaxed);
}
ComputedSizeStorage::ComputedSizeStorage(ComputedSizeStorage&& other) noexcept : data_(other.data_) {
    other.data_ = nullptr;
}
ComputedSizeStorage& ComputedSizeStorage::operator=(const ComputedSizeStorage& other) noexcept {
    if (this != &other) { ComputedSizeStorage copy(other); std::swap(data_, copy.data_); }
    return *this;
}
ComputedSizeStorage& ComputedSizeStorage::operator=(ComputedSizeStorage&& other) noexcept {
    if (this != &other) { reset(); data_ = other.data_; other.data_ = nullptr; }
    return *this;
}
ComputedSizeStorage::~ComputedSizeStorage() { reset(); }
void ComputedSizeStorage::reset() noexcept {
    const auto* old = data_;
    data_ = nullptr;
    if (old && old->references.fetch_sub(1, std::memory_order_acq_rel) == 1) delete old;
}
bool ComputedSizeStorage::set(SizeProperty property, SizeKind kind, double value) {
    const auto index = static_cast<unsigned>(property);
    if (index >= 7 || (kind != SizeKind::Pixels && kind != SizeKind::Percent) ||
        (property == SizeProperty::FlexBasis && kind == SizeKind::Percent)) return false;
    if (kind == SizeKind::Percent && std::isfinite(value)) value = std::min(1000.0, value);
    std::int64_t units = 0;
    if (!encode_dimension_units(value, units)) return false;
    if (units % kDimensionPrecisionScale == 0) { clear(property); return true; }
    SizeKind old_kind;
    std::int64_t old_units = 0;
    if (get(property, old_kind, old_units) && old_kind == kind && old_units == units) return true;
    auto next = data_ ? std::make_unique<ComputedSizeData>(*data_) : std::make_unique<ComputedSizeData>();
    next->slots[index] = {units, kind};
    next->mask |= static_cast<std::uint8_t>(1U << index);
    reset();
    data_ = next.release();
    return true;
}
void ComputedSizeStorage::clear(SizeProperty property) {
    const auto index = static_cast<unsigned>(property);
    if (index >= 7 || !data_ || !(data_->mask & (1U << index))) return;
    const auto mask = static_cast<std::uint8_t>(data_->mask & ~(1U << index));
    if (!mask) { reset(); return; }
    auto next = std::make_unique<ComputedSizeData>(*data_);
    next->mask = mask;
    next->slots[index] = {};
    reset();
    data_ = next.release();
}
bool ComputedSizeStorage::get(SizeProperty property, SizeKind& kind, std::int64_t& units) const noexcept {
    const auto index = static_cast<unsigned>(property);
    if (index >= 7 || !data_ || !(data_->mask & (1U << index))) return false;
    kind = data_->slots[index].kind;
    units = data_->slots[index].units;
    return true;
}
bool ComputedSizeStorage::equals(const ComputedSizeStorage& other) const noexcept {
    if (data_ == other.data_) return true;
    if (!data_ || !other.data_ || data_->mask != other.data_->mask) return false;
    for (unsigned index = 0; index < 7; ++index) {
        if ((data_->mask & (1U << index)) &&
            (data_->slots[index].kind != other.data_->slots[index].kind ||
             data_->slots[index].units != other.data_->slots[index].units)) return false;
    }
    return true;
}
} // namespace jellyframe::detail
