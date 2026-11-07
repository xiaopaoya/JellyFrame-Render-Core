# Bounded computed-dimension Flex layout

Local Core 0.9.0-dev / ABI4 stage after 58146fdd; unpublished and not device-qualified.

Single-line Flex rows and definite-height columns can consume the common Q1000
width/height/min/max values and retained pixel flex-basis. The checked length
solver is ported from the experiment: it distributes free space, repeatedly
freezes min/max violations, uses content-only bases for shrink weights and
preserves unused space when factors sum to less than one. Checked two-limb
integer weights handle extreme shrink factors without compiler-specific int128.
Only the record type used by this port is instantiated.

Allocation travels in stack-local context, rather than temporarily changing
Style width/height or erasing shared authored dimensions. Ordinary Block
 descendants consume the allocated content box as their percentage basis. Final
rectangles project edges from retained units. Order, justification, row main-axis
auto margins, cross alignment/stretch and min/max constraints are included.
An unresolved percentage cross height behaves as auto for stretch. A definite
cross size skips a redundant subtree probe; natural cross sizes still require
probing and final layout. This is a structural saving, not a measured speedup.

Admission is deliberately bounded: no wrapping, explicit/resolvable main size or
scalar/pixel basis, and ordinary Block items. Descendant-bearing or ratio-bearing
items need an explicit main-axis minimum so this stage does not pretend to solve
content-based automatic minima. Columns need definite container main size, and
an auto column cross width requires stretch. Ineligible containers use the legacy
solver, without precision qualification beneath it. Nested ordinary descendants
can carry precision; direct Flex/Grid items are not admitted by this helper.
Inline measurement inside admitted items remains the existing integer solver.

Percentage flex-basis is NOT migrated: the pre-existing parser resolves it through
the legacy length path. Relative-unit basis precision, intrinsic/automatic minima,
wrapping, Grid, positioned descendants and natural replaced-element metadata need
separate work. Existing integer margin/padding/border/gap storage is unchanged.
This stage is not general Flex conformance or browser raster parity.

Validation on Windows/Clang 22 Release:

- Full Core: 8/8 CTest targets. Minimal feature families disabled: 7/7; the Flex
  test and implementation translation unit are excluded when FLEX_GRID is off.
- 36 closed-form rational scenes cover both directions, fractional bases,
  grow/shrink, partial growth, min/max freezing, gaps, decoration and ordinary
  percentage grandchildren. The generator reads no Core/browser outputs; it was
  authored during this implementation, so is not blind historical evidence.
  Regenerated fixture SHA256 matches the checked fixture exactly. Scenarios use
  edge expectations whose pixel results are stable under the Q1000 allocation.
- Additional hand-authored checks cover shorthand/longhand basis publication and
  resets, invalid declarations, preferred percentage main dimensions, natural
  ratio cross sizing, order/auto margins, unchanged authored Style, paint/hit
  geometry, overflow clipping, unresolved cross percentages, invalid/oversized
  solver inputs and weights exceeding 64 bits.
- Full-library `-fsanitize=undefined -fsanitize-trap=all` build passes dedicated
  Flex and ordinary Block geometry targets (2/2), including extreme weights.
  This does not claim whole-suite sanitizer acceptance.
- A separate installed-package-only consumer verifies capped widths 25/75px,
  38px percentage descendant and unchanged retained authored width 999.25px.
  Its complete source is included in `consumer/`.

There are no new persistent Style or LayoutBox fields. Style remains 784 bytes
on this host. A solver item is 80 bytes here; the pass also has cross-size and
ordered-child vectors. Transient allocations, recursive probes and freezing scans
are real costs; MCU heap/latency and wider interaction workloads remain unmeasured.
No hardware flash, new live browser collection or Runtime compatibility approval
was performed. Runtime 0.9 manifests/lock remain pending; older ABI3 results do not
qualify this ABI4 stage.

## Reproduce

Use a fresh Ninja/Clang Release build with default feature families; run CTest.
For minimal, set JELLYFRAME_ENABLE_FLEX_GRID, ADVANCED_FORMS, CANVAS2D,
MODERN_PAINT and IMAGE_FILE_IO to OFF. For the sanitizer build, set
CMAKE_CXX_FLAGS to `-fsanitize=undefined -fsanitize-trap=all` and run tests
matching `computed_(flex|block)_layout`. Actual build roots are recorded in
validation.json. Regenerate cases with:

```
python tests/tools/generate_flex_precision_cases.py E:/Temp/flex-cases.h
```

Install the full build with `cmake --install <build> --prefix <prefix>`, then
configure `consumer/` in a fresh build using CMAKE_PREFIX_PATH=<prefix> and run
flex_precision_consumer. It must find ABI4 and only uses installed public headers.

Next: migrate the remaining Flex basis/intrinsic and Grid allocation consumers
with bounded tests, then the inline/positioned boundaries and matched Runtime
manifests/package. Do not collapse these exclusions into full precision acceptance.
