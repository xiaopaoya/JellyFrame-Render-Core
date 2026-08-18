# Render Core Active Roadmap

> Last updated: 2026-08-19; Applies to: 0.6.1

This is the Core-only planning document. It does not schedule JellyFrame App
Runtime, device ports, launcher policy, JerryScript or developer-image work.
Those consumers decide when a released Core version is adopted.

## Current Release Candidate

Signed `v0.6.0` established Core ABI `1`. `master` now prepares the compatible
`0.6.1` patch release. It retains standalone build/install CI and deterministic source
archives, and includes the following completed authoring subset:

- LTR horizontal logical size, spacing and inset mapping.
- Common flex/grid placement (`order`, `align-self`, `place-*`, bounded rows).
- Bounded sRGB `hsl()` / `hsla()` and common image-background placement.
- Text letter spacing, scalar-safe `overflow-wrap: anywhere`, ellipsis and
  bounded `text-wrap: balance` for short natural text.

`text-wrap: balance` is implemented in `0fa5c41`. Its unit, standalone,
benchmark and three-target desktop candidate evidence is recorded in
[`validation/text_wrap_balance_candidate_20260817.md`](validation/text_wrap_balance_candidate_20260817.md).
It remains a candidate capability until the signed Core release is consumed by
a locked Runtime dependency; it must not be advertised by an older Runtime
default build.

This patch also closes an HTML parser depth-budget gap: `max_depth` includes the
synthetic `document` root, and a child that would exceed the bound is dropped before
it enters the DOM. A malformed-input corpus now protects the behavior.

## 0.6.1 Release Gate

The next Core change is release closure, not another speculative CSS feature:

1. Review the candidate source, public-header and generated-profile changes.
2. Create an annotated, signed `0.6.1` tag and publish its deterministic source
   archive plus SHA-256 sidecar.
3. Have JellyFrame Runtime update its exact package/version/ABI/source lock and
   run its installed-package and local-source-override regressions.
4. Have Device OS record the exact consumed Runtime/Core provenance in a named
   board profile before making any device capability claim.

Until those gates are complete, a host may use a local source override for
cross-repository development, but production consumers must not float on this
branch.

## Candidate Intake After 0.6.1

A new Core capability starts only with an author-facing need and a bounded
proposal. Each accepted item requires positive and negative behavior tests,
three target desktop captures, capability/diagnostic/recipe updates and a
hot-path benchmark when it touches layout or paint.

`font-style` is deliberately not treated as a parser-only quick win. Correct
support requires a versioned text-style contract across text measurement,
painting and every host adapter, plus a defined fallback for bitmap fonts.
That work is a post-release candidate, not a silently ignored declaration.

## Explicitly Deferred

Do not make container queries, `:has()`, complex grid/subgrid, filters,
backdrop filters, Shadow DOM, Worker, iframe, full SVG/video, browser font
loading or complex-script shaping default `0.6` scope. Retained replay,
framebuffer reuse and tile/scanline rendering also remain separate proposals
with memory, pixel-correctness and target telemetry gates.
