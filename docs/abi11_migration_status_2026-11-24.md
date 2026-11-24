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
- WS147 production-trust V0 and V1 screen checks were observed successfully.
  They validate trust QA presentation and do not qualify contextual layout or
  JFSP2 paint-list output.

## Remaining gates and workstreams

| Workstream | Current evidence | Required next evidence |
| --- | --- | --- |
| Shared inline ownership for shaped text | Core already maps shaping clusters to multiple UTF-8 source owners. Core PR #101 adds a regression for one cluster spanning two multibyte owners. Existing shared-inline spacing tests pass. | The shaped paragraph/source-range model is still not connected to shared-inline layout and paint. Implement this as a separate integration after an owner/cluster paint contract is designed. Preserve per-owner source ranges and refuse unsupported mixed-owner cases until geometry and pixel parity are proven. |
| Contextual Flex topology | Core #100 and Runtime #156 cover a definite-width nowrap row. Core PR #101 adds bounded column admission for definite pixel container/item sizes, direct text items, nowrap, zero grow, and no decorations or percentage sizing. Full Release CTest passed 46/46; cropped profile passed 45/45. | Wrap, shrink/grow, decorated items, auto/intrinsic/percentage sizing remain refused. Extend only with explicit geometry and pixel oracles. |
| Paired host qualification | Full/cropped/contextual-off installed consumer suites passed 41/41 per profile against a candidate source package. That run used Runtime `c288dfa23e3997e284d9fa19e750a7a1a4d9c204`, before Runtime #156. | Rerun the full/cropped/OFF paired qualification using merged Runtime #156 and a source manifest generated from the reviewed Core revision. Reconcile candidate and production-lock identities without changing the production lock during qualification. |
| WS147 contextual device path | Runtime #156 adds host Worker regression coverage. It does not compile Xtensa, exercise device package installation/reboot, measure device resource use, or show the contextual result on screen. | Build an isolated ESP-IDF 5.3.1 qualification image using `sdkconfig.ws147_installed_worker_acceptance.defaults`. Verify the generated ABI11 profile advertises `css.flex-grid`, `text.contextual-jfsp2`, and `text.contextual-layout`. Preserve source/package identity, ELF, BIN, map, and manifest, then test install, restart, rendering, and touch behavior on the board. |
| Release adoption | The production lock remains on the previously reviewed Core package. Contextual layout remains opt-in. | Review the final Core API, source manifest, resource costs, and paired Runtime/device evidence before proposing a production lock update or release. |

## Parallel execution

The shaped-inline source-map regression and bounded Flex column change are
integrated on the current Core-based migration branch. Runtime/WS147
qualification established the remaining device gate. The Core branch still
needs review, paired host qualification, and device qualification before release
adoption. This checklist supersedes older ABI9 notes
that still describe the pre-ABI11 migration state; those files remain historical
records unless explicitly updated with newer evidence.
