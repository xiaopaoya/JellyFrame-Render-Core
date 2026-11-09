#include "flex_auto_min.h"
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
#include "computed_size_geometry.h"
#include <algorithm>
namespace jellyframe::detail {
FlexAutoMinKind flex_auto_min_kind(const Style& s,FlexMainAxis axis) {
    const bool vertical=axis==FlexMainAxis::Vertical;
    const int scalar=vertical?s.min_height:s.min_width;
    const int percent=vertical?s.min_height_percent:s.min_width_percent;
    if(scalar < -1 || percent < -1)return FlexAutoMinKind::Unresolved;
    SizeKind kind;std::int64_t units=0;
    if(scalar!=-1 || percent!=-1 || s.computed_sizes.get(vertical?SizeProperty::MinHeight:SizeProperty::MinWidth,kind,units))
        return FlexAutoMinKind::NotAuto;
    if(s.overflow=="hidden" || s.overflow=="auto" || s.overflow=="scroll")return FlexAutoMinKind::Zero;
    if(s.overflow.empty() || s.overflow=="visible" || s.overflow=="clip")return FlexAutoMinKind::Content;
    return FlexAutoMinKind::Unresolved;
}
FlexAutoMinResult flex_auto_minimum(const FlexAutoMinInput& in) {
    constexpr auto limit=kDimensionPrecisionMaximum;
    for(auto value:{in.content_size_units,in.specified_main_units,in.preferred_cross_units,
        in.min_cross_units,in.max_cross_units,in.max_main_units})
        if(value < -1 || value > limit)return {FlexAutoMinStatus::InvalidInput,-1};
    if(in.main_decoration_units<0 || in.main_decoration_units>limit ||
       in.cross_decoration_units<0 || in.cross_decoration_units>limit || in.ratio_main<0 || in.ratio_cross<0 ||
       ((in.ratio_main==0)!=(in.ratio_cross==0)))return {FlexAutoMinStatus::InvalidInput,-1};
    const auto floor=in.main_decoration_units;
    // A cap at the decoration floor proves the answer without a content walk.
    if((in.specified_main_units>=0 && in.specified_main_units<=floor) ||
       (in.max_main_units>=0 && in.max_main_units<=floor))return {FlexAutoMinStatus::Complete,floor};
    if(!in.content_complete || in.content_size_units<0)return {};
    const auto cross_to_main=[&](std::int64_t cross) {
        const auto size=in.ratio_uses_border_box?std::max(cross,in.cross_decoration_units):
            std::max<std::int64_t>(0,cross-in.cross_decoration_units);
        const auto converted=scale_size_units(size,in.ratio_main,in.ratio_cross);
        return std::max(floor,std::min(limit,converted+(in.ratio_uses_border_box?0:floor)));
    };
    auto content=std::max(floor,in.content_size_units);
    if(in.ratio_main>0) {
        if(in.max_cross_units>=0)content=std::min(content,cross_to_main(in.max_cross_units));
        if(in.min_cross_units>=0)content=std::max(content,cross_to_main(in.min_cross_units));
        if(in.preferred_cross_units>=0) {
            auto cross=in.preferred_cross_units;
            if(in.max_cross_units>=0)cross=std::min(cross,in.max_cross_units);
            if(in.min_cross_units>=0)cross=std::max(cross,in.min_cross_units);
            const auto transferred=cross_to_main(std::max(cross,in.cross_decoration_units));
            content=in.replaced?std::min(content,transferred):std::max(content,transferred);
        }
    }
    if(in.specified_main_units>=0)content=std::min(content,in.specified_main_units);
    if(in.max_main_units>=0)content=std::min(content,in.max_main_units);
    return {FlexAutoMinStatus::Complete,std::max(floor,content)};
}
}
#endif
