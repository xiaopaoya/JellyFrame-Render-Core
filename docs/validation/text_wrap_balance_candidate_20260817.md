# Historical Text Balance Candidate Evidence

> Recorded: 2026-08-17; Core source revision: `2df7ad7`; status: superseded historical evidence

This record describes an earlier candidate line. It is retained for provenance,
but the current `0.6.2-dev` branch does not contain the implementation and this
document is not a support or release claim.

## Intent

Validate the bounded `text-wrap: balance` subset before it is admitted through
a signed Core release and a Runtime dependency lock. This is not hardware
evidence and does not change the Runtime default capability matrix.

## Evidence

- Unit coverage: CSS cascade and `@supports`, normal-wrap fallbacks, bounded
  break-unit behavior, shared layout/layer line generation and hostile measure
  callback arithmetic are covered by `jellyframe_render_core_tests`.
- Benchmark: `jellyframe_render_core_microbench 200` reported
  `text_balance_layout=6.84 us` and `text_balance_layer=3.945 us` on the local
  desktop probe. These values are comparative host attribution only.
- Desktop source-override build: JellyFrame Runtime configured with
  `JELLYFRAME_RENDER_CORE_SOURCE_DIR` pointing at this Core checkout, with only
  `jellyframe_pseudo_browser` built.
- Captures: `round-300` (300x300), `rect-320x240` and `rect-172x320`, using
  `samples/pages/modern/text_wrap_balance.*` as the source fixture. Each final
  diagnostics report had zero entries, no horizontal overflow and no vertical
  overflow. BMP output was visually inspected for clipping, overlap and
  background artifacts.

## Reproduction

Configure a JellyFrame Runtime checkout with a local Core source override, then
invoke its `jellyframe_pseudo_browser` on the paired sample files at the three
viewport sizes above with `--diagnostics-json`. The capture fixture intentionally
stays within the documented 2-4 line, short natural-wrap range.

## Historical Exit

The historical candidate evidence was complete for its then-current source
line. It was not carried into the current `0.6.2-dev` implementation. Any
future re-admission must repeat the current candidate gate.
