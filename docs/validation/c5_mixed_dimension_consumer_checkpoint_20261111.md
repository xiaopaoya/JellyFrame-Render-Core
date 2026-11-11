# C5 mixed-dimension consumer checkpoint (2026-11-11)

CSS declaration admission remains closed. The production parser does not call
`parse_length_percentage_calc`; mixed values are constructed only through
internal typed APIs or direct Style fixtures.

Static normal Block objects support all six dimension properties. The precise
Block path resolves them through `computed_content_axis` and
`computed_size_units`, preserving signed terms until combination, clamping at
used-value time, and subtracting border-box decoration after evaluation.
Indefinite percentage-bearing height remains auto/content as a whole
expression.

The qualified nowrap Flex slice admits static Flex containers with a definite
main size and direct static empty Block items. The qualified wrapped-row slice
uses the existing bounded text-query planner: at most 128 direct items, simple
Block items with zero or one direct text child, start packing, no aspect ratio,
and a supplied TextSystem. Both routes resolve mixed main/cross dimensions and
FlexBasis; an indefinite column main basis uses content behavior. When the
precise plan declines that indefinite column slice, ordinary Flex runs with
temporarily materialized typed values under complete Style snapshots, while
the mutation journal protects geometry and prepared-text caches.

Grid admits direct static empty Block items under a static definite-width Grid
container. The ordinary assigned-track path resolves mixed width/min-width/
max-width against each item's track width and preserves the authored result at
the final rectangle boundary. Its RAII override restores typed storage, all
dimension scalar and percent proxies, FlexBasis kind, table provenance and
box-sizing.

Remaining refusals have concrete callers:

* Other Flex shapes are refused by `LayoutEngine::build_layout_tree`: positioned
  or replaced items, nested/non-Block items, wrapped columns, wrapped rows
  outside the bounded text-query shape, and indefinite row main sizing. The
  ordinary fallback projects resolved values to integer pixels.
* Grid item mixed height/min-height/max-height and mixed sizing on the Grid
  container are refused. `layout_grid_box` supplies an indefinite row basis
  during track discovery; only the definite column track is admitted.
* Fixed table sizing is not admitted. `LayoutEngine::layout_fixed_table`
  requires integral pixel dimensions and rejects retained non-pixel slots.
* Intrinsic width queries admit mixed preferred width only as wholly auto
  under `CyclicSameAxis`, through
  `SourceWidthQuery::has_only_cyclic_width_sizes`; mixed min/max remain
  `Unsupported`. Intrinsic height queries refuse authored vertical sizing in
  `SourceHeightQuery::run`. `DefiniteIndependent` remains `Unsupported` in
  `SourceWidthQuery::run` until per-block contribution semantics exist.
* Replaced/native, inline, relative, decorated and unknown-basis contexts do
  not meet the layout preflight predicates and cannot consume scalar proxies
  as complete values.

`apply_computed_length_percentage` publishes all seven typed dimension slots,
including FlexBasis. It sets the FlexBasis scalar proxy to `-1` and resets
`flex_basis_kind` to Length so typed storage remains authoritative. Ordinary
Flex replay temporarily uses Percentage kind for an unresolved mixed basis to
select whole-expression content behavior, then restores the authored kind from
its Style snapshot. No installed public header or API changed.

Focused consumer tests compare mixed results with equivalent pixel fixtures
for Block, nowrap Flex, wrapped-row Flex and Grid. They also check authored
typed storage restoration after Grid overrides and ordinary Flex replay.
Parser admission stays closed until unsupported-context probes, fixed-table
refusal coverage, full parity over all qualified edge cases and cropped-profile
package validation pass together.
