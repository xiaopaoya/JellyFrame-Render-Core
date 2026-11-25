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
  paragraph packets alongside a scalar `DisplayList`.

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
   Runtime input path, which currently creates its input controller from the
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

Build owner tokens from the normalized paragraph's `CoreTextParagraph::sources`
and original source ranges. Admit `white-space: normal` only. Feed the exact
normalized text to `contextual_text::layout_paragraph`, then require its
`Paragraph::text()` bytes to equal the Core normalized input before mapping.
For each line packet, shift its cluster byte ranges by `line.source_begin` and
map them against that line's visible source interval. Represent leading,
collapsed, trimmed and consumed whitespace as omitted source records rather
than glyph owners. Feed those tokens and decoded JFSP2 cluster/glyph intervals
to `map_core_text_clusters`. An initial admission requires text-only spans,
one matching font binding, uniform geometry and paint styles across the run,
and no generated content, explicit break/opportunity events, atomics, preserved
whitespace or unmappable normalization. Add split-owner ASCII ligature, split
multibyte scalar, collapsed/trimmed whitespace and malformed/unsupported
cluster cases. Unsupported normalization must refuse before publishing.

### Stage B: retained contextual inline result

Add an opt-in contextual inline result owned by the existing contextual layout
transaction. It carries JFSP2 commands and per-owner hit fragments with glyph
ink rectangles derived from decoded packet positions and the bound font
catalog's glyph metrics. Keep existing `Style`, `RenderObject`, `LayoutBox` and
`DisplayCommand` footprints unchanged. Layer capture must match the current
scalar shared-inline paint order and final pixels. A cross-owner glyph command
uses the uniform paint style proven by Stage A, while its mapped source
fragments update each owner's hit region through a private retained side table
and an explicit Runtime input adapter.

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
