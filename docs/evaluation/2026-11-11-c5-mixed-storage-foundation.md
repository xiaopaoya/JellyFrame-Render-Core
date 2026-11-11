# C5 mixed-dimension storage foundation

This scoped change keeps the existing eleven-slot `ComputedSizeStorage` payload
and adds no persistent fields. `set_length_percentage` now accepts all seven
dimension properties, retaining signed length and percentage Q1000 components
with the existing bounds and copy-on-write publication. Invalid input,
allocation failure, and no-op writes leave the prior snapshot unchanged.

`read_typed(property, kind, units, percentage_units)` is the presence-aware
read for consumers. It reports `SizeKind::LengthPercentage`, including a zero
percentage coefficient. The legacy three-output `get` continues to return
false for mixed values and must not be used as a presence predicate.

`computed_size_units` reads typed storage first and evaluates a mixed value as
`lengthQ + basisQ * percentageQ / 100000`, using signed half-away-from-zero
rounding, checked decomposition, saturation, and final nonnegative used-size
clamping. An indefinite basis returns `-1` for the whole expression. Existing
FlexBasis percentage compatibility remains a fallback only when no typed slot
is present.

Parser admission remains closed. The style publisher must atomically encode a
bounded two-term calc and synchronize scalar/provenance state before consumers
can rely on this API. Layout, intrinsic-query, and temporary-override callers
must migrate presence checks to `read_typed`; those consumer changes are the
next integration step and are intentionally outside this foundation commit.

Validation: `jellyframe_computed_size_storage_tests` passes in the fresh
`E:\\Builds\\c5-mixed-storage` build; it covers all seven dimension slots,
legacy getter hiding, signed bounds, snapshot isolation, and allocation-failure
atomicity.

## Atomic Style publication follow-up

Core commit `4fbbe070` adds the internal
`apply_computed_length_percentage` publisher. It validates the six width,
height, min/max dimension slots, mutates a COW `Style` candidate, retains the
typed signed Q1000 pair (including a zero-percent coefficient), sets legacy
scalar and percent proxies to `-1`, and publishes only after all operations
succeed. Failure leaves the original Style unchanged. Table sizing
qualification provenance is marked with the authored dimension in the same
publication.

Tests cover positive/negative components, zero-percent preservation, invalid
and unsupported slots, and injected allocation failure. The exact candidate
passes a fresh Release build and all 25 Core CTest entries in
`E:\\Builds\\c5-mixed-storage-r3`.

This still does not admit mixed CSS syntax. Consumers have distributed
presence/safety checks in layout, intrinsic width/height queries and Flex
auto-min sizing; temporary table and fallback snapshots require review. Keep
parser admission closed until those consumers, precise/ordinary parity and
intrinsic/table refusal semantics are integrated as a coordinated package.
