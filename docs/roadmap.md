# Render Core Active Roadmap

> Last updated: 2026-09-17; Applies to: 0.6.2 development line

This is the Core-only planning document. It does not schedule JellyFrame App
Runtime, device ports, launcher policy, JerryScript or developer-image work.
Those consumers decide when a released Core version is adopted.

## Current Development Line

Signed `v0.6.0` established Core ABI `1`; signed `v0.6.2` is the Runtime's
current locked dependency. The current `master` is the post-release `0.6.2`
development line. The latest synchronization imports reviewed Core changes from
JellyFrame mainline through `b1196f67`, superseding the earlier `0d04dfca` /
`8630bf6c` synchronization point. This source revision passed the monorepo's
desktop, scripting, sanitizer and standalone-Core CI and was integrated into the
accepted WS147 `0.6.2-ws147.2` developer image. The standalone line retains its
own build/install CI and deterministic source archives, and now also includes:

- LTR horizontal logical size, spacing and inset mapping.
- Common flex/grid placement (`order`, `align-self`, `place-*`, bounded rows).
- Bounded sRGB `hsl()` / `hsla()` and common image-background placement.
- Text letter spacing, scalar-safe `overflow-wrap: anywhere` and ellipsis.
- Bounded dirty-region and rounded-raster hot paths, cached text layout and
  viewport-aware units.
- Command owner/span trace instrumentation and Windows CPU2D comparison
  workloads.

`text-wrap: balance` was explored in historical commit `0fa5c41`, but is not
present in the current development implementation or capability surface. The
old candidate evidence is retained as historical context only and must not be
used as support evidence for this branch. Re-admission requires a new proposal,
positive/negative tests, three-target captures and an explicit Runtime decision.

This patch also closes an HTML parser depth-budget gap: `max_depth` includes the
synthetic `document` root, and a child that would exceed the bound is dropped before
it enters the DOM. A malformed-input corpus now protects the behavior.

## Next Release Gate

The next Core change is candidate evaluation, not an implicit Runtime upgrade:

1. Review the candidate source, public-header and generated-profile changes.
2. For an accepted capability, publish a reviewed signed Core release and its
   deterministic source archive plus SHA-256 sidecar.
3. Have JellyFrame Runtime update its exact package/version/ABI/source lock only
   after installed-package and local-source-override regressions pass.
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
