# Render Core Active Roadmap

> Last updated: 2026-11-13; Applies to: 0.9.0-dev / ABI 4 qualification line

This is the Core-only planning document. It does not schedule JellyFrame App
Runtime, device ports, launcher policy, JerryScript or developer-image work.
Those consumers decide when a released Core version is adopted.

## Current Development Line

Signed `v0.8.0` / ABI `3` is the Runtime production dependency. Core `master` is
`0.9.0-dev` / ABI `4` at `ef042b26`. ABI4 is a qualification track, not yet a
signed Runtime dependency. Runtime master `14aa785f` pins this Core revision in
its full/cropped installed-consumer matrix using CMake package identity
`aa908e3852afd4a06ed6df6b26a5cc759cfefcd7d39c02270d1304aa633d18c4` across 165
implementation inputs. Those consumers cover frame v9/v10, generated-text
replay, and shared-inline native decimal/disc list markers. The bounded shaping
foundation prototypes are isolated and are not exercised as runtime features.
The standalone archive and consumer gates run in CI. A signed ABI4 release and
named Device OS image acceptance remain open. The current development line
includes:

- LTR horizontal logical size, spacing and inset mapping.
- Common flex/grid placement (`order`, `align-self`, `place-*`, bounded rows).
- Bounded sRGB `hsl()` / `hsla()` and common image-background placement.
- Text letter spacing, scalar-safe `overflow-wrap: anywhere` and ellipsis.
- Bounded dirty-region and rounded-raster hot paths, cached text layout and
  viewport-aware units.
- Command owner/span trace instrumentation and Windows CPU2D comparison
  workloads.
- A private cluster/source/owner mapper and offline GID bitmap catalog spike,
  which establish bounded prerequisites for generated shaping resources.

`text-wrap: balance` was explored in historical commit `0fa5c41`, but is not
present in the current development implementation or capability surface. The
old candidate evidence is retained as historical context only and must not be
used as support evidence for this branch. Re-admission requires a new proposal,
positive/negative tests, three-target captures and an explicit Runtime decision.

This patch also closes an HTML parser depth-budget gap: `max_depth` includes the
synthetic `document` root, and a child that would exceed the bound is dropped before
it enters the DOM. A malformed-input corpus now protects the behavior.

## Next Release Gate

The first shaping-foundation prototypes are merged; they are not an implicit
Runtime upgrade. The offline `JFGID1` spike shows that Core can rasterize a
small set of non-cmap glyph IDs, and the private cluster/source/owner mapper has
synthetic coverage tests. Neither integrates shaping or changes `JFFONT2`,
JFTP, the public API, or default behavior. The bounded selected-profile
prototype now produces the same 71-GID missing-output inventory for all three
product weights and matches eight HarfBuzz cases per face; its three 16 px GID
catalogs total 23,938 bytes. This is bitmap-catalog size only, not a complete
shaping resource or runtime budget. The next gate is to define a deterministic
encoding for the admitted lookup profile and measure table plus catalog size.
An isolated N16R8 build should then measure flash delta; target load peak,
scratch, bounded work, latency, and physical-device rendering remain later
gates. See the closure evaluation for exact inputs and limits.

ABI4 release and adoption remain serial gates:

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

## Candidate Intake During ABI4 Qualification

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
