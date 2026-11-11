#pragma once

#include "render_core/layout.h"
#include "flex_length_precision.h"
#include "proportional_share.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>
#include <type_traits>

namespace jellyframe::detail {

enum class FlexLengthChargeDecision { Continue, ResourceLimit };

// All state belongs to one layout pass. Sizes are border-box sizes; shrink
// weights use the separately recorded nonnegative inner flex base size.
inline std::int64_t flex_length_coordinate_maximum(const FlexLengthPrecisionItem&) {
    return kDimensionPrecisionMaximum;
}
template<class Item>
inline auto flex_length_clamp(std::int64_t value, const Item& item) -> decltype(item.target) {
    if (item.maximum >= 0) value = std::min<std::int64_t>(value, item.maximum);
    value = std::max<std::int64_t>(value, item.minimum);
    using Coordinate = decltype(item.target);
    return static_cast<Coordinate>(std::clamp<std::int64_t>(value, 0, flex_length_coordinate_maximum(item)));
}

// floor(a*b/d), with b <= d, without a widened compiler-specific integer.
// Ordinary UI sizes take the one-multiply fast path.
inline std::uint64_t flex_mul_div(std::uint64_t a, std::uint64_t b, std::uint64_t d) {
    return proportional_share(a,b,d);
}

struct FlexLengthWeight { std::uint64_t low = 0, high = 0; };
inline bool flex_weight_at_least(FlexLengthWeight a, FlexLengthWeight b) {
    return a.high > b.high || (a.high == b.high && a.low >= b.low);
}
inline FlexLengthWeight flex_weight_add(FlexLengthWeight a, FlexLengthWeight b) {
    const auto low = a.low + b.low;
    return {low,a.high+b.high+(low<a.low?1U:0U)};
}
inline FlexLengthWeight flex_weight_subtract(FlexLengthWeight a, FlexLengthWeight b) {
    return {a.low-b.low,a.high-b.high-(a.low<b.low?1U:0U)};
}
// Exact floor(amount*weight/total), weight<=total. The admitted source/layout
// item counts keep total below 2^127, so doubling a remainder cannot overflow
// the two limbs. Ordinary weights retain the original one-multiply fast path.
inline std::uint64_t flex_weight_share(std::uint64_t amount, FlexLengthWeight weight, FlexLengthWeight total) {
    if (!total.high) return flex_mul_div(amount,weight.low,total.low);
    std::uint64_t quotient = 0;
    FlexLengthWeight remainder;
    for (int bit=63;bit>=0;--bit) {
        quotient *= 2;
        remainder = flex_weight_add(remainder,remainder);
        if (flex_weight_at_least(remainder,total)) {
            remainder = flex_weight_subtract(remainder,total); ++quotient;
        }
        if ((amount>>bit)&1U) {
            remainder = flex_weight_add(remainder,weight);
            if (flex_weight_at_least(remainder,total)) {
                remainder = flex_weight_subtract(remainder,total); ++quotient;
            }
        }
    }
    return quotient;
}
inline FlexLengthWeight flex_length_weight_parts(const FlexLengthPrecisionItem& item, bool grow) {
    const auto factor = static_cast<std::uint64_t>(std::max(0,
        grow ? item.child->style.flex_grow : item.child->style.flex_shrink));
    if (grow) return {factor, 0};
    // A Q1000 inner base times an INT_MAX factor exceeds 64 bits. Split the
    // base before multiplying; each partial product fits independently.
    const auto base = static_cast<std::uint64_t>(std::max<std::int64_t>(0,item.inner_base));
    const auto low = (base & 0xffffffffULL) * factor;
    const auto upper = (base >> 32) * factor;
    const auto result = low + (upper << 32);
    return {result, (upper >> 32) + (result < low ? 1U : 0U)};
}

template<class Item> struct FlexLengthRange {
    Item* data;
    std::size_t count;
    Item* begin() const { return data; }
    Item* end() const { return count ? data + count : data; }
    std::size_t size() const { return count; }
    bool empty() const { return count == 0; }
};

// Intrinsic planning uses bounded subranges and charges every solver scan.
// Ordinary layout instantiates the same algorithm with a no-op work callback.
// A refused charge stops before its scan, but earlier scans may have changed
// scratch items. Callers must discard a refused plan before publishing geometry.
// The charge callback owns the ledger and must reject before exceeding its cap.
template<class Range, class Coordinate, class Charge>
inline bool resolve_flex_lengths(Range& items, Coordinate main_size, Coordinate gap, const Charge& charge) {
    if (items.empty()) return true;
    std::int64_t hypothetical_sum = 0;
    std::int64_t available = main_size - static_cast<std::int64_t>(std::max<Coordinate>(0, gap)) * static_cast<std::int64_t>(items.size() - 1);
    if (charge(items.size())!=FlexLengthChargeDecision::Continue) return false;
    for (auto& item : items) {
        available -= item.margins;
        hypothetical_sum += item.hypothetical;
    }
    const bool grow = hypothetical_sum < available;
    if (charge(items.size())!=FlexLengthChargeDecision::Continue) return false;
    for (auto& item : items) {
        item.target = item.base;
        const int factor = grow ? item.child->style.flex_grow : item.child->style.flex_shrink;
        item.frozen = factor <= 0 || (grow ? item.base > item.hypothetical : item.base < item.hypothetical);
        if (item.frozen) item.target = item.hypothetical;
    }
    const auto free_space = [&](std::int64_t& result) {
        std::int64_t remaining = available;
        if (charge(items.size())!=FlexLengthChargeDecision::Continue) return false;
        for (const auto& item : items) remaining -= item.frozen ? item.target : item.base;
        result = remaining;
        return true;
    };
    std::int64_t initial_free = 0;
    if (!free_space(initial_free)) return false;
    // At least one item is frozen on each nonterminal iteration.
    for (std::size_t iteration = 0; iteration <= items.size(); ++iteration) {
        std::uint64_t factor_sum = 0, weight_sum = 0, weight_high = 0;
        // Two limbs keep extreme weight sums exact, without compiler-specific
        // widened integers or floating subtraction of almost equal weights.
        // A size_t-count of INT_MAX-squared weights fits within these limbs.
        std::size_t unfrozen = 0, weighted_items = 0;
        if (charge(items.size())!=FlexLengthChargeDecision::Continue) return false;
        for (const auto& item : items) if (!item.frozen) {
            ++unfrozen;
            factor_sum += static_cast<std::uint64_t>(std::max(0,
                grow ? item.child->style.flex_grow : item.child->style.flex_shrink));
            const auto weight = flex_length_weight_parts(item, grow);
            if (weight.low != 0 || weight.high != 0) ++weighted_items;
            const std::uint64_t previous_sum = weight_sum;
            weight_sum += weight.low;
            weight_high += weight.high;
            if (weight_sum < previous_sum) ++weight_high;
        }
        if (unfrozen == 0) return true;
        std::int64_t remaining = 0;
        if (!free_space(remaining)) return false;
        if (factor_sum < 1000) {
            const std::uint64_t magnitude = static_cast<std::uint64_t>(initial_free < 0 ? -initial_free : initial_free);
            const auto partial = static_cast<std::int64_t>(flex_mul_div(magnitude, factor_sum, 1000));
            if (partial < (remaining < 0 ? -remaining : remaining)) remaining = initial_free < 0 ? -partial : partial;
        }
        std::uint64_t remaining_amount = static_cast<std::uint64_t>(remaining < 0 ? -remaining : remaining);
        std::uint64_t remaining_weight = weight_sum;
        std::uint64_t remaining_weight_high = weight_high;
        std::int64_t total_violation = 0;
        if (charge(items.size())!=FlexLengthChargeDecision::Continue) return false;
        for (auto& item : items) if (!item.frozen) {
            const auto weight = flex_length_weight_parts(item, grow);
            std::uint64_t share = 0;
            if (weight.low != 0 || weight.high != 0) {
                if (remaining_weight_high != 0) {
                    share = weighted_items == 1 ? remaining_amount : flex_weight_share(remaining_amount,
                        weight,{remaining_weight,remaining_weight_high});
                } else {
                    share = flex_mul_div(remaining_amount, weight.low, remaining_weight);
                }
                if (remaining_weight < weight.low) --remaining_weight_high;
                remaining_weight -= weight.low;
                remaining_weight_high -= weight.high;
                remaining_amount -= share;
                --weighted_items;
            }
            const std::int64_t tentative = static_cast<std::int64_t>(item.base) +
                (remaining < 0 ? -static_cast<std::int64_t>(share) : static_cast<std::int64_t>(share));
            item.target = flex_length_clamp(tentative, item);
            const std::int64_t violation = static_cast<std::int64_t>(item.target) - tentative;
            item.violation = violation > 0 ? 1 : violation < 0 ? -1 : 0;
            total_violation += violation;
        }
        if (charge(items.size())!=FlexLengthChargeDecision::Continue) return false;
        for (auto& item : items) if (!item.frozen && (total_violation == 0 ||
            (total_violation > 0 ? item.violation > 0 : item.violation < 0))) item.frozen = true;
    }
    return true;
}

// One fixed callback type shares the checked solver across caller lambdas.
// Records retain direct array access; no per-item type erasure is needed.
struct FlexLengthPrecisionCharge {
    const void* context = nullptr;
    FlexLengthChargeDecision (*charge)(const void*,std::size_t) = nullptr;
    FlexLengthChargeDecision operator()(std::size_t count) const { return charge(context,count); }
};
template<class Item>
FlexLengthPrecisionStatus resolve_flex_lengths_precise_typed(FlexLengthRange<Item> items,
    std::int64_t main_size,std::int64_t gap,FlexLengthPrecisionCharge charge) {
    if(main_size<0 || gap<0 || !charge.charge) return FlexLengthPrecisionStatus::InvalidInput;
    constexpr auto limit=kDimensionPrecisionMaximum;
    if(main_size>limit || gap>limit || items.size()>
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()/(4*limit)))
        return FlexLengthPrecisionStatus::CoordinateOverflow;
    if (charge(items.size())!=FlexLengthChargeDecision::Continue) return FlexLengthPrecisionStatus::ResourceLimit;
    for(const auto& item:items) {
        if(!item.child || item.base<0 || item.inner_base<0 || item.hypothetical<0 ||
            item.minimum<0 || item.maximum<-1) return FlexLengthPrecisionStatus::InvalidInput;
        if(item.base>limit || item.inner_base>limit || item.hypothetical>limit ||
            item.minimum>limit || item.maximum>limit || item.margins<-limit || item.margins>limit)
            return FlexLengthPrecisionStatus::CoordinateOverflow;
    }
    return resolve_flex_lengths(items,main_size,gap,charge) ?
        FlexLengthPrecisionStatus::Success : FlexLengthPrecisionStatus::ResourceLimit;
}
extern template FlexLengthPrecisionStatus resolve_flex_lengths_precise_typed(
    FlexLengthRange<FlexLengthPrecisionItem>,std::int64_t,std::int64_t,FlexLengthPrecisionCharge);

template<class Range,class Charge>
inline FlexLengthPrecisionStatus resolve_flex_lengths_precise(Range& items,
    std::int64_t main_size,std::int64_t gap,const Charge& charge) {
    using Item=typename std::remove_reference<decltype(*items.begin())>::type;
    FlexLengthRange<Item> range{items.empty()?nullptr:&*items.begin(),items.size()};
    FlexLengthPrecisionCharge callback{&charge,[](const void* context,std::size_t count) -> FlexLengthChargeDecision {
        return (*static_cast<const Charge*>(context))(count);
    }};
    return resolve_flex_lengths_precise_typed(range,main_size,gap,callback);
}

} // namespace jellyframe::detail
