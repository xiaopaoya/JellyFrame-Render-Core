# ABI11 contextual migration status

Last updated 2026-11-26. This file tracks the ABI11 contextual migration after
the signed Core 0.13.0 release and Runtime production-lock adoption. Merged PRs
and successful tests are evidence for their stated scope only.

## Merged work

- Core PR #94–#98 added bounded scalar text grouping for Flex/Grid, generated
  text under `display: contents`, generated pseudo-element display handling,
  anonymous flex/grid text flattening, and list-number counters. Each change is
  merged into Core `master`.
- Core PR #100 merged as `bea35c2f83f1fb36354a35d9900f32003e3b4125`. It adds a
  constrained contextual Flex row layout route and a JFSP2 contextual display
  list that preserves the existing `DisplayCommand` route. All PR and
  post-merge Core CI checks passed.
- Runtime PR #156 merged as
  `072bd773ea0bddcb80f91146428870e4486c6cad`. It adds an installed-worker
  regression for row geometry, shaped text, paint-list pixels, feature-off
  refusal, unsupported wrap/column topologies, and retained replay. All PR CI
  checks passed.
- Core PR #101 merged as
  `3f51df64f19603c09001c138c4025724a07c0989`. It adds bounded definite-size
  nowrap Flex columns and cross-owner UTF-8 cluster mapping coverage. Core
  full contextual and cropped contextual CTest passed 46/46 and 45/45.
- Runtime PR #157 merged as
  `d716a89e12759d178d3249754e8fb3b7fc3db826`. Its installed-worker test covers
  column box geometry, text baselines, pixel samples, auto-height refusal, and
  replay after teardown. Focused full/cropped/contextual-off consumers pass
  1/1 in each profile; all PR CI checks passed.
- Core PR #102 merged as
  `87509da0195275983a3e5281a2b07e2e09b4b77a` and corrected this document's
  references to merged work.
- Core PR #106 merged as
  `508992200070f38b394b1b9d258031eec7d2f844`; it propagates the HTML or BODY
  canvas background across the contextual viewport.
- Core PR #107 merged as
  `c9bf73df950abea43072e618f69b62c492fc1575` and records the #106 qualification
  and WS147 candidate state.
- Core PR #108 merged as
  `69b6e28673d1b00c8175c5f3431969b739fe0eaa`; it tests CSS line-height,
  Flex cross-axis centering, JFSP2 baselines, and final glyph pixels.
- Core PR #109 merged as
  `2ae16b2f190c54ccc0aa0803b0f53a2fd254c7f2`. Its merge did not include the
  contextual row-wrap implementation. Core PR #111 later merged bounded row
  wrapping as `79c1ed8c356acc34f8dc23b27feb2a0b7b0df3fc`; Core PR #112 then
  corrected this migration status as `ce93e5950318ca085c53dc165c4bf6c5ad7bb659`.
- WS147 production-trust V0 and V1 screen checks were observed successfully.
  They validate trust QA presentation and do not qualify contextual layout or
  JFSP2 paint-list output.
- Core PR #114 merged the 0.13.0 release candidate as
  `0861c451ff5cb81ea23e0b523b19f57aa0048f79`. Signed tag `v0.13.0` and the
  deterministic source archive are published. Runtime PR #159 adopted that
  package as the production lock, and both PR and post-merge CI passed 26/26.
- Core PR #116 merged as
  `28c061c6efe200e28f185206fe8828ce0cd1722b`. It adds bounded, opt-in decimal
  CSS counters for ordinary block-flow trees, including `::before` and
  `::after`, with explicit budget/topology refusal and style-reuse safeguards.
  Core full, counter-off and cropped tests passed 43/43, 42/42 and 42/42. The
  Runtime full/off/cropped installed-package qualification passed 41/41 for
  each profile against the same Core source identity. Runtime PR #161 updates
  the bilingual capability matrix and CSSWG support tables. This is an
  optional Core feature and does not change Runtime's production lock.

## Migration gates and workstreams

| Workstream | Current evidence | Required next evidence |
| --- | --- | --- |
| Shared inline ownership for shaped text | Core already maps shaping clusters to multiple UTF-8 source owners. Core PR #101 adds a regression for one cluster spanning two multibyte owners. Existing shared-inline spacing tests pass. | The shaped paragraph/source-range model is still not connected to shared-inline layout and paint. Implement this as a separate integration after an owner/cluster paint contract is designed. Preserve per-owner source ranges and refuse unsupported mixed-owner cases until geometry and pixel parity are proven. |
| Contextual Flex topology | Core #100 and Runtime #156 cover a definite-width nowrap row. Core #101 adds bounded definite-pixel columns. Core #111 adds bounded row wrapping with geometry, baseline, pixel, refusal, and replay coverage. This change adds per-line `justify-content` distribution for the same admitted wrapped-row topology and tests all six supported modes against exact Q1000 geometry and final pixels. Contextual Core CTest passes 46/46 and the default feature-off contextual build passes 43/43. Runtime full/cropped/off installed-package qualification passes 41/41 for each profile against candidate commit `84c74bf83da0cdef54b83a92752b170cb551f674`; full and cropped signed shaped routes and all desktop consumers pass, with the production lock unchanged. The route remains limited to admitted direct text items and fixed geometry; unsupported Flex combinations still refuse. Core 0.13.0 is signed and published, and Runtime #159 consumes it. | Complete the Core PR CI and merge. Broader topology and any device claim remain separate gates, with device claims limited to layouts exercised on that device. |
| Paired host qualification and release adoption | Runtime #158's exact installed-package qualification passed 41/41 in full, cropped, and disabled-feature profiles against the published Core 0.13.0 archive. Core standalone full/cropped/off suites passed 46/46, 45/45, and 42/42. Runtime PR #159 updated the production lock to Core 0.13.0 / ABI11 / source hash `290ff2ded3619d8be23e1320ad9530c07c81593cd5b94a8ff25faccb745e4dfd`; its 26 CI jobs passed, and post-merge master CI also passed 26/26. Signed `v0.13.0` points to Core master commit `0861c451ff5cb81ea23e0b523b19f57aa0048f79`; archive SHA-256 is `ec1773ad7d35b88f5e84903259433a6e5ceb922b6a391caec2cdcf2c453cf1f9`. | Complete. Future Core releases require their own package qualification and reviewed Runtime lock change. |
| WS147 contextual device acceptance | The latest reported device observation for Contextual Flex 0.1.4 showed the full background, centered/bottom-aligned A/V and left/bottom-aligned Chinese lines. The user noted that the browser alignment may differ. | Re-render the exact 0.1.4 app in a browser at the device viewport and compare the two alignment results before making a parity claim. Host rendering does not establish device parity. |

## Parallel execution

Core #111's bounded row-wrap implementation and Core #112/#113 migration status
corrections are merged. Runtime #158's installed-worker regression and
full/cropped/off package matrix are merged. Core `v0.13.0` is signed and
published, and Runtime #159 has adopted its exact source identity
`290ff2ded3619d8be23e1320ad9530c07c81593cd5b94a8ff25faccb745e4dfd` as the
production lock. PR and post-merge Runtime CI both passed 26/26 jobs.

The owner-map adapter remains experimental because it stores mapping data but
has no shaped-glyph consumer. Remaining work includes the separate
shared-inline layout/paint integration, Runtime qualification for the wrapped
row `justify-content` slice, and a browser comparison for the latest WS147
Contextual Flex app. The Core 0.13.0 release and Runtime production-lock
migration are complete.
Older ABI9 notes remain historical records unless explicitly updated with
newer evidence.
