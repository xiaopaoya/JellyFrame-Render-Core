# Flex auto-minimum policy and intrinsic-size dependency audit

Local Core 0.9.0-dev/ABI4 stage after 1172db5e; not merged or device-qualified.

## Audit result

The experiment separates automatic-minimum policy (`src/flex_auto_min.*`) from
source measurement (`intrinsic_source_sizes`, `intrinsic_flex_rows/sizes`, inline
plans and natural image data). Porting the policy does not port measurement.

| Dependency | Mainline status and migration decision |
| --- | --- |
| Automatic-minimum arithmetic | Ported here in border-box Q1000 units, with explicit completion/refusal. |
| Percentage/min/max/ratio suggestions | Reuses computed-size resolution and checked ratio scaling. |
| Overflow classification | Uses mainline's single coupled overflow string; independent x/y semantics are not qualified. |
| Text min/max-content | TextSystem owns font measurement, but a whole-string advance is not min-content width. A complete query is still needed. |
| Mixed inline/block source flow | Experimental plans handle whitespace, breaks and anonymous groups; final rectangles cannot replace source measurement. |
| Replaced intrinsic metadata | Arithmetic for replaced suggestions is tested, but natural image data is not wired to this consumer. |
| Work/capacity/refusal | Experimental IntrinsicSizingFrame accounts for work and scratch. Mainline box/depth limits do not bound repeated intrinsic queries. |

`grid_intrinsic_outer_width` is not a general min-content consumer: it honors a
box's declared width before measuring content, measures whole transformed text,
and has simplified inline/block aggregation. These differ from content minimum,
even when a short fixture returns the same number. Grid behavior is unchanged.

## Implemented policy and bounded consumers

[Flexbox 4.5](https://www.w3.org/TR/css-flexbox-1/#min-size-auto) combines content
and ratio-transferred suggestions, choosing differently for replaced and
non-replaced items, then applies preferred-main and maximum-main caps. The port
preserves these choices, max-then-min cross constraints, decoration floors and
explicit missing-content state. Checked Q1000 ratio transfer avoids rounding
suggestions to pixels before allocation. Invalid ranges, sentinels and half-present
ratios refuse before any completion shortcut.

The qualified nowrap path now asks this policy instead of rejecting every
child-bearing or ratio-bearing automatic-minimum item. Admission is limited to:

- `hidden`/`auto`/`scroll` in the existing coupled overflow model, whose automatic
  minimum is zero content plus unavoidable decoration;
- a definite preferred-main or max-main cap at/below the decoration floor,
  proving the answer without content measurement;
- empty ordinary boxes with known zero content, including authored ratio
  transfers and definite cross constraints handled by the policy.

Unknown content stays Incomplete and the container uses the legacy path. Prior
rectangles, failed font measurements and Grid estimates are not measured zero.
Known native/replaced/special leaf tags do not acquire an empty-content proof.
Natural image metadata is not qualified. Explicit minima retain their behavior.
Style/source minima stay unchanged. The policy adds no persistent fields or heap
allocations; input/result are pass-local. Existing Flex vectors remain.

## Validation

- Windows Clang 22 Release full CTest: 10/10; minimal families OFF: 9/9.
  The implementation translation unit belongs to FLEX_GRID; the cropped test
  verifies the unsupported stub.
- Sixteen hand-authored arithmetic cases cover incomplete content, cap proofs,
  replaced/non-replaced choices, main caps, conflicting cross constraints,
  content/border ratio boxes, fractional thirds and extreme saturation.
  Classifier checks cover both axes, overflow, explicit zero/percentage minima
  and invalid states.
- Ten layout scenes cover decorated fractional shrink with hidden/auto/scroll
  in both directions (six), two main-cap proofs with descendants and two empty
  ratio transfers. Prior Block/Flex/percentage tests remain included.
- A full-library UBSan trap build passes auto-min, percentage-basis, prior Flex
  and Block targets (4/4), including extreme arithmetic. This is not whole-suite
  sanitizer acceptance.
- The installed-package-only consumer verifies 50/51px adjacent boxes, 44/45px
  content descendants and a 41px ratio minimum. Style remains 784 bytes on this
  host. Complete consumer sources are in `consumer/`.

These are Core checks. No live browser corpus, MCU resource measurement, new
font fixture or Runtime package/manifest acceptance was added. Broader experiment
acceptance is not inherited. Hashes/build roots are in validation.json.

## Next dependency group

Build a Core-internal source query returning complete min/max-content dimensions
in retained units or explicit refusal, without new port measurement callbacks.
Use Core-owned TextSystem/fonts and the normal whitespace/break model; distinguish
inline/block contributions and definite/indefinite containing bases. Bound query
work, depth and temporary capacity before publishing results. Start with ordinary
horizontal text/block flow; keep nested Flex/Grid, positioned/replaced content and
unsupported text behavior outside admission until qualified. Repeated-query reuse
needs source/basis identity and separate cost evidence. Feed this policy only
complete results after that query gate.

Then extend intrinsic/wrapped Flex and Grid allocation consumers. Independent
overflow axes, natural images, CJK shaping/wrapping, retained percentage math and
inline/positioned precision remain separate. Runtime 0.9 manifests/lock still
require coordinated qualification before publication.

## Reproduce

Use fresh Ninja/Clang Release builds and CTest at the recorded roots. Minimal
disables FLEX_GRID, ADVANCED_FORMS, CANVAS2D, MODERN_PAINT and IMAGE_FILE_IO.
Sanitizer adds `-fsanitize=undefined -fsanitize-trap=all` to CMAKE_CXX_FLAGS and
runs `(flex_auto_min|flex_basis_percentage|computed_(flex|block)_layout)` tests.
Install the full build, configure `consumer/` with CMAKE_PREFIX_PATH pointing to
the prefix, build and run flex_auto_min_consumer. It uses installed public files.
