# JellyFrame Render Core

> Last updated: 2026-09-04; Applies to: 0.6.2 release candidate

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

For Core ABI `1`, every installed header below `render_core/` is part of the
supported C++ integration surface. A host includes those installed paths and
links `JellyFrame::jellyframe_render_core`; it must not include Core files by
repository-relative `src/` paths or compile Core source files itself. This is
the boundary used by JellyFrame Runtime and verified by an independent package
consumer regression.

The package does not currently define a separate stable C ABI or a hidden
private-header tier. Before `1.0`, deliberate source API replacement is
allowed only with an explicit version/ABI decision and a package-consumer
regression update; it is not silently covered by compatibility aliases.

The source manifest is an integrity/provenance identity for the source files;
it is not a signature or release authority. Consumers should lock both package
version and engine ABI according to their own compatibility policy.

The JellyFrame Runtime repository owns App packages, JerryScript integration,
desktop tooling and device-layer documentation.
