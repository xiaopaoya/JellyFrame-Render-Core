# Ordinary computed-dimension layout

Local Core 0.9.0-dev/ABI4 stage after 526f68d8; no remote or device acceptance.

The common Q1000 resolver consumes retained width/height/min/max values and
fractional percentage coefficients. Ordinary in-flow block chains carry content
bases and origins in stack-local recursive context, with no new LayoutBox data
fields or allocations for that context. Width percentages use the full parent
content basis; auto widths separately subtract margins and decoration. Box-sizing
conversion and max-then-min constraints happen before pixel projection. A
definite constrained height becomes the descendant percentage basis; an ordinary
auto-height parent leaves that basis indefinite. The existing document/html
viewport-root percentage behavior is preserved explicitly.

Child flow extents accumulate in Q1000. Final rectangles project both edges,
rather than rounding each sibling's height independently. Existing software
paint and layout/layer hit testing consume those projected rectangles. Root
layout no longer overwrites the root border-box height with its flow extent.
Checked/split products bound large ratios and manual percentage values; int64
edge extremes project without signed overflow.

The new path is admitted only through ordinary in-flow Block nodes. Text/native
controls, inline/inline-block, Flex/Grid and positioned calls retain their prior
layout paths, so their temporary scalar allocations cannot accidentally be
overridden by retained authored dimensions. This is a transitional boundary:
ordinary descendants below these unmigrated formatting contexts are not precision
qualified either. Inline descendants of an admitted block still receive rounded
available width through the existing inline solver. This stage does not qualify
exact inline wrapping, natural image metadata, margin collapsing, tables,
ratio-dependent percentage cycles or fractional Flex/Grid distribution.

Validation:

- 72 rationally authored nested scenes cover fractional content/border-box parent
  widths, child/grandchild percentages, definite fractional heights and margins.
  The generator reads no Core or browser output. Intermediate Q1000 quantization
  and final edge projection are declared assumptions, not browser raster parity.
- Hand-authored checks cover conflicting min/max, constrained descendant bases,
  indefinite heights, auto margins, fractional vertical flow, actual paint pixels,
  layout/layer hits, arena operation, depth refusal and Flex override isolation.
- Core Release full/minimal each pass 7/7 CTest targets. A full-library
  `-fsanitize=undefined -fsanitize-trap=all` build passes the dedicated block
  layout test (1/1); no whole-suite sanitizer claim is made.
- A consumer built solely against the installed package resolves 37.5% of a
  100.25px parent to a projected 38px box. Style remains 784 bytes on the previously
  measured Windows host; this stage adds no Style or LayoutBox members.

Source/fixture hashes are recorded in validation.json. No new live browser
collection or MCU cost measurement was performed. Runtime manifests/lock still
need coordinated migration from 0.8 to the new candidate line; the older 56/56
ABI3 consumer result is not acceptance for this layout stage.

Next: carry the shared resolver into Flex/Grid after replacing or suppressing
retained slots consistently during temporary allocated-size overrides, then
qualify those paths and retire duplicated scalar solving where possible.
