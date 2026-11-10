# C5 positioned precision review, 2026-11-10

Candidate `codex/c5-positioned-precision` is based on `a0067eebde0ffe677e6e38acdd4c75da8bc78b50`.
The reviewed source manifest is `520dc484aef3984e7b72b02ee77c50e153eeb71e2230cd09e527f82bc72fc355`,
with 139 inputs. Changes remain available for parent review.

## Reviewed behavior

The shared immutable computed-size owner now has eleven slots. Insets retain
signed Q1000 pixels, percentages, or bounded two-term length/percentage sums.
Coefficients are bounded to magnitude 1000% and pixel terms to INT_MAX.
Invalid declarations and allocation failures preserve prior retained values.
An initially empty integral-pixel inset keeps storage allocation-free.

The final positioned traversal uses measured Q1000 content geometry. Absolute
children use the nearest positioned ancestor's padding box across static
wrappers. Fixed children use both viewport axes. Relative children use parent
content dimensions, with indefinite-height percentage offsets resolving to
auto. The traversal preserves static block-flow origins, resolves opposite-edge
stretching and min/max constraints, and restores temporary style proxies.
Geometry participates in journal capture and rollback alongside rectangles and
allocated text caches.

The inherited candidate passed its initial suites. Review found that the mixed
setter admitted dimension slots whose existing consumers cannot read that kind.
It now refuses all seven original slots atomically without allocation. The
legacy getter also leaves both outputs intact when refusing a mixed inset.
Review added independent
numeric rectangle checks to the paint/hit/damage fixture and actual journal
allocation measurement. Journal tests now inject failure into both the traversal
scratch allocation and slot allocation, verify pre-mutation refusal, and retry
successfully. A table-branch indentation defect was corrected. The existing
layout expectation change deliberately replaces a content-box absolute basis
with the positioned parent's padding box.

## Final host verification

Windows x64, Clang 22.1.8, Ninja, Release. Every command began by sourcing
`E:/Start-Development.ps1` and selecting this candidate tree.

| Build directory under E:/Builds | Result |
| --- | --- |
| c5-positioned-full | 22/22 passed |
| c5-positioned-cropped | 21/21 passed |
| c5-positioned-asan | 22/22 passed |
| c5-positioned-ubsan | 22/22 passed |

Cropped disables Canvas2D, ModernPaint, Flex/Grid and AdvancedForms. ASan uses
`-fsanitize=address -fno-omit-frame-pointer`. UBSan uses
`-fsanitize=undefined -fsanitize-trap=all`. The matching LLVM runtime directory
is on PATH through the development entry script. Test details remain in each
build's `Testing/Temporary/LastTest.log`. All four manifests match the source
hash above. These are host memory-safety checks. Device memory and leak-sanitizer
qualification require separate evidence.

The aggregate suite retains the F4 fixed-table cases, including relative table
subtree offsets and unchanged following flow. F5 rounded coverage, typed display
commands, multilayer paint and existing aggregate paint regressions pass in each
available feature profile.

The positioning fixture checks exact initial, inset-mutated and ancestor-shifted
child/grandchild rectangles. It checks old/new hit snapshots, previous/current
damage coverage, pixel colors, and byte-for-byte retained dirty replay against
complete repaint. Signed edges, relative precedence, indefinite height, nested
containing blocks, viewport/ancestor resize, auto/invalid replacement, fractional
origins, bounded arithmetic and layout resource limits are also covered.

Nested opposite-edge work is measured through the normal text-provider entry
point. Absolute chains at depths 8, 16, 32 and 64 produce 16, 32, 64 and 128 text
measurements. Each positioned node measures its in-flow content at most twice
in this fixture. Out-of-flow descendants are skipped during those layouts and
visited once by the final traversal, so nesting does not multiply their
remeasurement. Relative subtree shifts can still visit descendants at each
relative ancestor, bounded by the existing depth and box budgets.

`git -c core.whitespace=cr-at-eol diff --check` passes.

### MSVC Debug allocation-injection follow-up

Fresh Visual Studio 2022 BuildTools MSVC 19.41.34120 x64 Debug build at
`E:/Builds/c5-positioned-msvc-debug` reproduced journal termination with
`bad allocation`. The local checked vector allocates an iterator proxy in its
noexcept default constructor before the guarded reserve. Test injection now
allows that one metadata allocation and persistently fails the intended reserve
or slot storage instead. Release retains its original persistent-failure
injection. Allocation measurement selects the largest real allocation, excluding
later tiny checked-container proxies.

The same Debug run found that the storage test incorrectly counted all Style
string/vector iterator-proxy allocations as precision-owner allocations. It now
checks an allocation-free precision-owner copy independently and verifies the
copied Style retains the same payload values. Product code is unchanged by
these fixes. Targeted inset, journal, storage and observer tests pass 4/4 under
MSVC Debug, MSVC Release and Clang Release after the corrections. Retained MSVC
logs are `E:/Temp/c5-positioned-msvc-debug-20261110.log` and
`E:/Temp/c5-positioned-msvc-release-20261110.log`. Debug journal allocation is
392 B per slot, compared with the 360 B prior member layout, again a 32 B delta.

## Actual allocation and layout growth

| Windows x64 value | Base | Candidate | Delta |
| --- | ---: | ---: | ---: |
| Computed-size owner | 8 B | 8 B | 0 B |
| Style | 920 B | 920 B | 0 B |
| LayoutBox | 1272 B | 1304 B | 32 B |
| Layout content geometry | absent | 32 B | 32 B |
| Computed payload allocation request | 128 B | 192 B | 64 B |
| Journal slot | 320 B | 352 B | 32 B |

The payload values are intercepted allocation requests, excluding allocator
headers and capacity rounding. Base storage and LayoutBox were compiled from
`git show a0067eeb` into `E:/Temp/c5-positioned-baseline-storage-20261110`.
The candidate storage test measures the first retained allocation. The journal
test intercepts a real single-slot allocation of 352 B and checks it against
the member-layout measurement.

At the default 4096-box ceiling the box metadata adds 128 KiB. Capturing 4096
journal slots adds another 128 KiB, with total slot storage 1408 KiB and existing
pointer scratch 32 KiB. Each distinct pre-existing payload grows by 64 B, or
256 KiB for 4096 payloads. A newly retained inset needs a 192 B payload when no
payload exists. Copies share it until mutation. The sparse fixture has eight
boxes and three retained owners. These figures describe allocation requests
and structural costs. They do not measure a target peak heap.

## Historical independent browser evidence

The immutable fixture is
`E:/Workspaces/Codex/2026-11-07-html-dimension-hints/docs/evaluation/2026-11-09-inset-containing-block-oracle.html`.
Its SHA256 is `c157fe92b043f16b86c4eaa8c5b8f05ca08bcbd9a5a467c58cd082f85cc425b9`.
Raw Chrome157 observations at a 776x501 viewport remain in
`E:/Temp/inset-browser-oracle-20261109-chrome/results.json`, SHA256
`e99dd43ecbc00e49c8f248de71ecc520cfec2c55bc1e065a397c44b47b054486`.
All ten recorded assertions are true. Inspection confirms the fixture's HTML,
CSS and dimensions match the ten Core reference cases, with browser fractional
edges projected using Core's integer geometry contract.

Fresh browser execution remains an evidence gap. The earlier local HTTP and
headless Edge attempt was rejected by automatic approval review as blocked by
policy. This continuation inspected the historical bytes and performed no
browser retry. The historical cases establish geometry expectations. They do
not establish fresh browser pixel parity for this source manifest.

## Parent acceptance

Parent-owned installed public API consumers passed1/1 for each full/cropped
prefix at source520dc484.../139. The checked-in consumer is
tests/package/positioned_precision_consumer and automatic CI runs it against
both freshly installed profiles with exact exported source/profile/manifest
checks. Local consumer builds are E:/Builds/c5-positioned-independent-full and
E:/Builds/c5-positioned-independent-cropped. It covers six frozen-reference
geometry cases, inset mutation, old/new layout hits, layer hit and painted pixels.
The document has an explicit viewport-sized extent for this bounded hit test.

An initial consumer with only out-of-flow content exposed an existing layout
hit-test limitation. hit_test_box prunes an entire subtree when the point is
outside the ancestor rectangle, including zero-height ancestors with visible
overflow. The installed consumer's explicit document extent isolates the
positioned contract; general overflow hit traversal remains separate work.

Review the dirty implementation and the measured persistent/journal growth,
then qualify the integrated installed consumer and Runtime package identity.
Fresh full and cropped installs are available at
`E:/Builds/c5-positioned-install-full` and
`E:/Builds/c5-positioned-install-cropped` for the parent-owned public consumer
gate. They preserve package version `0.9.0-dev` and engine ABI 4, with profiles
`render-core-default` and `render-core-minimal-no-forms-advanced` respectively.
The parent independently linked the public package consumer against both
installs and passed its full/cropped gates with the exact source identity.
A fresh browser gate still needs an approved execution path. Target resource
and device qualification remain open. Scope is ordinary block containing boxes.
Transformed/fragmented inline containing blocks, general shrink-to-fit behavior,
and broader table/flex/replaced-element positioning need separate acceptance.

### Inherited layout hit-test boundary

The parent's initial public consumer used a zero-height root with only an
absolute child. Layout-based HitTester prunes a root whose rectangle does not
contain the query before it traverses overflowing children. Giving the fixture
an explicit document viewport height makes that consumer exercise its intended
bounded positioning/hit contract. The pruning implementation in `src/hit_test.cpp`
is identical to base `a0067eeb`. Absolute children already consume no normal-flow
height at that base. Overflow-child hit traversal under a zero-height root is a
separate inherited behavior issue for future acceptance.
