#pragma once

namespace jellyframe::detail {

// Stored in Style::grid_min_track_width for an explicit flexible minmax track.
// Explicit track sizing ignores this field; mixed-dimension admission uses it
// to avoid treating the min() component as a fixed track width.
inline constexpr int kFlexibleMinmaxTrackProvenance = -2;

} // namespace jellyframe::detail
