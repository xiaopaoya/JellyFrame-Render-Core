# C5 mixed-dimension consumer checkpoint (2026-11-11)

This candidate keeps CSS declaration admission closed. The production parser
does not call `parse_length_percentage_calc`; mixed values are constructed only
through internal typed APIs or direct Style fixtures.

The current layout admission gate is `LayoutEngine::build_layout_tree` in
`src/layout.cpp`. It permits typed mixed dimension slots only when each
affected render object is a static, normal Block-path object, and refuses
`FlexBasis` (slot 7) and any object outside that path before geometry changes.
The resulting supported consumer is the precise Block path through
`computed_content_axis` and `computed_size_units`: width, height, min-width,
min-height, max-width and max-height resolve against the actual definite
containing basis, preserve signed terms until combination, clamp at used-value
time, and subtract border-box decoration after evaluation.

Remaining refusals are intentional and have concrete callers:

* Flex width/height, min/max and flex-basis are refused by
  `LayoutEngine::build_layout_tree`; `layout_flex_box` has mixed-aware precise
  helper calls, but its legacy row/column algorithms still use scalar
  `Style` proxies and temporary width/height overrides. They do not yet
  provide a complete ordinary/precise replay contract.
* Grid item sizing is refused by `LayoutEngine::build_layout_tree` because
  Grid objects do not enter the qualified static Block path. `layout_grid_box`
  still has integer scalar sizing and a temporary width override, so it must
  not consume typed mixed values through proxies.
* Fixed table sizing is not admitted. `LayoutEngine::layout_fixed_table`
  requires integral pixel dimensions and must explicitly refuse mixed slots.
* Intrinsic width queries return `Unsupported` for mixed preferred/min/max
  sizes through `SourceWidthQuery::has_only_cyclic_width_sizes`; intrinsic
  height queries refuse authored vertical sizing in
  `SourceHeightQuery::run`. `DefiniteIndependent` remains explicitly
  unsupported in `SourceWidthQuery::run` until per-block contribution
  semantics are implemented.
* Replaced/native, inline, relative, decorated and unknown-basis contexts do
  not meet the layout preflight predicate; they must remain refused instead
  of using scalar proxies as complete values.

`apply_computed_length_percentage` now publishes all seven typed dimension
slots, including FlexBasis. For FlexBasis it sets the scalar proxy to `-1`
and resets `flex_basis_kind` to Length so typed storage remains authoritative;
layout admission still refuses that slot. No installed public header or API
changed.

Temporary width/height overrides in `SizeOverrideRestore` retain the shared
typed storage snapshot and restore table provenance and scalar proxies. This
does not establish forced precise-refusal replay parity for Flex/Grid, which
remains a release gate. Do not enable parser admission until Flex and Grid
consumers, explicit table/intrinsic refusals, full rollback, and installed
profile regressions pass together.
