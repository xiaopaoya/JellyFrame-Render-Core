# C5 bounded nowrap Flex/Grid delivery slice

This independent slice starts at Core master
`b951751a23901e6e89560c5954b6b143be48db8c`. The four selected commits apply
without conflict and without additional source dependencies. The branch remains
on that base and is not rebased onto the separate R3 delivery.

| Original commit | Replayed commit | Change |
| --- | --- | --- |
| 3a4d8c7e | 14eb3d65 | Bounded source-query consumers for nowrap Flex auto minima and Grid auto tracks |
| 5a62d4a7 | 93762514 | Checked Flex solver can refuse before each scan |
| a2235354 | bc7a5258 | One cumulative source-query ledger per public layout call |
| 9e8f0100 | 6613b4ef | Exact Grid staging payload reservation and pass-budget regressions |

No R3 completion/failure/Submit, wrapped-row planner, overflow/hit-target or mixed
dimension implementation is included. Source-query width/height foundations,
ordinary precise Flex/Block sizing and inherited-style initialization already
exist in this master base and are reused.

## Contract

The consumers use the existing bounded, complete-only source width query through
Core TextSystem. Nowrap horizontal Flex stages content automatic minima before
publication; admitted Grid auto tracks stage max-content widths for ordinary,
unconstrained Block roots. Unsupported source, cancellation, exhausted allowance
or allocation refusal discards that consumer's staged source contribution and
uses its legacy allocation. Declared minima remain effective. Earlier completed
independent consumers are retained; this is not whole-document rollback.

One stack-owned ledger and a synchronous engine copy are shared by recursion,
probes, retries and positioned layout during each public layout call. Width and
height queries debit the same ledger. Work already done is not refunded after
speculation. Independent public calls start fresh ledgers; the original engine
remains reusable. Borrowed statistics output is replaced on normal completion.
Consumers must rebuild for the extended LayoutEngineOptions/LayoutEngine ABI4
development surface.

Defaults bound added source-query work: 256 visited boxes, depth 32, 8192 source
bytes, 512 measurements, 65536 work units and 32768 simultaneous query/planner
payload bytes. Individual queries retain their own safety ceilings. Scratch is a
live payload reservation, including exact Grid staging, not total heap usage.
These limits do not bound all legacy layout scans, allocation, painting or frame
latency. Wrapped rows, general Flex/Grid sizing, mixed calc storage/admission and
unmodeled intrinsic sources remain outside this slice.

## Delivery gates

The existing installed `c5_flex_grid_consumer` is reused. Its full-profile tests
exercise Core-font min-content/automatic minima, declared percentage minima,
Grid max-content, unsupported calc refusal, zero-work fallback and next-call
statistics/normal geometry. A small cropped public-API extension now checks
ordinary Block text layout under zero query allowance and replacement of stale
statistics. Cropped disables Flex/Grid and does not qualify those consumers.

The existing full/cropped package CI matrix now configures, builds and runs this
consumer against its install prefix. It passes `EXPECTED_SOURCE_HASH` and
`EXPECTED_PROFILE` from generated manifest/profile identities. The consumer
verifies the package exports, ABI4, installed manifest hash and source-input
count. The full standalone CTest job remains in place.

## Fresh qualification of this source

- Package: `0.9.0-dev`, engine ABI `4`.
- Source hash: `6de5a85154a29a316276370a6a024e0f7121edd57abe7b61cb2c2525e0355d62`.
- Source inputs: `145`.
- Full profile: `render-core-default`.
- Cropped profile: `render-core-minimal-no-forms-advanced`.

Fresh Windows x64 Clang 22.1.8 / Ninja / Release builds:

| Gate | Result |
| --- | --- |
| Full CTest | 24/24, 11.21 seconds |
| Cropped CTest | 23/23, 11.27 seconds |
| Exact installed full-profile public consumer | 1/1 |
| Exact installed cropped-profile public consumer | 1/1 |

The existing source-query, solver cancellation, exact/one-below scratch,
aggregate ledger and atomic fallback regressions execute in these fresh suites;
no duplicate implementation or replacement rollback-test machinery was added.
Both generated and installed package identities agree with the hash above.
`git diff --check` passes. Builds use
`E:/Builds/core-c5-bounded-delivery-{full,cropped}-20261111`; installation prefixes
are `E:/Builds/core-c5-bounded-delivery-install-{full,cropped}-20261111` and the
independent consumer builds are
`E:/Builds/core-c5-bounded-delivery-consumer-{full,cropped}-20261111`.

Reproduction runner, configure/build/CTest/install logs and source/profile copies
are under `E:/Experiments/core-c5-bounded-delivery-20261111`.

The two replayed historical checkpoint documents retain their original hashes,
branch context and observations. Their prior sanitizer/resource results are not
new evidence for this identity. No fresh sanitizer, Win32, MCU timing/stack/heap,
device execution, Runtime adoption or remote CI result is claimed. This local
slice has not been pushed and no PR was created. No shared board was changed.
