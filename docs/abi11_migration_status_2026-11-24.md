# ABI11 contextual migration status

Last updated 2026-11-24. This file tracks the in-progress migration beyond the
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
- WS147 production-trust V0 and V1 screen checks were observed successfully.
  They validate trust QA presentation and do not qualify contextual layout or
  JFSP2 paint-list output.

## Remaining gates and workstreams

| Workstream | Current evidence | Required next evidence |
| --- | --- | --- |
| Shared inline ownership for shaped text | Core already maps shaping clusters to multiple UTF-8 source owners. Core PR #101 adds a regression for one cluster spanning two multibyte owners. Existing shared-inline spacing tests pass. | The shaped paragraph/source-range model is still not connected to shared-inline layout and paint. Implement this as a separate integration after an owner/cluster paint contract is designed. Preserve per-owner source ranges and refuse unsupported mixed-owner cases until geometry and pixel parity are proven. |
| Contextual Flex topology | Core #100 and Runtime #156 cover a definite-width nowrap row. Core #101 adds bounded column admission for definite pixel container/item sizes, direct text items, nowrap, zero grow, and no decorations or percentage sizing. Core full/cropped contextual CTest passed 46/46 and 45/45. Runtime #157 verifies the new column route in the installed Worker. | Wrap, shrink/grow, decorated items, auto/intrinsic/percentage sizing remain refused. Extend only with explicit geometry and pixel oracles. |
| Paired host qualification | With Core 0.12.1 / ABI11 source hash `38d9678a581959a8f48deabce28524aa2f867d144648c086f5917760d0088a66` and the Runtime #157 tree, the full, cropped, and contextual-off installed consumer suites each passed 41/41 with zero skips. The qualification lock was separate from production, and package fallback was disabled. | Add any future contextual capability under the same candidate-package matrix before considering production adoption. |
| WS147 contextual device path| An isolated ESP-IDF 5.3.1 installed-worker image builds and links with Core 0.12.1 / ABI11 features `css.flex-grid`, `text.contextual-jfsp2`, and `text.contextual-layout`. It uses a 3 MiB storage partition, 1024 KiB configured bundle cap, and 1,044,480-byte physical slot. BIN SHA-256 starts `f66580b3`; ELF SHA-256 starts `8050c031`. The 8 MiB app partition retains 3,821,600 bytes. Runtime tree matches official PR #157 merge tree `b1864c24ace54306edc2f8f86d9c3e9a3d3d4bef`.| The first 811,676-byte JFAPP candidate is not valid acceptance evidence. Its `catalog.jfcat` is only six bytes (`broken`), and package preflight checks resource presence without proving Core can bind it. The generated full regular catalog is about 733,641 bytes by itself. Both the candidate and valid full catalog exceed the current 327,680-byte device package cap. No JFSG was produced, and no image was flashed. Next choose between a factory-image flash with the 3 MiB storage layout, or keeping the existing partition and implementing a supported compact shaping-resource package. The existing encrypted signing key must also be unlocked locally before producing a signed app.|
| Release adoption | The production lock remains on the previously reviewed Core package. Contextual layout remains opt-in. | Review final Core API, source manifest, device observation, and resource evidence before proposing a production lock update or release. |

## Parallel execution

The bounded Flex column change and Runtime Worker regression are merged. The
paired full/cropped/OFF host qualification passes. The next open gates are the
board observation for the exact built firmware and the larger shaped-inline
owner/cluster integration. The production lock remains unchanged. This checklist supersedes older ABI9 notes
that still describe the pre-ABI11 migration state; those files remain historical
records unless explicitly updated with newer evidence.
