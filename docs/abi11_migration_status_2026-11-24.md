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
- Core PR #117 merged as
  `abe401125bc94a53843403c5896d9a4b7011ddf8`. It adds all six `justify-content`
  distributions to the existing fixed-width contextual wrapped-row subset.
  Contextual CTest passed 46/46, the default build passed 43/43, and the Core
  PR plus post-merge CI passed. Runtime full/off/cropped installed-package
  qualification passed 41/41 in each profile against the candidate, with the
  production lock unchanged.

- Core PRs #119 and #120 added the JFSP2 source-owner mapper and glyph
  geometry based on the contextual painter. Core PR #121 added opt-in
  capture of glyph bounds and original UTF-8 byte spans for the existing
  direct-text topology. Local Core suites passed 47/47 with contextual
  layout enabled, 45/45 with layout disabled, and 41/41 with contextual
  text disabled. PR CI and post-merge CI passed for #121.
- Core PR #122 corrected contextual line height and baselines to use positive
  font descent. PR and post-merge CI passed. A desktop capture of the exact
  Contextual Flex 0.1.4 page at 172 × 320 and an Edge capture with the same
  Noto Sans SC source font put A/V ink on rows 161–172. The previous Core
  put it on rows 166–177. The four Flex item bounds agreed and Chinese glyph
  pixel bounds still differed.

## Migration gates and workstreams

| Workstream | Current evidence | Required next evidence |
| --- | --- | --- |
| Shared inline ownership for shaped text | Core #119–#121 cover source-owner mapping, glyph bounds and opt-in capture for direct text, including wrapping, original byte ranges and rollback. The installed Worker currently uses the paint-only capture path. | Admit runs spanning multiple inline owners with uniform geometry and paint. Verify a shared cluster paints once and retains all contributing source spans. Then connect the capture to Worker input and qualify full/cropped/off packages. |
| Contextual Flex topology | Core #100 and Runtime #156 cover a definite-width nowrap row. Core #101 adds bounded definite-pixel columns. Core #111 adds bounded row wrapping with geometry, baseline, pixel, refusal, and replay coverage. Core #117 adds per-line `justify-content` distribution for the same admitted wrapped-row topology and tests all six supported modes against exact Q1000 geometry and final pixels. Contextual Core CTest passed 46/46 and the default build passed 43/43. Runtime full/off/cropped installed-package qualification passed 41/41 for each profile against Core commit `8e4ac459ca7398dc0a786cb297e7e46f28f172eb`, with full/cropped signed shaped routes and desktop consumers passing and the production lock unchanged. Core PR #117 and post-merge CI passed. The route remains limited to admitted direct text items and fixed geometry; unsupported Flex combinations still refuse. | Broader topology and any device claim remain separate gates, with device claims limited to layouts exercised on that device. |
| Paired host qualification and release adoption | Runtime #158's exact installed-package qualification passed 41/41 in full, cropped, and disabled-feature profiles against the published Core 0.13.0 archive. Core standalone full/cropped/off suites passed 46/46, 45/45, and 42/42. Runtime PR #159 updated the production lock to Core 0.13.0 / ABI11 / source hash `290ff2ded3619d8be23e1320ad9530c07c81593cd5b94a8ff25faccb745e4dfd`; its 26 CI jobs passed, and post-merge master CI also passed 26/26. Signed `v0.13.0` points to Core master commit `0861c451ff5cb81ea23e0b523b19f57aa0048f79`; archive SHA-256 is `ec1773ad7d35b88f5e84903259433a6e5ceb922b6a391caec2cdcf2c453cf1f9`. | Complete. Future Core releases require their own package qualification and reviewed Runtime lock change. |
| WS147 contextual device acceptance | Contextual Flex 0.1.4 has a full background on the device. The Core #122 baseline correction agrees with Edge for A/V in the matched desktop comparison. The WS147 candidate is `0.6.2-abi11-contextual-baseline1` (Core source SHA-256 prefix `78e1559d`, app BIN SHA-256 prefix `160ac23d`). Its partition table matches the existing 5 MiB storage build. | Run the original signed 0.1.4 package on the candidate and verify the background and text alignment on screen. |

## Parallel execution

Core #111's bounded row-wrap implementation and Core #112/#113 migration status
corrections are merged. Runtime #158's installed-worker regression and
full/cropped/off package matrix are merged. Core `v0.13.0` is signed and
published, and Runtime #159 has adopted its exact source identity
`290ff2ded3619d8be23e1320ad9530c07c81593cd5b94a8ff25faccb745e4dfd` as the
production lock. PR and post-merge Runtime CI both passed 26/26 jobs.

Core #121 retains direct-text hit fragments in an opt-in capture. Admission
for multiple inline owners and Worker pointer dispatch remain open. The
desktop baseline comparison and the WS147 candidate build are complete.
Device observation of that candidate is pending.

Older ABI9 notes remain historical records unless explicitly updated with
newer evidence.
