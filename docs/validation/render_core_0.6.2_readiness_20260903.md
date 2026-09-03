# Render Core 0.6.2 Upgrade Readiness

Date: 2026-09-03  
Candidate commit: `5721ba04b24146720e8ec0e9d8f8ae7633d2b42a`  
Candidate line: `0.6.2-dev`  
Engine ABI: `1`

This report records upgrade evidence. It is not a release approval and does
not authorize changing the JellyFrame Runtime dependency lock.

## Identity

- Source hash: `41c05d13a2aebb480f020b272531d8419ecb4d87c6bb1117996e7e7bd237378f`
- Source manifest file count: `104`
- Deterministic development archive:
  `jellyframe-render-core-0.6.2-dev.tar.gz`
- Archive SHA-256:
  `e8bdf0fcee04ca2baf4567fbe7062cd55b9fef7085e0ad4be726a850c260252b`

The archive was created twice from the committed checkout. Both byte hashes
were identical, and the sidecar checksum matched the archive.

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

The package version fix in this candidate keeps `0.6.2-dev` in Core metadata
but emits numeric CMake package version `0.6.2`, so exact downstream package
discovery works for a future stable release.

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
