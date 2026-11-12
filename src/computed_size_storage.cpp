#include "render_core/computed_size_storage.h"
#include "css_dimension_precision.h"
#include <array>
#include <atomic>
#include <memory>
#include <utility>

namespace jellyframe::detail {
struct ComputedSizeData {
    struct Slot {
        std::int64_t units = 0;
        std::int32_t percentage_units = 0;
        SizeKind kind = SizeKind::Pixels;
    };
    mutable std::atomic<std::size_t> references{1};
    std::array<Slot, 11> slots{};
    std::uint16_t mask = 0;
    ComputedSizeData() = default;
    ComputedSizeData(const ComputedSizeData& other) : slots(other.slots), mask(other.mask) {}
};
std::size_t ComputedSizeStorage::retained_payload_bytes() const noexcept {
    return data_ ? sizeof(ComputedSizeData) : 0;
}
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
    if (index >= 11 || (kind != SizeKind::Pixels && kind != SizeKind::Percent) ||
        (property == SizeProperty::FlexBasis && kind == SizeKind::Percent)) return false;
    const bool inset = index >= 7;
    if (kind == SizeKind::Percent && std::isfinite(value)) {
        if (inset && std::abs(value) > 1000.0) return false;
        value = std::min(1000.0, value);
    }
    std::int64_t units = 0;
    if (!encode_dimension_units(inset ? std::abs(value) : value, units)) return false;
    if (inset && std::signbit(value)) units = -units;
    if (units % kDimensionPrecisionScale == 0 && !(inset && kind == SizeKind::Percent)) {
        clear(property); return true;
    }
    SizeKind old_kind;
    std::int64_t old_units = 0;
    std::int64_t old_percentage_units = 0;
    if (get(property, old_kind, old_units, old_percentage_units) && old_kind == kind &&
        old_units == units && old_percentage_units == 0) return true;
    auto next = data_ ? std::make_unique<ComputedSizeData>(*data_) : std::make_unique<ComputedSizeData>();
    next->slots[index] = {units, 0, kind};
    next->mask |= static_cast<std::uint16_t>(1U << index);
    reset();
    data_ = next.release();
    return true;
}
bool ComputedSizeStorage::set_length_percentage(
    SizeProperty property, double length_pixels, double percentage) {
    const auto index = static_cast<unsigned>(property);
    if (index >= 11 || !std::isfinite(length_pixels) || !std::isfinite(percentage) ||
        std::abs(length_pixels) > std::numeric_limits<int>::max() ||
        std::abs(percentage) > 1000.0) return false;
    std::int64_t length_units = 0;
    const auto encode_signed = [](double value, std::int64_t& units) {
        const bool negative = std::signbit(value);
        if (!encode_dimension_units(std::abs(value), units)) return false;
        if (negative) units = -units;
        return true;
    };
    if (!encode_signed(length_pixels, length_units)) return false;
    std::int64_t percentage_units = 0;
    if (!encode_dimension_units(std::abs(percentage), percentage_units)) return false;
    if (std::signbit(percentage)) percentage_units = -percentage_units;
    SizeKind old_kind;
    std::int64_t old_units = 0;
    std::int64_t old_percentage_units = 0;
    if (get(property, old_kind, old_units, old_percentage_units) &&
        old_kind == SizeKind::LengthPercentage && old_units == length_units &&
        old_percentage_units == percentage_units) return true;
    auto next = data_ ? std::make_unique<ComputedSizeData>(*data_) : std::make_unique<ComputedSizeData>();
    next->slots[index] = {length_units, static_cast<std::int32_t>(percentage_units), SizeKind::LengthPercentage};
    next->mask |= static_cast<std::uint16_t>(1U << index);
    reset();
    data_ = next.release();
    return true;
}
void ComputedSizeStorage::clear(SizeProperty property) {
    const auto index = static_cast<unsigned>(property);
    if (index >= 11 || !data_ || !(data_->mask & (1U << index))) return;
    const auto mask = static_cast<std::uint16_t>(data_->mask & ~(1U << index));
    if (!mask) { reset(); return; }
    auto next = std::make_unique<ComputedSizeData>(*data_);
    next->mask = mask;
    next->slots[index] = {};
    reset();
    data_ = next.release();
}
bool ComputedSizeStorage::get(SizeProperty property, SizeKind& kind, std::int64_t& units) const noexcept {
    SizeKind stored_kind;
    std::int64_t stored_units = 0;
    std::int64_t percentage_units = 0;
    if (!get(property, stored_kind, stored_units, percentage_units) ||
        stored_kind == SizeKind::LengthPercentage) return false;
    kind = stored_kind;
    units = stored_units;
    return true;
}
bool ComputedSizeStorage::get(SizeProperty property, SizeKind& kind, std::int64_t& units,
                              std::int64_t& percentage_units) const noexcept {
    const auto index = static_cast<unsigned>(property);
    if (index >= 11 || !data_ || !(data_->mask & (1U << index))) return false;
    kind = data_->slots[index].kind;
    units = data_->slots[index].units;
    percentage_units = data_->slots[index].percentage_units;
    return true;
}
bool ComputedSizeStorage::read_typed(SizeProperty property, SizeKind& kind,
                                     std::int64_t& units,
                                     std::int64_t& percentage_units) const noexcept {
    return get(property, kind, units, percentage_units);
}
bool ComputedSizeStorage::equals(const ComputedSizeStorage& other) const noexcept {
    if (data_ == other.data_) return true;
    if (!data_ || !other.data_ || data_->mask != other.data_->mask) return false;
    for (unsigned index = 0; index < 11; ++index) {
        if ((data_->mask & (1U << index)) &&
            (data_->slots[index].kind != other.data_->slots[index].kind ||
             data_->slots[index].units != other.data_->slots[index].units ||
             data_->slots[index].percentage_units != other.data_->slots[index].percentage_units)) return false;
    }
    return true;
}
} // namespace jellyframe::detail
