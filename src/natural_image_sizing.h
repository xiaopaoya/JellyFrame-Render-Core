#pragma once

#include "render_core/layout.h"
#include "computed_size_geometry.h"
#include <new>
#include <stdexcept>

namespace jellyframe::detail {

// Sparse per-call memo: no per-LayoutBox storage, pointers or callback ownership
// survive the call. Reserve before the first host query; all refusals are cached.
struct NaturalImageSizingFrame {
    struct Entry {
        const Node* source;
        ImageIntrinsicSize size;
    };
    const NaturalImageSizingOptions& options;
    NaturalImageSizingStatistics statistics;
    std::vector<Entry> memo;
    bool initialized = false;
    bool closed = false;

    explicit NaturalImageSizingFrame(const NaturalImageSizingOptions& value) : options(value) {}
    ~NaturalImageSizingFrame() {
        if (options.statistics) {
            *options.statistics = statistics;
        }
    }
    ImageIntrinsicSize retained(const Node& source) {
        for (const auto& entry : memo) {
            if (entry.source == &source) {
                ++statistics.cache_hits;
                return entry.size;
            }
        }
        return {};
    }
    ImageIntrinsicSize metadata(const Node& source) {
        for (const auto& entry : memo) {
            if (entry.source == &source) {
                ++statistics.cache_hits;
                return entry.size;
            }
        }
        const auto limit = std::min({options.max_queries, options.max_memo_entries,
            std::size_t{4096}, options.max_scratch_bytes / sizeof(Entry)});
        if (closed || memo.size() >= limit || statistics.queries >= options.max_queries) {
            ++statistics.limit_refusals;
            return {};
        }
        if (!initialized) {
            initialized = true;
            try {
                memo.reserve(limit);
            } catch (const std::bad_alloc&) {
                closed = true;
                ++statistics.allocation_failures;
                return {};
            } catch (const std::length_error&) {
                closed = true;
                ++statistics.allocation_failures;
                return {};
            }
            statistics.scratch_bytes = memo.capacity() * sizeof(Entry);
            if (statistics.scratch_bytes > options.max_scratch_bytes) {
                closed = true;
                ++statistics.limit_refusals;
                return {};
            }
        }
        memo.push_back({&source, {}});
        ++statistics.queries;
        ImageIntrinsicSize size;
        try {
            size = options.provider.intrinsic_size(source, options.provider.context);
        } catch (const std::bad_alloc&) {
            ++statistics.allocation_failures;
        }
        if (size.state == ImageIntrinsicSizeState::Known &&
            size.width > 0 && size.width <= 8191 && size.height > 0 && size.height <= 8191) {
            ++statistics.accepted;
        } else if (size.state == ImageIntrinsicSizeState::Unknown) {
            size = {};
            ++statistics.unknown;
        } else {
            size = {ImageIntrinsicSizeState::Invalid, 0, 0};
            ++statistics.invalid;
        }
        memo.back().size = size;
        return size;
    }
};

struct NaturalImageContentSize {
    std::int64_t width = -1;
    std::int64_t height = -1;
    bool valid = false;
};

// Exact rounded mul/div for the both-auto coordinate-box ratio. Decorations can
// make its denominator larger than the int32 bound of scale_size_units. Reuse
// that helper for representable components; otherwise binary accumulation
// avoids an overflowing product in at most 42 iterations on the Q1000 lattice.
inline std::int64_t scale_natural_ratio(std::int64_t value, std::int64_t numerator,
    std::int64_t denominator) {
    if (value < 0 || value > 2 * kDimensionPrecisionMaximum ||
        numerator < 0 || numerator > 2 * kDimensionPrecisionMaximum ||
        denominator <= 0 || denominator > 2 * kDimensionPrecisionMaximum) {
        return -2;
    }
    if (value <= kDimensionPrecisionMaximum && numerator <= kDimensionPrecisionMaximum &&
        denominator <= std::numeric_limits<int>::max() &&
        (numerator == 0 || value % denominator <=
            (std::numeric_limits<std::int64_t>::max() - denominator / 2) / numerator)) {
        return scale_size_units(value, numerator, denominator);
    }
    std::int64_t whole = 0;
    std::int64_t remainder = 0;
    auto term_whole = value / denominator;
    auto term_remainder = value % denominator;
    while (numerator) {
        if (numerator & 1) {
            if (term_whole > kDimensionPrecisionMaximum - whole) {
                return kDimensionPrecisionMaximum;
            }
            whole += term_whole;
            remainder += term_remainder;
            if (remainder >= denominator) {
                remainder -= denominator;
                ++whole;
            }
            if (whole >= kDimensionPrecisionMaximum) {
                return kDimensionPrecisionMaximum;
            }
        }
        numerator >>= 1;
        if (!numerator) {
            break;
        }
        term_remainder *= 2;
        const auto carry = term_remainder >= denominator ? 1 : 0;
        if (carry) {
            term_remainder -= denominator;
        }
        term_whole = term_whole > (kDimensionPrecisionMaximum - carry) / 2
            ? kDimensionPrecisionMaximum : term_whole * 2 + carry;
    }
    return std::min(kDimensionPrecisionMaximum, whole + (remainder >= (denominator + 1) / 2 ? 1 : 0));
}

// Existing CSS2 replaced-image semantics adapted from 88eb0539 to the current
// typed Q1000 geometry. Both auto axes scale together unless opposing bounds
// require independent clamps. A natural ratio describes content; an explicit
// authored ratio describes the box selected by box-sizing.
inline NaturalImageContentSize natural_image_content_size(const Style& style,
    ImageIntrinsicSize natural, std::int64_t width_basis, std::int64_t height_basis,
    std::int64_t horizontal, std::int64_t vertical) {
    if (natural.state != ImageIntrinsicSizeState::Known || natural.width == 0 || natural.width > 8191 ||
        natural.height == 0 || natural.height > 8191 || horizontal < 0 || vertical < 0 ||
        horizontal > kDimensionPrecisionMaximum || vertical > kDimensionPrecisionMaximum) {
        return {};
    }
    auto width = computed_size_units(style, SizeProperty::Width, width_basis);
    auto height = computed_size_units(style, SizeProperty::Height, height_basis);
    const bool auto_width = width == -1;
    const bool auto_height = height == -1;
    if (width < -1 || height < -1 || (!auto_width && !auto_height)) {
        return {};
    }
    const auto content = [&](std::int64_t value, std::int64_t decoration) {
        return value < 0 || !style.box_sizing_border_box ? value : std::max<std::int64_t>(0, value - decoration);
    };
    const auto bound = [&](SizeProperty property, std::int64_t basis, std::int64_t decoration) {
        return content(computed_size_units(style, property, basis), decoration);
    };
    auto min_width = bound(SizeProperty::MinWidth, width_basis, horizontal);
    auto max_width = bound(SizeProperty::MaxWidth, width_basis, horizontal);
    auto min_height = bound(SizeProperty::MinHeight, height_basis, vertical);
    auto max_height = bound(SizeProperty::MaxHeight, height_basis, vertical);
    if (min_width < -1 || max_width < -1 || min_height < -1 || max_height < -1) {
        return {};
    }
    min_width = std::max<std::int64_t>(0, min_width);
    min_height = std::max<std::int64_t>(0, min_height);
    // A saturated authored minimum plus decoration cannot be represented as a
    // border box. Refuse before constructing any clamp interval.
    if (min_width > kDimensionPrecisionMaximum - horizontal ||
        min_height > kDimensionPrecisionMaximum - vertical) {
        return {};
    }
    max_width = max_width < 0 ? kDimensionPrecisionMaximum - horizontal : std::max(min_width, max_width);
    max_height = max_height < 0 ? kDimensionPrecisionMaximum - vertical : std::max(min_height, max_height);
    const bool authored = !style.aspect_ratio_auto && style.aspect_ratio_width > 0 && style.aspect_ratio_height > 0;
    const auto ratio_width = authored ? style.aspect_ratio_width : natural.width;
    const auto ratio_height = authored ? style.aspect_ratio_height : natural.height;
    const auto dx = authored && style.box_sizing_border_box ? horizontal : 0;
    const auto dy = authored && style.box_sizing_border_box ? vertical : 0;
    const auto from_width = [&](std::int64_t value) {
        const auto scaled = scale_size_units(value + dx, ratio_height, ratio_width);
        return scaled < 0 ? scaled : std::max<std::int64_t>(0, scaled - dy);
    };
    const auto from_height = [&](std::int64_t value) {
        const auto scaled = scale_size_units(value + dy, ratio_width, ratio_height);
        return scaled < 0 ? scaled : std::max<std::int64_t>(0, scaled - dx);
    };
    if (auto_width && auto_height) {
        width = static_cast<std::int64_t>(natural.width) * 1000;
        height = authored ? from_width(width) : static_cast<std::int64_t>(natural.height) * 1000;
        if (height < 0) {
            return {};
        }
        // Preserve the initial coordinate-box ratio even when a very small
        // authored ratio has clamped one content extent to zero.
        const auto initial_width = width + dx;
        const auto initial_height = height + dy;
        const auto coupled_height = [&](std::int64_t value) {
            const auto scaled = scale_natural_ratio(value + dx, initial_height, initial_width);
            return scaled < 0 ? scaled : std::max<std::int64_t>(0, scaled - dy);
        };
        const auto coupled_width = [&](std::int64_t value) {
            if (initial_height == 0) {
                return width;
            }
            const auto scaled = scale_natural_ratio(value + dy, initial_width, initial_height);
            return scaled < 0 ? scaled : std::max<std::int64_t>(0, scaled - dx);
        };
        const bool opposing = (width < min_width && height > max_height) ||
            (width > max_width && height < min_height);
        if (!opposing && (width > max_width || height > max_height)) {
            // Choose the tighter upper constraint in the ratio's coordinate box.
            const auto height_at_max_width = coupled_height(max_width);
            if (height_at_max_width >= 0 && height_at_max_width <= max_height) {
                width = max_width;
                height = height_at_max_width;
            } else {
                height = max_height;
                width = coupled_width(height);
            }
        } else if (!opposing && (width < min_width || height < min_height)) {
            const auto height_at_min_width = coupled_height(min_width);
            if (height_at_min_width >= min_height) {
                width = min_width;
                height = height_at_min_width;
            } else {
                height = min_height;
                width = coupled_width(height);
            }
        }
        if (width < 0 || height < 0) {
            return {};
        }
        width = std::clamp(width, min_width, max_width);
        height = std::clamp(height, min_height, max_height);
    } else if (auto_height) {
        width = std::clamp(content(width, horizontal), min_width, max_width);
        height = from_width(width);
        if (height < 0) {
            return {};
        }
        height = std::clamp(height, min_height, max_height);
    } else {
        height = std::clamp(content(height, vertical), min_height, max_height);
        width = from_height(height);
        if (width < 0) {
            return {};
        }
        width = std::clamp(width, min_width, max_width);
    }
    if (width > kDimensionPrecisionMaximum - horizontal || height > kDimensionPrecisionMaximum - vertical) {
        return {};
    }
    return {width, height, true};
}

} // namespace jellyframe::detail
