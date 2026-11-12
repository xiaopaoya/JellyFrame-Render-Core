#include "style_resolver_test_hooks.h"

namespace jellyframe::detail::test {
namespace {
TextStyleFastPathCounters counters;
}

void reset_text_style_fast_path_calls() {
    counters = {};
}

TextStyleFastPathCounters text_style_fast_path_counters() {
    return counters;
}

void record_selector_lookup_attempt() { ++counters.selector_lookup_attempts; }

void record_text_style_fast_path() {
    ++counters.calls;
    ++counters.custom_property_walks_skipped;
    ++counters.selector_lookups_skipped;
    ++counters.cascade_slot_sets_skipped;
}

} // namespace jellyframe::detail::test
