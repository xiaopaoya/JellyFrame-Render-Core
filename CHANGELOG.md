# Changelog

## 0.12.1 / ABI11

- Preserve intrinsic image dimensions for start-aligned block images with
  auto height in a single fixed Grid column. This covers integer widths and
  unconstrained, undecorated auto widths.
- Publish input state only after DOM destruction observers are registered.
  Roll back new registrations on failure and clean up observers if autofocus
  throws during construction.

## 0.12.0 / ABI11

ABI11 changes the public `Node` layout. Rebuild every Core consumer.

- Add bounded, opt-in tracked computed-style reuse for render-tree builds.
  The feature defaults off and refuses unsupported selectors, oversized
  snapshots, and mismatched DOM, resolver, viewport, or builder state.
- Track local style and structural mutations and invalidate the document
  structure before destroying descendants, including `set_text_content()`.
- Bind snapshot completion to the exact tree returned by the prepared builder.

## 0.11.0 / ABI10

Rebuild all consumers against the new public style and text layout cache
definitions. The installed CMake package accepts the 0.11 version line.

- Apply bounded CSS `word-spacing` to ASCII and nonbreaking spaces in Core
  scalar text, including layout, glyph placement and admitted intrinsic sizes.
- Measure bounded intrinsic height for uniform BMP Han paragraphs with
  ordinary whitespace and hard breaks.

## 0.9.0 / ABI4

ABI4 is a source and binary compatibility break from ABI3. Rebuild every Core
consumer and review the exact package profile and source manifest before
adoption.

### Breaking changes

- Remove host-owned text measurement, line metrics, wrapping and glyph-paint
  callbacks, including `TextMeasureProvider`, `TextPainter`, range/family
  helpers and bitmap-font measurement/paint helpers. Core owns text selection,
  metrics, preparation and rasterization from registered font resources.
- Remove provider-driven layout cache identity and repaint/reuse hooks. Hosts
  use the Core-owned text and layout contracts instead of supplying parallel
  text policy.
- Replace flat display-command paint fields with kind-specific typed payloads.
  Rebuild callers and use the accessor for the selected command kind.

### Added and expanded contracts

- Add Core-prepared text packets for inline text and native form controls,
  including visible select-popup rows, bounded ellipsis and shared paragraphs.
- Add independent horizontal and vertical corner radii, layered backgrounds,
  typed image-resource metadata and the gated natural-image sizing API.
- Preserve the deterministic source archive, source manifest and generated
  feature profile as the package identity evidence.

The supported subset and explicit exclusions remain documented in the Core
reference. ABI4 does not claim full browser CSS or shaping parity.
