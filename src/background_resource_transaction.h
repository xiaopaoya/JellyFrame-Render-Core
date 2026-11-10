#pragma once

#include "render_core/style.h"

namespace jellyframe::detail {
struct BackgroundResourceTransaction {
    static bool register_urls(const StyleResolver& resolver,
                              const std::array<std::string_view, 4>& urls,
                              std::size_t count,
                              std::array<std::uint16_t, 4>& ids) {
        return resolver.background_image_resource_ids_for(urls, count, ids);
    }
};
} // namespace jellyframe::detail
