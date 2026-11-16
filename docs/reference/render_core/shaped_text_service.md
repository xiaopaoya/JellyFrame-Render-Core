# Selected-Font Shaped Text Service

`shaped_service::prepare` is a private Core composition boundary for selected
font shaping. It accepts an admitted raw UTF-8 run plus immutable, identity-
bound normalization tables, per-font composition eligibility, a raster
catalog and caller-owned scratch buffers. It normalizes, executes the selected
catalog-bound GSUB plan, resolves advances for substituted GIDs, applies GPOS,
scales geometry to Q1000 CSS units, computes run metrics and source clusters,
then encodes a JFSP1 shaped packet.

The service publishes its packet and metrics together only after every stage
succeeds. Work is cumulative across stages. Every scratch range is checked
against other writable ranges, source/resource bytes and the retained packet;
the budget and request/configuration objects cannot overlap writable storage.
The service uses original font-unit advances and upem scaling for pen geometry,
while bitmap bearings and dimensions use the catalog raster strike size. Font
Y-up positioning values are converted once to Core's CSS-down coordinates.

The current bounded profile supports the admitted Noto shaping selections and
the catalog's single/ligature substitutions plus pair and mark attachment
positioning. Unsupported scalars, missing raster GIDs, invalid resources,
overflow, exhausted budgets or insufficient capacities refuse without
publishing partial metrics or packets. Empty runs produce an empty valid
packet. Source intervals are UTF-8 byte ranges; they are not grapheme or caret
boundaries.

This service is not registered with `TextSystem`, layout, paint or Runtime.
Existing product rendering remains on the scalar text path until those
integrations and device acceptance are completed. HarfBuzz evidence is recorded
in the shaping fixture tests; the work does not claim general browser text
parity.
