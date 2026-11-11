#include "render_core/computed_size_storage.h"
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace jellyframe::detail;

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        for (unsigned index = 0; index < 7; ++index) {
            const auto property = static_cast<SizeProperty>(index);
            ComputedSizeStorage storage;
            require(storage.set_length_percentage(property, -20.125, 50.25),
                    "signed mixed dimension stores");
            SizeKind kind = SizeKind::Pixels;
            std::int64_t length = 0, percentage = 0;
            require(storage.read_typed(property, kind, length, percentage),
                    "typed mixed dimension is present");
            require(kind == SizeKind::LengthPercentage && length == -20125 && percentage == 50250,
                    "signed terms retain Q1000 precision");
            kind = SizeKind::Percent;
            length = 17;
            require(!storage.get(property, kind, length) && kind == SizeKind::Percent && length == 17,
                    "legacy scalar getter hides mixed state without mutating outputs");
        }

        ComputedSizeStorage provenance;
        require(provenance.set_length_percentage(SizeProperty::Width, 20, 0),
                "zero coefficient expression stores");
        SizeKind kind = SizeKind::Pixels;
        std::int64_t length = 0, percentage = -1;
        require(provenance.read_typed(SizeProperty::Width, kind, length, percentage) &&
                    kind == SizeKind::LengthPercentage && length == 20000 && percentage == 0,
                "zero percent remains percentage-bearing mixed provenance");

        auto snapshot = provenance;
        require(provenance.set_length_percentage(SizeProperty::Width, 10, -25),
                "shared snapshot mutation succeeds");
        require(snapshot.read_typed(SizeProperty::Width, kind, length, percentage) &&
                    length == 20000 && percentage == 0,
                "shared snapshot remains isolated");

        const auto before = provenance;
        for (double invalid : {std::numeric_limits<double>::quiet_NaN(),
                               std::numeric_limits<double>::infinity(),
                               1000.001}) {
            require(!provenance.set_length_percentage(SizeProperty::Width, 1, invalid),
                    "invalid coefficient refuses");
            require(provenance.equals(before), "invalid input is atomic");
        }
        require(!provenance.set_length_percentage(SizeProperty::Height, 2147483648.0, 0),
                "length outside signed pixel bound refuses");
        require(provenance.equals(before), "length refusal preserves snapshot");
        std::cout << "C5 mixed storage oracle passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
