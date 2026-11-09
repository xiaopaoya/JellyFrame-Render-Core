#include "flex_length_resolution.h"
namespace jellyframe::detail {
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
// This port uses one pass-owned record layout. Do not instantiate unused
// experimental planner variants into the embedded library.
template FlexLengthPrecisionStatus resolve_flex_lengths_precise_typed(
    FlexLengthRange<FlexLengthPrecisionItem>,std::int64_t,std::int64_t,FlexLengthPrecisionCharge);
#endif
} // namespace jellyframe::detail
