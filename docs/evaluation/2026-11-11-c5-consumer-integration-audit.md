# C5 consumer integration audit (design-only)

Audit baseline: Core candidate `63b9ccf1` (`Add mixed dimension storage and
signed evaluator foundation`). No parser admission or consumer behavior is
enabled by this audit.

## Why a consumer-only slice is unsafe

The typed storage/evaluator is present, but the publisher still accepts only
`Pixels` and `Percent` in `apply_computed_size` (`src/computed_size_values.h`).
There is no atomic mixed publisher that updates retained components, scalar
proxies, and provenance together. Enabling a parser path now would either
leave stale scalar/kind state or expose a mixed slot to consumers that still
use the hiding three-output getter.

Presence guards remain distributed across `layout.cpp` and
`intrinsic_source_query.cpp`; several calls to `computed_sizes.get` are
admission/safety checks rather than numeric reads. They would classify a
LengthPercentage slot as absent. The precise and ordinary paths also have
temporary override and fallback transactions whose restoration must include
the typed slot and compatibility state. Updating only Block sizing would make
Flex, Grid, table, and intrinsic fallback behavior inconsistent.

The current parser has multiple `parse_length_px` routes and shorthand token
loops. A bounded two-term calc parser must be shared before any declaration
can publish mixed state; otherwise percentages can reach viewport
approximation or parenthesized flex-basis tokens can split incorrectly.

## Required integration order

1. Add a bounded parser returning signed length/percentage components and
   source provenance; reject unsupported grammar atomically.
2. Add `apply_computed_length_percentage` to publish the typed slot before
   proxies, setting dimension proxies to `-1` and preserving zero
   percentage provenance.
3. Replace every presence/safety `get(property, kind, units)` call with
   `read_typed`; leave numeric legacy reads only where mixed is explicitly
   refused.
4. Route ordinary and precise Block/Flex/Grid decisions through the signed
   evaluator, with explicit indefinite-basis behavior and complete temporary
   state restoration.
5. Add intrinsic and table/context refusal guards, then enable parser
   admission only after cross-family fixtures pass.

## Focused migration checks

The first consumer PR should assert `calc(20px + 0%)` remains typed and
percentage-bearing, that `calc(10px - 50%)` clamps only after combination,
and that forcing precise fallback reproduces ordinary mixed results while
restoring the full authored snapshot. Mixed min/max and unsupported intrinsic
contexts must return explicit Unsupported rather than fall through scalar
proxies.

This audit intentionally contains no implementation or parser changes; it is
the concrete handoff contract for the next integrated package.
