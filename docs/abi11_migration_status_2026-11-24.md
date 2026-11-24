# ABI11 contextual migration status

Last updated 2026-11-25. This file tracks the in-progress migration beyond the
signed Core 0.12.1 / ABI11 production baseline. Merged PRs and successful tests
are evidence for their stated scope only.

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
  contextual row-wrap implementation. The implementation is now proposed in
  Core PR #111 and awaits CI and review.
- WS147 production-trust V0 and V1 screen checks were observed successfully.
  They validate trust QA presentation and do not qualify contextual layout or
  JFSP2 paint-list output.

## Remaining gates and workstreams

| Workstream | Current evidence | Required next evidence |
| --- | --- | --- |
| Shared inline ownership for shaped text | Core already maps shaping clusters to multiple UTF-8 source owners. Core PR #101 adds a regression for one cluster spanning two multibyte owners. Existing shared-inline spacing tests pass. | The shaped paragraph/source-range model is still not connected to shared-inline layout and paint. Implement this as a separate integration after an owner/cluster paint contract is designed. Preserve per-owner source ranges and refuse unsupported mixed-owner cases until geometry and pixel parity are proven. |
| Contextual Flex topology | Core #100 and Runtime #156 cover a definite-width nowrap row. Core #101 adds bounded column admission for definite pixel container/item sizes, direct text items, nowrap, zero grow, and no decorations or percentage sizing. Core full/cropped contextual CTest passed 46/46 and 45/45. Runtime #157 verifies the new column route in the installed Worker. | Wrap, shrink/grow, decorated items, auto/intrinsic/percentage sizing remain refused. Extend only with explicit geometry and pixel oracles. |
| Paired host qualification | Requalified after Core PR #106 using Core 0.12.1 / ABI11 source hash `4b1a9ce21fc1d1c790b1c654f4e83677d9e5e0a137afdf15856c1cdd7e01dfaa` from merge commit `508992200070f38b394b1b9d258031eec7d2f844` and Runtime HEAD `18ebb332e8687026f24d52ec9997317596444f22`. Exact full, cropped, and contextual-OFF installed-consumer profiles each passed 41/41 with zero skips or failures. The qualification used an isolated lock with hash `b29460a2c2207d4b4648d6f84018f4d1023cabd9906127a324164c5be9e13c2a`; Core source fallback was disabled, and the production lock and Runtime inputs were unchanged. | Retain the isolated evidence and repeat this matrix if either source identity or installed-package profile changes. The earlier 41/41 result at `38d9678a…` remains evidence for that older Core source only. |
| WS147 contextual device path | The 3 MiB/811,676-byte candidate was invalid because `catalog.jfcat` contained only six bytes (`broken`). The corrected full-resource candidate uses the real 733,641-byte `catalog.jfcat1`, companion, complete font, and Unicode/eligibility tables. A 5 MiB storage layout gives a 1,744,896-byte package slot. Image `0.6.2-abi11-contextual-column5m` is on COM19 and verified by factory-image readback. It advertises ABI11 contextual JFSP2/layout, a 1,744,896-byte slot, and 5,234,688 bytes available. Candidate 1 failed contextual layout admission due to unsupported `flex-flow` shorthand and out-of-profile dimensions. Candidate 2 version 0.1.1 then launched and rendered with `presents_failed=0`. The user later reported that the lower blank viewport stayed white and A/V sat low, while the browser showed centered text. Core PR #106 (`Propagate contextual canvas background to viewport`) merged as `508992200070f38b394b1b9d258031eec7d2f844` after all CI checks passed. It propagates the HTML root background, or body background when the root is transparent, across the contextual viewport, and tests canvas precedence and paint order. Local full contextual CTest passes 46/46 and Flex/Grid-off cropped passes 45/45. Runtime native contextual desktop and installed-bundle consumer tests pass 1/1 each with the patched Core source. Test firmware `0.6.2-abi11-contextual-column5m-bg2` was rebuilt from the merged Core source identity `4b1a9ce2…7e01dfaa`, BIN SHA-256 `6a3b292f…5d9990c`, and is packaged with a verified 16 MiB factory image. App candidate 0.1.2 uses `line-height` matching each card height to center Chinese and A/V text. A real Edge headless render at 172×320 shows the Chinese glyphs and A/V text centered in their cards. The latest board report still finds a white empty viewport and bottom-aligned A/V text, but bg2/0.1.2 has not been installed, so device parity remains unverified. Runtime package check reports zero layout diagnostics; rendered bottom and empty-viewport pixels are dark. JFAPP is 1,595,968 bytes, below the 1,744,896-byte slot. Its signed JFSG still requires a local PEM unlock. The device remains on 0.1.1 pending signing and visual retest. Production Runtime lock is unchanged.|
| Release adoption | The production lock remains on the previously reviewed Core package. Contextual layout remains opt-in. | Review final Core API, source manifest, device observation, and resource evidence before proposing a production lock update or release. |

## Parallel execution

Core #109 did not contain the row-wrap implementation. Core PR #111 now carries
the actual Core changes and awaits CI. Runtime PR #158 carries a candidate-only
qualification matrix pinned to Core #111's exact commit. The local package
matrix passed, but the first GitHub run failed before testing because the
qualification script required evidence outside the Runtime checkout. That path
was corrected and a new run is in progress.

The owner-map adapter remains experimental because it stores mapping data but
has no shaped-glyph consumer. Remaining gates are Core #111 review/CI, Runtime
#158 candidate CI, official Core package/release planning, production lock
review, and WS147 firmware/app visual verification. The production Runtime lock
remains unchanged. Older ABI9 notes remain historical records unless explicitly
updated with newer evidence.
