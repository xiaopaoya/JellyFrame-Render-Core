# Flex basis percentage migration

Local Core 0.9.0-dev / ABI4 stage after 30b53cb2; not merged, published or device-qualified.

The previous parser passed percentage bases to parse_length_px, resolving them
against the viewport width during style resolution. The candidate retains a
Q1000 percentage coefficient with an explicit `FlexBasisKind` instead. It uses
the existing scalar and a byte in Style padding: Windows host Style is still
784 bytes. Coefficients need no optional precision owner, including fractional
percentages. Pixel residues continue using the existing shared immutable owner.
The coefficient cap is 2,147,483.647%, matching the experiment's bounded design;
rounding a tiny coefficient to zero still preserves its percentage kind.

[CSS Flexbox 7.2.3 and 9.2](https://www.w3.org/TR/css-flexbox-1/#flex-basis-property)
resolve percentages using the container's inner main size. If that size is
indefinite, the basis is content rather than auto; the authored main size must
not replace it. The bounded legacy column path therefore probes with authored
height/height-percent suppressed and restored. Definite zero resolves normally;
0% in an auto-height column retains the content dependency, unlike 0px.

The shared Q1000 axis resolver handles qualified nowrap rows/definite columns.
Legacy row, row-wrap and column consumers call the same resolver and project at
their existing integer boundary. This fixes their basis source but does not
migrate all legacy distribution, decoration, wrapping or intrinsic sizing.

The declaration parser now handles a lone basis and basis-first contiguous
factors as well as the existing factor-first syntax. Shorthand validates before
publishing factors, kind and retained size; omitted basis remains 0px. Whole
initial/unset reset the relevant components. Invalid negative literals, misplaced
factors and unsupported percentage math leave prior values intact. Switching
between length, auto and percentage clears obsolete pixel residues. The style
layout-equality check includes kind, even if both scalar integers are equal.

`@supports` previously used a second permissive grammar (for example accepting
mixed percentage math through a generic length test). A private shared syntax
entry now delegates Flex longhand/shorthand/factor queries to the declaration
parser, without publishing a Style or allocating a precision owner. Unsupported
percentage expressions are rejected rather than converted using the viewport.

Validation, Windows/Clang 22.1.8 Release:

- Full Core CTest 9/9; minimal families disabled 8/8, including the cropped
  percentage admission test. Earlier ordinary/Flex/ratio tests remain included.
- 48 newly authored rational scenes cover row/column, three fractional container
  bases, four percentage coefficients including 150%, content/border-box items,
  decoration, a percentage grandchild and following sibling positions. Expected
  values are computed with fractions and declared Q1000/edge projection, without
  Core/browser output. Regeneration is byte-identical. These are not blind tests
  or browser raster acceptance.
- Additional tests cover all admitted shorthand orders, supports agreement,
  decimal-half coefficients, huge/tiny bounds, resets, atomic invalid overrides,
  viewport independence, definite zero, integer wrap and indefinite columns.
  An equal-scalar length-to-percentage change refuses layout reuse.
- A full-library `-fsanitize=undefined -fsanitize-trap=all` build passes the
  percentage, prior Flex and Block tests (3/3); no whole-suite sanitizer claim.
- The installed-package-only consumer checks item width 38px, grandchild 19px,
  preserved author width and an indefinite column content height 12px. Its source
  is in `consumer/`. The percentage owner remains empty and Style prints 784B.

New test/source hashes and build roots are in validation.json. No new live
browser observation, MCU timing/heap measurement, Runtime lock or app-manifest
migration was performed. Existing experiment timings do not qualify this port.
This stage adds a Style kind field while preserving the measured host size;
there are no new LayoutBox fields. The development ABI remains 4; released ABI3
consumers still require coordinated manifests/package and a clean rebuild.

## Remaining boundaries

General content/max-content measurement, automatic minima, explicit content,
CSS-wide inheritance, contextual relative-unit precision, retained percentage
math, balanced shorthand math functions and complete wrap semantics remain
separate work. Definite percentage bases are admitted only within the prior
bounded precision path; legacy fallback retains integer rounding and its old
wider limitations. Grid and inline/positioned precision are still pending.

Next audit the intrinsic-size dependency group before broadening Flex admission,
then port Grid allocations using the same pass-local allocated-size contract.

## Reproduce

Configure a fresh Ninja/Clang Release build and run CTest. Minimal sets
JELLYFRAME_ENABLE_FLEX_GRID, ADVANCED_FORMS, CANVAS2D, MODERN_PAINT and IMAGE_FILE_IO
OFF. Sanitizer adds `-fsanitize=undefined -fsanitize-trap=all` to CMAKE_CXX_FLAGS
and runs tests matching `(flex_basis_percentage|computed_(flex|block)_layout)`.
Regenerate with `python tests/tools/generate_flex_basis_percentage_cases.py <out>`.
Install the full build, configure `consumer/` with CMAKE_PREFIX_PATH pointing to
that prefix, build and run flex_basis_consumer. Only installed headers/library
are used by this consumer.
