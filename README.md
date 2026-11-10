# JellyFrame Render Core

> Last updated: 2026-11-10; Applies to: 0.9.0 development line

JellyFrame Render Core is a platform-neutral, modular HTML/CSS subset,
document/layout pipeline and CPU software renderer for bounded embedded and
desktop hosts. It does not include the JellyFrame App Runtime, JerryScript,
ports, launchers, device protocols or vendor SDKs.

## Build

With CMake 3.20+ and Ninja available, presets make the default or smallest
supported profile one command away:

```sh
cmake --preset default
cmake --build --preset default

cmake --preset minimal
cmake --build --preset minimal
```

The following explicit commands remain compatible with the minimum required
CMake version and are useful when a host selects its own generator:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build --prefix install
```

Performance work can use the opt-in benchmark preset:

```sh
cmake --preset benchmarks
cmake --build --preset benchmarks
```

On Windows this also builds `jellyframe_cpu2d_compare`, which emits fixed-size
Core and GDI+ workload captures plus machine-readable timing summaries.

The install exports `JellyFrame::jellyframe_render_core`, public headers, a
feature profile and a deterministic source manifest. Optional build families
are selected with `JELLYFRAME_ENABLE_CANVAS2D`,
`JELLYFRAME_ENABLE_MODERN_PAINT`, `JELLYFRAME_ENABLE_FLEX_GRID` and
`JELLYFRAME_ENABLE_ADVANCED_FORMS`.

The repository intentionally contains no App Runtime, JerryScript integration,
device ports or launcher implementation. Those belong to the Runtime and
Device OS layers. The `benchmarks/`, `tests/unit/`, `docs/` and `samples/`
directories contain platform-neutral Core maintenance material.

## C++ Integration Surface

For Core ABI `4`, every installed header below `render_core/` is part of the
supported C++ integration surface. A host includes those installed paths and
links `JellyFrame::jellyframe_render_core`; it must not include Core files by
repository-relative `src/` paths or compile Core source files itself. This is
the boundary used by JellyFrame Runtime and verified by an independent package
consumer regression.

The package does not currently define a separate stable C ABI or a hidden
private-header tier. Before `1.0`, deliberate source API replacement is
allowed only with an explicit version/ABI decision and a package-consumer
regression update; it is not silently covered by compatibility aliases.

The unpublished 0.9.0 / ABI4 line replaces flat `DisplayCommand` paint fields
with active typed payloads. Select a command kind with the constructor or
`set_type()`, inspect it with `type()`, and use `basic_paint()`, `text_paint()`
or `image_paint()` for that kind. A changed kind resets its payload. Selecting
the same kind preserves it. A mismatched accessor throws `std::logic_error`.
Prepared text stores its explicit origin and baseline in the text payload.
The public installed-consumer regression covers the source API replacement.
Hosts qualify and pin the new source manifest before adopting this candidate.

The same ABI4 candidate replaces image resolver output with
`ResolvedImageResource` (handle plus Unknown/Known/Invalid natural-size metadata)
and image paint arguments with `ImagePaintRequest`. Known background dimensions
must be positive and at most 8191 per axis; invalid background metadata omits
that image. Content images retain their existing handle-based paint behavior.
The resolver output begins cleared and failed resolution or a zero handle never
publishes an image command. Existing URL sizing and object-fit behavior remain.

Image and gradient payloads carry an optional independent `background_paint_clip`.
All-zero metadata means absent; otherwise both extents must be positive. The
relative clip and radius bound painting while the command rectangle retains the
complete sampling tile. `flatten_with_clip_metadata()` preserves that geometry;
consumers apply indexed external clips separately. Paint requests use coordinates
of the actual target, including temporary surfaces. Core enforces the painting
clip even for a painter that writes beyond it.

Image callbacks, their contexts, and provider resource bytes are borrowed. The
host keeps them alive through the final paint of every display list that uses
the handles. A callback return value does not transfer provider ownership to
Core. The installed `image_callback_consumer` regression exercises this public
contract with full and cropped feature profiles.




The source manifest is an integrity/provenance identity for the source files;
it is not a signature or release authority. Consumers should lock both package
version and engine ABI according to their own compatibility policy.

The JellyFrame Runtime repository owns App packages, JerryScript integration,
desktop tooling and device-layer documentation.
