# C5 cumulative intrinsic-query allowance

Local implementation over `cb139014` in `codex/c5-flex-grid-consumer`.
The initial implementation commit is `fbc34a35`; the following scoped change
adds the exact Grid scratch buffer and installed/public-entry regressions.
Production source identity is
`ee390f203e9d6a3bb52f6f7c2a539a747f47b5b8893597cbc41f4c51dc154cc7`
with 145 source-manifest inputs. This is Core 0.9 development / ABI4 evidence.

## Contract and ownership

The previous nowrap Flex and Grid consumers each created fresh aggregate query
counters. A document with nested or sibling formatting contexts could therefore
multiply the added source-query work allowance during one layout pass.

`build_with_arena` now owns one `IntrinsicSourceQueryLedger` on its stack and
uses a stack-local copy of LayoutEngine with a borrowed pointer to that ledger.
All recursive layout, sizing probes, retries and positioned layout through that
worker share the same counters. Work is not refunded when a speculative result
is abandoned. Public Node, RenderObject and arena entry points converge on this
single lifetime. A separate/reentrant public call creates its own ledger; the
original engine has no mutable pass state and may be reused for the next pass.

`LayoutEngineOptions::intrinsic_query_limits` controls the added work.
`intrinsic_query_statistics` is optional borrowed synchronous output, replaced
when a public layout call completes. Cancellation is a borrowed atomic flag.
These additions require rebuilding a consumer against the matching development
package; the installed consumer regression does so. No Node, Style or LayoutBox
field, per-node allocation, process global or thread-local state was added.

| Allowance | Default scope and value |
| --- | --- |
| Boxes visited | 256 cumulative source-query visits per pass |
| Raw source bytes | 8192 cumulative per pass |
| Measurements | 512 cumulative per pass |
| Work | 65536 cumulative source-query units plus explicit planner charges |
| Depth | 32 per query; independent traversal/ancestor checks remain |
| Scratch | 32768 simultaneously reserved query/planner payload bytes |

Each query receives the smaller of the pass remainder and the standalone
query's existing default allowance. Raising a pass allowance does not raise an
individual query's safety limit. Width and height share the same ledger through
`query_width`/`query_height`; callers can also charge explicit planning work.
Zero allowance declines added query work while Core Text and legacy layout
remain usable.

The ledger bounds this added query work. It does not bound all layout scans,
legacy measurement, allocation, rendering, or the total complexity of a pass.
Existing layout-box/depth limits and legacy behavior remain separate controls.

## Atomic refusal and scratch

Nowrap Flex stages every measured auto minimum before applying any of them.
If a source query refuses, all staged auto minima for that consumer are ignored
and it performs its legacy allocation, retaining declared minimum handling.
Grid likewise stages source widths before changing track widths; refusal uses
legacy intrinsic estimates for the whole consumer. A previously completed
independent consumer remains valid; there is no whole-document rollback.

Grid's staging buffer is one `int[]` allocation of exactly the placement count,
reserved before allocation. Allocation failure takes the same consumer fallback.
The reservation remains live until track finalization ends. Query results now
report their stream payload peak, including overlapping old/new buffers; ledger
statistics add it to simultaneously live planner reservations. Reservations are
released for reuse, while work/visits/source/measurement counters are monotonic.
Allocator bookkeeping, borrowed font/input storage, and existing layout vectors
are outside this payload limit. The scratch peak is a conservative reservation
measurement (including attempted allocations), not a process heap watermark.

## Regressions and host measurements

- Nested fixed-track Grid sections contain both nowrap Flex and auto-track Grid
  consumers. A 180-unit pass allowance consumes exactly 180 units across 50 query
  attempts, with 49 resource refusals. Earlier completed Flex min-content remains
  18px; a later exhausted consumer uses its 0px legacy basis. A larger allowance
  restores the later 18px minimum.
- Reusing the same engine, the arena overload, and the public RenderObject
  overload reproduce the same bounded work and geometry with fresh allowances.
- Width followed by height shares one exact work threshold; one-below work,
  cancellation and nested scratch reservations decline without dimensions.
- A second-item Flex refusal discards the first staged minimum and reproduces
  the complete legacy subtree rectangles. A two-auto-track Grid fixture asserts
  that both source queries execute, then verifies late refusal and whole-consumer
  legacy geometry.
- The Grid fixture needs 8 bytes of staging plus 5 bytes of query stream. Its
  13-byte allowance reproduces normal geometry and reports a 13-byte peak;
  12 bytes refuses atomically within its allowance.
- Existing automatic-minimum redistribution, Q1000 percentage bases, source
  refusal, aggregate-budget fallback and ordinary geometry regressions remain.
- The installed full consumer exercises zero-work refusal and replacement of
  statistics/normal geometry on the next call using only installed public APIs.

Windows x64 Clang 22.1.8 reports `sizeof(IntrinsicSourceQueryLedger)=120`,
`sizeof(LayoutEngine)=168`, and `sizeof(LayoutEngineOptions)=104` bytes.
The pass owns one ledger plus one engine copy (288 bytes of C++ object storage,
separate from compiler frame overhead). The engine grows by the 64-byte options
extension and one 8-byte pointer; there is no per-node growth. Query traversal
and stream safety checks remain; no target stack-watermark claim is made.

## Validation

| Gate | Build under E:/Builds | Result |
| --- | --- | ---: |
| Full Release | c5-flex-grid-consumer-release | 24/24 |
| Cropped Release, optional four feature families OFF | c5-intrinsic-pass-ledger-cropped | 23/23 |
| UBSan trap, source width/height, Flex and Block geometry | c5-intrinsic-pass-ledger-ubsan | 6/6 |
| Installed full package consumer | c5-intrinsic-pass-ledger-package-full | 1/1 |
| Installed cropped package consumer | c5-intrinsic-pass-ledger-package-cropped | 1/1 |

All builds use Ninja and Clang 22.1.8. UBSan sets both compile and executable
link flags to `-fsanitize=undefined -fsanitize-trap=all`; its six tests match
`(intrinsic_source_query|flex_auto_min|flex_basis_percentage|computed_flex_layout|computed_block_layout)`.
Install prefixes are `E:/Builds/c5-intrinsic-pass-ledger-install-{full,cropped}`.
Both packages enforce the source identity above and their expected profile;
the profiles are `render-core-default` and `render-core-minimal-no-forms-advanced`.
Build CMake caches, generated manifests and CTest logs retain the exact commands
and results. `git diff --check` passes.

MCU timing, heap/stack watermarks, allocator overhead and whole-frame latency
remain unmeasured. There is no device, Runtime manifest/lock, push, merge, or
release qualification in this change. Wrapped-row work is owned by a parallel
branch and must join this same ledger before its own combined qualification.
