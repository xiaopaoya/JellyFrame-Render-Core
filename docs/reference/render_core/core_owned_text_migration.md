# Core-Owned Font and Text Migration

## Goal

Render Core owns font selection, metrics, scalar text preparation, layout
geometry, and software glyph painting. Runtime and platform ports register
immutable font resources and provide storage/display access; they do not
provide text measurement, line metrics, shaping decisions, or glyph-paint
callbacks.

The public migration is a coordinated ABI change. It removes
`TextMeasureProvider`, `TextPainter`, `HostTextAdapter`, and the old JFFONT
resource loader rather than preserving compatibility wrappers. Existing
products must migrate their font assets and consumers in the same release
series.

## Contract

- Core accepts the canonical JFFONT2 resource profile and validates its
  metadata, checksum, glyph table, coverage bounds, and resource limits before
  publishing a resource view.
- Font bytes and face registrations are immutable and remain owned by Runtime
  for every layout, prepared-text, paint, and retained-frame reference.
- Core computes scaled ascent, descent, line height, advances, bearings,
  fallback selection, and glyph geometry from registered resources.
- A text request identifies resolved family IDs, size, weight, style, and
  spacing. Core applies a documented bounded scalar profile and reports
  unsupported shaping or coverage explicitly.
- Layout and paint consume the same Core `TextSystem`; prepared glyph geometry
  is reused instead of asking Runtime to measure the same source again.
- Resource or text-operation failure is transactional: no partially accepted
  catalog, layout publication, prepared output, or target paint is exposed.
- Runtime owns resource loading/session lifetimes and display access. Core does
  not depend on Win32, ESP-IDF, LVGL, a host font API, or an RTOS.

## API Replacement Surface

| Existing Core surface | Replacement |
| --- | --- |
| `BitmapFontResource` and JFFONT0/1 parsing | `FontResource` and JFFONT2 |
| `TextMeasureProvider` in `LayoutEngine` | `LayoutEngineOptions::text_system` |
| `TextPainter` in layer/software rendering | Core-prepared text consumed by `TextSystem::paint` |
| `HostTextAdapter` and adapter factories | No callback bridge |
| Provider parameters on style/text repaint | Retained Core text snapshots and the bound `TextSystem` |
| Callback text on Canvas2D | Core text service in the Canvas2D context |

The initial supported glyph profile is bounded and explicit. It does not imply
general Unicode shaping, bidi, ligatures, kerning, or cluster-safe wrapping.
Unsupported content must refuse through the existing failure contract; it
must not silently fall back to host metrics or a second renderer.

## Migration Order

1. In Core, replace the font resource format and public text-service entry
   points; migrate layout, layer building, repaint, software rendering, and
   Canvas2D in the same ABI change. Remove old callback types, loader, adapter,
   and their compatibility tests. Keep the existing product feature profile
   opt-in until Runtime coverage and resource delivery pass their gates.
2. In Runtime, update the package lock to the reviewed Core ABI, migrate the
   desktop shell, native UI, script worker, benchmark fixtures, and app-font
   packaging to registered Core resources. Remove callback plumbing and old
   JFFONT asset generation. Do not change product defaults in this migration.
3. Qualify the matched Core/Runtime source and installed package in full,
   cropped, and disabled feature profiles; exercise desktop and ESP32 builds,
   worker/session teardown, allocation failures, and the named physical smoke
   page. Open separate PRs in dependency order and merge each only after CI is
   green. Keep a release artifact and clean-checkout supply path as an explicit
   gate before changing defaults.
4. Port additional feature-expansion scopes only after the new Core API and
   Runtime package are on mainline. Do not merge the experiment branch history.

## Mainline Baseline

The starting Core mainline is `0.7.0 / ABI 2` at `1308a640`. It contains the
callback-based `TextMeasureProvider`, `TextPainter`, `HostTextAdapter`, and
JFFONT0/1 loader; it has no `CoreTextFrame`, `FontResource`, `TextSystem`, or
JFTP implementation. The experiment is based on a different implementation
history (`0.6.2 / ABI 9`), so its feature deltas are reference material, not
standalone cherry-picks.

This document specifies the migration boundary and release order. It does not
claim that Core, Runtime, packaging, or device qualification has been migrated.

## Baseline Validation

At the clean Core `master` revision `1308a640`, Visual Studio 2022 x64 Release
builds `jellyframe_render_core_tests`, and CTest passes 1/1. The baseline build
is isolated at
`D:/CodexTemp/Builds/core-owned-font-mainline-baseline-20261105`.

## Implementation Checkpoint (2026-11-05)

The mainline topic branch `codex/core-owned-font-text-mainline` now contains
the JFFONT2 `FontResource`, its bounded preparation tool, and Core-owned
`TextSystem` font selection/metrics/scalar preparation. The source and package
consumers are registered in the mainline CMake structure. Core Release CTest
passes 3/3. The font-resource generator suite passes 4 tests; its optional real
Noto check was skipped because that test did not receive a font. A separate
real-font TextSystem check does use the local Noto Sans SC Regular/Medium/Bold
sources at 16/24/32 px and passes 21 selection/metric rows covering 126 glyphs.
Installed-package FontResource and TextSystem consumers pass 1/1 each.

This is still an in-progress, additive source branch. The old JFFONT loader,
text callbacks, and adapter remain until layout, layer painting, repaint,
Canvas2D, and Runtime consumers are switched together. Do not open a merge PR
or publish a package from this intermediate checkpoint.

This Core slice adds the JFTP v1 prepared-text packet codec and a bounded
Core-owned bitmap paint primitive. The decoder allocates no memory; painting
validates packet glyph identity and geometry and admits total work before target
writes. Synthetic raster tests cover clipping, tile equivalence, and unchanged
targets on budget refusal. Core CTest passes 3/3 and the installed TextSystem
consumer passes 1/1. A dedicated `PreparedText` display command carries JFTP
through the Core software rasterizer when the frame is supplied its bound
`TextSystem`; it uses the command rectangle's
integer x/y as glyph origin and baseline, with the rectangle and outer clip
bounding paint. The display command remains within its existing 160-byte size
gate. `TextFontFace` registrations
can now carry the normalized CSS family hash, and `TextSystem::resolve_family_hash`
maps that parser token to the stable family ID without a callback. Binding rejects
conflicting hash-to-family mappings. Layout now consumes that mapping for a
bounded scalar-layout case: when a run uses the supported repertoire and needs
no letter spacing or ellipsis, `LayoutEngineOptions::text_system` measures,
wraps where required, and prepares per-line JFTP packets. The layout cache
retains each packet and baseline plus TextSystem identity; the matching
`LayerTreeBuilderOptions::text_system` emits one `PreparedText` command per
line, which the compositor paints through that service. Grid intrinsic widths
use the same Core metrics. Unsupported shaping, line controls, spacing,
ellipsis or missing family mappings are diagnosed and suppressed on
this opt-in path, with no host-text callback fallback. The old provider path
remains selectable while no Core TextSystem is configured. Runtime still must
register matching aliases and retain resource bytes for the frame; full CSS text
layout and removal of the callback ABI remain open. Layout preparation defaults
to a per-text-node limit of 128 glyphs to cap retained JFTP packet size; ports
can lower the limit through `LayoutEngineOptions`.

The layout-to-paint slice has regression coverage for refusal of an unregistered
CSS family and a run over the prepared-glyph limit. Both cases emit a layout
diagnostic, publish no text paint command, and do not call a supplied legacy
host measurement callback. A wrapped two-line run produces two prepared paint
commands. Prepared-text cache coordinates now track layout shifts;
the cache validity check also includes the text box position so stale absolute
baselines are not reused. TextSystem catalog generation is also part of cache
validity, so rebinding fonts invalidates packets made against the old catalog.
The complete Core Release CTest suite passes 3/3.

The Runtime integration worktree is based on fetched Runtime `master` and has
an additive Core font catalog. Manifest loading now routes JFFONT2 into that
catalog, while JFFONT0/1 keep the old loader. The Win32 browser can opt into
the same `TextSystem` for layout, Canvas2D, layer construction and software
composition when the package provides a valid default face. The ESP32 image
host now binds a flash-resident family-0 Noto face, and its retained UI has an
opt-in Core route through layout and software composition. That route remains
disabled by default pending visual acceptance; script-frame rendering still
uses the legacy path. Callback ABI removal and old-loader removal remain
blocked on those consumers and a desktop default family-0 face.

Canvas2D now has an opt-in Core text-service path: `measureText` uses Core
metrics, while `fillText` prepares a bounded JFTP packet and paints through the
Core software rasterizer. Tests cover metrics, visible paint, unsupported text
refusal, and the absence of fallback to a supplied legacy callback. The old
Canvas text backend setter remains until Runtime consumers move.
Release validation also passes with `JELLYFRAME_ENABLE_CANVAS2D=OFF`: the Core
aggregate and TextSystem test executables pass 2/2 in that cropped profile.
The Win32 Runtime shell binds `set_text_system` for its opt-in app-font mode.
Other Runtime consumers still need to bind the matching catalog before their
Canvas or DOM text uses Core in production.

The Runtime `AppFontSet` integration worktree now has an additive JFFONT2
catalog API. It owns a copied family-0 system resource, can copy or borrow app
resources, assigns catalog-local family IDs and monotonic resource IDs, binds a
complete Core `TextSystem` transactionally, and rebuilds it after app teardown.
Its Runtime test covers family resolution, Core measurement, rollback when no
default face exists, generation changes, and preservation of the system face
after app exit. Manifest loading and the Win32 browser now consume this catalog.
Device UI, ESP32 and script-frame renderer consumers remain to be migrated.

## Installed Script-Frame Checkpoint (2026-11-06)

The Runtime topic worktree now threads the host-owned `TextSystem` through the
installed-script worker's `LayoutEngine` and `LayerTreeBuilder`, and into the
UI task's `ScriptTaskFrameRenderer`. The value-frame codec v5 admits opaque
Core JFTP `PreparedText` packets while v1-v4 retain their previous command
sets. Host ownership lasts through worker and UI teardown. Runtime desktop
builds with the script-task target enabled pass, as do the focused app-runtime
and script-task test targets. ESP-IDF 5.3.1 also builds the WS147 target with
the Core text profile, but the existing project sdkconfig had script-task
runtime disabled; that build does not yet qualify the enabled device path.

This remains source-level migration work: the worker's Core text path is
available when the host catalog is bound, but installed-package lock/version
updates, enabled ESP32 script-task qualification, physical display acceptance,
and removal of legacy callback/font APIs remain pending. No PR has been opened.

## Cross-Repository Validation (2026-11-06)

Fresh Core Release builds with Canvas2D enabled and disabled both pass all three
registered CTest targets. In the Runtime source-override Release build, the
Core library, AppRuntime, desktop shell, JerryScript backend, script worker,
and all test executables compile after binding Win32 Canvas2D to the already
available `TextSystem`. Nine focused cross-repository tests pass, including
Core text, app-font catalog, desktop workflows, Canvas smoke pages and the
Script Task Runtime suite.

The full Runtime suite currently reports 88/91 passing. Three existing gates
need resolution before the paired PR is ready: the isolated Core-only build
boundary test configures Runtime's bundled Core snapshot, whose copied headers
refer to `text_system.h` but do not yet include the new Core-owned text sources;
the external-trial evidence test rejects the approved D-drive build path because
it requires build output under the repository; and legacy `jellyframe_script_tests`
segfaults at its first runtime test while the separate script-task suite passes;
that crash has not been attributed to this migration or reproduced on baseline.
The first failure shows that Runtime's clean-checkout Core supply path is not
yet synchronized; the second is a test/workspace path constraint; the third
remains an unresolved test failure. These results are from source
override and do not establish installed-package compatibility or device
acceptance.

The Core candidate is now versioned `0.8.0 / ABI 3`; Runtime's clean-checkout
lock still selects the prior Core. Produce and review the `0.8.0` source archive,
then update Runtime's exact version/ABI/source-hash lock and bootstrap path.
Resolve the in-tree Core-only build contract and rerun paired Core/Runtime CI
before opening the Runtime PR. The experiment branch remains a reference; do not
merge its history.
