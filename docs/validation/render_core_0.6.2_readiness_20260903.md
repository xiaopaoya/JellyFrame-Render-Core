# Render Core 0.6.2 Upgrade Readiness

Date: 2026-09-04
Candidate branch: `codex/core-0.6.2-release-prep`
Candidate line: `0.6.2` release candidate
Engine ABI: `1`

This report records upgrade evidence. It is not a release approval and does
not authorize changing the JellyFrame Runtime dependency lock.

## Identity

- Source hash: `539a894519d3251f02c8b3aee8d0d0fb715bf49a732fc74126ccb2188462e3f0`
- Source manifest file count: `104`
- Deterministic development archive:
  `jellyframe-render-core-0.6.2.tar.gz`

The archive must be created from the final committed candidate checkout. The
generator writes a matching `.sha256` sidecar; CI creates it twice and checks
that both archive bytes and sidecars are identical. The exact checksum belongs
to that immutable artifact, not to a file included inside the archive.

## Completed Evidence

1. Standalone default Release configure, build and CTest passed.
2. Standalone minimal profile configure and build passed with Canvas2D,
   modern paint, flex/grid and advanced forms disabled.
3. Standalone benchmark configure, build and execution passed. The run covered
   parsing, style/layout, layers, full pipeline, rounded raster, gradients,
   shadows, text, dirty replay, animation and Canvas2D paths.
4. Source archive lifecycle test passed all three tests: deterministic
   packing, line-ending stability, untracked-input isolation, extraction,
   standalone builds, CTest and installation.
5. A package-only downstream CMake consumer found `JellyFrameRenderCore 0.6.2
   EXACT`, compiled against installed headers and library, and ran successfully.
6. A temporary Runtime checkout consumed the candidate installed package with
   an exact temporary lock. Runtime provenance reported matching version,
   ABI, source hash and source file count; App Runtime tests passed.
7. The same temporary Runtime checkout consumed the candidate through the
   local source override. Provenance and App Runtime tests passed.

The package version fix emits the numeric CMake package version `0.6.2`, so
exact downstream package discovery works for the stable release candidate.

## Remaining Release Gates

- Review and approve the candidate capability and public-header changes.
- Complete any required target captures and performance evidence for accepted
  capabilities.
- Create the reviewed stable `0.6.2` release checkout and signed annotated tag.
- Create and publish the stable source archive and SHA-256 sidecar from that
  reviewed tag.
- Repeat Runtime installed-package and source-override tests against the
  stable release artifact, then open a separate Runtime dependency-lock PR.
- Record exact Runtime/Core provenance in the Device OS image before claiming
  the new Core on hardware.

Until these gates are complete, Runtime remains locked to Core `0.6.1`.
