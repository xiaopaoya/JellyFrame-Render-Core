# Bounded wrapped-row delivery slice

This slice starts at Core master `69c554fc` and replays only `9f534225` as
`c41fc031`. It reuses the already-delivered intrinsic query/whole-pass ledger
and precise solver. No mixed CSS dimension admission or overflow/hit feature
is included. The final production source hash is
`f7c17009b7660a84ef6a492367637187dff6915c4af3418c769902b3b7eb2559` / 145,
package0.9.0-dev / ABI4.

## Behavior and bounds

Admitted shapes are ordinary horizontal row/wrap with automatic container
height, start line packing/justification and static empty Blocks or one direct
Core Text run per item. The planner computes line breaks and per-line precise
grow/shrink/min/max sizing before publishing. Unsupported source, numeric
publication mismatch, cancellation, allocation or resource refusal uses whole-
container fallback. A counted raw-storage mutation journal restores geometry,
precise geometry and text caches after late refusal.

The existing shared ledger accounts for planner scans, ordering, solver work,
queries and journal capture. Exact item/line/traversal/journal payload shares
the simultaneous scratch allowance. Limits remain256 source boxes/depth32,
8192 source bytes,512 measurements,65536 work and32768 scratch payload bytes;
planner admission additionally caps128 items. These do not bound legacy fallback
work, allocator metadata, persistent caches, whole-frame latency or device RAM.
Selected fractional text inline widths refuse; this is not general wrapped Flex.

## Fresh qualification

Windows x64 Clang22.1.8/Ninja with assertions enabled:

| Gate | Result |
| --- | --- |
| Full Release Core | 25/25 |
| Cropped Release Core | 24/24 |
| Full ASan+UBSan Core | 25/25 |
| Five installed consumers per full/cropped profile | 10/10 total |

Installed consumers cover bounded nowrap layout, wrapped-row layout, event
completion/failure and Submit kind with `-fno-rtti`. Each verifies exact package
source and profile. Full is `render-core-default`; cropped is
`render-core-minimal-no-forms-advanced`, disables Flex/Grid and qualifies fallback
and shared behavior only. New CI adds the wrapped consumer to both package jobs.

ASan+UBSan instruments the Core C++ library and23 C++ test binaries; two source
manifest Python checks run normally. Leak detection is disabled. Tests include
exact/one-below line thresholds, per-line grow/shrink, ordering/decorations/cross
alignment, aggregate/exact work and scratch ceilings, allocation refusal, late
cancellation and journal rollback. Existing tests are reused.

Builds: `E:/Builds/core-c5-wrapped-delivery-*`; runner, configure/build/CTest,
install logs and source/profile copies:
`E:/Experiments/core-c5-wrapped-delivery-20261111`.

The original `c5_wrapped_row_flex_checkpoint_20261111.md` retains historical
browser observations and static Xtensa frames for its original source. They are
not relabeled as fresh evidence for this package. New target link/resource,
whole-call-chain/device stack, timing, UI and broader browser parity remain open.
Remote CI must pass before mainline merge.
