# Changelog

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
