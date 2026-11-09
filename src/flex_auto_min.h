#pragma once
#include "render_core/style.h"
#include <cstdint>
namespace jellyframe::detail {
enum class FlexMainAxis : std::uint8_t { Horizontal, Vertical };
enum class FlexAutoMinKind : std::uint8_t { NotAuto, Zero, Content, Unresolved };
enum class FlexAutoMinStatus : std::uint8_t { Complete, Incomplete, InvalidInput, Unsupported };
// Pass-local border-box Q1000 suggestions, excluding margins. -1 means absent.
// A measured content size must be complete; an estimate is never a measured zero.
struct FlexAutoMinInput {
    std::int64_t content_size_units=-1;
    bool content_complete=false;
    std::int64_t specified_main_units=-1, preferred_cross_units=-1;
    std::int64_t min_cross_units=-1, max_cross_units=-1, max_main_units=-1;
    std::int64_t main_decoration_units=0, cross_decoration_units=0;
    int ratio_main=0, ratio_cross=0;
    bool ratio_uses_border_box=false, replaced=false;
};
struct FlexAutoMinResult {
    FlexAutoMinStatus status=FlexAutoMinStatus::Incomplete;
    std::int64_t minimum_border_box_units=-1;
};
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
// This mainline Style has one coupled overflow value, not per-axis metadata.
FlexAutoMinKind flex_auto_min_kind(const Style&,FlexMainAxis);
FlexAutoMinResult flex_auto_minimum(const FlexAutoMinInput&);
#else
inline FlexAutoMinKind flex_auto_min_kind(const Style&,FlexMainAxis){return FlexAutoMinKind::NotAuto;}
inline FlexAutoMinResult flex_auto_minimum(const FlexAutoMinInput&){return {FlexAutoMinStatus::Unsupported,-1};}
#endif
}
