#pragma once
#include "render_core/layout.h"
#include "css_dimension_precision.h"
#include <cstdint>

namespace jellyframe::detail {
// Optional, pass-owned Q1000 input/output for the shared Flex length solver.
// Border-box lengths and content-only shrink bases retain their separate roles.
// Existing integer items and public layout holders do not acquire these fields.
struct FlexLengthPrecisionItem {
    LayoutBox* child = nullptr;
    std::int64_t base = 0, inner_base = 0, hypothetical = 0, target = 0;
    std::int64_t minimum = 0, maximum = -1, margins = 0, probe = 0;
    std::int8_t violation = 0;
    bool frozen = false;
};
enum class FlexLengthPrecisionStatus { Success, InvalidInput, CoordinateOverflow };
} // namespace jellyframe::detail
