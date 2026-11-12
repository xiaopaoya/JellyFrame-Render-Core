#include "../../src/inline_fragment_lines.h"

#include <iostream>
#include <stdexcept>
#include <vector>

using namespace jellyframe::detail;

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<InlineMeasuredPiece> forced_lines(std::size_t count) {
    std::vector<InlineMeasuredPiece> input;
    for (std::size_t line = 0; line < count; ++line) {
        InlineMeasuredPiece text;
        text.kind = InlinePieceKind::Text;
        text.advance = 1;
        text.before = 1;
        text.own_before = 1;
        input.push_back(text);
        InlineMeasuredPiece marker;
        marker.kind = InlinePieceKind::ForcedBreak;
        input.push_back(marker);
    }
    return input;
}

InlineLinePlanOptions compact_options() {
    InlineLinePlanOptions options;
    options.width = 100;
    options.strut_before = 1;
    options.height_only = true;
    options.compact_line_storage = true;
    return options;
}

void cumulative_line_growth() {
    const auto input = forced_lines(9);
    InlineLinePlan output;
    std::size_t acquired = 0;
    const auto status = plan_inline_fragment_lines(input.data(), input.size(),
        compact_options(), output, {}, nullptr, &acquired);
    check(status == InlineLinePlanStatus::Complete && output.lines.size() == 9,
          "nine forced lines must complete");
    check(acquired > output.lines.capacity() * sizeof(InlineFragmentLine),
          "planner charge must include superseded line allocations");
    check(acquired >= (4 + 8 + 16) * sizeof(InlineFragmentLine),
          "compact reserve growth must retain every successful acquisition");

    InlineLinePlan preserved;
    preserved.height = 91;
    InlineLinePlanLimits limits;
    limits.max_capacity_bytes = output.lines.capacity() * sizeof(InlineFragmentLine);
    acquired = 0;
    const auto refused = plan_inline_fragment_lines(input.data(), input.size(),
        compact_options(), preserved, limits, nullptr, &acquired);
    check(refused == InlineLinePlanStatus::CapacityLimit && preserved.height == 91 &&
          preserved.lines.empty(), "cumulative limit must refuse without publishing partial lines");
    check(acquired != 0 && acquired <= limits.max_capacity_bytes,
          "failed compact planning must retain admitted acquisition charges");
}

struct RangeContext {
    std::size_t capacity = 0;
    std::size_t calls = 0;
    std::size_t offered_capacity = 0;
};

void failed_measurement_keeps_acquisitions() {
    const auto input = forced_lines(9);
    RangeContext context;
    context.capacity = 37;
    auto options = compact_options();
    options.range.context = &context;
    options.range.usage = [](void* raw) {
        return InlineLineRangeUsage{0, static_cast<RangeContext*>(raw)->capacity};
    };
    options.range.advance = [](std::size_t, std::size_t, int,
                               const InlineLineRangeBudget& budget, int*, void* raw) {
        auto& context = *static_cast<RangeContext*>(raw);
        ++context.calls;
        context.offered_capacity = budget.capacity;
        return false;
    };
    InlineLinePlanLimits limits;
    limits.max_capacity_bytes = context.capacity + 4 * sizeof(InlineFragmentLine);
    InlineLinePlan output;
    output.height = 73;
    std::size_t acquired = 0;
    const auto status = plan_inline_fragment_lines(input.data(), input.size(), options,
        output, limits, nullptr, &acquired);
    check(status == InlineLinePlanStatus::MeasurementDeclined && context.calls == 1,
          "measurement refusal must occur after the initial reservation");
    check(context.offered_capacity == 0,
          "callback budget must include its earlier acquisitions and planner storage");
    check(acquired == 4 * sizeof(InlineFragmentLine) && output.height == 73 && output.lines.empty(),
          "failed measurement must preserve output and initial planner charge");
}
}

int main() {
    try {
        cumulative_line_growth();
        failed_measurement_keeps_acquisitions();
        std::cout << "Shared paragraph planner cumulative capacity checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
