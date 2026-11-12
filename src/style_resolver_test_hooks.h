#pragma once

#include <cstddef>

namespace jellyframe::detail::test {

struct TextStyleFastPathCounters {
    std::size_t calls = 0;
    std::size_t selector_lookup_attempts = 0;
    std::size_t custom_property_walks_skipped = 0;
    std::size_t selector_lookups_skipped = 0;
    std::size_t cascade_slot_sets_skipped = 0;
};

void reset_text_style_fast_path_calls();
TextStyleFastPathCounters text_style_fast_path_counters();
void record_text_style_fast_path();
void record_selector_lookup_attempt();

} // namespace jellyframe::detail::test
