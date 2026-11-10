# C5 private intrinsic source queries

Reviewed dirty candidate `codex/c5-intrinsic-query` over
`a0067eebde0ffe677e6e38acdd4c75da8bc78b50` on 2026-11-10.
The final normalized source manifest has 145 inputs and source hash
`0884d8dfc0526d4450225476d9a5cfa68960045ddc3be53468d9a17dc5a466aa`.
The revised evidence directory is `E:/Experiments/c5-intrinsic-review-20261110-final-v2`.
The earlier evidence directory without `-v2` remains frozen.

## Implementation and admission

The two synchronous private entry points live in `src/intrinsic_source_query.h`
and `src/intrinsic_source_query_height.h`. Both consume immutable LayoutBox/Node
source and Core TextSystem measurements. Dimensions use 1/1000-pixel units.
Only Complete publishes dimensions. Every other status retains the -1 sentinel.

Width returns the source min-content/max-content border-box widths for one
ordinary horizontal Block. Root horizontal padding and borders contribute.
Root authored sizing constraints remain the future consumer's responsibility.
Neutral inline and block descendants, br/wbr, ASCII case transforms,
overflow-wrap:anywhere, ASCII punctuation and collapsed CSS whitespace are
admitted. Uniform-font simple Han runs also admit per-ideograph opportunities,
neutral partitions, literal spaces, br and anywhere wrapping.

Height returns the border-box block size at an explicit content inline size.
Its initial profile admits uniform ASCII inline/block source with the same
bounded break handling. Explicit line-height is converted from the style's
pixel value. Normal line-height comes from Core Text metrics. Vertical root
padding and borders contribute and root margins are excluded.

Width explicitly requires CyclicSameAxis percentage provenance. Neutral Block
descendant width/min-width/max-width percentages contribute using cyclic
intrinsic semantics. Other provenance modes and cross-axis percentages refuse.
The current seven-slot computed-size representation does not encode mixed calc.
No alternate storage representation was added for this candidate.

Mixed scripts, combining sequences, Han punctuation and Han LF/CR/FF segment
breaks remain Unsupported. The latter need contextual CSS segment-break
processing and were absent from the recorded Han fixture profile. ASCII
SP/TAB/LF/CR/FF collapse. Vertical tab refuses. Replaced/native elements, mixed
metrics, unmodeled formatting contexts and generated content refuse.

## Confirmed review fixes

- Context checks borrow attributes and walk ancestors within depth/work bounds.
  Cyclic parent links refuse through the same bounded path.
- Height now charges line-height discovery, whitespace backscans over break
  markers, trailing trim, space search and line scanning to the shared work
  budget. Core Text measurement and named-family resolution also share it.
- Descendant height indent, list-marker and aspect-ratio cases, plus root height
  aspect ratios, now refuse instead of publishing incomplete dimensions.
- Width rejects malformed root sizing sentinels. Han segment breaks and
  vertical tab receive explicit refusals instead of incorrect space widths.
- Root review found a normal-entry fractional-percentage admission gap.
  HtmlParser, StyleResolver and LayoutEngine produce retained Percent 37500,
  scalar -1 and rounded percentage proxy 38 for 37.5%. The original query
  incorrectly required proxy -1 and returned Unsupported. Admission now
  requires the actual rounded proxy. Real-pipeline regressions cover width,
  min-width and max-width across inline and nested-block partitions, plus
  absent/conflicting percentage proxies and conflicting pixel scalars.
- Scratch new[] failures are caught inside both noexcept queries. A regression
  replaces new[] in the test executable and forces each entry point to fail.

The regression suite also covers exact/one-below box, source, measurement,
scratch and work budgets, named-family work, ancestor attributes, repeated
wbr/whitespace scans, cancellation, source immutability, missing glyphs,
partitioned punctuation, Han partitions and percentage provenance.
The before-fix failing regression output is retained as
`c5-intrinsic-review-v2-reproduce-ctest.log`. Another regression verifies that
all seven original dimension slots reject the unsupported third SizeKind and
preserve prior pixel precision. The positioned branch has not been joined in
this worktree. Root will rerun the integration matrix after that join.

## Bounds and measured host resources

| Resource | Default | Hard ceiling |
| --- | ---: | ---: |
| Layout boxes visited | 256 | 4096 |
| Layout depth and per-node ancestor depth | 32 | 64 |
| Raw source bytes and normalized marker-stream bytes, each | 8192 | 65536 |
| Text measurements | 512 | 4096 |
| Query work units | 65536 | 1048576 |
| Simultaneously live dynamic stream payload bytes | 32768 | 65536 |

Scratch growth checks old capacity plus proposed new capacity before allocation.
Copying the old stream is charged to work. Input/font allocations and allocator
metadata are borrowed or external to the scratch payload limit. Traversal uses
65 fixed frames totaling 2080 bytes on this x64 host. It does not recurse with
source depth. Cancellation is polled while consuming traversal, normalization,
scanning and measurement work. One Core Text measurement remains synchronously
bounded by the remaining work budget.

Clang 22.1.8 x64 MSVC-target /O2 /MD /EHsc stack-usage output gives the following
individual function frames. These are compiler frame measurements, separate
from an end-to-end target stack watermark.

| Function | Bytes |
| --- | ---: |
| Width query entry | 264 |
| Height query entry | 328 |
| Width/height collect_source, each | 2216 |
| Width measure_range | 296 |
| Height measure_wrapped_lines | 360 |
| TextSystem process | 888 |
| TextSystem candidates | 216 |
| TextSystem walk | 312 |

The `.su` files and compiler command are retained in the evidence directory.
ESP32 timing, heap peaks and whole-call stack watermarks remain unmeasured.

## Exact final validation

All four existing mutable candidate build trees were rebuilt after the final
production/CMake edits. Each generated manifest matches the hash above.

| Gate | Build under E:/Builds | Result |
| --- | --- | ---: |
| Full Release, clang-cl | c5-intrinsic-full | 22/22 |
| Optional feature families disabled, clang++ | c5-intrinsic-cropped | 21/21 |
| AddressSanitizer, clang++ | c5-intrinsic-asan | 22/22 |
| UndefinedBehaviorSanitizer trap mode, clang++ | c5-intrinsic-ubsan | 22/22 |
| Installed full TextSystem consumer | c5-intrinsic-review-full-text-consumer | 1/1 |
| Installed cropped TextSystem consumer | c5-intrinsic-review-cropped-text-consumer | 1/1 |

The package consumers use only the installed public headers and library.
Both install manifests match the final source identity. `git diff --check`
passes. Saved CMake caches, build/CTest logs, source manifests and normalized
test hashes identify these runs. The source manifest excludes tests and docs,
so their individual hashes are retained separately with the evidence.

## Browser reference evidence

The three fixtures in `E:/Temp/c5-intrinsic-oracle` exactly match historical
fixtures under
`E:/Workspaces/Codex/2026-11-07-html-dimension-hints/docs/evaluation`.
Their SHA-256 values establish the correspondence.

| Current fixture | Historical fixture | SHA-256 |
| --- | --- | --- |
| width.html | 2026-11-08-intrinsic-source-query-oracle.html | 03fc25902650c1fff783e02ad48195aa7e1d38f4eacf47838124f02ec7c82316 |
| han.html | 2026-11-08-han-intrinsic-oracle.html | 573f88d3fac87c3929ad8ecd202a0ff1aced66b71e0ecd6eb81001683c48fdf1 |
| breaks.html | 2026-11-09-empty-break-height-oracle.html | e77b0ec6347f3617807c706ab225f51c41c07b57cf7c0f6fea5f74eb0e3eb3a8 |

Historical `2026-11-08-intrinsic-source-query-oracle-results.json` retains
17 measured user-run Chromium cases and cyclic percentages, plus Edge 156
supplements for ASCII anywhere, transforms, question-mark context and the
32-character punctuation scan. The punctuation record qualifies its tested
boundary forms. Historical `2026-11-08-han-intrinsic-oracle-results.json`
retains Chrome 157, Edge 156 and Firefox 155 measurements for ideographs,
partitions, spaces, br and anywhere. The two expanded break-height captures in
`E:/Temp/empty-break-oracle-20261109-{chrome,edge}-expanded` each retain 21 probe
measurements and 12 passing grouped assertions, plus browser DOM and command
records. Copies and hashes of these historical observations are included in
the new evidence directory under `reference-history`.

The previous handoff also reported fresh CUA assertions. A saved raw capture
for that fresh run remains unavailable. The historical observations above
provide the recorded browser evidence for the matching fixtures. This review
did not rerun browser operations.

The queries remain isolated from Flex/Grid consumers. Layout, Style and
computed-size storage files are unchanged. Candidate changes are uncommitted
for root review.
