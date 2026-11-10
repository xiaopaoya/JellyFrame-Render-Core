#pragma once

#include "render_core/text_system.h"

namespace jellyframe::detail {

struct TextSystemAccess {
    static TextSystemStatus resolve_family_hash(const TextSystem& system,
        std::uint32_t hash, std::uint32_t& family, std::size_t work_limit,
        std::size_t& work_used) noexcept {
        work_used = 0;
        if (!system.ready()) return TextSystemStatus::NotReady;
        if (hash == 0) { family = 0; return TextSystemStatus::Ok; }
        for (std::size_t i = 0; i < system.count_; ++i) {
            if (work_used == work_limit) return TextSystemStatus::ResourceLimit;
            ++work_used;
            if (system.faces_[i].css_family_hash != hash) continue;
            family = system.faces_[i].family_id;
            return TextSystemStatus::Ok;
        }
        return TextSystemStatus::InvalidFontSet;
    }

    static TextSystemStatus measure(const TextSystem& system, std::string_view text,
                                    const TextFontRequest& font, TextRunMetrics& metrics,
                                    std::size_t work_limit, std::size_t& work_used) noexcept {
        return system.process(text, font, nullptr, 0, false, metrics,
                              work_limit, &work_used);
    }
};

} // namespace jellyframe::detail
