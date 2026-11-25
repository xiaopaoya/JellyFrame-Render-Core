# ABI11 shared-inline contextual integration design

> Last updated: 2026-11-26; Applies to: 0.13.0 / ABI 11

## Current boundary

The scalar shared-inline path and the contextual shaped-text path are separate
pipelines.

- `layout_shared_inline` in `src/shared_inline.cpp` collects text, structural
  whitespace/events, generated content and atomic images into
  `CoreTextParagraph`. It retains per-owner paint styles, fragment rectangles,
  hit geometry and replay proofs in `SharedInlineSnapshot`.
- `CoreTextClusterMap` in `src/core_text_cluster_map.{h,cpp}` maps JFSP2 cluster
  source intervals to ordered DOM-owner byte ranges. It is covered by isolated
  unit tests and is not used by production layout or paint.
- `ContextualLayoutFrame::admit` in `src/contextual_text_layout.cpp` admits
  static block/view trees where each text host has one direct text child.
  `layout_paragraph` returns JFSP2 line packets. `LayerCapture` retains the
  background layer tree and its associated contextual layout paragraphs.
- Runtime's `RuntimeShapedTextBinding::layout_document` supplies the verified
  binding to `layout_contextual`. The installed Worker serializes contextual
  paragraph packets alongside a scalar `DisplayList`. It creates its
  `InputController` from the captured `LayerNode`; pointer input goes through
  `HitTester::hit_test(const LayerNode&)` and currently has no contextual
  cluster-owner side data.

The migration gap spans layout, source ownership, hit testing, layer order and
Runtime packet transport. Replacing scalar shaping in the current inline
snapshot without carrying the contextual packets through `LayerCapture` and the
Worker would create output that tests locally but cannot be presented by the
installed app.

## Required owner and paint contract

1. JFSP2 clusters are atomic paint units. When one cluster covers source bytes
   from several DOM owners, paint its glyphs once.
2. Every contributing owner receives the cluster's source interval and ink
   rectangle for hit testing. The source interval stays local to its DOM text
   node. The integration must explicitly transport owner fragments into the
   Runtime input path through a retained owner map associated with that frame's
   captured layout/layer state. Tests must lock down overlapping-owner hit
   ordering.
3. The first admitted run uses one uniform supported paint style across all
   text owners. This means one color, opacity 1, and no text shadow,
   decoration, transform, generated content or other effect rejected by the
   current contextual layout contract. Per-owner paint variation refuses the
   contextual inline transaction. The scalar shared-inline result remains
   available to ordinary non-shaped callers.
4. A failed map, resource check, packet validation or allocation publishes no
   contextual inline geometry or commands. Work already consumed remains
   charged. The caller retains the prior complete frame according to its
   existing transaction boundary.
5. No contextual result borrows source strings or font resources beyond the
   binding's existing retained lifetime contract. Retained packets own their
   bytes and retain the exact immutable Binding.

## Staged integration

### Stage A: source-map adapter

The private adapter is compiled with contextual text and is used by
`LayerCapture` when the caller requests text hit fragments.

The adapter builds owner tokens from `CoreTextParagraph::sources` and original
owner strings, admits only text-only source spans, and maps each real
contextual JFSP2 line packet to owner-local byte ranges. It verifies exact
normalized-text identity, projects trimmed and consumed line whitespace into
omitted records, checks UTF-8 scalar boundaries, and publishes only complete
maps. The bounded work budget includes source validation, projection, packet
decoding and cluster mapping. Unsupported source normalization and exhausted
work refuse transactionally.

Tests use real JFSP2 output for a ligature crossing two owners, split UTF-8
owners, and collapsed/trimmed whitespace. They also cover malformed source
tables, mismatched normalized text, truncated byte-offset tables, non-whitespace
source gaps, exhausted work, and preservation of prior output. Stage A does not
yet produce ink rectangles or input hit geometry. The contextual paint-style,
geometry and binding admission checks remain responsibilities of Stage B.

### Stage B: retained contextual inline result

The opt-in hit capture overload retains viewport-clipped glyph bounds and
original UTF-8 byte ranges for direct text owners alongside the painted
snapshot. The existing paint-only overload skips hit capture. A work or
fragment limit preserves the prior capture. Tests verify source ranges
across collapsed whitespace and wrapping, alignment, and pixel parity with
the paint-only path.

The explicit `LayoutEngine::layout_contextual_inline` entry point also admits
ordinary blocks containing uniformly styled text and neutral inline spans.
It shapes each run as one paragraph, recovers standalone collapsible
whitespace in DOM order and retains the source owner table. Capture inserts
the paragraph after the host background and validates its viewport-clipped
hit fragments before publishing. A ligature can have multiple source owners
with the same glyph bounds. Each owner resolves only while the document and
its recorded generation are valid.

Core verifies one real JFSP2 ligature spanning two DOM text nodes, equal
packets and pixels against the unsplit text, and rejection of mixed styles or
unsupported structure. Core hit testing selects the last contributing owner
in source order when several owners share a glyph. It rejects hits outside the
viewport and refuses stale or expired document snapshots. The Worker still
needs to use the capture associated with the presented frame.

### Stage C: installed Runtime path

Extend the shaped Worker serializer to consume the retained contextual inline
result without converting packets to scalar `PreparedText`. The frame must keep
the current shaped binding identity, command ordering, work limits, rollback
behavior and retained replay semantics. The Runtime production lock stays on
Core 0.13.0 until a later reviewed Core release is paired through full, cropped
and off qualification.

## Acceptance gates

- Core tests prove source-byte to owner mapping, cross-owner cluster paint-once
  behavior, deterministic hit-test ownership, style-disagreement refusal,
  source mutation invalidation, and transaction rollback.
- Pixel output is compared with direct JFSP2 painting for identical geometry.
  Tests include a cluster split across two owners and a multibyte source split
  at a UTF-8 boundary.
- Existing scalar shared-inline tests remain unchanged and pass for mixed fonts,
  colors, images, generated text, whitespace and decoration cases outside the
  contextual admission.
- Runtime full/cropped/off installed-worker qualification covers installed app
  presentation, worker acceptance, teardown/replay and feature-off refusal.
- Device acceptance identifies the exact Core archive, Runtime package, app
  generation and board image, then compares screen captures against the browser
  at the same viewport.

## Parallel work boundaries

The source-map adapter and its Core regression can proceed independently from
the Runtime frame-transport test design. The LayerCapture ordering change must
be integrated before either is considered a shipped capability. Device testing
starts only after the paired package and host gates pass.
