#include "render_core/text_backend.h"

#include <iostream>
#include <limits>
#include <stdexcept>

using namespace jellyframe;

namespace {

struct ProbeTextBackend {
    int measure_calls = 0;
    int last_font_weight = 0;
};

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool probe_measure(const std::string& text,
                   int font_size,
                   int font_weight,
                   TextMetrics* metrics,
                   void* context) {
    auto* probe = static_cast<ProbeTextBackend*>(context);
    if (probe == nullptr || metrics == nullptr) {
        return false;
    }
    ++probe->measure_calls;
    probe->last_font_weight = font_weight;
    metrics->width = static_cast<int>(text.size()) * font_size;
    metrics->line_height = font_size + 2;
    return true;
}

bool probe_additive_measurement(int, int, std::uint32_t, void*) {
    return true;
}

bool probe_measure_range(const char* data,
                         std::size_t length,
                         int font_size,
                         int,
                         TextMetrics* metrics,
                         void* context) {
    auto* probe = static_cast<ProbeTextBackend*>(context);
    if (probe == nullptr || metrics == nullptr || (data == nullptr && length != 0)) {
        return false;
    }
    ++probe->measure_calls;
    metrics->width = static_cast<int>(length) * font_size;
    metrics->line_height = font_size + 2;
    return true;
}

void letter_spacing_and_utf8_anywhere_wrap_share_scalar_boundaries() {
    ProbeTextBackend probe;
    const TextMeasureProvider measure{probe_measure, &probe};
    const TextMetrics spaced = measure_text_with_letter_spacing(measure, "ABC", 10, 400, 0, 2);
    check(spaced.width == 34, "letter spacing measures scalar advances plus two gaps");

    const std::string cjk = "\xe4\xb8\xad\xe6\x96\x87";
    const std::vector<std::string> lines = wrap_text_anywhere(measure, cjk, 10, 400, 0, 0, 30);
    check(lines.size() == 2, "anywhere wrap breaks two utf-8 scalars into separate lines");
    check(lines[0].size() == 3 && lines[1].size() == 3,
          "anywhere wrap never splits the bytes of a utf-8 scalar");
}

void additive_provider_wraps_without_remeasuring_the_current_line() {
    ProbeTextBackend probe;
    const TextMeasureProvider measure{probe_measure, &probe, nullptr, probe_additive_measurement};
    const std::vector<std::string> lines = wrap_text_at_opportunities(
        measure, "A-A-A-A-A-A", 10, 400, 0, 0, 20);
    check(lines.size() == 6, "additive provider wraps each fixed-width token");
    check(probe.measure_calls == 6,
          "additive provider measures each token once instead of each growing candidate");
}

void range_provider_avoids_scalar_string_allocations() {
    ProbeTextBackend probe;
    const TextMeasureProvider measure{
        probe_measure,
        &probe,
        nullptr,
        nullptr,
        probe_measure_range,
        nullptr,
    };
    const std::vector<std::string> lines = wrap_text_anywhere(
        measure, "AAAA", 10, 400, 0, 0, 20);
    check(lines.size() == 2, "range provider preserves anywhere wrapping");
    check(probe.measure_calls == 1, "range provider caches repeated scalar widths");
}

void counting_wrap_matches_materialized_wrap() {
    ProbeTextBackend probe;
    const TextMeasureProvider measure{probe_measure, &probe, nullptr, probe_additive_measurement};
    const std::string text = "A-A A-A\nBBBB";
    check(count_wrapped_lines_anywhere(measure, text, 10, 400, 0, 0, 20) ==
              wrap_text_anywhere(measure, text, 10, 400, 0, 0, 20).size(),
          "anywhere counting matches materialized wrapping");
    check(count_wrapped_lines_at_opportunities(measure, text, 10, 400, 0, 0, 30) ==
              wrap_text_at_opportunities(measure, text, 10, 400, 0, 0, 30).size(),
          "opportunity counting matches materialized wrapping");
}

void extreme_letter_spacing_uses_one_bounded_value() {
    ProbeTextBackend probe;
    const TextMeasureProvider measure{probe_measure, &probe};
    check(bounded_letter_spacing(10, 1000) == 20, "positive letter spacing is bounded");
    check(bounded_letter_spacing(10, -1000) == -5, "negative letter spacing is bounded");
    check(measure_text_with_letter_spacing(measure, "AB", 10, 400, 0, 1000).width == 40,
          "measurement uses the bounded positive letter spacing");
}

void extreme_fallback_font_sizes_remain_defined() {
    const int maximum = std::numeric_limits<int>::max();
    const TextMetrics metrics = fallback_text_metrics("A", maximum, 700);
    check(metrics.width == maximum && metrics.line_height == maximum,
          "fallback metrics saturate extreme font sizes");
    check(bounded_letter_spacing(maximum, maximum) == maximum,
          "extreme font size keeps spacing bound representable");
    const std::vector<std::string> lines = wrap_text_anywhere(
        TextMeasureProvider{}, "AB", maximum, 400, 0, maximum, 10);
    check(lines.size() == 2 && lines[0] == "A" && lines[1] == "B",
          "extreme fallback widths wrap without arithmetic overflow");
}

void fallback_normal_line_height_tracks_font_size() {
    check(fallback_text_metrics("A", 9, 400).line_height == 11,
          "small fallback text uses a proportional normal line height");
    check(fallback_text_metrics("A", 14, 400).line_height == 17,
          "fallback normal line height does not add a fixed six-pixel floor");
}

} // namespace

int main() {
    try {
        letter_spacing_and_utf8_anywhere_wrap_share_scalar_boundaries();
        additive_provider_wraps_without_remeasuring_the_current_line();
        range_provider_avoids_scalar_string_allocations();
        counting_wrap_matches_materialized_wrap();
        extreme_letter_spacing_uses_one_bounded_value();
        extreme_fallback_font_sizes_remain_defined();
        fallback_normal_line_height_tracks_font_size();
    } catch (const std::exception& error) {
        std::cerr << "text provider test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "text provider tests passed\n";
    return 0;
}
